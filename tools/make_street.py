#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the rural street of the test level and writes its room: a village street at night, a
cottage with a lit window, a picket fence, a tree, a bench and a gas lamp whose light falls on
all of it (lighting = palette, dithered), a puddle on the road that mirrors the lamp, the bench,
the sky and whoever stands over it (a `Puddle` object, src/world/reflect.h), and a knight on
watch by the bench. West of the debug lobby, east of the park; the cottage is Mitsuki's home (its door leads to
assets/mitsuki/home, her prologue), its window lit while her light is on (the room's
`glow_switch`, turned by assets/mitsuki/home/cottage.ms); the lamp's glass glows with its
flicker (`glow_light`).

usage: make_street.py <assets/test_room>

The two layers are drawn as pictures (480 x 160) with a height map for their normals, then cut
into 16 x 16 tiles (the same tile once): street_tiles.png and street_tiles_n.png (the normal
map: x right, y down, z toward the viewer; black where a pixel gives light and is drawn as it
is: the sky, the lamp's glass, the window), rural_street.tmx.
"""
import math
import os
import random
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_test_room as rooms  # noqa: E402  (csv, prop)

T = 16
W, H = 480, 160              # the room in pixels (30 x 10 tiles)
GROUND = 128                 # the road's top: where everything stands
LAMP_X = 298                 # the lamp post's middle
LAMP_LIGHT = (298, 59)       # the flame in its glass
PUDDLE = (254, GROUND, 116, 20)  # x, y (the water line), w, h
WINDOW = (128, 88, 28, 16)       # the cottage's window (frame included): x, y, w, h

# Colours (few: every lit colour takes a palette entry per level of light it is used at).
SKY = [(10, 14, 34), (14, 20, 44), (20, 27, 56), (28, 36, 68)]
STAR, MOON, MOON_SHADE = (236, 236, 214), (240, 232, 190), (176, 170, 140)
HILL = [(16, 22, 34), (22, 30, 42)]
PLASTER = [(196, 184, 156), (172, 160, 134)]
WOOD = [(132, 92, 58), (104, 72, 46), (78, 54, 36)]
ROOF = [(98, 64, 54), (74, 48, 42)]
GLOW = [(255, 206, 120), (238, 170, 86)]
IRON = [(62, 66, 72), (40, 42, 48)]
GLASS = [(255, 240, 186), (255, 252, 230)]
LEAF = [(46, 74, 44), (34, 56, 34)]
GRASS = [(66, 98, 46), (50, 78, 36)]
EARTH = [(110, 88, 64)]
STONE = [(112, 108, 102), (88, 86, 82)]
GAP = (50, 46, 42)
WET = (58, 50, 44)


class Layer:
    """A picture being drawn: colours, a height map (for the normals), what gives light."""

    def __init__(self):
        self.rgb = np.zeros((H, W, 3), np.uint8)
        self.alpha = np.zeros((H, W), bool)
        self.height = np.zeros((H, W), np.float32)
        self.emissive = np.zeros((H, W), bool)

    def put(self, x, y, colour, height=0.0, emissive=False):
        if 0 <= x < W and 0 <= y < H:
            self.rgb[y, x] = colour
            self.alpha[y, x] = True
            self.height[y, x] = height
            self.emissive[y, x] = emissive

    def rect(self, x0, y0, x1, y1, colour, height=0.0, emissive=False):
        for y in range(max(0, y0), min(H, y1)):
            for x in range(max(0, x0), min(W, x1)):
                self.put(x, y, colour, height, emissive)

    def normals(self):
        """Normals from the height map (zero height outside what was drawn), as RGBA."""
        h = self.height
        gx = np.zeros_like(h)
        gy = np.zeros_like(h)
        gx[:, 1:-1] = (h[:, 2:] - h[:, :-2]) * 0.5
        gy[1:-1, :] = (h[2:, :] - h[:-2, :]) * 0.5
        # Edges of a part (where the picture's next pixel belongs to something else) count flat.
        n = np.stack([-gx, -gy, np.ones_like(h)], axis=-1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        out = np.zeros((H, W, 4), np.uint8)
        out[..., :3] = np.clip(np.round((n + 1.0) * 127.5), 0, 255).astype(np.uint8)
        out[..., 3] = 255
        out[self.emissive] = (0, 0, 0, 255)
        out[~self.alpha] = (128, 128, 255, 0)
        return out


def background():
    """Everything above the road: sky, hills, the cottage, the fence, a tree, the lamp, the bench."""
    L = Layer()
    rnd = random.Random(7)
    # The sky (gives its own light: the lamp does not light it), in bands with a dither between.
    for y in range(GROUND):
        t = min(1.0, y / 100.0) * (len(SKY) - 1)
        for x in range(W):
            k = int(t)
            frac = t - k
            bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y % 4][x % 4]
            c = SKY[min(k + (1 if frac * 16 > bayer + 0.5 else 0), len(SKY) - 1)]
            L.put(x, y, c, emissive=True)
    for _ in range(26):
        x, y = rnd.randrange(W), rnd.randrange(4, 70)
        L.put(x, y, STAR, emissive=True)
    # The moon, a crescent.
    mx, my, mr = 418, 24, 9
    for y in range(my - mr, my + mr + 1):
        for x in range(mx - mr, mx + mr + 1):
            if (x - mx) ** 2 + (y - my) ** 2 <= mr * mr:
                shade = (x - mx + 4) ** 2 + (y - my - 2) ** 2 <= (mr - 1) ** 2
                L.put(x, y, MOON_SHADE if shade else MOON, emissive=True)
    # Far hills, two ridges (as far as the moon: not lit).
    for x in range(W):
        r1 = int(92 + 8 * math.sin(x / 47.0) + 4 * math.sin(x / 13.0 + 1.0))
        r2 = int(104 + 6 * math.sin(x / 31.0 + 2.0) + 3 * math.sin(x / 9.0))
        for y in range(r1, GROUND):
            L.put(x, y, HILL[0], emissive=True)
        for y in range(r2, GROUND):
            L.put(x, y, HILL[1], emissive=True)
    # The cottage: plaster between timbers, a slate roof, a door and a lit window.
    cx0, cx1, eave = 24, 168, 80
    for y in range(eave, GROUND):
        for x in range(cx0, cx1):
            L.put(x, y, PLASTER[0], 0.4 + 0.15 * ((x * 13 + y * 7) % 5) / 5)
    for bx in (cx0, 72, 120, cx1 - 4):          # posts
        L.rect(bx, eave, bx + 4, GROUND, WOOD[1], 1.5)
        for y in range(eave, GROUND):
            L.put(bx, y, WOOD[2], 1.0)
            L.put(bx + 3, y, WOOD[0], 1.0)
    for by in (eave, 104):                      # rails
        L.rect(cx0, by, cx1, by + 4, WOOD[1], 1.5)
        for x in range(cx0, cx1):
            L.put(x, by, WOOD[0], 1.0)
            L.put(x, by + 3, WOOD[2], 1.0)
    for y in range(48, eave):                   # roof: rows of slates
        inset = (eave - y) * 7 // 8
        for x in range(cx0 - 10 + inset, cx1 + 10 - inset):
            row = (y - 48) // 4
            edge = (y - 48) % 4 == 3 or (x + row * 5) % 10 == 0
            L.put(x, y, ROOF[1] if edge else ROOF[0], 0.6 if edge else 1.4)
    L.rect(132, 40, 144, 60, ROOF[1], 1.0)      # chimney
    L.rect(130, 38, 146, 42, ROOF[0], 1.6)
    L.rect(82, 92, 106, GROUND, WOOD[1], 1.0)   # door
    for x in range(82, 106, 6):
        for y in range(92, GROUND):
            L.put(x, y, WOOD[2], 0.4)
    L.put(100, 112, IRON[0], 1.8)
    wx, wy, ww, wh = WINDOW
    for y in range(wy, wy + wh):                # window, lit from inside
        for x in range(wx, wx + ww):
            bar = x in (141, 142) or y in (95, 96)
            frame = x in (128, 155) or y in (88, 103)
            if frame or bar:
                L.put(x, y, WOOD[1], 1.2)
            else:
                L.put(x, y, GLOW[0] if (x + y) % 7 else GLOW[1], emissive=True)
    # A tree east of the road.
    tx = 438
    L.rect(tx - 3, 84, tx + 3, GROUND, WOOD[2], 1.0)
    for y in range(40, 100):
        for x in range(tx - 34, tx + 35):
            d = ((x - tx) / 32.0) ** 2 + ((y - 70) / 30.0) ** 2
            wob = 0.12 * math.sin(x * 0.9) * math.cos(y * 0.7)
            if d < 1.0 + wob:
                bump = 1.0 + 0.8 * math.sin(x * 0.35 + y * 0.2) * math.sin(y * 0.45 - x * 0.1)
                L.put(x, y, LEAF[0] if bump > 1.0 else LEAF[1], bump * (1.0 - d) * 2.0)
    # The grass verge along the road.
    for y in range(116, GROUND):
        for x in range(W):
            if y >= 120 or rnd.random() < 0.35:
                L.put(x, y, GRASS[(x * 3 + y) % 5 == 0], 0.5 + 0.5 * rnd.random())
    # Picket fences either side of the lamp.
    for x0, x1 in ((176, 276), (390, 402), (472, W)):
        for x in range(x0, x1):
            k = (x - x0) % 8
            if k < 6:
                top = 102 if k in (2, 3) else 103
                for y in range(top, GROUND):
                    u = (k - 2.5) / 3.0
                    L.put(x, y, WOOD[0] if k < 4 else WOOD[1], 1.2 * math.sqrt(max(0.0, 1 - u * u)))
        for ry in (108, 118):
            for x in range(x0, x1):
                L.put(x, ry, WOOD[1], 1.0)
                L.put(x, ry + 1, WOOD[2], 0.8)
    # The bench: two slats to sit on, two to lean on, iron legs and arms.
    b0, b1 = 318, 372
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
    for x in range(b0, b0 + 8):
        L.put(x, 110, IRON[0], 1.4)
    for x in range(b1 - 8, b1):
        L.put(x, 110, IRON[0], 1.4)
    # The lamp post: an iron pole, a foot, an arm and a glass box with the flame.
    for y in range(64, GROUND):
        for x in range(LAMP_X - 2, LAMP_X + 2):
            u = (x - LAMP_X + 0.5) / 2.0
            L.put(x, y, IRON[0] if x < LAMP_X else IRON[1], 1.8 * math.sqrt(max(0.0, 1 - u * u)))
    for y in range(118, GROUND):
        half = 3 + (y - 118) // 3
        for x in range(LAMP_X - half, LAMP_X + half):
            L.put(x, y, IRON[0] if x < LAMP_X else IRON[1], 1.6)
    L.rect(LAMP_X - 8, 46, LAMP_X + 8, 50, IRON[1], 1.4)     # the cap
    L.rect(LAMP_X - 6, 44, LAMP_X + 6, 46, IRON[0], 1.6)
    L.put(LAMP_X, 43, IRON[0], 1.8)
    for y in range(50, 66):                                   # the glass, lit
        for x in range(LAMP_X - 6, LAMP_X + 6):
            if x in (LAMP_X - 6, LAMP_X + 5) or y == 65:
                L.put(x, y, IRON[1], 1.4)
            else:
                core = abs(x - LAMP_X + 0.5) < 2.5 and 54 <= y <= 62
                L.put(x, y, GLASS[1] if core else GLASS[0], emissive=True)
    L.rect(LAMP_X - 4, 66, LAMP_X + 4, 68, IRON[1], 1.4)
    return L


def road():
    """The road: a strip of packed earth, then cobbles; the puddle is left empty."""
    L = Layer()
    rnd = random.Random(11)
    for y in range(GROUND, H):
        for x in range(W):
            L.put(x, y, GAP, 0.0)
    for y in range(GROUND, GROUND + 4):
        for x in range(W):
            L.put(x, y, EARTH[0], 0.8 - 0.2 * (y - GROUND))
    # Cobbles in rows, each a dome.
    y = GROUND + 4
    row = 0
    while y < H:
        hgt = 7
        x = -(row % 2) * 6
        while x < W:
            w = 10 + rnd.randrange(4)
            c = STONE[rnd.randrange(2)]
            for yy in range(y, min(H, y + hgt - 1)):
                for xx in range(max(0, x), min(W, x + w - 1)):
                    u = (xx - x - (w - 2) / 2.0) / ((w - 1) / 2.0)
                    v = (yy - y - (hgt - 2) / 2.0) / ((hgt - 1) / 2.0)
                    d = u * u + v * v
                    if d <= 1.05:
                        L.put(xx, yy, c, 2.2 * math.sqrt(max(0.0, 1.05 - d)))
            x += w
        y += hgt
        row += 1
    # The puddle: an empty half ellipse under the water line, a wet rim around it.
    px, py, pw, ph = PUDDLE
    cx, rx, ry = px + pw / 2.0, pw / 2.0, ph - 1.0
    for y in range(py, py + ph):
        for x in range(px - 2, px + pw + 2):
            d = ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - py) / ry) ** 2
            if d <= 1.0:
                L.rgb[y, x] = 0
                L.alpha[y, x] = False
                L.height[y, x] = 0
            elif d <= 1.35:
                L.put(x, y, WET, 0.2)
    return L


def cut(layers):
    """Both layers in 16 x 16 tiles: the tileset (each tile once), its normal map, the maps."""
    tiles, index = [], {}
    maps = []
    for L in layers:
        rgba = np.zeros((H, W, 4), np.uint8)
        rgba[..., :3] = L.rgb
        rgba[..., 3] = np.where(L.alpha, 255, 0)
        rgba[~L.alpha] = 0
        nrm = L.normals()
        m = []
        for ty in range(H // T):
            row = []
            for tx in range(W // T):
                pic = rgba[ty * T:(ty + 1) * T, tx * T:(tx + 1) * T]
                if not pic[..., 3].any():
                    row.append(0)
                    continue
                nm = nrm[ty * T:(ty + 1) * T, tx * T:(tx + 1) * T]
                key = pic.tobytes() + nm.tobytes()
                if key not in index:
                    index[key] = len(tiles)
                    tiles.append((pic.copy(), nm.copy()))
                row.append(index[key] + 1)
            m.append(row)
        maps.append(m)
    return tiles, maps


def write_tiles(out, tiles):
    cols = 16
    rows = (len(tiles) + cols - 1) // cols
    img = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg[...] = (128, 128, 255, 0)
    for i, (pic, nm) in enumerate(tiles):
        y, x = (i // cols) * T, (i % cols) * T
        img[y:y + T, x:x + T] = pic
        nimg[y:y + T, x:x + T] = nm
    Image.fromarray(img, "RGBA").save(os.path.join(out, "street_tiles.png"))
    Image.fromarray(nimg, "RGBA").save(os.path.join(out, "street_tiles_n.png"))
    return cols, rows


def write_room(path, tiles, maps, cols, rows):
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
    # The lamp's glass glows with its flicker (glow_light); the cottage's window is Mitsuki's,
    # lit while her light is on (glow_switch, turned by assets/mitsuki/home/cottage.ms).
    for k, v in (("music", "night_escape"), ("attach", "Threat, Bench, Cottage, Uneasy"), ("room_id", "rural_street"), ("title", "Rural street"), ("lighting", "palette"),
                 ("ambient", "0.3"), ("light_levels", "6"), ("light_curve", "perceived"), ("light_dither", "true"),
                 ("backdrop", "#101a2a"), ("glow_light", hexes(GLASS)), ("glow_switch", hexes(GLOW))):
        rooms.prop(mp, k, v)
    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": "street_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(len(tiles)), "columns": str(cols)})
    tsp = ET.SubElement(ts, "properties")
    rooms.prop(tsp, "normal", "street_tiles_n.png")
    ET.SubElement(ts, "image", {"source": "street_tiles.png", "width": str(cols * T), "height": str(rows * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": "../engine/blutmond_collision.tsx"})
    solid = [[first_col + 1 if y * T >= GROUND else 0 for x in range(tw)] for y in range(th)]
    for lid, name, role, data, extra in ((1, "background", "background", bg, None), (2, "main", "main", main, None),
                                         (3, "collision", "collision", solid, {"opacity": "0.5"})):
        a = {"id": str(lid), "name": name, "width": str(tw), "height": str(th)}
        if extra:
            a.update(extra)
        layer = ET.SubElement(m, "layer", a)
        lp = ET.SubElement(layer, "properties")
        rooms.prop(lp, "role", role)
        ET.SubElement(layer, "data", {"encoding": "csv"}).text = rooms.csv(data)
    og = ET.SubElement(m, "objectgroup", {"id": "4", "name": "objects"})
    ogp = ET.SubElement(og, "properties")
    rooms.prop(ogp, "role", "objects")
    px, py, pw, ph = PUDDLE
    objects = [
        ("CameraZone", "room", 0, 0, W, H, False, []),
        ("PlayerStart", "start", 120, GROUND, 0, 0, True, []),   # out of the knight's sight: the music calm
        ("Light", "lamp", LAMP_LIGHT[0], LAMP_LIGHT[1], 0, 0, True,
         [("colour", "#ffd890"), ("radius", "140"), ("height", "24"), ("intensity", "1.6"), ("flicker", "0.2"),
          ("flicker_speed", "4")]),
        ("Puddle", "puddle", px, py, pw, ph, False, [("squash", "4"), ("ripple", "1"), ("strength", "12")]),
        ("Door", "exit_east", W - 16, GROUND - 48, 16, 48, False,
         [("target_room", "debug_lobby"), ("target_door", "to_rural_street"), ("direction", "east"), ("panel", "door_panel")]),
        # West: the park (assets/mitsuki/park, tools/make_park.py), where Hina waits.
        ("Door", "to_park", 0, GROUND - 48, 16, 48, False,
         [("target_room", "park"), ("target_door", "to_street"), ("direction", "west")]),
        # The cottage's door: Mitsuki's home (assets/mitsuki/home), with Up.
        ("Door", "to_home", 82, GROUND - 48, 24, 48, False,
         [("target_room", "mitsuki_home"), ("target_door", "to_street"), ("direction", "north")]),
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
    ET.ElementTree(m).write(path, encoding="UTF-8", xml_declaration=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "test_room")
    tiles, maps = cut([background(), road()])
    cols, rows = write_tiles(out, tiles)
    write_room(os.path.join(out, "rural_street.tmx"), tiles, maps, cols, rows)
    print("%d tiles of 16 x 16" % len(tiles))


if __name__ == "__main__":
    main()
