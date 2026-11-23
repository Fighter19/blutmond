#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the knight (assets/test_room/knight.png) and writes knight.bmsprite.

usage: make_knight_sprite.py <assets/test_room>

A knight in plate with a red plume, a blue tabard and a kite shield with a gold crescent,
facing right; 32x32 frames, the anchor at its feet (12, 32), so the sword has room ahead.
Every frame is put together from the same parts (legs, armour, helm, shield, sword), so
the walk and the attack stay one figure. The frames carry their own boxes: the hurt box on
the body, the guard box `shield` while the shield is ahead (not while the sword strikes:
that is when it can be struck from the front), the hit box `sword` on the strike.
Everything here is drawn by this script.
"""
import os
import sys

from PIL import Image

W, H = 32, 32
ANCHOR = (12, 32)

PALETTE = {
    '.': (0, 0, 0, 0),
    'o': (20, 18, 28, 255),     # outline, visor
    'D': (66, 72, 92, 255),     # steel, shade
    'M': (122, 132, 156, 255),  # steel
    'L': (190, 200, 218, 255),  # steel, lit
    'W': (236, 240, 248, 255),  # glint, blade's edge
    'r': (196, 24, 40, 255),    # plume
    'R': (112, 14, 32, 255),    # plume, shade
    'b': (36, 38, 92, 255),     # tabard, shield field, shade
    'B': (62, 72, 150, 255),    # tabard, shield field
    'g': (180, 136, 36, 255),   # gold
    'G': (250, 220, 90, 255),   # gold, lit
    'k': (74, 44, 26, 255),     # leather: boots, belt, grip
    'h': (140, 92, 40, 255),    # leather, lit
}


class Canvas:
    def __init__(self):
        self.px = [['.'] * W for _ in range(H)]

    def put(self, x, y, c):
        if 0 <= x < W and 0 <= y < H and c != '.':
            self.px[y][x] = c

    def hline(self, x0, x1, y, c):
        for x in range(x0, x1 + 1):
            self.put(x, y, c)

    def rows(self, x, y, art):
        """Puts rows of palette letters with the top left at (x, y)."""
        for j, row in enumerate(art):
            for i, c in enumerate(row):
                self.put(x + i, y + j, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy


# --- Parts (x, y: where the part's top left goes; dx, dy shift the upper body) --------------

LEG = [
    "oMLo",
    "oDMo",
    "oDMo",
    "oMLo",   # knee
    "oDLo",
    "oDMo",
    "okko",
    "okhko",
    "ooooo",
]
LEG_BACK = [r.replace('L', 'M').replace('M', 'D', 1) for r in LEG]


def legs(c, rear, front):
    """rear, front: (x shift, lift) of each leg; the leg tops at y 23."""
    for (shift, lift), art, x in ((rear, LEG_BACK, 8), (front, LEG, 12)):
        top = 23 - lift
        c.rows(x + shift, top, art[:len(art) - lift] if lift else art)
        if lift:   # the lifted boot
            c.rows(x + shift, 31 - lift - 2, ["okhko", "ooooo"])


def torso(c, dx, dy):
    x, y = 6 + dx, 12 + dy
    c.rows(x, y, [
        "..oooooooo",
        ".oDMMMLLMo.",
        "oDDMbBBLLMo",
        "oDMMbBBMLMo",
        "oDMbBGBBLLo",
        "oDMbGBBBLMo",
        "oDMbBGBBLMo",
        "oDMMbBBMLMo",
        "okkkkgGkkko",
        "oDMMbBBMLMo",
        "oDMbbBBBMMo",
        "ooDbbBBBMoo",
    ])


def pauldron(c, dx, dy):
    """The rear shoulder, over the arm that holds the sword."""
    c.rows(5 + dx, 12 + dy, [
        ".ooo.",
        "oMLMo",
        "oDMDo",
        ".ooo.",
    ])


def helm(c, dx, dy):
    x, y = 7 + dx, 2 + dy
    # The plume first, streaming back over the crest.
    c.rows(x - 4, y - 1, [
        "....oooo",
        "..oorrrro",
        ".orrrRRro",
        "orrRRo...",
        "oRRo.....",
        "oRo......",
        ".o.......",
    ])
    c.rows(x, y + 1, [
        "..ooooo.",
        ".oMLLWLo",
        "oDMMLLLLo",
        "oDMMLLMoo",
        "oDMMMoooo",
        "oDMMLLMMo",
        "oDMMLoLoo",
        "oDDMMMMMo",
        ".ooDDMMo.",
        "...oooo..",
    ])


SHIELD = [
    "oooooooo",
    "ogGGGGgo",
    "ogBBBBgo",
    "ogBGGBgo",
    "ogGGbBgo",
    "ogGbBBgo",
    "ogGbBBgo",
    "ogGGbBgo",
    "ogBGGBgo",
    "ogBBBBgo",
    "ogbBBBgo",
    "ogbBBBgo",
    "ogbBBBgo",
    ".ogbBgo.",
    ".ogbBgo.",
    "..ogbgo.",
    "..oggo..",
    "...oo...",
]


def shield_front(c, dx, dy):
    """Ahead of the body: x 16 to 23, y 9 to 26 (with dx, dy 0)."""
    c.rows(16 + dx, 9 + dy, SHIELD)


def shield_back(c, dx, dy):
    """Swung behind while the sword strikes: seen edge on, over the back."""
    c.rows(3 + dx, 12 + dy, [
        "ooo.",
        "ogGo",
        "ogbo",
        "ogbo",
        "ogbo",
        "ogbo",
        "ogbo",
        "ogbo",
        "ogbo",
        "ogbo",
        "oggo",
        ".oo.",
    ])


def gauntlet(c, x, y):
    c.rows(x, y, ["oMo", "oLo", "ooo"])


def sword_idle(c, dx, dy):
    """Held upright in the rear hand, along the back."""
    x = 4 + dx
    c.put(x + 1, 3 + dy, 'W')
    for y in range(4, 16):
        c.put(x, y + dy, 'L')
        c.put(x + 1, y + dy, 'W')
        c.put(x - 1, y + dy, 'o')
        c.put(x + 2, y + dy, 'o')
    c.hline(x - 2, x + 3, 16 + dy, 'g')
    c.rows(x - 2, 15 + dy, ["oooooo"])
    c.rows(x - 2, 16 + dy, ["ogGGgo"])
    c.rows(x - 2, 17 + dy, ["oooooo"])
    gauntlet(c, x - 1, 18 + dy)
    c.rows(x - 1, 21 + dy, [".k.", "oGo", ".o."])


def diagonal(c, x, y, n, sx, sy):
    """A blade of n rows along a 45 degree diagonal from (x, y), stepping (sx, sy) a row:
    its edge lit, outlined on both sides."""
    for i in range(n):
        px, py = x + i * sx, y + i * sy
        c.put(px - sx * 2, py, 'o')
        c.put(px - sx, py, 'L')
        c.put(px, py, 'W')
        c.put(px + sx, py, 'o')
    end = (x + n * sx, y + n * sy)
    c.put(end[0], end[1], 'o')
    c.put(end[0] - sx, end[1], 'o')


def sword_raised(c, dx, dy):
    """The wind-up: up and back over the shoulder."""
    hx, hy = 9 + dx, 9 + dy
    diagonal(c, hx - 2, hy - 2, 7, -1, -1)
    c.rows(hx - 3, hy - 3, ["o.o", ".G.", "o.g"])   # crossguard, across the blade
    gauntlet(c, hx - 1, hy - 1)


def sword_thrust(c, dx, dy):
    """The strike: straight ahead at chest height, the arm out."""
    y = 15 + dy
    c.rows(15 + dx, y - 1, ["oooo", "MLLo", "DMMo", "oooo"])   # the arm
    c.rows(19 + dx, y - 2, ["o", "g", "G", "g", "g", "o"])     # crossguard
    for x in range(20 + dx, 31):
        c.put(x, y, 'W')
        c.put(x, y + 1, 'L')
        c.put(x, y - 1, 'o')
        c.put(x, y + 2, 'o')
    c.put(31, y, 'o')
    c.put(31, y + 1, 'o')
    c.put(30, y + 1, 'o')


def sword_low(c, dx, dy):
    """Recovering: the blade down and ahead."""
    hx, hy = 17 + dx, 19 + dy
    diagonal(c, hx + 2, hy + 2, 8, 1, 1)
    c.rows(hx, hy, ["o.o", ".G.", "o.o"])
    gauntlet(c, hx - 2, hy - 1)


# --- Frames ---------------------------------------------------------------------------------

def figure(rear=(0, 0), front=(0, 0), dx=0, dy=0, shield='front', sword='idle', parts='all'):
    """parts: 'all' (the picture), 'body' (what can be struck: all but the shield ahead and a
    sword held out), 'shield' (the shield ahead alone)."""
    c = Canvas()
    if parts == 'shield':
        if shield == 'front':
            shield_front(c, dx, dy)
        return c.px
    if shield == 'back':
        shield_back(c, dx, dy)
    if sword == 'idle':
        sword_idle(c, dx, dy)
    legs(c, rear, front)
    torso(c, dx, dy)
    helm(c, dx, dy)
    if sword == 'raised' and parts == 'all':
        sword_raised(c, dx, dy)
    elif sword != 'raised':
        pauldron(c, dx, dy)
    if parts == 'all':
        if sword == 'thrust':
            sword_thrust(c, dx, dy)
        elif sword == 'low':
            sword_low(c, dx, dy)
        if shield == 'front':
            shield_front(c, dx, dy)
    return c.px


def bounds(px):
    """The drawn pixels' bounding box relative to the anchor: (x, y, w, h), or None."""
    xs = [x for row in px for x, ch in enumerate(row) if ch != '.']
    ys = [y for y, row in enumerate(px) for ch in row if ch != '.']
    if not xs:
        return None
    x0, y0 = min(xs), min(ys)
    return (x0 - ANCHOR[0], y0 - ANCHOR[1], max(xs) - x0 + 1, max(ys) - y0 + 1)


