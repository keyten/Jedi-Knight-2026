#!/usr/bin/env python3
"""Reproduce blind fixtures and two frozen small stock ROIs; keep mesh images private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys

from diagnostic import render
from evaluate import analytic, compiled
from triangulate import Bridge, POLICY, run, sha, write

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
# Earlier geometry-only selections, frozen before ASR13 source search.
STOCK = (("hoth2-local", [6144, -5632, 512, 6656, -5120, 1024]),
         ("hoth2-outdoor", [-736, -10432, 176, -224, -9920, 688]))


def compact(case):
    report = case.get("report", case)
    search = report["search"]
    return dict(name=case["name"], metrics=case["metrics"], input_sha256=case["input_sha256"],
                truth_sha256=case.get("truth_sha256"), repeat_identical=case["repeat_identical"],
                unknown_reasons=dict(Counter(u["reason"] for u in search["unknowns"])),
                candidates=[{k: c[k] for k in ("rank", "status", "confidence", "position", "angular_rms_degrees",
                                              "component_angular_rms_degrees", "angular_condition", "independent_records",
                                              "consensus_fraction", "reasons")} for c in search["candidates"]],
                evaluation=report["evaluation"])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tool", required=True, type=Path)
    p.add_argument("--captures", required=True, type=Path)
    p.add_argument("--suite", required=True, type=Path)
    p.add_argument("--stock-capture", type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--summary", required=True, type=Path, help="compact versionable JSON, no raw map geometry")
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(HERE/"test_triangulate.py"), "-v"], check=True)
    controlled = analytic(a.output/"analytic.json")
    baked = compiled(a.tool, a.captures, a.suite, a.output/"compiled.json")
    positives = [c for c in controlled["cases"] if c["kind"] in ("point", "spot")]
    assert sum(c["metrics"]["recalled"] for c in positives) == 6
    assert sum(c["metrics"]["false_candidates"] for c in positives) == 0
    assert all(c["metrics"]["high_confidence_candidates"] == 0 for c in controlled["cases"]+baked["cases"])
    assert all(c["metrics"]["conditional_candidates"] == 0 for c in controlled["cases"]
               if c["kind"] in ("multiple", "coloured", "sun", "sun_local"))
    summary = dict(schema="org.openjk.asr13-validation.v1", date="2026-11-09", policy=POLICY,
                   tests=dict(python_tests=18, positive_analytic_recall="6/6", positive_analytic_false_candidates=0,
                              aggregate_sun_high_confidence=0),
                   native_tool_sha256=sha(a.tool), frozen_truth_sha256=controlled["truth_sha256"],
                   analytic=[compact(c) for c in controlled["cases"]], compiled=[compact(c) for c in baked["cases"]], stock=[])
    if a.stock_capture:
        for name, roi in STOCK:
            reports = []
            for repeat in range(2):
                bridge = Bridge(a.tool, a.stock_capture)
                try:
                    report, rows = run(bridge, roi, "consumer256_unknown")
                    if repeat == 0:
                        mesh = bridge.query("mesh", *roi)
                        render(a.output/(name+".svg"), report, rows, mesh)
                        write(a.output/(name+".json"), report)
                finally:
                    bridge.close()
                reports.append(report)
            assert reports[0] == reports[1]
            summary["stock"].append(dict(name=name, capture_sha256=sha(a.stock_capture), repeat_identical=True,
                                         roi_selection="Earlier geometry-only local-v3 / ASR07 sky-origin selections; frozen before ASR13",
                                         mesh_coverage_complete=mesh["coverage_complete"], mesh_issues=mesh["issues"],
                                         diagnostic_svg_sha256=sha(a.output/(name+".svg")), report=reports[0]))
    files = list(HERE.glob("*.py"))+[HERE/"frozen_truth.json"]
    files += [ROOT/"tools/lightrecon_test"/n for n in ("lightrecon_grid_geometry_tool.cpp", "original_grid_evidence.cpp", "original_grid_evidence.h", "CMakeLists.txt")]
    summary["source_sha256"] = {str(f.relative_to(ROOT)).replace("\\", "/"): sha(f) for f in sorted(files)}
    summary["scope"] = "Offline only; conditional aesthetic source hypotheses; reused verified q3map2 bakes; no fresh compiler/runtime or exact historical-source certification"
    write(a.summary, summary)


if __name__ == "__main__": main()
