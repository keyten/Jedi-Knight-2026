#!/usr/bin/env python3
"""ASR12 original-lightgrid diagnostics and byte-for-byte repeat checks.

Consumes existing .jkobs captures; creates no probes and changes no source assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tool", type=Path, required=True)
    p.add_argument("--inputs", type=Path, nargs="+", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--q3map2-merge4", action="store_true")
    p.add_argument("--source-relative", action="store_true")
    p.add_argument("--cells", action="store_true", help="stream all cells to JSONL (can be large)")
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    results = []
    for ordinal, source in enumerate(a.inputs):
        stem = f"{ordinal:03d}-{source.stem}"
        reports, cell_files = [], []
        for repeat in range(2):
            report = a.output / f"{stem}-{repeat}.json"
            command = [str(a.tool.resolve()), str(source.resolve()), str(report.resolve())]
            if a.q3map2_merge4:
                command.append("--q3map2-merge4")
            if a.source_relative:
                command.append("--source-relative")
            if a.cells:
                cell_file = a.output / f"{stem}-{repeat}.jsonl"
                command += ["--cells", str(cell_file.resolve())]
                cell_files.append(cell_file)
            subprocess.run(command, check=True)
            reports.append(report)
        if digest(reports[0]) != digest(reports[1]):
            raise AssertionError("Report changed on repeat: " + str(source))
        if a.cells and digest(cell_files[0]) != digest(cell_files[1]):
            raise AssertionError("Cell extraction changed on repeat: " + str(source))
        report = json.loads(reports[0].read_text())
        assert report["generated_probes"] == 0
        assert not report["absolute_calibration_known"]
        assert not report["aggregate_is_individual_emitter"]
        assert sum(report["space_counts"].values()) == report["cells"]
        assert sum(report["presence_counts"].values()) == report["cells"]
        assert sum(report["edge_counts"].values()) == 6 * report["cells"]
        results.append(dict(input_name=source.name, input_sha256=digest(source),
                            repeat_identical=True, report=report,
                            cells_sha256=digest(cell_files[0]) if a.cells else None))
    (a.output / "validation.json").write_text(json.dumps(dict(
        schema="org.openjk.original-grid-evidence-validation.v1",
        tool_sha256=digest(a.tool), results=results), indent=2) + "\n")


if __name__ == "__main__":
    main()
