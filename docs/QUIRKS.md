# Original quirks reproduced by the decompilation

These are behaviours of the 1991 code that look like bugs (or odd
choices).  They are kept because the bundled demos, and gameplay feel,
depend on them.  Each is a candidate for an "enhanced mode" switch in a port.

| Where | Quirk |
|---|---|
| `player_update` (0097:1E77) | After landing, the landing check runs twice in the same frame: a second dust puff and landing sound are spawned. |
| `head_clear` (0097:1352) | Bumping your head always plays the bump sound, because the "already hanging" test runs after `player_hanging` was cleared. |
| boxed items, crates (many) | The 500-point "box opened" bonus is only awarded if the box is *not* resting on solid ground at that moment (`!solid_at(x, y+0x80)`). In practice boxes usually rest on the floor, so shooting them rarely scores. |
| `act_ffields_v` (0097:7CFA) | Vertical force-field records are 4 bytes, but the "active" word tested is at +4, i.e. the next record's x. The card reader clears it via the same out-of-record offset. |
| `sound_load_bank` (108E:3A38) | Sound bank B priorities are read from the low byte of each effect's offset field instead of the priority byte. |
| `update_exit_door` (0097:6292) | The door's frame divider increments then resets at 1, so it is always 0 and the door animates every frame. |
| death sequence (0097:F7A4..) | Half of the debris bursts use `0x80 - row + scroll_y` (sign inverted) for their y position. |
| `act_snake` (0097:6947) | The orb animation counter is advanced once per orb per frame (nine times) instead of once per frame. |
| `draw_decor`, molecules | Screen y is computed with an unsigned shift (harmless for in-view objects). |
| `update_elevators` | When an elevator retracts while on screen, its sprite is drawn 16 px higher than normal for that frame (missing +16 offset). |
| `act_signs` | A falling sign in state 1 is drawn twice (normal + 1-px wobble). |
| `ui_wait_cursor` callers | Many dialogs clear `in_right`/`in_down` but not the other direction flags. |
| map reads out of range (`solid_at` etc.) | The map is a far-heap block (`seg:0008`) and offsets wrap at 64 KiB within its segment. Coordinates above the top row (e.g. a shot-up soda can rocketing off the map) therefore read the heap header and the font/object/numbers sprite data that follow the map; header words such as 5DD1h count as solid. The port lays the eight buffers out exactly like Turbo C's far heap (`heap_arena`, `files.c`) to reproduce this. |
| `blit_column_up` (0000:0315), mirror floors | The reflection's source climbs two lines per row; near the top of the view on page 1 (segment A200h) it wraps to A200:FFxx = B1Fxxh, outside the EGA window, so the latches are not reloaded and the rest of the reflection is filled with the last valid read (the dark border): the reflection briefly turns black. |
| `main` | The "asp" command-line switch enables the G+O+D (all items) and G+O+W (warp) cheats; Backspace+PgDn gives keys and firepower without it. |
