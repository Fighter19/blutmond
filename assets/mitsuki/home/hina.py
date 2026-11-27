# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Hina, Mitsuki's friend, who calls her in the night (the prologue, assets/mitsuki/home): only
her portrait, for the dialogue box (hina_face). Mitsuki's opposite: chestnut hair in two
bouncing tails, wide green eyes, eyebrows up, mouth open, a drop of sweat, a yellow hoodie,
the phone pressed to her ear. Modelled as Mitsuki is (mitsuki.py: the same head, so the two
portraits match) and posed once.

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/home/hina.py \\
      --layout assets/mitsuki/home/hina_layout.bmsprite --out assets/mitsuki/home --name hina \\
      --face-only yes
"""

import math

OUT = (24, 20, 36)
HAIR = [(92, 52, 40), (150, 94, 60), (204, 148, 92)]
SKIN = [(226, 166, 150), (255, 226, 208)]
WHITE = [(255, 250, 240)]
GREEN = [(28, 104, 72), (92, 200, 124)]
HOODIE = [(176, 118, 34), (240, 196, 76)]
MOUTH = [(120, 16, 40)]
SWEAT = [(150, 204, 255)]
PHONE = [(30, 32, 62)]
SIDES = (('R', -1), ('L', 1))


def build(Rig):
    r = Rig('hina')
    r.view = {'yaw': 22.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 3.0
    r.material('hair', HAIR, bands=[0.35, 0.8])
    r.material('skin', SKIN, bands=[0.3])
    r.material('sclera', WHITE, weight=7, min_cover=0.15, bands=[], keep_inner=True)
    r.material('eye', [GREEN[1]], weight=8, min_cover=0.12, bands=[], keep_inner=True)
    r.material('pupil', [GREEN[0]], weight=9, min_cover=0.12, bands=[], keep_inner=True)
    r.material('shine', WHITE, weight=10, min_cover=0.5, bands=[], keep_inner=True)
    r.material('lash', [OUT], weight=7, min_cover=0.15, bands=[], keep_inner=True)
    r.material('mouth', MOUTH, weight=8, min_cover=0.2, bands=[], keep_inner=True)
    r.material('sweat', SWEAT, weight=6, min_cover=0.3, bands=[], keep_inner=True)
    r.material('hoodie', HOODIE, bands=[0.5])
    r.material('phone', PHONE, weight=3, min_cover=0.3, bands=[])

    r.joint('chest', None, (0, 0, 16.0))
    r.joint('head', 'chest', (0.2, 0, 20.2))
    r.joint('tail_R', 'head', (-0.6, -3.6, 25.0))
    r.joint('tail_L', 'head', (-0.6, 3.6, 25.0))
    r.joint('arm', 'chest', (0, -2.6, 19.0))

    r.ellipsoid('head', 'skin', (0.5, 0, 23.9), (3.3, 3.4, 3.5))
    r.ellipsoid('head', 'skin', (1.2, 0, 22.4), (2.3, 2.5, 1.9))
    r.tube('head', 'skin', (0.3, 0, 20.0), (0.4, 0, 21.6), 0.85)
    turn = 37.0
    for s, y in SIDES:
        nx, ny = math.cos(math.radians(turn)), math.sin(math.radians(turn)) * y
        tx, ty = math.sin(math.radians(turn)), -math.cos(math.radians(turn)) * y

        def on_face(out, z, inward=0.0):
            return (3.05 + out * nx + inward * tx, 1.95 * y + out * ny + inward * ty, z)
        gaze = 0.3 if y < 0 else 0.45
        # Wide open: taller than Mitsuki's, the lash high above, a brow higher still.
        r.ellipsoid('head', 'sclera', on_face(0.0, 23.1), (0.38, 0.9, 1.35), rot=(0, 0, turn * y))
        r.ellipsoid('head', 'eye', on_face(0.14, 23.0, gaze), (0.34, 0.5, 0.8), rot=(0, 0, turn * y))
        r.ellipsoid('head', 'pupil', on_face(0.26, 22.95, gaze + 0.04), (0.26, 0.24, 0.36), rot=(0, 0, turn * y))
        r.ellipsoid('head', 'shine', on_face(0.36, 23.3, gaze - 0.1), (0.1, 0.14, 0.17), rot=(0, 0, turn * y))
        r.ellipsoid('head', 'lash', on_face(0.18, 24.45), (0.42, 0.95, 0.2), rot=(10 * y, 0, turn * y))
        r.ellipsoid('head', 'lash', on_face(0.25, 25.6, -0.1), (0.3, 0.75, 0.14), rot=(-12 * y, 0, turn * y))
        # Bangs over the temples, and a tail tied high on each side, bouncing out.
        r.tube('head', 'hair', (0.6, 3.2 * y, 25.0), (0.9, 3.1 * y, 21.6), 0.75, 0.5)
        r.ellipsoid('tail_' + s, 'hair', (-0.6, 3.8 * y, 25.0), (1.0, 0.8, 1.0))
        r.tube('tail_' + s, 'hair', (-1.0, 4.3 * y, 25.0), (-3.2, 6.8 * y, 19.0), 1.6, 0.6)
    r.ellipsoid('head', 'mouth', (3.55, -0.9, 21.3), (0.3, 0.75, 0.6), rot=(0, 0, -30))
    r.ellipsoid('head', 'sweat', (2.0, -3.2, 26.0), (0.4, 0.4, 0.7))
    r.ellipsoid('head', 'hair', (0.0, 0, 24.7), (3.9, 3.9, 4.0), cuts=[((2.7, 0, 25.4), (0.7, 0, -0.6))])
    r.tube('head', 'hair', (2.6, -1.6, 26.6), (3.5, -0.6, 25.0), 0.7, 0.3)    # a few strands across
    r.tube('head', 'hair', (2.8, 1.4, 26.6), (3.5, 0.6, 25.2), 0.6, 0.3)
    r.tube('chest', 'hoodie', (0, 0, 19.4), (0, 0, 13.0), 2.4, 2.8)
    r.ellipsoid('chest', 'hoodie', (-1.0, 0, 20.0), (2.0, 3.0, 1.3))
    r.tube('chest', 'skin', (0, 0, 19.4), (0, 0, 20.6), 0.7)
    # Her right hand at her ear, holding the phone.
    r.tube('arm', 'hoodie', (0, -2.6, 19.0), (1.6, -3.4, 15.6), 1.0)
    r.tube('arm', 'hoodie', (1.6, -3.4, 15.6), (1.4, -3.8, 21.0), 1.0)
    r.ellipsoid('arm', 'skin', (1.3, -3.9, 21.8), (0.8, 0.7, 0.9))
    r.box('arm', 'phone', (0.9, -3.7, 23.2), (2.0, 0.5, 3.4), rot=(0, 20, 0))

    r.pose(joints={'tail_R': (0, -20), 'tail_L': (0, 20), 'head': (-4, 0, -6)})
    r.portrait(0, (0.9, 0, 23.6), 3.3, yaw=58.0, pitch=4.0)
    return r
