#!/usr/bin/env python3
"""Exporte / importe la police 10x16 sous forme d'image PNG.

    python3 tools/font_png.py export   # font10x16.txt -> font10x16.png
    python3 tools/font_png.py import   # font10x16.png -> font10x16.txt + header

Dans le PNG, chaque glyphe est une case de 10x16 pixels (fond blanc), avec
au-dessus le caractere en gris pour se reperer. Un pixel NOIR = allume,
tout le reste = eteint. Les lignes legerement teintees sont des reperes :
haut des majuscules (1), haut des minuscules (4) et ligne de base (12).

L'image peut etre agrandie (x2, x8...) avant edition : l'import detecte
l'echelle tout seul tant que l'agrandissement est un multiple entier.
"""
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TXT = os.path.join(HERE, "font10x16.txt")
PNG = os.path.join(HERE, "font10x16.png")
DISPLAY_H = os.path.join(HERE, "..", "headers", "display.h")

GW, GH = 10, 16          # taille d'un glyphe
COLS = 16                # glyphes par ligne
LABEL_H = 9              # bandeau d'etiquette au-dessus de chaque case
PAD = 2                  # marge autour de chaque case
CELL_W = GW + 2 * PAD
CELL_H = LABEL_H + GH + 2 * PAD

BG = (150, 150, 150)     # entre les cases
PAPER = (255, 255, 255)  # fond d'une case
GUIDE = (215, 228, 255)  # lignes reperes
LABEL = (90, 90, 90)
INK = (0, 0, 0)
GUIDE_ROWS = (1, 4, 12)


def load_txt():
    header, glyphs, cur = [], [], None
    for line in open(TXT, encoding="utf-8").read().split("\n"):
        if line.startswith("= "):
            cur = {"name": line, "rows": []}
            glyphs.append(cur)
        elif cur is None:
            header.append(line)
        elif len(line) == 10 and set(line) <= set("#."):
            cur["rows"].append(line)
    return header, glyphs


def save_txt(header, glyphs):
    out = [l for l in header if l != ""] + [""]
    for g in glyphs:
        out.append(g["name"])
        out += g["rows"]
    open(TXT, "w", encoding="utf-8").write("\n".join(out) + "\n")


def small_font():
    """Police 5x7 d'origine (display.h), pour dessiner les etiquettes."""
    import re
    src = open(DISPLAY_H, encoding="utf-8").read()
    body = src[src.index("font5x7[] = {"):]
    body = body[:body.index("};")]
    return [int(h, 16) for h in re.findall(r"0x[0-9A-Fa-f]{2}", body)]


def origin(i):
    cx, cy = i % COLS, i // COLS
    return cx * CELL_W + PAD, cy * CELL_H + LABEL_H + PAD


def export():
    _, glyphs = load_txt()
    f5 = small_font()
    rows = (len(glyphs) + COLS - 1) // COLS
    im = Image.new("RGB", (COLS * CELL_W, rows * CELL_H), BG)
    px = im.load()
    for i, g in enumerate(glyphs):
        ox, oy = origin(i)
        # etiquette : le glyphe 5x7 en gris, centre au-dessus de la case
        lx = ox + (GW - 5) // 2
        ly = oy - LABEL_H
        for c in range(5):
            col = f5[i * 5 + c]
            for r in range(8):
                if col >> r & 1:
                    px[lx + c, ly + r] = LABEL
        for r, row in enumerate(g["rows"]):
            for c, v in enumerate(row):
                if v == "#":
                    color = INK
                elif r in GUIDE_ROWS:
                    color = GUIDE
                else:
                    color = PAPER
                px[ox + c, oy + r] = color
    im.save(PNG)
    print("%d glyphes -> %s (%dx%d px)" % (len(glyphs), os.path.normpath(PNG), im.width, im.height))


def do_import():
    header, glyphs = load_txt()
    rows = (len(glyphs) + COLS - 1) // COLS
    base_w, base_h = COLS * CELL_W, rows * CELL_H
    im = Image.open(PNG).convert("RGB")
    sx, sy = im.width / base_w, im.height / base_h
    if sx != sy or sx < 1 or sx != int(sx):
        sys.exit("Taille de %s inattendue (%dx%d) : il faut %dx%d ou un multiple entier."
                 % (PNG, im.width, im.height, base_w, base_h))
    s = int(sx)
    px = im.load()
    changed = 0
    for i, g in enumerate(glyphs):
        ox, oy = origin(i)
        new = []
        for r in range(GH):
            line = ""
            for c in range(GW):
                rr, gg, bb = px[(ox + c) * s + s // 2, (oy + r) * s + s // 2]
                line += "#" if (rr + gg + bb) < 3 * 100 else "."
            new.append(line)
        if new != g["rows"]:
            changed += 1
            print("  modifie :", g["name"][2:])
        g["rows"] = new
    save_txt(header, glyphs)
    print("%d glyphe(s) modifie(s) -> %s" % (changed, os.path.normpath(TXT)))
    subprocess.check_call([sys.executable, os.path.join(HERE, "gen_font10x16.py")])


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "export":
        export()
    elif cmd == "import":
        do_import()
    else:
        sys.exit(__doc__)
