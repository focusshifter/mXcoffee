#!/usr/bin/env python3
"""Convert an exact-size PNG into row-major big-endian RGB565 pixels."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def dimensions(path: Path) -> tuple[int, int]:
    output = subprocess.check_output(
        ["identify", "-format", "%w %h", str(path)], text=True
    )
    width, height = output.split()
    return int(width), int(height)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument(
        "--core2-compensate",
        action="store_true",
        help="pre-compensate for the simulator's approximate Core2 panel profile",
    )
    args = parser.parse_args()

    actual = dimensions(args.input)
    expected = (args.width, args.height)
    if actual != expected:
        raise SystemExit(f"expected {expected[0]}x{expected[1]}, got {actual[0]}x{actual[1]}")

    rgb = subprocess.check_output(
        ["convert", str(args.input), "-alpha", "off", "-depth", "8", "rgb:-"]
    )
    expected_bytes = args.width * args.height * 3
    if len(rgb) != expected_bytes:
        raise SystemExit(f"expected {expected_bytes} RGB bytes, got {len(rgb)}")

    output = bytearray(args.width * args.height * 2)
    for pixel in range(args.width * args.height):
        red, green, blue = rgb[pixel * 3 : pixel * 3 + 3]
        if args.core2_compensate:
            red = inverse_panel_channel(red, 0.0, 0.95)
            green = inverse_panel_channel(green, 14.0, 0.82)
            blue = inverse_panel_channel(blue, 22.0, 0.70)
        raw = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output[pixel * 2] = raw >> 8
        output[pixel * 2 + 1] = raw & 0xFF

    args.output.write_bytes(output)
    print(f"wrote {args.output} ({len(output)} bytes)")


def inverse_panel_channel(value: int, black_floor: float, gamma: float) -> int:
    if value <= black_floor:
        return 0
    normalized = (value - black_floor) / (255.0 - black_floor)
    return round(255.0 * normalized ** (1.0 / gamma))


if __name__ == "__main__":
    main()
