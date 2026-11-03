import re, collections, json
src = open('build/lifted_all.c').read()
scal = collections.defaultdict(set)   # addr -> sizes
for m in re.finditer(r'\b([gb])([0-9a-f]{4})\b', src):
    scal[int(m.group(2), 16)].add('w' if m.group(1) == 'g' else 'b')
arrays = {}
# patterns: ((gCCCC * S) + BASE)  or  ((((gCCCC << 1) << 1) << 1) + BASE)  or ((gCCCC<<1)+BASE)  with si/di/loc idx too
pat = re.compile(r'DS_u(8|16)\[\(*\(?(\w+) \* (0x[0-9a-f]+|\d+)\) \+ (0x[0-9a-f]+)\)')
for m in pat.finditer(src):
    arrays.setdefault(int(m.group(4), 16), set()).add(int(m.group(3), 0))
pat2 = re.compile(r'DS_u(8|16)\[\(*(\w+)((?: << 1\)?)+) \+ (0x[0-9a-f]+)\]')
for m in re.finditer(r'DS_u(8|16)\[(\(*)(\w+)((?:\s*<<\s*1\)*)*)\s*\+\s*(0x[0-9a-f]+)\)?', src):
    n = m.group(4).count('<<')
    arrays.setdefault(int(m.group(5), 16), set()).add(1 << n)
for m in re.finditer(r'DS_u(8|16)\[(\w+) \+ (0x[0-9a-f]+)\]', src):
    arrays.setdefault(int(m.group(3), 16), set()).add(1 if m.group(1) == '8' else 0)
allk = sorted(set(scal) | set(arrays))
for k in allk:
    print('%04x' % k, 'scalar:' + ''.join(sorted(scal.get(k, ''))) if k in scal else '', 'array stride:' + ','.join(map(str, sorted(arrays[k]))) if k in arrays else '')
