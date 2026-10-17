# PLAN - the FreeJ2ME-Plus core

The design log: what was decided, why, and what was measured. Newest
sections last.

## 2026-10-17 - why this core, and what it is

Chimera issue #297 asks for J2ME (and DoJa, i-appli) games.
[FreeJ2ME-Plus](https://github.com/TASEmulators/freej2me-plus) is the
emulator the TASVideos community keeps for them; it is written in Java. An
earlier plan for KEmulator (2026-09-19) concluded that a core could not host
a Java VM: that was before miniBox's machine spec 3 gave guests timed waits
on the sandbox's own clock.

A spike (in a scratch folder, 2026-10-17) showed that OpenJDK 21 built as
Zero - HotSpot with only its interpreter, no compiler and no generated
machine code - runs in the sandbox, linked statically, and that a Java
program run there is the same in every run, through savestates and in a new
process. It found two things in miniBox, one of them a bug:

- **Host values in the vector registers.** miniBox left xmm0-15 as the host
  had them at every entry into the guest; a guest function that takes
  variable arguments saves them into its memory, so the states of two
  processes differed. Fixed in miniBox c0aad51 (and Chimera 1dae12a), with
  tests seen failing on Linux and Windows without the fix.
- **The order of threads woken together** is not saved in a state; this was
  looked at and is not a bug: every waiter due at a moment is woken before
  any runs, and the next to run is chosen by thread number.

## 2026-10-17 - the licence (user-decided, 2026-10-17)

HotSpot is under the GPL version 2 only, without the Classpath Exception
(the exception covers the class library). FreeJ2ME-Plus is under the GPL
version 3 or later. The two licences are not compatible for one program in
general. The design keeps the boundary the java launcher has: the C program
starts the VM and calls Java through JNI, and nothing in Java calls back
into our C (the front end declares no native methods). The owner accepted
this basis. The repository's own C is MIT; the front end in `java/` is
GPL-3.0-or-later, as it runs with FreeJ2ME-Plus.

## 2026-10-17 - the repository (user-decided, 2026-10-17)

Created locally as `chimera-core-freej2me`; making it public, and the roster
row, wait for the owner's word.

## 2026-10-17 - how the core is put together

- **The VM.** OpenJDK jdk21u at tag jdk-21.0.12.1-ga, Zero, with the serial
  collector and nothing else that is optional (no JIT, no CDS, no JVMTI, no
  management). `build-jdk.sh` builds it for the host with upstream's own
  build; `build-guest.py` compiles the same 1076 files again from the build's
  `compile_commands.json` with the guest's flags: `-DSTATIC_BUILD` (every
  library linked in), library-based thread-locals (the guest has no
  `__thread`), musl. HotSpot finds its libraries linked in through
  `JNI_OnLoad_<name>` (generated for those that have none) and a table of
  every native function that `dlsym` answers from.
- **Two patches to OpenJDK** (both inside `#ifdef CHIMERA_CORE`):
  0001 lets HotSpot take a library as present though no file of it exists;
  0002 makes Zero pay for the code it runs - every 1000 backward branches or
  method entries cost 20 microseconds of the machine's clock (a sleep), and a
  backward branch checks for a safepoint. Without it, a thread spinning on a
  variable never lets the thread that would set it run, and never lets a
  timed wait expire: the spike hung on exactly that.
- **The VM's thread.** `driver.c` creates the VM on a thread of its own (on
  the first thread HotSpot looks for the process's stack in /proc) and asks
  it for one thing at a time: start the game, run a frame. The VM thread
  calls `chimera.Frontend` through JNI.
- **Files.** The sandbox's file table is outside the guest and not in a
  state; a VM keeps files open across frames. So every file call is routed
  (`--wrap`) to `memfs.c`, which serves them from guest memory. The
  runtime's files (37 files, 66 MB: the class library, fonts, the sound
  bank, the two jars) are packed and built into the core's image as
  read-only data: in no savestate, and the class library, which the VM maps
  whole, is mapped as the pack's own bytes with nothing copied. The game and
  the settings are copied in by Init. What the game writes (FreeJ2ME's
  configuration, record stores) lives in guest memory and in the state.
