#!/usr/bin/env python3
"""Independently check stock entity extraction with Q3Map2 -exportents.

Only writes to the selected output directory; licensed archives are read-only.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import zipfile

from tools.rend2.stock_lights.extract import entities, sha, write_json


def normalized(raw):
    # Windows Q3Map2 writes CRLF in text mode and appends a final newline.
    return raw.replace(b"\r\n", b"\n").rstrip(b"\0\r\n")


def verify(base, output, q3map2):
    summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
    selected = {m["map"]: m for m in summary["maps"]}
    directory = (output / "q3map2-check").resolve()
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for variant in summary["variants"]:
        with zipfile.ZipFile(base / variant["archive"]) as archive:
            data = archive.read(variant["member"])
        if sha(data) != variant["sha256"]:
            raise ValueError("Source BSP changed since extraction")
        # Independent lump lookup, then native export of an untouched BSP copy.
        offset, size = struct.unpack_from("<ii", data, 8)
        raw = data[offset:offset + size]
        bsp = directory / "input.bsp"
        bsp.write_bytes(data)
        command = [str(q3map2.resolve()), "-game", "ja", "-fs_nobasepath",
                   "-fs_nohomepath", "-exportents", str(bsp)]
        process = subprocess.run(command, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=60)
        stem = variant["archive"] + "-" + variant["member"].replace("/", "_")[:-4]
        (directory / (stem + ".log")).write_bytes(process.stdout)
        if process.returncode:
            raise RuntimeError("Q3Map2 failed for " + stem)
        native = bsp.with_suffix(".ent").read_bytes()
        if normalized(native) != normalized(raw):
            raise AssertionError("Native entity text differs: " + stem)
        es = entities(native)
        native_lights = [e for e in es if e["keys"].get("classname", "").lower().startswith("light")]
        if len(native_lights) != variant["light_entities"] or len(es) != variant["entities"]:
            raise AssertionError("Native entity inventory differs: " + stem)
        is_selected = selected[variant["member"].lower()]["archive"] == variant["archive"]
        if is_selected:
            mapdir = output / variant["member"][:-4].lower()
            if (mapdir / "entities.ent").read_bytes() != raw:
                raise AssertionError("Direct export is not byte-exact")
            fragment = (mapdir / "lights.ent").read_bytes()
            extracted = entities(b'{"classname" "worldspawn"}\n' + fragment)[1:]
            if [e["pairs"] for e in native_lights] != [e["pairs"] for e in extracted]:
                raise AssertionError("Exported light fragment differs")
        records.append(dict(variant, selected=is_selected, native_export_matches=True,
                            native_light_classes=dict(Counter(e["keys"]["classname"] for e in native_lights)),
                            native_ent_sha256=sha(native)))
    result = dict(q3map2=str(q3map2.resolve()), q3map2_sha256=sha(q3map2.read_bytes()),
                  method="Native -exportents text equals original lump except CRLF/trailing NUL/newline; selected raw lumps byte-exact; light fragments equal native parsed pairs",
                  variants_verified=len(records), selected_maps_verified=sum(r["selected"] for r in records),
                  selected_lights_verified=sum(r["light_entities"] for r in records if r["selected"]),
                  records=records)
    write_json(output / "verification.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "records"}, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--q3map2", type=Path, required=True)
    args = parser.parse_args()
    verify(args.base, args.output, args.q3map2)


if __name__ == "__main__":
    main()
