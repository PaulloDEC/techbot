"""
Minimal DOS + EGA + PIT environment for running the ORIGINAL dn1.exe under Unicorn.

This is a reference oracle for the decompilation: it executes the real game code
so that per-frame game state can be captured and diffed against the C port.

Only what DN1 v1.0 needs is implemented:
  * DOS int 21h: memory, file I/O, vectors, version, date/time, exit
  * BIOS int 10h (mode set / page / palette), int 1Ah (ticks), int 16h (stub)
  * EGA planar memory with latches, map mask, bit mask, write modes 0/1/2
  * PIT channel 0 (game reprograms to ~145 Hz) -> interrupt injection
  * Keyboard port 60h + IRQ1 injection, joystick port 201h (no stick)
"""
import os, struct, sys
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN, UC_HOOK_CODE
from unicorn.x86_const import *

R = {n: globals()['UC_X86_REG_' + n.upper()] for n in
     'ax bx cx dx si di bp sp cs ds es ss ip flags'.split()}

STUB_SEG = 0xF000          # IRET stubs for BIOS vectors live at F000:0100+n
PSP_SEG = 0x0FF0
LOAD_SEG = 0x1000

class EGA:
    def __init__(self):
        self.planes = [bytearray(0x10000) for _ in range(4)]
        self.latch = [0, 0, 0, 0]
        self.seq_idx = 0; self.map_mask = 0xF
        self.gc_idx = 0; self.gc = [0] * 9; self.gc[8] = 0xFF
        self.display_page = 0
        self.palette = list(range(16))
    def read(self, off):
        for p in range(4): self.latch[p] = self.planes[p][off]
        return self.planes[self.gc[4] & 3][off]
    def write(self, off, val):
        mode = self.gc[5] & 3
        mask = self.gc[8]
        rot = self.gc[3] & 7
        func = (self.gc[3] >> 3) & 3
        if rot: val = ((val >> rot) | (val << (8 - rot))) & 0xFF
        for p in range(4):
            if not (self.map_mask >> p) & 1: continue
            if mode == 1:
                self.planes[p][off] = self.latch[p]; continue
            if mode == 0:
                if (self.gc[1] >> p) & 1:
                    d = 0xFF if (self.gc[0] >> p) & 1 else 0
                else:
                    d = val
            elif mode == 2:
                d = 0xFF if (val >> p) & 1 else 0
            else:
                d = 0xFF if (self.gc[0] >> p) & 1 else 0
                m2 = mask & val
                self.planes[p][off] = (d & m2) | (self.latch[p] & ~m2 & 0xFF); continue
            l = self.latch[p]
            if func == 1: d &= l
            elif func == 2: d |= l
            elif func == 3: d ^= l
            self.planes[p][off] = (d & mask) | (l & ~mask & 0xFF)
    def render(self, page_off, width=320, height=200):
        """Return list of 4-bit colour indices for the page (row-major)."""
        out = bytearray(width * height)
        P = self.planes
        for y in range(height):
            base = page_off + y * 40
            for xb in range(40):
                o = base + xb
                b0, b1, b2, b3 = P[0][o], P[1][o], P[2][o], P[3][o]
                for bit in range(8):
                    s = 7 - bit
                    out[y * width + xb * 8 + bit] = (((b0 >> s) & 1) | (((b1 >> s) & 1) << 1) |
                                                     (((b2 >> s) & 1) << 2) | (((b3 >> s) & 1) << 3))
        return out

