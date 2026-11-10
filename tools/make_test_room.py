#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Generates the test room in assets/test_room/: an own tileset (drawn here, no third-party
art) and a Tiled map that uses every room-format feature the cooker supports: layer roles with
parallax, flipped tiles, several palette banks, a map larger than the screen and the hardware
map, collision with slopes and jump-through platforms, materials and objects.

Needs Pillow; the outputs are committed, so this only runs when the test room changes.

  make_test_room.py [out dir]
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

T = 16
COLS = 8

# Colour groups, so the palette packer needs several banks.
GRASS = [(40, 160, 40), (90, 200, 60), (20, 100, 30)]
DIRT = [(120, 72, 40), (150, 96, 56), (96, 56, 32), (70, 40, 24)]
SKY = [(40, 60, 120), (56, 80, 150), (72, 100, 170), (88, 120, 190), (230, 230, 240)]
STONE = [(90, 90, 100), (120, 120, 130), (150, 150, 160), (60, 60, 70)]
MOON = [(220, 90, 90), (240, 130, 120), (180, 60, 70)]
BRICK = [(110, 30, 30), (140, 44, 40), (80, 20, 24)]
WOOD = [(150, 100, 50), (110, 70, 30)]
YELLOW = [(250, 220, 80), (200, 160, 40)]

TILES = {}  # name -> index


def tile(name):
    return TILES[name]


