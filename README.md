# Techbot - An Enhanced PC Port of Duke Nukem

**AI Disclosure: Pretty much the entire project was created by Claude AI. Widescreen-enhanced backdrop artwork is the primary exception.**

## The Human Bit

Techbot brings the original Duke Nukem DOS game to modern Windows with 
a host of optional enhancements. Play the game the classic way, or take advantage 
of new features including:

 - Support for widescreen displays
 - Multiple HUD options ranging from authentic to minimal
 - Full controller support, including menus
 - Fully re-bindable controls
 - In-game episode switching
 - Interpolation for smoother motion
 - Backdrop artwork extended (by hand, by a human) for widescreen play
 - Improved sound playback
 - Quicksave and Quickload
 - Support for custom levels built in Shrapnel
 
There might be more! I honestly can't remember.

Then, there's also:

# Shrapnel - A Modern Level Editor for Duke Nukem

Shrapnel allows users to edit existing levels or create entirely new
ones in a clean, modern editor. Key features include:

 - Support for Techbot enhanced or DOS compatible episodes
 - Clean, easy-to-use interface
 - Painstakingly categorised tiles, grouped by theme
 - Level checking tool that scans your level for issues
 - In-editor playtesting
 - ...and much more!

If you're asking "Why?" about all of this, it's a valid question. Like many projects, this
one started out simply because I had a couple of weeks of Claude to burn, and I was
curious. Eventually it became clear that the project was going to be good enough to
share, and so here we are.

## The AI Bit (everything below this point)

