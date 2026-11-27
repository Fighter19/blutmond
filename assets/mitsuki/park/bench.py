# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Mitsuki and Hina on the bench as one scene, both drawn, to look at (pair.py: the scene their
bench frames and the close-up of their hands are drawn from): four frames, holding hands,
Hina's head in Mitsuki's lap, Mitsuki stroking her hair (two); seen from the front as in the game,
Mitsuki's anchor at the frame's.

Not a sprite: both characters' colours together are more than a sprite has (one palette of 15).
make_sprite writes its --preview all the same, and --save-blend the scene:

  python3 tools/sprite3d/make_sprite.py assets/mitsuki/park/bench.py \\
      --layout assets/mitsuki/park/bench_layout.bmsprite --sink 2 --out /tmp/bench --name bench \\
      --save-blend bench.blend --preview bench_x4.png
"""

import os
import sys


def build(Rig):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pair
    r = Rig('bench')
    r.view = {'yaw': 90.0, 'pitch': 8.0, 'light': (-0.3, 0.75, 0.6), 'supersample': 8}
    r.outline = (24, 20, 36)
    r.inner_lines = 3.0
    r.joint('bench', None, (0, 0, 0))
    pair.add_mitsuki(r, 'm_')
    pair.add_hina(r, 'h_')
    m = lambda n: 'm_' + n  # noqa: E731
    h = lambda n: 'h_' + n  # noqa: E731
    solved = pair.solve(r, m, h, log=os.environ.get('BENCH_LOG'))
    pair.shrink(r, ['m_pelvis', 'h_pelvis'])
    show = ('m_body', 'm_far_eye', 'm_eyes', 'h_body', 'h_far_eye', 'h_lap', 'h_relaxed')
    for i, js in enumerate(pair.steps(r, solved['joints'], m, h)):
        r.pose(joints=js, show=show + ('h_relaxed_lids' if i == 0 else 'h_resting_lids',))
    return r
