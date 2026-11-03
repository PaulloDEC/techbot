# Techbot - An Enhanced PC Port of Duke Nukem

Techbot (named after Dr. Proton's robot henchmen) is an enhanced PC port of
Duke Nukem (1991), all three episodes, built on a complete decompilation of
the original.

A C reconstruction of `DN1.EXE` (v1.0, Turbo C 2.0, LZEXE-packed), built as
the foundation for a modern PC port.  `DN2.EXE` and `DN3.EXE` (Episodes 2 and
3) are the same program built with different text; the port runs them too,
from their own executables (see docs/VERIFICATION.md).  It is verified against the original
executable running in an x86 emulator: game state **and** every rendered
pixel match frame-for-frame (details below).

**No game data is included.**  Graphics, levels, sounds, text and constant
tables are read at runtime from your own copy of the game, including
`DN1.EXE` itself (unpacked in memory by `src/exe.c`).

## Layout

```
src/            the decompiled game (C99)
  dn1.h           types, prototypes, coordinate conventions
  globals.def     every global with its original data-segment offset
  exe.c           LZEXE 0.91 unpacker + access to the original data segment
                  (any episode: DN1 offsets are translated for DN2/DN3)
  episode_map.c   generated: where DN1's data lives in DN2/DN3.EXE
  ega.c           exact EGA emulation + the original's blit primitives
  core.c          collision helpers, effect pools, shots
  player.c        Duke: movement, jumping, hooks, somersaults, firing
  render.c        map/backdrop drawing, HUD
  actors1-8.c     all 50+ actor types (one routine each, as in the original)
  game.c          level loader / actor spawner, new game
  loop.c          the main game loop (one iteration = one frame)
  sound.c         PC-speaker effects + the 145 Hz timer interrupt
  input.c         keyboard interrupt, joystick
  files.c         data files, demos, saves, settings
  ui.c, ui_dialogs.c, frontend.c   text, dialogs, menus, high scores, ending
  main.c          entry point and title/menu flow
  port.h, port_*.c port enhancements around the game: TECHBOT.CFG settings,
                  cursor menus, save-slot picker, Options/remapping, quicksave,
                  controller button names, smooth motion (port_render.c),
                  overlapping / hi-fi sound (port_sound.c), widescreen
                  replacement art (port_art.c)
  third_party/    stb_image.h (public-domain PNG decoder, v2.30)
  pf_sdl.c        SDL2 platform layer (playable build, controller input)
  pf_headless.c   deterministic platform layer for automated testing
docs/
  FUNCTION_MAP.md original address -> C function
  FORMATS.md      file formats and in-memory layouts
  QUIRKS.md       original bugs that are deliberately preserved
  dn1.asm         full annotated disassembly (strings shown as offsets)
resources/        Windows icon (techbot.ico from techbot.png) and version info (techbot.rc)
BUILDING.md       build and run instructions
PORTING.md        plan for widescreen, high frame rate, quicksave, ...
tools/            extract_images.py (pictures/backdrops -> PNG), unpacker, disassembler, lifter, emulator harness, diff tools
```

## Building and running

Full step-by-step instructions (Visual Studio, VS Code, MSYS2, Linux/WSL,
the verification tools, troubleshooting) are in **[BUILDING.md](BUILDING.md)**.
The short version, on Windows with Visual Studio 2022 and the SDL2 VC package:

```
cmake -B build -A x64 -DCMAKE_PREFIX_PATH=C:/libs/SDL2-2.30.9
cmake --build build --config Release
build\Release\techbot.exe --dir C:\games\duke1
```

Linux / WSL: `make`, then `./build/techbot --dir <game folder>`.

## How it was made

1. `tools/unlzexe.py` unpacks the LZEXE 0.91 executable.
2. `tools/dn1dis.py` disassembles it by recursive descent (Turbo C large
   model: game module at seg 0097, UI/file/sound module at 108E, hand-written
   EGA routines in the startup segment, C runtime above 14F6), discovering
   jump tables and the actor routines that are only reached through the
   runtime function-pointer list.
3. `tools/lift.py` turns Turbo C idioms into pseudo-C; every function was
   then rewritten by hand as structured C with named globals.
   `tools/gen_ui.py` mechanically converts the ~45 straight-line dialog
   routines (they reference text only by data-segment offset).
4. `tools/emu/` runs the **original** executable under Unicorn with a small
   DOS/BIOS/EGA/PIT emulation, plays demos, and dumps the whole data segment,
   the live map and the displayed screen every frame.

## Verification

`pf_headless.c` + `tools/cmp_state.py` compare the port with the original:
every global (mapped through `globals.def`), the 128x90 map and the displayed
EGA page, after every game frame.

* Attract-mode demo (MY_DEMO.DN1, level C, 2635 frames): identical state and
  pixels for all frames.
* Fuzzing (`tools/fuzz_levels.py`): each level file is placed in slot 1 and a
  random scripted input stream is played through the demo-playback path in
  both the original and the port.  Results: `docs/VERIFICATION.md`.
  Run it with `DN1_GAME=<game folder> python3 tools/fuzz_levels.py 1,2,3 1,2 2500`
  (`FUZZ_TIMEOUT` caps each side in seconds; runs that stall in a dialog are
  compared up to the stall, and a different frame count is reported).

Reproduce: `python3 tools/unlzexe.py DN1.EXE build/dn1_unpacked.exe`, then
`python3 tools/emu/run_demo.py build/dn1_unpacked.exe <gamedir copy> ref.bin 3000 5`
and `./build/dn1_headless --dir <gamedir copy> --exe <unpacked exe> --menu 5 --frames 3000 --dump port.bin`,
then `python3 tools/cmp_state.py ref.bin port.bin src/globals.def`.

## Status / limitations

* All gameplay code (player, every actor, effects, level loading, main loop,
  sound, saves, demos) is reconstructed and verified as above.
* Front-end screens (title/menu idle timers, story pages, high-score entry,
  ending) are reconstructed but cannot be covered by demo diffing; their flow
  was checked against the disassembly only.  `main_menu()` keeps the original
  keys and return values but its idle/attract timing is simplified.
* Joystick calibration and the keyboard redefinition screen talk to the PC
  game port / scancodes directly in the original; the port maps a modern game
  controller instead (`pf_sdl.c`) and keeps KEYS.DN1 compatibility.
* Episode 1 only (the level/backdrop name tables come from this EXE).
