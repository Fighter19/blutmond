#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the test sprites (assets/sources/player.png, assets/test_room/bat.png, ...) and writes
their sprite files. The player is drawn whole; the game shows it as legs and torso, split from
it by tools/split_sprite.py (assets/test_room/player.bmfigure says how).

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
    'n': (96, 60, 36, 255),     # door planks, dark
    'o': (128, 84, 50, 255),    # door planks
    'p': (88, 92, 104, 255),    # iron
    'q': (150, 156, 170, 255),  # iron, light
    'r': (14, 10, 22, 255),     # doorway, darkest
    's': (30, 24, 40, 255),     # doorway, dark
    't': (48, 40, 58, 255),     # doorway, the edge's light
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

def sitting_legs():
    """15 rows (17..31): sitting on a seat at hip height (a bench, 15 pixels up), facing right:
    the thighs along the seat, the shins down to the ground, the boots forward."""
    rows = [["."] * 16 for _ in range(15)]
    for y in (1, 2):
        for x in range(5, 13):
            rows[y][x] = '6'
    for y in range(3, 15):
        for x in (11, 12):
            rows[y][x] = '1' if y >= 12 else '6'
    for y in (13, 14):
        rows[y][13] = '1'
    rows[0] = list(".....666666.....")
    return ["".join(r) for r in rows]


# Sitting (a bench to rest on, assets/test_room/scripts/bench.ms): still and breathing.
SIT_FRAMES = [
    player_frame(TORSO, sitting_legs()),
    player_frame(TORSO, sitting_legs(), bob=1),
]

# Pushing (a push block, src/world/puzzle.h): leaning into it, both arms straight ahead, the
# back leg braced far behind.
TORSO_PUSH = [
    ".....444444.....", "....44444444....", "...444555544444.", "...445555544333.",
    "...44555554.33..", "....4555554.....", "....4555554.....", "....4555554.....",
    ".....444444.....",
]


def leaning(rows, n):
    """Head and torso (the first 17 rows) n pixels ahead (to the right)."""
    return [("." * n + r[:16 - n]) if i < 17 else r for i, r in enumerate(rows)]


# The arms of TORSO_PUSH reach to column 14; leaning 1 more keeps the hands in the frame.


