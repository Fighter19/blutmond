#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the engine's font (assets/engine/font.png) and writes its font file (font.bmfont).

usage: make_engine_font.py <assets/engine>

ASCII 32 to 126 in 8x8 cells, 16 to a row, then the marks of BASE_MARKS (the dialogue box's
"more follows": a filled triangle pointing down). Each glyph is drawn in 5x7 pixels at (1, 0) of its
cell, in white. The dark shadow below right of it is only in the picture: the cooker keeps the
white pixels (1 bit each) and the runtime draws the shadow the same way. Drawn by this script,
so the engine's assets stay free of third-party art.
"""
import os
import sys

from PIL import Image

# 5 columns x 7 rows per glyph, '#' set.
GLYPHS = {
    ' ': ["     "] * 7,
    '!': ["  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "],
    '"': [" # # ", " # # ", "     ", "     ", "     ", "     ", "     "],
    '#': [" # # ", "#####", " # # ", " # # ", "#####", " # # ", "     "],
    '$': ["  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "],
    '%': ["##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##"],
    '&': [" ##  ", "#  # ", "# #  ", " #   ", "# # #", "#  # ", " ## #"],
    "'": ["  #  ", "  #  ", "     ", "     ", "     ", "     ", "     "],
    '(': ["   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "],
    ')': [" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "],
    '*': ["     ", "  #  ", "# # #", " ### ", "# # #", "  #  ", "     "],
    '+': ["     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "],
    ',': ["     ", "     ", "     ", "     ", "  ## ", "   # ", "  #  "],
    '-': ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
    '.': ["     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "],
    '/': ["     ", "    #", "   # ", "  #  ", " #   ", "#    ", "     "],
    '0': [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    '1': ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    '2': [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    '3': ["#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "],
    '4': ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    '5': ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    '6': ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    '7': ["#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "],
    '8': [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    '9': [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    ':': ["     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "],
    ';': ["     ", " ##  ", " ##  ", "     ", " ##  ", "  #  ", " #   "],
    '<': ["    #", "   # ", "  #  ", " #   ", "  #  ", "   # ", "    #"],
    '=': ["     ", "     ", "#####", "     ", "#####", "     ", "     "],
    '>': ["#    ", " #   ", "  #  ", "   # ", "  #  ", " #   ", "#    "],
    '?': [" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "],
    '@': [" ### ", "#   #", "    #", " ## #", "# # #", "# # #", " ### "],
    'A': [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'B': ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    'C': [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    'D': ["###  ", "#  # ", "#   #", "#   #", "#   #", "#  # ", "###  "],
    'E': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    'F': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    'G': [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"],
    'H': ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'I': [" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    'J': ["  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "],
    'K': ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    'L': ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    'M': ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    'N': ["#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"],
    'O': [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'P': ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    'Q': [" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"],
    'R': ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    'S': [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    'T': ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    'U': ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'V': ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    'W': ["#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "],
    'X': ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    'Y': ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    'Z': ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
    '[': [" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "],
    '\\': ["     ", "#    ", " #   ", "  #  ", "   # ", "    #", "     "],
    ']': [" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "],
    '^': ["  #  ", " # # ", "#   #", "     ", "     ", "     ", "     "],
    '_': ["     ", "     ", "     ", "     ", "     ", "     ", "#####"],
    '`': [" #   ", "  #  ", "     ", "     ", "     ", "     ", "     "],
    'a': ["     ", "     ", " ### ", "    #", " ####", "#   #", " ####"],
    'b': ["#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#### "],
    'c': ["     ", "     ", " ### ", "#    ", "#    ", "#   #", " ### "],
    'd': ["    #", "    #", " ## #", "#  ##", "#   #", "#   #", " ####"],
    'e': ["     ", "     ", " ### ", "#   #", "#####", "#    ", " ### "],
    'f': ["  ## ", " #  #", " #   ", "###  ", " #   ", " #   ", " #   "],
    'g': ["     ", " ####", "#   #", "#   #", " ####", "    #", " ### "],
    'h': ["#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#   #"],
    'i': ["  #  ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "],
    'j': ["   # ", "     ", "  ## ", "   # ", "   # ", "#  # ", " ##  "],
    'k': ["#    ", "#    ", "#  # ", "# #  ", "##   ", "# #  ", "#  # "],
    'l': [" ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    'm': ["     ", "     ", "## # ", "# # #", "# # #", "#   #", "#   #"],
    'n': ["     ", "     ", "# ## ", "##  #", "#   #", "#   #", "#   #"],
    'o': ["     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "],
    'p': ["     ", "     ", "#### ", "#   #", "#### ", "#    ", "#    "],
    'q': ["     ", "     ", " ## #", "#  ##", " ####", "    #", "    #"],
    'r': ["     ", "     ", "# ## ", "##  #", "#    ", "#    ", "#    "],
    's': ["     ", "     ", " ### ", "#    ", " ### ", "    #", "#### "],
    't': [" #   ", " #   ", "###  ", " #   ", " #   ", " #  #", "  ## "],
    'u': ["     ", "     ", "#   #", "#   #", "#   #", "#  ##", " ## #"],
    'v': ["     ", "     ", "#   #", "#   #", "#   #", " # # ", "  #  "],
    'w': ["     ", "     ", "#   #", "#   #", "# # #", "# # #", " # # "],
    'x': ["     ", "     ", "#   #", " # # ", "  #  ", " # # ", "#   #"],
    'y': ["     ", "     ", "#   #", "#   #", " ####", "    #", " ### "],
    'z': ["     ", "     ", "#####", "   # ", "  #  ", " #   ", "#####"],
    '{': ["   ##", "  #  ", "  #  ", " #   ", "  #  ", "  #  ", "   ##"],
    '|': ["  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    '}': ["##   ", "  #  ", "  #  ", "   # ", "  #  ", "  #  ", "##   "],
    '~': ["     ", "     ", " #   ", "# # #", "   # ", "     ", "     "],
}

# The accented letters French and German need, and their punctuation: made from the letters
# above. Lowercase letters keep their shape and get the accent in rows 0-1; capitals are
# squeezed to five rows (2-6) under it. A cedilla takes row 7.
ACCENTS = {
    'grave': [" #   ", "  #  "], 'acute': ["   # ", "  #  "], 'circ': ["  #  ", " # # "],
    'uml': ["     ", " # # "], 'uml_cap': [" # # ", "     "],
}
DOTLESS_I = ["     ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "]


def accented(base, accent):
    g = DOTLESS_I if base == 'i' else GLYPHS[base]
    if base.isupper():
        body = [g[r] for r in (0, 1, 3, 5, 6)]
        acc = ACCENTS['uml_cap'] if accent == 'uml' else ACCENTS[accent]
        return acc + body
    return ACCENTS[accent] + g[2:]


def cedilla(base):
    return GLYPHS[base] + ["  #  "]


LATIN = {
    0xA1: ["  #  ", "     ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],             # ¡
    0xAB: ["     ", "  # #", " # # ", "# #  ", " # # ", "  # #", "     "],             # «
    0xB0: [" ##  ", "#  # ", " ##  ", "     ", "     ", "     ", "     "],             # °
    0xBB: ["     ", "# #  ", " # # ", "  # #", " # # ", "# #  ", "     "],             # »
    0xBF: ["  #  ", "     ", "  #  ", " #   ", "#    ", "#   #", " ### "],             # ¿
    0xC0: accented('A', 'grave'), 0xC2: accented('A', 'circ'), 0xC4: accented('A', 'uml'),
    0xC7: cedilla('C'), 0xC8: accented('E', 'grave'), 0xC9: accented('E', 'acute'),
    0xCA: accented('E', 'circ'), 0xCB: accented('E', 'uml'), 0xCE: accented('I', 'circ'),
    0xCF: accented('I', 'uml'), 0xD4: accented('O', 'circ'), 0xD6: accented('O', 'uml'),
    0xD9: accented('U', 'grave'), 0xDB: accented('U', 'circ'), 0xDC: accented('U', 'uml'),
    0xDF: [" ### ", "#   #", "#  # ", "# #  ", "#  # ", "#   #", "# ## "],             # ß
    0xE0: accented('a', 'grave'), 0xE2: accented('a', 'circ'), 0xE4: accented('a', 'uml'),
    0xE7: cedilla('c'), 0xE8: accented('e', 'grave'), 0xE9: accented('e', 'acute'),
    0xEA: accented('e', 'circ'), 0xEB: accented('e', 'uml'), 0xEE: accented('i', 'circ'),
    0xEF: accented('i', 'uml'), 0xF4: accented('o', 'circ'), 0xF6: accented('o', 'uml'),
    0xF9: accented('u', 'grave'), 0xFB: accented('u', 'circ'), 0xFC: accented('u', 'uml'),
    0xFF: accented('y', 'uml'),
    0x152: ["#####", "# #  ", "# #  ", "# ###", "# #  ", "# #  ", "#####"],            # Œ
    0x153: ["     ", "     ", "## # ", "# # #", "# ###", "# #  ", "## ##"],            # œ
    0x2018: ["  #  ", " #   ", "     ", "     ", "     ", "     ", "     "],           # ‘
    0x2019: ["  #  ", "   # ", "     ", "     ", "     ", "     ", "     "],           # ’
    0x201A: ["     ", "     ", "     ", "     ", "     ", "  #  ", " #   "],           # ‚
    0x201C: [" # # ", "# #  ", "     ", "     ", "     ", "     ", "     "],           # “
    0x201D: [" # # ", "  # #", "     ", "     ", "     ", "     ", "     "],           # ”
    0x201E: ["     ", "     ", "     ", "     ", "     ", " # # ", "# #  "],           # „
    0x2026: ["     ", "     ", "     ", "     ", "     ", "     ", "# # #"],           # …
}
# Marks after ASCII in the first font file (cells 95 on), each a range of one.
BASE_MARKS = {
    0x25BC: ["     ", "#####", "#####", " ### ", " ### ", "  #  ", "     "],   # ▼ (more text follows)
}
# Ranges of the second font file, in the order of its cells.
LATIN_RANGES = [(0xA0, 96), (0x152, 2), (0x2018, 7), (0x2026, 1)]

INK = (255, 255, 255, 255)
SHADOW = (40, 40, 48, 255)


def draw(img, cell, glyph, columns):
    rows = len(glyph)
    assert rows in (7, 8) and all(len(r) == 5 for r in glyph), glyph
    ox, oy = (cell % columns) * 8 + 1, (cell // columns) * 8
    ink = {(x, y) for y in range(rows) for x in range(5) if glyph[y][x] == '#'}
    for x, y in ink:
        img.putpixel((ox + x, oy + y), INK)
    for x, y in ink:
        if (x + 1, y + 1) not in ink and y + 1 < 8:
            img.putpixel((ox + x + 1, oy + y + 1), SHADOW)


def main():
    out = sys.argv[1]
    columns = 16
    # font.png: ASCII 32 to 126.
    first, last = 32, 126
    count = last - first + 1
    cells = count + len(BASE_MARKS)
    img = Image.new("RGBA", (columns * 8, ((cells + columns - 1) // columns) * 8), (0, 0, 0, 0))
    for code in range(first, last + 1):
        draw(img, code - first, GLYPHS[chr(code)], columns)
    for k, code in enumerate(sorted(BASE_MARKS)):
        draw(img, count + k, BASE_MARKS[code], columns)
    img.save(os.path.join(out, "font.png"))
    with open(os.path.join(out, "font.bmfont"), "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<font version="1" name="font" image="font.png" cell_w="8" cell_h="8">\n')
        f.write(' <range first="%d" count="%d"/>\n' % (first, count))
        for code in sorted(BASE_MARKS):
            f.write(' <range first="0x%X" count="1"/>\n' % code)
        f.write('</font>\n')
    # font_latin.png: the same font's accented letters and punctuation (French, German).
    cells = sum(n for _, n in LATIN_RANGES)
    img = Image.new("RGBA", (columns * 8, ((cells + columns - 1) // columns) * 8), (0, 0, 0, 0))
    cell = 0
    for start, n in LATIN_RANGES:
        for code in range(start, start + n):
            if code in LATIN:
                draw(img, cell, LATIN[code], columns)
            cell += 1
    img.save(os.path.join(out, "font_latin.png"))
    with open(os.path.join(out, "font_latin.bmfont"), "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<!-- More characters of the font "font": what French and German need. -->\n')
        f.write('<font version="1" name="font" image="font_latin.png" cell_w="8" cell_h="8">\n')
        for start, n in LATIN_RANGES:
            f.write(' <range first="0x%X" count="%d"/>\n' % (start, n))
        f.write('</font>\n')


if __name__ == "__main__":
    main()
