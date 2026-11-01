#!/usr/bin/env python3
"""Bounded private X10 stock sampling/admission regression; never exports assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2/all_stock'))
from run import execute, sha, write

MAPS = ('mp/duel4', 't1_inter', 't2_rancor', 'hoth3', 'hoth2', 'vjun2', 'mp/ffa3')


def run(private, tool, revision, maps=None):
    inventory = json.loads((private / 'inventory.json').read_text())
    captures = json.loads((private / 'captures.json').read_text())
    record = private / ('runs-' + revision + '.json')
    if record.exists():
        raise ValueError('Preserve attempted runs; choose a fresh revision')
    native_hash = sha(tool)
    result = {}
    for name in maps or MAPS:
        item = next(x for x in inventory['maps'] if x['map'] == name)
        capture = captures[name]
        dataset = Path(capture['dataset'])
        derived = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if capture['status'] == 'ReusedNeedsIdentityCheck' and derived.exists():
            dataset = derived
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        if folder.exists() and any(folder.iterdir()):
            raise ValueError('Nonempty experiment folder')
        folder.mkdir(parents=True, exist_ok=True)
        if sha(tool) != native_hash:
            raise ValueError('Native binary changed during frozen run')
        resource = execute([tool, dataset, folder, item['sha256'], 'sampling-only'],
                           folder, 'native', seconds=600, memory=1024**3)
        entry = dict(bsp_sha256=item['sha256'], native_sha256=native_hash, resources=resource)
        for file, key in (('sampling.json', 'sampling'), ('sampling-proposals.json', 'proposals'),
                          ('census.json', 'census')):
            p = folder / file
            if p.exists():
                entry[key] = json.loads(p.read_text())
        if resource['exit_code']:
            entry['reason'] = (folder / 'native.log').read_text(errors='replace')[-2000:]
        for file in ('hierarchical-geometry.tsv', 'selected-geometry.tsv'):
            if (folder / file).exists():
                entry.setdefault('private_witnesses', {})[file] = sha(folder / file)
        result[name] = entry
        write(record, result)
        print(name, resource['state'], resource['exit_code'],
              sum(x['training']['admitted'] for x in entry.get('sampling', {}).get('jobs', [])), flush=True)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--revision', required=True)
    p.add_argument('--maps', nargs='*', choices=MAPS)
    a = p.parse_args()
    run(a.private, a.tool.resolve(), a.revision, a.maps)
