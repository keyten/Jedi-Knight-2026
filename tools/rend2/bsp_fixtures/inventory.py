"""Reproducible explicit-asset-view inventory; stock geometry stays in output.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import time

from catalogue import Assets, Masks, catalogue, digest, write_catalogue


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--maps", nargs="+", default=["hoth2", "hoth3"])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mask-axis", type=int, default=32)
    parser.add_argument("--threshold", type=float, default=.1)
    args = parser.parse_args()
    if not 4 <= args.mask_axis <= 512 or not 0 <= args.threshold < float("inf"):
        parser.error("Invalid mask options")
    if any(not name or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789_-" for c in name) for name in args.maps):
        parser.error("Map names must be lowercase bare map names")
    args.output.mkdir(parents=True, exist_ok=True)
    assets = Assets(args.base)
    try:
        table, conflicts = assets.shaders()
        summary = dict(schema="org.openjk.research.bsp-fixture-inventory/1", maps={},
            selection="explicit loose-base then reverse lexical PK3; first lexical shader definition; conflicts flagged",
            interpretation="aesthetic hypotheses; no historical bake or exact lamp certification",
            options=dict(mask_axis=args.mask_axis, threshold=args.threshold, max_work=1000000,
                         max_pieces=250000))
        for name in args.maps:
            data, source = assets.read("maps/"+name+".bsp")
            if data is None:
                raise ValueError("Missing BSP: "+name)
            started = time.monotonic()
            result = catalogue(data, table, Masks(assets, args.mask_axis, args.threshold), summary["options"], conflicts)
            result.update(map_path="maps/"+name+".bsp", map_source=source)
            directory = args.output/name
            write_catalogue(result, directory)
            first = {p.name: digest(p.read_bytes()) for p in directory.iterdir() if p.suffix in (".json", ".tsv", ".obj")}
            repeat = catalogue(data, table, Masks(assets, args.mask_axis, args.threshold), summary["options"], conflicts)
            repeat.update(map_path="maps/"+name+".bsp", map_source=source)
            write_catalogue(repeat, directory)
            second = {p.name: digest(p.read_bytes()) for p in directory.iterdir() if p.name in first}
            if first != second:
                raise AssertionError("Repeated stock export differs: "+name)
            summary["maps"][name] = dict(map_sha256=digest(data), source=source, counts=result["counts"],
                issues=dict(sorted(Counter(issue for f in result["fixtures"] for issue in f["issues"]).items())),
                static_eligible=sum(f["static_eligible"] for f in result["fixtures"]),
                emitting_area=sum(f["emitting_area"] or 0 for f in result["fixtures"]),
                repeated_export_byte_equal=True, output_sha256=first,
                representative_ids=[f["id"] for f in result["fixtures"][:8]])
            print(name, json.dumps(result["counts"], sort_keys=True), "seconds=%.3f" % (time.monotonic()-started))
        (args.output/"inventory.json").write_text(json.dumps(summary, indent=2, sort_keys=True)+"\n", encoding="utf-8")
    finally:
        assets.close()


if __name__ == "__main__":
    main()
