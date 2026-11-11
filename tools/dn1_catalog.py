#!/usr/bin/env python3
"""
dn1_catalog.py - a spreadsheet of every map tile and actor in Duke Nukem 1,
drawn as the game draws them, with a blank column for naming each one.

Shrapnel does the decoding (it runs the game's own code), so this needs a
built Shrapnel and the game folder:

    python dn1_catalog.py --shrapnel path/to/shrapnel.exe --game path/to/game -o DN1_objects.xlsx

or, from a catalog Shrapnel has already written
(shrapnel --dir <game> --catalog catalog.json):

    python dn1_catalog.py --json catalog.json -o DN1_objects.xlsx

Needs:  pip install openpyxl pillow

The workbook has two sheets:
  Tiles   - every map tile: animated ones as one row per four-frame group,
            background and solid tiles one row each
  Actors  - 3000h..3059h, each as it appears when its level starts (and, for
            crates, what the game draws once one is shot open)
Columns: designation, picture, a blank Name column, then what is known.
If the episodes in the folder draw something differently, extra picture
columns show the other episodes' versions.
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tempfile

try:
    from openpyxl import Workbook
    from openpyxl.drawing.image import Image as XLImage
    from openpyxl.styles import Alignment, Font, PatternFill
    from openpyxl.utils import get_column_letter
    from PIL import Image
except ImportError:
    sys.exit("This needs openpyxl and Pillow:  pip install openpyxl pillow")

TILE_SCALE = 4      # 16 px tiles shown at 64 px
ACTOR_SCALE = 3
ACTOR_MAX = 200     # px: bigger looks are shrunk to fit


# --------------------------------------------------------------- pictures
def palette(ep):
    return [((c >> 16) & 255, (c >> 8) & 255, c & 255, 255) for c in ep["palette"]]


def tile_image(hexpx, pal):
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = bytes.fromhex(hexpx)
    im.putdata([pal[v & 15] if v != 255 else (0, 0, 0, 0) for v in px])
    return im


def pieces_image(pieces, pal):
    """An actor's pieces (16x16 each, at dx, dy) as one picture, or None."""
    if not pieces:
        return None
    x0 = min(p["dx"] for p in pieces)
    y0 = min(p["dy"] for p in pieces)
    x1 = max(p["dx"] for p in pieces) + 16
    y1 = max(p["dy"] for p in pieces) + 16
    im = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    for p in pieces:            # in the game's order: later pieces on top
        t = tile_image(p["px"], pal)
        im.alpha_composite(t, (p["dx"] - x0, p["dy"] - y0))
    return im


def strip(images, gap=2):
    w = sum(i.width for i in images) + gap * (len(images) - 1)
    h = max(i.height for i in images)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x = 0
    for i in images:
        out.alpha_composite(i, (x, 0))
        x += i.width + gap
    return out


def scaled(im, scale, limit=None):
    w, h = im.width * scale, im.height * scale
    if limit and max(w, h) > limit:
        f = limit / max(w, h)
        w, h = max(1, int(w * f)), max(1, int(h * f))
    return im.resize((w, h), Image.NEAREST)


def same(a, b):
    return a is not None and b is not None and a.tobytes() == b.tobytes() and a.size == b.size


# --------------------------------------------------------------- sheet helpers
class Sheet:
    def __init__(self, ws, headers, widths):
        self.ws = ws
        self.row = 1
        for c, (h, w) in enumerate(zip(headers, widths), 1):
            cell = ws.cell(row=1, column=c, value=h)
            cell.font = Font(bold=True)
            cell.fill = PatternFill("solid", fgColor="DDDDDD")
            cell.alignment = Alignment(vertical="center", wrap_text=True)
            ws.column_dimensions[get_column_letter(c)].width = w
        ws.row_dimensions[1].height = 45    # (fixed: a header that grows moves the pictures in some programs)
        ws.freeze_panes = "D2"      # designation, picture and name stay in view
        ws.auto_filter.ref = "A1:%s1" % get_column_letter(len(headers))

    def add(self, values, pictures):
        """values: column -> value; pictures: column -> PIL image."""
        self.row += 1
        r = self.row
        tallest = 20
        for c, v in values.items():
            cell = self.ws.cell(row=r, column=c, value=v)
            cell.alignment = Alignment(vertical="center", wrap_text=True)
        for c, im in pictures.items():
            if im is None:
                continue
            buf = io.BytesIO()
            im.save(buf, "PNG")
            buf.seek(0)
            xl = XLImage(buf)
            xl.anchor = "%s%d" % (get_column_letter(c), r)
            self.ws.add_image(xl)
            tallest = max(tallest, im.height + 6)
            # widen the column to fit the widest picture (about 7 px a character)
            letter = get_column_letter(c)
            need = im.width / 7 + 2
            if (self.ws.column_dimensions[letter].width or 0) < need:
                self.ws.column_dimensions[letter].width = need
        self.ws.row_dimensions[r].height = tallest * 0.75     # points


