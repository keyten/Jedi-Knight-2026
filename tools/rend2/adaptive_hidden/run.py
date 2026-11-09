"""ASR16 public-input-only runner. Source answers never enter this module.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
import math
from pathlib import Path
import subprocess

from tools.rend2.hidden_candidates.run import request
from tools.rend2.lightmap_patches.run import read_rows, sha, write

ROOT = Path(__file__).resolve().parents[3]
POLICY = json.loads(Path(__file__).with_name('policy-v1.json').read_text())


def adapt(text, strategy, explained=(), directions=()):
    lines = text.splitlines()
    if not lines[0].startswith('JKHIDDEN1 '):
        raise ValueError('Expected original strict request')
    lines[0] = lines[0].replace('JKHIDDEN1 ', 'JKADAPTIVE1 ', 1)
    # Preserve training/geometry bounds, sample order and profile hypotheses.
    old = lines[2].split()
    lines[2] = ' '.join(map(str, [POLICY['max_nodes_per_area'], POLICY['coarse_divisions'],
        POLICY['refinement_rounds'], int(old[3]), 0, int(old[5])]))
    axes = strategy != 'strict'
    lines.insert(3, ' '.join(map(str, [int(strategy == 'adaptive'), int(axes), int(axes),
        POLICY['refine_seeds'], POLICY['max_rays'], POLICY['max_forward_calls'], POLICY['max_work']])))
    lines += [str(len(explained)), ' '.join(map(str, explained)), str(len(directions))]
    lines += [' '.join(map(str, d)) for d in directions]
    return '\n'.join(lines)+'\n'


def compiled_job(fixtures, name):
    inputs = fixtures/(name+'-views/inputs')
    metadata = json.loads((inputs/'manifest.json').read_text())
    # Separate descriptive discovery census. The historical folds remain intact;
    # no held-out prediction score or reuse of their selection is claimed here.
    source_rows = fixtures/(name+'.rows.jsonl')
    all_rows = [r for r in read_rows(source_rows) if r['position'] and r['normal'] and
                r['style'] == 0 and r['slot'] == 0 and r['coverage'] == 2 and r['fit_candidate']]
    # Predeclared floor census, farthest-point geometry coverage; no RGB crop.
    candidates = sorted([r for r in all_rows if r['normal'][2] > .99], key=lambda r: r['id'])
    rows = spatial(candidates, 64)
    if len(rows) < 6:
        raise ValueError('Insufficient supported original training rows for '+name)
    bounds = [min(r['position'][k] for r in all_rows)-16 for k in range(3)] + [
              max(r['position'][k] for r in all_rows)+16 for k in range(3)]
    return dict(name=name, text=request(rows, metadata['map_sha256'], bounds, bounds, False),
                geometry=inputs/'geometry.jkobs', provenance=dict(kind='existing_owned_compiled_floor_discovery_census',
                original_rows_sha256=sha(source_rows), prior_geometry_manifest_sha256=sha(inputs/'manifest.json'),
                scope='New full-floor discovery census; historical folds unchanged; no held-out predictive-quality claim'))


def spatial(candidates, maximum):
    if not candidates:
        return []
    selected, distance = [], {r['id']: float('inf') for r in candidates}
    remaining = {r['id']: r for r in candidates}
    while remaining and len(selected) < maximum:
        key = min(remaining, key=lambda key: (-distance[key], key))
        r = remaining.pop(key); selected.append(r)
        for k, s in remaining.items():
            distance[k] = min(distance[k], math.dist(r['position'], s['position']))
    return selected


def run(tool, output, fixtures, bridge, extra=()):
    output.mkdir(parents=True, exist_ok=False)
    jobs = [compiled_job(fixtures, name) for name in ('point_room', 'spot', 'two_colour')]
    for variant in ('direct', 'passive_bounce1'):
        folder = bridge/'bridge'/variant
        for i in range(1, 9):
            path = folder/('search-'+str(i)+'.request')
            jobs.append(dict(name='v02-'+variant+'-'+str(i), text=path.read_text(),
                geometry=folder/'views/inputs/geometry.jkobs', provenance=dict(kind='existing_V02_public_request',
                original_request_sha256=sha(path), variant=variant, shard=i)))
    for specification in extra:
        data = json.loads(Path(specification).read_text())
        jobs.append(dict(name=data['name'], text=Path(data['request']).read_text(),
                         geometry=Path(data['geometry']), provenance=data['provenance']))
    manifest = dict(schema='org.openjk.adaptive-hidden-frozen.v1', policy=POLICY,
        policy_sha256=sha(Path(__file__).with_name('policy-v1.json')), tool_sha256=sha(tool), jobs=[],
        sources={str(p.relative_to(ROOT)).replace('\\', '/'): sha(p) for p in
            sorted(list(Path(__file__).parent.glob('*.py'))+[
                ROOT/'tools/lightrecon_test/adaptive_hidden.cpp', ROOT/'tools/lightrecon_test/adaptive_hidden.h',
                ROOT/'tools/lightrecon_test/lightrecon_hidden_tool.cpp',
                ROOT/'shared/rd-rend2/lightrecon/hidden_candidates.cpp', ROOT/'shared/rd-rend2/lightrecon/scene_schedule.cpp'])},
        private_answers_read=False, heldout_targets_read=False, historical_source_certified=False)
    for job in jobs:
        folder = output/job['name']; folder.mkdir()
        for strategy in ('strict', 'strict_geometry', 'adaptive'):
            path = folder/(strategy+'.request')
            path.write_text(adapt(job['text'], strategy), encoding='ascii')
            result = subprocess.run([str(tool.resolve()), str(job['geometry'].resolve()), str(path.resolve())],
                capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise RuntimeError(job['name']+'/'+strategy+': '+result.stderr)
            report = json.loads(result.stdout)
            report.pop('resources', None)  # Freeze deterministic numerical data, not wall clock.
            write(folder/(strategy+'.json'), report)
            manifest['jobs'].append(dict(name=job['name'], strategy=strategy, provenance=job['provenance'],
                geometry_sha256=sha(job['geometry']), request_sha256=sha(path),
                report_sha256=sha(folder/(strategy+'.json')), candidates=len(report['candidates']),
                code=report['code'], rays=report['rays'], forward_calls=report['forward_calls'],
                nodes=sum(a['nodes'] for a in report['areas'])))
            print(job['name'], strategy, 'code', report['code'], 'candidates', len(report['candidates']),
                  'nodes', manifest['jobs'][-1]['nodes'], 'rays', report['rays'], flush=True)
    write(output/'frozen.json', manifest)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, default=ROOT/'build/x02/compiled-fixtures-frozen')
    parser.add_argument('--bridge', type=Path, default=Path(os.environ['TEMP'])/'jk-lightrecon-v02-bridge-v2')
    parser.add_argument('--extra', type=Path, nargs='*', default=[])
    a = parser.parse_args()
    run(a.tool, a.out, a.fixtures, a.bridge, a.extra)
