# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""The nightmare that takes Hina in the park (assets/mitsuki/park, park.ms): a tall, stooped
shadow in a ragged cloak, pale eyes, one long finger at where its lips would be (hush), long
arms with claws. Drawn as it is in the lit park (every material emissive: no light falls on
a shadow).

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/park/nightmare.py \\
      --layout assets/mitsuki/park/nightmare_layout.bmsprite --sink 2 --out assets/mitsuki/park --name nightmare

Frames: 0 rising out of the ground (its head and eyes), 1 looming, hushing, 2 reaching, 3
holding (Hina's own sprite in its hand), 4 leaping away.
"""

OUT = (6, 4, 10)
SHADE = [(8, 6, 14), (18, 14, 30), (32, 26, 50)]
EYES = [(236, 232, 255)]
CLAW = [(70, 60, 96)]

SIDES = (('R', -1), ('L', 1))


def build(Rig):
    r = Rig('nightmare')
    r.view = {'yaw': 22.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 4.0
    r.material('shade', SHADE, bands=[0.4, 0.85], emissive=True)
    r.material('eyes', EYES, weight=9, min_cover=0.15, bands=[], keep_inner=True, emissive=True)
    r.material('claw', CLAW, weight=4, min_cover=0.2, bands=[], keep_inner=True, emissive=True)
    r.joint('root', None, (0, 0, 0))
    r.joint('spine', 'root', (0, 0, 18))
    r.joint('chest', 'spine', (0, 0, 30))
    r.joint('head', 'chest', (2, 0, 38))
    for s, y in SIDES:
        r.joint('shoulder_' + s, 'chest', (0, 4.5 * y, 36))
        r.joint('elbow_' + s, 'shoulder_' + s, (0, 4.5 * y, 27))
        r.joint('hand_' + s, 'elbow_' + s, (0, 4.5 * y, 18))
    r.locator('hand', 'hand_R', (0, -4.5, 14))
    # The cloak: wide and ragged at the ground, narrow at the shoulders; tatters.
    r.tube('root', 'shade', (0, 0, 1), (0, 0, 18), 9, 6, caps=False, group='body')
    for k in range(7):
        a = -12 + k * 4
        r.ellipsoid('root', 'shade', (a * 0.4, (k % 3 - 1) * 3, 1.5), (2.2, 2.2, 3.5 + (k % 2) * 2), group='body')
    r.tube('spine', 'shade', (0, 0, 18), (0.5, 0, 31), 6, 5, group='body')
    r.ellipsoid('chest', 'shade', (1, 0, 33), (4.5, 5.5, 4.5), group='body')
    # The head: long, bowed, a hood's point; two pale eyes; no mouth to speak of.
    r.ellipsoid('head', 'shade', (3, 0, 41), (4.0, 3.6, 5.0), group='head')
    r.tube('head', 'shade', (1.0, 0, 44), (-4, 0, 49), 2.4, 0.6, group='head')
    for y in (-1, 1):
        r.ellipsoid('head', 'eyes', (6.6, 1.6 * y, 42), (0.5, 0.9, 0.6), rot=(0, 0, 30 * y), group='head')
    # Arms, long, ending in claws.
    for s, y in SIDES:
        r.tube('shoulder_' + s, 'shade', (0, 4.5 * y, 36), (0, 4.5 * y, 27), 1.6, 1.3, group='body')
        r.tube('elbow_' + s, 'shade', (0, 4.5 * y, 27), (0, 4.5 * y, 18), 1.3, 1.0, group='body')
        r.ellipsoid('hand_' + s, 'shade', (0, 4.5 * y, 17), (1.4, 1.2, 1.8), group='body')
        for f in (-1, 0, 1):
            r.tube('hand_' + s, 'claw', (0.5 * f, 4.5 * y + 0.5 * f, 16), (1.2 * f, 4.5 * y + 0.8 * f, 11), 0.5, 0.15, group='body')

    for p in poses():
        r.pose(**p)
    return r


def pose(root=(0, 0, 0), show=('head', 'body'), **joints):
    return {'joints': joints, 'root': root, 'show': show, 'view': None}


def poses():
    hush = dict(spine=-10, chest=-14, head=-16, shoulder_R=(110, -20), elbow_R=95, hand_R=20,
                shoulder_L=(10, 10), elbow_L=20)
    return [
        pose(root=(0, 0, -30), show=('head',), **hush),       # 0: rising: its head out of the ground
        pose(**hush),                                         # 1: looming, a finger at its lips
        # 2: reaching for her: drawn as holding (3) is, its frame here empty (the park's sprite
        # memory: the dialogue's portraits need room beside it).
        pose(show=(), spine=-24, chest=-16, head=-6, shoulder_R=(96, -10), elbow_R=4, hand_R=-20,
             shoulder_L=(60, 16), elbow_L=30),
        pose(spine=-6, chest=-6, head=4, shoulder_R=(70, -6), elbow_R=40, hand_R=-30,
             shoulder_L=(40, 16), elbow_L=50),                # 3: holding her up
        pose(root=(0, 0, 6), spine=-40, chest=-20, head=-10, shoulder_R=(140, -10), elbow_R=30,
             shoulder_L=(160, 10), elbow_L=20),               # 4: leaping away
    ]
