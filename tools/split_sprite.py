#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Splits a sprite drawn whole into two layers of a figure (layered-sprites.md §8): legs below,
torso above.

usage: split_sprite.py <whole.bmsprite> <out dir> <lower name> <upper name>
                       (--waist N | --mask mask.png) [--point waist]

Each frame is cut at a line N pixels above its anchor (--waist), or by a mask image of the
sheet's size (--mask: its opaque pixels go to the upper layer). The lower layer keeps the
frames' rectangles, anchors, hitboxes and boxes, and gets a point (`waist` unless --point says
otherwise) on every frame N pixels above the anchor; the upper layer has the same rectangles
with the anchor moved to the cut, so drawn at that point it lands where it was. Both keep the
animations. With --mask the point is at the anchor's height minus N as well (--waist is still
needed for where the point goes). Writes <name>.png and <name>.bmsprite for both.
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image


def main(argv):
    args = argv[1:]
    opts = {}
    rest = []
    i = 0
    while i < len(args):
        if args[i] in ("--waist", "--mask", "--point"):
            opts[args[i]] = args[i + 1]
            i += 2
        else:
            rest.append(args[i])
            i += 1
    if len(rest) != 4 or "--waist" not in opts:
        print(__doc__, file=sys.stderr)
        return 2
    src, out, lower, upper = rest
    waist = int(opts["--waist"])
    point = opts.get("--point", "waist")
    tree = ET.parse(src)
    root = tree.getroot()
    sheet = Image.open(os.path.join(os.path.dirname(src), root.get("image"))).convert("RGBA")
    mask = Image.open(opts["--mask"]).convert("RGBA") if "--mask" in opts else None
    low = Image.new("RGBA", sheet.size, (0, 0, 0, 0))
    high = Image.new("RGBA", sheet.size, (0, 0, 0, 0))

    frames = root.findall("frame")
    for f in frames:
        x, y, w, h = (int(f.get(k)) for k in ("x", "y", "w", "h"))
        ay = int(f.get("ay", h))
        cut = ay - waist   # rows above it (in the frame) go up
        for py in range(h):
            for px in range(w):
                c = sheet.getpixel((x + px, y + py))
                if c[3] < 128 or c[:3] == (255, 0, 255):
                    continue
                up = mask.getpixel((x + px, y + py))[3] >= 128 if mask else py < cut
                (high if up else low).putpixel((x + px, y + py), c)

    os.makedirs(out, exist_ok=True)
    low.save(os.path.join(out, lower + ".png"))
    high.save(os.path.join(out, upper + ".png"))

    def colours(img):
        return {"#%02x%02x%02x" % c[:3] for _, c in img.getcolors(1 << 16) if c[3] >= 128}

    def write(name, upper_layer):
        r = ET.Element("sprite", {"version": "1", "image": name + ".png"})
        present = colours(high if upper_layer else low)
        for child in root:
            if child.tag == "frame":
                f = ET.SubElement(r, "frame", dict(child.attrib))
                if upper_layer:
                    ay = int(child.get("ay", child.get("h")))
                    f.set("ay", str(ay - waist))
                for c in child:
                    # Points above the cut go up with the torso (from its anchor, at the cut);
                    # boxes and the rest stay below.
                    above = c.tag == "point" and int(c.get("y", "0")) < -waist
                    if above and upper_layer:
                        p = ET.SubElement(f, "point", dict(c.attrib))
                        p.set("y", str(int(c.get("y", "0")) + waist))
                    elif not above and not upper_layer:
                        f.append(c)
                if not upper_layer:
                    ET.SubElement(f, "point", {"name": point, "x": "0", "y": str(-waist)})
            elif child.tag == "palette" and child.get("image") is None:
                # Colour swaps: only those of colours this layer has (a palette left with none
                # is left out: the layer keeps its own colours in it).
                p = ET.Element("palette", {"name": child.get("name")})
                for sw in child:
                    if sw.get("from", "").lower() in present:
                        p.append(sw)
                if len(p):
                    r.append(p)
            elif child.tag in ("hitbox", "box"):
                if upper_layer:
                    if child.tag == "hitbox":
                        ET.SubElement(r, "hitbox", {"x": "0", "y": "0", "w": "0", "h": "0"})
                else:
                    r.append(child)
            else:
                r.append(child)
        ET.indent(r, space=" ")
        path = os.path.join(out, name + ".bmsprite")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            fh.write(ET.tostring(r, encoding="unicode"))
            fh.write("\n")

    write(lower, False)
    write(upper, True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
