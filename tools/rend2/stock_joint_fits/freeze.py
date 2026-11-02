#!/usr/bin/env python3
"""Freeze X12 selection before evaluating native stock fits. No licensed payloads.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import platform
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write

MAPS = ('mp/duel4', 't1_danger', 'mp/siege_desert', 'academy1', 'hoth3', 'hoth2')
REASONS = ('small MP; previous local admission failure', 'optional solar-provider regression',
           'previous overlapping transport geometry', 'positive observations; scene scheduling regression',
           'complex indoor SP regression', 'outdoor/sky control; no assumption of observable sunlight')


def freeze(private, out):
    if out.exists():
        raise ValueError('Frozen protocol already exists; never overwrite it')
    inventory = json.loads((private / 'inventory.json').read_text())
    captures = json.loads((private / 'captures.json').read_text())
    maps = []
    for name, reason in zip(MAPS, REASONS):
        item = next(m for m in inventory['maps'] if m['map'] == name)
        dataset = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
        if not dataset.exists():
            dataset = Path(captures[name]['dataset'])
        maps.append(dict(map=name, selection=reason, bsp_sha256=item['sha256'],
                         archive_member=item['member'], mode=item['mode'],
                         dataset_sha256=sha(dataset), dataset_bytes=dataset.stat().st_size))
    record = dict(schema='org.openjk.stock-joint-protocol.v2',
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  inventory_sha256=sha(private / 'inventory.json'),
                  preserved_v4_sha256=sha(ROOT / 'tools/rend2/all_stock/results/v4/maps.json'),
                  preserved_x11_sha256=sha(ROOT / 'tools/rend2/scene_schedule/results/v1/stock.json'),
                  maps=maps, frozen_before_new_evaluation=True,
                  environment=dict(platform=platform.platform(), cpu=platform.processor(),
                                   python=platform.python_version()),
                  selection_uses_heldout_rgb=False,
                  native_policy='scene_audit.h: coarseRows=max(64, distinct training channel/region/block tuples); other X11 bounds unchanged',
                  compiler='GCC 16.2.0; Release; CMake 4.4.2; double accumulation',
                  split=dict(seed=20261031, block_world=256, origin=[17, 31, 47],
                             proportions=[.6, .2, .2], gap='3 * footprint radius + 1 world unit'),
                  profiles=['conditional NetRadiant inverse-square', 'conditional NetRadiant linear'],
                  historical_stock_profile='UnknownProfile', absolute_photometry='Unknown',
                  maximum_joint_calls_per_profile=4, maximum_models_per_call=12,
                  scene_max_rays=4000000, scene_max_work=500000000,
                  process_seconds=600, process_bytes=1024**3,
                  no_fit_policy='No held-out query/publication; diagnose precise blockers without weakening physical gates',
                  acceptance=['positive comparable observations', 'independent held-out improvement over black/ambient',
                              'no known opaque-wall contradictions', 'uncertainty retained',
                              'canonical payload validation; no SI/exact-source claims'],
                  conditional_ablations='Only for an accepted stock fit: removal, wrong XYZ/profile/type/count, direct versus passive GI',
                  optional_later_changes='Must use a separately versioned protocol and retain this attempt')
    out.parent.mkdir(parents=True, exist_ok=True)
    write(out, record)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    freeze(a.private, a.out)
