#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Fails if engine code outside a platform back end contains a GBA hardware address.

Hardware addresses (0x02000000-0x0EFFFFFF) may only appear in src/hal/<platform>/ and in linker
scripts. See docs/PLAN.md section 3.
"""
import os
import re
import sys

ADDRESS = re.compile(r"\b0x0[2-9A-Ea-e][0-9A-Fa-f]{6}u?\b")
ALLOWED_DIRS = (os.path.join("src", "hal", "gba"),)


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    bad = []
    for base, _, files in os.walk(os.path.join(root, "src")):
        rel = os.path.relpath(base, root)
        if rel.startswith(ALLOWED_DIRS):
            continue
        for name in files:
            if not name.endswith((".c", ".h", ".s", ".S")):
                continue
            path = os.path.join(base, name)
            with open(path, encoding="utf-8", errors="replace") as f:
                for number, line in enumerate(f, 1):
                    if ADDRESS.search(line):
                        bad.append(f"{os.path.relpath(path, root)}:{number}: {line.strip()}")
    for entry in bad:
        print(entry)
    if bad:
        print(f"{len(bad)} hardware address(es) outside src/hal/gba", file=sys.stderr)
        return 1
    print("no hardware addresses outside src/hal/gba")
    return 0


if __name__ == "__main__":
    sys.exit(main())
