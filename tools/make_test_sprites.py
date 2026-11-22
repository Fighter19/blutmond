#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the test sprites (assets/test_room/player.png, bat.png) and writes their sprite files.

usage: make_test_sprites.py <assets/test_room>

The player is 16x32 with the anchor at the feet; the animations are the ones the engine's
player looks for (idle, run, jump, fall, climb) and the weapons' swing (slash, 32 wide). The
bat's mouth, below the middle of its body, is its hit box `bite`. Everything here is drawn by this script, so
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
    'b': (214, 222, 239, 255),  # blade
    'c': (156, 107, 41, 255),   # hilt
    'd': (90, 24, 48, 255),     # moon bat body
    'e': (52, 16, 40, 255),     # moon bat wings
    'f': (255, 64, 48, 255),    # moon bat eyes, fire's heart
    'g': (255, 160, 32, 255),   # fire
    'h': (255, 240, 140, 255),  # fire's core, glint
    'i': (140, 96, 56, 255),    # bag
    'j': (196, 150, 90, 255),   # bag, light
    'k': (222, 160, 120, 255),  # skin, shade
    'l': (120, 20, 36, 255),    # hair, shade
    'm': (130, 10, 24, 255),    # coat, shade
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

def slash_frame(torso, blade):
    """32x32: the player on the left 16 columns, a sword drawn by `blade` (x, y, colour)."""
    rows = [list(r + "." * 16) for r in player_frame(torso, legs(-2, 2))]
    for x, y, c in blade:
        rows[y][x] = c
    return ["".join(r) for r in rows]


