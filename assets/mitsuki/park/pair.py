# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki and Hina on the bench, holding hands: the one scene both the close-up of their hands
(hands.py) and their sprites on the bench (mitsuki.py's frames 55, 59, 60; hina.py's 9, 10, 14)
are drawn from, so they agree.

Both their models (assets/mitsuki/mitsuki.py, assets/mitsuki/home/hina.py) sit facing +x (as
their sprites are modelled; the front view looks at them from there), Mitsuki at the origin and
Hina at her left (+y, the screen's right), so close that their thighs touch (no bench between
them). Solved at the close-up's scale (SCALE: the units of its detailed hands), the sprites
shrink it after:

- Hina's right arm, by search (forward kinematics of her rig, the nearest of a grid): her wrist
  on her thigh, her forearm slanting toward Mitsuki's side; her hand (the close-up's) lies on
  her lap at the height measured there (rays down onto her skirt and thigh), lifted a little:
  Mitsuki holds it.
- Mitsuki, leaning in; her left arm, by search: her hand over Hina's, crossing it, her thumb
  under it, her cuff a little up her wrist.

The steps after (steps): Hina leaning in, her head in Mitsuki's lap; Mitsuki stroking her hair
(two frames).

Each character's file adds the other with `add_mitsuki` / `add_hina` (as a `holdout`, rig.py:
hiding what is behind it, drawn by its own sprite), then calls `solve` and, for the sprites,
`shrink`. `HINA_FACE`: their faces on the bench: Hina relaxed, Mitsuki turned toward her.
"""

import importlib.util
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 14.0          # the close-up's scale (its detailed hands' units)
SIT_ROOT = 3.0        # both raised that much, as their sitting frames (pelvis at 11.6 + 3)
env = os.environ.get
SHOULDERS = float(env('PAIR_APART', '7.2'))   # Hina's middle from Mitsuki's: their shoulders touching
KNEES = float(env('PAIR_KNEES', '1.6'))        # their knees' middles apart (touching, a little pressed)
HINA_TURN = float(env('PAIR_TURN', '-25'))     # the slant of Hina's forearm and hand (degrees from +x)
HINA_LIFT = float(env('PAIR_LIFT', '2.5'))     # her hand above her lap
HINA_WRIST = float(env('PAIR_WRIST', '0'))     # her hand turned from her forearm
HINA_PALM, HINA_THICK = 12.6, 2.5               # her detailed hand's (hands.py HINA)
MITSUKI_PALM = 14.5                             # hers (hands.py MITSUKI)
MITSUKI_LEAN = float(env('PAIR_LEAN', '-26'))  # leaning in to reach
MITSUKI_SHIFT = float(env('PAIR_SHIFT', '6'))  # her hand across Hina's: its edge in her thumb's web
SLEEVE_BACK = float(env('PAIR_SLEEVE', '10'))  # her cuff that far up from her wrist
MITSUKI_CROSS = float(env('PAIR_CROSS', '50')) # her hand to Hina's (degrees)
OVER = float(env('PAIR_OVER', '0.5'))          # her palm over Hina's hand, along it

# Their poses on the bench, before the arms are found (rig.py joint angles: swing or (swing,
# spread, twist)).
HINA_SIT = {'hip_R': (90, 8), 'knee_R': -86, 'hip_L': (90, -8), 'knee_L': -86, 'shoulder_L': (-24, 30), 'elbow_L': 10,
            'spine': (4, 0, 0)}
MITSUKI_SIT = {'hip_R': (90, 6), 'knee_R': -86, 'hip_L': (90, -2), 'knee_L': -86, 'shoulder_R': (20, -10), 'elbow_R': 40,
               'spine': (MITSUKI_LEAN, 0, 0), 'head': (-4 - MITSUKI_LEAN, 0, float(env('PAIR_LOOK', '40')))}
# The steps: Hina's head in Mitsuki's lap (leaning toward -y, spread +; forward, swing -).
# (Hina leans in, bent forward from her waist, her head resting in Mitsuki's lap: a figure this
# big-headed reaches it by leaning; her face turned a little toward us.)
HINA_LEAN = {'spine': (-float(env('PAIR_HFWD', '42')), float(env('PAIR_HSPINE', '16')), 0),
             'chest': (-float(env('PAIR_HCFWD', '24')), float(env('PAIR_HCHEST', '12')), 0),
             'head': (-float(env('PAIR_HNOD', '-30')), float(env('PAIR_HTILT', '10')), float(env('PAIR_HTURN', '14')))}   # (swing -: forward)
# Mitsuki looking down at her, then stroking her hair (CARESS: the stroke's half length).
MITSUKI_LOOK_DOWN = {'head': (-4 - MITSUKI_LEAN - float(env('PAIR_MNOD', '14')), 0, float(env('PAIR_MTURN', '30')))}
CARESS = float(env('PAIR_CARESS', '1.4'))


def load(rel, name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, rel))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class _Names:
    """Membership answering whether a model's material is there already (hina.model asks)."""

    def __init__(self, test):
        self.test = test

    def __contains__(self, name):
        return self.test(name)


