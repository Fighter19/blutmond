# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki's room (docs/plans/3d-sprites.md, "Rooms"): where her prologue starts, at night.

An attic bedroom seen as a doll's house, cut open at the front: lavender wallpaper over a
wooden panel, plank floor, a bed against the left wall with the same white-lilac sheets as
the blanket in her sprite's bed frames, a window with the moon and something outside that is
looking in, red curtains, a rug, a dresser with a plush rabbit, the rack her katana rests on
(the katana itself is an object, katana.bmsprite) and the door out to the street.

Rendered by tools/sprite3d/make_room.py:

  python3 tools/sprite3d/make_room.py assets/mitsuki/home/home.py --out assets/mitsuki/home

The world: x to the right (a pixel a unit), y away from the viewer, z up; the floor's front
edge is y = 0, z = 0 (the row GROUND of the picture: where the player's feet stand, the
sprites' sink of 2 below the collision's top). Mitsuki's bed frames (mitsuki.py, BED) lie on
this bed: keep the two in step.
"""

W, H = 320, 160
GROUND = 130
PITCH = 14.0

OUT = (24, 20, 36)
WALL = [(56, 52, 94), (76, 72, 122)]
STRIPE = [WALL[0]]
WOOD = [(74, 50, 40), (104, 72, 52), (138, 96, 64)]
PLANK_A = [(92, 62, 44), (122, 86, 58)]
PLANK_B = [(74, 50, 40), (104, 72, 52)]    # the wood's darker two: a plank in two of each
BEAM = [(40, 30, 38), (60, 44, 46)]
SHEET = [(118, 120, 160), (190, 196, 222), (240, 244, 255)]   # as Mitsuki's hair (her blanket)
RED = [(120, 16, 40), (200, 32, 56)]
NAVY = [(30, 32, 62), (52, 58, 104)]
GOLD = [(176, 128, 40), (248, 212, 96)]
SKY = [(18, 26, 58)]
NIGHT = [(10, 12, 28)]
MOON = [(240, 232, 190)]
STAR = MOON
EYES = [(255, 72, 64)]

# Where things are (x in pixels); BED_* are shared with mitsuki.py.
BED_X0, BED_X1 = 14, 70      # headboard to footboard
BED_TOP = 13.0               # the mattress's top
WINDOW = 160
RACK = 272
DOOR_X = 304

ROOM = {
    'name': 'mitsuki_home',
    'size': (W, H),
    'ground': GROUND,
    'solid': (8, 9),
    'collision_tileset': '../../engine/blutmond_collision.tsx',
    'properties': [
        ('room_id', 'mitsuki_home'), ('title', "Mitsuki's room"), ('music', 'none'),
        ('attach', 'Prologue'), ('lighting', 'palette'), ('ambient', '0.45'), ('light_levels', '4'),
        ('light_curve', 'perceived'), ('backdrop', '#101a2a'),
    ],
    'objects': [
        ('CameraZone', 'room', 0, 0, W, H, []),
        # By the foot of the bed: her bed frames are drawn from here (mitsuki.py, BED).
        ('PlayerStart', 'start', 56, 128, 0, 0, []),
        ('Light', 'moon', WINDOW, 62, 0, 0,
         [('colour', '#a8bcff'), ('radius', '360'), ('height', '70'), ('intensity', '1.3')]),
        ('SwordRack', 'katana', RACK, 92, 0, 0, [('Gives', 'Katana')]),
        ('Door', 'to_street', DOOR_X, 80, 16, 48,
         [('target_room', 'rural_street'), ('target_door', 'to_home'), ('direction', 'east'), ('needs', 'Katana')]),
    ],
}


def build(Rig):
    r = Rig('mitsuki_home')
    # Straight at the back wall, a little from above; lit from the top left, a little ahead.
    r.view = {'yaw': 0.0, 'pitch': PITCH, 'light': (-0.35, 0.7, 0.62), 'supersample': 8}
    r.outline = OUT
    # A pixel up the floor is 4 units further away: lines only where a part stands well apart.
    r.inner_lines = 6.0
    two = [0.5]
    r.material('wall', WALL, bands=[0.45])
    r.material('stripe', STRIPE, bands=[])
    r.material('wood', WOOD, bands=[0.35, 0.75])
    r.material('plank_a', PLANK_A, bands=two)
    r.material('plank_b', PLANK_B, bands=two)
    r.material('beam', BEAM, bands=[0.3])
    r.material('sheet', SHEET, bands=[0.35, 0.75])
    r.material('red', RED, bands=two)
    r.material('navy', NAVY, bands=two)
    r.material('gold', GOLD, weight=3, min_cover=0.3, bands=two, keep_inner=True)
    r.material('sky', SKY, bands=[], emissive=True)
    r.material('night', NIGHT, bands=[], emissive=True, keep_inner=True)
    r.material('moon', MOON, bands=[], weight=3, emissive=True, keep_inner=True)
    r.material('star', STAR, bands=[], weight=6, min_cover=0.2, emissive=True, keep_inner=True)
    r.material('eyes', EYES, bands=[], weight=9, min_cover=0.15, emissive=True, keep_inner=True)
    r.joint('room', None, (0, 0, 0))
    box = lambda m, c, s, **k: r.box('room', m, c, s, **k)   # noqa: E731
    ell = lambda m, c, rr, **k: r.ellipsoid('room', m, c, rr, **k)   # noqa: E731
    tube = lambda m, a, b, r0, r1=None, **k: r.tube('room', m, a, b, r0, r1, **k)   # noqa: E731

    # The shell: back wall, wainscot and rail, stripes, the floor's planks and its cut front,
    # the ceiling's beam across the top.
    box('wall', (W / 2, 52, 60), (W + 40, 8, 160))
    for x in range(8, W, 16):
        box('stripe', (x, 47.7, 74), (3, 0.6, 84))
    box('wood', (W / 2, 46.5, 15.5), (W + 40, 3, 31))
    box('wood', (W / 2, 45.5, 31.5), (W + 40, 5, 2.4))
    for x in range(20, W, 24):                            # grooves between the panels
        box('beam', (x, 44.85, 15), (1, 0.3, 26))
    # A drawing in a frame over the bed: a crescent moon.
    box('wood', (42, 47, 62), (22, 1.6, 18))
    box('navy', (42, 46.0, 62), (18, 0.6, 14))
    ell('gold', (42, 45.6, 62), (4, 0.2, 4), cuts=[((43.8, 45.6, 63), (-1, 0, -0.4))])
    for k in range(6):
        box('plank_a' if k % 2 == 0 else 'plank_b', (W / 2, 4 + 8 * k, -2), (W + 40, 8, 4))
    box('beam', (W / 2, -1, -30), (W + 40, 2, 60))
    box('wood', (W / 2, -2.4, -4), (W + 40, 1, 6))
    box('beam', (W / 2, 22, 122), (W + 40, 52, 16))
    box('wood', (W / 2, -4.4, 116), (W + 40, 1, 3))
    for x in (4, W - 4):                                  # corner posts
        box('beam', (x, 44, 60), (8, 6, 120))

    # The window: the night sky with stars, the moon, and something peeking in at the sill.
    wx, wz = WINDOW, 70
    box('sky', (wx, 47.2, wz), (52, 0.6, 42))
    for sx, sz in ((140, 86), (151, 80), (166, 88), (183, 77), (146, 70), (178, 62), (157, 60)):
        box('star', (sx, 46.8, sz), (0.9, 0.2, 0.9))
    ell('moon', (176, 46.6, 84), (4.2, 0.2, 4.2), cuts=[((178.5, 46.6, 85), (-1, 0, -0.4))])
    ell('night', (145, 46.7, 52), (11, 0.2, 9))                      # its head
    ell('night', (137, 46.7, 61), (1.6, 0.2, 4), rot=(0, 25, 0))      # its ears
    ell('night', (153, 46.7, 61), (1.6, 0.2, 4), rot=(0, -25, 0))
    for ex in (141.5, 148.5):
        ell('eyes', (ex, 46.4, 55), (1.3, 0.2, 0.8))
    for x in (wx - 27, wx + 27):
        box('wood', (x, 45, wz), (4, 4, 46))
    box('wood', (wx, 45, wz + 22), (58, 4, 4))
    box('wood', (wx, 44.6, wz), (2, 3, 40))
    box('wood', (wx, 44.6, wz + 6), (52, 3, 2))
    box('wood', (wx, 42, wz - 22.5), (64, 9, 3))          # the sill
    tube('gold', (wx - 38, 40, 96), (wx + 38, 40, 96), 0.9)
    for side in (-1, 1):                                  # curtains, gathered
        x = wx + side * 32
        tube('red', (x, 41, 95), (x - side * 1.5, 41, 50), 3.2, 4.6, squash=0.5)
        ell('red', (x - side * 1.5, 41, 49), (4.6, 2.2, 2.4))

    # The rug.
    ell('navy', (112, 20, 0.0), (28, 14, 0.6))
    ell('red', (112, 20, 0.2), (22, 11, 0.6))

    # The bed: headboard, footboard, frame, mattress, pillow, the blanket turned back.
    box('wood', (BED_X0 + 2, 27, 17), (4, 38, 34))
    ell('wood', (BED_X0 + 2, 27, 33), (2.4, 19, 4))
    box('wood', (BED_X1 - 2, 27, 11), (4, 38, 22))
    ell('wood', (BED_X1 - 2, 27, 21.5), (2.4, 19, 2.4))
    box('wood', ((BED_X0 + BED_X1) / 2, 27, 6.5), (BED_X1 - BED_X0 - 6, 36, 5))
    box('sheet', ((BED_X0 + BED_X1) / 2, 27, 11), (BED_X1 - BED_X0 - 7, 35, 4))
    ell('sheet', (BED_X0 + 10, 30, BED_TOP + 2.2), (6.5, 12, 2.6))
    box('sheet', (BED_X0 + 34, 27, BED_TOP + 0.9), (34, 36, 1.8))
    tube('sheet', (BED_X0 + 17, 9, BED_TOP + 1.6), (BED_X0 + 17, 45, BED_TOP + 1.6), 1.6)
    box('sheet', (BED_X0 + 34, 8.4, 9.5), (34, 1.4, 8))   # hanging over the front

    # The dresser, a plush rabbit and two books on it.
    dx = 220
    box('wood', (dx, 36, 13), (40, 20, 26))
    box('wood', (dx, 25.6, 26.6), (43, 23, 1.6))
    for z in (8, 19):
        box('wood', (dx, 25.6, z), (36, 0.8, 9))
        ell('gold', (dx, 25.0, z), (1.1, 0.4, 1.1))
    ell('sheet', (dx - 9, 34, 31), (4.2, 3.4, 4))          # the rabbit
    ell('sheet', (dx - 9, 33, 37.5), (3.6, 3.2, 3.2))
    for side in (-1, 1):
        tube('sheet', (dx - 9 + side * 1.4, 33, 40), (dx - 9 + side * 2.2, 33, 46), 1.0, 0.8)
    for side in (-1, 1):
        ell('eyes', (dx - 9 + side * 1.4, 29.8, 38), (0.5, 0.2, 0.5))
    box('navy', (dx + 8, 38, 29), (12, 8, 3))               # books
    box('red', (dx + 9, 38, 32), (10, 7, 3))

    # The rack her katana rests on (the katana: an object of the room), on the rail.
    for x in (RACK - 13, RACK + 13):
        box('wood', (x, 44, 37), (3, 4, 9))
        box('wood', (x, 42, 33.5), (3, 4, 2))

    # The door out (east), with a brass knob.
    box('beam', (DOOR_X + 4, 46, 27), (26, 3, 54))
    box('wood', (DOOR_X + 4, 44.6, 25), (20, 2, 50))
    for z in (13, 36):
        box('wood', (DOOR_X + 4, 43.4, z), (14, 0.6, 16))
    ell('gold', (DOOR_X - 3, 43.0, 25), (1.2, 0.8, 1.2))

    r.pose()
    return r
