# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""The snowman in the snowy park (Mitsuki's story: snowman.ms): three balls of snow, coal eyes,
a carrot nose, stick arms, a red scarf, and in his hands before his chest a Christmas ball:
clear glass, dark inside with a faint violet glow (where Hina is), a silver cap and ring.
Grabbed at the ball he holds on, grows and fights; beaten, he is a happy snowman again.

One size of him: grown, the engine draws him larger (set_scale 170: affine sprites; his boxes
in the sheet made as large, snowman_sheet.py). He is built twice (the rig has no groups per
pose for the faces of both), as built (s) and as the boss (b). snowman_sheet.py then makes the
beam's middle the same all along. (One sheet for him and his fight's things, the park's sprite
memory: the pack holds 28 sprites, all used.)

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/park/snowman.py \\
      --layout assets/mitsuki/park/snowman_layout.bmsprite --sink 2 --out assets/mitsuki/park --name snowman
  python3 assets/mitsuki/park/snowman_sheet.py assets/mitsuki/park/sources/snowman

Frames: 0 standing still (a straight coal mouth), 1 holding on (his sticks tight round the ball), 2
happy (small, smiling); the boss: 3 standing, 4 throwing (a stick arm back), 5 in a jump (arms
up), 6 his frost breath (the frost pouring from his mouth to the ground before him); then the
fight's things. Struck, he flashes (the engine's).
"""

OUT = (24, 20, 36)
SNOW = [(150, 170, 200), (205, 220, 240), (250, 252, 255)]
COAL = [(30, 28, 36)]
SCARF = [(150, 30, 44), (210, 60, 70)]
INSIDE = [(36, 16, 52), (110, 56, 150)]   # the Christmas ball's inside: dark, a faint violet glow (Hina there)
CAP = [(220, 180, 80)]
STICK = [(100, 70, 44)]
CARROT = [(230, 120, 36)]
ICE = [(110, 170, 225), (190, 230, 255)]
SILVER = [(96, 100, 120), SNOW[0]]      # the ball's cap (darker than the snow it is seen against)

SIZE = 1.0          # both of him (grown: drawn at 170 %, snowman.ms)
SMALL, BIG = SIZE, SIZE
SIZES = (('s', SMALL), ('b', BIG))
SIDES = (('R', -1), ('L', 1))
BEAM = 32           # the frost beam's frames: 1 to BEAMS times this long (pixels; a frame is at
BEAMS = 4           # most 128: a longer beam is two, one beyond the other)


def build(Rig):
    r = Rig('snowman')
    # (Turned further to the viewer than the characters: his face toward us.)
    r.view = {'yaw': 60.0, 'pitch': 10.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 3.0
    r.material('snow', SNOW, bands=[0.35, 0.75])
    r.material('coal', COAL, weight=8, min_cover=0.15, bands=[], keep_inner=True)
    # The Christmas ball: clear glass (its rim, light blue; a shine), dark inside, a silver cap.
    r.material('glass', ICE + [SNOW[2]], weight=6, min_cover=0.15, bands=[0.4, 0.8], keep_inner=True, emissive=True)
    r.material('inside', INSIDE, weight=7, min_cover=0.2, bands=[0.5], keep_inner=True, emissive=True)
    r.material('silver', SILVER, weight=8, min_cover=0.1, bands=[0.5], keep_inner=True)
    r.material('cap', CAP, weight=7, min_cover=0.2, bands=[], keep_inner=True)
    r.material('shine', [SNOW[2]], weight=9, min_cover=0.1, bands=[], keep_inner=True, emissive=True)
    r.material('carrot', CARROT, weight=7, min_cover=0.2, bands=[], keep_inner=True)
    r.material('stick', STICK, weight=3, min_cover=0.15, bands=[])
    r.material('scarf', SCARF, bands=[0.5])
    # (The fight's things: what strikes has a material of its own, its hit box.)
    r.material('ice', ICE + [SNOW[2]], bands=[0.4, 0.8])
    r.material('frost', ICE + [SNOW[2]], bands=[0.3, 0.7], emissive=True)
    r.material('dust', SNOW, bands=[0.35, 0.75])
    r.joint('ground', None, (0, 0, 0))
    for name, k in SIZES:
        man(r, name, k)
    things(r)
    for p in poses():
        r.pose(**p)
    return r


def man(r, n, k):
    """One snowman, `k` times the size, its joints and groups named with `n` (s, b)."""
    def j(name):
        return name + '_' + n

    def at(x, y, z):
        return (x * k, y * k, z * k)
    r.joint(j('base'), 'ground', (0, 0, 0))
    r.joint(j('body'), j('base'), at(0, 0, 14))
    r.joint(j('head'), j('body'), at(0, 0, 24))
    for s, y in SIDES:
        r.joint(j('arm_' + s), j('body'), at(0, 5.0 * y, 21))
    g = n
    # The snow: a big ball, a middle one, the head.
    r.ellipsoid(j('base'), 'snow', at(0, 0, 8), at(8.2, 8.2, 7.8), group=g)
    r.ellipsoid(j('body'), 'snow', at(0, 0, 18.5), at(6.2, 6.2, 5.8), group=g)
    r.ellipsoid(j('head'), 'snow', at(0, 0, 28), at(4.7, 4.7, 4.6), group=g)
    # Coal buttons, the scarf.
    for z in (16.5, 19.5):
        r.ellipsoid(j('body'), 'coal', at(5.8, 0, z), at(0.6, 0.7, 0.7), group=g)
    r.tube(j('body'), 'scarf', at(0, 0, 23.6), at(0, 0, 22.4), 4.6 * k, 4.8 * k, caps=False, group=g)
    r.tube(j('body'), 'scarf', at(2.5, -3.0, 23.0), at(3.5, -3.6, 18.0), 1.1 * k, 0.9 * k, group=g)
    # The Christmas ball, set into his forehead: red glass with a shine, its golden cap and hook.
    christmas_ball(r, j('body'), at, k, g)
    # The carrot nose.
    r.tube(j('head'), 'carrot', at(4.2, 0, 28.2), at(7.8, 0.3, 27.6), 0.85 * k, 0.12 * k, group=g)
    # Coal eyes; a mouth of coal under the nose: straight, angry (down at the ends), smiling, or
    # wide (the breath).
    for y in (-1, 1):
        r.ellipsoid(j('head'), 'coal', at(4.0, 2.1 * y, 29.4), at(0.6, 0.7, 0.8), group=g)
    for y in (-1.6, -0.55, 0.55, 1.6):
        flat = 25.4
        smile = 25.4 + (0.6 if abs(y) > 1 else 0.0)
        frown = 25.4 - (0.5 if abs(y) > 1 else 0.0)
        r.ellipsoid(j('head'), 'coal', at(4.2, y, flat), at(0.45, 0.45, 0.45), group=g + '_still')
        r.ellipsoid(j('head'), 'coal', at(4.2, y, smile), at(0.45, 0.45, 0.45), group=g + '_happy')
        r.ellipsoid(j('head'), 'coal', at(4.2, y, frown), at(0.45, 0.45, 0.45), group=g + '_angry')
    r.ellipsoid(j('head'), 'coal', at(4.1, 0, 25.2), at(0.7, 1.8, 1.2), group=g + '_roar')
    # His frost breath: from the mouth down to the ground before him, where the beam goes out
    # (toward the screen's right: (0.5, 0.866) in the view's yaw).
    r.tube('ground', 'frost', at(4.6, 1.0, 24.0), at(5.6, 5.5, 13.0), 0.9 * k, 1.9 * k, group=g + '_breath')
    r.tube('ground', 'frost', at(5.6, 5.5, 13.0), at(6.2, 10.5, 2.5), 1.9 * k, 2.6 * k, group=g + '_breath')
    # Stick arms with twig fingers.
    for s, y in SIDES:
        r.tube(j('arm_' + s), 'stick', at(0, 5.0 * y, 21), at(0, 5.0 * y, 12), 0.55 * k, 0.4 * k, group=g)
        for f in (-1, 1):
            r.tube(j('arm_' + s), 'stick', at(0, 5.0 * y, 13.5), at(1.2 * f, 5.0 * y + 0.8 * f, 11.0), 0.3 * k, 0.2 * k,
                   group=g)


BALL_AT = (8.2, 0.0, 16.5)   # the Christmas ball before his chest, in his hands
BALL_R = 4.2


def christmas_ball(r, joint, at, k, g):
    """The Christmas ball he holds: clear glass (a light blue rim, a shine up toward the light),
    dark inside with a faint violet glow and a little snow at its bottom, a silver cap and ring.
    What is inside lies toward the viewer (from (0.866, -0.5)), so the glass shows as a rim."""
    cx, cy, cz = BALL_AT
    rr = BALL_R

    def near(f, up=0.0, side=0.0):
        # From the middle toward the viewer by f radii, up and along the view's right by radii.
        return at(cx + rr * (0.866 * f + 0.5 * side), cy + rr * (-0.5 * f + 0.866 * side), cz + rr * up)
    r.ellipsoid(joint, 'glass', at(cx, cy, cz), (rr * k, rr * k, rr * k), group=g)
    r.ellipsoid(joint, 'inside', near(0.3), (0.78 * rr * k,) * 3, group=g)
    r.ellipsoid(joint, 'snow', near(0.45, -0.5), (0.62 * rr * k, 0.62 * rr * k, 0.25 * rr * k), group=g)
    r.ellipsoid(joint, 'shine', near(0.8, 0.45, -0.35), (0.2 * rr * k,) * 3, group=g)
    r.tube(joint, 'silver', at(cx, cy, cz + rr * 0.9), at(cx, cy, cz + rr * 1.25), 0.38 * rr * k, 0.36 * rr * k, group=g)
    r.tube(joint, 'silver', at(cx, cy - 0.3, cz + rr * 1.22), at(cx, cy + 0.3, cz + rr * 1.5), 0.08 * rr * k, group=g)


def things(r):
    """What the fight has (each its own frames in his sprite): a snowball that rolls (a twig in
    it turning), the ripple his stomp sends through the ground (snow thrown up as it goes), the
    pillar of ice it breaks out as (rising, then standing: cover from his breath), an icicle, his
    frost beam along the ground (BEAMS lengths: it reaches out, and stops at a pillar), and the
    shadow that leaves him."""
    r.joint('roll', 'ground', (0, 0, 5))
    r.ellipsoid('roll', 'dust', (0, 0, 5), (5, 5, 5), group='ball')
    r.tube('roll', 'stick', (0, -3.0, 6.5), (0, -5.6, 8.0), 0.5, 0.3, group='ball')
    # The ripple: a mound of broken snow, clods thrown up (two ways), ice glinting in it.
    for g, lift in (('ripple_a', 1.0), ('ripple_b', -1.0)):
        r.ellipsoid('ground', 'dust', (0, 0, 0.5), (3.5, 6.0, 3.0), group=g)
        for i, (x, z, s) in enumerate(((-4.5, 5.0, 1.8), (-1.5, 8.5, 1.6), (2, 6.0, 1.9), (4.5, 3.0, 1.5))):
            d = x + lift * (i % 2) * 1.5
            r.ellipsoid('ground', 'ice', (0.5 * d, 0.866 * d, z + lift * (1 - i % 2) * 1.5), (s, s, s), group=g)
    # The pillar: a cluster of ice spikes, breaking out of the ground (low), then standing.
    for g, h in (('pillar_low', 0.45), ('pillar', 1.0)):
        r.tube('ground', 'ice', (0, 0, 0), (0.3, 0, 40 * h), 4.6, 0.8, group=g)
        # (Beside it on the screen: along (0.5, 0.866), the view's right; within 16 pixels.)
        r.tube('ground', 'ice', (-1.5, -2.6, 0), (-1.9, -3.3, 26 * h), 2.6, 0.5, group=g)
        r.tube('ground', 'ice', (1.5, 2.6, 0), (1.9, 3.3, 22 * h), 2.5, 0.5, group=g)
        r.ellipsoid('ground', 'dust', (0, 0, 0.5), (4, 6.5, 2.0), group=g)
    # An icicle, flying (its point ahead).
    r.tube('ground', 'ice', (-7, 0, 4), (7, 0, 4), 2.6, 0.2, group='icicle')
    # The frost beam: its rows the same all along (the pieces repeat), a rounded end.
    for n in range(1, BEAMS + 1):
        length = BEAM * n
        r.tube('ground', 'frost', (1, 0, 4), (length - 5, 0, 4), 2.9, 2.9, caps=False, group='beam%d' % n)
        r.ellipsoid('ground', 'frost', (length - 5, 0, 4), (3, 2.9, 2.9), group='beam%d' % n)
    for g, sway in (('shade_a', 1.0), ('shade_b', -1.0)):
        r.ellipsoid('ground', 'coal', (0, 0, 16), (5.5, 6.0, 10.0), group=g)
        r.tube('ground', 'coal', (0, 0, 8), (0, 3.0 * sway, 1), 3.6, 0.5, group=g)
        for y in (-1, 1):
            r.ellipsoid('ground', 'cap', (4.8, 1.9 * y, 20), (0.6, 0.8, 0.7), group=g)


def thing(group, view=None, **joints):
    return {'joints': joints, 'root': (0, 0, 0), 'show': (group,), 'view': view}


def pose(size, face, root=(0, 0, 0), extra=(), **joints):
    js = {}
    for name, a in joints.items():
        js[name + '_' + size] = a
    return {'joints': js, 'root': root, 'show': (size, size + '_' + face) + tuple(extra), 'view': None}


SIDE = (0.0, 0.0)   # straight from the side: the beam's rows the same all along


def poses():
    hold = dict(arm_R=(62, 10), arm_L=(62, -10))          # his hands round the ball
    tight = dict(arm_R=(64, 16), arm_L=(64, -16))         # holding on (she grabs it)
    return [
        pose('s', 'still', **hold),                                                     # 0 still
        pose('s', 'angry', head=-6, **tight),                                           # 1 holding on
        pose('s', 'happy', **hold),                                                     # 2 happy
        pose('b', 'angry', head=-4, **hold),                                            # 3 the boss standing
        pose('b', 'angry', arm_R=(-60, -40), arm_L=(62, -10), body=6, head=-6),         # 4 throwing (one hand on it)
        pose('b', 'angry', head=-8, **tight),                                           # 5 in a jump
        pose('b', 'roar', extra=('b_breath',), body=4, head=8, **hold),                 # 6 frost breath
        # The fight's things: 7, 8 the snowball rolling; 9, 10 the ripple; 11, 12 the pillar
        # (rising, standing); 13 an icicle; 14, 15 the shadow; 16 on the frost beam, 1 to BEAMS
        # pieces long.
        thing('ball', roll=(0, 0, 0)), thing('ball', roll=(0, 120, 0)),
        thing('ripple_a'), thing('ripple_b'),
        thing('pillar_low'), thing('pillar'),
        thing('icicle', view=SIDE),
        thing('shade_a'), thing('shade_b'),
    ] + [thing('beam%d' % n, view=SIDE) for n in range(1, BEAMS + 1)]
