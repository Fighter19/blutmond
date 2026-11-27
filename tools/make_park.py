#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the park west of the rural street and writes its room: drawn as the street is
(tools/make_street.py: its colours, its layers with a height map for the normals, its light),
a park at night: trees, hedges, an iron railing, a swing set, two gas lamps, and far to the
west, under the lamp at the end of the path, the bench where Hina hides (Mitsuki's park
scene: assets/mitsuki/park). The bench is not seen from where the street leads in.

usage: make_park.py <assets/mitsuki/park>

Writes park_tiles.png, park_tiles_n.png (the normal map; black where a pixel gives light) and
park.tmx. The lamps' glass glows with their flicker (`glow_light`).
"""
import math
import os
import random
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_street as st  # noqa: E402  (Layer, cut, the colours)
import make_test_room as rooms  # noqa: E402  (csv, prop)

T = 16
W, H = 640, 160
st.W, st.H = W, H              # make_street's Layer and cut draw at this size
GROUND = st.GROUND
BENCH = (110, 164)             # the bench Hina hides under: from, to
LAMPS = (188, 472)             # the lamp posts' middles
LAMP_Y = 59                    # their flames
SWINGS = 404                   # the swing set's middle
TREES = ((40, 30, 58), (262, 36, 56), (346, 30, 62), (560, 34, 58))   # x, radius, crown's middle row

SKY, STAR, MOON, MOON_SHADE, HILL = st.SKY, st.STAR, st.MOON, st.MOON_SHADE, st.HILL
WOOD, IRON, GLASS, LEAF, GRASS, EARTH, STONE, GAP = st.WOOD, st.IRON, st.GLASS, st.LEAF, st.GRASS, st.EARTH, st.STONE, st.GAP
SHADOW = (14, 16, 24)          # under the bench: where something hides


def lamp(L, x):
    """A gas lamp as the street's: an iron pole, a foot, a cap and a glass box with the flame."""
    for y in range(64, GROUND):
        for px in range(x - 2, x + 2):
            u = (px - x + 0.5) / 2.0
            L.put(px, y, IRON[0] if px < x else IRON[1], 1.8 * math.sqrt(max(0.0, 1 - u * u)))
    for y in range(118, GROUND):
        half = 3 + (y - 118) // 3
        for px in range(x - half, x + half):
            L.put(px, y, IRON[0] if px < x else IRON[1], 1.6)
    L.rect(x - 8, 46, x + 8, 50, IRON[1], 1.4)
    L.rect(x - 6, 44, x + 6, 46, IRON[0], 1.6)
    L.put(x, 43, IRON[0], 1.8)
    for y in range(50, 66):
        for px in range(x - 6, x + 6):
            if px in (x - 6, x + 5) or y == 65:
                L.put(px, y, IRON[1], 1.4)
            else:
                core = abs(px - x + 0.5) < 2.5 and 54 <= y <= 62
                L.put(px, y, GLASS[1] if core else GLASS[0], emissive=True)
    L.rect(x - 4, 66, x + 4, 68, IRON[1], 1.4)


