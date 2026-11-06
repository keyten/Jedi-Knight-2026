#!/usr/bin/env python3
"""X18 frozen native appearance-field benchmark; no Python lighting predictions."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write

MAPS = ('hoth3', 'hoth2', 'vjun2', 'mp/duel4', 't1_danger')


def semantic(value):
    if isinstance(value, dict):
        return {k: semantic(v) for k, v in value.items()
                if k not in ('seconds', 'extraction_seconds', 'query_p50_ns', 'query_p95_ns',
                             'cache_store_seconds', 'cache_load_seconds')}
    if isinstance(value, list):
        return [semantic(v) for v in value]
    return value


def main(a):
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=False)
    inventory = json.loads((a.private / 'inventory.json').read_text())
    captures = json.loads((a.private / 'captures.json').read_text())
    sources = sorted(set(list((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*')) +
                         list((ROOT / 'shared/rd-rend2').glob('tr_lightrecon*')) +
                         [ROOT / 'tools/lightrecon_test/lightrecon_appearance_tool.cpp',
                          ROOT / 'tools/lightrecon_test/lightrecon_field_builder_test.cpp',
                          ROOT / 'tools/lightrecon_test/CMakeLists.txt', Path(__file__)]))
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources if p.is_file()}
    inputs = []
    for name in a.maps:
        bsp = next(x for x in inventory['maps'] if x['map'] == name)
        data = a.private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if not data.exists():
            data = a.private / 'runs-v4' / name.replace('/', '_') / 'all-pages.jkobs'
        if not data.exists():
            data = Path(captures[name]['dataset'])
        inputs.append(dict(map=name, bsp_sha256=bsp['sha256'], dataset_sha256=sha(data),
                           dataset_bytes=data.stat().st_size, private_path=str(data)))
    protocol = dict(schema='org.openjk.appearance-protocol/1',
                    revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                    source_hashes=hashes, native_sha256=sha(a.tool), maps=list(a.maps),
                    quantity='L0 original static style-0 RGB8 surface-code appearance proxy; unknown transfer/primaries',
                    physical_emitter_discovery=False, lux_nits=False, incoming_radiance=False,
                    root_path_membership='Unknown; addition/replacement forbidden',
                    roi_rule='3 x 512 WU cubes at static planar surface vertex ordinal quartiles, geometry only',
                    folds='128 WU geometry-hashed blocks, 3/5 train, 1/5 validation, 1/5 final; 8 WU + exact clipped footprint maximum radius guard (original 128x128 pages only)',
                    row_selection='lowest geometry-correlation hashes; 32 WU cells x 6 normal bins, one lowest stable sample ID; max 256/fold/ROI',
                    query_domain='16 WU along oriented geometric normal from held-out original surface; target remains original immutable RGB',
                    max_probe_spacing=64, min_probe_spacing=16, max_probes=10000,
                    appearance_radius=192, neighbours=32, validation_min_rows=6,
                    validation_min_fraction=.25, validation_max_relative_rmse=.6,
                    useful_gate='admitted field, >=6 heldout predictions, >=25% heldout rows, >=5% common-domain RMSE improvement over both grid and constant; no blocked supported rays',
                    process_seconds=600, process_bytes=1024**3,
                    missing_is_black=False, final_rgb_tunes_settings=False,
                    grid_baseline='Original tr_light.cpp style-0 LDR entity ambient + directed*max(dot(n, blended direction),0), unit renderer gains, no minimum ambient or display transform; independent modality, not physical truth',
                    material_context='original frozen SP capture, including MP BSP; MP interpretation unverified',
                    sources=[{k: v for k, v in x.items() if k != 'private_path'} for x in inputs],
                    environment=dict(os=platform.platform(), cpu=platform.processor(), logical_cpus=os.cpu_count(),
                                     compiler='GCC 16.2 C++11 Release', python=platform.python_version()))
    write(a.out / 'protocol.json', protocol)
    results = {}
    for item in inputs:
        label = item['map'].replace('/', '_')
        report = a.out / (label + '.json')
        resource = execute([a.tool, item['private_path'], report], a.out, label,
                           protocol['process_seconds'], protocol['process_bytes'])
        entry = dict(input={k: v for k, v in item.items() if k != 'private_path'}, resources=resource)
        if resource['state'] == 'Completed' and resource['exit_code'] == 0:
            entry['result'] = json.loads(report.read_text())
            assert entry['result']['bsp_sha256'] == item['bsp_sha256']
            for r in entry['result']['rois']:
                if r['field_code'] == 0:
                    assert r['roundtrip'] and r['corrupt_rejected'] and r['physical_capabilities_unavailable'] and r['cache_roundtrip']
                    assert r['support_check_blocked'] == 0
        results[item['map']] = entry
        write(a.out / 'stock.json', results)
        print(item['map'], resource['state'], [(r['field_code'], r['metrics'].get('geometry_L0'))
              for r in entry.get('result', {}).get('rois', [])], flush=True)
    repeat = {}
    if 'mp/duel4' in results and 'result' in results['mp/duel4']:
        item = next(x for x in inputs if x['map'] == 'mp/duel4')
        path = a.out / 'mp_duel4-repeat.json'
        resource = execute([a.tool, item['private_path'], path], a.out, 'mp_duel4-repeat', 600, 1024**3)
        assert resource['exit_code'] == 0 and resource['state'] == 'Completed'
        repeat = dict(resources=resource, semantic_equal=semantic(json.loads(path.read_text())) ==
                      semantic(results['mp/duel4']['result']))
        assert repeat['semantic_equal']
    write(a.out / 'repeat.json', repeat)
    for path, checksum in hashes.items():
        assert sha(ROOT / path) == checksum, path
    write(a.out / 'manifest.json', dict(schema='org.openjk.appearance-results/1',
                                       artifacts={p.name: sha(p) for p in a.out.iterdir() if p.is_file()},
                                       licensed_geometry=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_appearance_tool.exe')
    p.add_argument('--maps', nargs='+', choices=MAPS, default=MAPS)
    main(p.parse_args())
