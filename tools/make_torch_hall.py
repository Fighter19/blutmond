#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Draws the torch halls of the test level (docs/plans/lighting.md §7) and writes their rooms:
two rooms alike but for how they are lit, side by side behind a door of the debug lobby.

  torch_hall        lighting = palette: the light baked into the room's colours
  torch_hall_layer  lighting = layer: a light layer of normal tiles added over the room

usage: make_torch_hall.py <assets/test_room>

Writes torch_tiles.png (bricks, flagstones, a pillar) and torch_tiles_n.png (their normal map:
x right, y down, z toward the viewer), torch.png and torch.bmsprite (a bracket and a flame of
four frames, emissive), torch_hall.tmx and torch_hall_layer.tmx. Everything is drawn here.
"""
import math
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_test_room as rooms  # noqa: E402  (csv, prop)

T = 16
COLS = 8
NAMES = ["wall", "wall_crack", "floor_top", "floor", "pillar_l", "pillar_r", "pillar_cap_l", "pillar_cap_r",
         "ceiling", "wall_dark"]

BRICK = [(150, 70, 50), (128, 60, 44), (172, 92, 62)]
MORTAR = (72, 62, 56)
CRACK = (58, 30, 24)
FLAG = [(112, 106, 100), (92, 88, 84), (70, 66, 64)]
PILLAR = [(124, 118, 110), (100, 96, 90), (80, 76, 72)]
DARK = (40, 34, 32)


def norm(x, y, z):
    l = math.sqrt(x * x + y * y + z * z) or 1.0
    return (x / l, y / l, z / l)


def enc(n):
    return tuple(max(0, min(255, int(round((v + 1.0) * 127.5)))) for v in n) + (255,)


def brick_pixel(x, y, variant):
    """A wall of bricks 8 high, 16 long, every second row shifted by half: colour, normal."""
    row = y // 8
    ly = y % 8
    lx = (x + (8 if row % 2 else 0)) % 16
    if ly == 7 or lx == 15:
        return MORTAR, norm(0, 0, 1)
    # Bevels: one pixel strong, one mild, on all four sides (and so the corners face two ways).
    nx = ny = 0.0
    if lx == 0:
        nx = -0.75
    elif lx == 1:
        nx = -0.3
    elif lx == 14:
        nx = 0.75
    elif lx == 13:
        nx = 0.3
    if ly == 0:
        ny = -0.75
    elif ly == 1:
        ny = -0.3
    elif ly == 6:
        ny = 0.75
    elif ly == 5:
        ny = 0.3
    brick = (x // 16 * 3 + row * 5 + variant) % 3
    colour = BRICK[brick]
    if variant == 1 and row == 0 and 3 <= lx <= 10 and ly == 2 + (lx - 3) // 3:
        return CRACK, norm(0.4, -0.6, 0.6)   # a crack, its edge catching the light
    # A little grain.
    g = ((x * 7 + y * 13 + variant * 5) % 11) / 11.0 - 0.5
    return colour, norm(nx + g * 0.12, ny - g * 0.08, 1.0)


def tile_pixel(name, x, y):
    if name in ("wall", "wall_crack"):
        return brick_pixel(x, y, 1 if name == "wall_crack" else 0)
    if name == "wall_dark":
        return DARK, norm(0, 0, 1)
    if name in ("floor_top", "floor", "ceiling"):
        lx = x % 16
        if lx == 15:
            return FLAG[2], norm(0, 0, 1)
        nx = -0.5 if lx == 0 else 0.5 if lx == 14 else 0.0
        if name == "floor_top" and y < 3:
            return FLAG[0], norm(nx * 0.5, -1.0, 0.5)    # the top faces up
        if name == "ceiling" and y > 12:
            return FLAG[1], norm(nx * 0.5, 1.0, 0.5)     # the underside faces down
        return FLAG[1] if (x + y) % 5 else FLAG[2], norm(nx, 0, 1)
    if name.startswith("pillar"):
        px = x + (16 if name.endswith("_r") else 0)
        if name.startswith("pillar_cap") and y < 4:
            return PILLAR[0], norm((px - 15.5) / 18.0, -0.8, 0.6)
        nx = (px - 15.5) / 16.5
        groove = px in (6, 25)
        colour = PILLAR[2] if groove else PILLAR[1] if abs(nx) > 0.7 else PILLAR[0]
        return colour, norm(nx, 0, math.sqrt(max(0.05, 1 - nx * nx)))
    return (0, 0, 0), norm(0, 0, 1)


def draw_tiles(out):
    rows = (len(NAMES) + COLS - 1) // COLS
    img = Image.new("RGBA", (COLS * T, rows * T), (0, 0, 0, 0))
    nrm = Image.new("RGBA", (COLS * T, rows * T), (128, 128, 255, 0))
    for i, name in enumerate(NAMES):
        ox, oy = (i % COLS) * T, (i // COLS) * T
        for y in range(T):
            for x in range(T):
                c, n = tile_pixel(name, x, y)
                img.putpixel((ox + x, oy + y), c + (255,))
                nrm.putpixel((ox + x, oy + y), enc(n))
    img.save(os.path.join(out, "torch_tiles.png"))
    nrm.save(os.path.join(out, "torch_tiles_n.png"))
    return rows


FLAME = [(255, 244, 170), (255, 196, 70), (234, 116, 24), (150, 44, 12)]
IRON = [(46, 46, 52), (84, 84, 94)]


def draw_torch(out):
    """A bracket on the wall and a flame of four frames above it (16 x 24 each)."""
    w, h = 16, 24
    img = Image.new("RGBA", (w * 4, h), (0, 0, 0, 0))
    for f in range(4):
        ox = f * w
        # The bracket: a cup on a stem.
        for y in range(15, 24):
            for x in range(6, 10):
                img.putpixel((ox + x, y), IRON[0] + (255,))
        for x in range(4, 12):
            img.putpixel((ox + x, 14), IRON[1] + (255,))
            img.putpixel((ox + x, 15), IRON[0] + (255,))
        # The flame: a teardrop that sways and breathes.
        sway = [0, 1, 0, -1][f]
        tall = [10, 12, 11, 9][f]
        for y in range(14 - tall, 14):
            t = (14 - y) / tall              # 0 at the bottom, 1 at the tip
            half = 3.6 * math.sin(math.pi * min(1.0, 0.25 + t * 0.85)) * (1 - t * 0.5)
            cx = 7.5 + sway * t * 1.5
            for x in range(16):
                d = abs(x - cx)
                if d > half:
                    continue
                inner = d / max(half, 0.5)
                k = 0 if inner < 0.35 and t < 0.6 else 1 if inner < 0.7 else 2
                if t > 0.8:
                    k = min(3, k + 1)
                img.putpixel((ox + x, y), FLAME[k] + (255,))
    img.save(os.path.join(out, "torch.png"))
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<sprite version="1" image="torch.png" emissive="true">',
             ' <hitbox x="0" y="0" w="0" h="0"/>']
    for f in range(4):
        lines.append(' <frame x="%d" y="0" w="16" h="24" ax="8" ay="24"/>' % (f * w))
    lines.append(' <animation name="burn" loop="true">')
    for f, d in enumerate([6, 5, 7, 5]):
        lines.append('  <step frame="%d" duration="%d"/>' % (f, d))
    lines.append(' </animation>')
    lines.append('</sprite>')
    with open(os.path.join(out, "torch.bmsprite"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def gid(name, first=1):
    return NAMES.index(name) + first


W, H = 30, 12
GROUND = 10
TORCH_X, TORCH_Y = 184, 88           # the bracket's foot
LIGHT_X, LIGHT_Y = 184, 74           # the flame


def layers():
    bg = [[gid("wall_crack") if (x * 7 + y * 3) % 11 == 0 else gid("wall") for x in range(W)] for y in range(H)]
    main = [[0] * W for _ in range(H)]
    for x in range(W):
        main[0][x] = gid("ceiling")
        main[GROUND][x] = gid("floor_top")
        main[GROUND + 1][x] = gid("floor")
    for y in range(1, 7):
        main[y][0] = gid("wall_dark")
        main[y][W - 1] = gid("wall_dark")
    for y in range(3, GROUND):
        main[y][14] = gid("pillar_cap_l" if y == 3 else "pillar_l")
        main[y][15] = gid("pillar_cap_r" if y == 3 else "pillar_r")
    return bg, main


def collision(main, first):
    return [[first + 1 if main[y][x] else 0 for x in range(W)] for y in range(H)]


def write_room(path, room_id, title, lighting, doors, rows):
    bg, main = layers()
    first_col = 1 + len(NAMES)
    first_mat = first_col + 30
    m = ET.Element("map", {
        "version": "1.8", "tiledversion": "1.8.0", "orientation": "orthogonal", "renderorder": "right-down",
        "width": str(W), "height": str(H), "tilewidth": str(T), "tileheight": str(T), "infinite": "0",
        "nextlayerid": "6", "nextobjectid": "1",
    })
    mp = ET.SubElement(m, "properties")
    for k, v in (("music", "castle"), ("room_id", room_id), ("title", title), ("lighting", lighting),
                 ("ambient", "0.3"), ("acoustics", "hall")):
        rooms.prop(mp, k, v)
    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": "torch_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(len(NAMES)), "columns": str(COLS)})
    tsp = ET.SubElement(ts, "properties")
    rooms.prop(tsp, "normal", "torch_tiles_n.png")
    ET.SubElement(ts, "image", {"source": "torch_tiles.png", "width": str(COLS * T), "height": str(rows * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": "../engine/blutmond_collision.tsx"})
    ET.SubElement(m, "tileset", {"firstgid": str(first_mat), "source": "../engine/blutmond_material.tsx"})
    for lid, name, role, data, extra in (
        (1, "background", "background", bg, None),
        (2, "main", "main", main, None),
        (3, "collision", "collision", collision(main, first_col), {"opacity": "0.5"}),
    ):
        a = {"id": str(lid), "name": name, "width": str(W), "height": str(H)}
        if extra:
            a.update(extra)
        l = ET.SubElement(m, "layer", a)
        lp = ET.SubElement(l, "properties")
        rooms.prop(lp, "role", role)
        ET.SubElement(l, "data", {"encoding": "csv"}).text = rooms.csv(data)
    og = ET.SubElement(m, "objectgroup", {"id": "5", "name": "objects"})
    ogp = ET.SubElement(og, "properties")
    rooms.prop(ogp, "role", "objects")
    objects = [("CameraZone", "room", 0, 0, W * T, H * T, False, []),
               ("Light", "torch_light", LIGHT_X, LIGHT_Y, 0, 0, True,
                [("colour", "#ffb060"), ("radius", "160"), ("height", "16"), ("intensity", "1.25"),
                 ("flicker", "0.3"), ("flicker_speed", "5"), ("gain", "0.55")]),
               ("Torch", "torch", TORCH_X, TORCH_Y, 0, 0, True, []),
               ("Bat", "bat", 360, 40, 0, 0, True, [])] + doors
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


def door(name, x, target, target_door, direction):
    return ("Door", name, x, 112, 16, 48, False, [("target_room", target), ("target_door", target_door),
                                                  ("direction", direction), ("panel", "door_panel")])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "test_room")
    rows = draw_tiles(out)
    draw_torch(out)
    write_room(os.path.join(out, "torch_hall.tmx"), "torch_hall", "Torch hall: palettes", "palette",
               [door("exit_west", 0, "debug_lobby", "to_torch_hall", "west"),
                door("exit_east", W * T - 16, "torch_hall_layer", "exit_west", "east")], rows)
    write_room(os.path.join(out, "torch_hall_layer.tmx"), "torch_hall_layer", "Torch hall: light layer", "layer",
               [door("exit_west", 0, "torch_hall", "exit_east", "west")], rows)


if __name__ == "__main__":
    main()
