#!/usr/bin/env python3
"""ASR21 frozen sparse witnesses; no asset publication or runtime consumer.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.aesthetic_fit import run as baseline

sha, load, write = baseline.sha, baseline.load, baseline.write


def compiled(a, name):
    answer = load(a.bakes/'answers'/(name+'.q3map-recipe.json'))
    bsp = a.bakes/'authoring/base/maps'/(name+'.bsp')
    lock = load(ROOT/'tools/rend2/datasets/q3map2-validation.json')
    bsp_hash = sha(bsp)
    if (answer['compiled_bsp_sha256'] != bsp_hash or
            lock['maps'][name]['bsp_sha256'] != bsp_hash or
            answer['binary_sha256'] != lock['compiler']['executable_sha256']):
        raise ValueError('Frozen compiler/BSP mismatch')
    view = a.views/name/'views'
    train = baseline.TrainingView(view/'inputs', bsp_hash)
    evaluator = baseline.EvaluationView(view/'evaluator', bsp_hash,
                                       permitted=('validation', 'heldout'))
    partitions = [baseline.select(train.observations(), 48)]
    partitions += [baseline.select(evaluator.observations(role), 48)
                   for role in ('validation', 'heldout')]
    seen_ids, seen_blocks = set(), set()
    for rows in partitions:
        ids, blocks = {r['id'] for r in rows}, {baseline.block(r) for r in rows}
        if len(rows) < 8 or ids & seen_ids or blocks & seen_blocks:
            raise ValueError('Missing/disjoint compiled spatial coverage')
        seen_ids |= ids
        seen_blocks |= blocks
    # Owned benchmark anchors only. Authored RGB/intensity never enters fitting.
    anchors = [s['position_world'] for s in answer['ground_truth']['lights']]
    fixtures = [' '.join(map(str, ['point', i+1, *p, 100000, 100000, 100000,
                                  'visible', 'owned-authored-anchor-'+str(i+1)]))
                for i, p in enumerate(anchors)]
    if len(anchors) == 2:
        rival = [sum(p[k] for p in anchors)/2 for k in range(3)]
    else:
        rival = [anchors[0][0]+64, anchors[0][1]-64, anchors[0][2]]
    fixtures.append(' '.join(map(str, ['point', 100, *rival, 100000, 100000, 100000,
                                      'hidden', 'predeclared-geometric-rival'])))
    fixtures.append(fixtures[0])  # provider duplicate: one identity, never two amplitudes
    lines = ['JKASR21-SPARSE-ENCODED-ASSUMED-MATERIAL-1', '1', '8 8 8',
             '4 128 0.01 0.00001 0.00001 0.0000001 0.01 0.01 asr21-compiled-v1',
             str(len(fixtures)), *fixtures]
    lines += baseline.rows_text(partitions[0])+baseline.rows_text(partitions[1])
    fit, held = a.out/(name+'-fit.txt'), a.out/(name+'-held.txt')
    fit.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    held.write_text('\n'.join(baseline.rows_text(partitions[2]))+'\n', encoding='utf-8')
    frozen = dict(bsp_sha256=bsp_hash, compiler_sha256=answer['binary_sha256'],
                  geometry_sha256=sha(train.geometry_path), fit_sha256=sha(fit), held_sha256=sha(held),
                  partitions={role: sha(view/('inputs' if role == 'train' else 'evaluator')/(role+'.jsonl'))
                              for role in ('train', 'validation', 'heldout')},
                  selection='Geometry-only stable spatial strata, maximum 48 rows per fold',
                  candidate_geometry='Owned authored anchors, one fixed geometric rival, one exact duplicate',
                  authored_rgb_or_intensity_in_request=False)
    write(a.out/(name+'-protocol.json'), frozen)
    raw, report = baseline.native(a.tool, train.geometry_path, fit, held)
    repeated, _ = baseline.native(a.tool, train.geometry_path, fit, held)
    if raw != repeated:
        raise ValueError('Nondeterministic compiled report')
    (a.out/(name+'.json')).write_text(raw, encoding='utf-8')
    selected = report['selected']
    if selected < 0:
        raise ValueError('No validated sparse configuration on '+name)
    chosen = report['configurations'][selected]
    if (len(chosen['sources']) != len(anchors) or not chosen['heldout']['available'] or
            chosen['heldout']['robust'] >= .5*report['baseline']['heldout']['robust'] or
            report['sparse']['duplicate_inputs_removed'] != 1 or
            len({s['fixture'] for s in chosen['sources']}) != len(chosen['sources'])):
        raise ValueError('Sparse count/heldout/duplicate assertion failed on '+name)
    if (sha(bsp) != frozen['bsp_sha256'] or sha(train.geometry_path) != frozen['geometry_sha256'] or
            sha(fit) != frozen['fit_sha256'] or sha(held) != frozen['held_sha256'] or
            any(sha(view/('inputs' if role == 'train' else 'evaluator')/(role+'.jsonl')) != digest
                for role, digest in frozen['partitions'].items())):
        raise ValueError('Compiled inputs changed during evaluation')
    return dict(name=name, selected_ids=[s['fixture'] for s in chosen['sources']],
                baseline_heldout=report['baseline']['heldout'], selected_heldout=chosen['heldout'],
                models=report['sparse']['models'], repeat_identical=True)


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    files = [ROOT/'tools/lightrecon_test'/n for n in
             ('aesthetic_fit.h', 'aesthetic_fit.cpp', 'aesthetic_sparse.inc', 'aesthetic_test_scene.h',
              'lightrecon_sparse_test.cpp', 'lightrecon_aesthetic_test.cpp',
              'lightrecon_aesthetic_tool.cpp', 'aesthetic_region.h', 'joint_test_scene.h',
              'joint_fixture.h', 'CMakeLists.txt')]
    files += [Path(__file__).resolve(), ROOT/'tools/rend2/aesthetic_fit/run.py',
              ROOT/'tools/rend2/datasets/solver_view.py', ROOT/'tools/rend2/datasets/evaluation_view.py',
              ROOT/'tools/rend2/joint/compiled.py', ROOT/'tools/rend2/datasets/q3map2-validation.json']
    core = sorted((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    core += [ROOT/'shared/rd-rend2'/n for n in ('tr_lightrecon_types.cpp', 'tr_lightrecon_types.h')]
    frozen = {p.relative_to(ROOT).as_posix(): sha(p) for p in files+core}
    prior = load(ROOT/'tools/rend2/aesthetic_fit/results/asr20-v1/protocol.json')['sources']
    for path in core:
        name = path.relative_to(ROOT).as_posix()
        if prior.get(name) != frozen[name]:
            raise ValueError('Historical core or canonical storage changed since ASR20: '+name)
    binaries = {'tool': sha(a.tool), 'test': sha(a.test), 'baseline_test': sha(a.baseline_test)}
    write(a.out/'protocol.json', dict(schema='org.openjk.sparse-protocol.v1', sources=frozen,
          binaries=binaries, frozen_before_evaluation=True, target_domain='Explicit relative or forward encoded appearance',
          selection='Bounded training greedy/prune/swap; frozen validation; compact equivalent alternatives; visible tie preference',
          historical_source_truth='Unavailable', runtime_changed=False, canonical_storage_changed=False))
    for name in ('owned', 'owned-repeat'):
        subprocess.run([str(a.test), str((a.out/(name+'.json')).resolve())], check=True, timeout=300)
    if (a.out/'owned.json').read_bytes() != (a.out/'owned-repeat.json').read_bytes():
        raise ValueError('Nondeterministic owned report')
    subprocess.run([str(a.baseline_test)], check=True, timeout=300)
    cases = [compiled(a, n) for n in ('point_room', 'two_colour', 'door_occlusion')]
    write(a.out/'compiled.json', dict(cases=cases, historical_source_truth='Unavailable'))
    if any(sha(ROOT/p) != digest for p, digest in frozen.items()):
        raise ValueError('Sources changed during evaluation')
    if any(sha(getattr(a, n)) != digest for n, digest in binaries.items()):
        raise ValueError('Binaries changed during evaluation')
    write(a.out/'manifest.json', dict(sources_unchanged=True, historical_core_matches_asr20=True,
          owned_repeat_identical=True, compiled_repeat_identical=True, canonical_storage_changed=False,
          runtime_changed=False, artifacts={p.name: sha(p) for p in sorted(a.out.iterdir()) if p.is_file()}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--views', type=Path, default=ROOT/'build/x07-verified')
    parser.add_argument('--bakes', type=Path, default=ROOT/'build/q3map2-bake-verified')
    parser.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_aesthetic_tool.exe')
    parser.add_argument('--test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_sparse_test.exe')
    parser.add_argument('--baseline-test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_aesthetic_test.exe')
    main(parser.parse_args())
