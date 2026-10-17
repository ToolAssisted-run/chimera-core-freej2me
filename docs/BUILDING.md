# Building

This core is built on Linux (x86-64). The result, `core.wbx`, is one file
that runs in Chimera on Linux and on Windows.

## What is needed

- GCC 13 or newer (C and C++), make, git, python3, zip, and for libffi
  autoconf, automake, libtool and libltdl-dev (its `autogen.sh` uses a macro
  of libltdl's, `LT_SYS_SYMBOL_USCORE`).
- A JDK 21 to build OpenJDK with (the "boot JDK"; Debian's
  openjdk-21-jdk-headless). `BOOT_JDK` names it when it is not the one on
  the PATH.
- The headers OpenJDK's configure asks for even for a build without a
  display: CUPS, fontconfig, X11 (with Xext, Xrender, Xrandr, Xtst, Xt) and
  ALSA (Debian's libcups2-dev, libfontconfig-dev, libx11-dev, libxext-dev,
  libxrender-dev, libxrandr-dev, libxtst-dev, libxt-dev, libasound2-dev).
  Where they cannot be installed, unpack the packages with `dpkg-deb -x`
  into one folder and name it in `JDK_EXTRA_ROOT`; `zip` can come the same
  way.
- The DejaVu fonts, version 2.37 (Debian's fonts-dejavu-core and
  fonts-dejavu-extra). `FONT_DIR` names their folder when it is not
  `/usr/share/fonts/truetype/dejavu`. The build checks them against
  `waterbox/fonts.sha256`: the picture depends on them.
- IPAex Gothic, the Japanese font (Debian's fonts-ipaexfont-gothic,
  `ipaexg.ttf`). `JP_FONT_DIR` names its folder when it is not
  `/usr/share/fonts/opentype/ipaexfont-gothic` (the package unpacked with
  `dpkg-deb -x` will do); checked against `waterbox/fonts-ja.sha256`.
- The kernel's headers (Debian's linux-libc-dev; `KERNEL_INCLUDE` when they
  are not under `/usr/include`).
- A checkout of Chimera with miniBox built, with its C++ guest toolchain
  (Chimera's `AGENTS.md`, "Build"). Below, `<miniBox>` is its
  `extern/chimera-common-minibox` folder. Its host must be commit c0aad51
  or newer: older hosts leave host values in the vector registers, which a
  guest compiled as this one is can save into its memory.

## The sources

```sh
git submodule update --init
```

OpenJDK (`extern/jdk21u`) is a shallow submodule: about 150 MB. A change to
any submodule is a numbered patch in `patches/<submodule>/`;
`waterbox/apply-patches.sh` lays them over the submodules (the build runs
it; it does nothing to a tree that has them). To make a patch, edit the
files, build and test, add the file to the list in `waterbox/make-patches.sh`
if it is new, and run that script.

## The steps

1. **OpenJDK for this machine** (`waterbox/build-jdk.sh [-j JOBS]`, about
   4 minutes): upstream's own build of the Zero VM, headless, into
   `build/jdk`. It is the native reference, and its `compile_commands.json`
   says how each file is compiled.
2. **The Java half** (`waterbox/build-java.sh`): FreeJ2ME-Plus and the
   front end compiled with the boot JDK's javac (`--release 8`, as
   FreeJ2ME-Plus is) into `build/java/*.jar`, and the runtime in `build/rt`:
   OpenJDK's modules java.base, java.desktop, java.prefs and jdk.charsets
   (jlink), the DejaVu fonts, IPAex Gothic and their table
   (`make-fontconfig.py`), and
   the MIDI synthesizer's sound bank. The sound bank is made once, by the
   built VM (45 seconds), and kept in `build/soundbank`.
3. **The guest** (`waterbox/build-guest.sh -m <miniBox>`): libffi from its
   git tree; HotSpot and the JDK's native libraries compiled again from the
   same compile commands with the guest toolchain's flags
   (`build-guest.py`; 1076 files, and `heap.cpp`, ours, which tells the
   core where the Java heap is); the runtime's files packed
   (`make-pack.py`) and built into the core (`pack.S`); the link, with
   every file call routed to `memfs.c`; then `run-wbx`, the sandbox runner.
4. **The native runner** (`waterbox/build-native.sh -m <miniBox>`):
   `core.c` and `driver.c` over the host's own Zero VM, as
   `build/bin/run-native`.
5. **The test MIDlet** (`tests/midlet/build.sh`).
6. **The package** (`waterbox/build-package.sh -r <chimera checkout>`): runs
   whatever of steps 1 to 3 is missing, then writes
   `build/Cores/freej2me.chimeraCore` in the Chimera checkout. Packaging
   twice gives the same file.

## The runners

`build/bin/run-wbx <core.wbx> <folder> [options]` and
`build/bin/run-native <folder> [options]` run a game. The folder holds what
a project would mount: the game under its name, `slots`
(`{"game":["name.jar"]}`) and `settings` (`{}`, or settings by name). The
options are in `waterbox/gate-harness.h`: `--frames`, `--report`,
`--press INDEX:FIRST:COUNT`, `--axis`, `--vid-out` (a name with `%ld` in it
gets a picture at every report), `--aud-out`, and for `run-wbx`
`--rerecord`, `--session`, `--rewind`, `--state-hash`, `--save-at N FILE`,
`--resume FILE N` and `--dump-baseline FILE` (`run-wbx.c` says what each
does). `run-native` works inside the game's folder: give it absolute paths
for its output.

## The tests

```sh
CHIMERA_ROOT=<chimera checkout> bash waterbox/run-gate.sh
```

30 tests, about three and a half minutes. Without `CHIMERA_ROOT` the two that
need Chimera are skipped. Five run commercial games, which are never in the
repository: put the files in `tests/games-local` (ignored by git), or name
their folder in `GAMES_DIR`; each is named and checked by SHA-1 in the script,
and the test says SKIP where it is absent. Chimera's contract tests run on
the package too (Chimera's `AGENTS.md`).

## The window test

```sh
bash tests/run-frontend.sh --chimera-root <chimera checkout>
```

Runs the package in Chimera's own window (Chimera.exe under Mono, on an Xvfb
display of its own; needs mono and Xvfb, and Chimera's interface built):
the test MIDlet's Java heap after 300 frames against the headless
runner's, the key bindings Chimera adopts from the package, and Doom RPG
where `tests/games-local` has it. About a minute.

## Troubleshooting

- *configure says the machine is Windows* (WSL): `build-jdk.sh` passes
  `--build/--host/--target` for this; a configure run by hand needs them.
- *"Could not find required tool for ZIPEXE"*: install zip, or unpack it
  into `JDK_EXTRA_ROOT`.
- *"the DejaVu fonts ... are not the ones fonts.sha256 names"*: another
  version of the fonts is installed. The picture would change with them.
- *A change to `java/` seems to do nothing*: run `build-java.sh` and then
  `build-guest.sh`; the jars are packed into the core.
