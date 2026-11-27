# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki's room (docs/plans/3d-sprites.md, "Rooms"): where her prologue starts, at night.

An attic room seen as a doll's house, cut open at the front, the roof's gable cut open above
it: lavender wallpaper over a wooden panel, plank floor, her bed with the same white-lilac
sheets as the blanket in her sprite's bed frames, a window with the moon (and something
outside looking in: an object, props.py), red curtains, a rug, a low dresser with a plush
rabbit, a light switch (an object) and a lamp on the ceiling; through a door frame a little
hall with the rack her katana rests on (an object too), shoes, and the door out.

**Scale**: one unit a pixel, and a pixel 5 cm: Mitsuki is 31 pixels (1.55 m). The ceiling is
at 2.8 m, the bed 2 m long, the window 1.3 m, the dresser 0.65 m high (so the rabbit on it sits
at her head's height: standing in front of it, half its head shows over hers). Things are a
little larger than true where they must read at this size (the switch, the rabbit).

Rendered by tools/sprite3d/make_room.py:

  python3 tools/sprite3d/make_room.py assets/mitsuki/home/home.py --out assets/mitsuki/home \\
      --save-blend assets/mitsuki/home/mitsuki_home.blend

The world: x to the right, y away from the viewer, z up; the floor's front edge is y = 0,
z = 0 (the row GROUND of the picture: where the player's feet stand, the sprites' sink of 2
below the collision's top). Mitsuki's bed frames (mitsuki.py: HOME_X, BED_Y, MATTRESS) lie on
this bed, and the HomeProp objects (props.py) sit where `on_screen` puts them: keep them in
step.
"""

import math

W, H = 240, 160
GROUND = 130
PITCH = 14.0

OUT = (24, 20, 36)
WALL = [(104, 90, 140), (136, 120, 172)]   # as the lamp shows it: the moon darkens it
STRIPE = [WALL[0]]
WOOD = [(96, 64, 46), (136, 94, 62), (178, 126, 82)]
PLANK_A = [(118, 80, 52), (158, 112, 72)]
PLANK_B = [(96, 64, 46), (136, 94, 62)]    # the wood's darker two: a plank in two of each
BEAM = [(40, 30, 38), (60, 44, 46)]
SHEET = [(118, 120, 160), (190, 196, 222), (240, 244, 255)]   # as Mitsuki's hair (her blanket)
RED = [(120, 16, 40), (200, 32, 56)]
NAVY = [(30, 32, 62), (52, 58, 104)]
GOLD = [(176, 128, 40), (248, 212, 96)]
SKY = [(18, 26, 58)]
MOON = [(240, 232, 190)]
EYES = [(255, 72, 64)]
# The attic and the roof, unlit: as dark as the room by night.
ATTIC = [(22, 16, 24), (32, 24, 30)]
ROOF = [(62, 12, 28), (90, 16, 36)]
RAFTER = [(44, 30, 26), (62, 44, 34)]

CEILING = 56                 # 2.8 m
BACK = 48                    # the back wall's face (y)
APEX = (120, 116)            # the roof's ridge (x, z), its eaves at z 58 past either side

# Where things are; HOME_X, BED_* are mitsuki.py's too.
BED_X0, BED_X1 = 10, 52      # headboard to footboard: 2 m of mattress between
BED_Y0, BED_Y1 = 16, 44      # front to back (1.4 m)
BED_TOP = 11.0               # the mattress's top (55 cm)
HOME_X = 46                  # in front of the bed's foot: the room's PlayerStart
WINDOW = (86, 18, 24, 26)    # middle x, sill z, width, height (its frame)
DRESSER = (110, 130, 13)     # x from, to, its top
RABBIT = 115                 # the plush rabbit on it
POST = 138                   # the door frame into the hall
RACK = 172                   # the katana's rack
DOOR_X = 222                 # the door out (east)
LAMP = (60, 24, 46)          # the ceiling lamp's bulb: where the room's light comes from
SWITCH = (58, 47, 14)        # the light switch by the foot of the bed (an object, props.py)
MONSTER = (80, 46.2, 20.5)   # what peeks in at the bottom left pane (an object, props.py)


def on_screen(p):
    """A world point's place in the picture (x, row)."""
    x, y, z = p
    c, s = math.cos(math.radians(PITCH)), math.sin(math.radians(PITCH))
    return round(x), round(GROUND - z * c - y * s)


ROOM = {
    'name': 'mitsuki_home',
    'size': (W, H),
    'ground': GROUND,
    'solid': (8, 9),
    'collision_tileset': '../../engine/blutmond_collision.tsx',
    'properties': [
        ('room_id', 'mitsuki_home'), ('title', "Mitsuki's room"), ('music', 'none'),
        ('attach', 'Prologue'), ('lighting', 'palette'), ('ambient', '0.3'), ('light_levels', '3'),
        ('light_curve', 'perceived'), ('backdrop', '#101a2a'),
    ],
    'objects': [
        ('CameraZone', 'room', 0, 0, W, H, []),
        # In front of the foot of the bed: her bed frames are drawn from here (mitsuki.py).
        ('PlayerStart', 'start', HOME_X, 128, 0, 0, []),
        # One light, the ceiling lamp's place: off, the moon's cool, dim light through the
        # window (as here); switched on (prologue.ms, set_light), warm and bright. A lit colour
        # is at most as drawn: the room is drawn as the lamp shows it, the moon darkens it.
        ('Light', 'light', *on_screen(LAMP), 0, 0,
         [('colour', '#8ca0e6'), ('radius', '250'), ('height', '50'), ('intensity', '0.9')]),
        ('HomeProp', 'switch', *on_screen(SWITCH), 0, 0, [('Kind', 'Switch')]),
        ('HomeProp', 'monster', *on_screen(MONSTER), 0, 0, [('Kind', 'Monster')]),
        ('HomeProp', 'katana', *on_screen((RACK, 44, 30)), 0, 0, [('Kind', 'Katana'), ('Gives', 'Katana')]),
        ('Door', 'to_street', DOOR_X + 2, 88, 16, 40,
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
    r.material('moon', MOON, bands=[], weight=3, emissive=True, keep_inner=True)
    r.material('star', MOON, bands=[], weight=6, min_cover=0.2, emissive=True, keep_inner=True)
    r.material('eyes', EYES, bands=[], weight=9, min_cover=0.15, emissive=True, keep_inner=True)
    # Above the ceiling the room's lamp does not reach: the attic and the roof are drawn as
    # they are (emissive: no light levels of their own, which a lit room's palettes have no
    # room for).
    r.material('attic', ATTIC, bands=[0.3], emissive=True)
    r.material('roof', ROOF, bands=two, emissive=True)
    r.material('rafter', RAFTER, bands=[0.5], emissive=True)
    r.joint('room', None, (0, 0, 0))
    box = lambda m, c, s, **k: r.box('room', m, c, s, **k)   # noqa: E731
    ell = lambda m, c, rr, **k: r.ellipsoid('room', m, c, rr, **k)   # noqa: E731
    tube = lambda m, a, b, r0, r1=None, **k: r.tube('room', m, a, b, r0, r1, **k)   # noqa: E731

    # The night behind everything (seen above the roof), with stars.
    box('sky', (W / 2, 120, 100), (W + 80, 4, 200))
    for sx, sz in ((14, 150), (40, 168), (196, 160), (226, 146), (210, 175), (28, 182)):
        box('star', (sx, 117.5, sz), (1.2, 0.2, 1.2))

    # The roof's gable, cut open: the attic's dark inside under two slopes, the rafters' cut
    # ends along them, a king post and a collar beam.
    ax, az = APEX
    slope = (az - 58) / (ax + 20)
    left = ((ax, 0, az), (-slope, 0, 1))
    right = ((ax, 0, az), (slope, 0, 1))
    box('attic', (W / 2, BACK + 2, 100), (W + 40, 4, 100), cuts=[left, right])
    for side in (-1, 1):
        out = ((ax, 0, az + 7), (side * slope, 0, 1))     # the roof's top: 7 above the inside
        inner = ((ax, 0, az), (-side * slope, 0, -1))     # ... its underside
        mid = ((ax, 0, 0), (-side, 0, 0))              # one side of the ridge
        box('roof', (W / 2, 22, 100), (W + 60, 56, 120), cuts=[out, inner, mid])
    tube('rafter', (ax, 6, CEILING + 4), (ax, 6, az - 2), 1.6)
    tube('rafter', (ax - 40, 6, CEILING + 4 + 40 * slope * 0.9), (ax + 40, 6, CEILING + 4 + 40 * slope * 0.9), 1.4)

    # The ceiling: its cut edge along the front, a beam under it.
    box('rafter', (W / 2, 22, CEILING + 2), (W + 40, 56, 4))   # (its top: the attic's floor)
    box('beam', (W / 2, -1.5, CEILING + 0.5), (W + 40, 3, 1.5))

    # The walls: wallpaper, the wainscot and its rail, stripes; the floor's planks and its cut
    # front.
    box('wall', (W / 2, BACK + 4, CEILING / 2), (W + 40, 8, CEILING))
    for x in range(8, W, 12):
        box('stripe', (x, BACK - 0.3, 22 + (CEILING - 22) / 2), (2, 0.6, CEILING - 22))
    box('wood', (W / 2, BACK - 1.5, 10.5), (W + 40, 3, 21))
    box('wood', (W / 2, BACK - 2.5, 21.5), (W + 40, 5, 2))
    for x in range(16, W, 20):
        box('beam', (x, BACK - 3.15, 10), (1, 0.3, 18))
    for k in range(6):
        box('plank_a' if k % 2 == 0 else 'plank_b', (W / 2, 4 + 8 * k, -2), (W + 40, 8, 4))
    box('beam', (W / 2, -1, -30), (W + 40, 2, 60))
    box('wood', (W / 2, -2.4, -4), (W + 40, 1, 6))
    box('beam', (3, BACK - 3, CEILING / 2), (6, 6, CEILING))       # the corner

    # The window: the night sky, the moon, stars; its frame, a cross, the sill, curtains.
    wx, wz, ww, wh = WINDOW
    box('sky', (wx, BACK - 0.8, wz + wh / 2), (ww - 2, 0.6, wh - 2))
    for sx, sz in ((77, 39), (82, 33), (91, 41), (95, 34), (89, 27)):
        box('star', (sx, BACK - 1.2, sz), (0.9, 0.2, 0.9))
    ell('moon', (93, BACK - 1.3, 38.5), (2.6, 0.2, 2.6), cuts=[((94.6, BACK - 1.3, 39.2), (-1, 0, -0.4))])
    for x in (wx - ww / 2, wx + ww / 2):
        box('wood', (x, BACK - 3, wz + wh / 2), (2.5, 3, wh + 2))
    box('wood', (wx, BACK - 3, wz + wh), (ww + 2.5, 3, 2.5))
    box('wood', (wx, BACK - 2.6, wz + wh / 2), (1.6, 2, wh))
    box('wood', (wx, BACK - 2.6, wz + wh / 2), (ww, 2, 1.6))
    box('wood', (wx, BACK - 5, wz - 1), (ww + 6, 8, 2.2))          # the sill
    tube('gold', (wx - ww / 2 - 6, BACK - 7, wz + wh + 3), (wx + ww / 2 + 6, BACK - 7, wz + wh + 3), 0.7)
    for side in (-1, 1):                                             # curtains, gathered
        x = wx + side * (ww / 2 + 3)
        tube('red', (x, BACK - 7, wz + wh + 2.5), (x - side, BACK - 7, wz + 2), 2.0, 3.0, squash=0.5)
        ell('red', (x - side, BACK - 7, wz + 1.5), (3.0, 1.5, 1.6))

    # The rug.
    ell('navy', (84, 18, 0.0), (18, 10, 0.6))
    ell('red', (84, 18, 0.2), (14, 7.5, 0.6))

    # The bed: headboard, footboard, frame, mattress, pillow, the blanket turned back, hanging
    # over the front.
    by, bd = (BED_Y0 + BED_Y1) / 2, BED_Y1 - BED_Y0
    box('wood', (BED_X0 + 2, by, 12), (4, bd + 2, 24))
    ell('wood', (BED_X0 + 2, by, 24), (2.4, bd / 2 + 1, 2.6))
    box('wood', (BED_X1 - 2, by, 8), (4, bd + 2, 16))
    ell('wood', (BED_X1 - 2, by, 16), (2.4, bd / 2 + 1, 1.8))
    box('wood', ((BED_X0 + BED_X1) / 2, by, 5.5), (BED_X1 - BED_X0 - 6, bd, 5))
    box('sheet', ((BED_X0 + BED_X1) / 2, by, BED_TOP - 1.5), (BED_X1 - BED_X0 - 7, bd - 1, 3))
    ell('sheet', (BED_X0 + 9, by + 2, BED_TOP + 1.5), (4.6, 9, 2.0))
    box('sheet', (BED_X0 + 25, by, BED_TOP + 0.8), (26, bd, 1.6))
    tube('sheet', (BED_X0 + 13, BED_Y0 + 1, BED_TOP + 1.4), (BED_X0 + 13, BED_Y1 - 1, BED_TOP + 1.4), 1.4)
    box('sheet', (BED_X0 + 25, BED_Y0 - 0.6, 8), (26, 1.2, 7))

    # A drawing over the bed: a crescent moon.
    box('wood', (30, BACK - 0.8, 36), (13, 1.6, 10))
    box('navy', (30, BACK - 1.8, 36), (10, 0.6, 7))
    ell('gold', (30, BACK - 2.2, 36), (2.4, 0.2, 2.4), cuts=[((31.2, BACK - 2.2, 36.6), (-1, 0, -0.4))])

    # The ceiling lamp: a cord, a red shade, the bulb (not giving light of its own: the room's
    # light is there, so it is the brightest when on).
    lx, ly, lz = LAMP
    tube('beam', (lx, ly, CEILING), (lx, ly, lz + 5), 0.4)
    ell('red', (lx, ly, lz + 2.5), (6.0, 6.0, 4.0), cuts=[((lx, ly, lz + 0.5), (0, 0, -1))])
    ell('gold', (lx, ly, lz + 0.6), (6.2, 6.2, 0.7), cuts=[((lx, ly, lz + 0.5), (0, 0, -1))])
    ell('sheet', (lx, ly, lz - 0.4), (2.2, 2.2, 1.6))

    # The dresser, low (0.65 m), with the plush rabbit and two books on it. The rabbit is a
    # little large for a plush: it must read. Its eyes are the same red as the thing outside.
    d0, d1, dt = DRESSER
    dx = (d0 + d1) / 2
    box('wood', (dx, BACK - 7, dt / 2 - 0.5), (d1 - d0, 14, dt - 1))
    box('wood', (dx, BACK - 7.5, dt - 0.5), (d1 - d0 + 2, 15, 1.2))
    for z in (4, 10):
        box('wood', (dx, BACK - 14.4, z), (d1 - d0 - 3, 0.8, 5))
        ell('gold', (dx, BACK - 15.0, z), (0.9, 0.4, 0.9))
    bx, byy = RABBIT, BACK - 8
    ell('sheet', (bx, byy, dt + 3), (3.4, 2.8, 3.2))
    ell('sheet', (bx, byy - 0.5, dt + 8.3), (3.0, 2.6, 2.7))
    for side in (-1, 1):
        tube('sheet', (bx + side * 1.2, byy - 0.5, dt + 10.5), (bx + side * 1.9, byy - 0.5, dt + 15.5), 0.9, 0.7)
        ell('eyes', (bx + side * 1.15, byy - 3.0, dt + 8.6), (0.5, 0.2, 0.5))
    box('navy', (dx + 6, BACK - 6, dt + 1.4), (8, 6, 2.2))
    box('red', (dx + 6.5, BACK - 6, dt + 3.4), (7, 5, 2))

    # The door frame into the hall: a post at the front, one at the back, the lintel.
    for y in (0.5, BACK - 2):
        box('wood', (POST + 3, y, CEILING / 2), (6, 3, CEILING))
    box('wood', (POST + 3, BACK / 2, CEILING - 6), (6, BACK, 12))

    # The hall: the katana's rack (the katana: an object), shoes, the door out with a knob.
    for x in (RACK - 9, RACK + 9):
        box('wood', (x, BACK - 2, 30), (2.4, 4, 6))
        box('wood', (x, BACK - 4, 27.5), (2.4, 4, 1.6))
    box('wood', (204, BACK - 6, 4), (18, 10, 1.2))
    for sx, m in ((198, 'red'), (203, 'red'), (209, 'navy'), (214, 'navy')):
        ell(m, (sx, BACK - 7, 6.2), (2.0, 3.6, 1.4))
    box('beam', (DOOR_X + 9, BACK - 1, 22), (20, 3, 44))
    box('wood', (DOOR_X + 9, BACK - 2.4, 20.5), (16, 2, 41))
    for z in (10, 29):
        box('wood', (DOOR_X + 9, BACK - 3.6, z), (11, 0.6, 14))
    ell('gold', (DOOR_X + 3.5, BACK - 4.0, 20), (1.0, 0.7, 1.0))

    r.pose()
    return r