Techbot (named after Dr. Proton's robot henchmen) is an enhanced PC port of
Duke Nukem (1991), all three episodes, built on a complete decompilation of
the original.  Shrapnel is a level editor built on the same code, so what it
draws, checks and play-tests is exactly what the game does.

The core is a C reconstruction of `DN1.EXE` (v1.0, Turbo C 2.0,
LZEXE-packed).  `DN2.EXE` and `DN3.EXE` (Episodes 2 and 3) are the same
program built with different text and data; the port runs them too, from
their own executables (see `docs/VERIFICATION.md`).  It is verified against
the original executable running in an x86 emulator: game state **and** every
rendered pixel match frame-for-frame (details below).

**No game data is included.**  Graphics, levels, sounds, text and constant
tables are read at runtime from your own copy of the game, including the
EXE itself (unpacked in memory by `src/exe.c`).  Neither program ever writes
into the game folder's original files.

## Techbot

### Options

Techbot plays exactly like the original in **Original** game mode.  In
**Enhanced** mode these settings (Options menu) shape how it looks and sounds:

**Aspect**
- **Classic** - a 4:3 presentation, just like the original
- **Wide (16:9)** - a modern 16:9 presentation (the default)
- **Wide (Fill)** - an ultrawide presentation that fills your display

**HUD**
- **Classic** - just like the original
- **Slim Overlay** - a slimline HUD, displayed over a full-screen playfield
- **Slim Fixed** - a slimline HUD, displayed in a fixed panel below the playfield
- **Off** - no HUD at all, with a full-screen playfield (Tab toggles it in play)

**Sound**
- **Classic** - just like the original
- **Improved** - authentic, but improved under the hood (the default)
- **Wacky** - an experimental variant

Smooth motion (in-between frames at the display's refresh rate), quicksave
and quickload (one slot per episode), controller support with button names in
the prompts, remappable keys, and widescreen replacement art from a
`widescreen` subfolder all come with Enhanced mode.  See `BUILDING.md` for the
details of each.

### Custom levels and episodes

Levels (`.LEV`) and episodes (`.EPI`) made in Shrapnel go in a `LEVELS`
folder inside the game folder and are played from **U)ser levels** on the
main menu.  An episode can have its own level order, backdrops, Dr. Proton
messages and ending; everything else (tiles, actors, sounds) comes from the
player's own game files, so packs contain only the player's work.

## Shrapnel

Shrapnel edits single levels and whole episodes, either for Techbot or as a
copy of a DOS episode that the original game can still play.

- **Drawn by the game itself.** Tiles, animated tiles, backdrops and every
  actor appear exactly as the game draws them; a self-test compares
  Shrapnel's picture with the game's own, pixel for pixel, for every level.
- **Editing tools.** Select, Move, Magic wand, Pencil, Eraser, Line,
  Rectangle, Fill and Pick, with cut/copy/paste, flips, find and replace,
  layer protection (walls, background, backdrop, actors) and full undo
  history.
- **Every tile named.** Every tile has a name and most belong to a set of a
  kind (a 3 x 3 panel, a girder, a border), so the palette is searchable and
  ordered by look rather than by tile number.  All 94 actors (the game's 90
  and Techbot's four loose items) are named and placed with their real
  sprites.
- **Checks.** What the game relies on (a player start, an exit or a way to
  end the episode, a key for every lock, a reader for every access card,
  actor limits, DOS compatibility) is checked as you edit or on demand, with
  an explanation of each problem.  A reachability search shows where Duke can get to, where he would
  get stuck, and one way to the exit.
- **Play-testing.** Play from anywhere in the level, or from the start,
  right inside the editor and come straight back (F4), or open the project
  in Techbot.  Reaching the exit records the clear.
- **Episodes.** Reorder levels by drag and drop, add and remove levels with
  their hallways, choose each level's two backdrops (including your own
  pictures, converted to the game's 16 colours), write each level's
  Dr. Proton message, and pick or write the ending.
- **A modern interface.** Dark and light themes with an accent colour of
  your choice, resizable panels, an adjustable interface size, and
  Material Symbols icons.

`BUILDING.md` covers every feature in detail.

## Layout

```
src/            the decompiled game (C99), shared by Techbot and Shrapnel
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
  port.h, port_*.c port enhancements around the game:
                  TECHBOT.CFG settings (port_cfg.c), cursor menus and Options
                  (port_menu.c, port_options.c), quicksave and snapshots
                  (port_qsave.c, port_state.c), smooth motion (port_render.c),
                  Improved / Wacky sound (port_sound.c), widescreen art
                  (port_art.c), the Slim HUD (port_hud.c), controller button
                  names (port_labels.c), custom levels and episodes
                  (port_pack.c, port_dir.c), the start-up log (port_log.c),
                  and what Shrapnel needs from the game (port_editor.c,
                  port_reach.c)
  third_party/    stb_image.h (public-domain PNG decoder, v2.30)
  pf_sdl.c        SDL2 platform layer (playable build, controller input)
  pf_headless.c   deterministic platform layer for automated testing
shrapnel/
  src/            the editor (C++17, Dear ImGui on SDL2)
    main.cpp        the window, panels, menus and play-testing
    episode.cpp     loading an episode and its levels through the game's code
    project.cpp     projects (a level or an episode) and the .LEV/.EPI format
    tools.cpp       painting, lines, rectangles, fills, selections
    checks.cpp      the level checks
    picture.cpp     backdrops, and pictures converted into them
    theme.cpp       the themes, fonts and widgets
    tile_names.cpp  generated: every tile's name and set
    selftest.cpp, catalog.cpp   the self-test and the tile/actor catalogue
  third_party/    Dear ImGui 1.91.9 (MIT), miniz (MIT), Inter (SIL OFL) and
                  Material Symbols (Apache 2.0) fonts
  resources/      Shrapnel's icon and version info
docs/
  FUNCTION_MAP.md original address -> C function
  FORMATS.md      file formats and in-memory layouts
  QUIRKS.md       original bugs that are deliberately preserved
  VERIFICATION.md emulator comparisons and fuzzing results, all episodes
  dn1.asm         full annotated disassembly (strings shown as offsets)
resources/        Techbot's icon (techbot.ico from techbot.png), version info
                  (techbot.rc) and Slim HUD art
BUILDING.md       build and run instructions, and every feature in detail
PORTING.md        plan for widescreen, high frame rate, quicksave, ...
tools/            unpacker, disassembler, lifter, emulator harness, diff tools,
                  fuzzing, extract_images.py (pictures/backdrops -> PNG),
                  make_fonts.py and tile_names.py (Shrapnel's fonts and tile names)
```

## Building and running

Full step-by-step instructions (Visual Studio, VS Code, MSYS2, Linux/WSL,
the verification tools, troubleshooting) are in **[BUILDING.md](BUILDING.md)**.
The short version, on Windows with Visual Studio 2022 and the SDL2 VC package:

```
cmake -B build -A x64 -DCMAKE_PREFIX_PATH=C:/libs/SDL2-2.30.9
cmake --build build --config Release
build\Release\techbot.exe --dir C:\games\duke1
build\Release\shrapnel.exe --dir C:\games\duke1
```

Linux / WSL: `make`, then `./build/techbot --dir <game folder>` or
`./build/shrapnel --dir <game folder>`.

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
5. Techbot's enhancements and Shrapnel were then built around the verified
   core, which stays untouched in Original mode.

## Verification

`pf_headless.c` + `tools/cmp_state.py` compare the port with the original:
every global (mapped through `globals.def`), the 128x90 map and the displayed
EGA page, after every game frame.

* Attract-mode demo (MY_DEMO.DN1, level C, 2635 frames): identical state and
  pixels for all frames.  Episodes 2 and 3 match too (their attract demo,
  and every level with random input).
* Fuzzing (`tools/fuzz_levels.py`): each level file is placed in slot 1 and a
  random scripted input stream is played through the demo-playback path in
  both the original and the port.  Results: `docs/VERIFICATION.md`.
  Run it with `DN1_GAME=<game folder> python3 tools/fuzz_levels.py 1,2,3 1,2 2500`
  (`FUZZ_TIMEOUT` caps each side in seconds; runs that stall in a dialog are
  compared up to the stall, and a different frame count is reported).
* Snapshots (quicksave and Shrapnel's play-testing): saving, playing on and
  going back must replay identically; see "Snapshot and restart checks" in
  `BUILDING.md`.
* Shrapnel's self-test (`shrapnel --dir <game folder> --selftest`) draws every
  level of every episode both ways and compares the pixels, round-trips every
  map and project file, exercises every editing tool against undo and redo,
  places every actor, and runs the checks and reachability search against the
  stock levels and a set of deliberately broken ones.

Reproduce: `python3 tools/unlzexe.py DN1.EXE build/dn1_unpacked.exe`, then
`python3 tools/emu/run_demo.py build/dn1_unpacked.exe <gamedir copy> ref.bin 3000 5`
and `./build/dn1_headless --dir <gamedir copy> --exe <unpacked exe> --menu 5 --frames 3000 --dump port.bin`,
then `python3 tools/cmp_state.py ref.bin port.bin src/globals.def`.

## Status / limitations

* All gameplay code (player, every actor, effects, level loading, main loop,
  sound, saves, demos) is reconstructed and verified as above, for all three
  episodes.
* Front-end screens (title/menu idle timers, story pages, high-score entry,
  ending) are reconstructed but cannot be covered by demo diffing; their flow
  was checked against the disassembly only.  `main_menu()` keeps the original
  keys and return values but its idle/attract timing is simplified.
* Joystick calibration and the keyboard redefinition screen talk to the PC
  game port / scancodes directly in the original; the port maps a modern game
  controller instead (`pf_sdl.c`) and keeps KEYS.DN1 compatibility.
* Levels are 128 x 90 cells, as in the original; packs can't change that.
* Shrapnel is still in development: a first-run tour, a sample level,
  prefabs (reusable pieces of level), remappable editor keys and more
  accessibility options are planned next.

## Third-party code and fonts

SDL2 (zlib licence), Dear ImGui (MIT), miniz (MIT), stb_image (public
domain), Inter (SIL Open Font License 1.1) and Material Symbols Rounded
(Apache License 2.0).  Their licences are in the folders beside them.
