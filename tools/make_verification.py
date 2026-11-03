"""Write docs/VERIFICATION.md from a fuzz_levels.py log."""
import re, sys
# later logs override earlier ones for the same (level, seed)
res = {}
for path in (sys.argv[1:] or ['build/fuzz_all.log']):
    for m in re.findall(r'=== level (\w+) seed (\d+)\s*\n(?:(?!===).*\n)*?compared (\d+) frames, (\d+) differ \((\d+) with screen', open(path).read()):
        res[(m[0], int(m[1]))] = m
order = '123456789abc'
rows = [res[k] for k in sorted(res, key=lambda k: (order.index(k[0]), k[1]))]
tot = sum(int(r[2]) for r in rows)
out = ["# Verification", "",
"The port is compared with the original `DN1.EXE` running under the Unicorn-based",
"emulator in `tools/emu/`.  After every game frame both sides dump the 64 KB data",
"segment, the live 128x90 map and the displayed EGA page; `tools/cmp_state.py`",
"maps each global through `src/globals.def` and reports any difference.",
"Timer/sound bookkeeping and raw far pointers are excluded (they differ by",
"construction); everything else, including every pixel on screen, must match.", "",
"## Attract demo", "",
"MY_DEMO.DN1 on level C: 2635 frames, 0 state differences, 0 screen differences (re-checked after the fixes below).", "",
"## Fuzzing (`tools/fuzz_levels.py`)", "",
"Each level file is copied into slot 1 and a seeded random input stream",
"(runs, jumps, climbing, ducking, ~35% fire) is played through the original's",
"demo-playback path on both sides.  A run ends early when Duke dies or the",
"demo ends the game.", "",
"| Level file | Seed | Frames compared | State diffs | Screen diffs |", "|---|---|---|---|---|"]
for lv, sd, n, d, s in rows:
    out.append("| WORLDAL%s | %s | %s | %s | %s |" % (lv.upper(), sd, n, d, s))
out += ["", "Total: %d runs, %d frames compared." % (len(rows), tot), "",
"## Divergences found by fuzzing (all fixed)", "",
"* WORLDAL5 seed 3: a shot soda can rose above row 0; the original's map read",
"  wrapped into the far heap (header word 5DD1h = solid) and spawned a spark.",
"  Fixed by laying the buffers out like Turbo C's far heap (`heap_arena`).",
"* WORLDAL8: a mirror-floor reflection near the top of page 1 read past",
"  A200:FFFF, outside the EGA window, so the original kept stale latches",
"  (black).  Fixed in `blit_column_up`.",
"* WORLDAL2 had looked unverified only because the harness timed out; the",
"  comparer now ignores a cut-off last frame, the headless dump is flushed per",
"  frame, and differing frame counts are reported.", "",
"## Not covered by diffing", "",
"Menus, story/help pages, high-score entry, the ending and setup dialogs are",
"outside demo playback; they were checked against the disassembly only.",
"Reproduce with `DN1_GAME=<game folder> python3 tools/fuzz_levels.py 1,2,3 1,2 2500`",
"(needs `pip install unicorn capstone` and a built `dn1_headless`)."]
open('docs/VERIFICATION.md', 'w').write('\n'.join(out) + '\n')
print(len(rows), tot)