def _stand(r, prefix, material, group, skip=()):
    """A stand-in rig adding a model's joints (renamed prefix + name) and parts to `r`, each
    part's material through `material(name)` (None: left out) and its group through
    `group(group)` (None: left out); parts on joints in `skip` left out. Materials are added
    by `material` itself if it wants them."""

    class Stand:
        is_stand = True   # (a character's build adds no scene of two to a stand-in)

        def __init__(self, name=None):
            pass

        def __getattr__(self, name):
            return lambda *a, **k: None

        def __setattr__(self, name, value):
            pass

        @property
        def materials(self):
            return _Names(lambda n: False)   # (material() decides)

        def material(self, name, ramp, **k):
            material(name, ramp, k)

        def joint(self, name, parent, at):
            r.joint(prefix + name, None if parent is None else prefix + parent, at)

        def _part(self, kind, joint, mat, *a, group=None, **k):
            m, g = material(mat, None, None), group_of(group)
            if m is None or g is None or joint in skip:
                return
            getattr(r, kind)(prefix + joint, m, *a, group=g, **k)

        def ellipsoid(self, *a, **k):
            self._part('ellipsoid', *a, **k)

        def tube(self, *a, **k):
            self._part('tube', *a, **k)

        def box(self, *a, **k):
            self._part('box', *a, **k)

    group_of = group
    return Stand


def holdout(r, name='other'):
    """A `holdout` material named `name` (rig.py): the other character, hiding what is behind."""
    if name not in r.materials:
        r.material(name, [(0, 0, 0)], bands=[], holdout=True)
    return lambda mat, ramp, k: name


def add_mitsuki(r, prefix='m_', material=None, group=None, skip=()):
    """Mitsuki's model into `r`: her joints renamed prefix + name; her parts' materials through
    `material(name, ramp, settings)` (default: hers, renamed prefix + name) and their groups
    through `group(g)` (default: her body and eyes, as prefix + 'body', ...; her things left
    out). Returns her module."""
    mitsuki = load('../mitsuki.py', 'mitsuki_model')

    def own(name, ramp, k):
        if ramp is not None and prefix + name not in r.materials:
            r.material(prefix + name, ramp, **k)
        return prefix + name
    groups = {None: prefix + 'body', 'far_eye': prefix + 'far_eye', 'eyes': prefix + 'eyes'}
    mitsuki.build(_stand(r, prefix, material or own, group or (lambda g: groups.get(g)), skip))
    return mitsuki


