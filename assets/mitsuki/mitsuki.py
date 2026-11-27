# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki, a playable character modelled for 3D sprites (docs/plans/3d-sprites.md).

Cool and quiet (a kuudere): silver hair in a hime cut with a lock that will not lie down,
half-lidded crimson eyes, a navy greatcoat with cuffs too big for her, a red scarf trailing
behind, a crescent pin in her hair. A katana for the sword's swings, a lance with a red
lacquered shaft for its thrusts.

Built in Blender by tools/sprite3d/make_sprite.py (build() below), one pose for every frame
of her layout (layout.bmsprite: the player's frames, assets/sources/player.bmsprite: idle,
run, jump, fall, climb, the swings and thrusts, push, aim, sit; then her bed at home for her
prologue: sleep, ring, call, wake, flip), and a portrait for the dialogue box.
"""

import math

OUT = (24, 20, 36)
HAIR = [(118, 120, 160), (190, 196, 222), (240, 244, 255)]
SKIN = [(226, 166, 150), (255, 226, 208)]
RED = [(120, 16, 40), (200, 32, 56)]
COAT = [(30, 32, 62), (52, 58, 104), (84, 96, 156)]
BOOT = [(30, 32, 62), (66, 62, 86)]
WHITE = [(190, 196, 222), (240, 244, 255)]
GOLD = [(176, 128, 40), (248, 212, 96)]
STEEL = [(140, 150, 176), (240, 244, 255)]

SIDES = (('R', -1), ('L', 1))     # her right is the near side (-y)


def build(Rig):
    r = Rig('mitsuki')
    r.view = {'yaw': 22.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 3.0

    three, two = [0.35, 0.8], [0.5]
    r.material('hair', HAIR, bands=three)
    r.material('ahoge', HAIR, bands=three, min_cover=0.25)
    r.material('skin', SKIN, bands=[0.3])
    r.material('sclera', [WHITE[1]], weight=7, min_cover=0.15, bands=[], keep_inner=True)
    r.material('eye', [RED[1]], weight=8, min_cover=0.12, bands=[], keep_inner=True)
    r.material('pupil', [RED[0]], weight=9, min_cover=0.12, bands=[], keep_inner=True)
    r.material('shine', [WHITE[1]], weight=10, min_cover=0.5, bands=[], keep_inner=True)
    r.material('lash', [OUT], weight=7, min_cover=0.15, bands=[], keep_inner=True)
    r.material('coat', COAT, bands=three)
    r.material('skirt', COAT[:2], bands=two)
    r.material('tights', COAT[:2], bands=two)
    r.material('boot', BOOT, bands=two)
    r.material('trim', WHITE, bands=two)
    r.material('scarf', RED, bands=two)
    r.material('gold', GOLD, weight=3, min_cover=0.3, bands=two, keep_inner=True)
    r.material('grip', RED, bands=two)
    r.material('blade', STEEL, weight=4, min_cover=0.12, bands=[0.6], keep_inner=True)
    r.material('shaft', RED, weight=2, min_cover=0.2, bands=two, keep_inner=True)
    r.material('blanket', HAIR, bands=[0.35, 0.75])

    # The rig.
    r.joint('pelvis', None, (0, 0, 11.6))
    r.joint('spine', 'pelvis', (0, 0, 13.2))
    r.joint('chest', 'spine', (0, 0, 16.0))
    r.joint('head', 'chest', (0.2, 0, 20.2))
    r.joint('hair', 'head', (-1.4, 0, 23.0))
    r.joint('hair_end', 'hair', (-2.4, 0, 17.2))
    r.joint('scarf', 'chest', (-1.2, -0.4, 19.6))
    for s, y in SIDES:
        r.joint('shoulder_' + s, 'chest', (0, 2.6 * y, 19.0))
        r.joint('elbow_' + s, 'shoulder_' + s, (0, 2.6 * y, 15.8))
        r.joint('hand_' + s, 'elbow_' + s, (0, 2.6 * y, 12.8))
        r.joint('hip_' + s, 'pelvis', (0, 1.15 * y, 11.2))
        r.joint('knee_' + s, 'hip_' + s, (0, 1.15 * y, 6.3))
        r.joint('ankle_' + s, 'knee_' + s, (0, 1.15 * y, 1.5))
    r.locator('hand', 'hand_R', (0, -2.6, 11.7))

    # Head and hair.
    r.ellipsoid('head', 'skin', (0.5, 0, 23.9), (3.3, 3.4, 3.5))
    r.ellipsoid('head', 'skin', (1.2, 0, 22.4), (2.3, 2.5, 1.9))
    r.tube('head', 'skin', (0.3, 0, 20.0), (0.4, 0, 21.6), 0.85)
    for s, y in SIDES:
        # Big anime eyes, half-lidded, set back on the sides of her head (not at the edge of
        # her profile): the white, a crimson iris in it, the pupil darker, a glint, and a heavy
        # lash over them; each layer a little further out along the head's surface normal.
        turn = 37.0
        nx, ny = math.cos(math.radians(turn)), math.sin(math.radians(turn)) * y

        # Along the face toward its middle (the nose), at right angles to the normal.
        tx, ty = math.sin(math.radians(turn)), -math.cos(math.radians(turn)) * y

        def on_face(out, z, inward=0.0):
            return (3.05 + out * nx + inward * tx, 1.95 * y + out * ny + inward * ty, z)
        # The iris toward the nose in the white, so she looks ahead, not apart; the far eye,
        # seen across her face, more.
        gaze = 0.32 if y < 0 else 0.5
        # The far eye only in the portrait (from more in front): in the sprites, seen from the
        # side, it would peek out past the edge of her face.
        # The near eye is shown unless she sleeps (pose(closed=True): a line instead).
        g = 'far_eye' if y > 0 else 'eyes'
        r.ellipsoid('head', 'sclera', on_face(0.0, 23.05), (0.38, 0.85, 1.1), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'eye', on_face(0.14, 22.95, gaze), (0.34, 0.55, 0.85), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'pupil', on_face(0.26, 22.85, gaze + 0.04), (0.26, 0.3, 0.48), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'shine', on_face(0.36, 23.2, gaze - 0.1), (0.1, 0.14, 0.17), rot=(0, 0, turn * y), group=g)
        r.ellipsoid('head', 'lash', on_face(0.18, 23.95), (0.42, 0.95, 0.26), rot=(-5 * y, 0, turn * y), group=g)
        if y < 0:
            r.ellipsoid('head', 'lash', on_face(0.2, 22.7, 0.1), (0.4, 0.9, 0.22), rot=(5 * y, 0, turn * y), group='closed')
        r.tube('head', 'hair', (0.7, 3.25 * y, 24.8), (0.9, 3.05 * y, 20.2), 0.7, 0.45)
    # The hime cut: the cap of hair ends in a straight fringe above the eyes.
    r.ellipsoid('head', 'hair', (0.0, 0, 24.6), (3.9, 3.9, 4.1), cuts=[((3.0, 0, 24.7), (0.6, 0, -0.8))])
    r.tube('head', 'ahoge', (0.6, 0, 28.3), (1.8, 0, 30.0), 0.5, 0.2)
    r.tube('head', 'ahoge', (1.8, 0, 30.0), (2.7, 0, 29.4), 0.2, 0.12)
    r.ellipsoid('head', 'gold', (1.4, -3.7, 26.2), (0.6, 0.4, 0.7))
    r.tube('hair', 'hair', (-1.6, 0, 24.4), (-2.4, 0, 17.2), 2.9, 2.4, squash=0.6)
    r.tube('hair_end', 'hair', (-2.4, 0, 17.2), (-2.8, 0, 13.4), 2.4, 1.3, squash=0.5)

    # Body: the greatcoat, open at the front over a white shirt, a red ribbon at the collar.
    r.ellipsoid('chest', 'scarf', (0.2, 0, 20.1), (2.0, 2.3, 0.95))
    r.tube('scarf', 'scarf', (-1.2, -0.8, 19.8), (-4.6, -0.8, 18.6), 0.85, 0.6)
    r.ellipsoid('chest', 'coat', (0.15, 0, 16.9), (1.9, 2.45, 3.1))
    r.ellipsoid('chest', 'trim', (1.45, 0, 17.7), (0.7, 0.9, 1.4))
    r.ellipsoid('chest', 'scarf', (1.9, 0, 18.9), (0.5, 0.6, 0.45))
    r.ellipsoid('spine', 'coat', (0.1, 0, 14.0), (1.7, 2.1, 1.6))
    r.tube('pelvis', 'skirt', (0.1, 0, 12.6), (0.2, 0, 10.2), 2.0, 2.6, caps=False)
    r.tube('pelvis', 'coat', (-0.2, 0, 13.4), (-0.8, 0, 8.4), 2.2, 3.2, squash=0.85, caps=False,
           cuts=[((0.6, 0, 10.5), (1.0, 0, 0.3))])
    for s, y in SIDES:
        r.ellipsoid('shoulder_' + s, 'coat', (0, 2.5 * y, 18.9), (1.2, 1.15, 1.2))
        r.tube('shoulder_' + s, 'coat', (0, 2.6 * y, 18.8), (0, 2.6 * y, 15.8), 0.95)
        r.tube('elbow_' + s, 'coat', (0, 2.6 * y, 15.8), (0, 2.6 * y, 13.4), 0.95, 1.35, caps=False)
        r.ellipsoid('elbow_' + s, 'trim', (0, 2.6 * y, 13.25), (1.35, 1.35, 0.4))
        r.ellipsoid('hand_' + s, 'skin', (0, 2.6 * y, 12.3), (0.75, 0.7, 0.85))
        r.tube('hip_' + s, 'tights', (0, 1.15 * y, 11.2), (0, 1.15 * y, 6.3), 1.05, 0.85)
        r.ellipsoid('knee_' + s, 'trim', (0, 1.15 * y, 6.4), (1.1, 1.1, 0.45))
        r.tube('knee_' + s, 'boot', (0, 1.15 * y, 6.3), (0, 1.15 * y, 1.5), 0.95, 0.8)
        r.ellipsoid('ankle_' + s, 'boot', (0.8, 1.15 * y, 0.85), (1.65, 0.85, 0.85))

    # The katana and the lance, in her right hand: along the arm when the hand is straight.
    r.tube('hand_R', 'grip', (0, -2.6, 13.3), (0, -2.6, 11.4), 0.38, group='sword')
    r.ellipsoid('hand_R', 'gold', (0, -2.6, 11.3), (0.9, 0.55, 0.28), group='sword')
    r.tube('hand_R', 'blade', (0, -2.6, 11.1), (0, -2.6, -3.0), 0.42, 0.18, group='sword')
    r.tube('hand_R', 'shaft', (0, -2.6, 21.0), (0, -2.6, -2.6), 0.36, group='lance')
    r.ellipsoid('hand_R', 'gold', (0, -2.6, -2.6), (0.6, 0.6, 0.35), group='lance')
    r.tube('hand_R', 'blade', (0, -2.6, -2.6), (0, -2.6, -5.8), 0.7, 0.05, caps=False, group='lance')

    # At home (assets/mitsuki/home, her prologue): her blanket, over her lying and over her legs
    # sitting up; her phone on the bed (dark, or ringing and buzzing) and in her hand. The
    # bed's things hang on `prop`, a joint of its own that never turns: they stay where the
    # bed is whatever she does.
    r.material('screen', [GOLD[1]], weight=6, min_cover=0.3, bands=[], keep_inner=True)
    r.material('phone', BOOT[:1], weight=4, min_cover=0.3, bands=[])
    r.joint('prop', None, (0, 0, 0))
    r.box('pelvis', 'blanket', (1.4, 0, 6.5), (6.6, 11, 18), group='blanket')
    r.ellipsoid('pelvis', 'blanket', (1.4, 0, 15.2), (3.3, 5.6, 2.8), group='blanket')
    r.ellipsoid('pelvis', 'blanket', (1.6, 0, -2.2), (3.4, 5.6, 2.2), group='blanket')
    r.box('pelvis', 'blanket', (6.8, 0, 14.4), (12, 11, 2.2), group='blanket_legs')
    r.ellipsoid('pelvis', 'blanket', (12.6, 0, 14.2), (2.2, 5.6, 2.0), group='blanket_legs')
    px, py, pz = BED_PHONE
    for g, dx in (('phone_bed', 0.0), ('ring_a', -0.5), ('ring_b', 0.5)):
        r.box('prop', 'phone', (px + dx, py, pz), (4.6, 2.6, 0.8), group=g)
        if g != 'phone_bed':
            r.box('prop', 'screen', (px + dx, py - 0.2, pz + 0.45), (3.8, 2.0, 0.2), group=g)
    r.box('hand_R', 'phone', (0.6, -3.4, 11.8), (1.0, 0.7, 3.6), group='phone_hand')
    r.box('hand_R', 'screen', (0.6, -3.8, 11.8), (0.7, 0.2, 3.0), group='phone_hand')

    ps = poses()
    if not getattr(r, 'is_stand', False):
        bench(r, ps)
    for p in ps:
        r.pose(**p)
    r.portrait(0, (0.9, 0, 23.6), 3.3, yaw=58.0, pitch=4.0, show=('far_eye',))
    # Wearing the chain mail (assets/test_room/data/armour.bmdata): the coat in steel.
    r.palette('steel', [(COAT[0], (64, 70, 86)), (COAT[1], (110, 118, 138)), (COAT[2], (164, 172, 190))])
    return r


def bench(r, ps):
    """Her frames on the bench (55 holding Hina's hand in her lap, 59 Hina's head in her lap,
    60 and 61 stroking her hair) from the scene the close-up of their hands is drawn from
    too (assets/mitsuki/park/pair.py): Hina there as a holdout (hiding what of Mitsuki is
    behind her: she is drawn by her own sprite); Mitsuki's head turned to her.
    """
    import os
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'park'))
    import pair
    pair.add_hina(r, 'h_', material=pair.holdout(r), group=lambda g: 'pair_hina' if g in ('body', 'far_eye', 'lap') else None)
    m = lambda n: n  # noqa: E731
    h = lambda n: 'h_' + n  # noqa: E731
    solved = pair.solve(r, m, h)
    pair.shrink(r, ['pelvis', 'h_pelvis'])
    for i, js in zip((55, 59, 60, 61), pair.steps(r, solved['joints'], m, h)):
        p = pose(view=FRONT, show=('far_eye', 'pair_hina'), root=(0, 0, pair.SIT_ROOT), hair=4, scarf=6, **js)
        if i < len(ps):
            ps[i] = p
        else:
            ps.append(p)


# --- Poses: one for each frame of the layout, in its order ------------------------------

IDLE = {'shoulder_R': (-4, -8), 'shoulder_L': (4, 8), 'elbow_R': 12, 'elbow_L': 12, 'head': -3}


def pose(base=IDLE, root=(0, 0, 0), show=(), view=None, closed=False, **joints):
    j = dict(base)
    j.update(joints)
    show = tuple(show) + (('closed',) if closed else ('eyes',))
    return {'joints': j, 'root': root, 'show': show, 'view': view}


def mirror(p):
    """The same pose with right and left swapped (spread mirrored)."""
    j = {}
    for k, v in p['joints'].items():
        k2 = k[:-1] + {'R': 'L', 'L': 'R'}[k[-1]] if k[-2:] in ('_R', '_L') else k
        if isinstance(v, tuple):
            v = (v[0], -v[1]) + tuple(v[2:])
        j[k2] = v
    return {'joints': j, 'root': p['root'], 'show': p['show'], 'view': p['view']}


def swing(total, elbow=10, hand=0):
    """The arm's joints so the hand points `total` degrees from hanging down (90: ahead).
    Raised above ahead the arm comes out to the side as well, clear of her hair."""
    spread = -min(28.0, max(0.0, total - 95) * 0.35) - 6
    return {'shoulder_R': (total - elbow - hand, spread), 'elbow_R': elbow, 'hand_R': hand}


# Camera angles of their own (rig.py, pose(view=...)): from the front and from behind.
FRONT = (90.0, 8.0)
BACK = (-90.0, 8.0)

# Her bed at home (assets/mitsuki/home/home.py): the frames from 43 on are drawn from where she
# stands in front of it (the room's PlayerStart, x 46), from the room's own angle (yaw 0,
# pitch 14), so she lies on its mattress. HOME_X: the room's x of that place; BED_Y: the
# middle of the bed, away from the viewer; MATTRESS: its top with the sheet.
HOME = (0.0, 14.0)
HOME_X = 46.0
BED_Y = 30.0
MATTRESS = 12.6
BED_PHONE = (42.0 - HOME_X, 18.0, MATTRESS + 0.3)   # on the sheet at the front, by her feet

STANCE = {'hip_R': 25, 'knee_R': -20, 'hip_L': -20, 'knee_L': -10}
LUNGE = {'hip_R': 38, 'knee_R': -30, 'hip_L': -30, 'knee_L': -5}
TUCK = {'hip_R': 70, 'knee_R': -100, 'hip_L': 50, 'knee_L': -90, 'scarf': -20, 'hair': -15}


def poses():
    out = []
    # 0, 1: idle, at ease: both knees a little bent (the thighs nearly upright, the shins
    # slanting back to the feet, flat; the hips lower by as much), the front knee more,
    # turned a little
    # toward the viewer, the head tilted, one hand loose at her side, the other behind her;
    # breathing (the hair and the scarf settle a little).
    rest = dict(spine=(2, 0, -10), chest=(-1, 0, -4), head=(-5, -7, 6),
                hip_R=12, knee_R=-32, ankle_R=20, hip_L=6, knee_L=-24, ankle_L=18,
                shoulder_R=(-6, -10), elbow_R=22, hand_R=10, shoulder_L=(-20, 6), elbow_L=45,
                hair=2, scarf=3)
    out.append(pose(root=(0, 0, -0.4), **rest))
    rest.update(elbow_R=26, hand_R=14, elbow_L=48, hair=4, hair_end=2, scarf=6)
    out.append(pose(root=(0, 0, -0.4), **rest))
    # 2-5: run.
    stride = pose(spine=-8, head=4, hip_R=40, knee_R=-25, ankle_R=10, hip_L=-35, knee_L=-40, ankle_L=-10,
                  shoulder_R=(-40, -8), elbow_R=60, shoulder_L=(40, 8), elbow_L=60, hair=-25, hair_end=-10, scarf=-15)
    passing = pose(root=(0, 0, 0.5), spine=-8, head=4, hip_R=0, knee_R=-5, hip_L=25, knee_L=-80, ankle_L=-15,
                   shoulder_R=(-10, -8), elbow_R=50, shoulder_L=(10, 8), elbow_L=50, hair=-20, hair_end=-14, scarf=-22)
    out += [stride, passing, mirror(stride), mirror(passing)]
    # 6: jump, 7: fall.
    out.append(pose(spine=-3, hip_R=70, knee_R=-90, ankle_R=20, hip_L=20, knee_L=-50,
                    shoulder_L=(165, 8), elbow_L=15, shoulder_R=(-25, -8), elbow_R=40, hair=12, scarf=10))
    out.append(pose(hip_R=25, knee_R=-35, hip_L=-15, knee_L=-40, shoulder_R=(50, -30), elbow_R=30,
                    shoulder_L=(60, 30), elbow_L=30, hair=-60, hair_end=-25, scarf=-40))
    # 8, 9: climb, seen from behind (facing the ladder).
    # Her arms reach up and out to the rungs, past her hair.
    climb = pose(view=BACK, shoulder_L=(160, 42), elbow_L=10, shoulder_R=(120, -38), elbow_R=35, hip_R=(60, -8), knee_R=-95,
                 hip_L=(5, 6), knee_L=-10, head=8)
    out += [climb, mirror(climb)]
    # 10-12: the sword's swing: raised behind, ahead, down ahead.
    out.append(pose(show=('sword',), spine=4, **STANCE, **swing(240, elbow=40, hand=30)))
    out.append(pose(show=('sword',), spine=-6, **LUNGE, **swing(90, elbow=0, hand=-5), scarf=-15, hair=-12))
    out.append(pose(show=('sword',), spine=-10, **LUNGE, **swing(50, elbow=5, hand=-10), scarf=-8, hair=-6))
    # 13, 14: the lance up, drawn back then thrust.
    out.append(pose(show=('lance',), **STANCE, **swing(180, elbow=60, hand=-30)))
    out.append(pose(show=('lance',), spine=3, hip_R=10, knee_R=-8, hip_L=-10, knee_L=-5, **swing(180, elbow=0, hand=2)))
    # 15, 16: the lance down, in the air.
    out.append(pose(show=('lance',), **TUCK, **swing(0, elbow=50, hand=-80)))
    out.append(pose(show=('lance',), **TUCK, **swing(0, elbow=0, hand=-8)))
    # 17, 18: the low sweep, crouching.
    crouch = {'hip_R': 70, 'knee_R': -85, 'hip_L': -5, 'knee_L': -80, 'ankle_L': 20, 'spine': -15, 'head': 10}
    out.append(pose(show=('sword',), root=(0, 0, -2.5), **crouch, **swing(10, elbow=20, hand=20)))
    out.append(pose(show=('sword',), root=(0, 0, -3.0), **crouch, **swing(90, elbow=0, hand=60), scarf=-15))
    # 19-22: the arc upwards (the blade at -20, 35, 90, 145 degrees from ahead).
    for a, lean in ((-20, -4), (35, 0), (90, 3), (145, 6)):
        out.append(pose(show=('sword',), spine=lean, **STANCE, **swing(a + 90)))
    # 23-26: the arc downwards, in the air (30, -35, -90, -145).
    for a in (30, -35, -90, -145):
        out.append(pose(show=('sword',), **TUCK, **swing(a + 90)))
    # 27: the lance ahead.
    out.append(pose(show=('lance',), spine=-10, shoulder_L=(-30, 8), **LUNGE, **swing(90, elbow=0), scarf=-15, hair=-12))
    # 28-31: pushing.
    push = {'spine': -18, 'head': 14, 'shoulder_R': (78, -6), 'shoulder_L': (78, 6), 'elbow_R': 20, 'elbow_L': 20}
    p1 = pose(**push, hip_R=30, knee_R=-35, hip_L=-30, knee_L=-10)
    p2 = pose(**push, root=(0, 0, 0.4), hip_R=0, knee_R=-10, hip_L=15, knee_L=-50)
    out += [p1, p2, mirror(p1), mirror(p2)]
    # 32-40: aiming, from straight up (192) over ahead (0) to straight down (64).
    for a in (192, 208, 224, 240, 0, 16, 32, 48, 64):
        t = 90 - a * 360 / 256
        t = (t + 180) % 360 - 180
        out.append(pose(**swing(t if t != -180 else 180, elbow=0)))
    # 41, 42: sitting on a bench, seen from the front, her right leg crossed over her left (her
    # feet do not reach the ground), her hands in her lap; still, breathing.
    # Both eyes show from the front (the far one is hidden in the side views).
    sit = dict(hip_R=(102, 30), knee_R=(-78, 28), ankle_R=10, hip_L=(86, -6), knee_L=-88, shoulder_R=(26, 18), elbow_R=55,
               shoulder_L=(26, -18), elbow_L=55, hair=4, scarf=6, head=-4)
    out.append(pose(view=FRONT, show=('far_eye',), root=(0, 0, 3.0), **sit))
    sit.update(spine=1, knee_R=(-72, 28), elbow_R=58, elbow_L=58)
    out.append(pose(view=FRONT, show=('far_eye',), root=(0, 0, 3.0), **sit))
    # 43, 44: asleep in her bed at home, on her back under the blanket, her head on the pillow,
    # the phone dark beside it; breathing. 45, 46: the phone rings and buzzes. 47, 48: sitting
    # up, the phone at her ear. 49: sitting on the bed's edge, seen from the front, about to
    # get up (the next frame: standing there).
    lie = dict(pelvis=88, spine=-4, chest=-4, head=-8, hip_R=2, hip_L=2, shoulder_R=(4, -4), shoulder_L=(4, 4),
               elbow_R=6, elbow_L=6, hair=172, hair_end=8, scarf=90)
    at = (33.0 - HOME_X, BED_Y, MATTRESS - 0.5 - 11.6)
    for k, ph in enumerate(('phone_bed', 'phone_bed', 'ring_a', 'ring_b')):
        breath = dict(lie)
        if k == 1:
            breath.update(chest=-6, head=-9)
        out.append(pose(view=HOME, closed=True, show=('blanket', ph), root=at, **breath))
    call = dict(spine=-4, chest=-2, head=4, hip_R=88, hip_L=88, knee_R=-4, knee_L=-4, ankle_R=-30, ankle_L=-30,
                shoulder_R=(150, -20), elbow_R=150, hand_R=-20, shoulder_L=(20, 6), elbow_L=30, hair=-4, scarf=-10)
    seat = (35.0 - HOME_X, BED_Y - 4.0, MATTRESS + 1.2 - 11.6)
    out.append(pose(view=HOME, show=('blanket_legs', 'phone_hand'), root=seat, **call))
    call.update(head=8, spine=-3, chest=-3)
    out.append(pose(view=HOME, show=('blanket_legs', 'phone_hand'), root=seat, **call))
    edge = dict(hip_R=(88, 4), knee_R=-84, ankle_R=12, hip_L=(80, -4), knee_L=-70, ankle_L=10, shoulder_R=(20, 22),
                elbow_R=20, shoulder_L=(20, -22), elbow_L=20, head=-6, hair=2, scarf=4)
    out.append(pose(view=FRONT, show=('far_eye',), root=(-15.0, 0, MATTRESS + 1.0 - 11.6), **edge))
    # 50: reaching ahead to the light switch on the wall by the bed, leaning in a little.
    out.append(pose(spine=(-6, 0, -6), head=(2, -4, 4), hip_R=10, knee_R=-14, ankle_R=4, hip_L=-6, knee_L=-10,
                    **swing(96, elbow=4, hand=-6), shoulder_L=(-12, 6), elbow_L=30, hair=2, scarf=2))
    # The park (assets/mitsuki/park), seen from the side until they sit: 51 holding Hina, her
    # arms around her (Hina's own sprite curled against her, behind); 52 a hand on her head (a
    # pat); 53, 54 walking, leading her by the hand behind her (her near arm back); 55 on the
    # bench, seen from the front, Hina close at her side (8 to her left: their shoulders touch),
    # her left hand reaching over into Hina's lap, on hers (the arm found in the scene of both,
    # assets/mitsuki/park/bench.py); 56
    # reaching out after her, too late.
    hug = dict(spine=-10, chest=-6, head=16, shoulder_R=(84, 12), elbow_R=36, hand_R=20, shoulder_L=(80, -12), elbow_L=40,
               hand_L=20, hip_R=8, knee_R=-16, hip_L=-4, knee_L=-10, hair=2, scarf=4)
    out.append(pose(**hug))
    rock = dict(hug)
    hug.update(shoulder_R=(124, -4), elbow_R=24, hand_R=-40, head=18)
    out.append(pose(**hug))
    for k in (1, -1):
        out.append(pose(spine=-4, head=2, hip_R=26 * k, knee_R=-20 if k > 0 else -36, hip_L=-26 * k,
                        knee_L=-36 if k > 0 else -20, shoulder_R=(-48, -6), elbow_R=6, hand_R=10,
                        shoulder_L=(18 * k, 8), elbow_L=30, hair=-8, scarf=-10))
    sit_hand = dict(hip_R=(102, 30), knee_R=(-78, 28), ankle_R=10, hip_L=(86, -6), knee_L=-88, shoulder_R=(26, 18), elbow_R=55,
                    shoulder_L=(65, 35), elbow_L=0, hand_L=20, spine=(0, 4, 0), hair=4, scarf=6, head=(-4, 6, -8))
    out.append(pose(view=FRONT, show=('far_eye',), root=(0, 0, 3.0), **sit_hand))
    out.append(pose(spine=-14, head=6, hip_R=34, knee_R=-30, hip_L=-24, knee_L=-8, **swing(100, elbow=0, hand=-4),
                    shoulder_L=(-30, 8), elbow_L=30, hair=-14, scarf=-18))
    # 57, 58: rocking her: the whole hold leaning toward Hina (forward) and away (back), about
    # her feet; Hina's own frames lean the same way in the world (hina.py, 12 and 13).
    for lean in (5, -5):
        r2 = dict(rock)
        r2.update(pelvis=lean, spine=rock['spine'] + lean // 2, hip_R=rock['hip_R'] - lean, hip_L=rock['hip_L'] - lean,
                  hair=rock['hair'] - lean, scarf=rock['scarf'] - lean)
        out.append(pose(**r2))
    return out
