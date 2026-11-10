#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Compares two binary PPM (P6) images pixel by pixel in 5-bit-per-channel colour (the GBA's
colour depth). Exits with 1 and reports the differences if they are not equal.

  compare_images.py <a.ppm> <b.ppm> [--diff out.ppm] [--changed-in X0 Y0 X1 Y1]

With --changed-in the images must differ, and only inside that box (inclusive): for checking
that a live reload changed exactly what it should.
"""
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = []
    pos = 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    pos += 1
    if parts[0] != b"P6" or parts[3] != b"255":
        raise ValueError(f"{path}: only binary PPM with 8 bits per channel is supported")
    return int(parts[1]), int(parts[2]), data[pos:]


def main():
    args = sys.argv[1:]
    diff_path = None
    if "--diff" in args:
        i = args.index("--diff")
        diff_path = args[i + 1]
        del args[i:i + 2]
    box = None
    if "--changed-in" in args:
        i = args.index("--changed-in")
        box = tuple(int(v) for v in args[i + 1:i + 5])
        del args[i:i + 5]
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    wa, ha, a = read_ppm(args[0])
    wb, hb, b = read_ppm(args[1])
    if (wa, ha) != (wb, hb):
        print(f"size differs: {wa}x{ha} vs {wb}x{hb}")
        return 1
    diffs = []
    out = bytearray(len(a))
    for i in range(0, wa * ha * 3, 3):
        pa = tuple(v >> 3 for v in a[i:i + 3])
        pb = tuple(v >> 3 for v in b[i:i + 3])
        if pa != pb:
            diffs.append(i // 3)
            out[i:i + 3] = b"\xff\x00\xff"
        else:
            out[i:i + 3] = bytes(v // 4 for v in a[i:i + 3])
    if diff_path:
        with open(diff_path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (wa, ha) + bytes(out))
    if box is not None:
        x0, y0, x1, y1 = box
        outside = [d for d in diffs if not (x0 <= d % wa <= x1 and y0 <= d // wa <= y1)]
        if not diffs or outside:
            print(f"expected changes only in {box}: {len(diffs)} pixels differ, {len(outside)} outside")
            return 1
        print(f"{len(diffs)} pixels changed, all inside {box}")
        return 0
    if diffs:
        first = diffs[0]
        print(f"{len(diffs)} of {wa * ha} pixels differ; first at x={first % wa} y={first // wa}")
        return 1
    print(f"images match ({wa}x{ha})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
