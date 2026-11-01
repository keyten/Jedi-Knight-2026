#!/usr/bin/env python3
"""X09 private licensed-map regression. No payloads are copied into the repository.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2/all_stock'))
from run import execute, sha, write


def run(private, tool, revision, producer=False):
    inventory = json.loads((private / 'inventory.json').read_text())
    captures = json.loads((private / 'captures.json').read_text())
    result = {}
    record_path = private / ('runs-' + revision + '.json')
    if record_path.exists():
        raise RuntimeError('Preserve attempted runs: choose a new --revision')
    native_hash = sha(tool)
    for name in ('mp/siege_desert', 't2_dpred', 't3_bounty'):
        item = next(x for x in inventory['maps'] if x['map'] == name)
        capture = captures[name]
        dataset = Path(capture['dataset'])
        derived = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if capture['status'] == 'ReusedNeedsIdentityCheck' and derived.exists():
            dataset = derived
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        if folder.exists() and any(folder.iterdir()):
            raise RuntimeError('Do not reuse a nonempty experiment folder: ' + str(folder))
        folder.mkdir(parents=True, exist_ok=True)
        if sha(tool) != native_hash:
            raise RuntimeError('Native executable changed during frozen run')
        command = [tool, dataset, folder, item['sha256']]
        if not producer:
            command.append('transport-only')
        resource = execute(command, folder, 'native', seconds=600, memory=1024**3)
        record = dict(bsp_sha256=item['sha256'], native_sha256=native_hash, resources=resource)
        path = folder / 'transport.json'
        if path.exists():
            record['transport'] = json.loads(path.read_text())
        contacts = folder / 'transport-contact-regressions.json'
        if contacts.exists():
            record['contacts'] = json.loads(contacts.read_text())
        for filename in ('transport-overlap-witnesses.tsv', 'selected-geometry.tsv', 'transport.obj',
                         'observation-transport.tsv', 'transport-patches.tsv', 'transport-singular-geometry.tsv'):
            p = folder / filename
            if p.exists():
                record.setdefault('private_witness_hashes', {})[filename] = sha(p)
        census = folder / 'census.json'
        if census.exists():
            record['dataset_sha256'] = json.loads(census.read_text())['archive_sha256']
        for profile in ('isq', 'linear'):
            p = folder / (profile + '.json')
            if p.exists():
                record[profile] = json.loads(p.read_text())
        result[name] = record
        write(record_path, result)
        print(name, resource['state'], resource['exit_code'], record.get('transport', {}).get('transfer_reason'), flush=True)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--revision', required=True)
    p.add_argument('--producer', action='store_true')
    a = p.parse_args()
    run(a.private, a.tool.resolve(), a.revision, a.producer)
