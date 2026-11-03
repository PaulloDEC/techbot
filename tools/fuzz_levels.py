"""Differential test: every level, scripted input, original (emulated) vs port."""
import os, sys, shutil, struct, random, subprocess
GAME = os.environ.get('DN1_GAME', 'game')   # folder with the original episode's files
EXE = os.environ.get('DN1_EXE', 'build/dn1_unpacked.exe')   # unpacked DN1/DN2/DN3.EXE
EXT = os.environ.get('DN1_EXT', 'dn1')      # data file extension of that episode
def gfile(name):                            # case-insensitive lookup in GAME
    for f in os.listdir(GAME):
        if f.lower() == name.lower(): return f
    return name
levels = sys.argv[1].split(',') if len(sys.argv) > 1 else ['1','2','3','4','5','6','7','8','9','a','b','c']
seeds = [int(s) for s in sys.argv[2].split(',')] if len(sys.argv) > 2 else [1]
NFR = int(sys.argv[3]) if len(sys.argv) > 3 else 1500

def make_inputs(seed, n):
    r = random.Random(seed); out = bytearray()
    up = down = left = right = fire = jump = 0; hold = 0
    for i in range(n):
        if hold <= 0:
            mode = r.choice(['right', 'right', 'right', 'left', 'jumpr', 'jumpl', 'up', 'idle', 'down'])
            hold = r.randint(4, 40)
            up = down = left = right = jump = 0
            if mode == 'right': right = 1
            elif mode == 'left': left = 1
            elif mode == 'jumpr': right = jump = 1
            elif mode == 'jumpl': left = jump = 1
            elif mode == 'up': up = 1
            elif mode == 'down': down = 1
        hold -= 1
        fire = 1 if r.random() < 0.35 else 0
        if jump and r.random() < 0.3: jump = 0
        out += bytes([up, down, left, right, fire, jump])
    return out

FZ = os.environ.get('DN1_FUZZDIR', 'build/fuzz')
os.makedirs(FZ, exist_ok=True)
summary = []
for lv in levels:
    for seed in seeds:
        d = os.path.join(FZ, 'l%s_s%d' % (lv, seed))
        for sub in ('emu', 'port'):
            p = d + '_' + sub
            if os.path.exists(p): shutil.rmtree(p)
            shutil.copytree(GAME, p)
            for f in os.listdir(p):          # drop the original level 1 / user demo (any case)
                if f.lower() in ('worldal1.' + EXT, 'userdemo.' + EXT): os.remove(os.path.join(p, f))
            shutil.copy(os.path.join(GAME, gfile('worldal%s.%s' % (lv, EXT))), os.path.join(p, 'worldal1.' + EXT))
            inp = make_inputs(seed, NFR)
            data = struct.pack('<H', len(inp)) + bytes(inp) + bytes(0x4268 - len(inp))
            open(os.path.join(p, 'userdemo.' + EXT), 'wb').write(data)
        ref = d + '_ref.bin'; out = d + '_out.bin'
        # a run can legitimately stall in a dialog that waits for ENTER (the
        # scripted input never presses it); compare whatever frames exist then
        for cmd in (['python3', 'tools/emu/run_demo.py', EXE, d + '_emu', ref, str(NFR + 5), '2'],
                    [os.environ.get('DN1_HEADLESS', './build/dn1_headless'), '--dir', d + '_port', '--exe', os.path.abspath(EXE), '--menu', '2',
                     '--frames', str(NFR + 5), '--dump', out]):
            try: subprocess.run(cmd, capture_output=True, timeout=int(os.environ.get('FUZZ_TIMEOUT', '900')))
            except subprocess.TimeoutExpired: pass
        res = subprocess.run(['python3', 'tools/cmp_state.py', ref, out, 'src/globals.def', '2'], capture_output=True, text=True)
        print('=== level %s seed %d' % (lv, seed)); print(res.stdout.strip()[-1500:]); sys.stdout.flush()
        summary.append((lv, seed, res.stdout.strip().split('\n')[-1]))
        for f in (ref, out):
            if os.path.exists(f): os.remove(f)
print('\nSUMMARY'); [print(s) for s in summary]
