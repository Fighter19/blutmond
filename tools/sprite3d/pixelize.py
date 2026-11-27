# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""3D sprites (docs/plans/3d-sprites.md), the pixel step: from the passes render.py wrote, a
picture of the sprite's own pixels in the character's palette, and its normal map.

Each pixel of the sprite is `supersample` x `supersample` pixels of the render. It is drawn
when half of them hit the figure, or when a thin material (`min_cover`: a blade, an eye) has
its share of them; its material is the one with the most of them, each counted `weight`
times. Its colour is a step of that material's ramp: the light (the character's direction, in
the camera's space: x right, y up, z toward the viewer) on the mean normal of its part of the
pixel, cut at the material's `bands`. Around the figure goes a line in the outline colour (the
pixels next to it, left, right, above and below); where a part lies well in front of another
(`inner_lines`: units of depth), the farther one's edge pixel takes the line too, as it does
where two materials `apart` meet however near they lie. The normal
map is the mean normal (x right, y down, z toward the viewer, as lighting.md wants), the
line's pointing out of the figure.
"""
import json
import os

import numpy as np


def hex_rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def load(render_dir):
    meta = json.load(open(os.path.join(render_dir, 'meta.json')))
    return meta


def passes(render_dir, n, size):
    w, h = size
    out = {}
    for slot in ('idx', 'nrm', 'dep'):
        a = np.fromfile(os.path.join(render_dir, '%d_%s.bin' % (n, slot)), dtype=np.float32)
        out[slot] = a.reshape(h, w, 4)[::-1]
    return out


def pixelize(meta, render_dir, n):
    """Returns (rgba (h, w, 4) uint8, normals (h, w, 4) uint8, material (h, w): 0 none, -1 the
    line, else a material's index) for job n."""
    settings = meta['settings']
    view = settings['view']
    ss = int(view.get('supersample', 8))
    job = meta['jobs'][n]
    w, h = job['job']['w'], job['job']['h']
    p = passes(render_dir, n, job['size'])
    cam = job['camera']
    right, up, back = (np.array(cam[k], dtype=np.float64) for k in ('right', 'up', 'back'))
    idx = np.rint(p['idx'][..., 0]).astype(np.int32)
    nw = p['nrm'][..., :3].astype(np.float64)
    ncam = np.stack([nw @ right, nw @ up, nw @ back], axis=-1)      # x right, y up, z to the viewer
    dep = p['dep'][..., 0].astype(np.float64)

    def blocks(a):
        # (h*ss, w*ss, ...) -> (h, w, ss*ss, ...)
        rest = a.shape[2:]
        a = a.reshape((h, ss, w, ss) + rest).swapaxes(1, 2)
        return a.reshape((h, w, ss * ss) + rest)

    bi, bn, bd = blocks(idx), blocks(ncam), blocks(dep)
    mats = {m['index']: m for m in settings['materials']}
    total = ss * ss
    covered = (bi > 0).sum(-1)
    opaque = covered >= total * 0.5
    score = np.zeros((h, w), np.float64)
    chosen = np.zeros((h, w), np.int32)
    for i, m in mats.items():
        cnt = (bi == i).sum(-1)
        if m.get('min_cover'):
            opaque |= cnt >= total * m['min_cover']
        s = cnt * m.get('weight', 1.0)
        better = s > score
        score[better] = s[better]
        chosen[better] = i
    chosen[~opaque] = 0

    light = np.array(view['light'], np.float64)
    light /= np.linalg.norm(light)
    rgba = np.zeros((h, w, 4), np.uint8)
    nmap = np.zeros((h, w, 4), np.uint8)
    nmap[..., :3] = (128, 128, 255)
    normal = np.zeros((h, w, 3))
    depth = np.full((h, w), np.inf)
    for i, m in mats.items():
        sel = chosen == i
        if not sel.any():
            continue
        mine = (bi == i)[..., None]
        nsum = (bn * mine).sum(2)
        nn = nsum / np.maximum(np.linalg.norm(nsum, axis=-1, keepdims=True), 1e-6)
        lam = np.clip(nn @ light, 0.0, 1.0)
        band = np.searchsorted(np.array(m['bands']), lam)
        ramp = np.array([hex_rgb(c) for c in m['ramp']], np.uint8)
        rgba[sel, :3] = ramp[np.clip(band, 0, len(ramp) - 1)][sel]
        rgba[sel, 3] = 255
        normal[sel] = nn[sel]
        d = np.where(bi == i, bd, np.inf).min(-1)
        depth[sel] = d[sel]
    material = chosen.copy()

    outline = np.array(hex_rgb(settings['outline']), np.uint8)
    # Inner lines: an edge pixel of a part lying well behind its neighbour.
    gap = settings.get('inner_lines')
    if gap:
        far = np.zeros((h, w), bool)
        apart = np.zeros(max(list(mats.keys()) + [0]) + 2, bool)
        for i, m in mats.items():
            apart[i] = bool(m.get('apart', False))
        # (Diagonal neighbours too for `apart`: its line unbroken where the edge runs slant.)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nb = np.roll(depth, (dy, dx), (0, 1))
            nbm = np.roll(material, (dy, dx), (0, 1))
            with np.errstate(invalid='ignore'):
                edge = np.zeros((h, w), bool)
                if dy == 0 or dx == 0:
                    edge = (material > 0) & (nbm > 0) & (depth - nb > gap)
                # Two materials `apart`: the farther takes the line where they meet, however near.
                edge |= (material > 0) & (nbm > 0) & (material != nbm) & apart[material] & apart[nbm] & (depth > nb)
            if dy == 1: edge[0, :] = False
            if dy == -1: edge[-1, :] = False
            if dx == 1: edge[:, 0] = False
            if dx == -1: edge[:, -1] = False
            far |= edge
        keep = np.array([[mats[c].get('keep_inner', False) if c > 0 else False for c in row] for row in material])
        far &= ~keep
        rgba[far, :3] = outline
        material[far] = -1
    # The line around the figure.
    inside = material != 0
    line = np.zeros((h, w), bool)
    out_dir = np.zeros((h, w, 2))
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        nb = np.roll(inside, (dy, dx), (0, 1))
        if dy == 1: nb[0, :] = False
        if dy == -1: nb[-1, :] = False
        if dx == 1: nb[:, 0] = False
        if dx == -1: nb[:, -1] = False
        here = nb & ~inside
        line |= here
        out_dir[here] += (-dx, -dy)      # away from the neighbour (x right, y down)
    rgba[line, :3] = outline
    rgba[line, 3] = 255
    material[line] = -1

    # The normal map: x right, y down, z toward the viewer.
    n_img = np.stack([normal[..., 0], -normal[..., 1], normal[..., 2]], -1)
    o = np.concatenate([out_dir, np.full((h, w, 1), 0.5)], -1)
    o /= np.linalg.norm(o, axis=-1, keepdims=True)
    n_img[line] = o[line]
    drawn = material != 0
    nmap[drawn, :3] = np.clip(np.rint(n_img[drawn] * 127.5 + 127.5), 0, 255).astype(np.uint8)
    nmap[drawn, 3] = 255
    return rgba, nmap, material


def bounds(material, index):
    ys, xs = np.nonzero(material == index)
    if len(xs) == 0:
        return None
    return int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())
