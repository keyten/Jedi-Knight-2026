#!/usr/bin/env python3
"""Validate the non-destructive waterfall mist overlay against installed assets."""
import json
import pathlib
import sys
import zipfile

from build_water_body_overlay import bsp_entities
from water_audit import Pk3Files


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: test_waterfall_mist_overlay.py <game-base> <overlay.pk3>")
    base, overlay = argv
    files = Pk3Files(base)
    root = pathlib.Path(__file__).parent
    water = json.loads((root / 'water_body_overrides.json').read_text(encoding='utf-8'))
    mist = json.loads((root / 'waterfall_emitter_overrides.json').read_text(encoding='utf-8'))
    with zipfile.ZipFile(overlay) as archive:
        names = set(archive.namelist())
        for map_name, additions in water.items():
            path = f'cubemaps/{map_name}/env.json'
            merged = json.loads(archive.read(path))
            original = json.loads(files.read(path).decode('utf-8-sig')) if path in files.files else {}
            for key, value in original.items():
                assert merged[key] == value, f'{path}: existing {key} changed'
            for key, value in additions.items():
                assert merged[key] == value, f'{path}: missing authored {key}'
        for map_name, block in mist.items():
            path = f'cubemaps/{map_name}/waterfalls.json'
            assert path in names
            assert json.loads(archive.read(path))["WaterfallEmitters"] == block["WaterfallEmitters"]

        path = 'maps/t3_hevil.ent'
        original = (files.read(path).rstrip(b'\0').decode('cp1252') if path in files.files else
                    bsp_entities(files.read('maps/t3_hevil.bsp'))).rstrip()
        authored = archive.read(path).rstrip(b'\0').decode('cp1252')
        assert authored.startswith(original), 'stock t3_hevil entities were not preserved'
        expected = len(mist['t3_hevil']['FxEmitters'])
        assert authored.count('"spawnflags" "8388608"') == expected
        assert authored.count('"fxFile" "env/waterfall_mist"') >= expected
    print(f'{overlay}: waterfall overlay preserved source data; {expected} opt-in FX runners')


if __name__ == '__main__':
    main(sys.argv[1:])
