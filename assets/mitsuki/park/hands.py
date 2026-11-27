# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""The close-up of Mitsuki's and Hina's hands (the park's scene, park.ms): they sit close on the
bench, Mitsuki holding Hina's hand in Hina's lap. Seen from Hina's side, near: Hina's right hand
resting on her teal skirt over her thigh, out of her open yellow sleeve, small and round, short
soft fingers, bare nails a little pinker than her skin; Mitsuki's left hand over it, reaching
from her navy coat's sleeve with its white cuff (her thighs beside Hina's: no bench between
them): slender, long fingers with neat red nails wrapped round the far edge of Hina's hand,
her thumb down its near side and under it, the lower half of her palm showing below the cuff.
In warm colours, as the lamp shows them (the scene's warm moment).

Both figures are their models (assets/mitsuki/home/hina.py, assets/mitsuki/mitsuki.py: her
build run on a stand-in rig that renames her joints m_...), seated and scaled up (SCALE); their
arms posed by search (forward kinematics, the nearest of a grid) so the wrists reach the hands.
The hands are detailed: built as a procedural shader draws them (signed distance fields): each
finger three capsules tapering to the tip, the palm a few capsules from the wrist to the
knuckles, a pad under it and one over its back (smooth over the bones), all blended smoothly
(the smooth minimum of the fields); a nail a flat ellipsoid on the last joint. Here that is a
metaball per capsule (metaballs blend as the smooth minimum does), turned into a mesh and
skinned to an armature of the same bones (each vertex weighted to its nearest two by
distance), which is then posed: every joint bent its own amount, the fingers fanned a little,
the thumb bent and turned round (thumb_wrap), so the hands rest rather than lie flat. Hina's
lies on her lap at its measured height (rays down onto her skirt and thigh). Each hand is a
material of its own in the same colours, `apart`: a line where one lies on the other however
close they are. Only this close-up has them; the sprites' hands are the figures' simple shapes.

One frame, 96 x 48 (the park's sprite memory), on their hands (the scene moved so they are at
the anchor, scaled by ZOOM); every material `emissive`: drawn as it is in the lit park (a
cut-in, not a thing in the light).

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/park/hands.py \\
      --layout assets/mitsuki/park/hands_layout.bmsprite --out assets/mitsuki/park --name hands --clip yes

The HANDS_* environment variables override the numbers below (to try a pose). HANDS_DEBUG:
flat renders the hands unposed, side from the side, nomitsuki without Mitsuki's arm, nohina
without Hina's hand, skins Hina's hand green; x writes the searches to HANDS_LOG.
"""

import math

# Their colours as the lamp over the bench shows them: warm, the shadows reddish (the close-up
# is the scene's warm moment; their models' colours warmed).
OUT = (40, 22, 28)
SKIN = [(204, 120, 98), (238, 170, 138), (255, 226, 196)]
SKIRT = [(44, 70, 60), (82, 112, 88)]         # hina.py's teal, warmed
SHOE = [(170, 40, 50)]
NAIL_HINA = [(222, 150, 152)]                 # bare nails, a little darker and pinker than her skin
ZOOM = float(__import__("os").environ.get("HANDS_ZOOM", "1.5"))     # the close-up's nearness
# Wrapped around the far edge of Hina's hand, a little loose.
MITSUKI_CURL = ((12, 30, 22), (10, 32, 24), (12, 30, 22), (14, 28, 20))
MITSUKI_THUMB = (24, 22, 14)
_w = float(__import__("os").environ.get("HANDS_WRAP", "40"))
MITSUKI_WRAP = (_w * 0.5, _w, _w * 0.8)   # ... and round under Hina's hand
NAIL = [(150, 20, 44), (226, 52, 72)]
COAT = [(46, 34, 60), (84, 64, 100)]          # her navy, warmed
CUFF = [(255, 240, 220)]
HOODIE = [(190, 116, 30), (250, 200, 80)]

# A hand, in its own space: the wrist at the origin, the fingers along +x, the back of the hand
# up (+z), `thumb` the side (y) the thumb is on. Per finger: where its knuckle is across the
# palm (y), how far back from the palm's end, its three bones' lengths and its radius at the
# knuckle and at the tip; the thumb's three bones from its root at the wrist's side.
MITSUKI = {
    'palm': 14.5, 'width': 6.6, 'thick': 2.1, 'fingers': [
        # y, back, lengths, r0, r1
        (-4.5, 0.8, (9.4, 6.2, 4.8), 1.45, 1.0),
        (-1.5, 0.0, (10.4, 6.8, 5.2), 1.5, 1.04),
        (1.5, 0.4, (9.8, 6.4, 5.0), 1.45, 1.0),
        (4.4, 1.8, (7.6, 5.0, 4.2), 1.3, 0.92),
    ],
    'thumb': ((3.0, -5.0, -0.8), (6.8, 5.2, 4.2), 2.1, 1.35),
    # Her thumb goes down and in at the root: under the hand she holds.
    'thumb_dir': (0.66, 0.5, -0.45),
    'nails': 'nail', 'skin': 'skin_mitsuki',
}
HINA = {
    'palm': 12.6, 'width': 6.2, 'thick': 2.5, 'fingers': [
        (-5.1, 0.6, (6.6, 4.4, 3.6), 1.6, 1.4),
        (-1.7, 0.0, (7.2, 4.8, 3.8), 1.65, 1.45),
        (1.7, 0.4, (6.8, 4.5, 3.6), 1.6, 1.4),
        (5.0, 1.4, (5.6, 3.8, 3.2), 1.45, 1.3),
    ],
    'thumb': ((2.6, -5.4, -0.9), (5.6, 4.4, 3.6), 2.3, 1.75),
    'nails': 'nail_hina', 'skin': 'skin_hina',
}


def build(Rig):
    import os
    import sys
    import bpy
    from mathutils import Vector
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pair
    debug = os.environ.get('HANDS_DEBUG', '')
    r = Rig('hands')
    pitch = float(os.environ.get('HANDS_PITCH', '40'))
    # (From Hina's side, looking down at her lap: yaw in pair.py's scene, where they face +x.)
    r.view = {'yaw': float(os.environ.get('HANDS_YAW', '120')), 'pitch': pitch if debug != 'side' else 0.0,
              'light': (-0.35, 0.6, 0.72), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 1.6 * ZOOM * 1.6   # (in units of depth: the scene is scaled by ZOOM; only real overlaps)
    g = dict(emissive=True)
    r.material('skin', SKIN, bands=[0.25, 0.62], **g)
    # Each hand a material of its own in the same colours, `apart`: a line where one lies on
    # the other, however close (the depth alone would not split them).
    r.material('skin_mitsuki', SKIN, bands=[0.25, 0.62], apart=True, **g)
    r.material('skin_hina', [(60, 200, 60), (90, 230, 90), (150, 255, 150)] if debug == 'skins' else SKIN, bands=[0.25, 0.62],
               apart=True, **g)
    r.material('nail', NAIL, weight=5, min_cover=0.3, bands=[0.6], keep_inner=True, **g)
    r.material('nail_hina', NAIL_HINA, weight=5, min_cover=0.3, bands=[], keep_inner=True, **g)
    r.material('coat', COAT, bands=[0.5], **g)
    r.material('cuff', CUFF, bands=[], **g)
    r.material('ground', [OUT], bands=[], **g)   # what shows between them: shadow
    # Hina's own (her model's colours, drawn as they are here).
    r.material('hoodie', HOODIE, bands=[0.5], **g)
    r.material('skirt', SKIRT, bands=[0.5], **g)
    r.material('sock', CUFF, bands=[], **g)
    r.material('shoe', SHOE, bands=[], **g)
    r.joint('bench', None, (0, 0, 0))
    # The bench's dark wood under and around them.
    r.box('bench', 'ground', (0, 0, -60), (4000, 4000, 2))

    # The two of them on the bench (pair.py: the scene their sprites are drawn from too): Hina
    # without her head (out of the picture) and her right hand (drawn in detail); Mitsuki in
    # the close-up's colours, without her head and hair and her left hand.
    hina = pair.load('../home/hina.py', 'hina')
    hina.model(r, head=False, right_hand=False)
    pair.add_mitsuki(r, 'm_', material=lambda name, ramp, k: MITSUKI_MATERIALS.get(name),
                     group=lambda grp: 'body' if grp is None else None, skip=('head', 'hair', 'hair_end', 'hand_L'))
    solved = pair.solve(r, lambda n: 'm_' + n, lambda n: n, log=os.environ.get('HANDS_LOG') if debug else None)
    sit = solved['joints']
    wrist, turn, tilt = solved['hina_hand']
    at, mturn = solved['mitsuki_hand']
    middle = solved['middle']

    # Hina's right hand, out of her open sleeve, resting in her lap (lifted a little: Mitsuki
    # holds it), its thumb toward Mitsuki's side, curled a little; natural nails.
    if debug != 'nohina':
        hand(r, 'hina', HINA, thumb=1, at=tuple(wrist), turn=turn, tilt=tilt,
             curl=((6, 12, 6), (4, 10, 6), (6, 12, 8), (8, 14, 8)), fan=(6, 2, -2, -7),
             thumb_pose=(10, 18, 12))
    # Mitsuki's left hand over it, crossing it: her long fingers wrapped round its far edge, the
    # nails red, her thumb down its near side and under it.
    if debug != 'nomitsuki':
        hand(r, 'mitsuki', MITSUKI, thumb=-1, at=tuple(at), turn=mturn,
             curl=MITSUKI_CURL, fan=(-5, -1, 3, 8),
             thumb_pose=MITSUKI_THUMB, thumb_wrap=MITSUKI_WRAP, tilt=-6.0)
    # The frame on their hands: everything moved so the middle of Hina's hand, a little toward
    # Mitsuki's, is at the origin (the frame's anchor, its middle), and scaled up about there
    # (the frame's scale is fixed: a pixel a unit).
    focus = middle + (Vector(at) - middle) * 0.25
    focus.z = middle.z - 10.0   # (the hands a little above the frame's middle, as before)
    for name in [n for n in ('pelvis', 'm_pelvis', 'bones_hina', 'bones_mitsuki') if n in bpy.data.objects]:
        ob = bpy.data.objects[name]
        ob.location = (ob.location - focus) * ZOOM
        ob.scale = tuple(v * ZOOM for v in ob.scale)
    r.pose(joints=sit, show=('body', 'lap'))
    return r


# Mitsuki's model's materials, as drawn here (her tights and skirt are her coat's two darker
# tones, as in her model); parts of others (her head, her hair, her things) are left out.
MITSUKI_MATERIALS = {'skin': 'skin', 'coat': 'coat', 'skirt': 'coat', 'tights': 'coat', 'boot': 'coat', 'trim': 'cuff',
                     }   # her scarf left out: leaning in, it would hang over the shot


def hand(r, name, spec, thumb, at, turn, curl, fan, thumb_pose, tilt=0.0, thumb_wrap=(0, 0, 0)):
    """Builds a skinned hand (see the top): metaball capsules turned into a mesh, an armature of
    its bones, the mesh weighted to them, posed (`curl`: per finger its three joints' bend in
    degrees, `fan`: its spread; `thumb_pose` the thumb's three), placed at `at`, turned `turn`
    degrees about z and tilted `tilt` about its own across axis."""
    import bpy
    from mathutils import Euler, Matrix, Vector

    skin = r.materials[spec['skin']][0]
    nail = r.materials[spec['nails']][0] if spec['nails'] else None
    palm, width, thick = spec['palm'], spec['width'], spec['thick']

    # The bones, at rest: the palm (wrist to the middle knuckle), each finger's three, the thumb's.
    bones = []   # (name, head, tail, parent, r0, r1)
    bones.append(('palm', Vector((0, 0, 0)), Vector((palm, 0, 0)), None, width * 0.5, width * 0.5))
    for i, (fy, back, lengths, r0, r1) in enumerate(spec['fingers']):
        y = fy * (1 if thumb < 0 else -1)
        p = Vector((palm - back, y, 0))
        parent = 'palm'
        for j, length in enumerate(lengths):
            q = p + Vector((length, 0, 0))
            ra = r0 + (r1 - r0) * (j / 3.0)
            rb = r0 + (r1 - r0) * ((j + 1) / 3.0)
            bones.append(('f%d_%d' % (i, j), p.copy(), q.copy(), parent, ra, rb))
            parent = 'f%d_%d' % (i, j)
            p = q
    (tx, ty, tz), tlengths, tr0, tr1 = spec['thumb']
    m = 1 if thumb < 0 else -1   # the specs are of a hand with its thumb toward -y
    p = Vector((tx, ty * m, tz))
    direction = Vector(spec.get('thumb_dir', (0.9, 0.42, -0.25)))
    direction = Vector((direction.x, direction.y * thumb, direction.z)).normalized()
    parent = 'palm'
    for j, length in enumerate(tlengths):
        q = p + direction * length
        ra = tr0 + (tr1 - tr0) * (j / 3.0)
        rb = tr0 + (tr1 - tr0) * ((j + 1) / 3.0)
        bones.append(('t_%d' % j, p.copy(), q.copy(), parent, ra, rb))
        parent = 't_%d' % j
        p = q
        direction = (direction + Vector((0.35, 0, 0))).normalized()

    # The field: a capsule per bone (the palm a few side by side, flattened by a pad), blended.
    mb = bpy.data.metaballs.new('mb_' + name)
    mb.resolution = 0.22
    mb.render_resolution = 0.22
    mb.threshold = 0.6
    k = 1.55   # a metaball's radius over the surface's (for this threshold)

    def capsule(a, b, rad, stiff=2.0):
        e = mb.elements.new(type='CAPSULE')
        d = b - a
        e.co = (a + b) / 2
        e.size_x = d.length / 2
        e.radius = rad * k
        e.rotation = Vector((1, 0, 0)).rotation_difference(d.normalized())
        e.stiffness = stiff

    for i, (fy, back, lengths, r0, r1) in enumerate(spec['fingers']):
        y = fy * (1 if thumb < 0 else -1)
        capsule(Vector((1.5, y * 0.7, 0)), Vector((palm - back, y, 0)), thick * 0.95)
    pad = mb.elements.new(type='ELLIPSOID')
    pad.co = (palm * 0.5, 0, -0.3)
    pad.size_x, pad.size_y, pad.size_z = palm * 0.34, width * 0.62, thick * 0.4
    pad.radius = thick * k * 0.8
    # The back of the hand smooth over the bones (no ridges between them).
    back = mb.elements.new(type='ELLIPSOID')
    back.co = (palm * 0.55, 0, thick * 0.25)
    back.size_x, back.size_y, back.size_z = palm * 0.32, width * 0.55, thick * 0.3
    back.radius = thick * k * 0.75
    back.stiffness = 2.0
    pad.stiffness = 2.0
    capsule(Vector((-6, 0, 0)), Vector((1.5, 0, 0)), thick * 1.25)   # the wrist
    # The fingers stiffer than the palm: they meet it smoothly but stay apart from each other.
    for bn, a, b, parent, ra, rb in bones[1:]:
        capsule(a, b, (ra + rb) / 2, stiff=4.0)
    ob = bpy.data.objects.new('mb_' + name, mb)
    r.scene.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    bpy.data.metaballs.remove(mb)
    me.name = 'hand_' + name
    me.materials.append(skin)
    me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
    hand_ob = bpy.data.objects.new('hand_' + name, me)
    r.scene.collection.objects.link(hand_ob)

    # The armature of the same bones.
    arm = bpy.data.armatures.new('bones_' + name)
    arm_ob = bpy.data.objects.new('bones_' + name, arm)
    r.scene.collection.objects.link(arm_ob)
    bpy.context.view_layer.objects.active = arm_ob
    bpy.ops.object.mode_set(mode='EDIT')
    for bn, a, b, parent, ra, rb in bones:
        eb = arm.edit_bones.new(bn)
        eb.head, eb.tail = a, b
        eb.roll = 0.0
        if parent:
            eb.parent = arm.edit_bones[parent]
            eb.use_connect = (eb.head - arm.edit_bones[parent].tail).length < 1e-4
    bpy.ops.object.mode_set(mode='OBJECT')

    # Skinning: each vertex to its nearest two bones (by distance to the bone's segment), the
    # nearer more.
    def seg_dist(p, a, b):
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
        return (a + ab * t - p).length

    groups = {bn: hand_ob.vertex_groups.new(name=bn) for bn, *_ in bones}
    for v in me.vertices:
        ds = sorted((seg_dist(v.co, a, b) - (ra + rb) / 2, bn) for bn, a, b, parent, ra, rb in bones)
        (d0, b0), (d1, b1) = ds[0], ds[1]
        w0, w1 = 1.0 / (max(d0, 0.05) ** 4), 1.0 / (max(d1, 0.05) ** 4)
        s = w0 + w1
        groups[b0].add([v.index], w0 / s, 'REPLACE')
        groups[b1].add([v.index], w1 / s, 'REPLACE')
    mod = hand_ob.modifiers.new('bones', 'ARMATURE')
    mod.object = arm_ob
    hand_ob.parent = arm_ob

    # The nails: on the back of each fingertip, following its last bone.
    if spec['nails']:
        for i, (fy, back, lengths, r0, r1) in enumerate(spec['fingers']):
            bm_name = 'f%d_2' % i
            nm = bpy.data.meshes.new('nail_%s_%d' % (name, i))
            import bmesh
            bm = bmesh.new()
            bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=1.0,
                                      matrix=Matrix.Diagonal((r1 * 0.85, lengths[2] * 0.42, r1 * 0.38, 1.0)))
            bm.to_mesh(nm)
            bm.free()
            nm.materials.append(nail)
            nm.polygons.foreach_set('use_smooth', [True] * len(nm.polygons))
            nob = bpy.data.objects.new('nail_%s_%d' % (name, i), nm)
            r.scene.collection.objects.link(nob)
            nob.parent = arm_ob
            nob.parent_type = 'BONE'
            nob.parent_bone = bm_name
            # In the bone's space: y along it from its tail; the nail toward the tip, on the back.
            nob.location = (0, -lengths[2] * 0.42, r1 * 0.78)

    # The pose: each joint bent toward the palm, the fingers fanned, the thumb in.
    import os
    if os.environ.get('HANDS_DEBUG') == 'flat':
        curl = [(0, 0, 0)] * 4
        fan = (0, 0, 0, 0)
        thumb_pose = (0, 0, 0)
    for i in range(len(spec['fingers'])):
        for j in range(3):
            pb = arm_ob.pose.bones['f%d_%d' % (i, j)]
            pb.rotation_mode = 'XYZ'
            pb.rotation_euler = Euler((-math.radians(curl[i][j]), 0, math.radians(fan[i] if j == 0 else 0)))
    # The thumb: bent toward the palm (`thumb_pose`), and sideways (`thumb_wrap`: across, round
    # under what the hand holds).
    for j in range(3):
        pb = arm_ob.pose.bones['t_%d' % j]
        pb.rotation_mode = 'XYZ'
        pb.rotation_euler = Euler((-math.radians(thumb_pose[j]), 0, math.radians(thumb_wrap[j])))
    arm_ob.location = at
    arm_ob.rotation_euler = Euler((0, math.radians(tilt), math.radians(turn)))