def add_hina(r, prefix='h_', material=None, group=None, head=True, right_hand=True):
    """Hina's model into `r` (hina.model), as add_mitsuki does Mitsuki's. Returns her module."""
    hina = load('../home/hina.py', 'hina_model')

    def own(name, ramp, k):
        if ramp is not None and prefix + name not in r.materials:
            r.material(prefix + name, ramp, **k)
        return prefix + name
    shown = ('body', 'far_eye', 'lap', 'relaxed', 'relaxed_lids', 'resting_lids', 'head')
    hina.model(_stand(r, prefix, material or own, group or (lambda g: prefix + g if g in shown else None))(),
               head=head, right_hand=right_hand)
    return hina


def angles(r, joints):
    """Turns `r`'s joints as rig.py's poses do."""
    from mathutils import Euler
    for name, a in joints.items():
        if name not in r.joints:
            continue
        swing, spread, twist = (a, 0, 0) if isinstance(a, (int, float)) else (tuple(a) + (0, 0))[:3]
        r.joints[name].rotation_euler = Euler((math.radians(spread), math.radians(-swing), math.radians(twist)))


def solve(r, m, h, log=None):
    """Seats them and finds their arms. `m(name)`, `h(name)`: Mitsuki's and Hina's joint names
    in `r`. Returns a dict: 'joints' (the pose, both; at SCALE), 'hina_hand' (wrist, turn,
    tilt: where the close-up's detailed hand goes), 'mitsuki_hand' (at, turn), 'middle' (of
    Hina's hand)."""
    import bpy
    from mathutils import Vector
    S = SCALE
    js = {h(k): v for k, v in HINA_SIT.items()}
    js.update({m(k): v for k, v in MITSUKI_SIT.items()})
    angles(r, js)
    mp, hp = r.joints[m('pelvis')], r.joints[h('pelvis')]
    mp.scale = hp.scale = (S, S, S)
    mp.location = Vector((0, 0, (11.6 + SIT_ROOT) * S))
    bpy.context.view_layer.update()

    def at(name):
        return r.joints[name].matrix_world.translation.copy()
    # Hina beside her, shoulder to shoulder (SHOULDERS apart: their figures' shoulders, wider
    # than their hips, touch); their thighs turned toward each other, the knees pointing to
    # each other and touching (as the close-up shows them: no bench between them).
    side = at(m('hip_L')) - at(m('hip_R'))
    side.z = 0
    side.normalize()
    hp.location = Vector((0, 0, (11.6 + SIT_ROOT) * S)) + side * SHOULDERS * S
    bpy.context.view_layer.update()
    lo, hi = 0.0, 45.0
    for _ in range(16):
        a = (lo + hi) / 2
        angles(r, {m('hip_L'): (90, a), h('hip_R'): (90, -a)})
        bpy.context.view_layer.update()
        apart = (at(h('knee_R')) - at(m('knee_L'))).dot(side)
        if apart > KNEES * S:
            lo = a
        else:
            hi = a
    js[m('hip_L')] = (90, hi)
    js[h('hip_R')] = (90, -hi)
    angles(r, js)
    bpy.context.view_layer.update()

    # Hina's wrist on her thigh, its forearm along HINA_TURN.
    hip, knee = at(h('hip_R')), at(h('knee_R'))
    top = 1.7 * S + 0.6
    target = hip + (knee - hip) * 0.4 - side * 2.0 + Vector((0, 0, top))
    aim = Vector((math.cos(math.radians(HINA_TURN)), math.sin(math.radians(HINA_TURN)), -0.25)).normalized()
    best = None
    for sw in range(0, 100, 6):
        for sp in range(-40, 44, 6):
            for el in range(0, 130, 8):
                angles(r, {h('shoulder_R'): (sw, sp), h('elbow_R'): el})
                bpy.context.view_layer.update()
                w, e = at(h('hand_R')), at(h('elbow_R'))
                cost = (w - target).length + 12.0 * (1.0 - (w - e).normalized().dot(aim))
                if best is None or cost < best[0]:
                    best = (cost, sw, sp, el)
    js[h('shoulder_R')] = (best[1], best[2])
    js[h('elbow_R')] = best[3]
    angles(r, js)
    bpy.context.view_layer.update()
    wrist, elbow = at(h('hand_R')), at(h('elbow_R'))
    d = (wrist - elbow).normalized()
    turn = math.degrees(math.atan2(d.y, d.x)) + HINA_WRIST

    # Her hand out of her sleeve, on her lap at its measured height, lifted a little.
    wrist = wrist + d * (3.0 - 0.2 * S)
    deps = bpy.context.evaluated_depsgraph_get()
    dirv = Vector((math.cos(math.radians(turn)), math.sin(math.radians(turn)), 0))
    across = Vector((-dirv.y, dirv.x, 0))
    lap_parts = (h('pelvis'), h('hip_R'), h('hip_L'))

    def lap_z(p):
        best_z = None
        for off in (-5.0, 0.0, 5.0):
            origin = Vector((p.x, p.y, 4000.0)) + across * off
            while True:
                hit, loc, nrm, idx, ob, mtx = r.scene.ray_cast(deps, origin, Vector((0, 0, -1)))
                if not hit:
                    break
                if ob.parent is not None and ob.parent.name in lap_parts:
                    best_z = loc.z if best_z is None else max(best_z, loc.z)
                    break
                origin = loc - Vector((0, 0, 0.01))
        return best_z

    near, far = lap_z(wrist + dirv * 4.0), lap_z(wrist + dirv * 26.0)
    heights = [z for z in (near, far, lap_z(wrist + dirv * 15.0)) if z is not None]
    lap = max(heights) if heights else target.z
    tilt = 0.0
    if near is not None and far is not None:
        tilt = max(-25.0, min(25.0, math.degrees(math.atan2(near - far, 22.0))))
    wrist = Vector((wrist.x, wrist.y, (near if near is not None else lap) + HINA_THICK + 0.8 + HINA_LIFT))

    # Mitsuki's hand over it, crossing it at MITSUKI_CROSS, her thumb under it; her arm to it.
    t = math.radians(turn)
    mturn = turn + MITSUKI_CROSS
    mm = math.radians(mturn)
    mdir = Vector((math.cos(mm), math.sin(mm), 0))
    middle = wrist + Vector((math.cos(t), math.sin(t), 0)) * HINA_PALM * OVER
    macross = Vector((math.sin(mm), -math.cos(mm), 0))
    hand_at = middle - mdir * MITSUKI_PALM * 0.55 + macross * MITSUKI_SHIFT + Vector((0, 0, 3.6))
    maim = Vector((math.cos(mm), math.sin(mm), -0.2)).normalized()
    cuff = hand_at - mdir * SLEEVE_BACK
    best = None
    for sw in range(0, 120, 6):
        for sp in range(-60, 64, 6):
            for el in range(0, 130, 8):
                angles(r, {m('shoulder_L'): (sw, sp), m('elbow_L'): el})
                bpy.context.view_layer.update()
                w, e = at(m('hand_L')), at(m('elbow_L'))
                dd = (w - e).normalized()
                cost = (w + dd * (3.0 - 0.6 * S) - cuff).length + 10.0 * (1.0 - dd.dot(maim))
                if best is None or cost < best[0]:
                    best = (cost, sw, sp, el)
    js[m('shoulder_L')] = (best[1], best[2])
    js[m('elbow_L')] = best[3]
    angles(r, js)
    bpy.context.view_layer.update()
    mw, me = at(m('hand_L')), at(m('elbow_L'))
    dd = (mw - me).normalized()
    reached = mw + dd * (3.0 - 0.6 * S)
    hand_at = hand_at * 0.85 + (reached + mdir * SLEEVE_BACK) * 0.15
    if log:
        with open(log, 'a') as f:
            f.write('pair: hina arm %r, mitsuki arm %r, hina at %r\n' % (js[h('shoulder_R')], best, tuple(hp.location / S)))
    return {'joints': js, 'hina_hand': (wrist, turn, tilt), 'mitsuki_hand': (hand_at, mturn), 'middle': middle}


