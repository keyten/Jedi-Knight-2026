"""ASR03 metadata audit of three installed stock fixtures; not a model importer.

Reads MD3 headers, surface shader names and matching shader declarations only.
No game geometry, textures or shader bodies are exported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

FIXTURES = (
    "models/map_objects/hoth/light_wall.md3",
    "models/map_objects/factory/ceiling_light.md3",
    "models/map_objects/taspir/lamp2_double.md3",
)


def cstring(value):
    return value.split(b"\0", 1)[0].decode("ascii")


def inspect_md3(data):
    if len(data) < 108:
        raise ValueError("Truncated MD3 header")
    header = struct.unpack_from("<4si64s9i", data)
    if header[0] != b"IDP3" or header[1] != 15 or header[-1] != len(data):
        raise ValueError("Unsupported MD3 or invalid extent")
    if not 0 <= header[6] <= 4096:
        raise ValueError("Invalid MD3 surface count")
    offset = header[-2]
    surfaces = []
    for _ in range(header[6]):
        if offset < 108 or offset + 108 > len(data):
            raise ValueError("Invalid MD3 surface offset")
        h = struct.unpack_from("<4s64s10i", data, offset)
        end = offset + h[-1]
        if h[0] != b"IDP3" or not offset + 108 <= end <= len(data):
            raise ValueError("Invalid MD3 surface extent")
        if not 0 <= h[4] <= 4096 or h[8] < 108 or offset + h[8] + 68*h[4] > end:
            raise ValueError("Invalid MD3 shader table")
        shaders = [cstring(struct.unpack_from("<64si", data, offset+h[8]+68*i)[0])
                   for i in range(h[4])]
        surfaces.append(dict(name=cstring(h[1]), frames=h[3], vertices=h[5],
                             triangles=h[6], shaders=shaders))
        offset = end
    return dict(frames=header[4], surfaces=surfaces)


def declarations(text, material):
    # Match source definitions only. This audit does not emulate renderer finishing.
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    pattern = r"(?im)^\s*" + re.escape(material) + r"\s*\{"
    for match in re.finditer(pattern, text):
        start, depth, end = match.end(), 1, match.end()
        while depth and end < len(text):
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        if depth:
            raise ValueError("Unterminated shader declaration")
        body = text[start:end-1]
        yield dict(glow=bool(re.search(r"(?im)^\s*glow\s*$", body)),
                   rgb_wave=bool(re.search(r"(?i)\brgbGen\s+wave\b", body)),
                   two_sided=bool(re.search(r"(?i)\bcull\s+(?:twosided|none)\b", body)),
                   maps=re.findall(r"(?im)^\s*(?:clampmap|map)\s+([^\s]+)", body))


def inventory(base):
    rows = []
    with zipfile.ZipFile(base / "assets1.pk3") as archive:
        for name in FIXTURES:
            data = archive.read(name)
            rows.append(dict(archive="assets1.pk3", path=name,
                             sha256=hashlib.sha256(data).hexdigest(), **inspect_md3(data)))
    materials = {str(Path(shader).with_suffix("")).replace("\\", "/")
                 for row in rows for s in row["surfaces"] for shader in s["shaders"]}
    matches = {material: [] for material in sorted(materials)}
    for path in sorted(base.glob("assets?.pk3")):
        with zipfile.ZipFile(path) as archive:
            for name in sorted(archive.namelist()):
                if name.startswith("shaders/") and name.endswith(".shader"):
                    text = archive.read(name).decode("latin1")
                    for material in matches:
                        for facts in declarations(text, material):
                            matches[material].append(dict(archive=path.name, path=name, **facts))
    return dict(scope="Stock metadata only; no instance pose, fitted emission or historical-source claim",
                fixtures=rows, shader_declarations=matches,
                shader_scope="All matching definitions in assets?.pk3; loose/mod overrides not inspected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="Installed GameData/base directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(inventory(args.base), indent=2, sort_keys=True)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
