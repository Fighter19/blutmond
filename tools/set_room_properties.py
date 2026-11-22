#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes a copy of a Tiled room with map properties added or changed (for tests).

usage: set_room_properties.py <in.tmx> <out.tmx> name=value...
Relative tileset paths are made absolute so the copy can live anywhere.
"""
import os
import re
import sys
from xml.sax.saxutils import quoteattr


def main():
    src, dst, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
    base = os.path.dirname(os.path.abspath(src))
    text = open(src, encoding='utf-8').read()
    text = re.sub(r'source="([^"]+)"', lambda m: 'source="%s"' % os.path.normpath(os.path.join(base, m.group(1))), text)
    start = text.find('<properties>')
    if start < 0 or start > text.find('<tileset'):
        print(f"{src}: no map properties", file=sys.stderr)
        return 1
    end = text.find('</properties>', start)
    block = text[start:end]
    for pair in pairs:
        name, value = pair.split('=', 1)
        block = re.sub(r'\s*<property name=%s[^>]*/>' % re.escape(quoteattr(name)), '', block)
        block += '  <property name=%s value=%s />\n ' % (quoteattr(name), quoteattr(value))
    text = text[:start] + block + text[end:]
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
