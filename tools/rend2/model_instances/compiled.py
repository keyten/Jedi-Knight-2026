"""Fresh owned q3map2 MD3 instance/transform correspondence validation."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from instances import Assets, build, digest, write
from test_instances import entity, md3
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"bsp_fixtures"))
from bsp_fixtures.compiled import brush, tga


def author(output):
    base = output/"authoring/base"
    for folder in ("models", "maps", "shaders", "textures/asr10"):
        (base/folder).mkdir(parents=True, exist_ok=True)
    (base/"models/fixture.md3").write_bytes(md3(2))
    for material in ("housing", "emitter", "remapped"):
        tga(base/("textures/asr10/"+material+".tga"), 8, 8, lambda x, y: (200, 200, 200))
    shader = "textures/asr09/caulk\n{\n surfaceparm nodraw\n surfaceparm nolightmap\n}\n"
    for name in ("housing", "emitter", "remapped"):
        shader += "textures/asr10/%s\n{\n {\n map textures/asr10/%s.tga\n }\n}\n" % (name, name)
    (base/"shaders/asr10.shader").write_text(shader, encoding="ascii")
    (base/"shaders/shaderlist.txt").write_text("asr10\n", encoding="ascii")
    walls = [((-128,-128,-16),(256,256,0)), ((-128,-128,192),(256,256,208)),
             ((-128,-128,0),(-112,256,192)), ((240,-128,0),(256,256,192)),
             ((-112,-128,0),(240,-112,192)), ((-112,240,0),(240,256,192))]
    text = '{\n"classname" "worldspawn"\n'+''.join(brush(lo, hi) for lo, hi in walls)+'}\n'
    text += entity([("classname", "info_player_start"), ("origin", "64 64 32")])
    text += entity([("classname", "misc_model"), ("model", "models/fixture.md3"),
                    ("origin", "0 0 64"), ("angles", "30 90 20"), ("modelscale_vec", "2 3 4"),
                    ("_frame", "1"), ("_remap", "textures/asr10/emitter;textures/asr10/remapped")])
    text += entity([("classname", "misc_model"), ("model", "models/fixture.md3"),
                    ("origin", "64 0 64"), ("angles", "0 90 0"),
                    ("modelscale", "2"), ("modelscale_vec", "3 4 5")])
    text += entity([("classname", "misc_model_static"), ("model", "models/fixture.md3"),
                    ("origin", "0 32 64"), ("angles", "0 90 0"), ("modelscale_vec", "2 3 4"),
                    ("_frame", "1"), ("skin", "models/ignored.skin"), ("zoffset", "1000")])
    text += entity([("classname", "misc_model_static"), ("model", "models/missing.md3"), ("origin", "128 32 64")])
    text += entity([("classname", "misc_model_breakable"), ("model", "models/fixture.md3"),
                    ("origin", "64 32 64"), ("spawnflags", "2")])
    path = base/"maps/asr10.map"
    path.write_text(text, encoding="ascii")
    return base, path


def verify(result):
    checks = 0
    def need(ok, message):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(message)
    need(len(result["instances"]) == 5, "Five model records, no duplicated runtime source-map entities")
    compiled = [i for i in result["instances"] if i["role"] == "compiler_baked_model"]
    need(len(compiled) == 2, "Two stripped source compiler placements")
    for i in compiled:
        need(i["bsp_presence"] == "complete_geometry_correspondence", "Complete original compiler triangle/material correspondence: "+str(i))
        need(len(i["bsp_triangle_refs"]) == 2, "Both original MD3 surfaces correspond")
        need(not i["additional_geometry_eligible"], "Compiler geometry never imported twice")
        need(i["entity_origin"] == "source_map", "Stripped source identity retained")
    external, = [i for i in result["instances"] if i["role"] == "external_static_runtime" and i["asset_status"] == "available"]
    need(external["static_eligible"] and external["additional_geometry_eligible"], "Known static external placement")
    need(external["frame"] == 0, "Static loader frame zero")
    need(external["world_bounds"]["min"][2] == 64, "zoffset never moves geometry")
    need(external["world_bounds"]["max"][2] == 68, "Vertical scale applied to vertices")
    need(abs(external["world_bounds"]["min"][0]+12) < 1e-5, "Yaw/scaled world bound X")
    need(abs(external["world_bounds"]["max"][1]-36) < 1e-5, "Yaw/scaled world bound Y")
    missing, = [i for i in result["instances"] if i["asset_status"] == "unavailable"]
    need(missing["world_bounds"] is None and missing["surfaces"] == [], "No missing-model geometry invented")
    dynamic, = [i for i in result["instances"] if i["role"] == "dynamic_or_effect_model"]
    need(not dynamic["static_eligible"] and dynamic["world_bounds"] is None, "Animated breakable excluded")
    for i in result["instances"]:
        need(i["historical_bake_participation"] == "unknown", "No bake participation invented")
        need(i["occlusion_participation"] == "unknown", "No automatic occlusion assumption")
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
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
                         encoding="utf-8", errors="replace")
    (output/"compiler.log").write_text(run.stdout, encoding="utf-8")
    if run.returncode:
        raise ValueError("q3map2 failed; inspect compiler.log")
    data, source = path.with_suffix(".bsp").read_bytes(), path.read_bytes()
    assets = Assets(base)
    try:
        result = build(data, assets, source_map=source)
        write(result, output/"catalogue", True)
        checks = verify(result)
        repeat = build(data, assets, source_map=source)
        if result != repeat:
            raise AssertionError("Repeated compiler catalogue differs")
        report = dict(checks=checks, counts=result["counts"], compiler_sha256=digest(compiler.read_bytes()),
            command=command, bsp_sha256=digest(data), source_map_sha256=digest(source),
            repeated_extraction_equal=True, source="owned MD3 and fresh q3map2 -meta compilation",
            scope="pose/geometry correspondence only; no light phase, historical solver or runtime consumer")
        (output/"validation.json").write_text(json.dumps(report, sort_keys=True, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(report, sort_keys=True))
    finally:
        assets.close()


if __name__ == "__main__":
    main()
