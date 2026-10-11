#!/usr/bin/env python3
"""Generate targeted eye masks and a deterministic removable test PK3."""

from __future__ import annotations

import binascii
import math
import struct
import zlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TREE = ROOT / "pk3"
MAPS = TREE / "models" / "players" / "_hair_eye_test"
OUTPUT = ROOT / "zzzz_hair_eye_test.pk3"


def ellipse_coverage(x: float, y: float, cx: float, cy: float, rx: float, ry: float) -> float:
    # Two-pixel feather keeps mipmapped mask edges stable without changing the diffuse.
    distance = math.sqrt(((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2)
    feather = 2.0 / max(rx, ry)
    return max(0.0, min(1.0, (1.0 + feather - distance) / (2.0 * feather)))


def write_png(path: Path, width: int, height: int, ellipses: list[tuple[float, float, float, float]],
              skin_complement: bool = False) -> None:
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: None
        for x in range(width):
            coverage = max(ellipse_coverage(x + 0.5, y + 0.5, *e) for e in ellipses)
            if skin_complement:
                # One shared sampler: R is the complementary skin SSS mask,
                # RG is otherwise only interpreted for hair direction, and B
                # is eye-model coverage.
                rows.extend((round(255.0 * (1.0 - coverage)), 128,
                             round(255.0 * coverage), 255))
            else:
                rows.extend((128, 128, round(255.0 * coverage), 255))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> None:
    # Kyle: the single eyeball UV island; the other atlas regions are mouth/teeth.
    write_png(MAPS / "kyle_mouth_eyes_material.png", 128, 128, [(45, 20, 18, 18)])
    # Jedi HF: two spherical-eye islands above the painted face diffuse.
    jedi_eyes = [(139, 101, 69, 63), (374, 101, 69, 63)]
    # face_a also contains skin: R keeps SSS off the eye islands while B
    # selects the eye model, avoiding a second high-numbered sampler.
    write_png(MAPS / "jedi_hf_face_a_material.png", 512, 512, jedi_eyes,
              skin_complement=True)

    entries = sorted(p for p in TREE.rglob("*") if p.is_file())
    with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in entries:
            info = zipfile.ZipInfo(path.relative_to(TREE).as_posix(), (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)
    print(f"wrote {OUTPUT} ({len(entries)} files)")


if __name__ == "__main__":
    main()
