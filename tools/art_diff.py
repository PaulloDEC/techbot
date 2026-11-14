#!/usr/bin/env python3
"""
art_diff.py - widescreen art as the difference from the game's own pictures.

usage: python tools/art_diff.py [originals] [widescreen] [difference] [--tolerance N]
       (the three folders; by default "originals", "widescreen" and
       "difference" in the current folder)

For every PNG in the widescreen folder, the PNG of the same name in the
originals folder (usually the same height, the canvas widened about the
middle - tried first; otherwise looked for anywhere, at any whole-number
enlargement) (as tools/extract_images.py writes them: DROP3.DN2.png,
DN.DN1.png ...) is found inside it - at whatever whole-number enlargement
and position it was used - and every pixel of the wide art that is the
original's pixel at that place is made see-through.  What is left, written
to the difference folder under the same name, is only what the wide art adds.

Where the original was found is written into the difference PNG (a text
chunk, "techbot-base": the original's file name, the enlargement and the
position), so Techbot can rebuild the full picture from the player's own game
files: the original, enlarged and placed there, under the difference.  The
script checks that rebuilding gives back the wide art exactly (or, with
--tolerance, within it) before writing anything.

A wide picture with no original of its name is copied as it is (all new).
Original pixels copied to another place in the wide art (a building moved
along the skyline) are not found by this and stay in the difference.

Needs Pillow and numpy (pip install pillow numpy).
"""
import os, sys
import numpy as np
from PIL import Image, PngImagePlugin

def load_rgba(path):
    return np.asarray(Image.open(path).convert('RGBA'), dtype=np.uint8)

def find_ci(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    return None

def same(a, b, tol):
    """per pixel: equal colours (within tol on each channel), both opaque"""
    d = np.abs(a[..., :3].astype(np.int16) - b[..., :3].astype(np.int16)).max(axis=-1)
    return (d <= tol) & (a[..., 3] == 255) & (b[..., 3] == 255)

def best_place(wide, orig, tol):
    """the enlargement and position at which the most of the original matches"""
    H, W = wide.shape[:2]
    h, w = orig.shape[:2]
    best = (0.0, 1, 0, 0)
    if h == H and w <= W:                       # the usual: the same height, the canvas widened about the middle
        for dx in sorted(range(max(0, (W - w) // 2 - 1), min(W - w, (W - w + 1) // 2 + 1) + 1), key=lambda d: abs(d - (W - w) / 2)):
            full = same(wide[:, dx:dx + w], orig, tol).mean()
            if full > best[0]:
                best = (full, 1, dx, 0)
        if best[0] > 0.98:
            return best
    rng = np.random.default_rng(1)
    for s in range(1, 9):
        if w * s > W or h * s > H:
            break
        big = orig.repeat(s, axis=0).repeat(s, axis=1)
        bh, bw = big.shape[:2]
        ys = rng.integers(0, bh, 2000)
        xs = rng.integers(0, bw, 2000)
        sample = big[ys, xs]
        def score(dx, dy):
            return same(wide[ys + dy, xs + dx], sample, tol).mean()
        # every position if there are few; else every 4th, then all around the best of those
        nx, ny = W - bw + 1, H - bh + 1
        step = 1 if nx * ny <= 4000 else 4
        cand = sorted(((score(dx, dy), dx, dy) for dy in range(0, ny, step) for dx in range(0, nx, step)), reverse=True)[:5]
        if step > 1:
            near = set()
            for _, cx, cy in cand:
                for dy in range(max(0, cy - step), min(ny, cy + step + 1)):
                    for dx in range(max(0, cx - step), min(nx, cx + step + 1)):
                        near.add((dx, dy))
            cand = sorted(((score(dx, dy), dx, dy) for dx, dy in near), reverse=True)[:5]
        for m, dx, dy in cand:
            full = same(wide[dy:dy + bh, dx:dx + bw], big, tol).mean()
            if full > best[0]:
                best = (full, s, dx, dy)
    return best

def main():
    import argparse
    ap = argparse.ArgumentParser(description='Widescreen art as the difference from the game\'s own pictures.')
    ap.add_argument('originals', nargs='?', default='originals')
    ap.add_argument('widescreen', nargs='?', default='widescreen')
    ap.add_argument('difference', nargs='?', default='difference')
    ap.add_argument('--tolerance', type=int, default=0, help='colours this close (0-255 per channel) count as the same')
    a = ap.parse_args()
    orig_dir, wide_dir, out_dir, tol = a.originals, a.widescreen, a.difference, a.tolerance
    os.makedirs(out_dir, exist_ok=True)

    files = sorted(f for f in os.listdir(wide_dir) if f.lower().endswith('.png'))
    if not files:
        sys.exit('No PNG files in ' + wide_dir)
    print('%-22s %-11s %-11s %-22s %s' % ('file', 'wide', 'original', 'found at', 'pixels removed'))
    problems = 0
    for name in files:
        wide = load_rgba(os.path.join(wide_dir, name))
        H, W = wide.shape[:2]
        op = find_ci(orig_dir, name)
        out = os.path.join(out_dir, name)
        if not op:
            Image.fromarray(wide, 'RGBA').save(out, optimize=True)
            print('%-22s %-11s %-11s %-22s %s' % (name, '%dx%d' % (W, H), '-', 'no original: copied', '0%'))
            continue
        orig = load_rgba(op)
        h, w = orig.shape[:2]
        score, s, dx, dy = best_place(wide, orig, tol)
        if score == 0.0:
            Image.fromarray(wide, 'RGBA').save(out, optimize=True)
            print('%-22s %-11s %-11s %-22s %s' % (name, '%dx%d' % (W, H), '%dx%d' % (w, h), 'not found: copied', '0%'))
            problems += 1
            continue
        big = orig.repeat(s, axis=0).repeat(s, axis=1)
        bh, bw = big.shape[:2]
        diff = wide.copy()
        region = diff[dy:dy + bh, dx:dx + bw]
        match = same(region, big, tol)
        region[match] = 0                      # see-through: the original's pixel goes here

        # rebuild as Techbot will (the original, enlarged and placed, under the difference) and compare
        rebuilt = diff.copy()
        hole = rebuilt[dy:dy + bh, dx:dx + bw]
        clear = hole[..., 3] == 0
        hole[clear] = big[clear]
        ok = same(rebuilt, wide, tol) | ((rebuilt[..., 3] == wide[..., 3]) & (wide[..., 3] < 255) &
                                         (np.abs(rebuilt.astype(np.int16) - wide.astype(np.int16)).max(axis=-1) <= tol))
        if not ok.all():
            print('%-22s rebuilding it does not give the wide art back (%d pixels differ): copied as it is'
                  % (name, int((~ok).sum())))
            Image.fromarray(wide, 'RGBA').save(out, optimize=True)
            problems += 1
            continue

        info = PngImagePlugin.PngInfo()
        info.add_text('techbot-base', '%s %d %d %d' % (os.path.basename(op), s, dx, dy))
        Image.fromarray(diff, 'RGBA').save(out, pnginfo=info, optimize=True)
        removed = match.sum() / float(W * H)
        print('%-22s %-11s %-11s %-22s %d%%%s' % (name, '%dx%d' % (W, H), '%dx%d' % (w, h),
              '%dx at %d,%d' % (s, dx, dy), round(removed * 100),
              '' if score > 0.9 else '  (only %d%% of the original matched)' % round(score * 100)))
    print('\n%d files written to %s%s' % (len(files), out_dir, ', %d to look at' % problems if problems else ''))

if __name__ == '__main__':
    main()
