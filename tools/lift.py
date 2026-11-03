"""
Tiny symbolic lifter: Turbo C 2.0 (16-bit, large model) asm -> pseudo-C with gotos.
Heuristic and lossy, but it collapses register shuffling into expressions so that
hand-decompilation is fast. Usage: lift.py SEG_OFF [...]
"""
import sys, os, json, re
sys.argv, ARGS = [sys.argv[0]], sys.argv[1:]
exec(open(os.path.join(os.path.dirname(__file__), 'dn1dis.py')).read().split("if __name__ == '__main__':")[0])
from capstone.x86 import X86_OP_REG, X86_OP_IMM, X86_OP_MEM

NAMES = json.load(open('names.json')) if os.path.exists('names.json') else {}
FN = NAMES.get('funcs', {}); DV = NAMES.get('data', {})
owner = {}
for k, lins in FBODY.items():
    for l in lins: owner.setdefault(l, k)

def fname(seg, off):
    return FN.get('%04x:%04x' % (seg, off), 'sub_%04x_%04x' % (seg, off))

# calling convention of each function: pascal (ret N) -> args popped by callee
RETN = {}
for k, lins in FBODY.items():
    for l in lins:
        if l in insns and insns[l][1].mnemonic in ('ret', 'retf') and insns[l][1].op_str:
            RETN[k] = int(insns[l][1].op_str, 0)

def gname(off, size=2):
    n = DV.get('%04x' % off)
    if n: return n
    return ('g%04x' if size == 2 else 'b%04x') % off

R16 = ['ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp']
SUB8 = {'al': ('ax', 0), 'ah': ('ax', 8), 'bl': ('bx', 0), 'bh': ('bx', 8), 'cl': ('cx', 0), 'ch': ('cx', 8), 'dl': ('dx', 0), 'dh': ('dx', 8)}

def paren(e):
    return e if re.fullmatch(r'[\w\.\[\]\(\)]+|-?0x[0-9a-f]+|-?\d+', e) else '(' + e + ')'

def num(v):
    v &= 0xffff
    if v >= 0x8000 and v > 0xff00: return str(v - 0x10000)
    return hex(v) if v > 9 else str(v)

