import os
"""
Boot the original game in the emulator, force the attract-mode demo, and capture
per-frame snapshots of the data segment + level map.

usage: run_demo.py <unpacked exe> <game dir copy> <out.bin> [max_frames] [menu_choice]
Output format: sequence of frames: b'FRM0' + u32 frame + DS bytes (0x10000) + map (0x5a00)
"""
import sys, struct, os
sys.path.insert(0, os.path.dirname(__file__))
from dosemu import DOS, LOAD_SEG, R
from unicorn import UC_HOOK_CODE

exe, gdir, out = sys.argv[1:4]
max_frames = int(sys.argv[4]) if len(sys.argv) > 4 else 100000
choice = int(sys.argv[5]) if len(sys.argv) > 5 else 5
shots = os.environ.get('SHOTS')

G = LOAD_SEG + 0x97          # game code segment at runtime
dos = DOS(exe, gdir)
DSEG = LOAD_SEG + dos.dgroup
mu = dos.mu
fo = open(out, 'wb')
state = {'frames': 0, 'menu': 0, 'in_loop': False}

def lin(off): return G * 16 + off

def on_menu(uc, addr, size, ud):
    # fd15: "call 049f" -> pretend the menu returned `choice`
    state['menu'] += 1
    if state['menu'] > 1:
        dos.stop = True; uc.emu_stop(); return


def on_frame(uc, addr, size, ud):
    n = state['frames']
    ds = dos.rmem(DSEG * 16, 0x10000)
    if dos.bss_shift:   # DN2/DN3: lay the uninitialised data out as in DN1 (the port's offsets)
        k = 0x4ECA + dos.bss_shift
        ds = (ds[:0x4ECA] + ds[k:] + bytes(0x10000))[:0x10000] if k > 0x4ECA else (ds[:0x4ECA] + ds[k:])[:0x10000]
    mseg, moff = struct.unpack('<HH', ds[0x7729 + 2:0x7729 + 4] + ds[0x7729:0x7729 + 2])
    mp = dos.rmem(mseg * 16 + moff, 0x5a00) if mseg else bytes(0x5a00)
    pg = dos.ega.display_page * 0x2000
    vram = b''.join(bytes(dos.ega.planes[p][pg:pg + 8000]) for p in range(4))
    fo.write(b'FRM0' + struct.pack('<I', n) + ds + mp + vram)
    if n == 0 and os.environ.get('DN1_HEAPDUMP'):
        # raw 64 KiB window of the map segment (map + the heap blocks after it)
        open(os.environ['DN1_HEAPDUMP'], 'wb').write(dos.rmem(mseg * 16, 0x10000))
    if shots and n % int(shots) == 0:
        save_shot('%s_%05d.png' % (out, n))
    state['frames'] += 1
    if n + 1 >= max_frames:
        dos.stop = True; uc.emu_stop()

def save_shot(path):
    try:
        from PIL import Image
    except ImportError:
        return
    pal = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
           (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
    idx = dos.ega.render(dos.ega.display_page * 0x2000)
    im = Image.new('RGB', (320, 200))
    im.putdata([pal[c] for c in idx])
    im.save(path)

# patch "mov ax,7; push ax; push cs; call 049f" -> "mov ax,choice; nop x5"
mu.mem_write(lin(0xfd1b), bytes([0xB8, choice, 0, 0x90, 0x90, 0x90, 0x90, 0x90]))
# fd1b = "mov ax,7 / push / call 049f" in sub fd15 (after the 3114 call)
mu.hook_add(UC_HOOK_CODE, on_menu, None, lin(0xfd1b), lin(0xfd1b))
# ed53 = "call 290f" at the top of each game-loop iteration (after the frame wait)
mu.hook_add(UC_HOOK_CODE, on_frame, None, lin(0xed53), lin(0xed53))

ticks = dos.run(chunk=int(os.environ.get('CHUNK', '2500')))
fo.close()
print('frames', state['frames'], 'ticks', ticks, 'exit', dos.exited)
