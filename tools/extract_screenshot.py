#!/usr/bin/env python3
"""Extract prefixed RGB565 framebuffer dumps from firmware serial logs."""

import argparse
import base64
import re
import struct
import sys
from pathlib import Path


BEGIN_RE = re.compile(r"SCREENSHOT_RGB565_BEGIN:(\d+):(\d+):(\d+)")
DATA_PREFIX = "SCREENSHOT_RGB565_DATA:"
END_MARKER = "SCREENSHOT_RGB565_END"


def extract(lines):
    metadata = None
    chunks = []
    for raw_line in lines:
        line = raw_line.strip()
        match = BEGIN_RE.search(line)
        if match:
            metadata = tuple(map(int, match.groups()))
            chunks = []
            continue
        if metadata and DATA_PREFIX in line:
            chunks.append(line.split(DATA_PREFIX, 1)[1])
            continue
        if metadata and END_MARKER in line:
            data = base64.b64decode("".join(chunks), validate=True)
            yield metadata, data
            metadata = None
            chunks = []


def rgb565_to_bmp(width, height, pixels):
    row_size = (width * 3 + 3) & ~3
    image_size = row_size * height
    header = bytearray(54)
    struct.pack_into("<2sIHHI", header, 0, b"BM", 54 + image_size, 0, 0, 54)
    struct.pack_into("<IiiHHIIiiII", header, 14, 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0)
    output = bytearray(header)
    padding = bytes(row_size - width * 3)
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            value = struct.unpack_from("<H", pixels, (y * width + x) * 2)[0]
            red5 = (value >> 11) & 0x1F
            green6 = (value >> 5) & 0x3F
            blue5 = value & 0x1F
            red = (red5 << 3) | (red5 >> 2)
            green = (green6 << 2) | (green6 >> 4)
            blue = (blue5 << 3) | (blue5 >> 2)
            row.extend((blue, green, red))
        output.extend(row)
        output.extend(padding)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logfile", nargs="?", help="Serial log; reads stdin when omitted")
    parser.add_argument("-o", "--output", default="screenshot.bmp")
    args = parser.parse_args()
    lines = Path(args.logfile).read_text(errors="ignore").splitlines() if args.logfile else sys.stdin
    screenshots = list(extract(lines))
    if not screenshots:
        print("No complete SCREENSHOT_RGB565 block found", file=sys.stderr)
        return 1
    (width, height, expected), pixels = screenshots[-1]
    if len(pixels) != expected or expected != width * height * 2:
        print(f"Invalid framebuffer size: expected {expected}, got {len(pixels)}", file=sys.stderr)
        return 1
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(rgb565_to_bmp(width, height, pixels))
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
