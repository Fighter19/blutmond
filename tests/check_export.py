#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Export test: appends a pack to the game's cartridge image with bmexport, runs the result in
mGBA and checks that the game plays the appended pack (s_pack points behind the program and
has the pack's size), not the one embedded in the image.

  check_export.py <bmexport> <bm_mgba_run> <game.gba> <game.elf> <pack.bmpk> <out.gba>
"""
import os
import re
import subprocess
import sys

MAGIC = b"BMROMPACKSLT"


def main() -> int:
    bmexport, mgba_run, runtime, elf, pack, out = sys.argv[1:7]
    subprocess.run([bmexport, runtime, pack, out], check=True)
    with open(out, "rb") as f:
        rom = f.read()
    at = rom.find(MAGIC)
    if at < 0:
        print("no pack slot in the exported image")
        return 1
    offset = int.from_bytes(rom[at + 12:at + 16], "little")
    size = int.from_bytes(rom[at + 16:at + 20], "little")
    pack_size = os.path.getsize(pack)
    runtime_size = os.path.getsize(runtime)
    if size != pack_size or offset < runtime_size or offset % 256:
        print(f"slot offset {offset}, size {size}; pack {pack_size} bytes, runtime {runtime_size} bytes")
        return 1
    with open(pack, "rb") as f:
        if rom[offset:offset + size] != f.read():
            print("the pack in the image differs from the file")
            return 1
    complement = (-sum(rom[0xA0:0xBD]) - 0x19) & 0xFF
    if rom[0xBD] != complement:
        print("header complement is wrong")
        return 1

    run = subprocess.run([mgba_run, out, "--frames", "120", "--print", f"{elf}:s_pack", "--print", f"{elf}:s_pack+4"],
                         capture_output=True, text=True)
    print(run.stdout, run.stderr)
    if run.returncode != 0:
        return 1
    values = dict(re.findall(r"^(\S+) = (\d+)", run.stdout, re.M))
    data, used = int(values.get("s_pack", 0)), int(values.get("s_pack+4", 0))
    if data != 0x08000000 + offset or used != size:
        print(f"the game plays a pack at {data:#x} of {used} bytes, expected {0x08000000 + offset:#x} of {size}")
        return 1
    print(f"exported image plays the appended pack ({size} bytes at {offset:#x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
