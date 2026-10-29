#!/usr/bin/env python3
"""Compare existing shader regression snapshots against a Git revision, without resetting a baseline."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--reference", default="HEAD")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "rev-parse", "--verify", args.reference + "^{commit}"],
                                       cwd=ROOT, text=True).strip()
    spec = importlib.util.spec_from_file_location("regression", ROOT / "tools/rend2/lightrecon_regression.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cases = json.loads((module.BASELINE / "matrix.json").read_text())
    cpp = module.cpp_path()
    original, working, frozen = module.read, {}, {}

    def current(path):
        working[path] = original(path)
        return working[path]

    def reference(path):
        if path not in frozen:
            frozen[path] = subprocess.check_output(["git", "show", revision + ":" + path],
                                                   cwd=ROOT).decode("utf-8").replace("\r\n", "\n")
        return frozen[path]

    module.read = current
    a = module.snapshot(cases, cpp)
    bindings_a = module.binding_inventory(a)
    module.read = reference
    b = module.snapshot(cases, cpp)
    bindings_b = module.binding_inventory(b)
    report = dict(version=1, passed=a == b and bindings_a == bindings_b,
                  comparison="worktree versus explicit Git reference; existing regression assembler",
                  programs=len(cases), stages=sum(len(v["stages"]) for v in a.values()),
                  gpu_compiled=False, reference=revision,
                  inputs={p: hashlib.sha256(t.encode()).hexdigest() for p, t in sorted(working.items())})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "inputs"}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