def tree(L, tx, r, cy):
    L.rect(tx - 3, cy + r // 2, tx + 3, GROUND, WOOD[2], 1.0)
    for y in range(cy - r, cy + r):
        for x in range(tx - r - 2, tx + r + 3):
            d = ((x - tx) / float(r)) ** 2 + ((y - cy) / float(r * 0.94)) ** 2
            wob = 0.12 * math.sin(x * 0.9) * math.cos(y * 0.7)
            if d < 1.0 + wob:
                bump = 1.0 + 0.8 * math.sin(x * 0.35 + y * 0.2) * math.sin(y * 0.45 - x * 0.1)
                L.put(x, y, LEAF[0] if bump > 1.0 else LEAF[1], bump * (1.0 - d) * 2.0)


def background():
    L = st.Layer()
    rnd = random.Random(19)
    # The sky in bands, stars, the moon (as the street's), the far hills.
    for y in range(GROUND):
        t = min(1.0, y / 100.0) * (len(SKY) - 1)
        for x in range(W):
            k = int(t)
            bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y % 4][x % 4]
            c = SKY[min(k + (1 if (t - k) * 16 > bayer + 0.5 else 0), len(SKY) - 1)]
            L.put(x, y, c, emissive=True)
    for _ in range(40):
        L.put(rnd.randrange(W), rnd.randrange(4, 70), STAR, emissive=True)
    mx, my, mr = 520, 26, 9
    for y in range(my - mr, my + mr + 1):
        for x in range(mx - mr, mx + mr + 1):
            if (x - mx) ** 2 + (y - my) ** 2 <= mr * mr:
                shade = (x - mx + 4) ** 2 + (y - my - 2) ** 2 <= (mr - 1) ** 2
                L.put(x, y, MOON_SHADE if shade else MOON, emissive=True)
    for x in range(W):
        r1 = int(90 + 8 * math.sin(x / 53.0 + 0.4) + 4 * math.sin(x / 11.0))
        r2 = int(102 + 6 * math.sin(x / 29.0 + 1.0) + 3 * math.sin(x / 8.0))
        for y in range(r1, GROUND):
            L.put(x, y, HILL[0], emissive=True)
        for y in range(r2, GROUND):
            L.put(x, y, HILL[1], emissive=True)
    # Trees, then hedges along the back, an iron railing in front of them.
    for tx, r, cy in TREES:
        tree(L, tx, r, cy)
    for x in range(W):
        # (Its shapes and the grass's repeat every 32 pixels: few different tiles.)
        top = int(104 + 3 * math.sin(x * math.pi / 16.0) + 2 * math.sin(x * math.pi / 4.0))
        for y in range(top, 120):
            bump = 1.0 + 0.6 * math.sin(x * 0.7 + y * 0.9)
            L.put(x, y, LEAF[1] if (x * 5 + y * 3) % 7 else LEAF[0], bump * 0.8)
    for x in range(W):
        if x % 6 in (0, 1):
            for y in range(98, 120):
                L.put(x, y, IRON[1] if x % 6 else IRON[0], 1.2)
            L.put(x, 97, IRON[0], 1.6)
    for ry in (100, 112):
        for x in range(W):
            L.put(x, ry, IRON[0], 1.4)
            L.put(x, ry + 1, IRON[1], 1.0)
    # The grass verge, from a tile's edge (its colours do not share tiles with the hedge's).
    for y in range(120, GROUND):
        for x in range(W):
            L.put(x, y, GRASS[(x * 3 + y) % 5 == 0], 0.5 + 0.5 * ((x * 7 + y * 13) % 16) / 15.0)
    # The swing set: two A frames, a beam, two swings on chains.
    for fx in (SWINGS - 26, SWINGS + 26):
        for y in range(70, GROUND):
            spread = (y - 70) // 4
            for px in (fx - spread, fx + spread):
                L.put(px, y, IRON[0], 1.5)
                L.put(px + 1, y, IRON[1], 1.2)
    for x in range(SWINGS - 27, SWINGS + 28):
        L.put(x, 69, IRON[0], 1.6)
        L.put(x, 70, IRON[1], 1.4)
    for sx in (SWINGS - 12, SWINGS + 12):
        for y in range(71, 110):
            if y % 3:
                L.put(sx - 5, y, IRON[0], 1.0)
                L.put(sx + 5, y, IRON[0], 1.0)
        L.rect(sx - 6, 110, sx + 7, 112, WOOD[0], 1.4)
        L.rect(sx - 6, 112, sx + 7, 113, WOOD[2], 1.0)
    # The bench (as the street's), and the dark under its seat where something can hide.
    b0, b1 = BENCH
    L.rect(b0 + 4, 120, b1 - 4, GROUND, SHADOW, 0.0)
    for sy in (98, 103):
        for x in range(b0 + 2, b1 - 2):
            for y in range(sy, sy + 4):
                L.put(x, y, WOOD[0] if y - sy < 2 else WOOD[1], 1.6 - abs(y - sy - 1.5) * 0.6)
    for sy in (113, 117):
        for x in range(b0, b1):
            for y in range(sy, sy + 3):
                L.put(x, y, WOOD[0] if y == sy else WOOD[1], 1.8 - abs(y - sy - 1) * 0.7)
    for lx in (b0 + 3, b1 - 6):
        L.rect(lx, 96, lx + 3, GROUND, IRON[1], 1.2)
        for y in range(96, GROUND):
            L.put(lx, y, IRON[0], 1.4)
    for x in list(range(b0, b0 + 8)) + list(range(b1 - 8, b1)):
        L.put(x, 110, IRON[0], 1.4)
    for x in LAMPS:
        lamp(L, x)
    return L


def path():
    """The path: packed earth with a gravel edge, the gravel in small stones."""
    L = st.Layer()
    rnd = random.Random(23)
    for y in range(GROUND, H):
        for x in range(W):
            L.put(x, y, GAP, 0.0)
    for y in range(GROUND, GROUND + 4):
        for x in range(W):
            L.put(x, y, EARTH[0], 0.8 - 0.2 * (y - GROUND))
    for y in range(GROUND + 4, H, 3):
        x = -(y % 2) * 2
        while x < W:
            w = 3 + rnd.randrange(3)
            c = STONE[rnd.randrange(2)]
            for yy in range(y, min(H, y + 2)):
                for xx in range(max(0, x), min(W, x + w - 1)):
                    L.put(xx, yy, c, 1.2 - 0.4 * (yy - y))
            x += w + rnd.randrange(2)
    return L


