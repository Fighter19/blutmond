#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the engine's effect sprites for the test level and writes their sprite files.

usage: make_effect_sprites.py <assets/test_room>

spark.png, spark.bmsprite: where a guard box stops a hit (bmGameSpark, src/world/game.c). The
engine shows its first animation once, centred on the point the hit met the shield: a white
flash, a star of rays, sparks flying apart, embers. 16x16, the anchor in the middle.
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


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/test_room"
    for f in FRAMES:
        assert len(f) == 16 and all(len(r) == 16 for r in f), f
    img = Image.new("RGBA", (16 * len(FRAMES), 16), (0, 0, 0, 0))
    for i, f in enumerate(FRAMES):
        for y, row in enumerate(f):
            for x, ch in enumerate(row):
                img.putpixel((i * 16 + x, y), PALETTE[ch])
    img.save(os.path.join(out, "spark.png"))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="spark.png">',
             ' <hitbox x="-4" y="-4" w="8" h="8"/>']
    for i in range(len(FRAMES)):
        lines.append(' <frame x="%d" y="0" w="16" h="16" ax="8" ay="8"/>' % (i * 16))
    lines.append(' <animation name="burst" loop="0">')
    for i, d in enumerate(DURATIONS):
        lines.append('  <step frame="%d" duration="%d"/>' % (i, d))
    lines.append(' </animation>')
    lines.append('</sprite>')
    with open(os.path.join(out, "spark.bmsprite"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