class Lifter:
    def __init__(self, seg, off):
        self.seg, self.off = seg, off
        self.out = []
        self.reset()
    def reset(self):
        self.r = {r: r for r in R16}
        self.r.update(es='es', ds='DS')
        self.stack = []
        self.flags = None
    def regv(self, name):
        if name in SUB8:
            base, sh = SUB8[name]
            v = self.r[base]
            if sh == 0: return 'lo(%s)' % v if v != base else name
            return 'hi(%s)' % v if v != base else name
        return self.r.get(name, name)
    def setreg(self, name, val):
        if name in SUB8:
            base, sh = SUB8[name]
            # keep it simple: an 8-bit write materialises the 16-bit register
            if self.r[base] != base:
                self.emit('%s = %s;' % (base, self.r[base]))
            self.emit('%s = %s;' % (name, val)); self.r[base] = base
            return
        self.r[name] = val
    def flush(self, regs=None):
        for r in (regs or ['ax', 'bx', 'cx', 'dx', 'si', 'di', 'es']):
            if self.r.get(r) not in (r, None) and r not in ('ds',):
                self.emit('%s = %s;' % (r, self.r[r])); self.r[r] = r
    def emit(self, s): self.out.append('    ' + s)

    def mem(self, op, ins, size):
        m = op.mem
        seg = ins.reg_name(m.segment) if m.segment else None
        base = ins.reg_name(m.base) if m.base else None
        idx = ins.reg_name(m.index) if m.index else None
        disp = m.disp
        T = 'u8' if size == 1 else ('u16' if size == 2 else 'u32')
        if base == 'bp' and not idx:
            d = disp if disp < 0x8000 else disp - 0x10000
            if d < 0: return 'loc%d' % -d if size != 1 else 'locb%d' % -d
            return 'arg%d' % d if size != 1 else 'argb%d' % d
        if not base and not idx and (seg in (None, 'ds')):
            return gname(disp & 0xffff, size)
        parts = []
        if base: parts.append(paren(self.regv(base)))
        if idx: parts.append(paren(self.regv(idx)))
        if disp: parts.append(num(disp))
        addr = ' + '.join(parts) if parts else '0'
        if seg == 'es':
            e = self.r['es']
            pre = 'FAR(%s)' % e if e not in ('DS',) else 'DS'
            return '%s_%s[%s]' % (pre, T, addr)
        if seg == 'cs':
            return 'CS_%s[%s]' % (T, addr)
        return 'DS_%s[%s]' % (T, addr)

    def val(self, ins, op, size=2):
        if op.type == X86_OP_REG: return self.regv(ins.reg_name(op.reg))
        if op.type == X86_OP_IMM: return num(op.imm)
        return self.mem(op, ins, op.size)

    def store(self, ins, op, v):
        if op.type == X86_OP_REG:
            self.setreg(ins.reg_name(op.reg), v)
        else:
            self.flush(['ax', 'bx', 'cx', 'dx', 'si', 'di']) if False else None
            self.emit('%s = %s;' % (self.mem(op, ins, op.size), v))

    def lift(self):
        k = (self.seg, self.off)
        lins = sorted(l for l in FBODY[k] if owner.get(l) == k and l in insns)
        targets = set()
        for l in lins:
            s, ins = insns[l]
            if (ins.mnemonic.startswith('j') or ins.mnemonic.startswith('loop')) and ins.operands and ins.operands[0].type == X86_OP_IMM:
                targets.add(ins.operands[0].imm & 0xffff)
        jt = {t: (s, n) for t, (s, n) in jtables.items() if s == self.seg}
        nret = RETN.get(k)
        self.out.append('// %s  %04x:%04x %s' % (fname(*k), self.seg, self.off, '(pascal, %d arg bytes)' % nret if nret else '(cdecl/none)'))
        prev_end = None
        for l in lins:
            s, ins = insns[l]
            a = ins.address
            if a in targets or (prev_end is not None and l != prev_end):
                self.flush(); self.reset()
                self.out.append('L%04x:' % a)
            prev_end = l + ins.size
            self.step(ins, l, jt)
        return '\n'.join(self.out)

    def cond(self, m):
        f = self.flags
        if not f: return m + '?'
        kind, a, b = f
        ops = {'je': '==', 'jne': '!=', 'jl': '<', 'jge': '>=', 'jg': '>', 'jle': '<=',
               'jb': '<u', 'jae': '>=u', 'ja': '>u', 'jbe': '<=u', 'js': '<0', 'jns': '>=0'}
        if kind == 'cmp':
            return '%s %s %s' % (a, ops.get(m, m), b)
        if kind == 'test':
            if m == 'je': return '(%s & %s) == 0' % (paren(a), paren(b)) if a != b else '%s == 0' % a
            if m == 'jne': return '(%s & %s) != 0' % (paren(a), paren(b)) if a != b else '%s != 0' % a
            return '%s(%s & %s)' % (m, a, b)
        if kind == 'res':
            if m in ('je',): return '%s == 0' % paren(a)
            if m in ('jne',): return '%s != 0' % paren(a)
            if m in ('jl', 'js'): return '%s < 0' % paren(a)
            if m in ('jge', 'jns'): return '%s >= 0' % paren(a)
            if m == 'jg': return '%s > 0' % paren(a)
            if m == 'jle': return '%s <= 0' % paren(a)
        return '%s(%s)' % (m, f)

    def step(self, ins, lin, jt):
        m = ins.mnemonic; ops = ins.operands
        E = self.emit
        if m in ('push',) and ops[0].type == X86_OP_REG and ins.reg_name(ops[0].reg) in ('bp', 'si', 'di') and not self.stack and getattr(self, 'in_prologue', True):
            return
        if not (m in ('push', 'mov', 'sub') and (ins.op_str in ('bp', 'si', 'di', 'bp, sp') or ins.op_str.startswith('sp, '))):
            self.in_prologue = False
        if m == 'mov' and ins.op_str in ('bp, sp', 'sp, bp'): return
        if m == 'pop' and ins.reg_name(ops[0].reg) in ('bp', 'si', 'di') : return
        if m == 'sub' and ins.op_str.startswith('sp, '): return
        if m == 'push':
            v = self.val(ins, ops[0])
            if ops[0].type == X86_OP_REG and ins.reg_name(ops[0].reg) == 'cs':
                self.stack.append('CS'); return
            self.stack.append(v); return
        if m == 'pop':
            v = self.stack.pop() if self.stack else 'POP()'
            self.store(ins, ops[0], v); return
        if m in ('call', 'lcall'):
            if m == 'lcall' and lin + 3 in RELOCS:
                tseg, toff = w16(lin + 3), w16(lin + 1)
            elif m == 'call' and ops[0].type == X86_OP_IMM:
                tseg, toff = self.seg, ops[0].imm & 0xffff
                if self.stack and self.stack[-1] == 'CS': self.stack.pop()
            else:
                self.flush()
                E('CALL_INDIRECT(%s);' % self.val(ins, ops[0])); self.stack = []; return
            nret = RETN.get((tseg, toff))
            args = self.stack
            if nret:
                n = nret // 2
                args = args[-n:] if n <= len(args) else args
                self.stack = self.stack[:len(self.stack) - len(args)]
                al = list(args)
            else:
                al = list(reversed(args))
                self.stack = []
            self.flush()
            E('ax = %s(%s);' % (fname(tseg, toff), ', '.join(al)))
            self.r['ax'] = 'ax'; self.r['dx'] = 'dx'
            return
        if m == 'mov':
            self.store(ins, ops[0], self.val(ins, ops[1])); return
        if m == 'les' or m == 'lds':
            v = self.val(ins, ops[1])
            r = ins.reg_name(ops[0].reg)
            self.r[r] = v; self.r['es' if m == 'les' else 'ds'] = 'seg(%s)' % v
            # far pointer: es:bx -> FAR(ptr)
            self.r['es'] = v + '@'
            self.r[r] = '0'
            return
        if m in ('xor', 'sub') and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
            self.setreg(ins.reg_name(ops[0].reg), '0'); self.flags = ('res', '0', None); return
        binops = {'add': '+', 'sub': '-', 'and': '&', 'or': '|', 'xor': '^', 'shl': '<<', 'sal': '<<', 'shr': '>>u', 'sar': '>>', 'adc': '+c+', 'sbb': '-b-'}
        if m in binops:
            a = self.val(ins, ops[0]); b = self.val(ins, ops[1])
            if ops[0].type == X86_OP_REG and m in ('or', 'and') and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                self.flags = ('res', a, None); return
            v = '%s %s %s' % (paren(a), binops[m], paren(b))
            if ops[0].type == X86_OP_REG:
                self.setreg(ins.reg_name(ops[0].reg), v); self.flags = ('res', v, None)
            else:
                self.flush(['ax', 'bx', 'cx', 'dx', 'si', 'di'])
                tgt = self.mem(ops[0], ins, ops[0].size)
                E('%s %s= %s;' % (tgt, binops[m].rstrip('u'), b)); self.flags = ('res', tgt, None)
            return
        if m in ('inc', 'dec', 'neg', 'not'):
            a = self.val(ins, ops[0])
            v = {'inc': '%s + 1', 'dec': '%s - 1', 'neg': '-%s', 'not': '~%s'}[m] % paren(a)
            if ops[0].type == X86_OP_REG:
                self.setreg(ins.reg_name(ops[0].reg), v); self.flags = ('res', v, None)
            else:
                E('%s;' % ({'inc': '%s++', 'dec': '%s--', 'neg': '%s = -%s', 'not': '%s = ~%s'}[m] % ((a,) if m in ('inc', 'dec') else (a, a))))
                self.flags = ('res', a, None)
            return
        if m == 'mul':
            b = self.val(ins, ops[0]); self.r['ax'] = '%s * %s' % (paren(self.r['ax']), paren(b)); self.r['dx'] = 'HI(%s*%s)' % (self.r['ax'], b); return
        if m in ('imul', 'idiv', 'div'):
            self.flush()
            if m == 'idiv' or m == 'div':
                b = self.val(ins, ops[0]); E('dx = ax %% %s; ax = ax / %s;  // %s' % (b, b, m)); return
            E('%s %s;' % (m, ins.op_str)); return
        if m == 'cwd' or m == 'cdq':
            self.r['dx'] = 'SIGN(%s)' % self.r['ax']; return
        if m == 'cbw' or m == 'cwde':
            self.r['ax'] = '(s8)%s' % paren(self.regv('al')); return
        if m == 'cmp':
            self.flags = ('cmp', self.val(ins, ops[0]), self.val(ins, ops[1])); return
        if m == 'test':
            self.flags = ('test', self.val(ins, ops[0]), self.val(ins, ops[1])); return
        if m == 'xchg':
            a = self.val(ins, ops[0]); b = self.val(ins, ops[1])
            self.flush(); E('swap(%s, %s);' % (a, b)); return
        if m == 'jmp':
            if ops[0].type == X86_OP_IMM:
                self.flush(); E('goto L%04x;' % (ops[0].imm & 0xffff)); self.reset(); return
            self.flush(); E('switch (bx/2) { /* jump table %s */ }' % ins.op_str)
            if ops[0].type == X86_OP_MEM:
                t = self.seg * 16 + (ops[0].mem.disp & 0xffff)
                if t in jtables:
                    sg, n = jtables[t]
                    for i in range(n): E('  case %d: goto L%04x;' % (i, w16(t + 2 * i)))
            self.reset(); return
        if m.startswith('j') or m.startswith('loop'):
            t = ops[0].imm & 0xffff
            self.flush()
            if m.startswith('loop'): E('if (--cx) goto L%04x;' % t)
            elif m == 'jcxz': E('if (cx == 0) goto L%04x;' % t)
            else: E('if (%s) goto L%04x;' % (self.cond(m), t))
            return
        if m in ('ret', 'retf', 'iret'):
            self.flush(['ax'])
            E('return;  // ax=%s' % self.r['ax'] if False else 'return ax?;')
            return
        if m in ('cli', 'sti', 'nop', 'cld'): return
        if m in ('in', 'out'):
            self.flush(); E('%s %s;' % (m, ins.op_str)); return
        if m in ('shl', 'shr'): pass
        self.flush(); E('/* %s %s */' % (m, ins.op_str))

for a in ARGS:
    s, o = [int(x, 16) for x in a.replace(':', '_').split('_')]
    print(Lifter(s, o).lift()); print()
