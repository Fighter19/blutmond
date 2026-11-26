# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""3D sprites (docs/plans/3d-sprites.md), the render step. Runs in Blender:

  blender -b --factory-startup -P tools/sprite3d/render.py -- --character <character.py>
          --jobs <jobs.json> --out <dir> [--save-blend <file.blend>]
  blender -b <file.blend> -P tools/sprite3d/render.py -- --jobs <jobs.json> --out <dir>

--character builds the scene with the script's build(Rig) (tools/sprite3d/rig.py); without it
the opened .blend is rendered as it is (made by --save-blend, perhaps edited since).

jobs.json lists what to render: {"frame": timeline frame - 1, "w", "h": the sprite frame's
size, "ax", "ay": its anchor, "ground": the row the ground lies on (ay plus the sink),
"zoom": pixels of the sprite per unit (1; a portrait more), "target": [x, y, z] the world
point placed at the anchor ([0, 0, 0])}. Each is rendered `supersample` times larger with
Cycles, one sample a pixel, and its passes written as Blender holds them (RGBA float32, rows
from the bottom): the material index (`<n>_idx.bin`), the normal in world space
(`<n>_nrm.bin`) and the depth (`<n>_dep.bin`); meta.json has the camera's axes. meta.json has the
character's settings (rig.py, finish) and, per job, the camera's axes and where its locators
landed (pixels of the frame, from its top left). A character with a portrait gets one job more
for it (`"portrait": true`).
"""
import array
import json
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True   # no __pycache__ beside the character's script


def args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = {}
    i = 0
    while i < len(argv):
        out[argv[i].lstrip('-').replace('-', '_')] = argv[i + 1]
        i += 2
    return out


def setup_render(scene, tmp):
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 1
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.pixel_filter_type = 'BOX'
    scene.cycles.filter_width = 0.01
    scene.cycles.max_bounces = 0
    scene.cycles.seed = 0
    scene.render.film_transparent = True
    scene.render.resolution_percentage = 100
    scene.render.use_compositing = True
    vl = scene.view_layers[0]
    vl.use_pass_normal = True
    vl.use_pass_material_index = True
    vl.use_pass_z = True
    scene.use_nodes = True
    tree = scene.node_tree
    for n in list(tree.nodes):
        tree.nodes.remove(n)
    rl = tree.nodes.new('CompositorNodeRLayers')
    comp = tree.nodes.new('CompositorNodeComposite')
    tree.links.new(rl.outputs['Image'], comp.inputs['Image'])
    fo = tree.nodes.new('CompositorNodeOutputFile')
    fo.base_path = tmp
    fo.format.file_format = 'OPEN_EXR'
    fo.format.color_depth = '32'
    fo.format.color_mode = 'RGB'
    fo.file_slots.clear()
    for slot, output in (('idx', 'IndexMA'), ('nrm', 'Normal'), ('dep', 'Depth')):
        fo.file_slots.new(slot + '_')
        tree.links.new(rl.outputs[output], fo.inputs[slot + '_'])
    # A plain light: the render's own colours are not used, only its passes.
    scene.world = scene.world or bpy.data.worlds.new('World')


def camera(scene, view):
    cam = bpy.data.objects.get('sprite3d_camera')
    if cam is None:
        data = bpy.data.cameras.new('sprite3d_camera')
        cam = bpy.data.objects.new('sprite3d_camera', data)
        scene.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.clip_start = 0.1
    cam.data.clip_end = 1000.0
    cam.data.sensor_fit = 'AUTO'
    cam.rotation_mode = 'XYZ'
    cam.rotation_euler = Euler((math.radians(90.0 - view['pitch']), 0.0, math.radians(view['yaw'])), 'XYZ')
    scene.camera = cam
    return cam


def read_pass(path, w, h):
    """The pass as Blender holds it: RGBA float32, rows from the bottom."""
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = 'Non-Color'
    buf = array.array('f', [0.0]) * (w * h * 4)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    return buf


def main():
    a = args()
    if 'character' in a:
        import importlib.util
        from rig import Rig
        spec = importlib.util.spec_from_file_location('character', a['character'])
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        rig = mod.build(Rig)
        rig.finish()
    scene = bpy.context.scene
    settings = json.loads(scene['sprite3d'])
    if 'save_blend' in a:
        bpy.context.preferences.filepaths.save_version = 0    # no .blend1 beside it
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a['save_blend']), compress=True)
    out = a['out']
    tmp = os.path.join(out, 'exr')
    os.makedirs(tmp, exist_ok=True)
    jobs = json.load(open(a['jobs']))
    view = settings['view']
    ss = int(view.get('supersample', 8))
    if settings.get('portrait'):
        # The portrait (rig.py, Rig.portrait): its own pose, place, zoom and angle.
        p = settings['portrait']
        jobs.append({'frame': p['pose'], 'w': p['size'], 'h': p['size'], 'ax': p['size'] // 2,
                     'ground': p['size'] // 2, 'target': p['target'], 'zoom': p['zoom'],
                     'yaw': p['yaw'], 'pitch': p['pitch'], 'portrait': True, 'show': p.get('show', [])})
    setup_render(scene, tmp)
    meta = {'settings': settings, 'jobs': []}
    for n, job in enumerate(jobs):
        cam = camera(scene, {'yaw': job.get('yaw', view['yaw']), 'pitch': job.get('pitch', view['pitch'])})
        m = cam.rotation_euler.to_matrix()
        right, up, back = m.col[0], m.col[1], m.col[2]
        w, h = job['w'], job['h']
        zoom = job.get('zoom', 1.0)
        scene.frame_set(job['frame'] + 1)
        # Groups of parts the job shows beyond its pose's (the portrait's far eye): their keyed
        # visibility muted for this job (the render would key it again), restored after.
        muted = []
        for g in job.get('show', []):
            for name in settings.get('groups', {}).get(g, []):
                ob = bpy.data.objects[name]
                for fc in (ob.animation_data.action.fcurves if ob.animation_data and ob.animation_data.action else []):
                    if fc.data_path == 'hide_render' and not fc.mute:
                        fc.mute = True
                        muted.append(fc)
                ob.hide_render = False
        bpy.context.view_layer.update()
        target = Vector(job.get('target', (0, 0, 0)))
        # The target lands on the anchor (ax, ground) of a w x h frame.
        dx = (job['ax'] - w / 2.0) / zoom
        dy = (h / 2.0 - job['ground']) / zoom
        cam.location = target - dx * right - dy * up + 200.0 * back
        cam.data.ortho_scale = max(w, h) / zoom
        scene.render.resolution_x = w * ss
        scene.render.resolution_y = h * ss
        bpy.context.view_layer.update()
        bpy.ops.render.render(write_still=False)
        for fc in muted:
            fc.mute = False
        f = scene.frame_current
        for slot in ('idx', 'nrm', 'dep'):
            src = os.path.join(tmp, '%s_%04d.exr' % (slot, f))
            data = read_pass(src, w * ss, h * ss)
            os.remove(src)
            with open(os.path.join(out, '%d_%s.bin' % (n, slot)), 'wb') as fh:
                data.tofile(fh)
        locs = {}
        for name in settings['locators']:
            p = bpy.data.objects['loc:' + name].matrix_world.translation
            sx, sy = (p - cam.location).dot(right) * zoom, (p - cam.location).dot(up) * zoom
            locs[name] = [w / 2.0 + sx, h / 2.0 - sy]
        meta['jobs'].append({'job': job, 'size': [w * ss, h * ss], 'locators': locs,
                             'camera': {'right': list(right), 'up': list(up), 'back': list(back)}})
        print('sprite3d: rendered job %d (frame %d)' % (n, job['frame']), flush=True)
    with open(os.path.join(out, 'meta.json'), 'w') as fh:
        json.dump(meta, fh, indent=1)


main()
