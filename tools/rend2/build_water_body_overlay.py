"""Merge project water dynamics rules into installed env.json files in a new PK3.

The original PK3s are read only. Usage:
  python tools/rend2/build_water_body_overlay.py <game base> <output pk3>
"""
import json
import pathlib
import sys
import zipfile

from water_audit import Pk3Files


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    base, destination = argv
    files = Pk3Files(base)
    manifest = json.loads((pathlib.Path(__file__).with_name('water_body_overrides.json')).read_text(encoding='utf-8'))
    destination = pathlib.Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as output:
        for map_name, block in sorted(manifest.items()):
            path = f'cubemaps/{map_name}/env.json'
            environment = json.loads(files.read(path).decode('utf-8-sig')) if path in files.files else {}
            if 'WaterBodies' in environment:
                raise ValueError(f'{path} already defines WaterBodies; merge manually')
            environment['WaterBodies'] = block['WaterBodies']
            output.writestr(path, json.dumps(environment, indent=2) + '\n')
    print(f'{destination}: {len(manifest)} map env.json overlays')


if __name__ == '__main__':
    main(sys.argv[1:])
