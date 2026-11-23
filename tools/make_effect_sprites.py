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


def sheet(path, frames):
    for f in frames:
        assert len(f) == 16 and all(len(r) == 16 for r in f), f
    img = Image.new("RGBA", (16 * len(frames), 16), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        for y, row in enumerate(f):
            for x, ch in enumerate(row):
                img.putpixel((i * 16 + x, y), PALETTE[ch])
    img.save(path)


def sprite_file(path, image, count, anchor, hitbox, anim, durations, loop):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="%s">' % image,
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


if __name__ == "__main__":
    main()
