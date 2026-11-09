"""Repeatable stock MD3 placement inventory; no commercial geometry redistribution."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re

from instances import Assets, build, digest, write


def verify_examples(result):
    """Independent yaw-only scalar formulas check selected stock fixture poses.

    Placement truth is the retained source entity, not historical light identity.
    Models are selected for inspection by name; this is not emission detection.
    """
    examples = []
    for instance in result["instances"]:
        if not instance["static_eligible"] or not re.search(r"light|lamp", instance["model"]):
            continue
        fields = instance["source_fields"]
        if "angles" in fields or "modelscale_vec" in fields:
            continue
        yaw = float(fields.get("angle", "0"))*math.pi/180
        scale = float(fields.get("modelscale", "1").split()[0]) or 1.
        origin = tuple(float(v) for v in fields.get("origin", "0 0 0").split())
        asset = result["assets"][instance["model_asset"]]
        expected = [(origin[0]+scale*(p[0]*math.cos(yaw)-p[1]*math.sin(yaw)),
                     origin[1]+scale*(p[0]*math.sin(yaw)+p[1]*math.cos(yaw)),
                     origin[2]+scale*p[2]) for surface in asset["surfaces"] for p in surface["positions"]]
        for k in range(3):
            if abs(min(p[k] for p in expected)-instance["world_bounds"]["min"][k]) > 1e-6 or abs(max(p[k] for p in expected)-instance["world_bounds"]["max"][k]) > 1e-6:
                raise AssertionError("Stock world bounds disagree with entity yaw/scale: "+instance["id"])
        examples.append(dict(id=instance["id"], entity_ordinal=instance["entity_ordinal"], model=instance["model"],
            model_sha256=asset["sha256"], transform=instance["transform"], world_bounds=instance["world_bounds"],
            source_fields=fields, md3_surfaces=len(instance["surfaces"]), bsp_presence=instance["bsp_presence"]))
    return examples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--maps", nargs="+", default=["hoth2", "hoth3", "taspir1"])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if any(not re.fullmatch(r"[a-z0-9_-]+", name) for name in args.maps):
        parser.error("Expected lowercase bare map names")
    assets = Assets(args.base)
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(schema="org.openjk.research.md3-instance-inventory/1", profile="sp", maps={},
        scope="Original BSP entity placements and available MD3 poses; bake/occlusion participation unknown",
        selection="explicit loose base then reverse lexical PK3; source paths case folded",
        stripped_compiler_model_instances="not recoverable without original source placements")
    try:
        for name in args.maps:
            data, source = assets.read("maps/"+name+".bsp")
            if data is None:
                raise ValueError("Missing BSP: "+name)
            result = build(data, assets)
            result.update(map_path="maps/"+name+".bsp", map_source=source)
            examples = verify_examples(result)
            output = args.output/name
            write(result, output)
            first = {p.name: digest(p.read_bytes()) for p in output.iterdir() if p.suffix in (".json", ".tsv")}
            repeat = build(data, assets)
            repeat.update(map_path="maps/"+name+".bsp", map_source=source)
            write(repeat, output)
            second = {p.name: digest(p.read_bytes()) for p in output.iterdir() if p.name in first}
            if first != second:
                raise AssertionError("Stock repeated exports differ: "+name)
            report["maps"][name] = dict(map_sha256=digest(data), source=source, counts=result["counts"],
                output_sha256=first, repeated_export_byte_equal=True,
                fixture_name_examples_checked=len(examples), representative_examples=examples[:3],
                bsp_correspondence=dict(sorted(Counter(i["bsp_presence"] for i in result["instances"]).items())),
                issues=dict(sorted(Counter(issue for i in result["instances"] for issue in i["issues"]).items())))
            print(name, json.dumps(result["counts"], sort_keys=True), "fixture-name pose checks=", len(examples))
        (args.output/"inventory.json").write_text(json.dumps(report, indent=2, sort_keys=True)+"\n", encoding="utf-8")
    finally:
        assets.close()


if __name__ == "__main__":
    main()
