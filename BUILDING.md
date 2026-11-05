# Techbot - building and running

*Techbot - An Enhanced PC Port of Duke Nukem* (named after Dr. Proton's robot
henchmen).

There are two programs:

| Program | What it is | Platforms |
|---|---|---|
| `techbot` | the playable game (SDL2 window, sound, keyboard + controller) | Windows, Linux, macOS |
| `dn1_headless` | no window or sound; runs scripted input and dumps every frame, for the verification tools | Linux / WSL only |

Both need **your own copy of the game files** (any of the three episodes).
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
   automatically.  If a configure step fails, delete the `build` folder
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
there simply keep the original.  Original mode, and Enhanced mode with
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

## Linux / WSL / macOS

```sh
# Debian/Ubuntu (incl. WSL); macOS: brew install sdl2
sudo apt install build-essential cmake libsdl2-dev
make                      # builds build/techbot and build/dn1_headless
./build/techbot --dir ~/games/duke1
```

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
