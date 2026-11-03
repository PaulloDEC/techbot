# File formats and in-memory layouts (as used by DN1.EXE v1.0)

## Coordinates
* World x: 8-pixel columns. A map cell in column `c` sits at x = `2c + 2`
  (actors are one tile right of the map; `MAP_AT(x,y) = map[y + x/2 - 1]`).
* World y: 1/8 pixel = the map word offset of the row (`row * 128`).
  A tile row is 0x80, half a tile 0x40.
* Screen: `sx = x - scroll_x` (bytes), `sy = ((y - scroll_y) >> 3) + 16` (lines).
  The play window is 13x10 tiles at byte column 2, line 16; the right 12
  columns are the status panel.
* Duke is special: `player_x` is a screen column, `player_y` a screen line.

## WORLDALx.DN1 (levels)
128 x 90 little-endian words (23040 bytes).  Values < 3000h are tile-cache
byte offsets (tile index = value / 32): 0 = backdrop, 20h = second backdrop,
21h..5DFh animated tiles (4 frames, 20h apart), 1800h.. solid tiles
(> 17FFh is solid; 1800h is the destructible brick).  3000h..3059h are actor
markers consumed by the loader (`spawn_level_actors`, see `game.c` for the
complete table: what each marker spawns and which neighbour cell replaces it).

## Tile files (BACKn, SOLIDn, BORDER, DROPn)
3-byte header, then for each 8 pixels: mask, blue, green, red, intensity.
Loaded into the EGA tile cache at A000:4000 + offset (back0 0000, back1 0600,
back2 0C00, back3 1200, solid0 1800, solid1 1E00, solid2 2400, solid3 2A00,
border 3000; the two level backdrops at 4000 and 8100, 130 tiles each).
The loader always reads a fixed budget, so data past EOF loads as FFh.

## Sprite files (ANIMn, OBJECTn, MANn, NUMBERS, FONTn)
Same 5-plane format, 160 bytes per 16x16 sprite (40 per 8x8 glyph), kept in
RAM: anim0..5 contiguous (1E00h each), object0..2 (1F40h each), man0..3.
Code refers to sprites by byte offset into these buffers (`anim_gfx + off`).

## Full-screen pictures (DN, END, BADGUY, DUKE, CREDITS)
32000 bytes: four 8000-byte planes of a 320x200 image.

## DROPn.DN1
130 tiles (13x10) in tile-file format; drawn screen-fixed.

## Sound banks (DUKE1.DN1, DUKE1-B.DN1)
16-byte header, then 24 16-byte entries `{u16 offset, u8 priority, ...}`;
each effect is a list of PIT divisors played at 145 Hz, 0 = silence, FFFFh =
end.  (Bank B's priorities are effectively the low byte of the offset.)

## KEYS.DN1
6 scancodes: up, down, left, right, fire, jump.

## USERDEMO.DN1 / MY_DEMO.DN1
u16 length in bytes, then 6 bytes per frame (up, down, left, right, fire,
jump), up to 4268h bytes.  USERDEMO is recorded automatically during every
new game; MY_DEMO plays in attract mode on level C (index 20).

## SAVEDx.DN1
22 little-endian words (scroll x/y, player x/y, level, firepower, boots,
claws, health, robohand, 12 hint flags) then the score as a decimal line.

## HIGHS.DN1
10 x ("%ld\n" score, "%s\n" name).

## SPEED.DN1
One word: extra timer ticks per frame (0 = fastest).

## Timing
The PIT is reprogrammed to 1193182/8273 ≈ 144.2 Hz.  A frame waits for
`game_speed + 9` ticks (≈ 16 fps at speed 0).  Sound effects advance one
divisor per tick.
