# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Makes the snowman's sheet (snowman.py, rendered by make_sprite) cost less sprite memory:

- the boss's frames, drawn larger by the engine (snowman.ms: set_scale), get their boxes made
  as large;
- the frost beam's lengths are made of one column repeated (its middle) and the same rounded
  end: each 32 pixels a piece like the others. (The beam is one tile row high: two rows
  would be covered with 64 x 32 pieces, half of them empty.)

  python3 assets/mitsuki/park/snowman_sheet.py assets/mitsuki/park/sources/snowman

Rewrites <base>.png and <base>.bmsprite (and <base>_n.png, the normals, the same way).
"""

import sys
import xml.etree.ElementTree as ET

from PIL import Image

BOSS = (3, 4, 5, 6)                  # drawn at SCALE (snowman.ms set_scale): their boxes as large
SCALE = 1.7
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


if __name__ == '__main__':
    main(sys.argv[1])
