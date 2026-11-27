# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Hina, Mitsuki's friend (docs/plans/3d-sprites.md): who calls her in the night (the prologue,
assets/mitsuki/home) and waits for her in the park (assets/mitsuki/park). Mitsuki's opposite:
chestnut hair in two bouncing tails, wide green eyes, a yellow hoodie, a teal skirt, white
socks, red sneakers; a little rounder, warmer. Modelled on Mitsuki's rig (mitsuki.py: the same
joints and head, so the two match side by side), posed for the park's scene; her portraits are
the first pose seen closer.

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/home/hina.py \\
      --layout assets/mitsuki/park/hina_layout.bmsprite --sink 2 --out assets/mitsuki/park --name hina \\
      --face-name hina_soft
  HINA_FACE=panic python3 tools/sprite3d/make_sprite.py assets/mitsuki/home/hina.py \\
      --layout assets/mitsuki/park/hina_layout.bmsprite --out assets/mitsuki/home --name hina --face-only yes

The first writes her sprite (and a soft portrait, hina_soft: relieved, close to tears); the
second the panicked portrait of the phone call (hina_face in her home's folder).
"""

import math
import os

OUT = (24, 20, 36)
HAIR = [(92, 52, 40), (150, 94, 60), (204, 148, 92)]
SKIN = [(226, 166, 150), (255, 226, 208)]
WHITE = [(255, 250, 240)]
GREEN = [(28, 104, 72), (92, 200, 124)]
GLOW = [(150, 255, 170)]                     # her eyes in the dark under the bench
HOODIE = [(176, 118, 34), (240, 196, 76)]
SKIRT = [(30, 64, 70), (50, 104, 104)]
SHOE = [(170, 40, 50)]
SWEAT = [(150, 204, 255)]

SIDES = (('R', -1), ('L', 1))
LID_Z = float(os.environ.get('HINA_LID', '23.5'))       # her relaxed lids' edge (the bench)
LID_TILT = float(os.environ.get('HINA_LID_TILT', '-12'))
IRIS_IN = float(os.environ.get('HINA_IRIS_IN', '0'))
LOOK = float(os.environ.get('HINA_LOOK', '-0.45'))     # her pupils toward Mitsuki (-y), both the same way
PANIC = os.environ.get('HINA_FACE') == 'panic'


def build(Rig):
    r = Rig('hina')
    r.view = {'yaw': 22.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 3.0
    model(r)
    ps = poses()
    if not PANIC:   # (the panicked portrait's run needs no bench)
        bench(r, ps)
    for p in ps:
        r.pose(**p)
    # Panicked: the phone call (pose 0); soft: shy, close to tears (pose 8).
    r.portrait(0 if PANIC else 8, (0.9, 0, 23.6), 3.3, yaw=58.0, pitch=4.0, show=('portrait', 'far_eye'))
    return r


def bench(r, ps):
    """Her frames on the bench (9 holding hands, 10 her head in Mitsuki's lap, 14 and 15 her
    hair stroked: Mitsuki's hand there differs) from the scene the close-up of their hands is drawn from too (assets/mitsuki/park/
    pair.py): Mitsuki there as a holdout (hiding what of Hina is behind her: she is drawn by
    her own sprite), Hina at the origin, her face relaxed."""
    import sys
    from mathutils import Vector
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'park'))
    import pair
    pair.add_mitsuki(r, 'm_', material=pair.holdout(r),
                     group=lambda g: 'pair_mitsuki' if g in (None, 'far_eye', 'eyes') else None)
    m = lambda n: 'm_' + n  # noqa: E731
    h = lambda n: n  # noqa: E731
    solved = pair.solve(r, m, h)
    pair.shrink(r, ['pelvis', 'm_pelvis'])
    off = r.joints['pelvis'].location - Vector((0, 0, 11.6 + pair.SIT_ROOT))
    for n in ('pelvis', 'm_pelvis'):
        r.joints[n].location = r.joints[n].location - off
    show = ('body', 'far_eye', 'lap', 'relaxed', 'pair_mitsuki')
    for i, js in zip((9, 10, 14, 15), pair.steps(r, solved['joints'], m, h)):
        lids = 'relaxed_lids' if i == 9 else 'resting_lids'   # (her eyes shut, her head in her lap)
        p = pose(view=FRONT, show=show + (lids,), root=(0, 0, pair.SIT_ROOT), **js)
        if i < len(ps):
            ps[i] = p
        else:
            ps.append(p)


def model(r, head=True, right_hand=True):
    """Hina's materials (those `r` does not have yet), joints and parts, into the rig `r`: her
    sprite's, and the close-up of her hand in her lap (assets/mitsuki/park/hands.py: without her
    head, which is out of the picture, and without her right hand, which is drawn in detail)."""
    def mat(name, *a, **k):
        if name not in r.materials:
            r.material(name, *a, **k)
    three, two = [0.35, 0.8], [0.5]
    mat('hair', HAIR, bands=three)
    mat('skin', SKIN, bands=[0.3])
    mat('sclera', WHITE, weight=7, min_cover=0.15, bands=[], keep_inner=True)
    mat('eye', [GREEN[1]], weight=8, min_cover=0.12, bands=[], keep_inner=True)
    mat('pupil', [GREEN[0]], weight=9, min_cover=0.12, bands=[], keep_inner=True)
    mat('shine', WHITE, weight=10, min_cover=0.5, bands=[], keep_inner=True)
    mat('lash', [OUT], weight=7, min_cover=0.15, bands=[], keep_inner=True)
    mat('mouth', SHOE, weight=8, min_cover=0.2, bands=[], keep_inner=True)
    mat('sweat', SWEAT, weight=6, min_cover=0.3, bands=[], keep_inner=True)
    mat('hoodie', HOODIE, bands=two)
    mat('skirt', SKIRT, bands=two)
    mat('sock', WHITE, bands=[])
    mat('shoe', SHOE, bands=[])
    mat('phone', SKIRT[:1], weight=3, min_cover=0.3, bands=[])
    mat('glow', GLOW, weight=9, min_cover=0.2, bands=[], keep_inner=True, emissive=True)
    mat('lid', SKIN, weight=9.5, min_cover=0.3, bands=[0.3], keep_inner=True)   # her relaxed face's lids
    mat('lidline', [OUT], weight=11, min_cover=0.15, bands=[], keep_inner=True)   # their lashes, over them
    mat('pupil_front', [GREEN[0]], weight=30, min_cover=0.2, bands=[], keep_inner=True)   # (a pixel of its own)

    # The rig: Mitsuki's (mitsuki.py), her twin tails on the head.
    r.joint('pelvis', None, (0, 0, 11.6))
    r.joint('spine', 'pelvis', (0, 0, 13.2))
    r.joint('chest', 'spine', (0, 0, 16.0))
    r.joint('head', 'chest', (0.2, 0, 20.2))
    for s, y in SIDES:
        r.joint('tail_' + s, 'head', (-0.6, 3.6 * y, 25.0))
        r.joint('shoulder_' + s, 'chest', (0, 2.6 * y, 19.0))
        r.joint('elbow_' + s, 'shoulder_' + s, (0, 2.6 * y, 15.8))
        r.joint('hand_' + s, 'elbow_' + s, (0, 2.6 * y, 12.8))
        r.joint('hip_' + s, 'pelvis', (0, 1.2 * y, 11.2))
        r.joint('knee_' + s, 'hip_' + s, (0, 1.2 * y, 6.3))
        r.joint('ankle_' + s, 'knee_' + s, (0, 1.2 * y, 1.5))
    r.locator('hand', 'hand_R', (0, -2.6, 11.9))
    r.locator('hand_l', 'hand_L', (0, 2.6, 11.9))
    B = 'body'
    H = B if head else 'head'   # the head's parts (left out of the close-up)

    # Head and face (as her first portrait), her tails.
    r.ellipsoid('head', 'skin', (0.5, 0, 23.9), (3.3, 3.4, 3.5), group=H)
    r.ellipsoid('head', 'skin', (1.2, 0, 22.4), (2.3, 2.5, 1.9), group=H)
    r.tube('head', 'skin', (0.3, 0, 20.0), (0.4, 0, 21.6), 0.85, group=H)
    turn = 37.0
    for s, y in SIDES:
        nx, ny = math.cos(math.radians(turn)), math.sin(math.radians(turn)) * y
        tx, ty = math.sin(math.radians(turn)), -math.cos(math.radians(turn)) * y

        def on_face(out, z, inward=0.0):
            return (3.05 + out * nx + inward * tx, 1.95 * y + out * ny + inward * ty, z)
        gaze = 0.3 if y < 0 else 0.45
        g = H if y < 0 else 'far_eye'
        r.ellipsoid('head', 'sclera', on_face(0.0, 23.1), (0.38, 0.9, 1.35), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'eye', on_face(0.14, 23.0, gaze), (0.34, 0.5, 0.8), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'pupil', on_face(0.26, 22.95, gaze + 0.04), (0.26, 0.24, 0.36), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'shine', on_face(0.36, 23.3, gaze - 0.1), (0.1, 0.14, 0.17), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'lash', on_face(0.18, 24.45), (0.42, 0.95, 0.2), rot=(10 * y, 0, turn * y), group=g)
        # Panicked: the brow high up; soft: a brow tilted up in the middle, close to tears.
        if PANIC:
            r.ellipsoid('head', 'lash', on_face(0.25, 25.6, -0.1), (0.3, 0.75, 0.14), rot=(-12 * y, 0, turn * y), group='portrait')
        else:
            r.ellipsoid('head', 'lash', on_face(0.25, 25.3, -0.1), (0.3, 0.75, 0.14), rot=(18 * y, 0, turn * y), group='portrait')
        # Relaxed (on the bench, her hand held): her brows level; her lids half down, her pupils
        # showing under them (relaxed_lids), or down over them, resting (resting_lids), the
        # lashes with them.
        r.ellipsoid('head', 'lid', on_face(0.3, LID_Z + 0.95), (0.45, 1.0, 1.0), rot=(0, 0, turn * y), group='relaxed_lids')
        r.ellipsoid('head', 'lidline', on_face(0.34, LID_Z), (0.42, 1.0, 0.32), rot=(LID_TILT * y, 0, turn * y),
                    group='relaxed_lids')
        # (Seen from the front her irises fill her eyes, no white beside them; the pupils keep a
        # pixel of their own, both looking toward Mitsuki.)
        r.ellipsoid('head', 'eye', on_face(0.16, 23.0, gaze + IRIS_IN), (0.38, 1.05, 1.0), rot=(0, 0, turn * y), group='relaxed')
        px, py, pz = on_face(0.28, 22.95, gaze + IRIS_IN + 0.04)
        r.ellipsoid('head', 'pupil_front', (px, py + LOOK, pz), (0.3, 0.55, 0.62), rot=(0, 0, turn * y),
                    group='relaxed')
        r.ellipsoid('head', 'lid', on_face(0.3, 23.55), (0.45, 1.0, 1.0), rot=(0, 0, turn * y), group='resting_lids')
        r.ellipsoid('head', 'lidline', on_face(0.34, 22.6), (0.42, 1.0, 0.3), rot=(4 * y, 0, turn * y), group='resting_lids')
        r.ellipsoid('head', 'lash', on_face(0.25, 25.2, -0.1), (0.3, 0.75, 0.13), rot=(0, 0, turn * y), group='relaxed')
        # Her eyes in the dark (only these, glowing, while she hides under the bench).
        if y < 0:
            r.ellipsoid('head', 'glow', on_face(0.2, 23.0, gaze), (0.4, 0.7, 0.9), rot=(0, 0, turn * y), group='eyes')
            r.ellipsoid('head', 'glow', on_face(0.2, 23.0, gaze + 2.6), (0.4, 0.7, 0.9), rot=(0, 0, turn * y), group='eyes')
            r.ellipsoid('head', 'lash', on_face(0.2, 22.9, gaze), (0.4, 0.7, 0.22), rot=(0, 0, turn * y), group='blink')
            r.ellipsoid('head', 'lash', on_face(0.2, 22.9, gaze + 2.6), (0.4, 0.7, 0.22), rot=(0, 0, turn * y), group='blink')
        r.tube('head', 'hair', (0.6, 3.2 * y, 25.0), (0.9, 3.1 * y, 21.6), 0.75, 0.5, group=H)
        r.ellipsoid('tail_' + s, 'hair', (-0.6, 3.8 * y, 25.0), (1.0, 0.8, 1.0), group=H)
        r.tube('tail_' + s, 'hair', (-1.0, 4.3 * y, 25.0), (-3.2, 6.8 * y, 19.0), 1.6, 0.6, group=H)
    if PANIC:
        r.ellipsoid('head', 'mouth', (3.55, -0.9, 21.3), (0.3, 0.75, 0.6), rot=(0, 0, -30), group='portrait')
        r.ellipsoid('head', 'sweat', (2.0, -3.2, 26.0), (0.4, 0.4, 0.7), group='portrait')
    else:
        r.ellipsoid('head', 'mouth', (3.6, -0.8, 21.2), (0.25, 0.55, 0.3), rot=(0, 0, -30), group='portrait')
    # Relaxed: a small soft smile, closed.
    r.ellipsoid('head', 'mouth', (3.62, -0.75, 21.3), (0.14, 0.62, 0.11), rot=(0, 0, -30), group='relaxed')
    r.ellipsoid('head', 'hair', (0.0, 0, 24.7), (3.9, 3.9, 4.0), cuts=[((2.7, 0, 25.4), (0.7, 0, -0.6))], group=H)
    r.tube('head', 'hair', (2.6, -1.6, 26.6), (3.5, -0.6, 25.0), 0.7, 0.3, group=H)
    r.tube('head', 'hair', (2.8, 1.4, 26.6), (3.5, 0.6, 25.2), 0.6, 0.3, group=H)

    # The hoodie (its hood behind her neck, sleeves to her knuckles), the skirt, socks, shoes.
    r.ellipsoid('chest', 'hoodie', (-1.4, 0, 19.8), (1.6, 2.6, 1.4), group=B)
    r.ellipsoid('chest', 'hoodie', (0.2, 0, 17.0), (2.1, 2.6, 3.2), group=B)
    r.ellipsoid('spine', 'hoodie', (0.1, 0, 14.0), (1.9, 2.3, 1.8), group=B)
    r.tube('pelvis', 'skirt', (0.1, 0, 12.6), (0.1, 0, 9.4), 2.2, 3.0, caps=False, group=B)
    # Sitting, the skirt lies over her thighs (shown in the poses she sits in).
    r.ellipsoid('pelvis', 'skirt', (2.4, 0, 11.7), (3.2, 2.7, 1.1), group='lap')
    for s, y in SIDES:
        r.ellipsoid('shoulder_' + s, 'hoodie', (0, 2.5 * y, 18.9), (1.3, 1.2, 1.3), group=B)
        r.tube('shoulder_' + s, 'hoodie', (0, 2.6 * y, 18.8), (0, 2.6 * y, 15.8), 1.05, group=B)
        # (Without the right hand, for the close-up's detailed one, its sleeve is open at the end.)
        r.tube('elbow_' + s, 'hoodie', (0, 2.6 * y, 15.8), (0, 2.6 * y, 13.0), 1.05, 1.2, group=B,
               caps=right_hand or s != 'R')
        if right_hand or s != 'R':
            r.ellipsoid('hand_' + s, 'skin', (0, 2.6 * y, 12.3), (0.75, 0.75, 0.85), group=B)
        r.tube('hip_' + s, 'skin', (0, 1.2 * y, 10.0), (0, 1.2 * y, 6.3), 0.95, 0.85, group=B)
        r.tube('knee_' + s, 'sock', (0, 1.2 * y, 5.0), (0, 1.2 * y, 1.6), 0.85, 0.8, group=B)
        r.ellipsoid('ankle_' + s, 'shoe', (0.7, 1.2 * y, 0.9), (1.7, 0.95, 0.95), group=B)
    r.box('hand_R', 'phone', (0.6, -3.4, 12.8), (1.0, 0.6, 3.2), group='phone')


# --- Poses: one for each frame of her layout (assets/mitsuki/park/hina_layout.bmsprite) -----

FRONT = (90.0, 8.0)
REST = {'shoulder_R': (-4, -8), 'shoulder_L': (4, 8), 'elbow_R': 14, 'elbow_L': 14, 'head': -3,
        'tail_R': (0, -14), 'tail_L': (0, 14)}


def pose(base=REST, root=(0, 0, 0), show=('body', 'portrait'), view=None, **joints):
    j = dict(base)
    j.update(joints)
    return {'joints': j, 'root': root, 'show': tuple(show), 'view': view}


def poses():
    out = []
    # 0: the phone call, her phone at her ear: only the panicked portrait's pose (drawn for it;
    # in the park's sprite an empty frame, its tiles saved for the close-up).
    out.append(pose(head=(-4, 0, -6), shoulder_R=(50, -22), elbow_R=115, hand_R=-10, tail_R=(0, -20), tail_L=(0, 20),
                    show=('body', 'portrait', 'phone') if PANIC else ()))
    # 1, 2: hiding under the bench, crouched: only her eyes, glowing, then shut (a blink); low,
    # in the dark under the seat (the park's bench: its seat 9 to 15 pixels up, the dark below).
    crouch = dict(spine=30, head=-30, hip_R=120, knee_R=-140, hip_L=110, knee_L=-140)
    out.append(pose(root=(0, 0, -13.5), show=('eyes',), **crouch))
    out.append(pose(root=(0, 0, -13.5), show=('blink',), **crouch))
    # 3: out from under it in a leap, arms flung ahead.
    out.append(pose(root=(0, 0, 2.0), spine=-18, head=6, hip_R=40, knee_R=-60, hip_L=-30, knee_L=-70,
                    shoulder_R=(140, -20), elbow_R=10, shoulder_L=(130, 20), elbow_L=10, tail_R=(-30, -30), tail_L=(-30, 30)))
    # 4, 5: in Mitsuki's arms, curled up: her face against her, fists at her chest; shaking.
    hug = dict(spine=-14, chest=-8, head=-18, hip_R=14, knee_R=-22, hip_L=6, knee_L=-30,
               shoulder_R=(50, 10), elbow_R=120, shoulder_L=(50, -10), elbow_L=120, tail_R=(10, -6), tail_L=(10, 6))
    out.append(pose(root=(0, 0, -0.6), **hug))
    rock = dict(hug)
    hug.update(spine=-16, head=-20, tail_R=(14, -10), tail_L=(14, 10))
    out.append(pose(root=(0, 0, -0.9), **hug))
    # 6, 7: led by the hand, walking: her near arm ahead, holding on, her head low.
    for k in (1, -1):
        out.append(pose(spine=-4, head=8, hip_R=26 * k, knee_R=-20 if k > 0 else -36, hip_L=-26 * k,
                        knee_L=-36 if k > 0 else -20, shoulder_R=(70, -6), elbow_R=8, shoulder_L=(-14 * k, 8), elbow_L=30,
                        tail_R=(-8, -16), tail_L=(-8, 16)))
    # 8: standing, shy, her hands together at her chest.
    out.append(pose(head=8, shoulder_R=(30, 18), elbow_R=100, shoulder_L=(30, -18), elbow_L=100, hip_R=6, knee_R=-10))
    # 9, 10: sitting on the bench close beside Mitsuki, seen from the front, leaning to her a
    # little: her hands in her lap (Mitsuki's holding the right one there); then her head low.
    sit = dict(hip_R=(90, 8), knee_R=-86, hip_L=(90, -8), knee_L=-86, shoulder_R=(34, 16), elbow_R=50, hand_R=10,
               shoulder_L=(30, -16), elbow_L=60, spine=(0, 4, 0), head=(-2, 6, 0))
    out.append(pose(view=FRONT, show=('body', 'portrait', 'far_eye', 'lap'), root=(0, 0, 3.0), **sit))
    sit.update(head=16, spine=6, chest=4)
    out.append(pose(view=FRONT, show=('body', 'portrait', 'far_eye', 'lap'), root=(0, 0, 3.0), **sit))
    # 11: snatched: lifted, bent back, her near hand reaching out for Mitsuki.
    out.append(pose(root=(0, 0, 6.0), spine=24, chest=14, head=20, hip_R=-20, knee_R=-50, hip_L=10, knee_L=-80,
                    shoulder_R=(105, -10), elbow_R=0, hand_R=-10, shoulder_L=(-40, 20), elbow_L=40,
                    tail_R=(40, -30), tail_L=(40, 30)))
    # 12, 13: rocked in Mitsuki's arms: leaning with her (Mitsuki faces her, so Mitsuki's
    # forward is her back: the opposite lean in her own frame).
    for lean in (-5, 5):
        r2 = dict(rock)
        r2.update(pelvis=lean, spine=rock['spine'] + lean // 2, hip_R=rock['hip_R'] - lean, hip_L=rock['hip_L'] - lean,
                  tail_R=(rock['tail_R'][0] - lean * 2, -6), tail_L=(rock['tail_L'][0] - lean * 2, 6))
        out.append(pose(root=(0, 0, -0.6), **r2))
    return out
