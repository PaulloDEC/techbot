"""Compare per-frame state dumps: original (emulator) vs C port (headless)."""
import re, sys, struct
SIZES = {'int16_t': 2, 'uint16_t': 2, 'uint8_t': 1, 'int32_t': 4, 'Obj4': 4, 'Obj6': 6, 'Obj8': 8,
         'Obj12': 12, 'Conveyor': 10, 'EShot': 8, 'Debris': 30, 'FField': 6}
IGNORE = {'timer_ticks', 'snd_isr_div', 'snd_start', 'snd_current', 'snd_playing', 'snd_prio_cur'}
vars_ = []
for l in open(sys.argv[3] if len(sys.argv) > 3 else 'src/globals.def'):
    m = re.match(r'(GA?)\s*\(\s*(\w+),\s*(\w+),\s*(0x[0-9a-f]+)(?:,\s*(\d+))?', l)
    if m:
        t, name, off = m.group(2), m.group(3), int(m.group(4), 16)
        n = int(m.group(5)) if m.group(5) else 1
        if name in IGNORE or name.startswith('ptr_'): continue
        vars_.append((name, off, SIZES[t], n))
FR = 8 + 0x10000 + 0x5a00
def frames(path):
    with open(path, 'rb') as f:
        while True:
            h = f.read(8)
            if len(h) < 8: return
            ds = f.read(0x10000); mp = f.read(0x5a00); vr = f.read(32000)
            if len(vr) < 32000: return      # run was cut off mid-frame
            yield struct.unpack('<I', h[4:])[0], ds, mp, vr
def count(path):
    import os
    return os.path.getsize(path) // (8 + 0x10000 + 0x5a00 + 32000)
a = frames(sys.argv[1]); b = frames(sys.argv[2])
nfr = 0; bad = 0
pixbad = 0
for (fa, dsa, mpa, vra), (fb, dsb, mpb, vrb) in zip(a, b):
    diffs = []
    if vra != vrb:
        pixbad += 1
        k = next(i for i in range(len(vra)) if vra[i] != vrb[i])
        diffs.append('screen differs (plane %d, line %d, byte col %d)' % (k // 8000, (k % 8000) // 40, k % 40))
    for name, off, sz, n in vars_:
        x = dsa[off:off + sz * n]; y = dsb[off:off + sz * n]
        if x != y:
            if n == 1:
                diffs.append('%s: orig=%s port=%s' % (name, x.hex(), y.hex()))
            else:
                for i in range(n):
                    if x[i*sz:(i+1)*sz] != y[i*sz:(i+1)*sz]:
                        diffs.append('%s[%d]: orig=%s port=%s' % (name, i, x[i*sz:(i+1)*sz].hex(), y[i*sz:(i+1)*sz].hex()))
                        if len(diffs) > 12: break
    if mpa != mpb:
        k = next(i for i in range(0, 0x5a00, 2) if mpa[i:i+2] != mpb[i:i+2])
        diffs.append('map cell row %d col %d: orig=%s port=%s' % (k // 256, (k % 256) // 2, mpa[k:k+2].hex(), mpb[k:k+2].hex()))
    nfr += 1
    if diffs:
        bad += 1
        if bad <= int(sys.argv[4]) if len(sys.argv) > 4 else bad <= 3:
            print('frame %d: %d differences' % (fa, len(diffs)))
            for d in diffs[:25]: print('   ', d)
na, nb = count(sys.argv[1]), count(sys.argv[2])
if na != nb:
    # zip() stops at the shorter run: say so, a different stopping point is itself a divergence
    print('frame counts differ: original %d, port %d' % (na, nb))
print('compared %d frames, %d differ (%d with screen differences)' % (nfr, bad, pixbad))
