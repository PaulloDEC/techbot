# Verification

The port is compared with the original `DN1.EXE` running under the Unicorn-based
emulator in `tools/emu/`.  After every game frame both sides dump the 64 KB data
segment, the live 128x90 map and the displayed EGA page; `tools/cmp_state.py`
maps each global through `src/globals.def` and reports any difference.
Timer/sound bookkeeping and raw far pointers are excluded (they differ by
construction); everything else, including every pixel on screen, must match.

## Attract demo

MY_DEMO.DN1 on level C: 2635 frames, 0 state differences, 0 screen differences (re-checked after the fixes below).

## Fuzzing (`tools/fuzz_levels.py`)

Each level file is copied into slot 1 and a seeded random input stream
(runs, jumps, climbing, ducking, ~35% fire) is played through the original's
demo-playback path on both sides.  A run ends early when Duke dies or the
demo ends the game.

| Level file | Seed | Frames compared | State diffs | Screen diffs |
|---|---|---|---|---|
| WORLDAL1 | 1 | 2500 | 0 | 0 |
| WORLDAL1 | 2 | 677 | 0 | 0 |
| WORLDAL1 | 3 | 517 | 0 | 0 |
| WORLDAL2 | 1 | 2193 | 0 | 0 |
| WORLDAL2 | 2 | 10 | 0 | 0 |
| WORLDAL2 | 3 | 188 | 0 | 0 |
| WORLDAL3 | 1 | 2500 | 0 | 0 |
| WORLDAL3 | 2 | 2500 | 0 | 0 |
| WORLDAL3 | 3 | 2500 | 0 | 0 |
| WORLDAL4 | 1 | 2500 | 0 | 0 |
| WORLDAL4 | 2 | 2500 | 0 | 0 |
| WORLDAL4 | 3 | 2500 | 0 | 0 |
| WORLDAL5 | 1 | 2500 | 0 | 0 |
| WORLDAL5 | 2 | 2500 | 0 | 0 |
| WORLDAL5 | 3 | 936 | 0 | 0 |
| WORLDAL6 | 1 | 956 | 0 | 0 |
| WORLDAL6 | 2 | 677 | 0 | 0 |
| WORLDAL6 | 3 | 1021 | 0 | 0 |
| WORLDAL7 | 1 | 294 | 0 | 0 |
| WORLDAL7 | 2 | 462 | 0 | 0 |
| WORLDAL7 | 3 | 940 | 0 | 0 |
| WORLDAL8 | 1 | 2500 | 0 | 0 |
| WORLDAL8 | 2 | 2500 | 0 | 0 |
| WORLDAL8 | 3 | 2500 | 0 | 0 |
| WORLDAL9 | 1 | 2500 | 0 | 0 |
| WORLDAL9 | 2 | 2500 | 0 | 0 |
| WORLDAL9 | 3 | 2500 | 0 | 0 |
| WORLDALA | 1 | 2500 | 0 | 0 |
| WORLDALA | 2 | 2500 | 0 | 0 |
| WORLDALA | 3 | 2500 | 0 | 0 |
| WORLDALB | 1 | 2500 | 0 | 0 |
| WORLDALB | 2 | 2500 | 0 | 0 |
| WORLDALB | 3 | 2500 | 0 | 0 |
| WORLDALC | 1 | 2500 | 0 | 0 |
| WORLDALC | 2 | 2500 | 0 | 0 |
| WORLDALC | 3 | 2500 | 0 | 0 |

Total: 36 runs, 68871 frames compared.

## Divergences found by fuzzing (all fixed)

* WORLDAL5 seed 3: a shot soda can rose above row 0; the original's map read
  wrapped into the far heap (header word 5DD1h = solid) and spawned a spark.
  Fixed by laying the buffers out like Turbo C's far heap (`heap_arena`).
* WORLDAL8: a mirror-floor reflection near the top of page 1 read past
  A200:FFFF, outside the EGA window, so the original kept stale latches
  (black).  Fixed in `blit_column_up`.
* WORLDAL2 had looked unverified only because the harness timed out; the
  comparer now ignores a cut-off last frame, the headless dump is flushed per
  frame, and differing frame counts are reported.

## Not covered by diffing

Menus, story/help pages, high-score entry, the ending and setup dialogs are
outside demo playback; they were checked against the disassembly only.
Reproduce with `DN1_GAME=<game folder> python3 tools/fuzz_levels.py 1,2,3 1,2 2500`
(needs `pip install unicorn capstone` and a built `dn1_headless`).

## Quicksave round trip

