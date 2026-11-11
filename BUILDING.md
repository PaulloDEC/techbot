# Techbot - building and running

*Techbot - An Enhanced PC Port of Duke Nukem* (named after Dr. Proton's robot
henchmen).

There are three programs, all built on the same core library
(`techbot_core`: the game itself, everything in `src/` except the two
platform layers `pf_sdl.c` and `pf_headless.c`):

| Program | What it is | Platforms |
|---|---|---|
| `techbot` | the playable game (SDL2 window, sound, keyboard + controller) | Windows, Linux, macOS |
| `dn1_headless` | no window or sound; runs scripted input and dumps every frame, for the verification tools | Linux / WSL only |
| `shrapnel` | the level editor, in early development (C++, Dear ImGui on SDL2; sources in `shrapnel/`) | Windows, Linux, macOS |

All need **your own copy of the game files** (any of the three episodes).
Nothing from the game is built in: graphics, levels, sound and text are read
from that folder at runtime, including `DN1.EXE` / `DN2.EXE` / `DN3.EXE` (as
shipped, still LZEXE-packed).

> Builds before the Techbot name were called `dn1port`; their settings
> (`DN1PORT.CFG`) and quicksaves (`DN1PORT.QSV` etc.) are still read, and
> settings are then saved as `TECHBOT.CFG`.

---

## Windows

Three routes; pick one.  **A** is the most straightforward, **B** is the same
thing driven from VS Code, **C** suits people who already use MSYS2.

### A. Visual Studio + CMake (recommended)

1. **Compiler.** Install *Visual Studio* 2019, 2022 or 2026 Community (or the
   smaller *Build Tools for Visual Studio*) with the **Desktop development
   with C++** workload.  That workload includes MSVC, CMake and Ninja.
2. **SDL2.** From <https://github.com/libsdl-org/SDL/releases>, download
   `SDL2-devel-2.x.y-VC.zip` (tested with 2.30.9; under *Assets*, maybe
   behind "Show all assets").  It must be **SDL2**, not SDL3 (the SDL3
   packages are named `SDL3-devel-...`), and the **-devel-...-VC** package:
   the plain `SDL2-2.x.y.zip` next to it is SDL's source code and will not
   be found.  Extract it somewhere permanent, e.g. `C:\libs\SDL2-2.30.9`.
   That folder should contain `cmake\`, `include\` and `lib\` (no `src\`);
   `cmake\sdl2-config.cmake` must exist.
3. **Unzip the project**, e.g. to `C:\dev\dn1`.
4. **Configure and build.** Open the *Developer PowerShell* for your Visual
   Studio version (Start menu - not a plain PowerShell, which lacks the
   compiler) and run, naming your version:

   ```powershell
   cd C:\dev\dn1
   cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/libs/SDL2-2.30.9
   cmake --build build --config Release
   ```

   For VS 2026 use `-G "Visual Studio 18 2026"` (needs CMake 4.2 or newer;
   `cmake --version` shows yours), for VS 2019 `-G "Visual Studio 16 2019"`.
   Or, with any version, use Ninja (the result is then `build\techbot.exe`):

   ```powershell
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/libs/SDL2-2.30.9
   cmake --build build
   ```

   Use forward slashes in the path (or escape the backslashes).  The result
   is `build\Release\techbot.exe`, with `SDL2.dll` copied next to it
   automatically.  The same build makes the level editor,
   `build\Release\shrapnel.exe` (for now a skeleton: run it with
   `--dir C:\games\duke1` and it lists the episode's levels).  If a configure step fails, delete the `build` folder
   before trying again: CMake remembers its first choice of generator.
5. **Run** - see [Running](#running) below.

To work in the Visual Studio IDE instead, open the generated
`build\techbot.sln`, set `techbot` as the startup project, and put
`--dir C:\games\duke1` in *Project properties > Debugging > Command Arguments*.

### B. VS Code (CMake Tools)

Needs everything from route A steps 1-3 (Visual Studio or Build Tools provides
the compiler; VS Code just drives it).

1. Install the **C/C++** and **CMake Tools** extensions.
2. Open the project folder in VS Code and create `.vscode/settings.json`:

   ```json
   {
     "cmake.configureSettings": {
       "CMAKE_PREFIX_PATH": "C:/libs/SDL2-2.30.9"
     }
   }
   ```

3. When CMake Tools asks for a kit, choose
   **Visual Studio Community 2022 Release - amd64** (or the Build Tools
   equivalent).  Pick the build variant *Release* or *Debug* from the status
   bar, then click **Build**.
4. To run or debug with the game folder, add `.vscode/launch.json`:

   ```json
   {
     "version": "0.2.0",
     "configurations": [
       {
         "name": "techbot",
         "type": "cppvsdbg",
         "request": "launch",
         "program": "${command:cmake.launchTargetPath}",
         "args": ["--dir", "C:/games/duke1"],
         "cwd": "${workspaceFolder}"
       }
     ]
   }
   ```

   and press F5.

### C. MSYS2 (MinGW-w64 GCC)

1. Install MSYS2 from <https://www.msys2.org> and open the **UCRT64** shell.
2. Install the toolchain and SDL2:

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                      mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
   ```

3. Build (SDL2 is found automatically, no prefix path needed):

   ```sh
   cd /c/dev/dn1
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

   `build/techbot.exe` runs from the UCRT64 shell.  To run it from Explorer,
   copy `SDL2.dll` (already next to the exe) plus any other DLLs that
   `ldd build/techbot.exe | grep ucrt64` lists from `C:\msys64\ucrt64\bin`.

### vcpkg alternative (for routes A/B)

If you already use vcpkg: `vcpkg install sdl2:x64-windows`, then configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` instead of
`CMAKE_PREFIX_PATH`.

---

## Running

The game folder must contain the files of at least one episode: `DN1.EXE`
and the `*.DN1` data files, `DN2.EXE` and `*.DN2`, or `DN3.EXE` and `*.DN3`.
The extensions differ, so all three episodes can share one folder.  Either:

* copy `techbot.exe` and `SDL2.dll` into the game folder and double-click
  `techbot.exe` (it looks in its working directory by default), or
* run it from anywhere and point it at the folder:

  ```
  techbot.exe --dir C:\games\duke1
  ```

The game **writes into that folder** exactly as the original does: saved games
(`SAVEDx.DN1`), high scores, `SPEED.DN1`, `KEYS.DN1` and the automatic
`USERDEMO.DN1` recording (`.DN2` / `.DN3` for the other episodes).  Point it at a copy if you want to keep your
original folder untouched.

Options:

| Option | Effect |
|---|---|
| `--dir <folder>` | game data folder (default: current directory) |
| `--episode N` | start episode 1, 2 or 3 (default: the last one chosen in *Episode select*; if that episode's EXE is missing, the first one found) |
| `--level N` | skip the title, menu and intro and start a new game at level N (1-10) of the episode |
| `--level-index I` | the same by the EXE's level table index (0-20: even = level, odd = the hallway after it, 20 = the demo map) |
| `--scale N` | initial window size, 320x240 x N (default 4 = 1280x960); the window is resizable and keeps 4:3 |
| `--fullscreen` | start in borderless fullscreen; **Alt+Enter** toggles at any time |
| `asp`, `g` | passed through as the original's command-line switches |

On Windows Techbot runs as a normal windowed program with its own icon
(`resources/techbot.ico`, built from the 16x16 `techbot.png` at 16-256 px,
pixel-sharp), without a console window; startup errors (no game files found, SDL failing to start) appear in a
message box.  For debugging, configure with `-DTECHBOT_CONSOLE=ON` to get the
console back (it then also shows the original's exit text).

### Episodes

*E)pisode select* on the main menu lists the episodes found in the folder
(by the names the game's ordering screen gives them).  Choosing another one
saves what quitting saves (high scores, keys, speed, demo) and restarts the
port in that episode, straight at its main menu.  It replaces the original's
*T)itle screen* entry, which only showed the title picture (the title still
appears when the game starts and between menus).  Every port feature works in all three
episodes, and each has its own quicksave slot (`TECHBOT1.QSV`, `TECHBOT2.QSV`,
`TECHBOT3.QSV`).  The episodes are the same program with different text and
data, so the port runs Episodes 2 and 3 from their own `DN2.EXE` / `DN3.EXE`:
see "Episodes 2 and 3" in `docs/VERIFICATION.md`.

### Custom levels and episodes (packs)

Levels and episodes made in Shrapnel (`.LEV` and `.EPI` files) play in
Techbot.  Put them in a folder called `LEVELS` inside the game folder:
*U)ser levels* on the main menu lists them (name, author, episode and
number of levels) and starts the one you pick, with no intro.  A pack made
with another episode's art switches Techbot to that episode first, as
*E)pisode select* does.  `techbot --pack <file>` starts one directly (this is
what Shrapnel's *Play in Techbot* uses).

A pack plays like a stock episode, with what Shrapnel set: its own number of
levels, each with its two backdrops (from any episode in the folder, or
pictures of its own), its own hallways, the words on its note monitors and
Dr. Proton screens, and its ending - one of the three stock endings (another
episode's needs that episode's EXE in the folder) or its own text.  An
episode ends when Dr. Proton is beaten, or at the last level's exit.  Its
saved games, high scores and demos are kept apart from the stock episode's,
named after the pack's file (`MYPACK-SAVED1.DN1`, `MYPACK-HIGHS.DN1`, ...).

A pack is a zip file holding `PACK.INI` and its maps:

```ini
name    = My episode
author  = Me
target  = enhanced          ; or classic: made to the DOS game's rules
kind    = episode           ; or level
episode = 1                 ; whose art (and stock text) it uses
ending  = custom            ; or 1, 2, 3: a stock ending
ending_text = Line one|line two   ; | starts a new line

[level 1]
map        = LEVEL01.DN1
backdrop   = 3              ; the stock pair of level 3 of the episode...
backdrop_a = DROP5.DN2      ; ...or each one: a game file (any episode)
backdrop_b = LEVEL01B.DRP   ; or a backdrop file inside the zip
note       = Four lines|of up to|23 characters|each

[hallway 1]
map  = HALL01.DN1
note = ...

[level 2]
...
```

Levels and hallways take turns, and the last entry is a level.  A backdrop
named after a game file the folder doesn't hold falls back to the stock
pair; an entry with no `note` shows the episode's own words for that place
(none past the tenth level).

### Log file and crash reports

Each run writes **`TECHBOT.LOG`** in the game folder: versions, the video
driver and renderer, window and drawing size, audio, controller, episode and
settings, then any errors.  The previous run's log is kept as
`TECHBOT-PREVIOUS.LOG`, so after a crash on launch it is still there when you
start again.

If Techbot crashes, the log ends with a `CRASH:` line: the error code, the
place (`techbot.exe+0x...`), what it was doing at the time, and the call
stack.  On Windows it also writes **`TECHBOT.DMP`**.  Opened in Visual Studio
(*File > Open*, then *Debug with Native Only*), with `techbot.pdb` from the
build folder next to `techbot.exe`, the dump shows the exact line.  The
Release build produces `techbot.pdb` for this; it does not change the
program's speed.

### High-DPI displays

Techbot tells Windows it handles display scaling itself, so on a 4K display
at 250% (for example) it draws at the full 3840x2160 while window sizes keep
Windows' scaled units.  The *Override high DPI scaling behavior* setting on
the EXE is not needed (and best left off).

### Controls and menus

* **Keyboard:** the original's keys, read from `KEYS.DN1`.  Remap them in
  *Game Setup > Options > Keyboard* (saved back to `KEYS.DN1`, so DOS sees
  the same bindings).  Numeric-keypad keys work as on the original.
* **Controller** (any controller SDL recognises: Xbox, PlayStation, Switch Pro, ...):

  | | During play | In menus |
  |---|---|---|
  | D-pad / left stick | move | move the cursor |
  | A (south) | jump | confirm / answer **Y** |
  | X (west), B (east) | fire | B = back / answer **N** |
  | Start | open the game menu (F1) | close it |
  | Back / Select | Esc | back |
  | LB / RB | quicksave / quickload | - |

  Remap in *Game Setup > Options > Controller*: pick an action, press the
  button.  Assigning a button that another action uses swaps them.

  The controller always works alongside the keyboard (*Keyboard mode*).  The
  original's *Joystick mode* - analogue joystick only, keyboard movement off,
  no remapping - is kept in Original mode; in Enhanced mode it adds nothing,
  so *J)oystick mode* and *K)eyboard mode* just say "Not available in
  Enhanced mode" (and switching to Enhanced mode returns to Keyboard mode).

* **Sound volume:** *Game Setup > S)ound volume* (the original's sound on/off
  toggle): OFF, 1-4 (20% steps) or MAX, left/right to change; the jump sound
  plays at each step so you hear the level.  Saved as `volume` (0-5) in
  `TECHBOT.CFG`.  The in-game **S** key still toggles sound on and off.

* **Menus:** every menu shows the game's own spinning cursor beside the
  current option: up/down to move, Enter/Space (or A) to confirm, Esc (or B)
  to go back.  The original letter keys still work.  Restoring or saving a
  game opens a slot list showing the level and score in each slot, instead of
  the original's "type a number" prompt.
* **A name for the high scores** with a controller (or the arrow keys): up and
  down step through the letters and digits for the next place, right keeps
  the one shown and moves on (with none shown: a space), left goes back to
  change the last one, A / Enter finishes.  Typing works as in the original.