def write(out, tiles, maps):
    cols = 16
    rows = (len(tiles) + cols - 1) // cols
    img = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg[...] = (128, 128, 255, 0)
    for i, (pic, nm) in enumerate(tiles):
        y, x = (i // cols) * T, (i % cols) * T
        img[y:y + T, x:x + T] = pic
        nimg[y:y + T, x:x + T] = nm
    Image.fromarray(img, "RGBA").save(os.path.join(out, "park_tiles.png"))
    Image.fromarray(nimg, "RGBA").save(os.path.join(out, "park_tiles_n.png"))
    bg, main = maps
    tw, th = W // T, H // T
    first_col = 1 + len(tiles)
    m = ET.Element("map", {
        "version": "1.8", "tiledversion": "1.8.0", "orientation": "orthogonal", "renderorder": "right-down",
        "width": str(tw), "height": str(th), "tilewidth": str(T), "tileheight": str(T), "infinite": "0",
        "nextlayerid": "5", "nextobjectid": "1",
    })
    mp = ET.SubElement(m, "properties")
    hexes = lambda cs: ", ".join("#%02x%02x%02x" % c for c in cs)   # noqa: E731
    for k, v in (("music", "park_dread"), ("attach", "ParkScene"), ("room_id", "park"), ("title", "The park"),
                 ("lighting", "palette"), ("ambient", "0.25"), ("light_levels", "4"), ("light_curve", "perceived"),
                 ("light_dither", "false"), ("backdrop", "#101a2a"), ("glow_light", hexes(GLASS))):
        rooms.prop(mp, k, v)
    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": "park_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(len(tiles)), "columns": str(cols)})
    rooms.prop(ET.SubElement(ts, "properties"), "normal", "park_tiles_n.png")
    ET.SubElement(ts, "image", {"source": "park_tiles.png", "width": str(cols * T), "height": str(rows * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": "../../engine/blutmond_collision.tsx"})
    solid = [[first_col + 1 if y * T >= GROUND else 0 for x in range(tw)] for y in range(th)]
    for lid, name, data, extra in ((1, "background", bg, None), (2, "main", main, None), (3, "collision", solid, {"opacity": "0.5"})):
        a = {"id": str(lid), "name": name, "width": str(tw), "height": str(th)}
        if extra:
            a.update(extra)
        layer = ET.SubElement(m, "layer", a)
        rooms.prop(ET.SubElement(layer, "properties"), "role", name)
        ET.SubElement(layer, "data", {"encoding": "csv"}).text = rooms.csv(data)
    og = ET.SubElement(m, "objectgroup", {"id": "4", "name": "objects"})
    rooms.prop(ET.SubElement(og, "properties"), "role", "objects")
    b0, b1 = BENCH
    lamp_props = [("colour", "#ffd890"), ("radius", "150"), ("height", "24"), ("intensity", "1.5"), ("flicker", "0.2"),
                  ("flicker_speed", "4")]
    objects = [
        ("CameraZone", "room", 0, 0, W, H, False, []),
        ("PlayerStart", "start", W - 40, GROUND, 0, 0, True, [("facing", "left")]),
        ("Light", "lamp", LAMPS[0], LAMP_Y, 0, 0, True, lamp_props),
        ("Light", "lamp_swings", LAMPS[1], LAMP_Y, 0, 0, True, lamp_props),
        # Hina under the bench (her eyes), the nightmare, the close-up of their hands
        # (assets/mitsuki/park: their scripts).
        ("Hina", "hina", (b0 + b1) // 2, GROUND, 0, 0, True, []),
        ("Nightmare", "nightmare", b1 + 30, GROUND, 0, 0, True, []),
        ("Hands", "hands", 126, 56, 0, 0, True, []),
        ("Door", "to_street", W - 16, GROUND - 48, 16, 48, False,
         [("target_room", "rural_street"), ("target_door", "to_park"), ("direction", "east")]),
    ]
    oid = 1
    for otype, name, x, y, ow, oh, point, props in objects:
        a = {"id": str(oid), "name": name, "type": otype, "x": str(x), "y": str(y)}
        if ow or oh:
            a["width"], a["height"] = str(ow), str(oh)
        o = ET.SubElement(og, "object", a)
        if point:
            ET.SubElement(o, "point")
        if props:
            pp = ET.SubElement(o, "properties")
            for k, v in props:
                rooms.prop(pp, k, v)
        oid += 1
    m.set("nextobjectid", str(oid))
    ET.indent(m, " ")
    ET.ElementTree(m).write(os.path.join(out, "park.tmx"), encoding="UTF-8", xml_declaration=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "mitsuki", "park")
    os.makedirs(out, exist_ok=True)
    tiles, maps = st.cut([background(), path()])
    write(out, tiles, maps)
    print("%d tiles of 16 x 16" % len(tiles))


if __name__ == "__main__":
    main()
