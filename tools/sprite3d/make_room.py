#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""3D sprites (docs/plans/3d-sprites.md) for a room: renders a room modelled in Blender into
its tiles, their normal map and the room's Tiled map.

usage: make_room.py <room.py> --out <dir> [--save-blend <file.blend>] [--preview <file.png>]
                    [--blender <path>] [--keep <dir>]

<room.py> builds the room with build(Rig) as a character script does (tools/sprite3d/rig.py:
materials, parts on joints, one pose), with the camera straight at the back wall (`yaw` 0)
and a little from above (`pitch`), and says what the room is in ROOM, a dict:

  name        the files' name: <name>_tiles.png, <name>_tiles_n.png, <name>.tmx
  size        (width, height) in pixels, multiples of 16
  ground      the picture's row where the world point (x, 0, 0) lands: the player's feet
              stand on the floor there (the sprites' sink below the row the collision's top)
  solid       the rows of tiles that are solid, from the top (the floor)
  properties  [(name, value)] of the map (room_id, title, music, lighting, attach, ...)
  objects     [(type, name, x, y, w, h, [(property, value)])]: a point where w and h are 0

The world: x to the right (one unit a pixel), y away from the viewer, z up. The picture is
rendered as a sprite frame is (render.py, pixelize.py: material, light bands, inner lines
where a part lies well in front of another), with `emissive` materials (the night sky, a
lamp's glass) black in the normal map: in a lit room (`lighting = palette`) they are drawn as
they are, the rest is shaded by the room's lights from the normals. Then it is cut into
16 x 16 tiles, each once (the background layer); the main layer is empty.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
T = 16
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True   # no __pycache__ beside the room's script
import pixelize  # noqa: E402


def parse(argv):
    opts, rest, i = {}, [], 0
    while i < len(argv):
        if argv[i].startswith('--'):
            opts[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            rest.append(argv[i])
            i += 1
    return rest, opts


def room_of(path):
    import importlib.util
    spec = importlib.util.spec_from_file_location('room', path)
    mod = importlib.util.module_from_spec(spec)
    # The script imports bpy only inside build(): ROOM can be read here.
    spec.loader.exec_module(mod)
    return mod.ROOM


def csv(rows):
    return "\n" + ",\n".join(",".join(str(v) for v in row) for row in rows) + "\n"


def prop(parent, name, value):
    ET.SubElement(parent, "property", {"name": name, "value": str(value)})


def cut(rgba, nmap):
    """The picture in 16 x 16 tiles, each once: (tiles [(picture, normals)], map)."""
    h, w = rgba.shape[:2]
    tiles, index, m = [], {}, []
    for ty in range(h // T):
        row = []
        for tx in range(w // T):
            pic = rgba[ty * T:(ty + 1) * T, tx * T:(tx + 1) * T]
            if not pic[..., 3].any():
                row.append(0)
                continue
            nm = nmap[ty * T:(ty + 1) * T, tx * T:(tx + 1) * T]
            key = pic.tobytes() + nm.tobytes()
            if key not in index:
                index[key] = len(tiles)
                tiles.append((pic.copy(), nm.copy()))
            row.append(index[key] + 1)
        m.append(row)
    return tiles, m


def write_room(path, room, tiles, bg, cols, rows):
    w, h = room['size']
    tw, th = w // T, h // T
    first_col = 1 + len(tiles)
    m = ET.Element("map", {
        "version": "1.8", "tiledversion": "1.8.0", "orientation": "orthogonal", "renderorder": "right-down",
        "width": str(tw), "height": str(th), "tilewidth": str(T), "tileheight": str(T), "infinite": "0",
        "nextlayerid": "5", "nextobjectid": "1",
    })
    mp = ET.SubElement(m, "properties")
    for k, v in room['properties']:
        prop(mp, k, v)
    name = room['name']
    ts = ET.SubElement(m, "tileset", {"firstgid": "1", "name": name + "_tiles", "tilewidth": str(T),
                                      "tileheight": str(T), "tilecount": str(len(tiles)), "columns": str(cols)})
    prop(ET.SubElement(ts, "properties"), "normal", name + "_tiles_n.png")
    ET.SubElement(ts, "image", {"source": name + "_tiles.png", "width": str(cols * T), "height": str(rows * T)})
    ET.SubElement(m, "tileset", {"firstgid": str(first_col), "source": room['collision_tileset']})
    solid = [[first_col + 1 if y in room['solid'] else 0 for _ in range(tw)] for y in range(th)]
    empty = [[0] * tw for _ in range(th)]
    for lid, lname, data, extra in ((1, "background", bg, None), (2, "main", empty, None),
                                    (3, "collision", solid, {"opacity": "0.5"})):
        a = {"id": str(lid), "name": lname, "width": str(tw), "height": str(th)}
        if extra:
            a.update(extra)
        layer = ET.SubElement(m, "layer", a)
        prop(ET.SubElement(layer, "properties"), "role", lname)
        ET.SubElement(layer, "data", {"encoding": "csv"}).text = csv(data)
    og = ET.SubElement(m, "objectgroup", {"id": "4", "name": "objects"})
    prop(ET.SubElement(og, "properties"), "role", "objects")
    oid = 1
    for otype, oname, x, y, ow, oh, props in room['objects']:
        a = {"id": str(oid), "name": oname, "type": otype, "x": str(x), "y": str(y)}
        if ow or oh:
            a["width"], a["height"] = str(ow), str(oh)
        o = ET.SubElement(og, "object", a)
        if not (ow or oh):
            ET.SubElement(o, "point")
        if props:
            pp = ET.SubElement(o, "properties")
            for k, v in props:
                prop(pp, k, v)
        oid += 1
    m.set("nextobjectid", str(oid))
    ET.indent(m, " ")
    ET.ElementTree(m).write(path, encoding="UTF-8", xml_declaration=True)


def main(argv):
    rest, opts = parse(argv[1:])
    if len(rest) != 1 or 'out' not in opts:
        print(__doc__, file=sys.stderr)
        return 2
    src, out = rest[0], opts['out']
    room = room_of(src)
    blender = opts.get('blender') or os.environ.get('BLENDER') or shutil.which('blender')
    if not blender:
        print('make_room: Blender not found (--blender or $BLENDER)', file=sys.stderr)
        return 1
    w, h = room['size']
    jobs = [{'frame': 0, 'w': w, 'h': h, 'ax': 0, 'ay': room['ground'], 'ground': room['ground']}]
    work = opts.get('keep') or tempfile.mkdtemp(prefix='sprite3d_room_')
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, 'jobs.json'), 'w') as fh:
        json.dump(jobs, fh)
    cmd = [blender, '-b', '--factory-startup', '-P', os.path.join(HERE, 'render.py'), '--',
           '--character', src, '--jobs', os.path.join(work, 'jobs.json'), '--out', work]
    if 'save-blend' in opts:
        cmd += ['--save-blend', opts['save-blend']]
    # Blender 4.0's ray tracer (Embree) now and then crashes building its scene: tried again.
    for _ in range(3):
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode == 0:
            break
    if r.returncode != 0 or not os.path.exists(os.path.join(work, 'meta.json')):
        print(r.stdout[-4000:], file=sys.stderr)
        print('make_room: Blender failed', file=sys.stderr)
        return 1
    meta = pixelize.load(work)
    rgba, nmap, material = pixelize.pixelize(meta, work, 0)
    if (rgba[..., 3] == 0).any():
        print('make_room: %d pixels show nothing (the room needs a back wall everywhere)'
              % int((rgba[..., 3] == 0).sum()), file=sys.stderr)
    # What gives light: black normals (lighting.md), drawn as it is in a lit room.
    for m in meta['settings']['materials']:
        if m.get('emissive'):
            nmap[material == m['index']] = (0, 0, 0, 255)
    colours = {tuple(c) for c in rgba[rgba[..., 3] > 0][:, :3]}
    tiles, bg = cut(rgba, nmap)
    cols = 16
    rows = (len(tiles) + cols - 1) // cols
    img = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg = np.zeros((rows * T, cols * T, 4), np.uint8)
    nimg[...] = (128, 128, 255, 0)
    for i, (pic, nm) in enumerate(tiles):
        y, x = (i // cols) * T, (i % cols) * T
        img[y:y + T, x:x + T] = pic
        nimg[y:y + T, x:x + T] = nm
    os.makedirs(out, exist_ok=True)
    name = room['name']
    Image.fromarray(img, 'RGBA').save(os.path.join(out, name + '_tiles.png'))
    Image.fromarray(nimg, 'RGBA').save(os.path.join(out, name + '_tiles_n.png'))
    write_room(os.path.join(out, name + '.tmx'), room, tiles, bg, cols, rows)
    print('make_room: %s: %d tiles of 16 x 16, %d colours' % (name, len(tiles), len(colours)))
    if 'preview' in opts:
        Image.fromarray(rgba, 'RGBA').resize((w * 3, h * 3), Image.NEAREST).save(opts['preview'])
    if 'keep' not in opts:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
