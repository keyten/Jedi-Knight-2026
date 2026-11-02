#!/usr/bin/env python3
"""Bounded private X11 stock trials; a completed job is not lamp recovery.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import sys
import shutil

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2/all_stock'))
from run import execute, sha, write

MAPS = ('mp/duel4', 't1_inter', 't1_danger', 'hoth3', 'vjun2')


def run(private, tool, revision, maps):
    inventory = json.loads((private / 'inventory.json').read_text())
    captures = json.loads((private / 'captures.json').read_text())
    record = private / ('runs-' + revision + '.json')
    if record.exists():
        raise ValueError('Preserve previous attempts; choose a fresh revision')
    native = sha(tool)
    frozen = private / ('runs-' + revision + '-native.exe')
    if frozen.exists():
        raise ValueError('Frozen executable already exists; choose a fresh revision')
    shutil.copyfile(tool, frozen)
    if sha(frozen) != native:
        raise ValueError('Native copy integrity failure')
    results = {}
    for name in maps:
        item = next(m for m in inventory['maps'] if m['map'] == name)
        data = Path(captures[name]['dataset'])
        derived = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if derived.exists():
            data = derived
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        folder.mkdir(parents=True, exist_ok=False)
        if sha(frozen) != native:
            raise ValueError('Frozen native executable changed during the experiment')
        resources = execute([frozen, data, folder, item['sha256'], 'scene'], folder, 'native',
                            seconds=600, memory=1024**3)
        result = dict(bsp_sha256=item['sha256'], native_sha256=native, resources=resources,
                      physical_source_truth='Unavailable', historical_profile='UnknownProfile')
        for profile in ('isq', 'linear'):
            path = folder / ('scheduler-' + profile + '.json')
            if path.exists():
                result[profile] = json.loads(path.read_text())
            elif (folder / ('scheduler-' + profile + '-progress.json')).exists():
                result[profile] = json.loads((folder / ('scheduler-' + profile + '-progress.json')).read_text())
                result[profile]['incomplete_process'] = True
            else:
                result[profile] = dict(status='Unavailable', reason='No completed scheduler report')
        if (folder / 'census.json').exists():
            result['census'] = json.loads((folder / 'census.json').read_text())
        result['private_witnesses'] = {path.name: sha(path) for path in
                                      sorted(folder.glob('scheduler-*-units.tsv'))}
        result['tail'] = (folder / 'native.log').read_text(errors='replace')[-3000:]
        results[name] = result
        write(record, results)
        print(name, resources['state'], [(p, result[p]['status'], len(result[p].get('units', [])),
                                          result[p].get('joint_calls')) for p in ('isq', 'linear')], flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--revision', required=True)
    p.add_argument('--maps', nargs='+', default=MAPS)
    a = p.parse_args()
    run(a.private, a.tool.resolve(), a.revision, a.maps)
