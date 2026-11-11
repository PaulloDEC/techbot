#!/usr/bin/env python3
"""
tile_names.py - Shrapnel's tile names and sets, from the DN1 objects sheet.

usage: python tools/tile_names.py DN1_objects.xlsx

Reads the Tiles sheet (Designation, Picture, Name, Set Name, Kind, ...) and
writes shrapnel/src/tile_names.cpp.  Needs openpyxl (pip).  The tiles are the
same in all three episodes, so one table serves them all.
"""
import os, sys
import openpyxl

wb = openpyxl.load_workbook(sys.argv[1], read_only=True)
ws = wb['Tiles']
rows = []
for r in ws.iter_rows(min_row=2, values_only=True):
    if not r or not r[0] or not r[2]:
        continue
    d = str(r[0]).replace('h', '').replace('H', '')
    a, _, b = d.partition('-')             # an animation: "0500h-0560h", its frames
    for v in range(int(a, 16), int(b or a, 16) + 1, 0x20):
        rows.append((v, str(r[2]).strip(), str(r[3] or '').strip()))
rows.sort()

def c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shrapnel', 'src', 'tile_names.cpp')
with open(out, 'w', newline='\n') as f:
    f.write('/* tile_names.cpp - the map tiles\' names and sets (Paul\'s, from DN1_objects.xlsx).\n'
            ' * Made by tools/tile_names.py: edit the sheet and run that, not this. */\n')
    f.write('#include "shrapnel.h"\n\n')
    f.write('const TileName tile_names[] = {\n')
    for v, n, s in rows:
        f.write('    { 0x%04X, %s, %s },\n' % (v, c(n), c(s)))
    f.write('};\nconst int n_tile_names = %d;\n' % len(rows))
print('%d tiles written to %s' % (len(rows), os.path.normpath(out)))
