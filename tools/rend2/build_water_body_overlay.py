"""Merge project water dynamics rules into installed env.json files in a new PK3.

The original PK3s are read only. Usage:
  python tools/rend2/build_water_body_overlay.py <game base> <output pk3>
"""
import json
import pathlib
import struct
import sys
import zipfile

from water_audit import Pk3Files


def bsp_entities(data):
    offset, length = struct.unpack_from('<ii', data, 8)
    return data[offset:offset + length].rstrip(b'\0').decode('cp1252')


def waterfall_fx_entity(origin):
    xyz = ' '.join(str(value) for value in origin)
    return ('{\n'
            '"classname" "fx_runner"\n'
            f'"origin" "{xyz}"\n'
            '"fxFile" "env/waterfall_mist"\n'
            '"delay" "260"\n'
            '"random" "120"\n'
            '"spawnflags" "8388608"\n'
            '}\n')


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    base, destination = argv
    files = Pk3Files(base)
    manifest = json.loads((pathlib.Path(__file__).with_name('water_body_overrides.json')).read_text(encoding='utf-8'))
    waterfall_manifest = json.loads((pathlib.Path(__file__).with_name('waterfall_emitter_overrides.json')).read_text(encoding='utf-8'))
    destination = pathlib.Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as output:
        for map_name, block in sorted(manifest.items()):
            path = f'cubemaps/{map_name}/env.json'
            environment = json.loads(files.read(path).decode('utf-8-sig')) if path in files.files else {}
            for key, value in block.items():
                if key in environment:
                    raise ValueError(f'{path} already defines {key}; merge manually')
                environment[key] = value
            output.writestr(path, json.dumps(environment, indent=2) + '\n')
        for map_name, block in sorted(waterfall_manifest.items()):
            sidecar = {"WaterfallEmitters": block["WaterfallEmitters"]}
            output.writestr(f'cubemaps/{map_name}/waterfalls.json', json.dumps(sidecar, indent=2) + '\n')
            emitters = block.get("FxEmitters", [])
            if not emitters:
                continue
            ent_path = f'maps/{map_name}.ent'
            if ent_path in files.files:
                entities = files.read(ent_path).rstrip(b'\0').decode('cp1252')
            else:
                entities = bsp_entities(files.read(f'maps/{map_name}.bsp'))
            if '"spawnflags" "8388608"' in entities:
                raise ValueError(f'{ent_path} already has rend2 waterfall emitters')
            entities = entities.rstrip() + '\n' + ''.join(waterfall_fx_entity(origin) for origin in emitters)
            output.writestr(ent_path, entities.encode('cp1252') + b'\0')
    print(f'{destination}: {len(manifest)} env.json overlays, {len(waterfall_manifest)} waterfall sidecars')


if __name__ == '__main__':
    main(sys.argv[1:])
