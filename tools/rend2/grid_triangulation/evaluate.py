#!/usr/bin/env python3
"""Blind ASR13 position/false-candidate accounting, separate from search.

Analytic fixture authoring and compiled answers live only in this evaluator.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from fixtures import Geometry, observations
from triangulate import Bridge, POLICY, discover, evaluate, norm, prepare, run, sha, sub, write

HERE = Path(__file__).resolve().parent


def metrics(search, truth, tolerance):
    candidates = search["candidates"]
    conditional = [c for c in candidates if c["status"] == "conditional_location"]
    matches = [min((norm(sub(c["position"], t)) for c in candidates), default=None) for t in truth]
    conditional_matches = [min((norm(sub(c["position"], t)) for c in conditional), default=None) for t in truth]
    false = [c["rank"] for c in candidates if not any(norm(sub(c["position"], t)) <= tolerance for t in truth)]
    conditional_false = [c["rank"] for c in conditional if c["rank"] in false]
    return dict(truth_sources=len(truth), recalled=sum(d is not None and d <= tolerance for d in matches),
                conditional_recalled=sum(d is not None and d <= tolerance for d in conditional_matches),
                nearest_conditional_errors_world=conditional_matches,
                nearest_errors_world=matches, conditional_candidates=len(conditional), false_candidates=len(false),
                false_candidate_ranks=false, unknown_candidates=sum(c["status"] == "unknown" for c in candidates),
                false_conditional_candidates=len(conditional_false), spatial_alternatives=len(candidates),
                unknown_clusters=len(search["unknowns"]),
                high_confidence_candidates=sum(c["confidence"] not in ("conditional_only", "unknown") for c in candidates))


def analytic(output):
    truth_path = HERE/"frozen_truth.json"
    truth = json.loads(truth_path.read_text())
    results = []
    for case in truth["cases"]:
        # Only the generator gets light positions. Search receives original-byte-like observations and geometry.
        raw = observations(case)
        geometry = Geometry(case.get("solid_above", 300))
        prepared, rejected = prepare(raw, dict(map_sha256="analytic-v1"), "writer255_merge4")
        training = [r for r in prepared if r["role"] == "train"]
        search = discover(training, geometry, "writer255_merge4")
        repeat = discover(training, geometry, "writer255_merge4")
        assert search == repeat
        result = dict(name=case["name"], kind=case["kind"], metrics=metrics(search, case["lights"], truth["position_tolerance_world"]),
                      input_sha256=hashlib.sha256(json.dumps(raw, sort_keys=True).encode()).hexdigest(),
                      search=search, evaluation=evaluate(search, prepared, geometry), repeat_identical=True, rejected=rejected)
        results.append(result)
        print(case["name"], result["metrics"], flush=True)
    report = dict(schema="org.openjk.asr13-blind-evaluation.v1", policy=POLICY, truth_sha256=sha(truth_path),
                  truth_position_tolerance_world=truth["position_tolerance_world"], cases=results)
    write(output, report)
    return report


def compiled(tool, captures, suite, output):
    results = []
    for name in ("point_room", "spot", "two_colour", "overlap_whites", "bounce_colour", "sun_sky"):
        capture = captures/(name+".jkobs")
        bridge = Bridge(tool, capture)
        try:
            m = bridge.metadata
            lo = [m["origin"][k]-.01 for k in range(3)]
            hi = [m["origin"][k]+m["spacing"][k]*(m["shape"][k]-1)+.01 for k in range(3)]
            report, _ = run(bridge, lo+hi, "writer255_merge4")
        finally:
            bridge.close()
        bridge = Bridge(tool, capture)
        try:
            repeat, _ = run(bridge, lo+hi, "writer255_merge4")
            assert report == repeat
        finally:
            bridge.close()
        # Read frozen answers only after search and repeat have completed.
        answer = suite/"answers"/(name+".q3map-recipe.json")
        truth = json.loads(answer.read_text())
        assert truth["compiled_bsp_sha256"] == m["map_sha256"]
        bsp = (suite/"authoring/base/maps"/(name+".bsp")).read_bytes()
        assert sha(suite/"authoring/base/maps"/(name+".bsp")) == m["map_sha256"]
        assert bsp[:8] == b"RBSP\x01\0\0\0"
        for lump, key in ((15, "records_sha256"), (17, "indices_sha256")):
            offset, size = struct.unpack_from("<II", bsp, 8+8*lump)
            assert hashlib.sha256(bsp[offset:offset+size]).hexdigest() == m[key]
        result = dict(name=name, input_sha256=sha(capture), truth_sha256=sha(answer), repeat_identical=True,
                      metrics=metrics(report["search"], [l["position_world"] for l in truth["ground_truth"]["lights"]], 24.),
                      report=report)
        results.append(result)
        print(name, result["metrics"], flush=True)
    result = dict(schema="org.openjk.asr13-compiled-evaluation.v1", tool_sha256=sha(tool), tolerance_world=24., cases=results,
                  scope="Previously verified actual q3map2 bakes, fresh original-grid search; no fresh compiler invocation")
    write(output, result)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--compiled", action="store_true")
    p.add_argument("--tool", type=Path)
    p.add_argument("--captures", type=Path)
    p.add_argument("--suite", type=Path)
    a = p.parse_args()
    if a.compiled:
        if not all((a.tool, a.captures, a.suite)): p.error("compiled evaluation needs --tool --captures --suite")
        compiled(a.tool, a.captures, a.suite, a.output)
    else:
        analytic(a.output)


if __name__ == "__main__":
    main()
