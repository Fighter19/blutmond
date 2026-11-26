# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""3D sprites (docs/plans/3d-sprites.md), the Blender side: a character built from simple
shapes on a rig of joints, posed once per sprite frame, rendered for the pixel step.

Runs inside Blender (tools/sprite3d/render.py imports it); no numpy, so any Blender will do.

The world: x forward (a figure faces right), y away from the viewer (-y is the near side,
a figure's right), z up, one unit a pixel of the sprite. The ground is z = 0 and the point
(0, 0, 0) lands on a frame's anchor.

A **joint** is an empty; joints hang on joints, and in the rest pose none is turned, so parts
are given in world coordinates of the rest pose and follow their joint when it turns. A
**part** is a mesh (ellipsoid, tube, box) of one **material**; a material is a palette ramp
(dark to light) of the pixel step and has a pass index for the render. A **pose** turns
joints (swing: forward and back, in the picture's plane; spread: sideways; twist: about the
bone) and shows or hides groups of parts (a sword, a lance); poses are keyframes, timeline
frame i + 1 for sprite frame i, so the scene can be opened, looked at and edited in Blender
and rendered again as it is (render.py --blend). A **locator** is an empty whose place in
each frame is written out: the hand where something hangs (a point of the sprite).
"""
import json
import math

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector


def _hex(c):
    return '#%02x%02x%02x' % tuple(c)


class Rig:
    def __init__(self, name):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.scene = bpy.context.scene
        self.scene.name = name
        self.name = name
        self.joints = {}
        self.rest = {}
        self.materials = {}       # name -> (bpy material, settings)
        self.groups = {}          # group name -> [objects]
        self.locators = []
        self.poses = []
        self.view = {'yaw': 30.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
        self.outline = (24, 20, 36)
        self.inner_lines = 3.0
        self.palettes = []

    # --- Materials ------------------------------------------------------------------------

    def material(self, name, ramp, weight=1.0, min_cover=None, bands=None, keep_inner=False):
        """A material: `ramp` colours (r, g, b) from shade to light. `weight`: how strongly
        it claims a pixel it shares with others (an eye, a blade: more). `min_cover`: the
        share of a pixel it needs to make a pixel on its own (thin parts: less than half).
        `bands`: where the light's steps lie (0..1, one fewer than the ramp). `keep_inner`:
        its pixels never take an inner line (an eye)."""
        mat = bpy.data.materials.new(name)
        mat.pass_index = len(self.materials) + 1
        r, g, b = (c / 255.0 for c in ramp[len(ramp) // 2])
        mat.diffuse_color = (r, g, b, 1.0)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get('Principled BSDF')
        if bsdf is not None:
            bsdf.inputs['Base Color'].default_value = (r, g, b, 1.0)
            bsdf.inputs['Roughness'].default_value = 0.8
        if bands is None:
            bands = [(i + 1) / len(ramp) * 0.9 for i in range(len(ramp) - 1)]
        self.materials[name] = (mat, {
            'name': name, 'index': mat.pass_index, 'ramp': [_hex(c) for c in ramp],
            'weight': weight, 'min_cover': min_cover, 'bands': bands, 'keep_inner': keep_inner})
        return name

    # --- Joints and parts -----------------------------------------------------------------

    def joint(self, name, parent, at):
        ob = bpy.data.objects.new(name, None)
        ob.empty_display_type = 'SPHERE'
        ob.empty_display_size = 0.6
        self.scene.collection.objects.link(ob)
        at = Vector(at)
        if parent is not None:
            ob.parent = self.joints[parent]
            ob.location = at - self.rest[parent]
        else:
            ob.location = at
        ob.rotation_mode = 'XYZ'
        self.joints[name] = ob
        self.rest[name] = at
        return name

    def locator(self, name, joint, at):
        ob = bpy.data.objects.new('loc:' + name, None)
        ob.empty_display_type = 'PLAIN_AXES'
        ob.empty_display_size = 0.8
        self.scene.collection.objects.link(ob)
        ob.parent = self.joints[joint]
        ob.location = Vector(at) - self.rest[joint]
        self.locators.append(name)

    def _attach(self, name, bm, material, joint, group, smooth=True):
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        me.polygons.foreach_set('use_smooth', [smooth] * len(me.polygons))
        me.materials.append(self.materials[material][0])
        ob = bpy.data.objects.new(name, me)
        self.scene.collection.objects.link(ob)
        ob.parent = self.joints[joint]
        ob.location = -self.rest[joint]
        if group:
            self.groups.setdefault(group, []).append(ob)
        return ob

    @staticmethod
    def _cut(bm, cuts):
        """Takes away the side of each plane (point, normal) the normal points to."""
        for co, no in cuts or ():
            geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
            bmesh.ops.bisect_plane(bm, geom=geom, plane_co=Vector(co), plane_no=Vector(no).normalized(),
                                   clear_outer=True)

    def ellipsoid(self, joint, material, center, radii, rot=(0, 0, 0), cuts=None, group=None, segments=20):
        bm = bmesh.new()
        m = (Matrix.Translation(Vector(center)) @ Euler([math.radians(a) for a in rot]).to_matrix().to_4x4()
             @ Matrix.Diagonal((radii[0], radii[1], radii[2], 1.0)))
        bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=segments // 2 + 2, radius=1.0, matrix=m)
        self._cut(bm, cuts)
        return self._attach('%s.%s' % (joint, material), bm, material, joint, group)

    def tube(self, joint, material, p0, p1, r0, r1=None, squash=1.0, caps=True, cuts=None, group=None, segments=16):
        """A tapered tube from p0 (radius r0) to p1 (r1), rounded at both ends with `caps`;
        `squash` flattens it across the picture (along the bone's own x)."""
        r1 = r0 if r1 is None else r1
        p0, p1 = Vector(p0), Vector(p1)
        d = p1 - p0
        # The tube's own axes: z along it, x as near the world's x (forward) as it can be.
        z = d.normalized()
        ref = Vector((1, 0, 0)) if abs(z.x) < 0.9 else Vector((0, 0, 1))
        x = (ref - z * ref.dot(z)).normalized()
        rot = Matrix((x, z.cross(x), z)).transposed().to_4x4()
        m = Matrix.Translation((p0 + p1) / 2) @ rot @ Matrix.Diagonal((squash, 1.0, 1.0, 1.0))
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments, radius1=r0, radius2=r1,
                              depth=d.length, matrix=m)
        if caps:
            for p, r in ((p0, r0), (p1, r1)):
                cm = Matrix.Translation(p) @ rot @ Matrix.Diagonal((r * squash, r, r, 1.0))
                bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=segments // 2 + 2, radius=1.0, matrix=cm)
        self._cut(bm, cuts)
        return self._attach('%s.%s' % (joint, material), bm, material, joint, group)

    def box(self, joint, material, center, size, rot=(0, 0, 0), group=None):
        bm = bmesh.new()
        m = (Matrix.Translation(Vector(center)) @ Euler([math.radians(a) for a in rot]).to_matrix().to_4x4()
             @ Matrix.Diagonal((size[0], size[1], size[2], 1.0)))
        bmesh.ops.create_cube(bm, size=1.0, matrix=m)
        return self._attach('%s.%s' % (joint, material), bm, material, joint, group, smooth=False)

    # --- Poses ----------------------------------------------------------------------------

    def pose(self, joints=None, root=(0, 0, 0), show=()):
        """Adds the next frame's pose: `joints` name -> swing or (swing, spread, twist) in
        degrees (swing forward is positive: an arm hanging down swung by 90 points ahead);
        `root` moves the first joint; `show` names the groups of parts to show (the others
        are hidden)."""
        self.poses.append((dict(joints or {}), tuple(root), tuple(show)))

    def _key_poses(self):
        root = next(iter(self.joints))
        for i, (joints, offset, show) in enumerate(self.poses):
            f = i + 1
            for name, ob in self.joints.items():
                a = joints.get(name, 0)
                swing, spread, twist = (a, 0, 0) if isinstance(a, (int, float)) else (tuple(a) + (0, 0))[:3]
                # Swinging forward turns -z towards +x: about -y.
                ob.rotation_euler = (math.radians(spread), math.radians(-swing), math.radians(twist))
                ob.keyframe_insert('rotation_euler', frame=f)
                if name == root:
                    ob.location = self.rest[name] + Vector(offset)
                    ob.keyframe_insert('location', frame=f)
            for group, obs in self.groups.items():
                for ob in obs:
                    ob.hide_render = group not in show
                    ob.hide_viewport = ob.hide_render
                    ob.keyframe_insert('hide_render', frame=f)
                    ob.keyframe_insert('hide_viewport', frame=f)
        for ob in bpy.data.objects:
            ad = ob.animation_data
            if ad and ad.action:
                for fc in ad.action.fcurves:
                    for k in fc.keyframe_points:
                        k.interpolation = 'CONSTANT'
        self.scene.frame_start = 1
        self.scene.frame_end = len(self.poses)

    def portrait(self, pose, target, zoom, size=32, yaw=None, pitch=None, show=()):
        """A portrait for the dialogue box: pose number `pose` (one added with pose()), `size`
        pixels square, `zoom` pixels a unit, the world point `target` in its middle; `show`:
        groups of parts it shows besides its pose's (a far eye no sprite frame shows)."""
        self.portrait_job = {'pose': pose, 'target': list(target), 'zoom': zoom, 'size': size,
                             'show': list(show),
                             'yaw': self.view['yaw'] if yaw is None else yaw,
                             'pitch': self.view['pitch'] if pitch is None else pitch}

    def palette(self, name, swaps):
        """Another palette of the sprite (sprite-format.md): colour swaps (from, to)."""
        self.palettes.append((name, [(_hex(a), _hex(b)) for a, b in swaps]))

    def finish(self):
        """Keys the poses and stores what the pixel step needs in the scene (so a saved .blend
        carries it)."""
        self._key_poses()
        self.scene['sprite3d'] = json.dumps({
            'name': self.name,
            'view': self.view,
            'outline': _hex(self.outline),
            'materials': [s for _, s in self.materials.values()],
            'locators': self.locators,
            'palettes': self.palettes,
            'frames': len(self.poses),
            'portrait': getattr(self, 'portrait_job', None),
            'groups': {g: [ob.name for ob in obs] for g, obs in self.groups.items()},
            'inner_lines': self.inner_lines,
        })