### Quicksave / quickload

**F5** saves, **F9** loads (controller: LB / RB); both are remappable.  There
is one slot per episode, `TECHBOT1.QSV` (2, 3) in the game folder, and it works anywhere during
play, on any level.  A snapshot holds the complete game state (every
variable of the original, the live map, the actor list, video memory), so a
load returns to exactly the moment of saving.  It is separate from the
original's nine save slots, which still save only in the hallways.  The
quicksave also heads the *Restore an old game* list (as **Q**, with its level
and score; Q at the classic "1-9?" prompt too), so a game can be resumed
from the main menu after quitting.

### Smooth motion (Enhanced mode)

With *Game mode = Enhanced* and *Smoothing* set to 30, 60 or 120 fps or
*Display* (Options menu; Off disables it), the play window is redrawn at that
rate - never faster than the display - between the game's own updates: the camera and every sprite glide between their positions instead
of jumping 8-16 pixels at ~16 Hz.  The game still runs at its original speed
and the picture is still built from the original tiles and sprites on the
320x200 grid.  At the end of every update step the image is exactly the
original's frame.  Animation frames still change at the original rate.
Dialogs and large jumps (respawn, teleport) are shown without smoothing.
Smoothing shows each update with up to one update (~60 ms) of extra delay;
switch it off if you prefer the most immediate response.

### Aspect and HUD (Enhanced mode)

Two independent settings in *Options*:

| Aspect | |
|---|---|
| Classic | a 4:3 presentation, just like the original |
| Wide (16:9) (default) | a modern 16:9 presentation, the same on any display (letterboxed or pillarboxed on other shapes) |
| Wide (Fill) | an ultrawide presentation that fills your display (or the window's shape, if you resize it), up to about 2.4:1 |

| HUD | |
|---|---|
| Classic | just like the original: the border and status panel |
| Slim Overlay | a slimline HUD, displayed over a full-screen playfield |
| Slim Fixed | a slimline HUD, displayed in a fixed panel below the playfield |
| Off | no HUD at all, with a full-screen playfield |

In play, **Tab** (Options > Keyboard > HUD toggle) switches between Off and
the Slim HUD last used.  Any combination works: a Wide aspect with the
Classic HUD is the widened original screen described next; Classic aspect
with a Slim HUD is a 4:3 screen that is all play area.

**Wide aspect, Classic HUD.**  The play area widens to 16:9, or with Wide
(Fill) to the window's shape (resize the window or go fullscreen; up to about 2.4:1).
The left border stays on the left, the status panel stays on the right, the
top and bottom borders are extended with their own plain sections (the
"Press F1 for help" banner stays centred under the play area), and the level
backdrop is stretched by the graphics card at full window resolution.  The
view is centred on the game's own camera and stops at the map edges.
Dialog boxes and on-screen text keep their exact place in the original play
window, which is centred in the wide play area (so near a map edge they stay
centred rather than following the original view to one side); their rows are
the original's, as the play area is just as tall.
Enemies in the extra space are active - the activity window widens with the
view, plus a tile's margin so objects never pop in at the edge - which is a
(small) gameplay change, hence Enhanced only.  Menus, story pages and
pictures are shown in 4:3 with black bars (or behind replacement art).

**The Slim HUD bar.**  Along the bottom: health, score, pistol and
firepower, keys and inventory - and, in the 16:9 bar, the D-U-K-E letters
collected this level, each in its own slot.  Four bars in `resources/hud/`:
`hud_43.png` / `hud_43_alt.png` (320 wide) and `hud_wide.png` /
`hud_wide_alt.png` (424 wide), for Slim Overlay (over the play) and Slim
Fixed (on a black band).  The 16:9 bar is used when the picture is at least
424 wide, the 4:3 bar otherwise (Classic aspect, Wide (Fill) on 16:10);
bars are centred, with the play (Overlay) or black (Fixed) either side -
except the 16:9 Fixed bar in a wider picture, whose end columns are repeated
to the edges.
The contents are copied from the original status panel, which the game still
draws, so they always show what the panel would; the firepower meter is one
of `gun_power_1..4.png`.  `guide_*_full.png` show every element in place.
The art is built into the program: after changing a PNG there, run
`python tools/make_hud.py` (regenerates `src/hud_data.h`) and rebuild.  Every
opaque pixel must be one of the 16 EGA colours (so the bar fades with the
game).  The game's own view is centred in the part of the screen above the
bar, so nothing the original showed is ever behind it.

**Slim HUDs and Off.**  The whole picture (320 x 200 in Classic aspect,
424 x 200 in Wide (16:9), or the display's shape in Wide (Fill)) is play
area - down to just above the bar with Slim Fixed.  The game's own 13 x 10-tile view sits in
the middle of it; the view extends 20 lines above and below and to the
sides, stopping at the map edges (near the top or bottom of a level the
extra rows all go to the other side).  The activity window grows the same
way, with a tile's margin above and two below (tall objects show their
upper part first).  Dialog boxes and on-screen text keep their exact place
in the original window, which is centred on the screen - so they are
centred as before.  The level backdrop (or its replacement art) fills the
whole screen.

### Sound (Enhanced mode)

*Options > Sound*:

| Setting | What you get |
|---|---|
| Classic | just like the original: one effect at a time, as on the PC speaker (a new effect cuts off a lower-priority one, or is dropped) |
| Improved (default) | authentic, but improved under the hood: the same square-wave sounds, but up to six effects play at once, so nothing is cut off |
| Wacky | an experimental variant: overlapping, and each effect is re-synthesised from its own pitch data: glides become smooth sweeps on a rounder tone, stepped tones get a band-limited pulse with a slightly detuned twin, and the speaker's random-pitch "noise" effects (explosions, crashes) become filtered noise that follows the original pitch; with soft attack/release, a gentle low-pass and a little room reverb |

Timing and pitch follow the original data exactly (145 steps a second), and
the game's own sound logic still runs as before, so this changes nothing in
play.  In Original mode the sound is always the single-voice speaker.

To audition every effect without playing, the Linux/WSL test build can write
them out as WAV files (`NN_orig.wav` / `NN_hifi.wav`, plus `mix_*.wav` with
several effects overlapping):

```sh
build/dn1_headless --dir <game folder> --export-sounds <output folder>
```

### Scaling and frame pacing

*Options > Scaling*:

| Setting | What you get |
|---|---|
| Sharp (default) | as large as the window allows, original 4:3 shape, every game pixel the same size: the picture is enlarged by a whole number first, then smoothly shrunk to fit, so only the edge between two pixels is softened (by less than one screen pixel) |
| Nearest | plain nearest-neighbour; razor-sharp edges, but at most window sizes some game pixels come out one screen pixel wider or taller than their neighbours |

In windowed mode the window takes the shape of the picture: 4:3, or 16:9
with a Wide aspect (Enhanced mode), at the *Window size* setting.

Smooth motion presents on a fixed timeline at the chosen rate (30/60/120 or
the display's rate), so the frame rate stays at the cap with even spacing.
The cap applies to everything shown in Enhanced mode, menus and popups
included.
`show_fps = 1` in `TECHBOT.CFG` shows the measured frame rate in the window
title.

### Widescreen art (replacement pictures and backdrops)

In Enhanced mode with a Wide aspect (or, for backdrops, a Slim HUD or Off), Techbot looks in a **`widescreen`**
subfolder of the game folder for PNG files named after an original picture or
backdrop file plus `.png` - the names `tools/extract_images.py` writes
(below):

| File | Replaces | Shown |
|---|---|---|
| `DN.DN1.png`, `END.DN1.png`, `BADGUY.DN1.png`, `DUKE.DN1.png`, `CREDITS.DN1.png` | the full-screen pictures (title, ending, Dr. Proton, Duke, credits) | filling the wide screen; menus and dialogs the game draws over a picture stay on top |
| `DROP0.DN1.png` ... | level backdrops | filling the play area, with any HUD (both backdrops of levels that have two) |

(`.DN2` / `.DN3` in the names for the other episodes.)  Files that are not
there simply keep the original.  What the game draws on a picture lands
where it would on the original, the original's 320 x 200 centred in the art
(the ending's Dr. Proton flying up to the moon, say), so keep the middle of
wide art where the original has it.  The pages of white text on the red
tile (Ordering information, the instructions, the story) carry their tile
on across a wide screen.  Original mode, and Enhanced mode with
Classic aspect and HUD, always show the originals.  Backdrop art is scaled to
cover the play area and cropped evenly to its shape, never stretched, so one
picture serves both the Classic HUD (a wider, shorter area) and the Slim HUDs (the whole
screen); keep what matters away from the edges.

**Sizes.**  Any size and any colours.  Art in the game's own pixels - 200
lines tall (or 2x / 3x: 400, 600), and for backdrops also 160 (320, 480) -
is shown 1.2x taller than wide like the originals and scaled without
smoothing; anything else (e.g. 1920x1080, 1560x960) is taken to have square
pixels and is scaled smoothly.  Art is scaled to cover its area and cropped
evenly at the sides (or top and bottom) if its shape differs - never
stretched.

| Picture shape | Picture | Backdrop, Classic HUD | Backdrop, Slim / Off |
|---|---|---|---|
| 4:3 (original) | 320 x 200 | 208 x 160 | 320 x 200 |
| 16:10 | 384 x 200 | 272 x 160 | 384 x 200 |
| 16:9 | 424 x 200 | 312 x 160 | 424 x 200 |
| 21:9 | 568 x 200 | 456 x 160 | 568 x 200 |

(in game pixels; for square-pixel art match the displayed shape instead:
16:9 for pictures and Slim-HUD backdrops, e.g. 1920 x 1080; about 1.63:1
for Classic-HUD 16:9 backdrops, e.g. 1560 x 960.  One backdrop serves both:
a Slim-shaped one loses a little at the sides with the Classic HUD, a
Classic-HUD-shaped one a little at the top and bottom with a Slim HUD.)

**Fades.**  The game fades by turning its 16 palette colours to black (or
white) one at a time; each pixel of replacement art follows the palette colour
nearest to it, so art in the game's palette fades exactly like the original
and other art dissolves in step.

### Exporting the pictures (for replacement art)

`tools/extract_images.py` writes the full-screen pictures (title, ending,
Dr. Proton, Duke, credits) and every level backdrop of each episode in the
folder as PNG, named after the file they came from (`DN.DN1.png`,
`DROP3.DN2.png`, ...): 16-colour images with the game's palette, exactly as
the game shows them (pictures 320x200, backdrops 208x160).  Needs Python 3
and Pillow:

```powershell
pip install pillow
python tools\extract_images.py C:\games\duke1            # -> C:\games\duke1\extracted
python tools\extract_images.py C:\games\duke1 D:\art     # or a folder of your choice
```

### Controller button names in prompts

When a controller was used last, prompts such as "press Enter" name its
confirm button instead (A, CROSS on PlayStation pads, B on Switch Pro pads),
and the restart question's Y/N becomes the confirm/back buttons.  Using the
keyboard switches them back.

### Options menu

*Game Setup > Options* (from the main menu, or F1 > Game Setup during play):
game mode, menu style (cursor or classic), fullscreen, window size, scaling,
smooth motion, widescreen, sound, and the
controller and keyboard remapping screens.  Changes are saved at once.

### TECHBOT.CFG

The port's own settings live in `TECHBOT.CFG` in the game folder; it is
created with the defaults on first run.  The original's `.DN1` files are
never changed by port settings.

```ini
episode = 1            # 1, 2 or 3: which DNx.EXE in the folder to start
volume = 5             # 0 (off) .. 5 (full), in 20% steps
mode = original        # original | enhanced (enhanced features arrive in later versions)
menus = cursor         # cursor | classic (letter keys only, exactly as in DOS)
smooth = display       # Enhanced mode in-between frames: off | 30 | 60 | 120 | display
aspect = wide_16_9     # Enhanced mode: classic (4:3) | wide_16_9 | wide_fill (follows the display)
hud = classic          # Enhanced mode: classic | slim_overlay | slim_fixed | off
key.hud = 0x0f         # in play: HUD Off <-> the Slim HUD last used (scancode; 0x0f = Tab)
sound = improved       # Enhanced mode: classic | improved | wacky
scale = 4              # initial window size (command line --scale overrides)
scaling = sharp        # sharp (every pixel the same size) | nearest
show_fps = 0           # 1: frame rate in the window title
fullscreen = 0
deadzone = 12000       # left-stick deadzone, 0..32767
pad.jump = a           # controller bindings: pad.<action> = button[, second button]
pad.fire = x, b
pad.menu = start
pad.back = back
pad.quicksave = leftshoulder
pad.quickload = rightshoulder
key.quicksave = 0x3f   # PC scancodes: F5
key.quickload = 0x43   # F9
```

Actions: `up down left right fire jump menu back quicksave quickload`.  Buttons: `a b x y back
start leftshoulder rightshoulder leftstick rightstick dpup dpdown dpleft
dpright none`.  In menus, A and B are always confirm and back.

---

## Shrapnel (level editor, in development)

Shrapnel is built alongside Techbot (see above) and reads the same game
folder.  Levels are drawn by the game's own code - tiles, animated tiles,
backdrops, and every actor as the game draws it when the level starts - and
can be edited (tiles and actors), checked and played on the spot.

```
shrapnel --dir C:\games\duke1
```

Shrapnel only reads the game folder: nothing is ever saved into it.  On
Windows it has its own icon (`shrapnel/resources/shrapnel.ico`, built from
the 16x16 `shrapnel.png` at 16-256 px, pixel-sharp).

### The start screen

Shrapnel opens on a start screen: the game folder (change it there if it
has no `DN1.EXE`, `DN2.EXE` or `DN3.EXE`), two groups of choices, and the
projects you opened recently.

**Techbot Compatible** - plays in Techbot.  An episode can have any number of
levels (up to 99), each hallway its own map.

| Choice | |
|---|---|
| New level | an empty level: a small room in the middle of the map (as big inside as the blue hallway) with the player start and the exit door, in the art of the episode you pick |
| New episode | asks how many levels, then makes that many empty levels with a blue hallway (a copy of the stock one) between each |
| Open a level | any of the ten levels of an episode in the folder, or a level taken from a `.LEV` or `.EPI` file |
| Open an episode | a stock episode (its ten levels with a hallway of its own between each), or an `.EPI` file |

**DOS Compatible** - plays in the original DOS game too: an episode of ten
levels and the one hallway they all share.

| Choice | |
|---|---|
| New DOS episode | ten empty levels and the stock hallway, for the episode you pick |
| Open a DOS episode | a stock episode, or a folder you saved one in |

Stock levels and episodes open as copies: the first save asks where.  If
Shrapnel closed with a new project unsaved, the start screen offers it back.
**File > Start screen** returns there.

### Projects and saving

- A **Techbot level** saves as a `.LEV` file, a **Techbot episode** as an
  `.EPI`.  Both are zip files (rename one to `.zip` to look inside): a
  `PACK.INI` with the name, author, episode, ending and each level's
  backdrop, and the maps in the game's own format (`LEVEL01.DN1`,
  `HALL01.DN1`, ...).  Saving over a file keeps the previous version as
  `<name>.bak`.
- A **DOS episode** saves into a folder you choose, as that episode's own map
  files (`WORLDAL1.DN1` ... `WORLDALB.DN1`; `WORLDAL2` is the hallway).  To
  play it, copy them over a **copy** of the game folder.  The game folder
  itself is refused.  The demo map is never written (changing it would spoil
  the recorded demo).  A folder holding only some of the maps opens too: the
  others come from the stock episode.
- The level list (left) shows the project's levels and hallways; a dot after
  the name marks unsaved changes, and one at the right the worst check
  result.  In a Techbot episode, **+** puts a new level (with a hallway
  before it) after the one shown, and **-** takes one out with its hallway.  Drag a level onto another to change the order:
  it takes that one's place, the levels between move along, and the
  hallways stay where they are between the levels.
- **File > Project settings** has the name and author and, for a Techbot
  episode, its **ending**: one of the three stock endings, or your own words
  (five lines of up to 23 characters a box, over Duke's picture).
- Under the level list, the level shown has (in Techbot projects):
  - **backdrops A and B**.  A level's backdrop cells (the open space behind
    the walls) show A, except the ones painted as B, so one level can have
    two views.  Paint B with the Eraser set to B on the brush card, or the B
    tile in *Tiles > Backdrop*; the sidebar counts the cells showing each.
    Click a picture to pick from every backdrop in the game folder,
    all three episodes, each picture once (the open episode's file first, so
    it needs no other episode); or **a picture of your own** - any PNG, made
    to fill 208 x 160 pixels in the game's 16 colours, with or without
    dithering, kept inside the project.  A hallway shows the backdrops of the
    level before it, as the game does;
  - its **message**, the words its **Dr. Proton screen** or **note monitor**
    shows (one per level): the episode's own until *Customize* is ticked,
    then four lines of up to 23 characters, previewed as the game draws
    them.  The message window opens when you place either actor, when you
    double-click anywhere on its picture on the map, from *Message...* in the sidebar (shown
    while the level has one), or from *Edit > Level message*.

  A DOS episode keeps the backdrops, notes and ending its EXE fixes (the
  message window shows the stock words read-only).
- **Play in Techbot** (the menu beside **Play here**, *File* menu, `Ctrl+F5`) writes the project
  to Shrapnel's settings folder and starts Techbot (next to Shrapnel) on it:
  the whole level or episode from the start, with its notes, backdrops and
  ending, and Techbot's own settings.
- Every two minutes unsaved work is also written to an autosave
  (`<name>.autosave` beside the project, `SHRAPNEL.AUTOSAVE` in a DOS folder,
  or Shrapnel's own settings folder for a project never saved); Shrapnel
  offers it back the next time.

Launch options: `--open <file or folder>` opens a `.LEV`, `.EPI`, DOS
episode folder or a single map file; `--new level|episode|dos` makes a new
project (`--levels N` for an episode, `--episode N` for the art); `--level N`
or `--slot I` opens that level of a copy of stock episode `--episode N`
(`--dos` for the DOS version); `--at C,R` centres the view on a cell, `--zoom
Z` sets the zoom (1 = 100%).

### Looking around

| Control | Effect |
|---|---|
| Level list (left) | show one of the project's levels or hallways |
| Mouse wheel | zoom in or out at the cursor (25% to 800%) |
| Right or middle drag, or Space + left drag | pan |
| Arrow keys (Shift: further) | pan |
| `1` - `4` | zoom 100% - 400%; `0` shows the whole level |
| Overview (bottom left) | click or drag to move the view |
| Zoom (status bar) | out, in; click the percentage for 100% |
| View menu / toolbar | actors, labels (the markers the game shows only as tiles), grid (`Ctrl+G`), animation, backdrop (`Ctrl+B`: off shows backdrop cells as two flat shades, A darker than B) |

The toolbar lines up with the columns below it: playing over the sidebar,
the tools, undo and redo and what the map shows over the map, what is
protected over the palette (a narrow window puts them in one compact row).
**Check** (`F7`) is in the status bar, beside its result.  Drag the edge of either side
panel to make it wider or narrower, or the top edge of the Checks / History
drawer to make it taller or shorter (double-click an edge for the usual
size); the sizes are kept in `settings.txt`.  The status bar describes the cell under the cursor: column and row (from
0), its value, and what it is.  Backdrops repeat across the map every
13 x 10 tiles, which is how the game shows them with the camera at those
positions (in play they stay fixed to the screen).

**View > Interface size** makes everything bigger or smaller than your
display's own scaling (60% to 200%).  **View > Theme** switches between a dark and a light look and sets the
accent colour: seven presets or your own (a colour wheel).  Both are kept in
`settings.txt`.  The interface uses Inter (SIL Open Font License) and
Google's Material Symbols icons (Apache 2.0), built in; the licences are in
`shrapnel/third_party/fonts`, and `tools/make_fonts.py` remakes the fonts.

### Editing

| Tool | Key | |
|---|---|---|
| Select | `M` | drag a rectangle; Shift adds to the selection, Alt removes |
| Move | `V` | drag the selection somewhere else (outside it: the cell under the mouse, an actor say); Shift: move a copy.  What's left behind is backdrop (an actor's cell: what the game shows there); locked cells stay put |
| Magic select | `W` | click: the connected area of the same tile; Shift adds, Alt removes |
| Pencil | `B` | paint the brush; Alt-click picks up the tile under the cursor |
| Eraser | `E` | paint backdrop A (or B, chosen on the brush card) |
| Line | `L` | drag a straight line |
| Rectangle | `R` | drag a rectangle, outlined or filled (the *Filled* chip on the toolbar) |
| Fill | `G` | fill the connected area of the same tile; Shift-click: every matching tile in the level |
| Pick | `I` | click to take a tile or actor as the brush |

The brush is chosen in the palette (right): **Tiles** (backdrop, animated,
background and solid, as the episode has them) or **Actors**.  The search
box finds a tile or actor by its name, its set or its value; the card at the
top shows the brush (and, for the Eraser, backdrop A or B).  Every tile has a
name, and tiles of a kind (a 3 x 3 panel, a girder, a border) are kept together
as a **set**, on a shaded band, top row first.  The names and sets come from
the objects sheet: `tools/tile_names.py DN1_objects.xlsx` remakes
`shrapnel/src/tile_names.cpp` from it.  The **Backdrop** chip shows the open
level's two backdrops as pictures.

| Command | Keys |
|---|---|
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) |
| Cut / copy / paste | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` - a paste follows the cursor until you click; `X` mirrors it, `Y` flips it, Shift-click keeps pasting, Esc or right-click cancels |
| Duplicate | `Ctrl+D` - a copy of the selection one cell down and right |
| Delete | `Del` - the selection becomes backdrop |
| Flip horizontal / vertical | `Shift+H` / `Shift+J` (or the Edit menu) - the selection mirrored where it is; facing actors turn round (tiles aren't redrawn: DN1 has no mirrored ones) |
| Select all / none | `Ctrl+A` / `Esc` |
| Find and replace | `Ctrl+F` - select or replace every cell holding a value |
| Save / save as / open | `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+O` |

### Actors

The **Actors** panel (right) shows every actor as the game draws it, in
groups: enemies, hazards, crates, bonuses, keys and doors, machinery, and
start, exit and displays.  The game draws all crates alike, so each carries
a small picture of what is inside - recorded, like the looks, from the game
itself (a shot is put on the crate and what appears is kept).  With Labels
on, crates on the map show their contents too.  Click one to make it the brush; the Pencil then places
one per click, and its outline shows where it will be.

- **F** turns the brush round: flamethrowers, fans, biters (which wall they
  cling to), spikes (up or down), conveyors (which way they move).
- **Conveyors and the walkway** are placed by dragging along a row: the
  start where you press, the end (or the walkway's stop) where you let go.
- **Loose items** (Techbot projects only): the boots, grappling claws,
  robot hand and access card on their own, ready to pick up, instead of in a
  crate to shoot open.  The DOS game always starts these in a crate, so a
  DOS episode's palette leaves them out and the checks mark one as Broken
  there.  To the other rules a loose item is the same item (one per level, a
  card needs a reader, and so on).
- **One per level**: placing the player start, the exit door, a boss or a
  teleporter again moves it.
- **Sets**: placing a key with no lock of its colour makes the lock the
  brush (and then the doors); likewise the access card and card reader, the
  robot hand, hand reader and walkway, and the two teleporters.  The status
  bar says what comes next.
- **Moving**: with the Select tool, drag an actor to another cell.  The cell
  it leaves gets back what it held before, if the actor was put there in
  this session; otherwise it keeps the tile the game showed there.  (When
  a level loads, the game fills each actor's own cell by copying a
  neighbouring cell - which one depends on the actor - so an actor moved
  beside a tile shows that tile behind it, in the editor as in play.)
- **Conveyors** show their belt as in play (the game draws it into the map
  every frame from the start marker to the end).  A **walkway** shows only
  its start and stop until a hand reader runs it, as in the game; the path
  it will extend along is outlined.  A **bridge** is laid by the game's
  loader two cells at a time, checking for a wall only every second cell:
  with an odd gap it stops one cell short, and with an even gap and a wall
  one cell thick it runs on through the wall.  What Shrapnel shows is what
  the game builds.
- **Actor counts** (in the Checks drawer) shows how full each of the game's
  actor tables is against what the original DOS game holds, over-full ones
  first; going past a limit says so at once.

Mirroring moves cells and turns facing actors round (flamethrowers, fans,
biters, conveyors; flipping turns spikes up or down).  The tiles themselves
are not redrawn: DN1 has no mirrored tiles.

Painting background, animated or backdrop tiles over an actor puts the
tile **behind** the actor rather than over it: the actor stays, and shows
the tile behind it.  (The game fills an actor's own cell from a
neighbouring cell when the level loads - the status bar names which - so
Shrapnel paints that neighbour too when the stroke has not already.  If
that neighbour is a wall it is left alone, and the actor keeps the wall
behind it.)  Fills flow past actors the same way.  Walls painted over an
actor replace it, and the eraser removes it.

**Protect** (the padlocks on the toolbar: walls, background, backdrop,
actors) stops painting, filling, pasting and
deleting from touching walls, background, backdrop or actors.

**History** (the drawer under the map: *View > History panel*) lists every change to the level shown; click one
to go back to it.  Undo is unlimited within a session (each step keeps a
whole copy of the 23 KB map), and each level and hallway keeps its own
history while the project is open.  Closing the project or quitting with
unsaved changes asks first.

### Checks

The original game checks nothing: a conveyor with no end hangs it, a second
player start silently moves Duke.  Shrapnel checks every level of the
project and lists the results in the **Checks** drawer under the map
(click the result at the right of the status bar, or *View > Checks
panel*; it turns amber for warnings, red for something broken).  Each result
is one of:

- **Broken** - the game will hang, crash, or the level cannot be finished;
- **Warning** - the game allows it, but it is almost certainly a mistake;
- **Note** - worth knowing (hidden unless **Show notes** is ticked).

The checks run **as you edit** (the rules after each change; the
reachability search and the recorded clear once the map has settled), or
**when asked**: pick it at the top of the Checks drawer (or untick *View >
Check as you edit*) if they slow a big project down.  Then **Check** (`F7`)
runs all three, the panel says when its results are out of date, Shrapnel
reminds you now and then (after five minutes or 40 changes unchecked), and
the rules run anyway before saving and before Play in Techbot.  The choice
is kept in `settings.txt` in Shrapnel's settings folder.

Click a result to see why (what the game's code does) and to jump to the
cell, which flashes; broken and warning cells carry a red or amber corner
flag on the map, and levels with problems a dot in the level list.  Some
results have a one-click fix (a biter on the wrong side of its wall turns
round).  What is checked:

| | |
|---|---|
| The basics | one player start (the game keeps the last), not so near the edge that the opening camera shows past the map; an exit door (Techbot: the last level needs Dr. Proton or an exit; DOS: Dr. Proton, whose defeat is the EXE's only ending); Dr. Proton outside the last level; only real tiles and actors |
| One of each | the game keeps one exit, start, boots, claws, robot hand, card, card reader, hand reader, walkway, bridge, reactor, teleporter A and B, snake, note monitor and Dr. Proton: extra ones never appear |
| Sets | keys with locks and key doors of the same colour; the card with a card reader; vertical force fields with both; horizontal force fields with a reactor; the hand reader with the robot hand (which Duke keeps from level to level) and the walkway; teleporters in pairs; conveyor starts with an end after them on the same row; the walkway with a stop tile (2FE0h) to its right; the bridge with a wall an odd number of cells away (it is laid two cells at a time) |
| Placing | biters with a wall on the side they cling to; down spikes under a ceiling; floor spikes, up spikes and rockets on a floor; flamethrowers and fans not facing straight into a wall; the exit, teleporters, helicopters and Dr. Proton clear of walls; things that fall show where they land, or fall off the map |
| Limits | each of the game's actor tables within what it holds |

### A catalog of tiles and actors

`shrapnel --dir <game folder> --catalog catalog.json` writes every map tile
and actor of each episode in the folder, as the game draws them, with how the
stock levels use each.  `tools/dn1_catalog.py` (needs `pip install openpyxl
pillow`) turns that into a spreadsheet with a picture of each and a blank
Name column:

    python tools/dn1_catalog.py --shrapnel build/shrapnel --game <game folder> -o DN1_objects.xlsx

### Reachability

Shrapnel also works out where Duke can get to.  It does not guess with a
model of his jumps: it runs the game's own movement code (walking, jumping,
falling, hanging from the claws and pulling up, the camera that decides how
fast he falls) one frame at a time, from the player start, with every
combination of the controls, until nothing new turns up.  On the way it
takes in what changes the level: boots, claws, the robot hand, the access
card and keys (picked up where Duke can touch them), locks, the card reader
and hand reader (with the walkway growing a cell a frame), the collapsing
bridge, elevators, conveyor belts, teleporters, vertical force fields
pushing him back, horizontal ones and spikes knocking him off his feet, the
reactor (shot from where he can hit it), rockets (shot: they take their
launch pad with them and knock a hole in the ceiling) and breakable blocks
(shot away from wherever he can shoot them).  Enemies and damage are left
out: it answers "can he get there", not "will he survive".  Boots, claws
and the robot hand found in earlier levels of the episode count as carried.

It runs in the background a moment after the map stops changing (the
**Checks** panel shows its progress; a big level takes from a few seconds
to a minute) and adds to the checks:

- **Broken** - Duke can't reach the exit (or Dr. Proton) from the start;
- **Warning** - places he can get to but never back from to the end (red on
  the map); keys, items, locks and readers he can't reach; a lock he can
  reach but never with its key;
- **Note** - bonus items he can't reach.

With **Reach** on (the toolbar, or **Ctrl+R**) the map is shaded: green
where Duke can be, red where he can get stuck, dark where he never gets;
breakable blocks the search shot away are outlined, and a line shows one way
from the start to the end.

### The recorded clear

A play from the player start (**Shift+F4** or **From start**) that reaches the
exit or beats Dr. Proton, without **F2** or **F3**, is kept with the level:
the controls, frame by frame (saved in the project; a DOS folder keeps them
in `SHRAPNEL.CLR`, which the game ignores).  After every change, once the map
has settled for a second, the game plays those controls again, unseen, on
the level as it now is.  The game does the same thing every time it is given
the same controls, so if Duke still gets to the end, the change didn't get in
his way; if he doesn't, a warning in **Checks** says what happened to him and
where.  The **Play** panel shows the clear's state, plays it back for you to
watch (**Watch it**), or forgets it.  A newer clear replaces an older one.

Saving a DOS episode with something broken asks first.  **File > Convert
to Techbot episode** turns a DOS episode into a Techbot one (each hallway
then its own map), for a level that needs more than the original allows.

### Play-testing

**F4** (or **Play here** on the toolbar) plays the level as it stands, from
the cell under the mouse: Duke drops to the floor below it.  It is the game
itself running in Shrapnel's window - the same code, sound, controls,
`TECHBOT.CFG` settings and key bindings as Techbot - and nothing is saved
first: the map you are editing is what loads.  **From start** plays from the
level's player start instead, carrying nothing, as the game begins it.

| While playing | |
|---|---|
| `F4` | back to the editor, which centres on where Duke was |
| `F2` | invulnerable on / off |
| `F3` | refill health |
| `F10` | start again from the same place |
| `Esc` | the game's own quit question |

The **Play** panel (right) sets what Duke starts with: boots, claws, robot
hand, access card, keys, firepower, health, and whether he is
invulnerable.  A play also ends when Duke reaches the exit, dies, or beats
the boss; the status bar and the panel say which, and how long it took.
Back in the editor, the path Duke took is drawn over the map until the next
edit.  Quicksave and quickload are off during a play-test, and nothing it
does (score, high scores, saved games, demos) is written to the game folder.

### Self-test

`shrapnel --dir <game folder> --selftest` (exit code 0 when all pass):

- draws every slot of every episode in the folder twice - by Shrapnel and
  by the game's own `draw_world`, with the camera at positions covering the
  whole map at all four animation phases - and compares every pixel;
- saves every map back unchanged and compares the files byte for byte;
- makes a few hundred random edits with every tool on two levels per
  episode, then checks that undo, redo and jumping through the history give
  back exactly each recorded map, and that the picture redrawn piece by
  piece equals one drawn whole;
- places every one of the 94 actors (the 90 of the game, and Techbot's four
  loose items) beside the player start of level 1
  (with its end, for conveyors and the walkway), has the game's loader take
  it, and removes it again.  With `SHRAPNEL_ACTOR_MAPS=<folder>` set, each
  of those levels is also written there, to be played in Techbot;
- saves every stock episode as an `.EPI` and as a DOS folder, and every stock
  level as a `.LEV`, and opens them again: the maps must come back the same,
  and the DOS files byte for byte the stock ones.  New episodes, added and
  removed levels and the project details round-trip too, saving a DOS
  episode into the game folder must be refused, and afterwards the game
  folder must hold exactly what it did;
- runs the level checks on every stock level (none may come out Broken), and
  on 41 deliberately broken levels, one per rule, every one of which must be
  caught.  `SHRAPNEL_CHECKS_LIST=1` lists every stock level's results;
- paints tiles behind actors (a dab, a row of actors, an actor against a
  wall, a fill past actors) and checks the eraser and walls still replace;
- runs the reachability search on planted problems in a new level's room (a
  wall to the ceiling, a pit with no way out, a door with and without its
  key, a key behind its own door, a wall only the boots get over, with them
  in a crate and loose) and on every stock level, each of whose
  ends must be reachable.  With `SHRAPNEL_REACH_REPLAY=1` it then takes the enemies out of
  each level, searches again, and plays the route the search found in the
  game itself (unseen, at full speed): Duke must reach the exit, frame for
  frame where the search said he would be, and playing the recorded controls
  a second time must give exactly the same play (this takes a while).
  `SHRAPNEL_SKIP_REACH=1` leaves the search out;
- saves recorded clears in `.EPI` projects and DOS folders and reads them
  back, and own backdrops and notes in `.EPI` projects;
- decodes every backdrop file in the folder and encodes it again byte for
  byte, turns a PNG in the game's colours into exactly those colours (with
  and without dithering), and draws a note box.

It writes a temporary file into the game folder (and removes it).

---

## Linux / WSL / macOS

```sh
# Debian/Ubuntu (incl. WSL); macOS: brew install sdl2
sudo apt install build-essential cmake libsdl2-dev
make                      # builds build/techbot, build/dn1_headless and build/shrapnel
./build/techbot --dir ~/games/duke1
```

`make techbot`, `make dn1_headless` and `make shrapnel` build one program;
the shared core is compiled once into `build/libtechbot_core.a`.  Shrapnel
needs a C++ compiler (g++ or clang++) and SDL 2.0.18 or newer.

`cmake -B build && cmake --build build` works too.  File names in the game
folder may be upper- or lower-case; the port looks for both.

---

## Verification tools (optional, Linux / WSL)

These compare the port with the original `DN1.EXE` running in an emulator
(see `docs/VERIFICATION.md`).  On Windows, use WSL (Ubuntu):

```sh
cd /mnt/c/dev/dn1
python3 -m venv .venv && . .venv/bin/activate
pip install unicorn capstone pillow       # pillow only for screenshots
make dn1_headless

# unpack the original executable once
python3 tools/unlzexe.py /mnt/c/games/duke1/DN1.EXE build/dn1_unpacked.exe

# attract demo: original vs port, every frame (takes ~10-20 minutes)
cp -r /mnt/c/games/duke1 /tmp/dn1demo
python3 tools/emu/run_demo.py build/dn1_unpacked.exe /tmp/dn1demo ref.bin 2640 5
./build/dn1_headless --dir /tmp/dn1demo --exe build/dn1_unpacked.exe \
                     --menu 5 --frames 2640 --dump port.bin
python3 tools/cmp_state.py ref.bin port.bin src/globals.def

# random-input fuzzing of chosen levels and seeds
DN1_GAME=/mnt/c/games/duke1 python3 tools/fuzz_levels.py 1,2,3 1,2 2500

# Episodes 2 and 3: the same tools with that episode's (unpacked) EXE
python3 tools/unlzexe.py /mnt/c/games/duke2/DN2.EXE build/dn2_unpacked.exe
DN1_GAME=/mnt/c/games/duke2 DN1_EXE=build/dn2_unpacked.exe DN1_EXT=dn2 \
    python3 tools/fuzz_levels.py 1,2,3,4,5,6,7,8,9,a,b,c 1 1500

# regenerate src/episode_map.c (where DN1's data lives in DN2/DN3.EXE)
python3 tools/episode_map.py DN1.EXE DN2.EXE DN3.EXE > src/episode_map.c
```

The dumps are large (about 120 KB per frame, so ~300 MB for the demo);
delete them afterwards.  Use a copy of the game folder: the runs write
`USERDEMO.DN1` and other files into it.

### Snapshot and restart checks

`src/port_state.c` keeps whole-game snapshots in memory (what a quicksave
holds, plus the spent-shot pool) and `port_restart()` starts play afresh
without leaving the program; the level editor's play-here relies on both.
Two headless checks prove they are exact:

```sh
# snapshot at frame F, play K frames, go back, play them again: all must match
./build/dn1_headless --dir /tmp/dn1demo --frames 6000 --state-test 700,400

# play level index A, at frame N go back to the start-up state and start
# level index B: from then on the dump must match a fresh run of B
./build/dn1_headless --dir /tmp/dn1demo --level-index 0 --frames 500 --dump fresh.bin
./build/dn1_headless --dir /tmp/dn1demo --level-index 8 --reset-test 600,0 \
                     --frames 1101 --dump reset.bin
python3 tools/cmp_reset.py fresh.bin reset.bin 600
```

`--state-test` prints the number of differing frames and exits with 2 if
any differ.  Pick N before Duke can die standing still: the game-over
screen waits for a key the headless build never presses.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| CMake: *"SDL2 not found: only the headless test build is configured"* | `CMAKE_PREFIX_PATH` must be the folder that contains `cmake\` (e.g. `C:/libs/SDL2-2.30.9`, not its `lib` subfolder).  Delete the `build` folder after changing it, since CMake caches the failed search. |
| CMake finds nothing / generator errors | Run from *Developer PowerShell for VS 2022*, or let CMake Tools pick the Visual Studio kit, so the compiler is on the path. |
| *"Generator NMake Makefiles does not support platform specification"* / *"CMAKE_C_COMPILER not set"* | CMake found no Visual Studio it knows: run from the *Developer PowerShell*, name the generator (`-G "Visual Studio 17 2022"`, or `18 2026` with CMake 4.2+) or use `-G Ninja`, and delete the `build` folder first. |
| Warning *"Compatibility with CMake < 3.10 will be removed"* from `sdl2-config.cmake` | Harmless: SDL2's own CMake file is old-fashioned and CMake 4 says so. The build continues; add `-Wno-deprecated` to the configure command to hide it. |
| *"Could NOT find SDL2 (missing: SDL2_DIR)"* | `CMAKE_PREFIX_PATH` does not point at the extracted **SDL2-devel-...-VC** package (the folder holding `cmake\sdl2-config.cmake`); the plain `SDL2-x.y.z.zip` is the source code. Delete `build` and configure again. |
| *"SDL2.dll was not found"* on launch | Copy `SDL2.dll` from the build folder (or the SDL2 package's `lib\x64\`) next to `techbot.exe`. |
| *"No DN1.EXE, DN2.EXE or DN3.EXE found in ..."* | Wrong `--dir` or starting folder; the v1.0 files of at least one episode are required. |
| Linker errors mentioning `SDL_main` / `WinMain` | Make sure you downloaded the **VC** package for MSVC (the **mingw** package is for MSYS2/GCC), and that it is SDL2, not SDL3. |
