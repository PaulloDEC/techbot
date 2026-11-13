#!/usr/bin/env python3
"""
prefab_pack.py - the prefabs that come with Shrapnel, from .prefab files.

usage: python tools/prefab_pack.py <game folder> <prefab files or folders...>

Writes shrapnel/src/prefab_builtin.cpp.  A prefab cut from one of the game's
own levels (its file has a "from = WORLDALn.DNe c r" line, which Shrapnel
writes when the cells match a game level there) is stored only as where to
cut it: the cells are checked against the game folder given here, and
Shrapnel reads them from the player's own game files when it starts.  A
prefab drawn from scratch (no "from" line) is stored with its cells.

Nothing of the game itself goes into the source this way.
"""
import os, struct, sys

def parse(path):
    name, frm, rows = None, None, []
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith(';') or '=' not in line:
            continue
        k, v = [t.strip() for t in line.split('=', 1)]
        if k == 'name': name = v
        elif k == 'from': frm = v
        elif k == 'row': rows.append(v.split())
    if not rows:
        raise SystemExit('%s: no rows' % path)
    w = len(rows[0])
    if any(len(r) != w for r in rows):
        raise SystemExit('%s: rows of different lengths' % path)
    return name or os.path.splitext(os.path.basename(path))[0], frm, rows

def find_ci(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    return None

def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

game = sys.argv[1]
files = []
for a in sys.argv[2:]:
    if os.path.isdir(a):
        files += [os.path.join(a, f) for f in sorted(os.listdir(a)) if f.lower().endswith('.prefab')]
    else:
        files.append(a)

out_cells, entries = [], []
for path in files:
    name, frm, rows = parse(path)
    h, w = len(rows), len(rows[0])
    mask = ''.join('.' if t.startswith('.') else '#' for r in rows for t in r)
    cells = [0 if t.startswith('.') else int(t, 16) for r in rows for t in r]
    if frm:
        f, c, r = frm.split()
        c, r = int(c), int(r)
        mp = find_ci(game, f)
        if not mp:
            raise SystemExit('%s: %s is not in %s' % (path, f, game))
        m = struct.unpack('<11520H', open(mp, 'rb').read()[:23040])
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if mask[i] == '#' and m[(r + y) * 128 + c + x] != cells[i]:
                    raise SystemExit('%s: cell %d,%d differs from %s (edited after cutting? drop the "from" line to ship its cells)'
                                     % (path, x, y, f))
        entries.append('    { %s, %s, %d, %d, %d, %d, %s, NULL },' % (c_str(name), c_str(f.upper()), c, r, w, h, c_str(mask)))
    else:
        k = len(out_cells)
        out_cells.append('static const uint16_t cells%d[] = { %s };' % (k, ', '.join('0x%04X' % v for v in cells)))
        entries.append('    { %s, NULL, 0, 0, %d, %d, %s, cells%d },' % (c_str(name), w, h, c_str(mask), k))
    print('%-40s %s' % (name, 'cut from ' + frm if frm else 'its own cells'))

dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shrapnel', 'src', 'prefab_builtin.cpp')
with open(dst, 'w', newline='\n') as o:
    o.write('/* prefab_builtin.cpp - the prefabs that come with Shrapnel.\n'
            ' * Made by tools/prefab_pack.py: edit the .prefab files and run that, not this.\n'
            ' * Ones cut from the game\'s levels hold only where to cut them (prefabs.h). */\n')
    o.write('#include "prefabs.h"\n\n')
    for l in out_cells:
        o.write(l + '\n')
    o.write('\nconst PrefabBuiltin prefab_builtin[] = {\n')
    for e in entries:
        o.write(e + '\n')
    if not entries:
        o.write('    { NULL, NULL, 0, 0, 0, 0, NULL, NULL },\n')
    o.write('};\nconst int n_prefab_builtin = %d;\n' % len(entries))
print('%d prefabs written to %s' % (len(entries), os.path.normpath(dst)))
