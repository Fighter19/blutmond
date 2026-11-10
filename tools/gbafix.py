#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Fills in the header of a GBA image: title, game code and the complement check.

The Nintendo logo (156 bytes at offset 0x04) is not part of this repository. Real hardware only
boots an image that contains it; emulators do not check it, and multiboot tools such as xcomms
insert it during upload. Pass --logo with a file that holds the 156 logo bytes (or a ROM to take
them from) to insert it here.
"""
import argparse
import sys

LOGO_OFFSET, LOGO_SIZE = 0x04, 156
TITLE_OFFSET, TITLE_SIZE = 0xA0, 12
CODE_OFFSET = 0xAC
COMPLEMENT_OFFSET = 0xBD


def complement(header: bytes) -> int:
    value = 0
    for b in header[0xA0:0xBD]:
        value -= b
    return (value - 0x19) & 0xFF


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image")
    ap.add_argument("--title", default="BLUTMOND")
    ap.add_argument("--code", default="BMND")
    ap.add_argument("--logo", help="file with the 156 logo bytes, or a GBA ROM to copy them from")
    ap.add_argument("--pad", action="store_true", help="pad the image to a multiple of 256 bytes")
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = bytearray(f.read())
    if len(data) < 0xC0:
        print(f"{args.image}: too small for a GBA header", file=sys.stderr)
        return 1

    if args.logo:
        with open(args.logo, "rb") as f:
            logo = f.read()
        if len(logo) >= LOGO_OFFSET + LOGO_SIZE and len(logo) != LOGO_SIZE:
            logo = logo[LOGO_OFFSET:LOGO_OFFSET + LOGO_SIZE]
        if len(logo) != LOGO_SIZE:
            print(f"{args.logo}: expected {LOGO_SIZE} logo bytes", file=sys.stderr)
            return 1
        data[LOGO_OFFSET:LOGO_OFFSET + LOGO_SIZE] = logo

    title = args.title.upper().encode("ascii")[:TITLE_SIZE].ljust(TITLE_SIZE, b"\0")
    data[TITLE_OFFSET:TITLE_OFFSET + TITLE_SIZE] = title
    data[CODE_OFFSET:CODE_OFFSET + 4] = args.code.encode("ascii")[:4].ljust(4, b"0")
    data[0xB2] = 0x96
    data[COMPLEMENT_OFFSET] = complement(data)

    if args.pad and len(data) % 256:
        data += b"\0" * (256 - len(data) % 256)

    with open(args.image, "wb") as f:
        f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
