# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki's katana on its rack at home (home.py): the sprite of the SwordRack object of her
room, gone once she takes it. In its black lacquered scabbard with a red cord, a gold guard,
the grip wrapped in navy. Seen as the room is (yaw 0, pitch 14), its middle on the anchor.

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/home/katana.py \\
      --layout assets/mitsuki/home/katana_layout.bmsprite --out assets/mitsuki/home --name katana
"""

OUT = (24, 20, 36)
SAYA = [(30, 32, 62), (66, 62, 86)]
RED = [(120, 16, 40), (200, 32, 56)]
GOLD = [(176, 128, 40), (248, 212, 96)]
GRIP = [(30, 32, 62), (52, 58, 104)]


def build(Rig):
    r = Rig('katana')
    r.view = {'yaw': 0.0, 'pitch': 14.0, 'light': (-0.35, 0.7, 0.62), 'supersample': 8}
    r.outline = OUT
    r.material('saya', SAYA, bands=[0.5])
    r.material('cord', RED, weight=2, min_cover=0.3, bands=[0.5])
    r.material('gold', GOLD, weight=3, min_cover=0.3, bands=[0.5], keep_inner=True)
    r.material('grip', GRIP, bands=[0.5])
    r.joint('rack', None, (0, 0, 0))
    # Curved a little: the scabbard in two pieces, the edge up.
    r.tube('rack', 'saya', (-14, 0, 0.6), (-2, 0, 0.0), 1.0, 1.1)
    r.tube('rack', 'saya', (-2, 0, 0.0), (6, 0, 0.2), 1.1, 1.2)
    r.ellipsoid('rack', 'gold', (-14.4, 0, 0.6), (0.8, 1.0, 1.0))
    r.ellipsoid('rack', 'cord', (4, -0.9, 0.2), (1.2, 0.5, 1.3))
    r.tube('rack', 'cord', (4, -1.0, -0.6), (2.6, -1.0, -3.0), 0.35)
    r.ellipsoid('rack', 'gold', (6.6, 0, 0.2), (0.5, 1.8, 1.9))
    r.tube('rack', 'grip', (7, 0, 0.2), (13.5, 0, 0.6), 0.95, 0.85)
    for x in (8.4, 10.2, 12.0):
        r.ellipsoid('rack', 'gold', (x, -0.7, 0.4), (0.3, 0.4, 0.5))
    r.ellipsoid('rack', 'gold', (13.8, 0, 0.6), (0.5, 0.9, 0.9))
    r.pose()
    return r