# Raised behind the head, straight ahead at shoulder height, down in front.
SLASH_FRAMES = [
    slash_frame(TORSO_ARMS_UP_L, [(3, 7, 'c')] + [(3, y, 'b') for y in range(1, 7)] + [(2, 7, 'c'), (4, 7, 'c')]),
    slash_frame(TORSO, [(13, 13, '3'), (14, 13, 'c'), (14, 12, 'c'), (14, 14, 'c')] + [(x, 13, 'b') for x in range(15, 29)] + [(29, 13, 'b')]
                + [(x, 12, 'b') for x in range(16, 28)]),
    slash_frame(TORSO, [(13, 15, 'c')] + [(13 + i, 16 + i, 'b') for i in range(1, 9)]),
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


# The moon bat (the boss): the bat drawn twice as large in its own colours, with a third frame
# for the dive (wings folded).
def doubled(frame, colours):
    out = []
    for row in frame:
        r = "".join(colours.get(c, c) * 2 for c in row)
        out += [r, r]
    return out


MOON = {'8': 'd', '9': 'e', 'a': 'f'}
MOON_FRAMES = [doubled(BAT_FRAMES[0], MOON), doubled(BAT_FRAMES[1], MOON), [r.ljust(32, '.')[:32] for r in [
    "................................", "................................", "................................",
    "................................", "..............dddd..............", "............dddddddd............",
    "...........dddddddddd...........", "..........eedddddddddee..........", ".........eeedfddddfddeee.........",
    "........eeeedfddddfdddeeee......".ljust(32, '.')[:32], "........eeeedddddddddeeee.......".ljust(32, '.')[:32],
    ".........eeeddddddddeeee........".ljust(32, '.')[:32], "..........eeeddddddeeee.........".ljust(32, '.')[:32],
    "...........eeeddddeeee..........", "............eeddddee............", ".............dddddd.............",
    "..............dddd..............", "..............d..d..............", "..............d..d..............",
] + ["." * 32] * 13]]

FIREBALL_FRAMES = [
    ["..ff....", ".fggf...", "fghhgf..", "fghhggf.", ".fgggf..", "..ff....", "........", "........"],
    ["...ff...", "..fggf..", ".fghhgf.", ".fghhgf.", "..fggf..", "...ff...", "........", "........"],
]

BAG_FRAMES = [
    ["................", "................", "................", "................", "......hh........",
     ".....i..i.......", "......ii........", ".....iiii.......", "....ijjjii......", "...ijjjjiii.....",
     "...ijjjiiii.....", "...iiiiiiii.....", "....iiiiii......", "................", "................", "................"],
    ["................", "................", "................", ".......h........", "......h.h.......",
     ".....i.hi.......", "......ii........", ".....iiii.......", "....ijjjii......", "...ijjjjiii.....",
     "...ijjjiiii.....", "...iiiiiiii.....", "....iiiiii......", "................", "................", "................"],
]


def face(kind):
    """A 32 x 32 portrait for the dialogue box: 'hero' (the player's red hair and coat) or
    'vesper' (the moon bat's face, ears and eyes). Drawn from shapes, row by row."""
    rows = [['.'] * 32 for _ in range(32)]

    def put(x, y, c):
        if 0 <= x < 32 and 0 <= y < 32:
            rows[y][x] = c

    def ellipse(cx, cy, rx, ry, c):
        for y in range(32):
            for x in range(32):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    put(x, y, c)

    if kind == 'hero':
        ellipse(15.5, 34, 14, 9, '4')          # shoulders, the coat
        ellipse(15.5, 36, 9, 7, 'm')
        for y in range(25, 29):                # neck
            for x in range(13, 19):
                put(x, y, 'k')
        ellipse(15.5, 26.5, 4, 2, '5')         # collar
        ellipse(15.5, 15, 8, 10, '3')          # face
        ellipse(15.5, 7, 10, 6, '2')           # hair on top
        for y in range(7, 20):                 # hair at the sides
            put(6, y, '2'); put(7, y, '2'); put(24, y, '2'); put(25, y, '2')
            if y > 12:
                put(8, y, 'l'); put(23, y, 'l')
        for x in range(9, 23):                 # the fringe
            put(x, 9 + (x % 3 == 0), '2')
        for x in (12, 13, 18, 19):             # eyes
            put(x, 15, '7'); put(x, 14, '7' if x in (12, 19) else '5')
        for x in range(14, 18):                # mouth
            put(x, 20, 'k')
        put(15, 21, 'k'); put(16, 21, 'k')
    else:
        for i in range(10):                    # ears
            for x in range(4 + i // 2, 11 - i // 3):
                put(x, 3 + i, 'e')
                put(31 - x, 3 + i, 'e')
        ellipse(15.5, 18, 12, 11, 'd')         # head
        ellipse(15.5, 30, 15, 6, 'e')          # wings folded below
        ellipse(10, 15, 3, 2, 'f')             # eyes
        ellipse(21, 15, 3, 2, 'f')
        put(10, 15, 'h'); put(21, 15, 'h')
        for x in range(11, 21):                # mouth
            put(x, 23, 'e')
        for y in (24, 25):                     # fangs
            put(12, y, 'b'); put(19, y, 'b')
        put(12, 26, 'b'); put(19, 26, 'b')
    return ["".join(r) for r in rows]


def sheet(frames, columns, w, h):
    rows = (len(frames) + columns - 1) // columns
    img = Image.new("RGBA", (columns * w, rows * h), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        for y in range(h):
            for x in range(w):
                img.putpixel(((i % columns) * w + x, (i // columns) * h + y), PALETTE[frame[y][x]])
    return img


def sprite_file(path, image, frames, columns, w, h, anchor, hitbox, animations, boxes=(), extra=()):
    """`boxes`: sprite-wide boxes (kind, name, x, y, w, h); `extra`: frames of other sizes
    after the regular ones, as (x, y, w, h, ax, ay)."""
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="%s">' % image]
    out.append(' <hitbox x="%d" y="%d" w="%d" h="%d"/>' % hitbox)
    for kind, name, bx, by, bw, bh in boxes:
        out.append(' <box kind="%s" name="%s" x="%d" y="%d" w="%d" h="%d"/>' % (kind, name, bx, by, bw, bh))
    for i in range(frames):
        out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d"/>'
                   % ((i % columns) * w, (i // columns) * h, w, h, anchor[0], anchor[1]))
    for f in extra:
        out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d"/>' % f)
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
    img = sheet(PLAYER_FRAMES, 8, 16, 32)
    # The swing's frames are 32 wide, after the climb frames in the second row.
    for i, frame in enumerate(SLASH_FRAMES):
        for y in range(32):
            for x in range(32):
                img.putpixel((32 + i * 32 + x, 32 + y), PALETTE[frame[y][x]])
    img.save(os.path.join(out, "player.png"))
    extra = [(32 + i * 32, 32, 32, 32, 8, 32) for i in range(len(SLASH_FRAMES))]
    sprite_file(os.path.join(out, "player.bmsprite"), "player.png", len(PLAYER_FRAMES), 8, 16, 32, (8, 32), (-5, -30, 10, 30), [
        ("idle", True, [(0, 40), (1, 40)]),
        ("run", True, [(2, 6), (3, 6), (4, 6), (5, 6)]),
        ("jump", False, [(6, 1)]),
        ("fall", False, [(7, 1)]),
        ("climb", True, [(8, 10), (9, 10)]),
        ("slash", False, [(10, 4), (11, 4), (12, 8)]),
    ], extra=extra)
    sheet(BAT_FRAMES, 2, 16, 16).save(os.path.join(out, "bat.png"))
    sprite_file(os.path.join(out, "bat.bmsprite"), "bat.png", len(BAT_FRAMES), 2, 16, 16, (8, 12), (-6, -8, 12, 8), [
        ("idle", True, [(0, 8), (1, 8)]),
    ], boxes=[("hit", "bite", -3, -4, 6, 4)])
    for f in MOON_FRAMES:
        assert len(f) == 32 and all(len(r) == 32 for r in f), f
    sheet(MOON_FRAMES, 3, 32, 32).save(os.path.join(out, "moon_bat.png"))
    sprite_file(os.path.join(out, "moon_bat.bmsprite"), "moon_bat.png", len(MOON_FRAMES), 3, 32, 32, (16, 24), (-12, -16, 24, 16), [
        ("idle", True, [(0, 6), (1, 6)]),
        ("dive", True, [(2, 4)]),
    ], boxes=[("hit", "bite", -6, -8, 12, 8)])
    sheet(FIREBALL_FRAMES, 2, 8, 8).save(os.path.join(out, "fireball.png"))
    sprite_file(os.path.join(out, "fireball.bmsprite"), "fireball.png", len(FIREBALL_FRAMES), 2, 8, 8, (3, 6), (-3, -6, 6, 6), [
        ("idle", True, [(0, 4), (1, 4)]),
    ], boxes=[("hit", "core", -3, -6, 6, 6)])
    # Portraits for the dialogue box (records of Speaker, data/speakers.bmdata): the anchor is
    # their top left corner.
    for kind in ("hero", "vesper"):
        sheet([face(kind)], 1, 32, 32).save(os.path.join(out, kind + "_face.png"))
        sprite_file(os.path.join(out, kind + "_face.bmsprite"), kind + "_face.png", 1, 1, 32, 32, (0, 0), (0, 0, 32, 32), [
            ("neutral", True, [(0, 60)]),
        ])
    sheet(BAG_FRAMES, 2, 16, 16).save(os.path.join(out, "drop.png"))
    sprite_file(os.path.join(out, "drop.bmsprite"), "drop.png", len(BAG_FRAMES), 2, 16, 16, (8, 13), (-5, -9, 10, 9), [
        ("idle", True, [(0, 20), (1, 20)]),
    ])


if __name__ == "__main__":
    main()
