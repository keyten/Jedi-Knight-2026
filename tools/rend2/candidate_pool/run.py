#!/usr/bin/env python3
"""S05 deterministic CPU witness + separately measured Windows process resources.
SPDX-License-Identifier: GPL-2.0-or-later. Python standard library only.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]


def provenance(tool):
    files = ("shared/rd-rend2/lightrecon/candidate_pool.h",
             "shared/rd-rend2/lightrecon/candidate_pool.cpp",
             "tools/lightrecon_test/lightrecon_candidates_test.cpp",
             "tools/rend2/candidate_pool/run.py")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                            capture_output=True, text=True).stdout.strip()
    return dict(head=commit, tool_sha256=hashlib.sha256(tool.read_bytes()).hexdigest(),
                source_sha256={f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
                dirty_sources=True)


def peak_rss(process):
    if sys.platform != "win32":
        return None
    class Memory(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("faults", ctypes.c_ulong)] + [
            (n, ctypes.c_size_t) for n in (
                "peak_ws", "ws", "peak_paged", "paged", "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile")]
    m = Memory()
    m.cb = ctypes.sizeof(m)
    query = ctypes.WinDLL("psapi").GetProcessMemoryInfo
    query.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
    return m.peak_ws if query(int(process._handle), ctypes.byref(m), m.cb) else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    tool = args.tool.resolve()
    witnesses, resources, csv_outputs = [], [], []
    with tempfile.TemporaryDirectory(prefix="candidate-pool-") as folder:
        for repeat in range(2):
            witness = Path(folder) / f"witness-{repeat}.json"
            start = time.perf_counter()
            with subprocess.Popen([str(tool), str(witness)], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as process:
                stdout, stderr = process.communicate(timeout=120)
                peak = peak_rss(process)
                if process.returncode:
                    raise RuntimeError(stdout + stderr)
            value = json.loads(witness.read_text(encoding="utf-8"))
            if value["failures"] or value["inputs"] != value["models"]:
                raise RuntimeError("Invalid candidate-retention witness")
            witnesses.append(value)
            csv_outputs.append(Path(str(witness)+".csv").read_bytes())
            stages=json.loads(Path(str(witness)+".resources.json").read_text())
            resources.append(dict(seconds=time.perf_counter() - start, peak_rss_bytes=peak, stages=stages))
    if witnesses[0] != witnesses[1]:
        raise RuntimeError("Semantic output differs across identical executions")
    if csv_outputs[0] != csv_outputs[1]:
        raise RuntimeError("Per-home/provider/class diagnostics differ across executions")
    canonical = json.dumps(witnesses[0], sort_keys=True, separators=(",", ":")).encode()
    result = dict(schema="org.openjk.candidate-pool-benchmark", version=1,
                  deterministic=True, semantic_sha256=hashlib.sha256(canonical).hexdigest(),
                  diagnostics_csv_sha256=hashlib.sha256(csv_outputs[0]).hexdigest(),
                  witness=witnesses[0], resources=resources,
                  provenance=provenance(tool),
                  scope="Own-content scheduling/retention only; not physical reconstruction recall")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.out.with_suffix(".csv").write_bytes(csv_outputs[0])
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
