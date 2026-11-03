# Toward a modern port: widescreen, high frame rate and other improvements

The decompilation in `src/` is deliberately *faithful*: it reproduces the
original's game state and every rendered pixel, frame for frame (see
`README.md`, "Verification").  This document maps out how to turn it into an
improved PC port without losing that fidelity.

## 1. How the original is structured (what you are changing)

```
game_main            main.c      init, title/menu loop
 └ play_level_loop   loop.c      one iteration = one game frame (~16 fps)
    ├ wait for timer_ticks >= game_speed + 9      (145.15 Hz PIT ISR)
    ├ demo playback/recording of the 6 input flags
    ├ player_update  player.c    physics + draws the map, background objects
    │                            and Duke into the *back page*
    ├ update_scores/debris/dust/explosions/shots/eshots/fallers   core.c
    ├ actor_fns[]    actors*.c   per-type update+draw routines, registered by
    │                            the level loader in map-scan order
    ├ update_sparks, draw_mirrors, ui_frame
    └ page flip (set_draw_page / set_display_page)
```

Simulation and drawing are interleaved in every actor routine (`draw_obj()`
calls sit in the middle of the logic).  All drawing goes through five
primitives in `ega.c`:

| primitive | original | meaning |
|---|---|---|
| `blit_tile(src, dst)` | 0000:02AD | 16x16 tile from the VRAM tile cache to the back page |
| `draw_sprite(gfx, x, y)` | 0000:0424 | masked 16x16 sprite, x in bytes (8 px), y in lines |
| `draw_glyph_masked/opaque` | 0000:04C5/053C | 8x8 font glyphs |
| `blit_column_up` | 0000:0315 | mirror-floor reflection |
| `set_draw_page/set_display_page` | 05EB/05F7 | double buffering |

## 2. Recommended architecture for the port

**Step 1 - capture instead of draw.**  Replace the bodies of the primitives
with *recording*: each call appends `{kind, gfx_id, world_x, world_y}` to a
per-frame display list instead of touching `ega_planes`.  World coordinates
are recoverable because every call site uses `SCREEN_X(x)` / `SCREEN_Y(y)`
(`x - scroll_x`, `((y - scroll_y) >> 3) + 16`); a port-side
`draw_obj_world()` can take the world values directly.  Status-bar and dialog
drawing (fixed screen positions) stay in screen space.

**Step 2 - decouple the clocks.**  Keep `play_level_loop` exactly as it is,
driven by the original timer (145.15 Hz / (game_speed + 9) ≈ 16.1 fps at the
default speed).  Render on the display's refresh instead: keep the last two
display lists and interpolate positions between them using
`alpha = time_since_last_tick / tick_length`.  Match entries between frames
by (actor array, index) - add an `id` field to the display-list entry from
the call site (the actor loops already have the index `i`).

Things that must **not** be interpolated: teleports (`act_tele_a/b`), the
camera snap in `recenter_view()` (it moves 1 tile per frame), death/level
transitions, and anything whose position jumps by more than ~2 tiles.

**Step 3 - widescreen.**  The playfield is 13 x 10 tiles (`draw_world()`
iterates 14 x 10 cells starting at `scroll_x/2, scroll_y`).  To widen:

* draw more map columns in the renderer (tiles are just `map[y*128+x]`, see
  `MAP_AT`); the map is 128 x 90, so there is room on most levels.  Clamp
  extra columns to the map: reading past its edges is not "empty", it wraps
  into the heap (QUIRKS.md);
* the **activity window is gameplay**: `offscreen()` (core.c) decides which
  actors are updated *and* drawn - actors outside the 29-column window are
  frozen.  For an authentic widescreen mode keep `offscreen()` as is for
  logic and add a separate, wider `visible()` test for drawing; for a
  "modern" mode widen `offscreen()` itself (this changes difficulty: enemies
  wake up earlier, fire wheels/robots start patrolling sooner);
* the backdrop (`DROPn.DN1`, 13 x 10 tiles, screen-fixed) must be tiled or
  stretched horizontally;
* the camera logic in `player_update()` keeps Duke between columns 0Bh and
  11h and clamps `scroll_x` to 0..E6h - widen those constants consistently
  with the new view width;
* move the status panel (right-hand 12 columns, `ui_status_panel`) to an
  overlay or a side bar.

**Step 4 - smooth vertical camera.**  `recenter_view()` scrolls 16 px per
frame; with interpolation this already looks smooth.

## Status (port work)

Done: settings file and Original/Enhanced mode, cursor menus and save-slot
picker, Options menu with controller/keyboard remapping, quicksave/quickload,
controller button names in prompts, smooth motion (`port_render.c`: per-frame
display list + map snapshot, interpolated redraw of the play window).
Widescreen (`dl_compose_wide`, `dl_render_wide`): display camera centred on
the game's camera, wider map snapshot, widened `offscreen()` window, actor
sprites outside the original window kept out of EGA memory (status-panel
routines are tagged `dl_hud` so they are never clipped), borders extended,
backdrop stretched on the GPU.  Next: immersive mode (no frame, floating
status panel) can reuse the same composition with a different layout.

## 3. Other improvement hooks

* **Quicksave/quickload**: all game state is in the globals listed in
  `src/globals.def` plus `mapbuf` (the live map, modified by actors) and the
  `actor_fns[]` registry.  (Map writes at out-of-range coordinates can land in
  the sprite buffers after the map - see `heap_arena` in QUIRKS.md - so a
  bit-exact snapshot saves the whole 64 KiB map window, not just 5A00h bytes.)  Serialising those (the test harness already does
  this every frame in `pf_frame_hook`) gives a complete snapshot.
* **Input**: `keyboard_isr()` and `joystick_read()/joystick_buttons()` feed
  six flags (`in_up/down/left/right/fire/jump`).  Rebinding and controller
  support only need to set those.
* **Sound**: `timer_isr()` steps a PIT-divisor list per effect at 145 Hz; an
  enhanced port can render the same divisors with band-limited square waves
  or swap in sampled effects keyed by `snd_current`.
* **Speed**: `game_speed` (SPEED.DN1, ',' and '.' keys) adds timer ticks per
  frame.  A port running the simulation at a fixed 16.1 Hz should keep it as
  an option.

## 4. Behaviour you should *not* "fix" silently

`docs/QUIRKS.md` lists original bugs that the decompilation reproduces on
purpose (they affect gameplay or demo sync).  Fix them behind an option if
desired - the bundled demos only stay in sync with the original behaviour.
