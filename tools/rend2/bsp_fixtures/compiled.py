"""Compile owned fixture brushes and verify recovered geometry against authoring.

Requires a JA-capable q3map2 executable and Pillow. Nothing is installed into the
game. Source textures, map, compiler log and inspectable catalogue stay in output.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from catalogue import Assets, Masks, catalogue, digest, write_catalogue


CASES = [
    ("bulb", (0, 0, 32), (2, 2), 1, 4),
    ("bulb", (2.125, 0, 32), (2, 2), 1, 4),
    ("strip", (0, 32, 48), (64, 2), 1, 128),
    ("panel", (0, 64, 64), (32, 16), 1, 512),
    ("slots", (0, 96, 80), (32, 8), 2, 128),
    ("scale", (0, 128, 96), (32, 8), 2, 128),
    ("rotate", (0, 160, 112), (32, 8), 1, 128),
    ("layers", (0, 192, 128), (32, 8), 1, 256),
]


def brush(lo, hi, material="caulk", shift=(0, 0), scales=(1, 1)):
    x, y, z = lo
    X, Y, Z = hi
    planes = [((x,y,Z),(X,y,Z),(X,Y,Z)), ((x,Y,z),(X,Y,z),(X,y,z)),
              ((x,y,z),(X,y,z),(X,y,Z)), ((X,Y,z),(x,Y,z),(x,Y,Z)),
              ((x,Y,z),(x,y,z),(x,y,Z)), ((X,y,z),(X,Y,z),(X,Y,Z))]
    lines = []
    for i, (a, b, c) in enumerate(planes):
        shader = material if i == 0 else "caulk"
        lines.append(" ".join("( %g %g %g )" % p for p in (a, c, b))+
                     " asr09/%s %g %g 0 %g %g 0 0 0\n" % (shader, *shift, *scales))
    return "{\n"+"".join(lines)+"}\n"


def tga(path, width, height, colour):
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0x20)
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            pixels.extend(reversed(colour(x, y)))
    path.write_bytes(header+pixels)


def author(output):
    base = output/"authoring/base"
    for folder in ("maps", "shaders", "textures/asr09"):
        (base/folder).mkdir(parents=True, exist_ok=True)
    white, dark = (255, 255, 255), (0, 0, 0)
    for name, dims in (("bulb", (2, 2)), ("strip", (64, 2)), ("panel", (32, 16))):
        tga(base/("textures/asr09/"+name+".tga"), *dims, lambda x, y: (255, 188, 137))
    tga(base/"textures/asr09/slots.tga", 32, 8, lambda x, y: white if x < 8 or x >= 24 else dark)
    tga(base/"textures/asr09/half.tga", 32, 8, lambda x, y: white if x < 16 else dark)
    tga(base/"textures/asr09/solid.tga", 32, 8, lambda x, y: white)
    shader = "textures/asr09/caulk\n{\n surfaceparm nodraw\n surfaceparm nolightmap\n}\n"
    for name in ("bulb", "strip", "panel", "slots"):
        shader += "textures/asr09/%s\n{\n q3map_surfacelight 50\n {\n map textures/asr09/%s.tga\n glow\n }\n}\n" % (name, name)
    shader += """textures/asr09/scale
{
 qer_editorimage textures/asr09/half.tga
 {
  map textures/asr09/half.tga
  glow
  tcMod scale 2 1
 }
}
textures/asr09/rotate
{
 qer_editorimage textures/asr09/half.tga
 {
  map textures/asr09/half.tga
  glow
  tcMod transform 0 1 -1 0 1 0
 }
}
textures/asr09/layers
{
 qer_editorimage textures/asr09/solid.tga
 {
  map textures/asr09/solid.tga
  rgbGen const ( 1 0 0 )
  glow
 }
 {
  map textures/asr09/half.tga
  rgbGen const ( 0 0 1 )
  blendFunc add
  glow
 }
}
"""
    (base/"shaders/asr09.shader").write_text(shader, encoding="ascii")
    (base/"shaders/shaderlist.txt").write_text("asr09\n", encoding="ascii")
    walls = [((-64,-64,-16),(128,256,0)), ((-64,-64,192),(128,256,208)),
             ((-64,-64,0),(-48,256,192)), ((112,-64,0),(128,256,192)),
             ((-48,-64,0),(112,-48,192)), ((-48,240,0),(112,256,192))]
    geometry = "".join(brush(lo, hi) for lo, hi in walls)
    for name, (x, y, z), (w, h), _, _ in CASES:
        geometry += brush((x, y, z-2), (x+w, y+h, z), name, (-x, y+h))
    source = '{\n"classname" "worldspawn"\n'+geometry+'}\n'
    source += '{ "classname" "info_player_start" "origin" "64 0 32" }\n'
    path = base/"maps/asr09.map"
    path.write_text(source, encoding="ascii")
    return base, path


def verify(result):
    checks = 0
    def need(ok, message):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(message)
    fixtures = result["fixtures"]
    for name, (x, y, z), (w, h), count, area in CASES:
        fs = [f for f in fixtures if f["material"] == "textures/asr09/"+name and
              abs(f["centroid"][2]-z) < 1e-6 and x-1e-6 <= f["centroid"][0] <= x+w+1e-6 and
              y-1e-6 <= f["centroid"][1] <= y+h+1e-6]
        need(len(fs) == count, name+" region count: "+str(len(fs)))
        need(abs(sum(f["emitting_area"] for f in fs)-area) < 1e-5, name+" clipped emitting area")
        for f in fs:
            need(f["outward"] is not None and f["outward"][2] > .99999, name+" BSP emitting side")
            need(all(abs(p[2]-z) < 1e-6 and x-1e-6 <= p[0] <= x+w+1e-6 and
                     y-1e-6 <= p[1] <= y+h+1e-6 for poly in f["footprint"] for p in poly), name+" world footprint bounds")
            need(f["emitting_footprint_known"] and f["static_eligible"], name+" supported static footprint")
            need(f["historical_bake_participation"] == "unknown", name+" no historical certification")
            shapes = {s["type"]: s for s in f["suggestions"]}
            need("rectangle" in shapes, name+" rectangle suggestion")
            if name in ("bulb", "strip", "panel", "layers"):
                for a, b in zip(f["centroid"], (x+w/2, y+h/2, z)):
                    need(abs(a-b) < 1e-5, name+" expected centroid")
                need(abs(shapes["rectangle"]["width"]-max(w, h)) < 1e-5, name+" width")
                need(abs(shapes["rectangle"]["height"]-min(w, h)) < 1e-5, name+" height")
            if name == "bulb":
                need("point" in shapes, "Compact bulb point alternative")
            if name == "strip":
                need("tube" in shapes, "Long strip tube alternative")
                need(abs(shapes["tube"]["radius"]-1) < 1e-5, "Tube radius")
            if name == "slots":
                need(min(abs(f["centroid"][0]-4), abs(f["centroid"][0]-28)) < 1e-5, "Masked slot position")
                need(len(f["priors"]) == 2, "Glow/compiler hints do not duplicate footprint")
            if name == "scale":
                need(min(abs(f["centroid"][0]-4), abs(f["centroid"][0]-20)) < 1e-5, "Scaled repeated mask position")
            if name == "rotate":
                need(abs(f["centroid"][1]-(y+h*.25)) < 1e-5, "Transformed mask position")
            if name == "layers":
                need(len(f["layers"]) == 2, "Two retained emission layers")
                need(sorted(round(l["area"], 6) for l in f["layers"]) == [128, 256], "Layer area, union not sum")
    need(len(fixtures) == sum(c[3] for c in CASES), "No extra whole-triangle lamps from masked dark parts")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    base, path = author(output)
    compiler = args.compiler.resolve()
    command = [str(compiler), "-game", "ja", "-fs_basepath", str(base.parent), "-fs_game", "base",
               "-threads", "1", "-meta", str(path)]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=180, encoding="utf-8", errors="replace")
    (output/"compiler.log").write_text(run.stdout, encoding="utf-8")
    if run.returncode:
        raise RuntimeError("q3map2 failed; inspect compiler.log")
    data = path.with_suffix(".bsp").read_bytes()
    assets = Assets(base)
    try:
        table, conflicts = assets.shaders()
        result = catalogue(data, table, Masks(assets, 64, .1), conflicts=conflicts)
        checks = verify(result)
        repeat = catalogue(data, table, Masks(assets, 64, .1), conflicts=conflicts)
        if result != repeat:
            raise AssertionError("Repeated extraction differs")
        write_catalogue(result, output/"catalogue")
        manifest = dict(checks=checks, bsp_sha256=digest(data), map_sha256=digest(path.read_bytes()),
                        compiler_sha256=digest(compiler.read_bytes()), command=command,
                        counts=result["counts"], catalogue_sha256=digest((output/"catalogue/catalogue.json").read_bytes()),
                        repeated_extraction_equal=True, source="owned fresh q3map2 -meta compilation",
                        scope="geometry discovery only; compiler light phase and historical solver not invoked")
        (output/"validation.json").write_text(json.dumps(manifest, indent=2, sort_keys=True)+"\n", encoding="utf-8")
        print(json.dumps(manifest, sort_keys=True))
    finally:
        assets.close()


if __name__ == "__main__":
    main()
