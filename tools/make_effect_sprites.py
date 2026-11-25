#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the engine's effect sprites for the test level and writes their sprite files.

usage: make_effect_sprites.py <assets/test_room>

spark.png, spark.bmsprite: where a guard box stops a hit (bmGameSpark, src/world/game.c). The
engine shows its first animation once, centred on the point the hit met the shield: a white
flash, a star of rays, sparks flying apart, embers. 16x16, the anchor in the middle.

debris.png, debris.bmsprite: a broken wall (bmGameBreak, one per cell) and a broken crate
(effect(#debris)): chunks bursting apart and falling, dust. 16x16, the anchor in the middle.

crate.png, crate.bmsprite: a wooden crate that a hit breaks (scripts/crate.ms). 16x16, the
anchor at the bottom middle.

puzzle.png, puzzle.bmsprite: what physics puzzles are drawn with (src/world/puzzle.h), one
sheet (one palette bank): a stone block bound with iron (32x32, animation block), an iron
portcullis (16x48, gate), a pressure plate up and pressed (32x8, plate, plate_down). Anchored
at their top left.

splash.png, splash.bmsprite: a step in a puddle (reflect.h, bmReflectSplashes): droplets thrown
up and falling back, a ring spreading on the water. 16x16, the anchor at the bottom middle.

grapple.png, grapple.bmsprite: the grapple (src/world/grapple.h): its hook (animation hook,
8x8, pointing right, anchored in the middle), a link of its chain (link, 8x8), and the ring of
an anchor it bites (anchor, 16x16, glinting; anchored in the middle).
Everything here is drawn by this script.
"""
import os
import sys

from PIL import Image

PALETTE = {
    '.': (0, 0, 0, 0),
    'W': (255, 255, 255, 255),   # the flash
    'Y': (255, 240, 140, 255),   # hot
    'O': (255, 176, 48, 255),    # sparks
    'R': (208, 88, 24, 255),     # embers
    'o': (32, 24, 24, 255),      # outline
    'd': (84, 76, 80, 255),      # stone, dark
    's': (140, 134, 136, 255),   # stone
    'l': (196, 190, 188, 255),   # stone, lit; dust
    'w': (120, 74, 38, 255),     # wood
    'b': (168, 112, 58, 255),    # wood, lit
    'k': (80, 46, 24, 255),      # wood, dark
    'i': (58, 62, 74, 255),      # iron
    'I': (110, 118, 136, 255),   # iron, lit
    'r': (150, 60, 40, 255),     # rust
    'g': (180, 136, 36, 255),    # brass
    'G': (250, 220, 90, 255),    # brass, lit
    'B': (232, 244, 255, 255),   # water, the light on it
    'u': (150, 190, 230, 255),   # water, lit
    'n': (90, 130, 180, 255),    # water
}

FRAMES = [
    [   # 0: the flash
        "................",
        "................",
        "................",
        "................",
        "................",
        ".......Y........",
        "......YWY.......",
        ".....YWWWY......",
        "......YWY.......",
        ".......Y........",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ],
    [   # 1: a star of rays
        "................",
        "................",
        ".......O........",
        "...O...Y...O....",
        "....Y..Y..Y.....",
        ".....Y.W.Y......",
        "......YWY.......",
        "..OYYWWWWWYYO...",
        "......YWY.......",
        ".....Y.W.Y......",
        "....Y..Y..Y.....",
        "...O...Y...O....",
        ".......O........",
        "................",
        "................",
        "................",
    ],
    [   # 2: sparks flying apart
        "................",
        ".......O........",
        "..O.........O...",
        "...Y.......Y....",
        "................",
        "................",
        ".......W........",
        ".O....W.W....O..",
        ".......W........",
        "................",
        "................",
        "...Y.......Y....",
        "..O.........O...",
        ".......O........",
        "................",
        "................",
    ],
    [   # 3: embers
        "................",
        "................",
        ".R...........R..",
        "................",
        "................",
        "................",
        "................",
        "R.............R.",
        "................",
        "................",
        "................",
        "................",
        ".R...........R..",
        "................",
        ".......R........",
        "................",
    ],
]

DURATIONS = [2, 3, 3, 3]

DEBRIS = [
    [   # 0: bursting
        "................",
        "................",
        "................",
        "................",
        ".....od..do.....",
        "....oss..ssd....",
        "......llll......",
        "...dso.ll.osd...",
        "...dss.ll.ssd...",
        "......llll......",
        "....dss..sso....",
        ".....do..od.....",
        "................",
        "................",
        "................",
        "................",
    ],
    [   # 1: apart
        "................",
        "..od........do..",
        "..ss........ss..",
        "................",
        "......l..l......",
        "................",
        "dso....ll....osd",
        "dss....ll....ssd",
        "................",
        "......l..l......",
        "................",
        "..ss........ss..",
        "..do........od..",
        "................",
        "................",
        "................",
    ],
    [   # 2: falling
        "................",
        "................",
        "................",
        ".od..........do.",
        ".ss..........ss.",
        "................",
        ".....l....l.....",
        "................",
        "ds............sd",
        "dd............dd",
        "................",
        "......l..l......",
        "................",
        ".ss..........ss.",
        ".do..........od.",
        "................",
    ],
    [   # 3: dust
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "o..............o",
        "................",
        "....l......l....",
        "................",
        "d..............d",
        "................",
        "..l..........l..",
        "................",
        "s..............s",
        "................",
    ],
]

DEBRIS_DURATIONS = [3, 4, 4, 4]

CRATE = [
    "................",
    "oooooooooooooooo",
    "obbbbbbbbbbbbbbo",
    "obkkkkkkkkkkkkwo",
    "obkbw......bwkwo",
    "obk.bw....bw.kwo",
    "obk..bw..bw..kwo",
    "obk...bwbw...kwo",
    "obk....bw....kwo",
    "obk...bwbw...kwo",
    "obk..bw..bw..kwo",
    "obk.bw....bw.kwo",
    "obkbw......bwkwo",
    "obkkkkkkkkkkkkwo",
    "owwwwwwwwwwwwwwo",
    "oooooooooooooooo",
]
# Its open parts: dark wood behind the slats.
CRATE = [r.replace('.', 'k') for r in CRATE]
CRATE[0] = "................"


def block():
    rows = []
    for y in range(32):
        row = []
        for x in range(32):
            edge = x in (0, 31) or y in (0, 31)
            band = y in (5, 6, 25, 26)
            rivet = band and x in (3, 10, 21, 28)
            if edge:
                c = 'o'
            elif rivet:
                c = 'I'
            elif band:
                c = 'i' if y in (6, 26) else 'I'
            elif y == 1 or x == 1:
                c = 'l'
            elif y == 30 or x == 30:
                c = 'd'
            elif (x + 2 * y) % 13 == 0 or (3 * x + y) % 17 == 0:
                c = 'd'          # the stone's grain
            else:
                c = 's'
            row.append(c)
        rows.append("".join(row))
    # A crack across the middle.
    for x, y in ((8, 12), (9, 13), (10, 13), (11, 14), (12, 15), (13, 15), (14, 16), (15, 17)):
        rows[y] = rows[y][:x] + 'o' + rows[y][x + 1:]
    return rows


def gate():
    rows = []
    for y in range(48):
        row = []
        for x in range(16):
            bar = x in (1, 2, 7, 8, 13, 14)
            cross = y % 12 in (4, 5)
            spike = y >= 44
            if spike and bar:
                c = 'I' if (x in (1, 7, 13)) and y < 46 else ('i' if y < 47 else '.')
            elif spike:
                c = '.'
            elif bar and cross:
                c = 'I'
            elif bar:
                c = 'I' if x in (1, 7, 13) else 'i'
            elif cross:
                c = 'r' if (x + y) % 5 == 0 else ('I' if y % 12 == 4 else 'o')
            else:
                c = '.'
            row.append(c)
        rows.append("".join(row))
    return rows


def plate(pressed):
    top = 5 if pressed else 2
    rows = []
    for y in range(8):
        row = []
        for x in range(32):
            if y < top:
                c = '.'
            elif y == top:
                c = 'o' if x in (0, 31) else 'I'
            elif y == 7:
                c = 'o'
            else:
                c = 'o' if x in (0, 31) else ('i' if (x // 4) % 2 else 'd')
            row.append(c)
        rows.append("".join(row))
    return rows


HOOK = [
    "........",
    "...oo...",
    "..oIIo..",
    "oooiIIo.",
    "oIIIIIIo",
    "oooiIIo.",
    "..oIIo..",
    "...oo...",
]
LINK = [
    "........",
    "........",
    "...oo...",
    "..oIio..",
    "..oiIo..",
    "...oo...",
    "........",
    "........",
]


def ring(glint):
    rows = []
    for y in range(16):
        row = ""
        for x in range(16):
            d2 = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if 30 <= d2 <= 56:
                c = 'G' if (glint and x + y < 12) else ('g' if x + y < 16 else 'i')
            elif 56 < d2 <= 72 or 20 <= d2 < 30:
                c = 'o'
            else:
                c = '.'
            row += c
        rows.append(row)
    return rows


def grapple_sheet(out):
    frames = [HOOK, LINK]
    img = Image.new("RGBA", (48, 16), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        for y, row in enumerate(f):
            for x, ch in enumerate(row):
                img.putpixel((i * 8 + x, y), PALETTE[ch])
    for i, glint in enumerate((False, True)):
        for y, row in enumerate(ring(glint)):
            for x, ch in enumerate(row):
                img.putpixel((16 + i * 16 + x, y), PALETTE[ch])
    img.save(os.path.join(out, "grapple.png"))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="grapple.png">',
             ' <hitbox x="-4" y="-4" w="8" h="8"/>',
             ' <frame x="0" y="0" w="8" h="8" ax="4" ay="4"/>',
             ' <frame x="8" y="0" w="8" h="8" ax="4" ay="4"/>',
             ' <frame x="16" y="0" w="16" h="16" ax="8" ay="8"/>',
             ' <frame x="32" y="0" w="16" h="16" ax="8" ay="8"/>',
             ' <animation name="hook" loop="1">', '  <step frame="0" duration="60"/>', ' </animation>',
             ' <animation name="link" loop="1">', '  <step frame="1" duration="60"/>', ' </animation>',
             ' <animation name="anchor" loop="1">', '  <step frame="2" duration="50"/>', '  <step frame="3" duration="10"/>', ' </animation>',
             '</sprite>']
    with open(os.path.join(out, "grapple.bmsprite"), "w") as f:
        f.write("\n".join(lines) + "\n")


def puzzle_sheet(out):
    img = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    def put(px, x0, y0):
        for y, row in enumerate(px):
            for x, ch in enumerate(row):
                img.putpixel((x0 + x, y0 + y), PALETTE[ch])
    put(block(), 0, 0)
    put(gate(), 32, 0)
    put(plate(False), 0, 32)
    put(plate(True), 0, 40)
    img.save(os.path.join(out, "puzzle.png"))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="puzzle.png">',
             ' <hitbox x="0" y="0" w="32" h="32"/>',
             ' <frame x="0" y="0" w="32" h="32" ax="0" ay="0"/>',
             ' <frame x="32" y="0" w="16" h="48" ax="0" ay="0"/>',
             ' <frame x="0" y="32" w="32" h="8" ax="0" ay="0"/>',
             ' <frame x="0" y="40" w="32" h="8" ax="0" ay="0"/>']
    for i, name in enumerate(("block", "gate", "plate", "plate_down")):
        lines += [' <animation name="%s" loop="1">' % name, '  <step frame="%d" duration="60"/>' % i, ' </animation>']
    lines.append('</sprite>')
    with open(os.path.join(out, "puzzle.bmsprite"), "w") as f:
        f.write("\n".join(lines) + "\n")


def splash_frames():
    """A step in a puddle: droplets thrown up in a crown and falling back, a ring spreading on
    the water. 16x16, the anchor at the bottom middle (the water line under the foot)."""
    def frame(drops, ring, ring_char):
        rows = [["."] * 16 for _ in range(16)]
        for x, y, c in drops:
            rows[y][x] = c
        x0, x1 = ring
        for x in range(x0, x1 + 1):
            # The ring as an ellipse seen from the side: its ends a pixel up.
            y = 14 if x in (x0, x1) else 15
            if (x - x0) % 2 == 0 or ring_char != 'n':
                rows[y][x] = ring_char
        return ["".join(r) for r in rows]
    crown = [(6, 13, 'B'), (9, 13, 'B'), (5, 12, 'u'), (10, 12, 'u'), (7, 12, 'B'), (8, 12, 'B')]
    up = [(4, 10, 'u'), (11, 10, 'u'), (6, 9, 'B'), (9, 9, 'B'), (5, 11, 'n'), (10, 11, 'n'), (7, 11, 'u'), (8, 11, 'u')]
    top = [(3, 8, 'u'), (12, 8, 'u'), (5, 6, 'B'), (10, 6, 'B'), (7, 7, 'u'), (8, 8, 'n'), (2, 10, 'n'), (13, 10, 'n')]
    down = [(2, 12, 'n'), (13, 12, 'n'), (4, 11, 'u'), (11, 11, 'u'), (6, 10, 'n'), (9, 10, 'n')]
    return [
        frame(crown, (5, 10), 'u'),
        frame(up, (4, 11), 'u'),
        frame(top, (3, 12), 'u'),
        frame(down, (2, 13), 'n'),
        frame([], (1, 14), 'n'),
    ]


SPLASH_DURATIONS = [3, 3, 4, 4, 4]


def sheet(path, frames):
    for f in frames:
        assert len(f) == 16 and all(len(r) == 16 for r in f), f
    img = Image.new("RGBA", (16 * len(frames), 16), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        for y, row in enumerate(f):
            for x, ch in enumerate(row):
                img.putpixel((i * 16 + x, y), PALETTE[ch])
    img.save(path)


def sprite_file(path, image, count, anchor, hitbox, anim, durations, loop, light=""):
    """`light`: attributes for lit rooms (sprite-format.md), e.g. ' normal="auto"'."""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="%s"%s>' % (image, light),
             ' <hitbox x="%d" y="%d" w="%d" h="%d"/>' % hitbox]
    for i in range(count):
        lines.append(' <frame x="%d" y="0" w="16" h="16" ax="%d" ay="%d"/>' % (i * 16, anchor[0], anchor[1]))
    lines.append(' <animation name="%s" loop="%d">' % (anim, 1 if loop else 0))
    for i, d in enumerate(durations):
        lines.append('  <step frame="%d" duration="%d"/>' % (i, d))
    lines.append(' </animation>')
    lines.append('</sprite>')
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/test_room"
    sheet(os.path.join(out, "spark.png"), FRAMES)
    sprite_file(os.path.join(out, "spark.bmsprite"), "spark.png", len(FRAMES), (8, 8), (-4, -4, 8, 8), "burst", DURATIONS, False)
    sheet(os.path.join(out, "debris.png"), DEBRIS)
    sprite_file(os.path.join(out, "debris.bmsprite"), "debris.png", len(DEBRIS), (8, 8), (-4, -4, 8, 8), "burst", DEBRIS_DURATIONS, False)
    sheet(os.path.join(out, "crate.png"), [CRATE])
    sprite_file(os.path.join(out, "crate.bmsprite"), "crate.png", 1, (8, 16), (-7, -15, 14, 15), "idle", [60], True)
    puzzle_sheet(out)
    grapple_sheet(out)
    sheet(os.path.join(out, "splash.png"), splash_frames())
    # Lit by the room's lights (the street's lamp is over its puddle), the bright drops shining.
    sprite_file(os.path.join(out, "splash.bmsprite"), "splash.png", len(SPLASH_DURATIONS), (8, 16), (-4, -4, 8, 4), "splash",
                SPLASH_DURATIONS, False, ' normal="auto" shiny="#e8f4ff"')


if __name__ == "__main__":
    main()
