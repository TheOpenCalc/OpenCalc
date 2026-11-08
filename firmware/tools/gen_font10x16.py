#!/usr/bin/env python3
"""Convertit tools/font10x16.txt en headers/font10x16.h.

Format du .txt : une ligne "= <indice> U+XXXX <car>" puis 16 lignes de 10
caracteres ('#' = pixel allume, '.' = eteint). Les lignes commencant par
"# " sont des commentaires. Les glyphes suivent l'ordre de font5x7.

Encodage du header : 10 colonnes par glyphe, chaque colonne est un uint16_t
dont le bit 0 est la ligne du haut (meme convention que font5x7).
"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "font10x16.txt")
dst = os.path.join(here, "..", "headers", "font10x16.h")

glyphs = []
cur = None
for n, line in enumerate(open(src, encoding="utf-8").read().split("\n"), 1):
    if line.startswith("= "):
        cur = {"name": line[2:], "rows": []}
        glyphs.append(cur)
    elif line.startswith("# ") or line.strip() == "" or line.startswith("#  "):
        continue
    else:
        if cur is None or len(line) != 10 or set(line) - set("#."):
            sys.exit("%s:%d: ligne invalide %r" % (src, n, line))
        cur["rows"].append(line)

out = [
    "/* Genere par tools/gen_font10x16.py a partir de tools/font10x16.txt.",
    " * Ne pas modifier a la main : editer le .txt puis relancer le script. */",
    "#ifndef FONT10X16_H",
    "#define FONT10X16_H",
    "#include <stdint.h>",
    "",
    "const uint16_t font10x16[] = {",
]
for g in glyphs:
    if len(g["rows"]) != 16:
        sys.exit("glyphe %s : %d lignes au lieu de 16" % (g["name"], len(g["rows"])))
    cols = []
    for c in range(10):
        v = 0
        for r in range(16):
            if g["rows"][r][c] == "#":
                v |= 1 << r
        cols.append("0x%04X" % v)
    # Commentaire en /* */ : un "\\" en fin de commentaire // continuerait
    # la ligne et avalerait le glyphe suivant.
    out.append("    " + ", ".join(cols) + ", /* " + g["name"] + " */")
out += ["};", "", "static_assert(sizeof(font10x16) / sizeof(font10x16[0]) == %d * 10,"
        " \"font10x16 : nombre de glyphes inattendu\");" % len(glyphs), "", "#endif", ""]
open(dst, "w", encoding="utf-8").write("\n".join(out))
print("%d glyphes -> %s" % (len(glyphs), os.path.normpath(dst)))
