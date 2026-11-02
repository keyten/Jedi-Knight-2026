#!/usr/bin/env python3
"""X12 native execution of the previously frozen six-map protocol.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.scene_schedule.run import run


def execute(private, tool, protocol, revision, maps=None):
    policy = json.loads(protocol.read_text())
    expected = {m['map']: m for m in policy['maps']}
    maps = maps or list(expected)
    if len(set(maps)) != len(maps) or not set(maps) <= expected.keys():
        raise ValueError('Maps must belong to the frozen selection')
    captures = json.loads((private / 'captures.json').read_text())
    if sha(private / 'inventory.json') != policy['inventory_sha256']:
        raise ValueError('Original 57-map inventory changed')
    if sha(ROOT / 'tools/rend2/all_stock/results/v4/maps.json') != policy['preserved_v4_sha256']:
        raise ValueError('Historical v4 witness changed')
    for name in maps:
        dataset = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if not dataset.exists():
            dataset = Path(captures[name]['dataset'])
        if sha(dataset) != expected[name]['dataset_sha256']:
            raise ValueError('Frozen dataset mismatch: ' + name)
    source_files = sorted((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    source_files += sorted((ROOT / 'tools/lightrecon_test').glob('*.[ch]*'))
    identity = dict(protocol_sha256=sha(protocol), native_sha256=sha(tool), maps=maps,
                    sources={p.relative_to(ROOT).as_posix(): sha(p) for p in source_files if p.is_file()})
    target = private / ('identity-' + revision + '.json')
    if target.exists():
        raise ValueError('Preserve old run identity; use a fresh revision')
    write(target, identity)
    run(private, tool, revision, maps)
    # Never silently call the corrected executable an unchanged v1 experiment.
    for name in maps:
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        for profile in ('isq', 'linear'):
            c = json.loads((folder / ('scheduler-' + profile + '-controller.json')).read_text())
            if 'coarseRows=max' in policy['native_policy']:
                if c.get('coarse_rows') != max(64, c.get('training_evidence_blocks', 0)):
                    raise ValueError('Native coarse policy differs from frozen corrected protocol')
            elif c.get('coarse_rows', 64) != 64:
                raise ValueError('Corrected native cannot reproduce legacy v1 policy; use frozen v1 executable or a new protocol')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--protocol', type=Path, default=Path(__file__).parent / 'results/v1/protocol.json')
    p.add_argument('--revision', required=True)
    p.add_argument('--maps', nargs='+')
    a = p.parse_args()
    execute(a.private, a.tool.resolve(), a.protocol, a.revision, a.maps)
