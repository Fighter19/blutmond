#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""3D sprites (docs/plans/3d-sprites.md): renders a character modelled in Blender into a sprite
of the engine.

usage: make_sprite.py <character.py | scene.blend> --layout <sprite.bmsprite> --out <dir>
                      --name <name> [--sink N] [--split WAIST] [--figure <figure name>]
                      [--save-blend <file.blend>] [--preview <file.png>] [--blender <path>]
                      [--keep <dir>]

<character.py> builds the character in Blender (its build(Rig), tools/sprite3d/rig.py); a
.blend (made by --save-blend) is rendered as it is. --layout is a sprite whose frames (their
rectangles, anchors, boxes, points), animations and palettes the new one takes: frame i of the
layout is pose i of the character, rendered with the ground on the row --sink below the anchor
(the layout's sink: the picture drawn that much lower than the anchor). Boxes of kind `hit`
whose name is a material of the character (the blade) are put around that material's pixels;
points whose name is a locator (the hand) where it lies.

Writes <out>/sources/<name>.png, <name>_n.png (the normal map) and <name>.bmsprite; with
--split, legs and torso (<name>_legs, <name>_torso, tools/split_sprite.py, cut WAIST pixels
above the anchor) in <out>; with --figure, <out>/<figure name>.bmfigure of the two (named
`player`, it is the engine's player). A character with a portrait gets <out>/<name>_face.png
and .bmsprite (the dialogue box's, its anchor at the top left). --preview writes a sheet of
every frame four times larger on a grey ground; --keep leaves the render's passes in a folder.
Needs Blender (4.0 or later; --blender or $BLENDER, else `blender` on the path), numpy and PIL.
"""
import copy
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
sys.path.insert(0, HERE)
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


def layout_frames(root):
    frames = []
    for f in root.findall('frame'):
        frames.append({k: int(f.get(k)) for k in ('x', 'y', 'w', 'h', 'ax', 'ay')})
    return frames


def write_xml(root, path):
    ET.indent(root, space=' ')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        fh.write(ET.tostring(root, encoding='unicode'))
        fh.write('\n')


def main(argv):
    rest, opts = parse(argv[1:])
    if len(rest) != 1 or not all(k in opts for k in ('layout', 'out', 'name')):
        print(__doc__, file=sys.stderr)
        return 2
    src = rest[0]
    name, out = opts['name'], opts['out']
    sink = int(opts.get('sink', 0))
    blender = opts.get('blender') or os.environ.get('BLENDER') or shutil.which('blender')
    if not blender:
        print('make_sprite: Blender not found (--blender or $BLENDER)', file=sys.stderr)
        return 1
    tree = ET.parse(opts['layout'])
    lroot = tree.getroot()
    frames = layout_frames(lroot)
    jobs = [{'frame': i, 'w': f['w'], 'h': f['h'], 'ax': f['ax'], 'ay': f['ay'], 'ground': f['ay'] + sink}
            for i, f in enumerate(frames)]

    work = opts.get('keep') or tempfile.mkdtemp(prefix='sprite3d_')
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, 'jobs.json'), 'w') as fh:
        json.dump(jobs, fh)
    cmd = [blender, '-b']
    if src.endswith('.blend'):
        cmd += [src]
    else:
        cmd += ['--factory-startup']
    cmd += ['-P', os.path.join(HERE, 'render.py'), '--']
    if not src.endswith('.blend'):
        cmd += ['--character', src]
    cmd += ['--jobs', os.path.join(work, 'jobs.json'), '--out', work]
    if 'save-blend' in opts:
        cmd += ['--save-blend', opts['save-blend']]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(work, 'meta.json')):
        print(r.stdout[-4000:], file=sys.stderr)
        print('make_sprite: Blender failed', file=sys.stderr)
        return 1
    meta = pixelize.load(work)
    settings = meta['settings']
    if settings['frames'] < len(frames):
        print('make_sprite: the character has %d poses, the layout %d frames' % (settings['frames'], len(frames)),
              file=sys.stderr)
        return 1
    mats = {m['name']: m['index'] for m in settings['materials']}

    # The sheet: the layout's size, each frame where the layout has it.
    lsheet = Image.open(os.path.join(os.path.dirname(opts['layout']), lroot.get('image')))
    sheet = np.zeros((lsheet.height, lsheet.width, 4), np.uint8)
    nsheet = np.zeros_like(sheet)
    nsheet[..., :3] = (128, 128, 255)
    root = copy.deepcopy(lroot)
    root.set('image', name + '.png')
    root.set('normal', name + '_n.png')
    for k in ('emissive', 'shiny'):
        root.attrib.pop(k, None)
    used = set()
    for i, (f, fel) in enumerate(zip(frames, root.findall('frame'))):
        rgba, nmap, material = pixelize.pixelize(meta, work, i)
        sheet[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']] = rgba
        nsheet[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']] = nmap
        used |= {tuple(c) for c in rgba[rgba[..., 3] > 0][:, :3]}
        ground = f['ay'] + sink
        for box in fel.findall('box'):
            if box.get('kind') == 'hit' and box.get('name') in mats:
                b = pixelize.bounds(material, mats[box.get('name')])
                if b is None:
                    fel.remove(box)
                    continue
                x0, y0, x1, y1 = b
                box.set('x', str(x0 - 1 - f['ax']))
                box.set('y', str(y0 - 1 - ground))
                box.set('w', str(x1 - x0 + 3))
                box.set('h', str(y1 - y0 + 3))
        for pt in fel.findall('point'):
            loc = meta['jobs'][i]['locators'].get(pt.get('name'))
            if loc is not None:
                pt.set('x', str(int(round(loc[0] - 0.5)) - f['ax']))
                pt.set('y', str(int(round(loc[1] - 0.5)) - f['ay']))
    if len(used) > 15:
        print('make_sprite: %d colours, a sprite has 15' % len(used), file=sys.stderr)
        return 1
    # The character's palettes instead of the layout's.
    for p in root.findall('palette'):
        root.remove(p)
    at = list(root).index(root.findall('frame')[-1]) + 1
    for pname, swaps in settings.get('palettes', []):
        p = ET.Element('palette', {'name': pname})
        for a, b in swaps:
            ET.SubElement(p, 'swap', {'from': a, 'to': b})
        root.insert(at, p)
        at += 1
    sources = os.path.join(out, 'sources')
    os.makedirs(sources, exist_ok=True)
    Image.fromarray(sheet).save(os.path.join(sources, name + '.png'))
    Image.fromarray(nsheet).save(os.path.join(sources, name + '_n.png'))
    write_xml(root, os.path.join(sources, name + '.bmsprite'))
    print('make_sprite: %s: %d frames, %d colours' % (name, len(frames), len(used)))

    portrait = [j for j in meta['jobs'] if j['job'].get('portrait')]
    if portrait:
        n = meta['jobs'].index(portrait[0])
        rgba, nmap, _ = pixelize.pixelize(meta, work, n)
        Image.fromarray(rgba).save(os.path.join(out, name + '_face.png'))
        pw, ph = portrait[0]['job']['w'], portrait[0]['job']['h']
        face = ET.Element('sprite', {'version': '1', 'image': name + '_face.png'})
        ET.SubElement(face, 'hitbox', {'x': '0', 'y': '0', 'w': str(pw), 'h': str(ph)})
        ET.SubElement(face, 'frame', {'x': '0', 'y': '0', 'w': str(pw), 'h': str(ph), 'ax': '0', 'ay': '0'})
        a = ET.SubElement(face, 'animation', {'name': 'neutral', 'loop': '1'})
        ET.SubElement(a, 'step', {'frame': '0', 'duration': '60'})
        write_xml(face, os.path.join(out, name + '_face.bmsprite'))

    if 'split' in opts:
        r = subprocess.run([sys.executable, os.path.join(HERE, '..', 'split_sprite.py'),
                            os.path.join(sources, name + '.bmsprite'), out, name + '_legs', name + '_torso',
                            '--waist', opts['split']])
        if r.returncode != 0:
            return 1
        if 'figure' in opts:
            with open(os.path.join(out, opts['figure'] + '.bmfigure'), 'w') as fh:
                fh.write('# %s (layered-sprites.md §4): legs and torso, rendered from %s by\n'
                         '# tools/sprite3d/make_sprite.py (docs/plans/3d-sprites.md) and split at the waist.\n'
                         % (name, os.path.basename(src)))
                fh.write('layer legs   sprite=%s_legs\n' % name)
                fh.write('layer torso  sprite=%s_torso  at=legs.waist  mode=sync\n' % name)

    if 'preview' in opts:
        bg = Image.new('RGBA', (sheet.shape[1], sheet.shape[0]), (96, 104, 112, 255))
        bg.alpha_composite(Image.fromarray(sheet))
        bg.resize((bg.width * 4, bg.height * 4), Image.NEAREST).save(opts['preview'])
    if 'keep' not in opts:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
