#!/usr/bin/env python3
"""
extract_images.py - export Duke Nukem's full-screen pictures and level
backdrops as PNG files, as a starting point for replacement art (Techbot
looks for same-named PNGs in a "widescreen" subfolder of the game folder).

usage:  python tools/extract_images.py <game folder> [output folder]
        (output defaults to <game folder>/extracted; needs Pillow:
         pip install pillow)

Every episode found in the folder is exported (*.DN1, *.DN2, *.DN3).  Each
PNG is named after the file it came from plus ".png", e.g.

    DN.DN1.png        title screen           320 x 200
    END.DN1.png       ending picture         320 x 200
    BADGUY.DN1.png    Dr. Proton             320 x 200
    DUKE.DN1.png      Duke                   320 x 200
    CREDITS.DN1.png   credits                320 x 200
    DROP0.DN1.png     level backdrop         208 x 160  (13 x 10 tiles)

so files from different episodes never clash.  The PNGs are 16-colour
indexed images with the game's EGA palette, pixel for pixel what the game
shows (the full-screen pictures use the 320x200 EGA mode, whose pixels are
displayed 1.2 times taller than wide).
"""
import os, sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow:  pip install pillow")

# the 16 EGA colours in the order the game uses them (default palette)
EGA = [(0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
       (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
       (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
       (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF)]
PALETTE = [c for rgb in EGA for c in rgb] + [0] * (768 - 48)

PICTURES = [("DN", "title screen"), ("END", "ending picture"), ("BADGUY", "Dr. Proton"),
            ("DUKE", "Duke"), ("CREDITS", "credits")]


def indexed(w, h, pixels):
    im = Image.new("P", (w, h))
    im.putpalette(PALETTE)
    im.putdata(pixels)
    return im


def picture(data):
    """32000 bytes: four 8000-byte bit planes (blue, green, red, intensity),
    40 bytes per 320-pixel row"""
    data = data.ljust(32000, b"\0")
    px = []
    for y in range(200):
        for x in range(320):
            o, s = y * 40 + (x >> 3), 7 - (x & 7)
            px.append(sum(((data[p * 8000 + o] >> s) & 1) << p for p in range(4)))
    return indexed(320, 200, px)


def backdrop(data):
    """tile file: 3-byte header, then per 8 pixels: mask, blue, green, red,
    intensity.  130 tiles of 16x16 (32 bytes per plane each), 13 across.
    (The game reads a fixed amount, so anything past the end reads as FFh.)"""
    body = data[3:]
    planes = [bytearray(0x5140 // 4) for _ in range(4)]
    for i in range(len(planes[0])):
        g = body[i * 5:i * 5 + 5].ljust(5, b"\xff")
        for p in range(4):
            planes[p][i] = g[1 + p]
    px = []
    for y in range(160):
        for x in range(208):
            t = (y >> 4) * 13 + (x >> 4)
            o, s = t * 32 + (y & 15) * 2 + ((x & 15) >> 3), 7 - (x & 7)
            px.append(sum(((planes[p][o] >> s) & 1) << p for p in range(4)))
    return indexed(208, 160, px)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    game = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(game, "extracted")
    os.makedirs(out, exist_ok=True)
    files = {f.upper(): f for f in os.listdir(game)}
    n = 0
    for ep in ("DN1", "DN2", "DN3"):
        names = [(f"{base}.{ep}", what) for base, what in PICTURES]
        drops = sorted((k for k in files if k.startswith("DROP") and k.endswith("." + ep)),
                       key=lambda k: int(k[4:-4]) if k[4:-4].isdigit() else 999)
        found = [k for k, _ in names if k in files] + drops
        if not found:
            continue
        print(f"Episode {ep[-1]}:")
        for name, what in names:
            if name in files:
                picture(open(os.path.join(game, files[name]), "rb").read()).save(os.path.join(out, name + ".png"))
                print(f"  {name}.png  ({what}, 320x200)")
                n += 1
        for name in drops:
            backdrop(open(os.path.join(game, files[name]), "rb").read()).save(os.path.join(out, name + ".png"))
            print(f"  {name}.png  (level backdrop, 208x160)")
            n += 1
    if not n:
        sys.exit(f"no Duke Nukem picture files (DN.DN1, DROP0.DN1, ...) found in {game}")
    print(f"{n} images written to {out}")


main()
