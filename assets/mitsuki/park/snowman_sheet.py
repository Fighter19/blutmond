# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Finishes the snowman's sheet (snowman.py, rendered by make_sprite):

- the Christmas ball in his hands: what is seen in it, drawn (glass is not something a renderer
  of 15 colours does): his body behind it, tinted (his poses rendered again without the ball,
  frames 20 on, then dropped), and past him the park itself (every other pixel left empty), the diorama inside (a floor of snow, a tree under snow, a
  small figure in a yellow hoodie: Hina, a few flakes), the glass's rim and its shine, its
  outline all round;

- the boss's frames, drawn larger by the engine (snowman.ms: set_scale), get their boxes made
  as large;
- the frost beam's lengths are made of one column repeated (its middle) and the same rounded
  end: each 32 pixels a piece like the others. (The beam is one tile row high: two rows
  would be covered with 64 x 32 pieces, half of them empty.)

  python3 assets/mitsuki/park/snowman_sheet.py assets/mitsuki/park/sources/snowman

Rewrites <base>.png and <base>.bmsprite (and <base>_n.png, the normals, the same way).
"""

import math
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import snowman as model  # noqa: E402  (its colours)

HIM = 7                              # his frames 0 to 6 (with the ball), again from 20 without
BARE = 20
BOSS = (3, 4, 5, 6)                  # drawn at SCALE (snowman.ms set_scale): their boxes as large
SCALE = 2.0   # (a whole number: each pixel 2 x 2, the outline as even as drawn)
BEAMS = (16, 17, 18, 19)             # the beam, 1 to 4 pieces of BEAM long
BEAM = 32
TIP = 6                              # the beam's rounded end (pixels)
SHEET_W = 256


def main(base):
    tree = ET.parse(base + '.bmsprite')
    root = tree.getroot()
    frames = root.findall('frame')
    images = {}
    for suffix in ('', '_n'):
        try:
            images[suffix] = Image.open(base + suffix + '.png').convert('RGBA')
        except FileNotFoundError:
            pass
    rect = [{k: int(f.get(k)) for k in ('x', 'y', 'w', 'h', 'ax', 'ay')} for f in frames]
    # Each frame's picture, with its anchor.
    pics = {s: [im.crop((r['x'], r['y'], r['x'] + r['w'], r['y'] + r['h'])) for r in rect] for s, im in images.items()}
    for i in range(HIM):
        diorama(pics, rect, i, BARE + i)
    # The gold (Hina's hoodie in the ball, the shadow's eyes) glows: black in the normals.
    if '_n' in pics:
        for i, pic in enumerate(pics['']):
            for y in range(pic.height):
                for x in range(pic.width):
                    if pic.getpixel((x, y)) == rgba(model.CAP[0]):
                        pics['_n'][i].putpixel((x, y), (0, 0, 0, 255))
    # The frames without the ball: done with.
    for i in sorted(range(BARE, BARE + HIM), reverse=True):
        root.remove(frames[i])
        del frames[i], rect[i]
        for sfx in pics:
            del pics[sfx][i]
    # The beam: the longest one's middle column and its end; every length from them.
    longest = BEAMS[-1]
    for s in pics:
        src = pics[s][longest]
        w, h = src.size
        mid = src.crop((w // 2, 0, w // 2 + 1, h))
        end = src.crop((w - TIP, 0, w, h))
        for n, i in enumerate(BEAMS, 1):
            length = BEAM * n - (BEAM - w % BEAM if w % BEAM else 0)
            pic = Image.new('RGBA', (length, h), (0, 0, 0, 0))
            for x in range(0, length - TIP):
                pic.paste(mid, (x, 0))
            pic.paste(end, (length - TIP, 0))
            pics[s][i] = pic
            rect[i].update(w=length, h=h)
    # The boss's boxes as large as he is drawn.
    for i in BOSS:
        for box in frames[i].findall('box'):
            for k in ('x', 'y', 'w', 'h'):
                box.set(k, str(int(round(int(box.get(k)) * SCALE))))
    # Packed again in rows.
    x = y = row = 0
    for i, r in enumerate(rect):
        w, h = pics[''][i].size
        if x + w > SHEET_W:
            x, y, row = 0, y + row, 0
        r.update(x=x, y=y)
        x += w
        row = max(row, h)
    height = y + row
    for s, im in images.items():
        sheet = Image.new('RGBA', (SHEET_W, height), (0, 0, 0, 0))
        for i, r in enumerate(rect):
            sheet.paste(pics[s][i], (r['x'], r['y']))
        sheet.save(base + s + '.png')
    for f, r in zip(frames, rect):
        for k, v in r.items():
            f.set(k, str(v))
    ET.indent(root, space=' ')
    with open(base + '.bmsprite', 'w', encoding='utf-8') as fh:
        fh.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        fh.write(ET.tostring(root, encoding='unicode'))
        fh.write('\n')


def rgba(c):
    return tuple(c) + (255,)


def diorama(pics, rect, i, bare):
    """Frame `i`'s Christmas ball (its disc: the glass's pixels, where they differ from frame
    `bare`, his pose without it): his body seen through it, darkened, dithered; the diorama; the
    rim and shine. What lies over the ball (his sticks, its silver cap) stays."""
    pic, behind = pics[''][i], pics[''][bare]
    r, rb = rect[i], rect[bare]
    glass = {rgba(c) for c in model.ICE + [model.SNOW[2]]}
    keep = {rgba(c) for c in model.STICK + model.SILVER}
    w, h = pic.size

    def under(x, y):
        bx, by = x - r['ax'] + rb['ax'], y - r['ay'] + rb['ay']
        if 0 <= bx < behind.width and 0 <= by < behind.height:
            return behind.getpixel((bx, by))
        return (0, 0, 0, 0)
    found = [(x, y) for y in range(h) for x in range(w)
             if pic.getpixel((x, y)) in glass and pic.getpixel((x, y)) != under(x, y)]
    if not found:
        return
    cx = sum(x for x, _ in found) / len(found)
    cy = sum(y for _, y in found) / len(found)
    rad = max(math.hypot(x - cx, y - cy) for x, y in found) + 0.5
    dark, glow = rgba(model.COAL[0]), rgba(model.INSIDE[0])
    def ground(u):
        # The top of the floor of snow: the lower third, a little higher at the glass.
        return cy + rad * (0.25 - 0.15 * u * u)
    paint = {}
    for y in range(h):
        for x in range(w):
            d = math.hypot(x - cx, y - cy)
            if pic.getpixel((x, y)) in keep:
                continue
            # Its outline all round (against his body too).
            if rad < d <= rad + 1 and pic.getpixel((x, y))[3] != 0:
                paint[(x, y)] = rgba(model.OUT)
                continue
            if d > rad:
                continue
            u, v = (x - cx) / rad, (y - cy) / rad
            # Through the glass: his body behind it tinted (its snow the glow, the rest dark); where
            # nothing of him is behind, the park itself, every other pixel (left empty).
            b = under(x, y)
            light = (b[0] * 5 + b[1] * 9 + b[2] * 2) / 16
            if b[3] == 0:
                c = (0, 0, 0, 0) if (x + y) % 2 == 0 else dark
            else:
                c = glow if light > 120 else dark
            # The floor of snow, curving up at the sides; its top lighter.
            top = ground(u)
            if y >= top:
                c = rgba(model.GLOW[0] if y < top + 2 else model.GLOW[1])
            # The rim: light where the light comes from (up, left), else the glass's blue.
            if d > rad - 1:
                c = rgba(model.GLOW[0] if u + v < -0.2 else model.ICE[0])
            paint[(x, y)] = c
    # A tree (left), Hina (right: a yellow hoodie, brown hair), flakes, the shine; standing on
    # the floor (fy: its top row there).
    tx, hx = int(round(cx - 0.4 * rad)), int(round(cx + 0.3 * rad))
    fy = int(math.ceil(ground((tx - cx) / rad)))
    # (The tree under snow: light on the dark.)
    marks = [((tx, fy - 1), model.GLOW[1]), ((tx - 1, fy - 1), model.GLOW[1]), ((tx + 1, fy - 1), model.GLOW[1]),
             ((tx, fy - 2), model.GLOW[1]), ((tx, fy - 3), model.GLOW[0]),
             ((hx, int(math.ceil(ground((hx - cx) / rad))) - 1), model.CAP[0]),
             ((hx, int(math.ceil(ground((hx - cx) / rad))) - 2), model.COAL[0]),
             ((int(round(cx + 0.05 * rad)), int(round(cy - 0.45 * rad))), model.GLOW[0]),
             ((int(round(cx + 0.5 * rad)), int(round(cy - 0.15 * rad))), model.GLOW[0]),
             ((int(round(cx - 0.5 * rad)), int(round(cy - 0.45 * rad))), model.GLOW[0]),
             ((int(round(cx - 0.35 * rad)), int(round(cy - 0.6 * rad))), model.GLOW[0])]
    for at, c in marks:
        if at in paint:
            paint[at] = rgba(c)
    for at, c in paint.items():
        pic.putpixel(at, c)
        if '_n' in pics:
            pics['_n'][i].putpixel(at, (0, 0, 0, c[3]))   # (glowing: not lit)


if __name__ == '__main__':
    main(sys.argv[1])
