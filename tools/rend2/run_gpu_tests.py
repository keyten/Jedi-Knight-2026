#!/usr/bin/env python3
"""Run the Rend2 hidden-context GPU suite and lightweight contract tests."""

import argparse
import fnmatch
import os
from pathlib import Path
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent


def select_tests():
    gpu = sorted(HERE.glob("test_*_gl.py"))
    gpu += [HERE / "test_gl43_compute.py", HERE / "test_volumetric_compute.py"]
    selected = {path.resolve(): path for path in gpu}
    for path in sorted(HERE.glob("test_*.py")):
        if path.resolve() in selected or path.name.endswith("_runtime.py"):
            continue
        head = path.read_text(encoding="utf-8", errors="replace")[:512].lower()
        if "contract check" in head:
            selected[path.resolve()] = path
    return list(selected.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nvidia", action="store_true",
                        help="request the NVIDIA Optimus adapter")
    parser.add_argument("--only", action="append", metavar="GLOB",
                        help="run only matching test filenames (repeat for multiple patterns)")
    args = parser.parse_args()
    env = os.environ.copy()
    if args.nvidia:
        env["SHIM_MCCOMPAT"] = "0x800000001"

    tests = select_tests()
    if args.only:
        tests = [p for p in tests if any(fnmatch.fnmatchcase(p.name, pattern) for pattern in args.only)]
        if not tests:
            parser.error("--only matched no GPU/contract tests")
    results = []
    for index, path in enumerate(tests, 1):
        print(f"[{index}/{len(tests)}] {path.name}", flush=True)
        started = time.monotonic()
        result = subprocess.run([sys.executable, str(path)], cwd=str(HERE.parent.parent), env=env)
        results.append((path.name, result.returncode, time.monotonic() - started))

    print("\nRend2 GPU/contract test summary")
    for name, returncode, elapsed in results:
        print(f"  {'PASS' if returncode == 0 else 'FAIL'}  {name:<38} {elapsed:7.2f}s")
    failed = [name for name, returncode, _ in results if returncode]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    if failed:
        print("Failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
