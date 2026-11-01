#!/usr/bin/env python3
"""Freeze X10 aggregate evidence; never copy licensed geometry/RGB into git.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MAPS = ('mp/duel4', 't1_inter', 't2_rancor', 'hoth3', 'hoth2', 'vjun2', 'mp/ffa3')


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def semantic(value):
    if isinstance(value, dict):
        return {k: semantic(v) for k, v in value.items()
                if k not in ('resources', 'selection_seconds')}
    if isinstance(value, list):
        return [semantic(v) for v in value]
    return value


def collect(private, revision, repeat_revision, compiled, compiled_repeat, controlled, repeat, out, checks=None):
    current = load(private / ('runs-' + revision + '.json'))
    repeated = load(private / ('runs-' + repeat_revision + '.json'))
    if semantic(current) != semantic(repeated):
        raise ValueError('Stock numerical/selection results are not repeatable')
    numerical, again = load(controlled), load(repeat)
    if numerical != again or numerical['failures'] or numerical['checks'] < 400:
        raise ValueError('Controlled sampling tests failed or changed on repeat')
    cases, cases_repeat = load(compiled), load(compiled_repeat)
    # Absolute private command/request paths differ between output directories.
    def compiled_semantic(v):
        if isinstance(v, dict):
            return {k: compiled_semantic(x) for k, x in v.items() if k != 'resources'}
        if isinstance(v, list):
            return [compiled_semantic(x) for x in v]
        return v
    if compiled_semantic(cases) != compiled_semantic(cases_repeat):
        raise ValueError('Compiler-backed sampling results changed on repeat')
    records = []
    for name in MAPS:
        v = current[name]
        if v['resources']['exit_code'] or 'sampling' not in v or 'proposals' not in v:
            raise ValueError('Stock run did not execute: ' + name)
        s = v['sampling']
        if s['folds_changed'] or s['gaps_shrunk'] or s['heldout_rgb_used_for_selection']:
            raise ValueError('Invalid isolation policy')
        records.append(dict(map=name, bsp_sha256=v['bsp_sha256'],
                            dataset_sha256=v['census']['archive_sha256'],
                            native_sha256=v['native_sha256'], sampling=s, proposals=v['proposals'],
                            private_witnesses=v['private_witnesses'],
                            resources={k: x for k, x in v['resources'].items() if k != 'command'}))
    for v in cases['cases']:
        if v['resources']['exit_code'] or 'trusted_source_metrics' not in v:
            raise ValueError('Compiler-backed native case failed')
        v['resources'] = {k: x for k, x in v['resources'].items() if k != 'command'}
    out.mkdir(parents=True, exist_ok=True)
    def write(name, value):
        (out / name).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    write('controlled.json', numerical)
    write('stock.json', dict(schema='org.openjk.sampling-stock-x10.v1', maps=records))
    write('compiled.json', cases)
    write('validation.json', dict(schema='org.openjk.sampling-validation-x10.v1',
                                  stock_repeated_identically=True, compiled_repeated_identically=True,
                                  controlled_repeated_identically=True,
                                  stock_maps=len(records), compiler_experiments=len(cases['cases']),
                                  controlled_assertions=numerical['checks'],
                                  physical_source_recovery=False,
                                  stock_source_truth='Unavailable', renderer_lighting_modified=False))
    write('checks.json', load(checks) if checks else dict(
        schema='org.openjk.sampling-checks-x10.v1', status='Unavailable',
        reason='Independent build/test records were not supplied; no pass is inferred'))
    files = ['shared/rd-rend2/lightrecon/' + x for x in
             ('sampling.cpp', 'sampling.h', 'hidden_candidates.cpp', 'hidden_candidates.h',
              'joint.cpp', 'joint.h', 'CMakeLists.txt')]
    files += ['tools/lightrecon_test/' + x for x in
              ('sampling_audit.h', 'sampling_protocol.h', 'lightrecon_map_solver.cpp',
               'lightrecon_sampling_test.cpp', 'lightrecon_hidden_tool.cpp',
               'lightrecon_hidden_test.cpp', 'lightrecon_joint_test.cpp', 'CMakeLists.txt')]
    files += ['tools/rend2/datasets/' + x for x in ('prepare_blind.py', 'solver_view.py', 'test_blind_views.py')]
    files += ['tools/rend2/observation_sampling/' + x for x in ('run.py', 'compiled.py', 'collect.py', 'verify_results.py', 'test_sampling.py')]
    write('manifest.json', dict(schema='org.openjk.sampling-manifest-x10.v1',
                               base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                               sampling_version=1, hidden_version=2, joint_version=4,
                               sources={x: sha(ROOT / x) for x in files},
                               artifacts={x: sha(out / x) for x in
                                          ('controlled.json', 'stock.json', 'compiled.json', 'validation.json', 'checks.json')}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--revision', required=True)
    p.add_argument('--repeat-revision', required=True)
    p.add_argument('--compiled', type=Path, required=True)
    p.add_argument('--compiled-repeat', type=Path, required=True)
    p.add_argument('--controlled', type=Path, required=True)
    p.add_argument('--repeat', type=Path, required=True)
    p.add_argument('--out', type=Path, default=Path(__file__).parent / 'results/v1')
    p.add_argument('--checks', type=Path, help='Optional independent build/test record; absence stays Unavailable')
    a = p.parse_args()
    collect(a.private, a.revision, a.repeat_revision, a.compiled, a.compiled_repeat,
            a.controlled, a.repeat, a.out, a.checks)
