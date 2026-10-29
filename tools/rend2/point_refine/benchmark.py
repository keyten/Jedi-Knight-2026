#!/usr/bin/env python3
"""Repeat S07 production CPU tests, preserving numerical and resource witnesses separately."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.amplitude_fit.benchmark import peak

REQUIRED = {
    "open_multinormal", "wall_adjacent", "recessed_fixture", "partial_occlusion",
    "wrong_start_bounded_expansion", "single_broad_plane_multiple_starts",
    "symmetric_two_receivers", "parallel_direction_depth_ambiguity",
    "two_nearby_coloured_sources", "sparse_grid_plus_surface", "prior_driven_ambiguity",
}


def validate(witness):
    if witness.get("schema") != "org.openjk.point-refinement-tests" or witness.get("version") != 1:
        raise ValueError("Incompatible numerical witness")
    if witness.get("failures") != 0 or witness.get("checks", 0) < 1000:
        raise ValueError("Failed or partial CPU test suite")
    cases = {c["name"]: c for c in witness["cases"]}
    if set(cases) != REQUIRED or len(cases) != len(witness["cases"]):
        raise ValueError("Missing/duplicate S07 fixture")
    for name, c in cases.items():
        if "ambiguity" in name or name == "symmetric_two_receivers":
            if c["locally_identifiable"]:
                raise ValueError("Underconstrained fixture falsely identified")
        elif not c["after_world"] < c["before_world"] * .5:
            raise ValueError("Non-vacuous localization improvement failed")
        for s in c["sources"]:
            if any(q < 0 for q in s["q"]):
                raise ValueError("Negative source amplitude")
    # JSON itself rejects NaN/Inf, including in diagnostic arrays.
    json.dumps(witness, allow_nan=False)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    tool = args.tool.resolve()
    files = ["shared/rd-rend2/lightrecon/" + n for n in (
        "point_refine.h", "point_refine.cpp", "amplitude_fit.h", "amplitude_fit.cpp",
        "bake_forward.h", "bake_forward.cpp", "visibility.cpp", "regions.cpp")]
    files += ["tools/lightrecon_test/lightrecon_point_refine_test.cpp"]
    source_hashes = {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}
    tool_hash = hashlib.sha256(tool.read_bytes()).hexdigest()
    witnesses, resources, traces = [], [], []
    with tempfile.TemporaryDirectory(prefix="s07-") as temp:
        for repeat in range(2):
            target = Path(temp) / f"{repeat}.json"
            start = time.perf_counter()
            with subprocess.Popen([str(tool), str(target)], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as child:
                try:
                    stdout, stderr = child.communicate(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.communicate()
                    raise RuntimeError("S07 test process exceeded its wall-time budget")
                rss = peak(child)
                if child.returncode:
                    raise RuntimeError(stdout + stderr)
            witness = json.loads(target.read_text(encoding="utf-8"))
            validate(witness)
            witnesses.append(witness)
            resources.append({"wall_seconds": time.perf_counter() - start,
                              "peak_process_rss_bytes": rss,
                              "child": json.loads(Path(str(target) + ".resources.json").read_text())})
            traces.append(Path(str(target) + ".trace.csv").read_bytes())
            print(f"repeat {repeat + 1}: {witness['checks']} checks, {resources[-1]['wall_seconds']:.2f}s, RSS {rss}", flush=True)
    if witnesses[0] != witnesses[1] or traces[0] != traces[1]:
        raise ValueError("Identical CPU runs differ numerically or in the optimization trace")
    if source_hashes != {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files} or tool_hash != hashlib.sha256(tool.read_bytes()).hexdigest():
        raise ValueError("Source files or executable changed during the repeated experiment")
    report = {"schema": "org.openjk.s07-benchmark", "version": 1,
              "protocol": "FixedCountPointXYZAndRelativeRGBDiagnostic",
              "deterministic": True, "witness": witnesses[0],
              "source_sha256": source_hashes,
              "tool_sha256": tool_hash,
              "trace_sha256": hashlib.sha256(traces[0]).hexdigest()}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "numerical.json").write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n")
    (args.out / "resources.json").write_text(json.dumps({"version": 1, "runs": resources}, indent=2, sort_keys=True) + "\n")
    (args.out / "open-room-trace.csv").write_bytes(traces[0])
    rows = ["scene,before_world,after_world,after_metres,interval_objective,fits,evaluations,rays,rank,converged"]
    for c in witnesses[0]["cases"]:
        rows.append(",".join(map(str, (c["name"], c["before_world"], c["after_world"], c["after_metres"],
                    c["objective_final"], c["fits"], c["amplitude_evaluations"], c["rays"], c["rank"], c["converged"]))))
    (args.out / "summary.csv").write_text("\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
