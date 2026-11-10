#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Generates the engine's Tiled tilesets for collision and materials (assets/engine/).

Row 0 of the collision tileset holds the solid shapes, row 1 the jump-through shapes, in the
order of docs/spec/room-format.md section 3.3. Needs Pillow; the outputs are committed, so
this only runs when the tilesets change.

  make_engine_tilesets.py <out dir>
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

T = 16

# Collision shapes (byte 0, bits 0-3), see docs/PLAN.md "Rooms: ... collision".
SHAPES = ["empty", "full", "half_top", "half_bottom", "half_left", "slope_1_1",
          "slope_1_2_a", "slope_1_2_b", "slope_1_4_a", "slope_1_4_b", "slope_1_4_c",
          "slope_1_4_d", "slope_2_1_low", "slope_2_1_high", "custom"]
MATERIALS = ["normal", "water", "damage", "death", "conveyor", "ice", "ladder", "bounce", "sticky"]
SOLID_COLOR = (220, 40, 40)
JUMP_COLOR = (240, 200, 40)
MAT_COLORS = {"water": (60, 120, 240), "damage": (200, 40, 200), "death": (20, 20, 20),
              "conveyor": (160, 160, 160), "ice": (160, 230, 255), "ladder": (60, 200, 80),
              "bounce": (255, 140, 200), "sticky": (120, 80, 40)}


def shape_heights(shape):
    """Height of the floor in each of 16 columns, rising to the right (no flip)."""
    if shape == "full":
        return [16] * 16
    if shape == "half_top":
        return None  # drawn as a band
    if shape == "half_bottom":
        return [8] * 16
    if shape == "half_left":
        return [16] * 8 + [0] * 8
    if shape == "slope_1_1":
        return [i + 1 for i in range(16)]
    if shape.startswith("slope_1_2_"):
        base = 0 if shape.endswith("a") else 8
        return [base + (i + 1) // 2 for i in range(16)]
    if shape.startswith("slope_1_4_"):
        base = "abcd".index(shape[-1]) * 4
        return [base + (i + 1) // 4 for i in range(16)]
    if shape == "slope_2_1_low":
        return [min(16, 2 * (i + 1)) for i in range(16)]
    if shape == "slope_2_1_high":
        return [max(0, 2 * (i + 1) - 16) for i in range(16)]
    return [0] * 16


def draw_shape(d, ox, oy, shape, color, jump):
    fill = color + ((90,) if jump else (160,))
    if shape in ("empty",):
        return
    if shape == "custom":
        d.rectangle([ox, oy, ox + T - 1, oy + T - 1], outline=color + (255,))
        d.text((ox + 4, oy + 2), "C", fill=color + (255,))
        return
    if shape == "half_top":
        d.rectangle([ox, oy, ox + T - 1, oy + 7], fill=fill, outline=color + (255,))
    else:
        hs = shape_heights(shape)
        for i, hgt in enumerate(hs):
            if hgt:
                d.line([(ox + i, oy + T - hgt), (ox + i, oy + T - 1)], fill=fill)
        for i, hgt in enumerate(hs):
            if hgt:
                d.point((ox + i, oy + T - hgt), fill=color + (255,))
    if jump:  # arrow up: can be jumped through from below
        cx = ox + T // 2
        d.line([(cx, oy + 3), (cx, oy + 10)], fill=(255, 255, 255, 255))
        d.line([(cx - 3, oy + 6), (cx, oy + 3), (cx + 3, oy + 6)], fill=(255, 255, 255, 255))


def write_collision_tileset(out):
    """Row 0: solid on every side, row 1: jump-through (solid on top only)."""
    cols = len(SHAPES)
    img = Image.new("RGBA", (T * cols, T * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ts = ET.Element("tileset", {
        "version": "1.8", "tiledversion": "1.8.0", "name": "blutmond_collision",
        "tilewidth": str(T), "tileheight": str(T), "tilecount": str(cols * 2), "columns": str(cols),
    })
    ET.SubElement(ts, "image", {"source": "blutmond_collision.png",
                                "width": str(img.width), "height": str(img.height)})
    for row, solid in enumerate(("all", "top")):
        for c, shape in enumerate(SHAPES):
            draw_shape(d, c * T, row * T, shape, SOLID_COLOR if solid == "all" else JUMP_COLOR,
                       solid == "top")
            tid = row * cols + c
            tile = ET.SubElement(ts, "tile", {"id": str(tid), "type": "collision"})
            props = ET.SubElement(tile, "properties")
            ET.SubElement(props, "property", {"name": "shape", "value": shape})
            ET.SubElement(props, "property", {"name": "solid", "value": solid})
    img.save(os.path.join(out, "blutmond_collision.png"))
    ET.indent(ts, " ")
    ET.ElementTree(ts).write(os.path.join(out, "blutmond_collision.tsx"),
                             encoding="UTF-8", xml_declaration=True)

    img = Image.new("RGBA", (T * len(MATERIALS), T), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ts = ET.Element("tileset", {
        "version": "1.8", "tiledversion": "1.8.0", "name": "blutmond_material",
        "tilewidth": str(T), "tileheight": str(T), "tilecount": str(len(MATERIALS)),
        "columns": str(len(MATERIALS)),
    })
    ET.SubElement(ts, "image", {"source": "blutmond_material.png",
                                "width": str(img.width), "height": str(img.height)})
    for i, mat in enumerate(MATERIALS):
        if mat in MAT_COLORS:
            d.rectangle([i * T, 0, i * T + T - 1, T - 1], fill=MAT_COLORS[mat] + (110,),
                        outline=MAT_COLORS[mat] + (255,))
            d.text((i * T + 4, 2), {"death": "X", "sticky": "M"}.get(mat, mat[0].upper()), fill=(255, 255, 255, 255))
        tile = ET.SubElement(ts, "tile", {"id": str(i), "type": "material"})
        props = ET.SubElement(tile, "properties")
        ET.SubElement(props, "property", {"name": "material", "value": mat})
        ET.SubElement(props, "property", {"name": "parameter", "value": "0", "type": "int"})
    img.save(os.path.join(out, "blutmond_material.png"))
    ET.indent(ts, " ")
    ET.ElementTree(ts).write(os.path.join(out, "blutmond_material.tsx"),
                             encoding="UTF-8", xml_declaration=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "engine")
    os.makedirs(out, exist_ok=True)
    write_collision_tileset(out)


if __name__ == "__main__":
    main()
