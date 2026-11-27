#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the outskirts west of the park, where Mitsuki searches for Hina after the nightmare took
her (Mitsuki's story, assets/mitsuki/outskirts), and writes their rooms: drawn as the street and
the park are (tools/make_street.py, tools/make_park.py: their colours, layers with a height map
for the normals, their light), at night.

- riverside.tmx: a path along a river: a stone embankment and an iron railing, the water dark
  with the moon's glints in it, the far bank's trees; a willow, two lamps. Bats. East to the
  park, west to the shrine path.
- shrine_path.tmx: a path up to a small shrine in the woods: dark trees close around, two stone
  lanterns glowing, a wooden gate at the west end with a rope across it: the way is closed (a
  wall), the search ends here. A knight. East back to the riverside.

Their story is the attachment Search (assets/mitsuki/outskirts/search.ms; the room property
Search.Room says which).

usage: make_outskirts.py <assets/mitsuki/outskirts>

Writes <room>_tiles.png, <room>_tiles_n.png (the normal map; black where a pixel gives light) and
<room>.tmx for each.
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
import make_park as pk  # noqa: E402  (lamp, tree, path)
import make_test_room as rooms  # noqa: E402  (csv, prop)

T = 16
H = 160
GROUND = st.GROUND
SKY, STAR, MOON, MOON_SHADE, HILL = st.SKY, st.STAR, st.MOON, st.MOON_SHADE, st.HILL
WOOD, IRON, GLASS, LEAF, STONE, GAP = st.WOOD, st.IRON, st.GLASS, st.LEAF, st.STONE, st.GAP
WATER = [(12, 20, 36), (18, 30, 50)]
GLINT = (150, 160, 170)
ROPE = (176, 150, 96)
PAPER = (220, 214, 196)
DARK_LEAF = [(22, 38, 28), (30, 48, 34)]


def sky(L, rnd, moon=True):
    W = st.W
    for y in range(GROUND):
        t = min(1.0, y / 100.0) * (len(SKY) - 1)
        for x in range(W):
            k = int(t)
            bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y % 4][x % 4]
            L.put(x, y, SKY[min(k + (1 if (t - k) * 16 > bayer + 0.5 else 0), len(SKY) - 1)], emissive=True)
    for _ in range(W // 16):
        L.put(rnd.randrange(W), rnd.randrange(4, 60), STAR, emissive=True)
    if moon:
        mx, my, mr = W - 90, 24, 8
        for y in range(my - mr, my + mr + 1):
            for x in range(mx - mr, mx + mr + 1):
                if (x - mx) ** 2 + (y - my) ** 2 <= mr * mr:
                    shade = (x - mx + 4) ** 2 + (y - my - 2) ** 2 <= (mr - 1) ** 2
                    L.put(x, y, MOON_SHADE if shade else MOON, emissive=True)


def riverside():
    W = st.W
    L = st.Layer()
    rnd = random.Random(31)
    sky(L, rnd)
    # The far bank: a line of trees, dark against the sky.
    for x in range(W):
        top = int(74 + 6 * math.sin(x / 13.0) + 4 * math.sin(x / 5.0 + 1.0))
        for y in range(top, 96):
            L.put(x, y, HILL[(x // 3 + y) % 2], emissive=True)
    # The river: dark water in bands, the moon's glints on it (short, moving with nothing).
    for y in range(96, 112):
        for x in range(W):
            L.put(x, y, WATER[(y // 3) % 2], 0.1)
    for _ in range(W // 10):
        x, y = rnd.randrange(W - 6), rnd.randrange(98, 110)
        for k in range(rnd.randrange(2, 6)):
            L.put(x + k, y, GLINT, emissive=True)
    # The embankment: stone blocks, mortar between them.
    for y in range(112, GROUND):
        for x in range(W):
            row = (y - 112) // 5
            joint = (y - 112) % 5 == 4 or (x + (row % 2) * 8) % 16 == 15
            L.put(x, y, GAP if joint else STONE[(x // 16 + row) % 2], 0.4 if joint else 1.2)
    # The railing: posts, a rail on top.
    for x in range(0, W, 24):
        for y in range(100, 112):
            L.put(x, y, IRON[0], 1.6)
            L.put(x + 1, y, IRON[1], 1.2)
    for x in range(W):
        L.put(x, 100, IRON[0], 1.4)
        L.put(x, 101, IRON[1], 1.0)
    # A willow: a trunk leaning over the water, long hanging leaves.
    wx = 300
    for y in range(66, GROUND):
        lean = (GROUND - y) // 10
        L.rect(wx - 3 + lean, y, wx + 3 + lean, y + 1, WOOD[2], 1.0)
    for k in range(28):
        sx = wx - 30 + k * 2 + 6
        length = 26 + int(10 * math.sin(k * 0.9))
        for y in range(58, 58 + length):
            sway = int(2 * math.sin(y / 6.0 + k))
            L.put(sx + sway, y, LEAF[(k + y // 4) % 2], 0.8)
    for y in range(50, 66):
        for x in range(wx - 26, wx + 34):
            if ((x - wx - 4) / 30.0) ** 2 + ((y - 62) / 12.0) ** 2 < 1.0:
                L.put(x, y, LEAF[(x + y) % 3 == 0], 1.2)
    for x in (120, 380):
        pk.lamp(L, x)
    return L


def shrine_path():
    W = st.W
    L = st.Layer()
    rnd = random.Random(37)
    sky(L, rnd, moon=False)
    # The woods close around: dark masses of leaves, trunks between them.
    for x in range(W):
        top = int(30 + 10 * math.sin(x / 17.0) + 6 * math.sin(x / 7.0 + 2.0))
        for y in range(top, 110):
            L.put(x, y, DARK_LEAF[(x // 4 + y // 3) % 2], 0.6 + 0.4 * math.sin(x * 0.4 + y * 0.3))
    for tx in (60, 190, 260, 420):
        L.rect(tx - 4, 70, tx + 4, GROUND, WOOD[2], 1.0)
    for tx, r, cy in ((190, 30, 52), (420, 34, 48)):
        pk.tree(L, tx, r, cy)
    # The grass under the trees.
    for y in range(110, GROUND):
        for x in range(W):
            L.put(x, y, st.GRASS[(x * 3 + y) % 5 == 0], 0.5 + 0.5 * ((x * 7 + y * 13) % 16) / 15.0)
    # Two stone lanterns, a flame glowing in each.
    for lx in (150, 330):
        L.rect(lx - 7, GROUND - 6, lx + 7, GROUND, STONE[1], 1.2)
        L.rect(lx - 3, GROUND - 26, lx + 3, GROUND - 6, STONE[0], 1.4)
        L.rect(lx - 8, GROUND - 32, lx + 8, GROUND - 26, STONE[1], 1.2)
        for y in range(GROUND - 44, GROUND - 32):
            for x in range(lx - 7, lx + 7):
                edge = x in (lx - 7, lx + 6)
                L.put(x, y, STONE[0] if edge else GLASS[(abs(x - lx) < 3)], 1.2 if edge else 0.0,
                      emissive=not edge)
        for y in range(GROUND - 50, GROUND - 44):
            half = 10 - (GROUND - 44 - y)
            L.rect(lx - half, y, lx + half, y + 1, STONE[1], 1.6)
    # The gate at the west end: two posts, two beams; a rope across with paper strips: closed.
    g0, g1 = 34, 96
    for gx in (g0, g1):
        L.rect(gx - 3, 54, gx + 3, GROUND, WOOD[1], 1.4)
    L.rect(g0 - 12, 46, g1 + 12, 52, WOOD[0], 1.8)
    L.rect(g0 - 4, 60, g1 + 4, 64, WOOD[1], 1.4)
    for x in range(0, g1 + 2):
        y = int(92 + 6 * math.sin((x - 0) / (g1 + 2) * math.pi))
        L.put(x, y, ROPE, 1.2)
        L.put(x, y + 1, ROPE, 0.8)
        if x % 14 == 7:
            for k in range(2, 9):
                L.put(x, y + k, PAPER, 0.6)
                L.put(x + 1, y + k, PAPER, 0.6)
    return L


def write(out, base, W, tiles, maps, props, objects, wall_west=0):
    cols = 16
    rows_n = (len(tiles) + cols - 1) // cols
    img = np.zeros((rows_n * T, cols * T, 4), np.uint8)
    nimg = np.zeros((rows_n * T, cols * T, 4), np.uint8)
    nimg[...] = (128, 128, 255, 0)
    for i, (pic, nm) in enumerate(tiles):
        y, x = (i // cols) * T, (i % cols) * T
        img[y:y + T, x:x + T] = pic
        nimg[y:y + T, x:x + T] = nm
    Image.fromarray(img, "RGBA").save(os.path.join(out, base + "_tiles.png"))
    Image.fromarray(nimg, "RGBA").save(os.path.join(out, base + "_tiles_n.png"))
    bg, main = maps
    tw, th = W // T, H // T
    first_col = 1 + len(tiles)
    m = ET.Element("map", {
        "version": "1.8", "tiledversion": "1.8.0", "orientation": "orthogonal", "renderorder": "right-down",
        "width": str(tw), "height": str(th), "tilewidth": str(T), "tileheight": str(T), "infinite": "0",
        "nextlayerid": "5", "nextobjectid": "1",
    })
    mp = ET.SubElement(m, "properties")
    for k, v in props:
        rooms.prop(mp, k, v)
    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": base + "_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(len(tiles)), "columns": str(cols)})
    rooms.prop(ET.SubElement(ts, "properties"), "normal", base + "_tiles_n.png")
    ET.SubElement(ts, "image", {"source": base + "_tiles.png", "width": str(cols * T), "height": str(rows_n * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": "../../engine/blutmond_collision.tsx"})
    # The ground; a wall at the west end where the way is closed.
    solid = [[first_col + 1 if y * T >= GROUND or x * T < wall_west else 0 for x in range(tw)] for y in range(th)]
    for lid, name, data, extra in ((1, "background", bg, None), (2, "main", main, None), (3, "collision", solid, {"opacity": "0.5"})):
        a = {"id": str(lid), "name": name, "width": str(tw), "height": str(th)}
        if extra:
            a.update(extra)
        layer = ET.SubElement(m, "layer", a)
        rooms.prop(ET.SubElement(layer, "properties"), "role", name)
        ET.SubElement(layer, "data", {"encoding": "csv"}).text = rooms.csv(data)
    og = ET.SubElement(m, "objectgroup", {"id": "4", "name": "objects"})
    rooms.prop(ET.SubElement(og, "properties"), "role", "objects")
    oid = 1
    for otype, name, x, y, ow, oh, point, oprops in objects:
        a = {"id": str(oid), "name": name, "type": otype, "x": str(x), "y": str(y)}
        if ow or oh:
            a["width"], a["height"] = str(ow), str(oh)
        o = ET.SubElement(og, "object", a)
        if point:
            ET.SubElement(o, "point")
        if oprops:
            pp = ET.SubElement(o, "properties")
            for k, v in oprops:
                rooms.prop(pp, k, v)
        oid += 1
    m.set("nextobjectid", str(oid))
    ET.indent(m, " ")
    ET.ElementTree(m).write(os.path.join(out, base + ".tmx"), encoding="UTF-8", xml_declaration=True)


def lit(room_id, title, glow):
    hexes = ", ".join("#%02x%02x%02x" % c for c in glow)
    return [("music", "park_dread"), ("room_id", room_id), ("title", title), ("lighting", "palette"),
            ("ambient", "0.25"), ("light_levels", "4"), ("light_curve", "perceived"), ("light_dither", "false"),
            ("backdrop", "#101a2a"), ("glow_light", hexes)]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "mitsuki", "outskirts")
    os.makedirs(out, exist_ok=True)
    lamp = [("colour", "#ffd890"), ("radius", "150"), ("height", "24"), ("intensity", "1.5"), ("flicker", "0.2"),
            ("flicker_speed", "4")]
    lantern = [("colour", "#ffb070"), ("radius", "110"), ("height", "16"), ("intensity", "1.3"), ("flicker", "0.3"),
               ("flicker_speed", "6")]

    W = 480
    st.W = W
    tiles, maps = st.cut([riverside(), pk.path()])
    write(out, "riverside", W, tiles, maps,
          lit("riverside", "The riverside", GLASS) + [("attach", "Search"), ("Search.Room", "1")],
          [("CameraZone", "room", 0, 0, W, H, False, []),
           ("PlayerStart", "start", W - 40, GROUND, 0, 0, True, [("facing", "left")]),
           ("Light", "lamp", 120, 59, 0, 0, True, lamp),
           ("Light", "lamp_east", 380, 59, 0, 0, True, lamp),
           ("Bat", "bat", 250, 70, 0, 0, True, []),
           ("Bat", "bat_west", 90, 64, 0, 0, True, []),
           ("Door", "to_park", W - 16, GROUND - 48, 16, 48, False,
            [("target_room", "park"), ("target_door", "to_riverside"), ("direction", "east")]),
           ("Door", "to_shrine", 0, GROUND - 48, 16, 48, False,
            [("target_room", "shrine_path"), ("target_door", "to_riverside"), ("direction", "west")])])
    print("riverside: %d tiles of 16 x 16" % len(tiles))

    W = 480
    st.W = W
    tiles, maps = st.cut([shrine_path(), pk.path()])
    write(out, "shrine_path", W, tiles, maps,
          lit("shrine_path", "The shrine path", GLASS) + [("attach", "Search"), ("Search.Room", "2")],
          [("CameraZone", "room", 0, 0, W, H, False, []),
           ("PlayerStart", "start", W - 40, GROUND, 0, 0, True, [("facing", "left")]),
           ("Light", "lantern", 150, GROUND - 38, 0, 0, True, lantern),
           ("Light", "lantern_east", 330, GROUND - 38, 0, 0, True, lantern),
           ("Knight", "knight", 250, GROUND, 0, 0, True, []),
           ("Door", "to_riverside", W - 16, GROUND - 48, 16, 48, False,
            [("target_room", "riverside"), ("target_door", "to_shrine"), ("direction", "east")])],
          wall_west=16)
    print("shrine_path: %d tiles of 16 x 16" % len(tiles))


if __name__ == "__main__":
    main()