- **The VM starts in Init**, before the sandbox seals the machine. Its
  start-up and the game's loading are then part of the sealed image, not of
  every state. In the spike, which started the VM on the first frame, a
  state was about 100 MB; here it is 14 MB after 300 frames of the test
  MIDlet.
- **The front end** (`java/chimera/Frontend.java`) is modelled on
  FreeJ2ME-Plus's libretro front end: the same settings set in the same
  order (load, settings, run), the same key events (a held key repeats once
  a frame), a frame of 1/60 s of sleep, then the picture copied from
  FreeJ2ME's screen buffer.
- **Sound.** The JDK's sound devices are not built (no libjsound); the front
  end's jar registers a mixer of its own (`chimera.sound`), the only one Java
  Sound finds. Every line a program opens - Gervill, the JDK's MIDI
  synthesizer, opens one per synthesizer; FreeJ2ME plays WAV sound as clips -
  plays into one stereo bus at 44100 Hz, which each frame takes 735 samples
  of. A line's writer waits while its buffer (at most three frames) is full,
  so a synthesizer renders exactly as far ahead as that, in every run. Lines
  of other rates and sizes are converted as they play (linear steps).
- **The sound bank.** Gervill makes its bank the first time it opens when
  none is installed - 50 seconds in an interpreter, and the result is
  written into the user's home folder. The build makes it once, with the
  built VM, and installs it in the runtime's `lib/audio`, where Gervill looks
  first. Its maker draws random phases from `Math.random()`, which nothing
  seeds: two builds made two different banks. `MakeSoundbank.java` seeds
  that generator first; two builds now make the same bank, byte for byte.
- **Fonts.** FreeJ2ME draws text with Java's logical fonts. The runtime has
  the DejaVu fonts and a font table (`-Dsun.awt.fontconfig`, which needs no
  fontconfig library; paths through `$JRE_LIB_FONTS`). The build checks the
  fonts against `fonts.sha256`.
- **Settings** are FreeJ2ME's own: Screen Size, Phone Keys, Frame Limit,
  Sound, MIDI Synthesizers, and Text Encoding, which is the VM's
  `file.encoding` (a VM cannot change it once started; FreeJ2ME restarts its
  process for it, which a core cannot, so a game that asks for another
  encoding than the one chosen is refused with a message saying which).
- **Controls**: the phone's 20 keys in FreeJ2ME's numbering, and the touch
  screen as Touch and Touch X/Y (0 to 65535, a fraction of the picture).
  `InputWasRead` answers 1 every frame: a J2ME game is told of a key when it
  changes, so there is no frame that "did not read" its input to report.

## 2026-10-17 - measured

On this machine (WSL2, 20 threads), with the test MIDlet:

- Build from scratch: OpenJDK 4 minutes; the Java half 80 seconds the first
  time (45 of them the sound bank), 35 after; the guest a few minutes the
  first time, 40 seconds after.
- `core.wbx` 103 MB; the package 36.6 MB; packaging is deterministic.
- Start-up (host, Init, VM, game) 2.0 seconds. A frame 4.6 ms with four MIDI
  synthesizers, 2.8 ms with one (1200 frames). The host process uses about
  270 MB.
- A state after 300 frames: 14 MB.
- The test script (`run-gate.sh`): 17 passed, 0 failed, 0 skipped, with
  CHIMERA_ROOT set, once committed. With the games and `box:resume` added
  later the same day: 23 passed, 0 failed, 0 skipped.
- Chimera's contract tests on the package: 20 passed, 0 failed.

The sound was checked by measuring it: the dominant pitch of the test
MIDlet's tune, a quarter of a second at a time, walks C4 (262 Hz) to C5
(523 Hz) and back, and the WAV beep shows at the frame it is pressed.

## 2026-10-17 - the tests, and how each was seen to fail