PUSH_FRAMES = [
    leaning(player_frame(TORSO_PUSH, legs(-4, 1)), 1),
    leaning(player_frame(TORSO_PUSH, legs(-3, 2, lift_r=1), bob=1), 1),
    leaning(player_frame(TORSO_PUSH, legs(-4, 0)), 1),
    leaning(player_frame(TORSO_PUSH, legs(-2, 1, lift_l=2), bob=1), 1),
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


def door_panel():
    """A door of planks, 32 x 48, with two iron bands, studs and a ring: its left edge is the
    hinge (the anchor at the bottom left)."""
    rows = []
    for y in range(48):
        row = ""
        for x in range(32):
            if x == 0 or x == 31 or y == 0 or y == 47:
                c = '7'
            elif y in (8, 9, 38, 39):
                c = 'q' if y in (8, 38) else 'p'
                if x % 6 == 3 and y in (8, 38):
                    c = 'b'                       # a stud
            elif x % 8 == 0:
                c = 'n'                           # the gap between planks
            else:
                c = 'o' if (x // 8 + y // 12) % 2 == 0 or (x + y) % 7 else 'n'
            row += c
        rows.append(row)
    # The ring, right of the middle.
    for dy, dx in ((22, 25), (23, 24), (23, 26), (24, 24), (24, 26), (25, 25)):
        rows[dy] = rows[dy][:dx] + 'p' + rows[dy][dx + 1:]
    return rows


def doorway():
    """The dark behind a door in the back wall, 32 x 48: the opening's sides and top catch a
    little light, the depth is near black."""
    rows = []
    for y in range(48):
        row = ""
        for x in range(32):
            if y < 2 or x < 2:
                c = 't'                           # the frame's inner edge, lit
            elif y < 6 or x < 5 or x > 28:
                c = 's'
            else:
                c = 'r'
            row += c
        rows.append(row)
    return rows


def sheet(frames, columns, w, h):
    rows = (len(frames) + columns - 1) // columns
    img = Image.new("RGBA", (columns * w, rows * h), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        for y in range(h):
            for x in range(w):
                img.putpixel(((i % columns) * w + x, (i // columns) * h + y), PALETTE[frame[y][x]])
    return img


# Aiming (layered-sprites.md §6): the torso with its right arm pointing from straight up to
# straight down in nine steps (0 forward, 64 down, 192 up), the hand at the arm's end; drawn
# 24 wide so the arm has room ahead. The legs stand.
AIM_ANGLES = [192, 208, 224, 240, 0, 16, 32, 48, 64]
AIM_SHOULDER = (11, 9)
AIM_REACH = 6


def aim_frame(angle):
    import math
    torso = [r[:12] + "." * 4 if 13 <= 8 + i <= 15 else r for i, r in enumerate(TORSO)]   # the right arm lifted off
    rows = [list(r + "." * 8) for r in player_frame(torso, legs(0, 0))]
    a = angle * 2 * math.pi / 256
    sx, sy = AIM_SHOULDER
    hx, hy = sx, sy
    for k in range(1, AIM_REACH + 1):
        hx, hy = sx + round(k * math.cos(a)), sy + round(k * math.sin(a))
        rows[hy][hx] = '3' if k == AIM_REACH else '4'
    return ["".join(r) for r in rows], (hx, hy)


AIM_FRAMES = [aim_frame(a) for a in AIM_ANGLES]


# The Iron Bat's armour (layered-sprites.md §5): a cap and a band drawn over the bat's body,
# placed at the bat's point `body`; its frames go with the bat's (sync).
BAT_ARMOUR = [
    "................", "................", "................", "................",
    "......qqqq......", ".....pqqqqp.....", ".....p....p.....", "................",
    "................", ".....pppppp.....", "................", "................",
    "................", "................", "................", "................",
]


def sprite_file(path, image, frames, columns, w, h, anchor, hitbox, animations, boxes=(), extra=(), palettes=(), points=(), normal=None):
    """`boxes`: sprite-wide boxes (kind, name, x, y, w, h); `extra`: frames of other sizes
    after the regular ones, as (x, y, w, h, ax, ay); `palettes`: other palettes of the same
    pixels, as (name, [(from, to), ...]) colour swaps (#rrggbb); `normal`: its normals for the
    light (lighting.md), "auto" or a normal map."""
    head = '<sprite version="1" image="%s"%s>' % (image, ' normal="%s"' % normal if normal else "")
    out = ['<?xml version="1.0" encoding="UTF-8"?>', head]
    out.append(' <hitbox x="%d" y="%d" w="%d" h="%d"/>' % hitbox)
    for kind, name, bx, by, bw, bh in boxes:
        out.append(' <box kind="%s" name="%s" x="%d" y="%d" w="%d" h="%d"/>' % (kind, name, bx, by, bw, bh))
    for name, px, py, angle in points:
        out.append(' <point name="%s" x="%d" y="%d" angle="%d"/>' % (name, px, py, angle))
    for i in range(frames):
        out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d"/>'
                   % ((i % columns) * w, (i // columns) * h, w, h, anchor[0], anchor[1]))
    for f in extra:
        # (x, y, w, h, ax, ay) or with its own boxes: (..., [(kind, name, x, y, w, h), ...]) and
        # points (..., boxes, [(name, x, y, angle), ...])
        if (len(f) > 6 and f[6]) or (len(f) > 7 and f[7]):
            out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d">' % f[:6])
            for kind, name, bx, by, bw, bh in f[6]:
                out.append('  <box kind="%s" name="%s" x="%d" y="%d" w="%d" h="%d"/>' % (kind, name, bx, by, bw, bh))
            for name, px, py, angle in (f[7] if len(f) > 7 else []):
                out.append('  <point name="%s" x="%d" y="%d" angle="%d"/>' % (name, px, py, angle))
            out.append(' </frame>')
        else:
            out.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d"/>' % f[:6])
    for name, swaps in palettes:
        out.append(' <palette name="%s">' % name)
        for a, b in swaps:
            out.append('  <swap from="%s" to="%s"/>' % (a, b))
        out.append(' </palette>')
    for anim in animations:
        name, loop, steps = anim[:3]
        directions = anim[3] if len(anim) > 3 else None   # (count, from, to): a direction set
        if directions:
            out.append(' <animation name="%s" loop="0" directions="%d" from="%d" to="%d">' % ((name,) + directions))
        else:
            out.append(' <animation name="%s" loop="%d">' % (name, 1 if loop else 0))
        for frame, duration in steps:
            out.append('  <step frame="%d" duration="%d"/>' % (frame, duration))
        out.append(' </animation>')
    out.append('</sprite>')
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def tall_frame(top, torso, leg_rows, blade):
    """16x48: the player in the lower 32 rows (`top` False: the upper 32), a blade drawn by
    `blade` (x, y, colour) in frame coordinates."""
    body = player_frame(torso, leg_rows)
    empty = ["................"] * 16
    rows = [list(r) for r in (empty + body if not top else body + empty)]
    for x, y, c in blade:
        rows[y][x] = c
    return ["".join(r) for r in rows]


def blade_line(x, y0, y1, hilt):
    """A vertical blade in column x from y0 to y1 (inclusive), the hilt (grip and guard) at
    row `hilt`."""
    out = [(x, y, 'b') for y in range(min(y0, y1), max(y0, y1) + 1)]
    out += [(x - 1, hilt, 'c'), (x, hilt, 'c'), (x + 1, hilt, 'c')]
    return out


def lance_line(x, y0, y1, tip_at_top):
    """A lance in column x from y0 to y1: a wooden shaft with a steel tip of 4 at one end."""
    lo, hi = min(y0, y1), max(y0, y1)
    out = []
    for y in range(lo, hi + 1):
        tip = y < lo + 4 if tip_at_top else y > hi - 4
        out.append((x, y, 'b' if tip else 'c'))
    return out


# The lance: thrust straight up above the head (drawn back, then at full length) and, in the
# air, straight down below the feet.
THRUST_UP_FRAMES = [
    tall_frame(False, TORSO_ARMS_UP_R, legs(-1, 1), lance_line(12, 12, 30, True)),
    tall_frame(False, TORSO_ARMS_UP_R, legs(-2, 2), lance_line(12, 0, 28, True)),
]
THRUST_DOWN_FRAMES = [
    tall_frame(True, TORSO, legs(-1, 2, lift_l=3, lift_r=3), lance_line(12, 12, 32, False)),
    tall_frame(True, TORSO, legs(-1, 2, lift_l=3, lift_r=3), lance_line(12, 14, 47, False)),
]


def arc_frame(top, torso, leg_rows, pivot, angle, length):
    """32x48: the player in columns 8 to 23 (in the lower 32 rows, or the upper ones with
    `top`), a sword from `pivot` at `angle` degrees (0 ahead, 90 up) `length` long. Returns the
    rows and the blade's hit box relative to the anchor (column 16, the feet)."""
    import math
    body = player_frame(torso, leg_rows)
    empty = ["." * 32] * 16
    rows = [list("." * 8 + r + "." * 8) for r in body]
    rows = [list(r) for r in (empty + ["".join(r) for r in rows] if not top else ["".join(r) for r in rows] + empty)]
    px, py = pivot
    xs, ys = [], []
    for i in range(length + 1):
        x = int(round(px + i * math.cos(math.radians(angle))))
        y = int(round(py - i * math.sin(math.radians(angle))))
        if 0 <= x < 32 and 0 <= y < 48:
            rows[y][x] = 'c' if i < 2 else 'b'
            if i >= 2:
                xs.append(x)
                ys.append(y)
    anchor_y = 48 if not top else 32
    box = (min(xs) - 2 - 16, min(ys) - 2 - anchor_y, max(xs) - min(xs) + 5, max(ys) - min(ys) + 5)
    return ["".join(r) for r in rows], box


# The sword's arcs: upwards from ahead over the head to behind; in the air from ahead under the
# feet to behind. A wind-up frame, then three along the arc, each with its hit box "blade".
SLASH_UP_ARC = [arc_frame(False, TORSO_ARMS_UP_R, legs(-1, 1), (20, 23), a, 15) for a in (-20, 35, 90, 145)]
SLASH_DOWN_ARC = [arc_frame(True, TORSO, legs(-1, 2, lift_l=3, lift_r=3), (19, 22), a, 18) for a in (30, -35, -90, -145)]


def low_frame(reach):
    """32x32: a sweep at knee height, the arm down and the blade ahead `reach` long."""
    rows = [list(r + "." * 16) for r in player_frame(TORSO, legs(-3, 3), bob=1)]
    for y in range(16, 23):
        rows[y][13] = '3'
    rows[23][13] = 'c'
    rows[22][14] = 'c'
    rows[24][14] = 'c'
    for x in range(15, 15 + reach):
        rows[23][x] = 'b'
    return ["".join(r) for r in rows]


SLASH_LOW_FRAMES = [low_frame(6), low_frame(15)]

# The lance ahead: the shaft at chest height, the tip far out.
THRUST_AHEAD = [list(r + "." * 16) for r in player_frame(TORSO, legs(-3, 3))]
for _x in range(10, 32):
    THRUST_AHEAD[18][_x] = 'b' if _x >= 28 else 'c'
THRUST_AHEAD = ["".join(r) for r in THRUST_AHEAD]


def main():
    out = sys.argv[1]
    img = sheet(PLAYER_FRAMES, 8, 16, 32)
    # The swing's frames are 32 wide, after the climb frames in the second row.
    for i, frame in enumerate(SLASH_FRAMES):
        for y in range(32):
            for x in range(32):
                img.putpixel((32 + i * 32 + x, 32 + y), PALETTE[frame[y][x]])
    # Below, in a third row: the swing up and down (16 x 48) and the low sweep (32 x 32).
    tall = Image.new("RGBA", (img.width, 112), (0, 0, 0, 0))
    tall.paste(img, (0, 0))
    img = tall
    for i, frame in enumerate(THRUST_UP_FRAMES + THRUST_DOWN_FRAMES):
        for y in range(48):
            for x in range(16):
                img.putpixel((i * 16 + x, 64 + y), PALETTE[frame[y][x]])
    for i, frame in enumerate(SLASH_LOW_FRAMES):
        for y in range(32):
            for x in range(32):
                img.putpixel((64 + i * 32 + x, 64 + y), PALETTE[frame[y][x]])
    # A fourth row: the sword's arcs (32 x 48 each) and the lance's thrust ahead (32 x 32).
    taller = Image.new("RGBA", (256, 160), (0, 0, 0, 0))
    taller.paste(img, (0, 0))
    img = taller
    for i, (frame, _) in enumerate(SLASH_UP_ARC + SLASH_DOWN_ARC):
        for y in range(48):
            for x in range(32):
                img.putpixel((i * 32 + x, 112 + y), PALETTE[frame[y][x]])
    for y in range(32):
        for x in range(32):
            img.putpixel((128 + x, 64 + y), PALETTE[THRUST_AHEAD[y][x]])
    for i, frame in enumerate(PUSH_FRAMES):
        for y in range(32):
            for x in range(16):
                img.putpixel((160 + i * 16 + x, 64 + y), PALETTE[frame[y][x]])
    # Sitting, in the second row after the swing.
    for i, frame in enumerate(SIT_FRAMES):
        for y in range(32):
            for x in range(16):
                img.putpixel((128 + i * 16 + x, 32 + y), PALETTE[frame[y][x]])
    # A fifth row: aiming (24 x 32 each).
    tallest = Image.new("RGBA", (256, 192), (0, 0, 0, 0))
    tallest.paste(img, (0, 0))
    img = tallest
    for i, (frame, _) in enumerate(AIM_FRAMES):
        for y in range(32):
            for x in range(24):
                img.putpixel((i * 24 + x, 160 + y), PALETTE[frame[y][x]])
    sources = os.path.join(out, "..", "sources")
    os.makedirs(sources, exist_ok=True)
    img.save(os.path.join(sources, "player.png"))
    extra = [(32 + i * 32, 32, 32, 32, 8, 32) for i in range(len(SLASH_FRAMES))]
    extra += [(i * 16, 64, 16, 48, 8, 48) for i in range(2)]        # 13, 14: the lance up
    extra += [(32 + i * 16, 64, 16, 48, 8, 32) for i in range(2)]   # 15, 16: the lance down
    extra += [(64 + i * 32, 64, 32, 32, 8, 32) for i in range(2)]   # 17, 18: low
    extra += [(i * 32, 112, 32, 48, 16, 48, [] if i == 0 else [("hit", "blade") + SLASH_UP_ARC[i][1]]) for i in range(4)]   # 19-22: arc up
    extra += [(128 + i * 32, 112, 32, 48, 16, 32, [] if i == 0 else [("hit", "blade") + SLASH_DOWN_ARC[i][1]]) for i in range(4)]   # 23-26: arc down
    extra += [(128, 64, 32, 32, 8, 32)]                               # 27: the lance ahead
    extra += [(160 + i * 16, 64, 16, 32, 8, 32) for i in range(4)]    # 28-31: pushing
    extra += [(i * 24, 160, 24, 32, 8, 32, [], [("hand", hx - 8, hy - 32, AIM_ANGLES[i])])
              for i, (_, (hx, hy)) in enumerate(AIM_FRAMES)]                # 32-40: aiming
    extra += [(128 + i * 16, 32, 16, 32, 8, 32) for i in range(len(SIT_FRAMES))]   # 41, 42: sitting
    sprite_file(os.path.join(sources, "player.bmsprite"), "player.png", len(PLAYER_FRAMES), 8, 16, 32, (8, 32), (-5, -30, 10, 30), [
        ("idle", True, [(0, 40), (1, 40)]),
        ("run", True, [(2, 6), (3, 6), (4, 6), (5, 6)]),
        ("jump", False, [(6, 1)]),
        ("fall", False, [(7, 1)]),
        ("climb", True, [(8, 10), (9, 10)]),
        ("slash", False, [(10, 4), (11, 4), (12, 8)]),
        ("thrust_up", False, [(13, 4), (14, 10)]),
        ("thrust_down", False, [(15, 4), (16, 14)]),
        ("slash_low", False, [(17, 4), (18, 10)]),
        ("slash_up", False, [(19, 4), (20, 3), (21, 3), (22, 4)]),
        ("slash_down", False, [(23, 4), (24, 3), (25, 3), (26, 8)]),
        ("thrust", False, [(17, 4), (27, 10)]),
        ("push", True, [(28, 10), (29, 10), (30, 10), (31, 10)]),
        ("aim", False, [(32 + i, 1) for i in range(9)], (9, 192, 64)),
        ("sit", True, [(41, 60), (42, 60)]),
    ], extra=extra, palettes=[
        # Wearing the chain mail (layered-sprites.md §5): the coat in steel.
        ("steel", [("#b51021", "#8c94a5"), ("#820a18", "#5a6270")]),
    ], normal="auto")
    sheet(BAT_FRAMES, 2, 16, 16).save(os.path.join(out, "bat.png"))
    sprite_file(os.path.join(out, "bat.bmsprite"), "bat.png", len(BAT_FRAMES), 2, 16, 16, (8, 12), (-6, -8, 12, 8), [
        ("idle", True, [(0, 8), (1, 8)]),
    ], boxes=[("hit", "bite", -3, -4, 6, 4)], palettes=[
        # The red bat (the Fast Bat's): the same drawing, its colours swapped.
        ("red", [("#6b428c", "#b02838"), ("#422963", "#64141e"), ("#ffe763", "#fff8c8")]),
    ], points=[("body", 0, -5, 0)], normal="auto")
    sheet([BAT_ARMOUR, BAT_ARMOUR], 2, 16, 16).save(os.path.join(out, "bat_armour.png"))
    sprite_file(os.path.join(out, "bat_armour.bmsprite"), "bat_armour.png", 2, 2, 16, 16, (8, 8), (0, 0, 0, 0), [
        ("idle", True, [(0, 8), (1, 8)]),
    ])
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
    # Frame 1: the dark doorway, drawn behind doors in the back wall.
    sheet([door_panel(), doorway()], 2, 32, 48).save(os.path.join(out, "door_panel.png"))
    sprite_file(os.path.join(out, "door_panel.bmsprite"), "door_panel.png", 2, 2, 32, 48, (0, 48), (0, -48, 32, 48), [
        ("idle", True, [(0, 60)]),
    ])
    sheet(BAG_FRAMES, 2, 16, 16).save(os.path.join(out, "drop.png"))
    sprite_file(os.path.join(out, "drop.bmsprite"), "drop.png", len(BAG_FRAMES), 2, 16, 16, (8, 13), (-5, -9, 10, 9), [
        ("idle", True, [(0, 20), (1, 20)]),
    ])


if __name__ == "__main__":
    main()
