#!/usr/bin/env python3
"""Compare two validation captures, reporting exact and maximum differences."""

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError as exc:
    raise SystemExit("Pillow is required (run with the workspace Python runtime)") from exc


def compare(left_path, right_path, diff_path=None, max_delta=0, max_pixels=0, max_mean=0.0):
    """Reusable 8-bit comparison; defaults preserve the existing exact contract."""
    left = Image.open(left_path).convert('RGBA')
    right = Image.open(right_path).convert('RGBA')
    if left.size != right.size:
        raise ValueError(f'size mismatch: {left.size} != {right.size}')
    delta = ImageChops.difference(left, right)
    histogram = delta.histogram()
    pixels = delta.get_flattened_data() if hasattr(delta, 'get_flattened_data') else delta.getdata()
    result = dict(pixels=left.width * left.height,
                  differing_pixels=sum(max(pixel) != 0 for pixel in pixels),
                  max_channel_delta=max((i % 256 for i, count in enumerate(histogram) if count), default=0),
                  mean_rgb_delta=sum((i % 256) * count for i, count in enumerate(histogram[:768])) / (left.width * left.height * 3))
    result['exact'] = result['differing_pixels'] == 0
    result['passed'] = (result['max_channel_delta'] <= max_delta and
                        result['differing_pixels'] <= max_pixels and result['mean_rgb_delta'] <= max_mean)
    if diff_path:
        diff = ImageChops.difference(left.convert('RGB'), right.convert('RGB'))
        diff.point(lambda value: min(255, value * 8)).save(diff_path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--diff", type=Path, help="write an amplified RGB diff")
    args = parser.parse_args()

    try:
        result = compare(args.left, args.right, args.diff)
    except ValueError as exc:
        print(str(exc))
        return 2
    print(f"{result['differing_pixels']}/{result['pixels']} pixels differ; max 8-bit channel delta {result['max_channel_delta']}")
    if args.diff:
        print(f"amplified diff: {args.diff}")
    return 0 if result['passed'] else 1


if __name__ == "__main__":
    raise SystemExit(main())
