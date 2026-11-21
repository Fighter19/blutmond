#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the test sprites (assets/test_room/player.png, bat.png) and writes their sprite files.

usage: make_test_sprites.py <assets/test_room>

The player is 16x32 with the anchor at the feet; the animations are the ones the engine's
player looks for (idle, run, jump, fall, climb). Everything here is drawn by this script, so
the test assets stay free of third-party art.
"""
import os
import sys

from PIL import Image

PALETTE = {
    '.': (0, 0, 0, 0),
    '1': (49, 33, 33, 255),     # boots
    '2': (165, 33, 49, 255),    # hair
    '3': (255, 198, 148, 255),  # skin
    '4': (181, 16, 33, 255),    # coat
    '5': (231, 214, 181, 255),  # shirt
    '6': (49, 49, 99, 255),     # trousers
    '7': (16, 16, 24, 255),     # eyes, outline
    '8': (107, 66, 140, 255),   # bat body
    '9': (66, 41, 99, 255),     # bat wings
    'a': (255, 231, 99, 255),   # bat eyes
}

HEAD = [
    "................", "......2222......", ".....222222.....", ".....233332.....",
    ".....237373.....", ".....233332.....", "......2332......", ".......22.......",
]
TORSO = [
    ".....444444.....", "....44444444....", "...4445555444...", "...4455555544...",
    "...4455555544...", "...3.455554.3...", "...3.455554.3...", "...3.455554.3...",
    ".....444444.....",
]
TORSO_ARMS_UP_L = [
    "...3.444444.....", "...344444444....", "...4445555444...", "....455555544...",
    "....455555544...", ".....455554.3...", ".....455554.3...", ".....455554.3...",
    ".....444444.....",
]
TORSO_ARMS_UP_R = [row[::-1] for row in TORSO_ARMS_UP_L]


def legs(left, right, lift_l=0, lift_r=0):
    """15 rows (17..31): two legs, each shifted sideways towards the foot and lifted."""
    rows = [["."] * 16 for _ in range(15)]

    def leg(x0, shift, lift):
        length = 13 - lift
        for i in range(length):
            x = x0 + round(shift * i / max(1, length - 1))
            boot = i >= length - 3
            for dx in range(2):
                if 0 <= x + dx < 16:
                    rows[i][x + dx] = '1' if boot else '6'
            if boot and i >= length - 2:
                tip = x + (2 if shift >= 0 else -1)
                if 0 <= tip < 16:
                    rows[i][tip] = '1'

    leg(5, left, lift_l)
    leg(9, right, lift_r)
    rows[0] = list(".....666666.....")
    return ["".join(r) for r in rows]


def player_frame(torso, leg_rows, bob=0):
    rows = HEAD + torso + leg_rows
    rows = rows[:32]
    if bob:
        # Head and torso one pixel lower (breathing, landing).
        rows = ["................"] + rows[:16] + rows[17:]
    return rows


PLAYER_FRAMES = [
    player_frame(TORSO, legs(0, 0)),                    # 0 idle
    player_frame(TORSO, legs(0, 0), bob=1),             # 1 idle, breathing
    player_frame(TORSO, legs(-3, 3)),                   # 2 run: stride
    player_frame(TORSO, legs(-1, 1, lift_r=2), bob=1),  # 3 run: passing
    player_frame(TORSO, legs(3, -3)),                   # 4 run: other stride
    player_frame(TORSO, legs(1, -1, lift_l=2), bob=1),  # 5 run: passing
    player_frame(TORSO_ARMS_UP_L, legs(-1, 2, lift_l=3, lift_r=1)),  # 6 jump
    player_frame(TORSO, legs(-2, 2, lift_l=1, lift_r=1)),            # 7 fall
    player_frame(TORSO_ARMS_UP_L, legs(0, 0, lift_r=3)),             # 8 climb
    player_frame(TORSO_ARMS_UP_R, legs(0, 0, lift_l=3)),             # 9 climb
]

BAT_FRAMES = [
    [
        "................", "................", "9..............9", "99............99",
        "999....88....999", "9999..8888..9999", ".9999888888999 .".replace(" ", "9"), "..99a8888a899...".replace("a8888a8", "8a88a88"),
        "...9888888889...", "....88888888....", ".....888888.....", "......8..8......",
        "................", "................", "................", "................",
    ],
    [
        "................", "................", "................", "................",
        ".......88.......", "......8888......", "..9988888888999.", ".9998a88a889999.".replace("8a88a88", "8a88a88"),
        "999988888888 999".replace(" ", "9"), "999.88888888.999", "99...888888...99", "9.....8..8.....9",
        "................", "................", "................", "................",
    ],
]


def sheet(frames, columns, w, h):
    rows = (len(frames) + columns - 1) // columns
    img = Image.new("RGBA", (columns * w, rows * h), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        for y in range(h):
            for x in range(w):
                img.putpixel(((i % columns) * w + x, (i // columns) * h + y), PALETTE[frame[y][x]])
    return img


def sprite_file(path, image, frames, columns, w, h, anchor, hitbox, animations):
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="%s">' % image]
    out.append(' <hitbox x="%d" y="%d" w="%d" h="%d"/>' % hitbox)
    for i in range(frames):
        out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d"/>'
                   % ((i % columns) * w, (i // columns) * h, w, h, anchor[0], anchor[1]))
    for name, loop, steps in animations:
        out.append(' <animation name="%s" loop="%d">' % (name, 1 if loop else 0))
        for frame, duration in steps:
            out.append('  <step frame="%d" duration="%d"/>' % (frame, duration))
        out.append(' </animation>')
    out.append('</sprite>')
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    out = sys.argv[1]
    sheet(PLAYER_FRAMES, 8, 16, 32).save(os.path.join(out, "player.png"))
    sprite_file(os.path.join(out, "player.bmsprite"), "player.png", len(PLAYER_FRAMES), 8, 16, 32, (8, 32), (-5, -30, 10, 30), [
        ("idle", True, [(0, 40), (1, 40)]),
        ("run", True, [(2, 6), (3, 6), (4, 6), (5, 6)]),
        ("jump", False, [(6, 1)]),
        ("fall", False, [(7, 1)]),
        ("climb", True, [(8, 10), (9, 10)]),
    ])
    sheet(BAT_FRAMES, 2, 16, 16).save(os.path.join(out, "bat.png"))
    sprite_file(os.path.join(out, "bat.bmsprite"), "bat.png", len(BAT_FRAMES), 2, 16, 16, (8, 12), (-6, -8, 12, 8), [
        ("idle", True, [(0, 8), (1, 8)]),
    ])


if __name__ == "__main__":
    main()
