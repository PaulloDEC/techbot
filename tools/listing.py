"""Emit annotated listing: build/dn1.asm  (uses names.json for symbols)."""
import sys, os, json
sys.argv = [sys.argv[0]]
exec(open('tools/dn1dis.py').read().split("if __name__ == '__main__':")[0])
NAMES = json.load(open('names.json')) if os.path.exists('names.json') else {}
FN = NAMES.get('funcs', {}); DV = NAMES.get('data', {}); CM = NAMES.get('comments', {})
def fname(seg, off):
    k = '%04x:%04x' % (seg, off)
    return FN.get(k, 'sub_%04x_%04x' % (seg, off))
def dname(off):
    k = '%04x' % off
    return DV.get(k)
def ds_str(off):
    a = DSBASE + off
    if a >= len(img): return None
    e = a
    while e < len(img) and 32 <= img[e] < 127 and e - a < 70: e += 1
    if e - a >= 4 and e < len(img) and img[e] == 0:
        return img[a:e].decode()
    return None
# owner function for each insn
owner = {}
for k, lins in FBODY.items():
    for l in lins:
        owner.setdefault(l, k)
callers = {k: v for k, v in funcs.items()}
import re
def annotate(seg, ins, lin):
    s = ins.mnemonic + ' ' + ins.op_str
    notes = []
    if ins.mnemonic == 'lcall' and lin+3 in RELOCS:
        s = 'call far ' + fname(w16(lin+3), w16(lin+1))
    elif ins.mnemonic == 'call' and ins.operands and ins.operands[0].type == X86_OP_IMM:
        s = 'call ' + fname(seg, ins.operands[0].imm & 0xffff)
    for m in re.finditer(r'\[(0x[0-9a-f]+)\]', ins.op_str):
        v = int(m.group(1), 16)
        n = dname(v)
        if n: notes.append(n)
    for m in re.finditer(r'(?:, |^)(0x[0-9a-f]+)$', ins.op_str):
        v = int(m.group(1), 16)
        st = ds_str(v) if v > 0x100 else None
        if st and ins.mnemonic in ("mov","push") and (lin-1 in insns and insns[lin-1][1].mnemonic=="push" and insns[lin-1][1].op_str=="ds"): pass  # strings deliberately not emitted
        n = dname(v)
        if n and ins.mnemonic in ('mov','push'): notes.append('&' + n)
    c = CM.get('%05x' % lin)
    if c: notes.append(c)
    return s, notes
seg_filter = None
out = open('build/dn1.asm', 'w')
for k in sorted(FBODY, key=lambda k: k[0]*16 + k[1]):
    seg, off = k
    lins = sorted(l for l in FBODY[k] if owner.get(l) == k and l in insns)
    cs = sorted({('%04x:%04x' % c) if isinstance(c[0], int) else c[0] for c in callers.get(k, [])})
    out.write('\n;' + '=' * 70 + '\n; %s  (%04x:%04x lin %05x)  callers: %s\n' % (fname(seg, off), seg, off, seg*16+off, ', '.join(cs[:12])))
    targets = set()
    for l in lins:
        s, ins = insns[l]
        if ins.mnemonic.startswith('j') or ins.mnemonic.startswith('loop'):
            if ins.operands and ins.operands[0].type == X86_OP_IMM: targets.add(ins.operands[0].imm & 0xffff)
    prev = None
    for l in lins:
        s, ins = insns[l]
        if prev is not None and l != prev: out.write('    ; ...\n')
        if ins.address in targets: out.write('  L%04x:\n' % ins.address)
        txt, notes = annotate(seg, ins, l)
        out.write('    %04x  %-40s%s\n' % (ins.address, txt, ('; ' + ' | '.join(notes)) if notes else ''))
        prev = l + ins.size
out.close()
json.dump({'%04x:%04x' % k: sorted('%04x:%04x' % c if isinstance(c[0], int) else c[0] for c in v) for k, v in funcs.items()}, open('build/callers.json', 'w'), indent=0)
print('ok')