def draw_tileset():
    names = [
        "empty", "grass", "dirt", "edge_left", "edge_right", "platform", "brick", "brick2",
        "sky_top", "sky_bottom", "cloud_left", "cloud_right", "bush", "flower", "lantern", "spikes",
        "slope_up", "slope_down", "moon_tl", "moon_tr", "moon_bl", "moon_br", "stone", "stone2",
        "water", "ladder", "pillar", "pillar_top", "dirt2", "grass2", "sky_mid", "stone3",
    ]
    for i, n in enumerate(names):
        TILES[n] = i
    rows = (len(names) + COLS - 1) // COLS
    img = Image.new("RGBA", (COLS * T, rows * T), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def at(name):
        i = tile(name)
        return (i % COLS) * T, (i // COLS) * T

    def fill(name, colour):
        x, y = at(name)
        d.rectangle([x, y, x + T - 1, y + T - 1], fill=colour)

    def dirt(name, seed):
        x, y = at(name)
        d.rectangle([x, y, x + T - 1, y + T - 1], fill=DIRT[0])
        for k in range(10):
            px, py = (k * 7 + seed * 3) % T, (k * 5 + seed) % T
            d.point((x + px, y + py), fill=DIRT[1 + k % 3])

    # Ground
    dirt("dirt", 1)
    dirt("dirt2", 4)
    for name, variant in (("grass", 0), ("grass2", 1)):
        dirt(name, 2 + variant)
        x, y = at(name)
        d.rectangle([x, y, x + T - 1, y + 4], fill=GRASS[0])
        for k in range(0, T, 3):
            d.line([(x + k, y), (x + k, y + 5 + (k + variant) % 3)], fill=GRASS[1 + (k // 3) % 2])
    # Left edge: grass corner with the dirt fading out. The right edge is its mirror image, so
    # the cooker can store it once and use a flipped copy.
    for name, flip in (("edge_left", False), ("edge_right", True)):
        x, y = at(name)
        tmp = Image.new("RGBA", (T, T), (0, 0, 0, 0))
        td = ImageDraw.Draw(tmp)
        td.rectangle([4, 0, T - 1, T - 1], fill=DIRT[0])
        td.rectangle([2, 0, T - 1, 4], fill=GRASS[0])
        td.line([(4, 5), (4, T - 1)], fill=DIRT[3])
        td.point((9, 9), fill=DIRT[1])
        td.point((12, 13), fill=DIRT[2])
        if flip:
            tmp = tmp.transpose(Image.FLIP_LEFT_RIGHT)
        img.paste(tmp, (x, y))
    # Slopes: up is drawn, down is its mirror.
    for name, flip in (("slope_up", False), ("slope_down", True)):
        x, y = at(name)
        tmp = Image.new("RGBA", (T, T), (0, 0, 0, 0))
        td = ImageDraw.Draw(tmp)
        td.polygon([(0, T - 1), (T - 1, 0), (T - 1, T - 1)], fill=DIRT[0])
        td.line([(0, T - 1), (T - 1, 0)], fill=GRASS[0], width=2)
        if flip:
            tmp = tmp.transpose(Image.FLIP_LEFT_RIGHT)
        img.paste(tmp, (x, y))
    # Jump-through platform
    x, y = at("platform")
    d.rectangle([x, y, x + T - 1, y + 5], fill=WOOD[0])
    d.line([(x, y + 5), (x + T - 1, y + 5)], fill=WOOD[1])
    d.line([(x + 7, y), (x + 7, y + 5)], fill=WOOD[1])
    # Bricks
    for name, offset in (("brick", 0), ("brick2", 4)):
        x, y = at(name)
        d.rectangle([x, y, x + T - 1, y + T - 1], fill=BRICK[0])
        for row in range(4):
            yy = y + row * 4
            d.line([(x, yy), (x + T - 1, yy)], fill=BRICK[2])
            sx = (offset + row * 4) % 8
            d.line([(x + sx, yy), (x + sx, yy + 3)], fill=BRICK[2])
            d.point((x + (sx + 3) % T, yy + 2), fill=BRICK[1])
    # Sky
    fill("sky_top", SKY[0])
    fill("sky_mid", SKY[1])
    x, y = at("sky_bottom")
    for row in range(T):
        d.line([(x, y + row), (x + T - 1, y + row)], fill=SKY[2] if row < 8 else SKY[3])
    for name, flip in (("cloud_left", False), ("cloud_right", True)):
        x, y = at(name)
        tmp = Image.new("RGBA", (T, T), SKY[1])
        td = ImageDraw.Draw(tmp)
        td.ellipse([4, 6, 20, 14], fill=SKY[4])
        td.ellipse([8, 3, 18, 11], fill=SKY[4])
        if flip:
            tmp = tmp.transpose(Image.FLIP_LEFT_RIGHT)
        img.paste(tmp, (x, y))
    # Moon (2x2 tiles) on the sky
    big = Image.new("RGBA", (2 * T, 2 * T), SKY[0])
    bd = ImageDraw.Draw(big)
    bd.ellipse([3, 3, 28, 28], fill=MOON[0])
    bd.ellipse([8, 7, 16, 14], fill=MOON[1])
    bd.ellipse([17, 18, 23, 24], fill=MOON[2])
    for name, ox, oy in (("moon_tl", 0, 0), ("moon_tr", 1, 0), ("moon_bl", 0, 1), ("moon_br", 1, 1)):
        x, y = at(name)
        img.paste(big.crop((ox * T, oy * T, ox * T + T, oy * T + T)), (x, y))
    # Foreground decoration
    x, y = at("bush")
    d.ellipse([x + 1, y + 6, x + 14, y + 15], fill=GRASS[2])
    d.ellipse([x + 4, y + 4, x + 11, y + 12], fill=GRASS[0])
    x, y = at("flower")
    d.line([(x + 7, y + 8), (x + 7, y + 15)], fill=GRASS[2])
    d.ellipse([x + 4, y + 3, x + 10, y + 9], fill=YELLOW[0])
    x, y = at("lantern")
    d.rectangle([x + 5, y + 4, x + 10, y + 12], fill=YELLOW[1])
    d.rectangle([x + 6, y + 5, x + 9, y + 11], fill=YELLOW[0])
    d.line([(x + 7, y), (x + 7, y + 4)], fill=STONE[3])
    x, y = at("spikes")
    for k in range(4):
        d.polygon([(x + k * 4, y + T - 1), (x + k * 4 + 2, y + 6), (x + k * 4 + 3, y + T - 1)], fill=STONE[2])
    # Stone
    for name, shade in (("stone", 0), ("stone2", 1), ("stone3", 2)):
        x, y = at(name)
        d.rectangle([x, y, x + T - 1, y + T - 1], fill=STONE[shade])
        d.rectangle([x, y, x + T - 1, y + T - 1], outline=STONE[3])
    fill("water", (40, 80, 200, 255))
    x, y = at("water")
    d.line([(x, y + 2), (x + T - 1, y + 2)], fill=(120, 160, 240))
    x, y = at("ladder")
    d.line([(x + 3, y), (x + 3, y + T - 1)], fill=WOOD[1])
    d.line([(x + 12, y), (x + 12, y + T - 1)], fill=WOOD[1])
    for yy in range(2, T, 5):
        d.line([(x + 3, y + yy), (x + 12, y + yy)], fill=WOOD[0])
    x, y = at("pillar")
    d.rectangle([x + 3, y, x + 12, y + T - 1], fill=STONE[1])
    d.line([(x + 5, y), (x + 5, y + T - 1)], fill=STONE[2])
    x, y = at("pillar_top")
    d.rectangle([x + 1, y + 4, x + 14, y + T - 1], fill=STONE[1])
    d.rectangle([x + 3, y + 8, x + 12, y + T - 1], fill=STONE[2])
    return img


W, H = 64, 12  # metatiles: 1024 x 192 px, larger than the screen and the 512x256 hardware map


def gid(name, hflip=False, vflip=False, first=1):
    g = tile(name) + first
    if hflip:
        g |= 0x80000000
    if vflip:
        g |= 0x40000000
    return g


def build_layers():
    bg = [[0] * W for _ in range(H)]
    main = [[0] * W for _ in range(H)]
    fg = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            bg[y][x] = gid("sky_top" if y < 4 else ("sky_mid" if y < 8 else "sky_bottom"))
    for cx in (3, 17, 33, 50):
        bg[2][cx] = gid("cloud_left")
        bg[2][cx + 1] = gid("cloud_right")
    bg[1][24], bg[1][25], bg[2][24], bg[2][25] = gid("moon_tl"), gid("moon_tr"), gid("moon_bl"), gid("moon_br")
    # a cloud flipped upside down: only the flip bit differs
    bg[5][40], bg[5][41] = gid("cloud_left", vflip=True), gid("cloud_right", vflip=True)

    ground = 10  # top row of the ground
    pits = set(range(14, 18)) | set(range(40, 43))
    for x in range(W):
        if x in pits:
            continue
        main[ground][x] = gid("grass" if x % 3 else "grass2")
        main[ground + 1][x] = gid("dirt" if x % 2 else "dirt2")
        if x - 1 in pits:
            main[ground][x] = gid("edge_left")
        if x + 1 in pits:
            main[ground][x] = gid("edge_right")
    # A hill with slopes: up at x=22, plateau 23..26, down at x=27
    main[ground - 1][22] = gid("slope_up")
    for x in range(23, 27):
        main[ground - 1][x] = gid("grass")
        main[ground][x] = gid("dirt")
    main[ground - 1][27] = gid("slope_down")
    main[ground][22] = gid("dirt")
    main[ground][27] = gid("dirt")
    # Jump-through platforms over the first pit and later
    for x in range(13, 19):
        main[7][x] = gid("platform")
    for x in range(44, 49):
        main[6][x] = gid("platform")
    # A brick wall with a ladder, a pillar, spikes and water
    for y in range(4, ground):
        main[y][32] = gid("brick" if y % 2 else "brick2")
        main[y][33] = gid("brick2" if y % 2 else "brick", hflip=True)
    for y in range(4, ground):
        main[y][31] = gid("ladder")
    main[3][31] = gid("platform")
    main[ground - 1][36] = gid("pillar_top")
    for y in range(ground - 1, ground):
        main[y][37] = gid("pillar")
    for x in (54, 55, 56):
        main[ground - 1][x] = gid("spikes")
    for x in (40, 41, 42):
        main[ground + 1][x] = gid("water")
    for x in (60, 61, 62, 63):
        for y in range(5, ground):
            main[y][x] = gid("stone" if (x + y) % 3 == 0 else ("stone2" if (x + y) % 3 == 1 else "stone3"))
    # Foreground
    for x in (5, 9, 29, 47, 58):
        fg[ground - 1][x] = gid("bush")
    for x in (7, 20, 52):
        fg[ground - 1][x] = gid("flower")
    fg[3][12] = gid("lantern")
    fg[3][44] = gid("lantern", hflip=True)
    return bg, main, fg


def collision_layer(main, first):
    # Collision tileset ids: row 0 solid shapes, row 1 jump-through (offset 15).
    SOLID, JUMP = 0, 15
    shapes = {"full": 1, "half_bottom": 3, "slope_1_1": 5}
    col = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            g = main[y][x]
            if not g:
                continue
            name = [n for n, i in TILES.items() if i == (g & 0x0FFFFFFF) - 1][0]
            if name in ("grass", "grass2", "dirt", "dirt2", "edge_left", "edge_right", "brick", "brick2",
                        "stone", "stone2", "stone3", "pillar", "pillar_top"):
                col[y][x] = first + SOLID + shapes["full"]
            elif name == "slope_up":
                col[y][x] = first + SOLID + shapes["slope_1_1"]
            elif name == "slope_down":
                col[y][x] = (first + SOLID + shapes["slope_1_1"]) | 0x80000000
            elif name == "platform":
                col[y][x] = first + JUMP + shapes["full"]
    return col


def material_layer(main, first):
    MATERIALS = ["normal", "water", "damage", "death", "conveyor", "ice", "ladder", "bounce", "sticky"]
    mat = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            g = main[y][x]
            if not g:
                continue
            name = [n for n, i in TILES.items() if i == (g & 0x0FFFFFFF) - 1][0]
            m = {"water": "water", "spikes": "damage", "ladder": "ladder"}.get(name)
            if m:
                mat[y][x] = first + MATERIALS.index(m)
    return mat


def csv(rows):
    return "\n" + ",\n".join(",".join(str(v) for v in row) for row in rows) + "\n"


def prop(parent, name, value, ptype=None):
    a = {"name": name, "value": str(value)}
    if ptype:
        a["type"] = ptype
    ET.SubElement(parent, "property", a)


def write_tmx(path):
    bg, main, fg = build_layers()
    tile_count = len(TILES)
    first_col = 1 + tile_count
    first_mat = first_col + 30

    m = ET.Element("map", {
        "version": "1.8", "tiledversion": "1.8.0", "orientation": "orthogonal", "renderorder": "right-down",
        "width": str(W), "height": str(H), "tilewidth": str(T), "tileheight": str(T), "infinite": "0",
        "nextlayerid": "7", "nextobjectid": "1",
    })
    mp = ET.SubElement(m, "properties")
    prop(mp, "room_id", "test_room")
    prop(mp, "area", "test")
    prop(mp, "gravity", "0.25", "float")

    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": "test_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(tile_count), "columns": str(COLS)})
    rows = (tile_count + COLS - 1) // COLS
    ET.SubElement(ts, "image", {"source": "tileset.png", "width": str(COLS * T), "height": str(rows * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": "../engine/blutmond_collision.tsx"})
    ET.SubElement(m, "tileset", {"firstgid": str(first_mat), "source": "../engine/blutmond_material.tsx"})

    def layer(lid, name, role, data, extra=None):
        a = {"id": str(lid), "name": name, "width": str(W), "height": str(H)}
        if extra:
            a.update(extra)
        l = ET.SubElement(m, "layer", a)
        lp = ET.SubElement(l, "properties")
        prop(lp, "role", role)
        ET.SubElement(l, "data", {"encoding": "csv"}).text = csv(data)

    layer(1, "background", "background", bg, {"parallaxx": "0.5"})
    layer(2, "main", "main", main)
    layer(3, "foreground", "foreground", fg)
    layer(4, "collision", "collision", collision_layer(main, first_col), {"opacity": "0.5"})
    layer(5, "materials", "collision_material", material_layer(main, first_mat), {"opacity": "0.5"})

    og = ET.SubElement(m, "objectgroup", {"id": "6", "name": "objects"})
    ogp = ET.SubElement(og, "properties")
    prop(ogp, "role", "objects")
    objects = [
        ("PlayerStart", "start", 40, 160, 0, 0, True, [("facing", "right")]),
        ("CameraZone", "room", 0, 0, W * T, H * T, False, []),
        ("KillZone", "pits", 0, H * T, W * T, 16, False, [("respawn", "last_ground")]),
        ("Door", "exit_east", W * T - 16, 48, 16, 32, False, [("target_room", "test_room_2"), ("target_door", "exit_west"), ("direction", "east")]),
        ("SavePoint", "save", 400, 160, 0, 0, True, []),
        ("Pickup", "heart", 250, 112, 0, 0, True, [("item", "heart_max_up"), ("persistent", "true")]),
    ]
    oid = 1
    for otype, name, x, y, w, h, point, props in objects:
        a = {"id": str(oid), "name": name, "type": otype, "x": str(x), "y": str(y)}
        if w or h:
            a["width"], a["height"] = str(w), str(h)
        o = ET.SubElement(og, "object", a)
        if point:
            ET.SubElement(o, "point")
        if props:
            p = ET.SubElement(o, "properties")
            for k, v in props:
                if isinstance(v, tuple):
                    prop(p, k, v[0], v[1])
                else:
                    prop(p, k, v)
        oid += 1
    m.set("nextobjectid", str(oid))
    ET.indent(m, " ")
    ET.ElementTree(m).write(path, encoding="UTF-8", xml_declaration=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "test_room")
    os.makedirs(out, exist_ok=True)
    draw_tileset().save(os.path.join(out, "tileset.png"))
    write_tmx(os.path.join(out, "room.tmx"))


if __name__ == "__main__":
    main()
