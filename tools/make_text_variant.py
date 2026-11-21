#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Copies a text file with one piece of text replaced (for tests that need a changed asset).

usage: make_text_variant.py <in> <out> <from> <to>
"""
import sys


def main():
    src, dst, old, new = sys.argv[1:5]
    with open(src) as f:
        text = f.read()
    if old not in text:
        print(f"{src} does not contain {old!r}", file=sys.stderr)
        return 1
    with open(dst, "w") as f:
        f.write(text.replace(old, new))
    return 0


if __name__ == "__main__":
    sys.exit(main())