def frame(hit=None, **pose):
    """A frame's picture and boxes, worked out from the drawing: the hurt box over the body
    (up to the shield, so that it lies behind it), the guard box over the shield ahead, and
    the hit box `sword` if given."""
    hx, hy, hw, hh = bounds(figure(parts='body', **pose))
    boxes = []
    shield = bounds(figure(parts='shield', **pose))
    if shield is not None:
        hw = min(hw, shield[0] - hx)   # the body ends where the shield begins
        boxes.append(('guard', 'shield') + shield)
    boxes.insert(0, ('hurt', '', hx, hy, hw, hh))
    if hit is not None:
        boxes.append(('hit', 'sword') + hit)
    return (figure(**pose), boxes)


# (pixels, boxes)
FRAMES = [
    frame(),                                                       # 0 idle
    frame(dy=1),                                                   # 1 idle, breathing
    frame(rear=(-2, 0), front=(2, 0)),                             # 2 walk
    frame(rear=(0, 2), dy=-1),                                     # 3
    frame(rear=(2, 0), front=(-2, 0)),                             # 4
    frame(front=(0, 2), dy=-1),                                    # 5
    frame(rear=(-1, 0), dx=-1, sword='raised'),                    # 6 wind-up
    frame(rear=(-2, 0), front=(2, 0), dx=2, shield='back', sword='thrust', hit=(7, -19, 13, 6)),   # 7 the strike
    frame(rear=(-1, 0), front=(1, 0), dx=1, shield='back', sword='low'),   # 8 recovering
    frame(dx=-1, dy=1),                                            # 9 flinch
]