def shrink(r, names):
    """The solved scene at the sprites' scale (a unit a pixel): the roots `names` (the pelvises)
    scaled back to 1, their places divided by SCALE (Mitsuki's pelvis then where her frames'
    root puts it)."""
    for n in names:
        ob = r.joints[n]
        ob.location = ob.location / SCALE
        ob.scale = (1.0, 1.0, 1.0)


def _reach(r, shoulder, elbow, hand, target, grid):
    """The arm's angles (forward kinematics, the nearest of a grid) putting the hand's middle
    (half a unit past its joint) nearest `target`."""
    import bpy
    best = None
    sws, sps, els = grid
    for sw in sws:
        for sp in sps:
            for el in els:
                angles(r, {shoulder: (sw, sp), elbow: el})
                bpy.context.view_layer.update()
                w = r.joints[hand].matrix_world.translation
                e = r.joints[elbow].matrix_world.translation
                cost = (w + (w - e).normalized() * 0.5 - target).length
                if best is None or cost < best[0]:
                    best = (cost, sw, sp, el)
    if env('PAIR_LOG'):
        with open(env('PAIR_LOG'), 'a') as f:
            f.write('reach %s: %r\n' % (hand, best))
    return (best[1], best[2]), best[3]


def steps(r, js, m, h):
    """The bench frames' poses from the solved one (at the sprites' scale, after shrink):
    holding hands; Hina leaning in, her head in Mitsuki's lap (Mitsuki still holding her hand,
    looking down at her); Mitsuki stroking her head with her other hand, two frames."""
    import bpy
    from mathutils import Vector
    hold = dict(js)
    lean = dict(js)
    lean.update({h(k): v for k, v in HINA_LEAN.items()})
    lean.update({m(k): v for k, v in MITSUKI_LOOK_DOWN.items()})
    angles(r, lean)
    bpy.context.view_layer.update()

    def at(n):
        return r.joints[n].matrix_world.translation.copy()
    # Her left hand still on Hina's, which moved with her.
    hand, elbow = at(h('hand_R')), at(h('elbow_R'))
    target = hand + (hand - elbow).normalized() * 0.5 + Vector((0, 0, 0.9))
    lean[m('shoulder_L')], lean[m('elbow_L')] = _reach(r, m('shoulder_L'), m('elbow_L'), m('hand_L'), target,
                                                       (range(0, 120, 4), range(-60, 64, 4), range(0, 130, 6)))
    angles(r, lean)
    bpy.context.view_layer.update()
    # Her right hand on Hina's hair where it is nearest her, a little above and toward us (to be
    # seen), stroking over the top of her head.
    head = r.joints[h('head')]
    centre = head.matrix_world @ (Vector((0.0, 0, 24.7)) - r.rest[h('head')])
    toward = (at(m('shoulder_R')) - centre).normalized() + Vector((float(env('PAIR_FRONT', '1.2')), 0, 0.6))   # (and toward us: seen)
    crown = centre + toward.normalized() * 4.2
    n = toward.normalized()
    stroke = Vector((0, 1, 0.5))
    stroke = stroke - n * stroke.dot(n)   # (along her hair, over the top of her head)
    stroke.normalize()
    out = [hold, lean]
    for k in (-1, 1):
        c = dict(lean)
        c[m('hand_R')] = -20
        angles(r, c)
        c[m('shoulder_R')], c[m('elbow_R')] = _reach(r, m('shoulder_R'), m('elbow_R'), m('hand_R'),
                                                     crown + stroke * CARESS * k,
                                                     (range(0, 150, 4), range(-70, 64, 4), range(0, 140, 6)))
        out.append(c)
    angles(r, js)
    return out
