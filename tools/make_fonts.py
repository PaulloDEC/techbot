#!/usr/bin/env python3
"""
make_fonts.py - Shrapnel's interface fonts (shrapnel/third_party/fonts).

usage: python tools/make_fonts.py <work folder>

The work folder holds the two npm packages, unpacked:
    <work>/inter/package        (npm pack @fontsource/inter, then tar xzf)
    <work>/msym/pkg/package     (npm pack material-symbols, then tar xzf)
It writes <work>/fontout/ (the TrueType files and icons.h), then the C arrays
into shrapnel/third_party/fonts.  Needs fontTools and brotli (pip).

To add an icon: add its Material Symbols name to NAMES below (and to FILLED
for a filled one, named ICON_<NAME>_FILLED), run this, rebuild.
"""
import os, shutil, sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

NAMES = ('select open_with magic_button edit ink_eraser diagonal_line rectangle format_color_fill colorize undo redo '
         'play_arrow skip_previous sports_esports expand_more expand_less grid_4x4 person label landscape route brick '
         'texture lock lock_open task_alt add remove drag_indicator search error warning info history close settings '
         'folder_open save palette dark_mode light_mode check chevron_right more_vert visibility zoom_in zoom_out help description folder note_add create_new_folder file_open check_circle stacks build healing').split()
FILLED = 'play_arrow error warning lock'.split()
FILLED_BASE = 0xF8B0            # the filled glyphs' own codepoints

work = sys.argv[1]
out = os.path.join(work, 'fontout') + os.sep
os.makedirs(out, exist_ok=True)
here = os.path.dirname(os.path.abspath(__file__))
dest = os.path.join(here, '..', 'shrapnel', 'third_party', 'fonts')
sym_src = os.path.join(work, 'msym', 'pkg', 'package', 'material-symbols-rounded.woff2')

for w in ('400', '600'):
    f = TTFont(os.path.join(work, 'inter', 'package', 'files', 'inter-latin-%s-normal.woff2' % w))
    f.flavor = None
    f.save(out + 'inter-%s.ttf' % w)

cm = TTFont(sym_src).getBestCmap()
inv = {}
for k, v in cm.items():
    inv.setdefault(v, k)
missing = [n for n in NAMES if n not in inv]
if missing:
    sys.exit('not in Material Symbols: ' + ' '.join(missing))
cps = {n: inv[n] for n in NAMES}

def make(fill, keep, path):
    f = TTFont(sym_src)
    f.flavor = None
    f = instancer.instantiateVariableFont(f, {'FILL': fill, 'wght': 400, 'GRAD': 0, 'opsz': 20})
    opt = subset.Options()
    opt.layout_features = []
    opt.name_IDs = ['*']
    opt.notdef_outline = True
    s = subset.Subsetter(opt)
    s.populate(unicodes=[cps[n] for n in keep])
    s.subset(f)
    f.save(path)

make(0, NAMES, out + 'symbols.ttf')
make(1, FILLED, out + 'symbols-filled.ttf')
used = set(cps.values())
remap = {}
for i, n in enumerate(FILLED):
    assert FILLED_BASE + i not in used
    remap[n] = FILLED_BASE + i
f = TTFont(out + 'symbols-filled.ttf')
for t in f['cmap'].tables:
    t.cmap = {remap[n]: t.cmap[cps[n]] for n in FILLED if cps[n] in t.cmap}
f.save(out + 'symbols-filled.ttf')

def u8(c):
    return ''.join('\\x%02X' % b for b in chr(c).encode('utf-8'))
allcp = list(used) + list(remap.values())
with open(out + 'icons.h', 'w') as h:
    h.write('/* Material Symbols Rounded (Google, Apache 2.0): the codepoints Shrapnel uses, as UTF-8 */\n#pragma once\n')
    for n in NAMES:
        h.write('#define ICON_%s "%s"\n' % (n.upper(), u8(cps[n])))
    for n in FILLED:
        h.write('#define ICON_%s_FILLED "%s"\n' % (n.upper(), u8(remap[n])))
    h.write('#define ICON_MIN 0x%04X\n#define ICON_MAX 0x%04X\n' % (min(allcp), max(allcp)))

def hdr(src, dst, name, note):
    b = open(src, 'rb').read()
    with open(dst, 'w') as f:
        f.write('/* %s */\n' % note)
        f.write('static const unsigned int %s_size = %d;\n' % (name, len(b)))
        f.write('static const unsigned char %s[%d] = {\n' % (name, len(b)))
        for i in range(0, len(b), 20):
            f.write(','.join(str(x) for x in b[i:i + 20]) + ',\n')
        f.write('};\n')

hdr(out + 'inter-400.ttf', os.path.join(dest, 'inter_400.h'), 'inter_400_ttf',
    'Inter Regular (latin), The Inter Project Authors, SIL Open Font License 1.1 (OFL.txt)')
hdr(out + 'inter-600.ttf', os.path.join(dest, 'inter_600.h'), 'inter_600_ttf',
    'Inter SemiBold (latin), The Inter Project Authors, SIL Open Font License 1.1 (OFL.txt)')
hdr(out + 'symbols.ttf', os.path.join(dest, 'symbols.h'), 'symbols_ttf',
    'Material Symbols Rounded (Google), Apache License 2.0 (LICENSE-material-symbols.txt): the glyphs Shrapnel uses')
hdr(out + 'symbols-filled.ttf', os.path.join(dest, 'symbols_filled.h'), 'symbols_filled_ttf',
    'Material Symbols Rounded, filled (Google), Apache License 2.0: a few glyphs, moved to F8B0h on')
shutil.copy(out + 'icons.h', os.path.join(dest, 'icons.h'))
print('fonts written to', os.path.normpath(dest))
