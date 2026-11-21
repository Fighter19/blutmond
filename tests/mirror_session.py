#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""The PC game plays along with a GBA (docs/spec/link-protocol.md section 10):

  mirror_session.py <bmlink> <game> <pack> <image.gba> <image.elf>

The GBA image (in mGBA) is played by its autopilot; bmlink mirrors it into a PC game that
waits for it. The PC game must reach the same frames with the same state: it compares a
checksum of its state with the GBA's every 64 frames and ends with an error if one differed.
The run goes through a room change."""

import re
import socket
import subprocess
import sys
import time


def main():
    bmlink, game, pack, image, elf = sys.argv[1:6]
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    # After the mirror ends the PC game plays on by itself to frame 3000 and ends.
    pc = subprocess.Popen([game, pack, "--headless", "--listen", str(port), "--follow", "--until-frame", "3000"],
                          stdout=subprocess.PIPE, text=True)
    try:
        time.sleep(0.5)                      # until it listens
        stats = elf + ":bm_game_stats"
        link = subprocess.run([bmlink, "--mgba", image, "run", "60", "mirror", "127.0.0.1:%d" % port, "900",
                               "check", stats + "+8=0", "check", stats + "+12=1"], capture_output=True, text=True, timeout=120)
        print(link.stdout + link.stderr, end="")
        out = pc.communicate(timeout=60)[0]
    except Exception:
        pc.kill()
        raise
    print(out, end="")
    m = re.search(r"(\d+) state checks against the other device, (\d+) different", out)
    ok = link.returncode == 0 and pc.returncode == 0 and m and int(m.group(1)) >= 10 and int(m.group(2)) == 0
    ok = ok and "0 restarts" in link.stdout
    print("ok" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
