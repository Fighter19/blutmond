#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""One of the GBA and the PC game plays, the other plays along (docs/spec/link-protocol.md
section 10):

  mirror_session.py from|to <bmlink> <game> <pack> <image.gba> <image.elf>

from: the GBA image (in mGBA) is played by its autopilot and the PC game follows.
to:   the PC game is played by its autopilot (at 60 frames per second) and the GBA follows.

The one that follows compares a checksum of its state with the other's every 64 frames; no
check may differ and the state must never have to be loaded again. Both runs go through a
room change."""

import re
import socket
import subprocess
import sys
import time


def main():
    direction, bmlink, game, pack, image, elf = sys.argv[1:7]
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    stats = elf + ":bm_game_stats"
    address = "127.0.0.1:%d" % port
    if direction == "from":
        # After the mirror ends the PC game plays on by itself to frame 3000 and ends.
        pc_args = ["--follow", "--until-frame", "3000"]
        link_args = ["run", "60", "mirror", "from", address, "900", "check", stats + "+8=0", "check", stats + "+12=1"]
    else:
        pc_args = ["--autoplay", "--realtime", "--until-frame", "1000"]
        # The GBA followed into the second room, compared its state and never differed.
        link_args = ["run", "30", "mirror", "to", address, "800", "check", stats + "+16=1", "check", stats + "+88=0", "check", stats + "+84"]
    pc = subprocess.Popen([game, pack, "--headless", "--listen", str(port)] + pc_args, stdout=subprocess.PIPE, text=True)
    try:
        time.sleep(0.5)                      # until it listens
        link = subprocess.run([bmlink, "--mgba", image] + link_args, capture_output=True, text=True, timeout=150)
        print(link.stdout + link.stderr, end="")
        out = pc.communicate(timeout=60)[0]
    except Exception:
        pc.kill()
        raise
    print(out, end="")
    ok = link.returncode == 0 and pc.returncode == 0 and "0 restarts" in link.stdout
    if direction == "from":
        m = re.search(r"(\d+) state checks against the other device, (\d+) different", out)
        ok = ok and m and int(m.group(1)) >= 10 and int(m.group(2)) == 0
    else:
        m = re.search(r"bm_game_stats\+84 = (\d+)", link.stdout)
        ok = ok and m and int(m.group(1)) >= 10
    print("ok" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