One build broke on purpose: the keys and the touch never reached the
machine, the sound was never mixed, the Screen Size setting was ignored, a
value kept in memory that is in no state was drawn into the picture, the
message for a missing game was changed, and two buttons were swapped in
`waterbox.config`; a source was touched after the build, and the package
was left from before the break. The test script then failed `package:fresh`,
`package:controls`, `j2me:keys`, `j2me:touch`, `j2me:sound`, `j2me:effects`,
`j2me:screen-size`, `box:session`, `box:rewind`, `refused:no-game`,
`native:runs` and `engine:boot`; `source:patched` failed as there was no
commit yet.

Not seen to fail: `box:again` and `box:rerecord`. Nothing in the core alone
makes two runs differ (the sandbox gives a guest nothing of the host's),
and a save and a load of the same moment leave everything the same - a
value in no state survives them too. miniBox's host from before c0aad51,
which made the spike's runs differ, no longer makes this core's differ
either: the VM's start-up, where the spike's leaked values landed, is now
before the seal. Their comparison is the one `box:session` failed with.

## 2026-10-17 - Windows, and Chimera's own runner

The package in Chimera's headless runner (`chimera-run`, Chimera 1dae12a
with miniBox c0aad51) plays a project of the test MIDlet with presses to
the same picture as `run-wbx` (the project's row N is the runner's frame
N + 1), on Linux and on Windows 11 (the Windows build of the same runner,
run through WSL from %TEMP%\chimera-freej2me): the pictures at frames 120
and 299 are identical between the two systems, and so is the picture after
`chimera-run --rerecord` (a state saved and loaded around every frame).

`chimera-run --state` with a state saved at frame 150 at first seemed to
follow the straight run for 60 frames and then depart from it, on Linux and
on Windows. It does not: `--state` makes the loaded machine the one the
movie starts from, so the runner plays the movie from its first row again,
and the test project's Right, held from row 59, was pressed a second time
59 frames after the load - where the run departed (the score the test
MIDlet saved differed by two steps of the square). With a project of no
presses for the rest of the run, the frames after the load are the
straight run's, on both systems, with a state saved on either: the test
`engine:resume`. The first form of that test, which replayed the presses,
is the case of it failing.

## 2026-10-17 - the first games

The owner gave five games (kept outside the repository, in
`tests/games-local`; the test script names each by SHA-1 and says SKIP
without it). All five start with the default settings (240x320, Nokia keys)
and draw correctly to their menus:

- **Angry Birds** (Rovio, MIDP 2.0): through its logos, language and menu
  to level 1, with its music. In the level two small rectangles are drawn
  plain white beside the slingshot; the native runner draws them the same,
  so it is not the sandbox: FreeJ2ME, or this runtime. Not traced.
- **Baldur's Gate** (Sorrent, MIDP 1.0): its loading form and title menu.
- **Cars 2** (Disney, MIDP 2.0): logos, its sound question, the title and
  the player name screen, with sound.
- **Doom RPG** (JAMDAT, MIDP 2.0; this file is a Russian build, made for
  176x208, which it draws in the corner of 240x320): logos, its sound
  question, the menu, loading and the story pages, with music from the
  moment sound is chosen. Two runs, savestates around every one of 1800
  frames (26 seconds), a new host from frame 900 and another process from
  frame 900 all end in the same 27 MB state with the same 60 reports.
- **The Game of Life** (Glu, MIDP 2.0): its logos and licence screens.

Speed: 900 frames in 7 to 10 seconds for each (8 to 11 ms a frame, on this
machine), so faster than the phone ran them. No game was played for long:
what happens deeper in each is not known.

The game: tests were seen to fail with a core whose picture never changed
after the first frame; `box:resume` with a value kept in no state drawn
into the picture.

## 2026-10-17 - Japanese text, save data, memory (owner: "do the rest")

