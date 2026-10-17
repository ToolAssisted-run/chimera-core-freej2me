#!/usr/bin/env python3
"""Writes Java's table of logical fonts (fontconfig.properties, read through
-Dsun.awt.fontconfig, which needs no fontconfig library): Dialog, SansSerif,
Serif, Monospaced and DialogInput, each in four styles, from the DejaVu
files in the runtime's lib/fonts ($JRE_LIB_FONTS).

  make-fontconfig.py <out>
SPDX-License-Identifier: MIT"""
import sys

out = sys.argv[1]
faces = {
    "DejaVu Sans": "DejaVuSans", "DejaVu Sans Bold": "DejaVuSans-Bold",
    "DejaVu Sans Oblique": "DejaVuSans-Oblique", "DejaVu Sans Bold Oblique": "DejaVuSans-BoldOblique",
    "DejaVu Sans Mono": "DejaVuSansMono", "DejaVu Sans Mono Bold": "DejaVuSansMono-Bold",
    "DejaVu Sans Mono Oblique": "DejaVuSansMono-Oblique", "DejaVu Sans Mono Bold Oblique": "DejaVuSansMono-BoldOblique",
    "DejaVu Serif": "DejaVuSerif", "DejaVu Serif Bold": "DejaVuSerif-Bold",
    "DejaVu Serif Italic": "DejaVuSerif-Italic", "DejaVu Serif Bold Italic": "DejaVuSerif-BoldItalic",
}
logical = {"dialog": "DejaVu Sans", "sansserif": "DejaVu Sans", "serif": "DejaVu Serif",
           "monospaced": "DejaVu Sans Mono", "dialoginput": "DejaVu Sans Mono"}
styles = {"plain": "", "bold": " Bold", "italic": " Oblique", "bolditalic": " Bold Oblique"}
lines = ["version=1"]
for name, base in logical.items():
    for st, suffix in styles.items():
        s = suffix.replace("Oblique", "Italic") if base == "DejaVu Serif" else suffix
        lines.append("%s.%s.latin-1=%s%s" % (name, st, base, s))
lines.append("sequence.allfonts=latin-1")
for face, f in faces.items():
    lines.append("filename.%s=$JRE_LIB_FONTS/%s.ttf" % (face.replace(" ", "_"), f))
with open(out, "w") as o:
    o.write("\n".join(lines) + "\n")
