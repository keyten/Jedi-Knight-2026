#!/usr/bin/env python3
"""Evaluator-only checks against pinned own-content BSPs and private source answers.

The diagnostic does not receive these answers. No stock lamp identity is asserted.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def angle(a, b):
    norm = lambda v: math.sqrt(sum(x*x for x in v))
    return math.acos(max(-1., min(1., sum(x*y for x, y in zip(a, b))/(norm(a)*norm(b)))))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--suite", type=Path, required=True)
    p.add_argument("--diagnostics", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    results = []
    for report_path in sorted(a.diagnostics.glob("*-0.json")):
        scene = report_path.name.split("-", 1)[1][:-7]
        report = json.loads(report_path.read_text())
        recipe = json.loads((a.suite / f"answers/{scene}.q3map-recipe.json").read_text())
        bsp = (a.suite / f"authoring/base/maps/{scene}.bsp").read_bytes()
        assert bsp[:8] == b"RBSP\x01\0\0\0"
        sha = hashlib.sha256(bsp).hexdigest()
        assert sha == report["bsp_sha256"] == recipe["compiled_bsp_sha256"]
        assert report["compiler_known"] and report["synthetic"]
        assert report["source_transfer"] == 1  # documented Linear, never stock assumption
        assert report["colour_space"] == "explicit_profile_source_relative"
        lumps = [struct.unpack_from("<II", bsp, 8+8*i) for i in range(18)]
        lump = lambda i: bsp[lumps[i][0]:sum(lumps[i])]
        assert hashlib.sha256(lump(15)).hexdigest() == report["records_sha256"]
        assert hashlib.sha256(lump(17)).hexdigest() == report["indices_sha256"]
        world = struct.unpack_from("<6f", lump(7))
        spacing = recipe["ground_truth"]["grid_spacing_world"]
        origin = [s*math.ceil(v/s) for s, v in zip(spacing, world[:3])]
        shape = [int((s*math.floor(v/s)-o)/s+1) for s, v, o in zip(spacing, world[3:], origin)]
        assert report["origin"] == origin and report["spacing"] == spacing and report["bounds"] == shape
        rows = [json.loads(line) for line in report_path.with_suffix(".jsonl").read_text().splitlines()]
        assert len(rows) == report["cells"] == len(lump(17))//2
        max_angle, directions, nominal_gradients = 0., 0, 0
        for ordinal, row in enumerate(rows):
            idx = struct.unpack_from("<H", lump(17), 2*ordinal)[0]
            raw = lump(15)[30*idx:30*(idx+1)]
            xyz = [ordinal % shape[0], (ordinal//shape[0]) % shape[1], ordinal//(shape[0]*shape[1])]
            assert row["ordinal"] == ordinal and row["record"] == idx and row["xyz"] == xyz
            assert row["world"] == [o+x*s for o, x, s in zip(origin, xyz, spacing)]
            assert row["lat_long"] == list(raw[28:30])
            for slot, style in enumerate(row["styles"]):
                assert style["id"] == raw[24+slot]
                assert style["raw_ambient"] == list(raw[3*slot:3*slot+3])
                assert style["raw_directed"] == list(raw[12+3*slot:15+3*slot])
                if style["active"]:
                    for field, raw_field in (("ambient", "raw_ambient"), ("directed", "raw_directed")):
                        for value, code in zip(style[field]["rgb"], style[raw_field]):
                            assert abs(value["value"]-code/255) < 1e-6
                for component in ("ambient", "directed"):
                    for axis, g in enumerate(style[component+"_gradient_xyz"]):
                        if g["mean_rgb"] is None:
                            continue
                        assert row["space"] == "free" and not row["boundary"]
                        assert row["edges"][2*axis:2*axis+2] == ["clear", "clear"]
                        stride = (1, shape[0], shape[0]*shape[1])[axis]
                        left, right = rows[ordinal-stride], rows[ordinal+stride]
                        by_id = lambda r: next(s for s in r["styles"] if s["active"] and s["id"] == style["id"])
                        l, r = by_id(left), by_id(right)
                        for channel, value in enumerate(g["rgb"]):
                            expected = (r["raw_"+component][channel]-l["raw_"+component][channel])/(255*2*spacing[axis])
                            assert abs(value["value"]-expected) < 1e-8
                            if value["code_bounds"]:
                                assert value["code_bounds"][0]-1e-8 <= expected <= value["code_bounds"][1]+1e-8
                        nominal_gradients += 1
            if scene == "point_room" and row["space"] == "free" and not row["boundary"]:
                light = recipe["ground_truth"]["lights"][0]["position_world"]
                expected = [l-v for l, v in zip(light, row["world"])]
                error = angle(expected, row["incoming_writer255_hypothesis"])
                assert error <= report["writer255_encoding_bound_radians"]+report["merge_angular_bound_radians"]
                max_angle = max(max_angle, error)
                directions += 1
        results.append(dict(scene=scene, bsp_sha256=sha, exact_original_cells=len(rows),
                            known_single_light_directions=directions, maximum_writer255_error_radians=max_angle if directions else None,
                            independently_checked_gradient_axes=nominal_gradients))
    assert len(results) >= 6 and sum(r["known_single_light_directions"] for r in results) > 0
    a.output.write_text(json.dumps(dict(schema="org.openjk.original-grid-compiled-checks.v1",
                                       results=results, passed=True), indent=2)+"\n")


if __name__ == "__main__":
    main()
