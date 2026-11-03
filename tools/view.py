"""Condensed view of functions from build/dn1.asm. usage: view.py seg_off [...]"""
import re, sys, json, os
NAMES = json.load(open('names.json')) if os.path.exists('names.json') else {'funcs':{},'data':{}}
txt = open('build/dn1.asm').read().split('\n;' + '=' * 70 + '\n')
idx = {}
for blk in txt:
    m = re.match(r'; (\S+)\s+\((\w+):(\w+)', blk)
    if m: idx[m.group(2) + '_' + m.group(3)] = blk
DN = NAMES.get('data', {})
def dsub(s):
    def r(m):
        v = m.group(1); n = DN.get('%04x' % int(v, 16))
        return '[' + (n if n else v) + ']'
    s = re.sub(r'\[(0x[0-9a-f]+)\]', r, s)
    s = s.replace('word ptr ', 'w').replace('byte ptr ', 'b').replace('dword ptr ', 'd').replace('ptr ', '')
    return s
def condense(blk):
    lines = blk.split('\n')
    out = [lines[0]]
    body = []
    for l in lines[1:]:
        m = re.match(r'\s+([0-9a-f]{4})  (.*?)\s*(; .*)?$', l)
        if m: body.append([m.group(1), m.group(2).strip(), (m.group(3) or '')[2:]]); continue
        m = re.match(r'\s+L([0-9a-f]{4}):', l)
        if m: body.append(['L', m.group(1), '']); continue
    i = 0; res = []
    while i < len(body):
        a, ins, c = body[i]
        if a == 'L': res.append('L%s:' % ins); i += 1; continue
        nxt = body[i+1] if i + 1 < len(body) else None
        if ins in ('inc sp', 'push cs') : i += 1; continue
        if re.match(r'add sp, \d', ins) and res and 'call' in res[-1]: i += 1; continue
        if re.match(r'mov (ax|dx), (0x[0-9a-f]+|\d+)$', ins) and nxt and nxt[1] == 'push ' + ins[4:6]:
            res.append('  push ' + ins.split(', ')[1] + (('   ; ' + c) if c else '')); i += 2; continue
        if ins in ('xor ax, ax',) and nxt and nxt[1] == 'push ax':
            res.append('  push 0'); i += 2; continue
        res.append('  ' + dsub(ins) + (('   ; ' + c) if c else ''))
        i += 1
    return '\n'.join(out + res)
for a in sys.argv[1:]:
    print(condense(idx[a])); print()
