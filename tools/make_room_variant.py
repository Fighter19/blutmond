#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
# Copyright (C) 2025 Blutmond contributors
"""Writes a copy of a Tiled room with one background tile changed (for live reload tests).

usage: make_room_variant.py <in.tmx> <out.tmx>
Relative tileset paths are made absolute so the copy can live anywhere.
"""
import os
import re
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    base = os.path.dirname(os.path.abspath(src))
    text = open(src, encoding='utf-8').read()
    text = re.sub(r'source="([^"]+)"', lambda m: 'source="%s"' % os.path.normpath(os.path.join(base, m.group(1))), text)
    # First tile of the background layer: sky (9) becomes a cloud tile (11).
    marker = '<data encoding="csv">\n'
    at = text.index(marker) + len(marker)
    first = text[at:].split(',', 1)[0]
    text = text[:at] + ('11' if first != '11' else '12') + text[at + len(first):]
    open(dst, 'w', encoding='utf-8').write(text)


if __name__ == '__main__':
    main()