- **Japanese text.** FreeJ2ME draws with Java's logical fonts. The font
  table now has IPAex Gothic (Debian's fonts-ipaexfont-gothic, the IPA Font
  License, carried unmodified, checked by SHA-256) as the fallback for
  characters DejaVu lacks; DejaVu stays first, so Latin text keeps its look
  and its measurements. Measured on the five games: four give the same
  pictures and sound in every report as before; Angry Birds took one press
  a report earlier (a timing shift: the machine changed) and is the same
  again from frame 750. No file was needed from the owner. The test
  `j2me:japanese` checks the MIDlet's Japanese line against the pixels IPAex
  Gothic draws; built without the fallback, it fails.
- **Save data.** FreeJ2ME keeps a record store as a description,
  `rms/<suite>/<name>.rms`, and a file for each record. Export Save Data
  lists every file under `rms/` that the game wrote (from memfs), so
  several files make a zip; the "Save data" slot takes that zip back, and
  the front end unpacks it into `rms/<suite>/` after the game is loaded and
  before it runs (in Init, so in the sealed image). A zip with files of
  another game, or files that are not FreeJ2ME's, is refused with a message.
  One `.rms` file alone is not accepted: it is a store without its records.
- **Memory.** One domain, "Java Heap": the serial collector's reserved range,
  64 MB, which `heap.cpp` (compiled with HotSpot's own flags) reads from the
  VM. The VM now commits its whole heap from the start (`-Xms64m`, was 16),
  so every byte of it is memory the machine has. RAM Search finds the test
  MIDlet's square: exactly one place goes from 100 to 151 when Right is
  held. The collector moves objects, so an address holds its value until
  the next collection moves the object; static fields live in the heap
  too. Reading the memory leaves the machine as it was (`memory:looking`).

Seen to fail, each once: `save:export` with an export that listed nothing,
`save:import` with the slot's zip ignored (it passed on the first try with
both broken at once, as two empty folders match; it now also asks for the
store at frame 20, before the MIDlet's own first save), `save:refused` with
the zip ignored, `memory:heap` with the domain half its size,
`memory:looking` with a read that changed the picture.

The test script: 30 passed, 0 failed, 0 skipped (games and CHIMERA_ROOT).

## 2026-10-17 - in Chimera's window

`tests/run-frontend.sh` (from chimera-core-tic80) runs the package in
Chimera's window under Mono on an Xvfb display of its own, through a Lua
script: the test MIDlet project, 300 frames with Right, Fire and Num 5
pressed, leaves a Java heap whose SHA-256 is the headless runner's
(`window:midlet`); the key bindings the package ships are the ones Chimera
adopts (`window:keys`); Doom RPG, 1500 frames through its sound question
into the game, leaves the headless runner's heap (`window:doom`, SKIP
without the file). 3 passed, 0 failed, 0 skipped, 47 seconds. Seen to fail:
`window:keys` against changed bindings, `window:midlet` and `window:doom`
against a headless run one frame short.

On Windows 11, Chimera's window (the same build, `--headless --chromeless`,
from %TEMP%\chimera-freej2me) played the same project; its machine at the
frame the script stopped on is the Linux headless runner's at that frame,
byte for byte (the Java heap's SHA-256). There the window had run two
frames before the Lua script began (302 for the script's 300); the movie
drove those frames too, so this is when the script attaches, not the core.

## 2026-10-17 - published (user-decided, 2026-10-17)

The owner: "Publish. The usual: readme, tools/ page, discord hook,
nightly/dev releases". The repository is public at
ToolAssisted-run/chimera-core-freej2me; CI builds it from a fresh clone
(OpenJDK included) and publishes through Chimera's publish-core workflow.
Chimera issue #297 asked for two cores, openDoJa and FreeJ2ME-Plus; this
is the second, and no DoJa game has been tried, so the issue stays open.

## Not done

- **Games** were run to their menus and a little further (above); none
  was played through.
- **DoJa (i-appli)** games: not tried, for want of one. Their scratchpad
  (DoJa's save data) is not in the export yet.
- CI has not run (the repository is not public).
- At start-up something in the JDK asks for a socket (IPv4, stream), which
  the sandbox refuses and says so once; not traced yet.
- FreeJ2ME's rotation and its compatibility and speed settings are not
  offered yet.