Run A plays scripted input and quicksaves; run B plays the same input,
then different input, then quickloads.  With no input after the save (A) and
after the load (B), all 860 frames after the load are byte-identical to the
frames after the save (globals, map and screen).  The only difference ever
seen is `last_scancode` (F5 vs F9 released), i.e. the physical key, not game
state.  Port features are off in the verification build (no settings file),
and the attract demo is unchanged by them.

## Smooth motion

`dn1_headless --dl-test` redraws the play window from the recorded frames
after every game frame and compares it with the original's own image: at
the end of each step (alpha ~1) all smoothed frames are pixel-identical to
the exact frame (attract demo: 649 of 649; scripted normal play: 1998 of 2000
frames smoothed, all identical; the other two have no previous frame or show
a dialog).  With smoothing off (the verification build) the attract demo is
unchanged.

Object integrity: many objects (Duke included) are 2x2 blocks of 16x16
pieces.  The test build checks that every block's pieces are matched to the
same block, in the same order, in the previous frame; otherwise the object
would tear while gliding (seen when Duke fell 16 px per update with the camera
following and nearest-distance matching paired his top pieces with his old
bottom ones).  With the order-preserving alignment: about 17,000 blocks over
six levels and two input scripts, none torn (the old matcher tore 25 of them
in one level-1 run).

## Widescreen

`dn1_headless --wide W` composes the widescreen picture after every frame;
`WIDE_CHECK=1` compares the part of the wide play area where the original
view sits with the original's own window: over 800 frames all differences
(4476 px in 59 frames) lie within 16 px of the view's left/right edge - actor
sprites straddling the original window edge, which widescreen draws in full
and keeps out of EGA memory (that memory still supplies the borders and the
status panel).  With widescreen off, the attract demo is unchanged.

## Sound

The Enhanced mixer (`port_sound.c`) only listens: `play_sound()` hands each
effect to it before the original priority logic runs unchanged, and the
mixer's output replaces the speaker's in the audio callback.  A build with
the two hooks removed and the normal build produce byte-identical dumps over
2700 attract-demo frames.

## Mirror floors near the top of the window

A mirror floor high in the play window reflects lines from above the
window.  The original then copies the top border and, further up, bytes from
outside the display page (black on one page, stale latches on the other).
The smooth/wide renderer reflects the world above instead (it draws up to 64
rows above the window for this), and frames with such a mirror are always
drawn by the renderer rather than switching to the original's frame at the
end of a step, which made the mirror flicker.  Those frames are the only
end-of-step differences in the `--dl-test` runs (level 8: 10 frames; levels
1, 3, 5: none).  Original mode is unaffected; attract demo identical.

## Episodes 2 and 3

`DN2.EXE` and `DN3.EXE` are the same program as `DN1.EXE` built with
different text.  Aligning the code segments instruction by instruction (with
operands masked), 99.9% (DN2) and 99.5% (DN3) of the instructions match; all
of the game module (player, actors, collision, rendering, sound, saves,
demos) is identical.  The differences:

* data addresses: text of different lengths moves the data behind it.
  `tools/episode_map.py` derives every moved offset the code refers to from
  the matched instructions themselves (737 in DN2, 721 in DN3, no
  conflicts) plus the unchanged runs of the data segment, and writes
  `src/episode_map.c`; `ds_str()` and friends translate DN1 offsets through
  it.  Uninitialised data moves by +216 (DN2) / -276 (DN3) bytes, the far
  heap by +12h / -0Bh paragraphs (`HEAP_MAP_SEG`).
* the per-level hint texts, level and backdrop file names are tables of far
  pointers at the same offsets in all three EXEs, pointing into each
  episode's own text: they need no translation.
* `hints_on` starts at 0 in DN2/DN3 (1 in DN1); it is read from the EXE.
* routines that draw a different number of lines or at other rows - the
  episode introduction, the two new-game dialogs, the five ending dialogs,
  the final congratulations screen (DN3) - and DN3's title routine, which
  does not clear the page: given per episode in `ui_dialogs.c` from that
  episode's code.  The keyboard-redefinition routine also differs slightly;
  the port replaces it with its own screen.

Verification, the original EXE under the emulator vs the port, game state
and screen every frame:

* attract demo (shared by both episodes): DN2 700 frames, DN3 700 frames -
  0 differences;
* every level (1-C) with random input, seed 1, 1500 frames: DN2 12 levels,
  16,072 frames; DN3 12 levels, 13,314 frames - 0 differences (shorter runs
  end where Duke dies and the game waits for a key).
* the same with seed 2: DN2 15,770 frames, DN3 12,428 frames - 0 differences.
* Episode 1 is unaffected: the attract demo is byte-identical to the build
  before episode support.
