"""Recursive-descent disassembler for the unpacked DN1 image (Turbo C, large model)."""
import struct, json, sys, re, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

EXE = sys.argv[1] if len(sys.argv) > 1 else 'build/dn1_unpacked.exe'
d = open(EXE, 'rb').read()
hdr = struct.unpack('<H', d[8:10])[0] * 16
nrel = struct.unpack('<H', d[6:8])[0]
img = d[hdr:]
RELOCS = set()
for i in range(nrel):
    o, s = struct.unpack('<HH', d[0x1c + i*4:0x20 + i*4])
    RELOCS.add(s*16 + o)
DSEG = 0x17e3
DSBASE = DSEG * 16
CODESEGS = sorted({struct.unpack('<H', img[r:r+2])[0] for r in RELOCS} - {DSEG})
CODESEGS = [s for s in CODESEGS if s < DSEG]
def seg_end(s):
    i = CODESEGS.index(s)
    return (CODESEGS[i+1]*16 + 16) if i+1 < len(CODESEGS) else DSBASE

md = Cs(CS_ARCH_X86, CS_MODE_16); md.detail = True
w16 = lambda a: struct.unpack('<H', img[a:a+2])[0]

funcs = {}          # (seg,off) -> set of callers
insns = {}          # linear -> insn
labels = {}         # linear -> name
jtables = {}        # linear table addr -> (seg, count)
farptrs = []
work = collections.deque()

def add_func(seg, off, caller=None):
    k = (seg, off)
    if k not in funcs:
        funcs[k] = set(); work.append(k)
    if caller is not None: funcs[k].add(caller)

def body(seg, off):
    """Disassemble a function by flow; returns set of linear addrs."""
    base = seg*16; end = seg_end(seg)
    todo = [off]; seen = set()
    while todo:
        o = todo.pop()
        while True:
            lin = base + o
            if lin in seen or lin >= end: break
            seen.add(lin)
            try:
                ins = next(md.disasm(img[lin:lin+16], o))
            except StopIteration:
                break
            insns[lin] = (seg, ins)
            m = ins.mnemonic; nxt = o + ins.size
            ops = ins.operands
            if m == 'lcall' and lin+3 in RELOCS:
                add_func(w16(lin+3), w16(lin+1), (seg, off))
            elif m == 'call' and ops and ops[0].type == X86_OP_IMM:
                tgt = ops[0].imm & 0xffff
                add_func(seg, tgt, (seg, off))
            elif m in ('jmp',) and ops and ops[0].type == X86_OP_IMM:
                todo.append(ops[0].imm & 0xffff); break
            elif m.startswith('j') or m in ('loop', 'loope', 'loopne', 'jcxz'):
                if ops and ops[0].type == X86_OP_IMM:
                    todo.append(ops[0].imm & 0xffff)
                if m == 'jmp':
                    # indirect: jump table cs:[bx+tab]
                    op = ops[0]
                    if op.type == X86_OP_MEM and ins.bytes[0] == 0x2e and op.mem.disp:
                        tab = base + (op.mem.disp & 0xffff)
                        n = guess_table(seg, tab, ins, lin)
                        jtables[tab] = (seg, n)
                        for i in range(n):
                            todo.append(w16(tab + 2*i))
                    break
            if m in ('ret', 'retf', 'iret', 'hlt'): break
            if m == 'ljmp': break
            o = nxt
    return seen

def guess_table(seg, tab, ins, lin):
    # look back for "cmp bx, N ; ja" or "mov cx, N" (Turbo C value-table switch)
    for back in range(3, 40):
        a = lin - back
        if a in insns:
            s, p = insns[a]
            if p.mnemonic == 'cmp' and (p.op_str.startswith('bx, ') or p.op_str.startswith('ax, ')):
                try: return int(p.op_str.split(', ')[1], 0) + 1
                except: pass
            if p.mnemonic == 'mov' and p.op_str.startswith('cx, '):
                try: return int(p.op_str.split(', ')[1], 0)
                except: pass
    return 1

entry = (0, 0)
add_func(0, 0)
# also every relocated far pointer referencing a code segment in data (function pointer tables)
for r in sorted(RELOCS):
    s = w16(r)
    if s in CODESEGS and r >= DSBASE:
        off = w16(r-2)
        farptrs.append((r-2, s, off))
        add_func(s, off, ('data', r-2-DSBASE))
# seed: any lcall in any code segment with reloc
for r in RELOCS:
    if r < DSBASE and r >= 3 and img[r-3] == 0x9a:
        add_func(w16(r), w16(r-2))

FBODY = {}
def drain():
    while work:
        seg, off = work.popleft()
        FBODY[(seg, off)] = body(seg, off)
drain()
# far function pointers built at runtime: "mov reg, OFFSET" next to a relocated "mov reg, SEG"
CODEREF = []
for r in sorted(RELOCS):
    if r >= DSBASE: continue
    s = w16(r)
    if s not in CODESEGS or s == 0: continue
    if r >= 3 and img[r-3] == 0x9a: continue
    # scan back up to 12 bytes for an insn 'mov r16, imm' (B8..BF) or 'mov word [..], imm' (C7 06)
    for back in range(3, 14):
        a = r - back
        if 0xb8 <= img[a] <= 0xbf and a+1 != r:
            off = w16(a+1); CODEREF.append((r, s, off)); add_func(s, off, ('ref', r)); break
        if img[a] == 0xc7 and img[a+1] in (0x06,0x46,0x47,0x87,0x86,0x44,0x45,0x05,0x07,0x04):
            pass
drain()
# prologue scan in uncovered regions of the big code segments
def covered():
    cov = bytearray(len(img))
    for lin,(s,ins) in insns.items():
        cov[lin:lin+ins.size] = b'\1'*ins.size
    return cov
for _ in range(4):
    cov = covered(); found = 0
    for s in CODESEGS:
        a0 = s*16; e = seg_end(s)
        for x in range(a0, e-3):
            if not cov[x] and img[x:x+3] == b'\x55\x8b\xec' and (x == a0 or cov[x-1] or img[x-1] in (0xcb,0xc3,0x90)):
                add_func(s, x-a0, ('scan', 0)); found += 1
    drain()
    if not found: break
# frameless functions: uncovered code right after a terminating instruction
ENDERS = ('ret','retf','iret','jmp','ljmp')
for _ in range(6):
    cov = covered()
    for t,(s,n) in jtables.items(): cov[t:t+2*n] = b'\1'*(2*n)
    found = 0
    ends = {lin+ins.size for lin,(s,ins) in insns.items() if ins.mnemonic in ENDERS}
    for x in sorted(ends):
        y = x
        while y < len(img) and img[y] == 0x90 and not cov[y]: y += 1
        if y >= DSBASE or cov[y]: continue
        s = max(c for c in CODESEGS if c*16 <= y)
        if y >= seg_end(s) - 2: continue
        if img[y] == 0 and img[y+1] == 0: continue
        add_func(s, y - s*16, ('tail', 0)); found += 1
    drain()
    if not found: break

# Turbo-C value switch tables: "mov cx,N ; mov bx, offset ; cs: mov ax,[bx]" -> table of N values then N targets
if __name__ == '__main__':
    print('functions', len(funcs), 'insns', len(insns), 'jtables', len(jtables), 'farptrs', len(farptrs))
    bys = collections.Counter(s for s, o in funcs)
    for s in CODESEGS: print(hex(s), hex(seg_end(s) - s*16), bys[s])
