# chimera-core-freej2me

A core for [Chimera](https://github.com/ToolAssisted-run/chimera), the
frontend for tool-assisted speedruns, for **J2ME games**: the Java games of
2000s mobile phones. It runs
[FreeJ2ME-Plus](https://github.com/TASEmulators/freej2me-plus) on a Java VM
(OpenJDK 21) built into the core, and produces one file,
`freej2me.chimeraCore`, that goes in Chimera's `Cores` folder.

## Status

Published: each push to `main` that passes the tests publishes a `dev`
release, and a dated nightly is made once a day when `main` moved. Chimera's
Core Manager lists it.

- The core's own test MIDlet runs in the sandbox with its keys, touch
  screen, text, record store and sound (MIDI music, tones and WAV sound
  effects), the same in every run, through savestates and in a new process,
  and in Chimera's own headless runner.
- Five games tried, from boot to their menus and further: Angry Birds
  (to level 1, with its music), Baldur's Gate, Cars 2 (to its name screen,
  with sound), Doom RPG (a Russian build: through its sound question, menu
  and loading into the story, with music) and The Game of Life. Doom RPG is
  the same through savestates around every one of 1800 frames.
- Windows: the test MIDlet's project gives the same pictures in Chimera's
  Windows headless runner as on Linux, and a state saved on either and
  loaded by another run continues the same.
- Japanese text draws with IPAex Gothic, a Japanese font in the core.
- Export Save Data gives the game's record stores as a zip, and a project
  can start from one.
- RAM Watch and RAM Search read the Java heap (64 MB), where the game's
  objects and its classes' variables are. The collector moves objects now and
  then, so an address found holds its value until the next collection.
- In Chimera's window, on Linux and on Windows, the test MIDlet ends in the
  same machine as in the headless runner; on Linux so does Doom RPG.
- Not tried yet: DoJa (i-appli) games, for want of one.

## What you bring

A game: its `.jar`, with the `.jad` that came with it if there is one. A
KDDI game is one `.kjx` file. Nothing else is needed: the Java VM, its
fonts and the MIDI synthesizer's sounds are in the core. A project can also
start from saved data: the zip Export Save Data writes.

## Buttons and settings

- **Keys**: Up, Down, Left, Right, Fire, Left Soft, Right Soft, Num 0 to Num
  9, Star, Pound and Clear.
- **Touch screen**: Touch, held where Touch X and Touch Y say.
- **Settings**: Screen Size, Phone Keys (which maker's key codes the keys
  send), Frame Limit, Sound, MIDI Synthesizers and Text Encoding. All of them
  are part of the machine: a movie needs the same values.

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has every step. [docs/PLAN.md](docs/PLAN.md)
is the design log: what was decided, why, and what was measured.

## Licence

This repository's own code is MIT, except the front end in `java/`, which is
GPL-3.0-or-later like FreeJ2ME-Plus. FreeJ2ME-Plus is GPL-3.0-or-later;
OpenJDK's HotSpot is GPL-2.0-only and its class library adds the Classpath
Exception. The package carries every licence text. See [LICENSE](LICENSE).