class DOS:
    def __init__(self, exe_path, game_dir, args=''):
        self.game_dir = game_dir
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0xA0000)
        self.ega = EGA()
        mu.mmio_map(0xA0000, 0x10000, self._vread, None, self._vwrite, None)
        mu.mem_map(0xB0000, 0x50000)
        # IVT -> stubs
        for n in range(256):
            mu.mem_write(n * 4, struct.pack('<HH', 0x100 + n, STUB_SEG))
            mu.mem_write(STUB_SEG * 16 + 0x100 + n, b'\xCF')
        mu.mem_write(0xFFFF5, b'01/01/91')
        # BIOS data area
        mu.mem_write(0x463, struct.pack('<H', 0x3D4))
        self._load_exe(exe_path, args)
        self.files = {}
        self.next_handle = 5
        self.mem_top = 0x9FFF
        self.alloc_next = 0x9000
        self.port60 = 0
        self.key_queue = []
        self.pit_div = 0
        self.ticks = 0
        self.exited = None
        self.tick_count = 0
        self.vsync = 0
        self.speaker = []
        mu.hook_add(UC_HOOK_INTR, self._intr)
        mu.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)

    # ------------------------------------------------------------------ loader
    def _load_exe(self, path, args):
        d = open(path, 'rb').read()
        (_, lastp, np, nrel, hdrp, minx, maxx, ss, sp, ck, ip, cs, relo, ov) = struct.unpack('<2s13H', d[:28])
        img = bytearray(d[hdrp * 16:])
        for i in range(nrel):
            o, s = struct.unpack('<HH', d[relo + i * 4:relo + i * 4 + 4])
            a = s * 16 + o
            v = struct.unpack('<H', img[a:a + 2])[0]
            img[a:a + 2] = struct.pack('<H', (v + LOAD_SEG) & 0xFFFF)
        mu = self.mu
        mu.mem_write(LOAD_SEG * 16, bytes(img))
        # episode: DGROUP from the startup code's "mov dx, DGROUP" (17E3/17E7/17E9h)
        self.dgroup = (struct.unpack('<H', bytes(img[1:3]))[0] - LOAD_SEG) & 0xFFFF   # (relocated above)
        # uninitialised data (4ECAh in DN1) moves with the size of the text
        self.bss_shift = {0x17E3: 0, 0x17E7: 216, 0x17E9: -276}[self.dgroup]
        psp = bytearray(256)
        psp[0:2] = b'\xCD\x20'
        psp[2:4] = struct.pack('<H', 0xA000)
        env = 0x0F00
        psp[0x2C:0x2E] = struct.pack('<H', env)
        tail = (' ' + args) if args else ''
        psp[0x80] = len(tail)
        psp[0x81:0x81 + len(tail)] = tail.encode()
        psp[0x81 + len(tail)] = 0x0D
        mu.mem_write(PSP_SEG * 16, bytes(psp))
        mu.mem_write(env * 16, b'PATH=C:\\\0\0\x01\0C:\\DN1.EXE\0')
        for r, v in (('ss', ss + LOAD_SEG), ('sp', sp), ('cs', cs + LOAD_SEG), ('ds', PSP_SEG), ('es', PSP_SEG)):
            mu.reg_write(R[r], v)
        self.ip = ip
        self.img_size = len(img)

    # ------------------------------------------------------------------ helpers
    def rd(self, r): return self.mu.reg_read(R[r])
    def wr(self, r, v): self.mu.reg_write(R[r], v & 0xFFFF)
    def lin(self, seg, off): return (seg << 4) + off
    def rmem(self, a, n): return bytes(self.mu.mem_read(a, n))
    def cstr(self, seg, off):
        s = bytearray(); a = self.lin(seg, off)
        while True:
            c = self.mu.mem_read(a, 1)[0]
            if c == 0: return s.decode('latin1')
            s.append(c); a += 1
    def set_cf(self, on):
        f = self.rd('flags')
        self.wr('flags', (f | 1) if on else (f & ~1))
    def ds_base(self): return (LOAD_SEG + self.dgroup) * 16

    # ------------------------------------------------------------------ EGA
    def _vread(self, uc, off, size, ud):
        v = self.ega.read(off)
        if size == 2: v |= self.ega.read(off + 1) << 8
        return v
    def _vwrite(self, uc, off, size, val, ud):
        self.ega.write(off, val & 0xFF)
        if size == 2: self.ega.write(off + 1, (val >> 8) & 0xFF)

    # ------------------------------------------------------------------ ports
    def _in(self, uc, port, size, ud):
        if port == 0x60: return self.port60
        if port == 0x61: return 0
        if port == 0x201: return 0xFF            # no joystick: all axis bits stay high, buttons up
        if port == 0x3DA:
            self.vsync ^= 0x09; return self.vsync
        return 0xFF
    def _out(self, uc, port, size, val, ud):
        e = self.ega
        if size == 2:
            self._out(uc, port, 1, val & 0xFF, ud); self._out(uc, port + 1, 1, val >> 8, ud); return
        if port == 0x3C4: e.seq_idx = val
        elif port == 0x3C5:
            if e.seq_idx == 2: e.map_mask = val & 0xF
        elif port == 0x3CE: e.gc_idx = val & 0xF
        elif port == 0x3CF:
            if e.gc_idx < 9: e.gc[e.gc_idx] = val
        elif port == 0x43: self.pit_latch = []
        elif port == 0x40:
            self.pit_latch = getattr(self, 'pit_latch', []) + [val]
            if len(self.pit_latch) == 2: self.pit_div = self.pit_latch[0] | self.pit_latch[1] << 8; self.pit_latch = []
        elif port == 0x42: self.speaker.append(val)

    # ------------------------------------------------------------------ interrupts
    def _intr(self, uc, intno, ud):
        seg, off = struct.unpack('<HH', self.rmem(intno * 4, 4))[::-1]
        if seg != STUB_SEG:
            # vector installed by the program: perform a real-mode INT
            self.raise_int(intno)
            return
        h = getattr(self, '_int%02x' % intno, None)
        if h: h()
        else: print('unhandled int %02x ax=%04x' % (intno, self.rd('ax')), file=sys.stderr)

    def raise_int(self, n):
        mu = self.mu
        sp = (self.rd('sp') - 6) & 0xFFFF
        ss = self.rd('ss')
        mu.mem_write(self.lin(ss, sp), struct.pack('<HHH', self.rd('ip'), self.rd('cs'), self.rd('flags')))
        self.wr('sp', sp)
        self.wr('flags', self.rd('flags') & ~0x0200 & ~0x100)
        off, seg = struct.unpack('<HH', self.rmem(n * 4, 4))
        self.wr('cs', seg); self.wr('ip', off)

    def _int10(self):
        ah = self.rd('ax') >> 8; al = self.rd('ax') & 0xFF
        if ah == 0x0F: self.wr('ax', 0x2800 | 0x0D); self.wr('bx', self.rd('bx') & 0xFF)
        elif ah == 0x05: self.ega.display_page = al
        elif ah == 0x10 and al == 0:
            bx = self.rd('bx'); self.ega.palette[bx & 0xF] = bx >> 8
        elif ah in (0x00, 0x10, 0x0B, 0x02, 0x01, 0x12, 0x1A, 0x0E): pass
        else: pass
    def _int16(self):
        ah = self.rd('ax') >> 8
        if ah == 1: self.wr('flags', self.rd('flags') | 0x40)
        else: self.wr('ax', 0)
    def _int1a(self):
        self.wr('cx', 0); self.wr('dx', self.tick_count & 0xFFFF); self.wr('ax', 0)
    def _int11(self): self.wr('ax', 0x0026)
    def _int12(self): self.wr('ax', 640)
    def _int15(self): self.set_cf(True)
    def _int08(self): pass
    def _int09(self): pass
    def _int1c(self): pass
    def _int33(self): self.wr('ax', 0)
    def _int20(self): self.exited = 0; self.mu.emu_stop()

    def _int21(self):
        mu = self.mu
        ax = self.rd('ax'); ah = ax >> 8; al = ax & 0xFF
        ok = lambda: self.set_cf(False)
        def err(c): self.wr('ax', c); self.set_cf(True)
        if ah == 0x30: self.wr('ax', 0x0005); self.wr('bx', 0); self.wr('cx', 0)
        elif ah == 0x25:
            mu.mem_write(al * 4, struct.pack('<HH', self.rd('dx'), self.rd('ds')))
        elif ah == 0x35:
            off, seg = struct.unpack('<HH', self.rmem(al * 4, 4))
            self.wr('bx', off); self.wr('es', seg)
        elif ah == 0x4A:
            want = self.rd('bx'); es = self.rd('es')
            if es + want > self.mem_top: self.wr('bx', self.mem_top - es); err(8)
            else: ok()
        elif ah == 0x48:
            want = self.rd('bx')
            seg = self.alloc_next
            if seg + want > self.mem_top: self.wr('bx', self.mem_top - seg); err(8)
            else: self.alloc_next += want; self.wr('ax', seg); ok()
        elif ah == 0x49: ok()
        elif ah == 0x4C: self.exited = al; mu.emu_stop()
        elif ah in (0x3D, 0x3C, 0x5B):
            name = self.cstr(self.rd('ds'), self.rd('dx'))
            path = self._path(name)
            try:
                if ah == 0x3D:
                    mode = ['rb', 'r+b', 'r+b'][al & 3]
                    f = open(path, mode)
                else:
                    f = open(path, 'w+b')
            except OSError:
                err(2); return
            h = self.next_handle; self.next_handle += 1
            self.files[h] = f; self.wr('ax', h); ok()
        elif ah == 0x3E:
            f = self.files.pop(self.rd('bx'), None)
            if f: f.close()
            ok()
        elif ah == 0x3F:
            h = self.rd('bx'); n = self.rd('cx')
            if h not in self.files: self.wr('ax', 0); ok(); return
            data = self.files[h].read(n)
            mu.mem_write(self.lin(self.rd('ds'), self.rd('dx')), data)
            self.wr('ax', len(data)); ok()
        elif ah == 0x40:
            h = self.rd('bx'); n = self.rd('cx')
            data = self.rmem(self.lin(self.rd('ds'), self.rd('dx')), n)
            if h in self.files: self.files[h].write(data)
            elif h in (1, 2): sys.stderr.write(data.decode('latin1'))
            self.wr('ax', n); ok()
        elif ah == 0x42:
            h = self.rd('bx'); pos = (self.rd('cx') << 16) | self.rd('dx')
            if pos & 0x80000000: pos -= 1 << 32
            f = self.files.get(h)
            if not f: err(6); return
            f.seek(pos, al); p = f.tell()
            self.wr('ax', p & 0xFFFF); self.wr('dx', p >> 16); ok()
        elif ah == 0x44:
            h = self.rd('bx')
            if al == 0:
                self.wr('dx', 0x80 | 0x03 if h < 5 else 0x0000); ok()
            else: ok()
        elif ah == 0x41:
            try: os.remove(self._path(self.cstr(self.rd('ds'), self.rd('dx'))))
            except OSError: pass
            ok()
        elif ah == 0x2A: self.wr('cx', 1991); self.wr('dx', 0x0101); self.wr('ax', 0x0002)
        elif ah == 0x2C: self.wr('cx', 0); self.wr('dx', 0)
        elif ah == 0x1A: self.dta = (self.rd('ds'), self.rd('dx'))
        elif ah == 0x0B: self.wr('ax', ax & 0xFF00)
        elif ah in (0x06, 0x07, 0x08, 0x01): self.wr('ax', ax & 0xFF00)
        elif ah == 0x09: pass
        elif ah == 0x19: self.wr('ax', ax & 0xFF00)
        elif ah == 0x0E: self.wr('ax', (ax & 0xFF00) | 3)
        elif ah == 0x33: self.wr('dx', 0)
        elif ah == 0x4E: err(18)
        else:
            print('unhandled int21 ah=%02x' % ah, file=sys.stderr); err(1)

    def _path(self, name):
        name = name.replace('\\', '/').split('/')[-1]
        for f in os.listdir(self.game_dir):
            if f.lower() == name.lower(): return os.path.join(self.game_dir, f)
        return os.path.join(self.game_dir, name.lower())

    # ------------------------------------------------------------------ run control
    def push_key(self, scancode): self.key_queue.append(scancode)

    def run(self, chunk=2500, max_ticks=None, on_tick=None):
        """Run, injecting a PIT interrupt every `chunk` instructions."""
        mu = self.mu
        self.wr('ip', self.ip)
        ticks = 0
        while self.exited is None:
            start = self.lin(self.rd('cs'), self.rd('ip'))
            try:
                mu.emu_start(start, 0xFFFFFFFF, count=chunk)
            except UcError as e:
                print('emu error', e, hex(self.rd('cs')), hex(self.rd('ip')), file=sys.stderr)
                raise
            if self.exited is not None or getattr(self, 'stop', False): break
            ticks += 1; self.tick_count += 1
            if self.tick_count % 8 == 0:      # BIOS 18.2 Hz tick counter (0040:006C)
                v = struct.unpack('<I', self.rmem(0x46C, 4))[0] + 1
                mu.mem_write(0x46C, struct.pack('<I', v))
            self.last_pc = (self.rd('cs'), self.rd('ip'))
            if self.rd('flags') & 0x200:
                if self.key_queue and ticks % 4 == 0:
                    self.port60 = self.key_queue.pop(0)
                    self.raise_int(9)
                elif self.pit_div:
                    self.raise_int(8)
            if on_tick: on_tick(self)
            if max_ticks and ticks >= max_ticks: break
        return ticks