ANIMATIONS = [
    ("idle", True, [(0, 30), (1, 30)]),
    ("walk", True, [(2, 8), (3, 8), (4, 8), (5, 8)]),
    ("attack", False, [(6, 18), (7, 8), (8, 16)]),
    ("flinch", False, [(9, 12)]),
]

COLUMNS = 4


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/test_room"
    rows = (len(FRAMES) + COLUMNS - 1) // COLUMNS
    img = Image.new("RGBA", (COLUMNS * W, rows * H), (0, 0, 0, 0))
    for i, (px, _) in enumerate(FRAMES):
        for y in range(H):
            for x in range(W):
                img.putpixel(((i % COLUMNS) * W + x, (i // COLUMNS) * H + y), PALETTE[px[y][x]])
    img.save(os.path.join(out, "knight.png"))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<sprite version="1" image="knight.png">',
             ' <hitbox x="-5" y="-28" w="10" h="28"/>']
    for i, (_, boxes) in enumerate(FRAMES):
        lines.append(' <frame x="%d" y="%d" w="%d" h="%d" ax="%d" ay="%d">'
                     % ((i % COLUMNS) * W, (i // COLUMNS) * H, W, H, ANCHOR[0], ANCHOR[1]))
        for kind, name, bx, by, bw, bh in boxes:
            named = ' name="%s"' % name if name else ''
            lines.append('  <box kind="%s"%s x="%d" y="%d" w="%d" h="%d"/>' % (kind, named, bx, by, bw, bh))
        lines.append(' </frame>')
    for name, loop, steps in ANIMATIONS:
        lines.append(' <animation name="%s" loop="%d">' % (name, 1 if loop else 0))
        for frame, duration in steps:
            lines.append('  <step frame="%d" duration="%d"/>' % (frame, duration))
        lines.append(' </animation>')
    lines.append('</sprite>')
    with open(os.path.join(out, "knight.bmsprite"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
