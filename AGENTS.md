# AGENTS.md - FreeJ2ME-Plus core for Chimera

This repository builds a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns, that runs J2ME phone games. It produces one file,
`freej2me.chimeraCore`: a Java VM (OpenJDK 21's HotSpot built as Zero, its
interpreter-only form) with FreeJ2ME-Plus and a small front end, compiled
to run inside Chimera's sandbox (miniBox), and the declarations Chimera
reads (buttons, settings, file types).

Read [docs/BUILDING.md](docs/BUILDING.md) before building and
[docs/PLAN.md](docs/PLAN.md) before changing how the core works: it is the
design log, with what was decided, why, and what was measured.

## Layout

- `extern/jdk21u` - OpenJDK 21 (jdk21u, tag jdk-21.0.12.1-ga), a shallow
  submodule. `extern/freej2me-plus` - FreeJ2ME-Plus. `extern/libffi` -
  libffi 3.4.6, which Zero calls native code through. None is edited in
  place: a change is a numbered patch in `patches/<submodule>/`, laid over
  the submodule by `waterbox/apply-patches.sh` and written by
  `waterbox/make-patches.sh` (its list says which file is in which patch).
  Every change to OpenJDK is inside `#ifdef CHIMERA_CORE`.
- `java/` - the front end FreeJ2ME runs under (`chimera/Frontend.java`) and
  the machine's sound mixer (`chimera/sound/`). GPL-3.0-or-later.
- `waterbox/core.c` - the functions Chimera calls: reads the project,
  starts the VM, turns the controls into FreeJ2ME's keys, hands back the
  picture and the sound. `driver.c` runs the VM on a thread of its own, a
  frame at a time, through JNI. The same two files make the native runner.
- `waterbox/memfs.c` - the machine's files, served from guest memory;
  `guest-shims.c` - what HotSpot asks of the system that the sandbox does not
  answer; `pack.S` - the runtime's files built into the core; `heap.cpp` -
  where the Java heap is (the memory domain), compiled with HotSpot's flags.
- `waterbox/build-jdk.sh`, `build-java.sh`, `build-guest.sh` (with
  `build-guest.py`), `build-native.sh`, `build-package.sh` - the build, in
  that order. `run-gate.sh` - the test script.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - what the package declares.
- `waterbox/run-wbx.c`, `run-native.c`, `gate-harness.h` - the runners.
- `tests/midlet/` - the test MIDlet; `tests/make-project.py` writes a
  project, `tests/check-key-names.py` checks the default bindings,
  `tests/vid2png.py` turns a runner's picture into a PNG.
- `tests/games-local/` - commercial games for the `game:` tests, named by
  SHA-1 in `run-gate.sh`. Ignored by git: never committed, never uploaded
  to CI or attached to an issue.
- `build/` - every build output, ignored by git.

## Build and test

```sh
git submodule update --init
waterbox/build-jdk.sh
waterbox/build-java.sh
waterbox/build-guest.sh -m <miniBox>
waterbox/build-native.sh -m <miniBox>
tests/midlet/build.sh
waterbox/build-package.sh -r <chimera checkout>
CHIMERA_ROOT=<chimera checkout> bash waterbox/run-gate.sh
```

Run the test script before every commit that touches what it covers, and
say in the commit what ran, with the numbers. A new test is broken on
purpose once and seen to fail.

## Rules of the repository

- **One clock.** The game's threads run on the sandbox's own clock (miniBox
  machine spec 3): time moves when a thread sleeps or waits, and Zero pays
  for the code it runs (patch 0002). Nothing reads the host's clock. A frame
  is 1/60 s of that clock.
- **C calls Java, never the other way.** The front end has no native
  methods; the core calls it as the java launcher would. This is the
  licence boundary the owner accepted (docs/PLAN.md).
- **Every file the VM opens is memfs's.** The sandbox's own file table is
  not in a savestate. The runtime's files are built into the core (no copy
  in any state); the game and the settings are copied in by Init; what the
  game writes lives in guest memory.
- **The VM starts in Init**, before the sandbox seals the machine, so its
  start-up is in no savestate.
- **The controls are in two places that must agree**: the table in
  `core.c` and the `input` in `waterbox.config`. The test
  `package:controls` compares them.
- **Native and sandbox are not compared.** Natively the game's threads run on
  the host's clock. The sandbox runs are compared with each other.
- **Say what was not tried.** A game that was not run has not been shown to
  work.
- **Write in plain language**: commit messages, documents, comments and
  setting descriptions are read by people who have never seen this
  repository. `.md` files are ASCII.
- **Commits**: `type(scope): what changed`; a short body with what changed,
  why, and how it was tested, with the numbers. Stage explicit paths. No
  amending, no force push.

## What is the owner's to decide

Publishing, the roster row in Chimera, moving to another OpenJDK or
FreeJ2ME-Plus commit, and anything a licence does not plainly allow.
