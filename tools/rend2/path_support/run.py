#!/usr/bin/env python3
"""X17 paired production map runs. No licensed geometry is copied to results.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write

MAPS = ('hoth3', 'hoth2', 'vjun2', 'kor1', 'yavin1b', 't1_danger', 'mp/duel4', 'mp/ffa3')
ISSUES = ('OriginUnclassified', 'OriginInsideSolid', 'OriginOnBoundary', 'EndpointInsideSolid',
          'OpticalUnknown', 'SidednessUnknown', 'PatchApproximation', 'UnsupportedGeometry',
          'IncompleteGeometry', 'SkyBoundaryMissing', 'CoplanarRay', 'BiasObstructed',
          'PrecisionLimit', 'PolicyAssumption', 'CoincidentSurfaces', 'BakeOccupancyUnknown',
          'UnboundedOccupancy', 'ConditionalBakePath')


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def bits(value):
    return [name for bit, name in enumerate(ISSUES) if value & (1 << bit)]


def summarize(folder):
    record = {}
    for filename, key in (('receiver-support.json', 'receivers'), ('transport.json', 'transport')):
        if (folder / filename).exists():
            record[key] = read(folder / filename)
    record['profiles'] = {}
    for profile in ('isq', 'linear'):
        path = folder / ('scheduler-' + profile + '.json')
        if not path.exists():
            continue
        raw = read(path)
        summary = {k: v for k, v in raw.items() if k not in ('units', 'influence', 'branches')}
        summary['jobs'] = len(raw['units'])
        summary['job_statuses'] = dict(Counter(x['status'] for x in raw['units']))
        summary['proposal_jobs'] = sum(x['candidates'] > 0 for x in raw['units'])
        summary['branches'] = raw['branches']
        # Keep every bounded attempt and every missing stable ID/issue. Never
        # replace failed or absent final predictions with zero error.
        failures = Counter()
        for branch in summary['branches']:
            for attempt in branch.get('attempts', []):
                for row in attempt.get('unavailable_receivers', []):
                    row['typed_visibility_causes'] = bits(row['visibility_issues'])
                    failures.update(row['typed_visibility_causes'])
        summary['missing_path_issue_counts'] = dict(failures)
        controller = folder / ('scheduler-' + profile + '-controller.json')
        if controller.exists():
            summary['controller'] = read(controller)
        debug = folder / (profile + '-debug.json')
        if debug.exists():
            data = read(debug)
            records = data.get('records', [])
            summary['diagnostic_classes'] = dict(Counter(str(r.get('layer')) + '/' + str(r.get('state')) for r in records))
            summary['diagnostic_omitted'] = data.get('omitted')
            actual = [r for r in records if r.get('reason', '').startswith('actual Joint factor response')]
            summary['actual_joint_factor_receipts'] = dict(
                recorded=len(actual), available=sum(r['value'] is not None for r in actual),
                positive=sum(r['value'] is not None and max(r['value']) > 0 for r in actual),
                explicit_zero=sum(r['value'] is not None and max(r['value']) == 0 for r in actual),
                unavailable=sum(r['value'] is None for r in actual),
                sources=len({r['source'] for r in actual}), samples=len({r['sample'] for r in actual}),
                scope='Bounded first-amplitude factor receipts; not accepted fitted-source values or independent coverage')
        record['profiles'][profile] = summary
    record['raw_artifact_hashes'] = {p.name: sha(p) for p in folder.iterdir()
                                    if p.is_file() and p.suffix in ('.json', '.tsv', '.jkdbg')}
    return record


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    a.work.mkdir(parents=True, exist_ok=False)
    inventory = read(a.private / 'inventory.json')
    captures = read(a.private / 'captures.json')
    binary = a.work / 'lightrecon_map_solver.exe'
    shutil.copyfile(a.tool, binary)
    policy = dict(schema='org.openjk.source-path-stock-protocol/1', label=a.label,
                  code_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  native_sha256=sha(binary), inventory_sha256=sha(a.private / 'inventory.json'),
                  selected_before_run=list(MAPS), process_seconds=900, process_bytes=1024**3,
                  mode='scene-receiver-preflight', acceptance_gate_changed=False,
                  final_targets_used_for_sampling_or_fitting=False,
                  scope='Two independent conditional NetRadiant profiles, same frozen X10/X15 folds, direct-only missing GI/sun unknown',
                  stock_profile='UnknownProfile', stock_source_answers=None, absolute_photometry=None,
                  binary_source_revision='d3ef7c59a' if a.label.startswith('before') else 'working-tree',
                  source_hashes=None if a.label.startswith('before') else {p.relative_to(ROOT).as_posix(): sha(p)
                                 for p in sorted((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*'))},
                  environment=dict(os=platform.platform(), cpu=platform.processor(), logical_cpus=os.cpu_count(),
                                   python=platform.python_version(), compiler='GCC 16.2 C++11 Release'))
    policy['maps'] = []
    for name in a.maps:
        item = next(m for m in inventory['maps'] if m['map'] == name)
        source = a.private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if not source.exists(): source = a.private / 'runs-v4' / name.replace('/', '_') / 'all-pages.jkobs'
        if not source.exists(): source = Path(captures[name]['dataset'])
        policy['maps'].append(dict(map=name, bsp_sha256=item['sha256'], dataset_sha256=sha(source),
                                   dataset_bytes=source.stat().st_size, source=str(source)))
    write(a.out / 'protocol.json', policy)
    results = {}
    for item in policy['maps']:
        name = item['map']; folder = a.work / name.replace('/', '_'); folder.mkdir()
        if sha(binary) != policy['native_sha256'] or sha(item['source']) != item['dataset_sha256']:
            raise ValueError('Frozen dependency changed')
        resource = execute([binary, item['source'], folder, item['bsp_sha256'], policy['mode']], folder,
                           'native', policy['process_seconds'], policy['process_bytes'])
        result = summarize(folder)
        result.update(input={k: v for k, v in item.items() if k != 'source'}, resources=resource)
        if resource['exit_code'] or resource['state'] != 'Completed':
            result['process_failure'] = (folder / 'native.log').read_text(errors='replace')[-1600:]
        results[name] = result
        write(a.out / 'stock.json', results)
        print(name, resource['state'], {p: (v.get('fitting_forward_calls'), v.get('final_evaluated'))
                                      for p, v in result.get('profiles', {}).items()}, flush=True)
    write(a.out / 'manifest.json', dict(artifacts={p.name: sha(p) for p in a.out.iterdir()
                                                  if p.is_file() and p.name != 'manifest.json'},
                                       raw_geometry_committed=False, missing_prediction_is_black=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--maps', nargs='+', choices=MAPS, default=MAPS)
    main(p.parse_args())
