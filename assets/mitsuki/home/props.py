# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Things of Mitsuki's home, one sprite for the HomeProp objects (home_prop.ms), each frame its
own thing:

0  her katana on its rack in her room (home.py): a black lacquered scabbard with a red cord,
   a gold guard, the grip wrapped in navy; its middle on the anchor
1  the light switch by her bed: an ivory plate, its brass rocker pressed in at the bottom (off)
2  ... and at the top (on)
3  the thing peeking in at her window (home.py, MONSTER): a dark head with two ears over the
   sill, its red eyes glowing (black in the normal map: not shaded by the room's light)
4  ... its eyes shut (a blink)
5  ... ducking away below the sill

The first three are seen as her room is (yaw 0, pitch 14).

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/home/props.py \\
      --layout assets/mitsuki/home/props_layout.bmsprite --out assets/mitsuki/home --name mitsuki_props
"""

OUT = (24, 20, 36)
SAYA = [(30, 32, 62), (66, 62, 86)]
RED = [(120, 16, 40), (200, 32, 56)]
GOLD = [(176, 128, 40), (248, 212, 96)]
GRIP = [(30, 32, 62), (52, 58, 104)]
PLATE = [(190, 196, 222), (240, 244, 255)]
NIGHT = [(10, 12, 28)]
EYES = [(255, 72, 64)]

ROOM = (0.0, 14.0)


def build(Rig):
    r = Rig('mitsuki_props')
    r.view = {'yaw': 0.0, 'pitch': 14.0, 'light': (-0.35, 0.7, 0.62), 'supersample': 8}
    r.outline = OUT
    r.material('saya', SAYA, bands=[0.5])
    r.material('cord', RED, weight=2, min_cover=0.3, bands=[0.5])
    r.material('gold', GOLD, weight=3, min_cover=0.3, bands=[0.5], keep_inner=True)
    r.material('grip', GRIP, bands=[0.5])
    r.material('plate', PLATE, bands=[0.5])
    r.material('night', NIGHT, bands=[], emissive=True)
    r.material('eyes', EYES, bands=[], weight=9, min_cover=0.15, emissive=True, keep_inner=True)
    r.joint('prop', None, (0, 0, 0))
    r.joint('head', 'prop', (0, 0, 0))

    # The katana, curved a little, the edge up.
    k = 'katana'
    r.tube('prop', 'saya', (-14, 0, 0.6), (-2, 0, 0.0), 1.0, 1.1, group=k)
    r.tube('prop', 'saya', (-2, 0, 0.0), (6, 0, 0.2), 1.1, 1.2, group=k)
    r.ellipsoid('prop', 'gold', (-14.4, 0, 0.6), (0.8, 1.0, 1.0), group=k)
    r.ellipsoid('prop', 'cord', (4, -0.9, 0.2), (1.2, 0.5, 1.3), group=k)
    r.tube('prop', 'cord', (4, -1.0, -0.6), (2.6, -1.0, -3.0), 0.35, group=k)
    r.ellipsoid('prop', 'gold', (6.6, 0, 0.2), (0.5, 1.8, 1.9), group=k)
    r.tube('prop', 'grip', (7, 0, 0.2), (13.5, 0, 0.6), 0.95, 0.85, group=k)
    for x in (8.4, 10.2, 12.0):
        r.ellipsoid('prop', 'gold', (x, -0.7, 0.4), (0.3, 0.4, 0.5), group=k)
    r.ellipsoid('prop', 'gold', (13.8, 0, 0.6), (0.5, 0.9, 0.9), group=k)

    # The switch: the plate, the rocker's raised half low (off) or high (on).
    r.box('prop', 'plate', (0, 0, 0), (4.4, 0.8, 6.4), group='switch')
    r.box('prop', 'gold', (0, -0.9, -1.3), (2.4, 1.0, 2.4), group='rocker_off')
    r.box('prop', 'gold', (0, -0.9, 1.3), (2.4, 1.0, 2.4), group='rocker_on')

    # The thing at the window: its head on the sill (the anchor), ears up, eyes on her.
    m = 'monster'
    r.ellipsoid('head', 'night', (0, 0, 1.6), (4.2, 0.6, 3.4), group=m)
    for side in (-1, 1):
        r.ellipsoid('head', 'night', (side * 2.5, 0, 5.6), (1.2, 0.5, 3.0), rot=(0, side * 22, 0), group=m)
        r.ellipsoid('head', 'eyes', (side * 1.6, -0.7, 2.2), (0.9, 0.2, 0.6), group='eyes')

    r.pose(show=('katana',))
    r.pose(show=('switch', 'rocker_off'))
    r.pose(show=('switch', 'rocker_on'))
    r.pose(show=('monster', 'eyes'))
    r.pose(show=('monster',))
    r.pose(show=('monster',), root=(0, 0, -1.6))
    return r
