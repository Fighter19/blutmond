# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""The snowman in the snowy park (Mitsuki's story: snowman.ms): three balls of snow, coal eyes,
stick arms, a red scarf, and in the middle of his head, where a nose would be, a Christmas ball:
dark glass, deep red going violet, glowing faintly from inside (where Hina is). Grabbed at the
ball he holds on, grows and fights; beaten, he is a happy snowman again.

Two of him at two sizes (the rig has no scale per frame): as built (small) and grown by a third
(big: the boss); each frame shows one of them. (Few frames: the park's sprite memory.)

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/park/snowman.py \\
      --layout assets/mitsuki/park/snowman_layout.bmsprite --sink 2 --out assets/mitsuki/park --name snowman

Frames: 0 standing still (a straight coal mouth), 1 holding on (his sticks round the ball), 2
the boss standing (grown, angry; it also lands his stomps), 3 throwing (his stick arm back), 4
a stomp in the air, 5 his frost breath (head back, mouth wide), 6 happy (small, smiling); then
the fight's things. Struck, he flashes (the engine's).
"""

OUT = (24, 20, 36)
SNOW = [(150, 170, 200), (205, 220, 240), (250, 252, 255)]
COAL = [(30, 28, 36)]
BALL = [(52, 8, 40), (110, 24, 76), (190, 70, 150)]
CAP = [(220, 180, 80)]
STICK = [(84, 56, 36), (120, 84, 52)]
SCARF = [(150, 30, 44), (210, 60, 70)]

SIZES = (('s', 1.0), ('b', 1.3))
SIDES = (('R', -1), ('L', 1))


def build(Rig):
    r = Rig('snowman')
    # (Turned further to the viewer than the characters: his face, the ball, toward us.)
    r.view = {'yaw': 60.0, 'pitch': 10.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = OUT
    r.inner_lines = 3.0
    r.material('snow', SNOW, bands=[0.35, 0.75])
    r.material('coal', COAL, weight=8, min_cover=0.15, bands=[], keep_inner=True)
    r.material('ball', BALL, weight=6, min_cover=0.2, bands=[0.4, 0.8], keep_inner=True, emissive=True)
    r.material('cap', CAP, weight=7, min_cover=0.2, bands=[], keep_inner=True)
    r.material('stick', STICK, weight=3, min_cover=0.15, bands=[0.5])
    r.material('scarf', SCARF, bands=[0.5])
    # (The fight's things, in his colours: what strikes has a material of its own, its hit box.)
    r.material('ice', SNOW, bands=[0.35, 0.75])
    r.material('frost', SNOW, bands=[0.3, 0.7], emissive=True)
    r.joint('ground', None, (0, 0, 0))
    for name, k in SIZES:
        man(r, name, k)
    things(r)
    for p in poses():
        r.pose(**p)
    return r


def man(r, n, k):
    """One snowman, `k` times the size, its joints and groups named with `n` (s, m, b)."""
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
    # The Christmas ball where his nose would be: dark glass glowing, its golden cap in the snow.
    r.ellipsoid(j('head'), 'ball', at(3.9, 0, 27.6), at(2.5, 2.5, 2.5), group=g)
    r.tube(j('head'), 'cap', at(2.4, 0, 27.7), at(1.5, 0, 27.7), 0.9 * k, group=g)
    # Coal eyes; a mouth of coal under the ball: straight, angry, smiling, or wide (the breath).
    for y in (-1, 1):
        r.ellipsoid(j('head'), 'coal', at(4.0, 1.7 * y, 30.4), at(0.6, 0.7, 0.8), group=g)
        # Angry: brows of coal slanting down to the middle; happy: arches.
        r.tube(j('head'), 'coal', at(3.9, 2.6 * y, 32.2), at(4.2, 0.9 * y, 31.4), 0.35 * k, group=g + '_angry')
        r.tube(j('head'), 'coal', at(3.9, 2.4 * y, 31.4), at(4.1, 1.3 * y, 32.0), 0.3 * k, group=g + '_happy')
    for i, y in enumerate((-1.6, -0.55, 0.55, 1.6)):
        flat = 24.6
        smile = 24.6 + (0.6 if abs(y) > 1 else 0.0)
        frown = 24.6 - (0.5 if abs(y) > 1 else 0.0)
        r.ellipsoid(j('head'), 'coal', at(4.1, y, flat), at(0.45, 0.45, 0.45), group=g + '_still')
        r.ellipsoid(j('head'), 'coal', at(4.1, y, smile), at(0.45, 0.45, 0.45), group=g + '_happy')
        r.ellipsoid(j('head'), 'coal', at(4.1, y, frown), at(0.45, 0.45, 0.45), group=g + '_angry')
    r.ellipsoid(j('head'), 'coal', at(4.0, 0, 24.4), at(0.7, 1.8, 1.2), group=g + '_roar')
    # Stick arms with twig fingers.
    for s, y in SIDES:
        r.tube(j('arm_' + s), 'stick', at(0, 5.0 * y, 21), at(0, 5.0 * y, 12), 0.55 * k, 0.4 * k, group=g)
        for f in (-1, 1):
            r.tube(j('arm_' + s), 'stick', at(0, 5.0 * y, 13.5), at(1.2 * f, 5.0 * y + 0.8 * f, 11.0), 0.3 * k, 0.2 * k,
                   group=g)


def things(r):
    """What the fight has (each its own frames in his sprite: the pack has room for few more
    sprites): a snowball that rolls (a twig in it turning), ice shards knocked up from the ground,
    his frost breath, a block of ice to hide behind, and the shadow that leaves him."""
    r.joint('roll', 'ground', (0, 0, 5))
    r.ellipsoid('roll', 'ice', (0, 0, 5), (5, 5, 5), group='ball')
    r.tube('roll', 'stick', (0, -3.0, 6.5), (0, -5.6, 8.0), 0.5, 0.3, group='ball')
    for i, (x, h) in enumerate(((-4, 1.0), (0, 1.4), (4, 0.9))):
        r.tube('ground', 'ice', (x, 0, 0), (x + 0.6 * (i - 1), 0, 9 * h), 2.0, 0.2, group='shard_tall')
        r.tube('ground', 'ice', (x, 0, 0), (x + 0.4 * (i - 1), 0, 4 * h), 1.8, 0.3, group='shard_low')
    for k in range(5):
        r.ellipsoid('ground', 'frost', (0, k * 3.4 - 7, 8 + 1.2 * ((k % 2) * 2 - 1)), (2.4, 2.4, 2.0), group='breath_a')
        r.ellipsoid('ground', 'frost', (0, k * 3.4 - 7, 8 - 1.2 * ((k % 2) * 2 - 1)), (2.2, 2.6, 2.2), group='breath_b')
    r.box('ground', 'snow', (0, 0, 15), (10, 10, 30), group='block')
    r.ellipsoid('ground', 'snow', (0, 0, 30), (5, 5, 2.0), group='block')
    for g, sway in (('shade_a', 1.0), ('shade_b', -1.0)):
        r.ellipsoid('ground', 'coal', (0, 0, 14), (4.5, 5.0, 8.0), group=g)
        r.tube('ground', 'coal', (0, 0, 7), (0, 2.5 * sway, 1), 3.0, 0.5, group=g)
        for y in (-1, 1):
            r.ellipsoid('ground', 'cap', (3.8, 1.6 * y, 17), (0.5, 0.7, 0.6), group=g)


def thing(group, **joints):
    return {'joints': joints, 'root': (0, 0, 0), 'show': (group,), 'view': None}


def pose(size, face, root=(0, 0, 0), **joints):
    js = {}
    for name, a in joints.items():
        js[name + '_' + size] = a
    return {'joints': js, 'root': root, 'show': (size, size + '_' + face), 'view': None}


def poses():
    rest = dict(arm_R=(30, -50), arm_L=(30, 50))
    return [
        pose('s', 'still', **rest),                                                    # 0 still
        pose('s', 'angry', arm_R=(95, 20), arm_L=(95, -20), head=-6),                  # 1 holding on
        pose('b', 'angry', arm_R=(40, -60), arm_L=(40, 60), head=-4),                  # 2 the boss standing
        pose('b', 'angry', arm_R=(-60, -40), arm_L=(30, 60), body=8, head=-6),         # 3 throwing
        pose('b', 'angry', root=(0, 0, 10), arm_R=(150, -30), arm_L=(150, 30)),        # 4 stomp: up
        pose('b', 'roar', arm_R=(60, -70), arm_L=(60, 70), head=14),                   # 5 frost breath
        pose('s', 'happy', **rest),                                                    # 6 happy
        # The fight's things: 7, 8 the snowball rolling; 9, 10 ice shards, low, up; 11, 12 the
        # frost breath; 13 a block of ice; 14, 15 the shadow.
        thing('ball', roll=(0, 0, 0)), thing('ball', roll=(0, 120, 0)),
        thing('shard_low'), thing('shard_tall'),
        thing('breath_a'), thing('breath_b'),
        thing('block'),
        thing('shade_a'), thing('shade_b'),
    ]
