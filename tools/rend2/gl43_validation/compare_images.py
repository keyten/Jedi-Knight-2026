#!/usr/bin/env python3
"""Compare two validation captures, reporting exact and maximum differences."""

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError as exc:
    raise SystemExit("Pillow is required (run with the workspace Python runtime)") from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--diff", type=Path, help="write an amplified RGB diff")
    args = parser.parse_args()

    left = Image.open(args.left).convert("RGBA")
    right = Image.open(args.right).convert("RGBA")
    if left.size != right.size:
        print(f"size mismatch: {left.size} != {right.size}")
        return 2
    a, b = left.tobytes(), right.tobytes()
    pixels = left.width * left.height
    differing = 0
    max_delta = 0
    for offset in range(0, len(a), 4):
        delta = max(abs(a[offset + channel] - b[offset + channel]) for channel in range(4))
        differing += delta != 0
        max_delta = max(max_delta, delta)
    print(f"{differing}/{pixels} pixels differ; max 8-bit channel delta {max_delta}")
    if args.diff:
        diff = ImageChops.difference(left.convert("RGB"), right.convert("RGB"))
        diff.point(lambda value: min(255, value * 8)).save(args.diff)
        print(f"amplified diff: {args.diff}")
    return 0 if differing == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