# --------------------------------------------------------------- the workbook
def kind(v):
    if v < 0x5E0:
        return "Animated"
    if v < 0x1800:
        return "Background"
    return "Solid"


def usage_text(ep, values):
    """cells, levels across one or more values in one episode's stock levels"""
    cells, levels = 0, []
    for v in values:
        u = ep["usage"].get("%04X" % v)
        if u:
            cells += u["cells"]
            for l in u["levels"].split():
                if l not in levels:
                    levels.append(l)
    order = lambda l: (l == "H", int(l[1:]) if l[1:].isdigit() else 0)
    return cells, " ".join(sorted(levels, key=order))


def build(cat, out):
    eps = cat["episodes"]
    if not eps:
        sys.exit("The catalog holds no episodes.")
    pals = [palette(e) for e in eps]
    first = eps[0]
    wb = Workbook()

    # ---------------- tiles
    ws = wb.active
    ws.title = "Tiles"
    others = ["E%d picture (if different)" % e["number"] for e in eps[1:]]
    use_cols = []
    for e in eps:
        use_cols += ["E%d cells" % e["number"], "E%d levels" % e["number"]]
    headers = ["Designation", "Picture", "Name", "Kind", "Value", "Tile no.", "Notes"] + others + use_cols
    widths = [16, 12, 28, 12, 9, 9, 30] + [14] * len(others) + [9, 22] * len(eps)
    sh = Sheet(ws, headers, widths)
    col_other = 8
    col_use = 8 + len(others)

    def tile_row(values, title, note, make):
        pics = {2: make(0)}
        for k in range(1, len(eps)):
            im = make(k)
            if not same(im, pics[2]):
                pics[col_other + k - 1] = im
        vals = {1: title, 4: kind(values[0]), 5: values[0], 6: values[0] // 0x20, 7: note}
        for k, e in enumerate(eps):
            cells, lv = usage_text(e, values)
            vals[col_use + 2 * k] = cells or None
            vals[col_use + 2 * k + 1] = lv or None
        sh.add(vals, pics)

    # animated: a cell shows its tile and the next three, in groups of four at multiples of 80h
    for g in range(0, 0x5E0, 0x80):
        frames = [v for v in range(g, g + 0x80, 0x20) if 0x40 <= v < 0x5E0]
        if not frames:
            continue
        title = "%04Xh-%04Xh" % (frames[0], frames[-1])
        note = "%d frames, shown in turn. A cell can start part-way (at any of these)." % len(frames)
        tile_row(frames, title, note,
                 lambda k, frames=frames: scaled(strip([tile_image(eps[k]["tiles"]["%04X" % v], pals[k]) for v in frames]), TILE_SCALE))
    for v in range(0x5E0, 0x3000, 0x20):
        note = "Plain background (not animated, though in the animated range)" if v < 0x600 else ""
        tile_row([v], "%04Xh" % v, note,
                 lambda k, v=v: scaled(tile_image(eps[k]["tiles"]["%04X" % v], pals[k]), TILE_SCALE))

    # ---------------- actors
    ws = wb.create_sheet("Actors")
    others = ["E%d picture (if different)" % e["number"] for e in eps[1:]]
    headers = (["Designation", "Picture", "Name", "Shrapnel's name now", "Crate holds", "Loader leaves", "Table limit", "Notes"]
               + others + use_cols)
    widths = [12, 16, 28, 28, 14, 12, 10, 34] + [16] * len(others) + [9, 22] * len(eps)
    sh = Sheet(ws, headers, widths)
    col_other = 9
    col_use = 9 + len(others)

    def look(k, a):
        im = pieces_image(a["pieces"], pals[k])
        if im is None:          # no sprite: the game shows the tile it leaves
            t = a["after"]
            if t in eps[k]["tiles"]:
                im = tile_image(eps[k]["tiles"][t], pals[k])
        return scaled(im, ACTOR_SCALE, ACTOR_MAX) if im is not None else None

    for i, a in enumerate(first["actors"]):
        pics = {2: look(0, a)}
        held = pieces_image(a["contents"], pals[0])
        if held is not None:
            pics[5] = scaled(held, ACTOR_SCALE, ACTOR_MAX)
        for k in range(1, len(eps)):
            im = look(k, eps[k]["actors"][i])
            if not same(im, pics[2]):
                pics[col_other + k - 1] = im
        notes = []
        if not a["pieces"]:
            notes.append("No sprite of its own: the picture is the tile the loader leaves")
        if a["crate"]:
            notes.append("A crate: shoot it open")
        vals = {1: "%sh" % a["id"], 4: a["name"], 6: "%sh" % a["after"],
                7: a["cap"] if a["table"] else None, 8: "; ".join(notes) or None}
        v = int(a["id"], 16)
        for k, e in enumerate(eps):
            cells, lv = usage_text(e, [v])
            vals[col_use + 2 * k] = cells or None
            vals[col_use + 2 * k + 1] = lv or None
        sh.add(vals, pics)

    # ---------------- notes
    ws = wb.create_sheet("About")
    lines = [
        "Duke Nukem 1: map tiles and actors, as the game draws them (decoded by Shrapnel, which runs the game's own code).",
        "",
        "Designation: the value a map cell holds (hex). Tiles are multiples of 20h; actors are 3000h-3059h.",
        "Animated tiles: a cell shows its tile and the next three in turn; they come in groups of four at multiples of 80h.",
        "Backdrops (0000h, 0020h) are not listed: each level picks its own pictures for them.",
        "Kind: Animated (40h-5C0h), Background (5E0h-17E0h, Duke passes in front), Solid (1800h-2FE0h, walls and floors).",
        "Tile no.: the value / 20h, the tile's place in the game's tile cache.",
        "Loader leaves: what the game puts in an actor's cell when the level loads (the actor itself lives elsewhere).",
        "Table limit: how many of that actor the original game's tables hold in one level (blank: no limit of its own).",
        "En cells / levels: how many cells of the stock maps of episode n hold the value, and which levels (H = the hallway).",
        "",
        "Episodes in this catalog: " + ", ".join("E%d" % e["number"] for e in eps),
    ]
    for r, l in enumerate(lines, 1):
        ws.cell(row=r, column=1, value=l)
    ws.column_dimensions["A"].width = 120

    wb.save(out)
    print("wrote %s" % out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", help="a catalog written by  shrapnel --catalog")
    ap.add_argument("--shrapnel", help="Shrapnel's executable (to write the catalog)")
    ap.add_argument("--game", help="the game folder (DN1.EXE, DN2.EXE, DN3.EXE and their files)")
    ap.add_argument("-o", "--out", default="DN1_objects.xlsx", help="the spreadsheet to write")
    args = ap.parse_args()

    if args.json:
        path = args.json
    elif args.shrapnel and args.game:
        path = os.path.join(tempfile.mkdtemp(), "catalog.json")
        r = subprocess.run([args.shrapnel, "--dir", args.game, "--catalog", path])
        if r.returncode != 0 or not os.path.exists(path):
            sys.exit("Shrapnel couldn't write the catalog.")
    else:
        ap.error("give --json, or --shrapnel and --game")
    with open(path, encoding="utf-8") as f:
        cat = json.load(f)
    build(cat, args.out)


if __name__ == "__main__":
    main()
