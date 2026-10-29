#!/usr/bin/env python3
"""Repeat the production S08 CPU executable; no Python light fitting."""
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

SCENES = {"true_point", "narrow_spot", "rectangle", "long_tube", "two_sided_sheet",
          "two_adjacent_panels", "partial_occlusion", "broad_spot", "panel_pose",
          "tube_axis", "historical_spot", "authored_SI_panel", "near_field_tube"}
MODELS = {"point_vs_overcomplex", "panel", "tube", "spot", "missing_shape_evidence"}


def validate(witness, models):
    if witness.get("schema") != "org.openjk.s08-tests" or witness.get("version") != 1:
        raise ValueError("Unknown S08 witness")
    if witness.get("failures") != 0 or witness.get("checks", 0) < 2000:
        raise ValueError("Failed or partial test suite")
    cases = witness["cases"]
    if {c["name"] for c in cases} != SCENES or len(cases) != len(SCENES):
        raise ValueError("Missing or duplicate source fixture")
    for c in cases:
        if c["black_mse"] <= 0 or not c["heldout_mse"] < c["black_mse"] * .05:
            raise ValueError("Positive-light held-out gate failed")
    if {c["name"] for c in models["cases"]} != MODELS:
        raise ValueError("Missing comparison/adversarial case")
    for c in models["cases"]:
        if {m["kind"] for m in c["models"]} != {1, 2, 3, 4} or not c["retained_kinds"]:
            raise ValueError("Point/spot/rectangle/tube comparison incomplete")
        if c["name"] == "missing_shape_evidence" and len(c["retained_kinds"]) < 2:
            raise ValueError("Unidentifiable shape forced into one type")
    json.dumps([witness, models], allow_nan=False)


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument("--tool", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    tool = a.tool.resolve()
    files = ["shared/rd-rend2/lightrecon/" + s for s in
             ("emitter_shape.h", "emitter_shape.cpp", "bake_forward.h", "bake_forward.cpp",
              "bake_profile.h", "bake_math.h", "point_refine.h", "point_refine.cpp",
              "amplitude_fit.h", "visibility.h", "visibility.cpp", "geometry_key.h")]
    files += ["shared/rd-rend2/tr_lightrecon_types.h", "shared/rd-rend2/tr_lightrecon_types.cpp"]
    files += ["tools/lightrecon_test/lightrecon_shape_test.cpp"]
    source_hash = {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}
    exe_hash = hashlib.sha256(tool.read_bytes()).hexdigest()
    runs, resources = [], []
    with tempfile.TemporaryDirectory(prefix="s08-") as tmp:
        for repeat in range(2):
            target = Path(tmp) / f"{repeat}.json"
            start = time.perf_counter()
            with subprocess.Popen([str(tool), str(target)], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as child:
                try:
                    stdout, stderr = child.communicate(timeout=600)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.communicate()
                    raise RuntimeError("S08 process time limit")
                rss = peak(child)
                if child.returncode:
                    raise RuntimeError(stdout + stderr)
            witness = json.loads(target.read_text())
            models = json.loads(Path(str(target) + ".models.json").read_text())
            validate(witness, models)
            runs.append(dict(witness=witness, models=models))
            resources.append(dict(seconds=time.perf_counter() - start, peak_process_rss_bytes=rss,
                                  stages=json.loads(Path(str(target) + ".resources.json").read_text())))
            print(f"run {repeat+1}: {witness['checks']} checks; {resources[-1]['seconds']:.2f}s; RSS {rss}", flush=True)
    if runs[0] != runs[1]:
        raise ValueError("Repeated S08 results differ")
    if source_hash != {f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files} or exe_hash != hashlib.sha256(tool.read_bytes()).hexdigest():
        raise ValueError("Sources or executable changed during experiment")
    report = dict(schema="org.openjk.s08-benchmark", version=1, deterministic=True,
                  source_sha256=source_hash, executable_sha256=exe_hash, **runs[0])
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out/"numerical.json").write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False)+"\n")
    (a.out/"resources.json").write_text(json.dumps(dict(version=1, runs=resources), indent=2, sort_keys=True)+"\n")
    rows = ["scene,initial_xyz_error_world,final_xyz_error_world,angular_error_radians,heldout_mse,black_mse,trials,rays,stop"]
    for c in report["witness"]["cases"]:
        rows.append(",".join(str(c[k]) for k in ("name", "before_world", "after_world", "angular_error_radians",
                                                "heldout_mse", "black_mse", "trials", "rays", "stop")))
    (a.out/"summary.csv").write_text("\n".join(rows)+"\n")


if __name__ == "__main__":
    main()
