#!/usr/bin/env python3
"""Freeze ASR19 owned witnesses and bounded geometry-only stock rankings.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.emitter_support.run import MAPS, dataset
from tools.rend2.emitter_support.run_diagnosis import semantic


def run(command, folder, label):
    resource = execute(command, folder, label, seconds=300, memory=1024**3)
    if resource['state'] != 'Completed' or resource['exit_code']:
        raise ValueError('Native shape audit failed: '+label)
    return resource


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    private = a.private/('runs-asr19-'+a.out.name)
    private.mkdir(exist_ok=False)
    tool, test = private/'map-solver.exe', private/'controlled.exe'
    shutil.copyfile(a.tool, tool)
    shutil.copyfile(a.test, test)
    baseline = json.loads((a.baseline/'stock.json').read_text())
    captures = json.loads((a.private/'captures.json').read_text())
    paths = list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    paths += [ROOT/'shared/rd-rend2'/name for name in ('tr_lightrecon_types.cpp', 'tr_lightrecon_types.h')]
    paths += [ROOT/'tools/lightrecon_test'/name for name in (
        'CMakeLists.txt', 'lightrecon_map_solver.cpp', 'shape_selection_audit.h',
        'lightrecon_shape_selection_test.cpp', 'mount_audit.h', 'mount_diagnosis.h')]
    paths += list(Path(__file__).resolve().parent.glob('*.py'))
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
    before = json.loads((a.baseline/'protocol.json').read_text())
    unchanged = ('emitter_shape.cpp', 'emitter_shape.h', 'emitter_support.cpp', 'emitter_support.h',
                 'regions_support.cpp', 'point_refine.cpp', 'visibility.cpp', 'evidence.cpp',
                 'candidate_pool.cpp', 'producer.cpp', 'bake_forward.cpp', 'bake_integral.cpp')
    for name in unchanged:
        path = 'shared/rd-rend2/lightrecon/'+name
        if hashes[path] != before['sources'][path]:
            raise ValueError('Existing solver/admission changed: '+path)
    protocol = dict(schema='org.openjk.shape-selection-protocol.v1',
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        remote=subprocess.check_output(['git', 'remote', 'get-url', 'origin'], cwd=ROOT, text=True).strip(),
        sources=hashes, native_sha256=sha(tool), controlled_sha256=sha(test),
        baseline_protocol_sha256=sha(a.baseline/'protocol.json'), unchanged_solver_files=list(unchanged),
        frozen_before_evaluation=True, max_stock_fixtures_per_map=64, max_geometry_work=16000000,
        selection='Stable original candidate ID; exact surface aliases collapsed for audit only',
        stock_target_rgb_consumed=False, stock_fits_attempted=False,
        shape_prior_is_probability=False, historical_source_truth='Unavailable',
        maps=[baseline[name]['input'] for name in MAPS])
    write(a.out/'protocol.json', protocol)
    for label in ('controlled-first', 'controlled-repeat'):
        run([test, (a.out/(label+'.json')).resolve()], a.out, label)
    if (a.out/'controlled-first.json').read_bytes() != (a.out/'controlled-repeat.json').read_bytes():
        raise ValueError('Owned ranking/numeric witnesses changed on repeat')
    controlled = json.loads((a.out/'controlled-first.json').read_text())
    if controlled['failures']:
        raise ValueError('Owned numerical fixture failed')
    reports = {}
    for name in MAPS:
        entry = baseline[name]['input']
        archive = dataset(a.private, captures, name)
        if sha(archive) != entry['dataset_sha256']:
            raise ValueError('Original input archive identity changed')
        folder = private/name.replace('/', '_')
        folder.mkdir()
        resource = run([tool, archive, folder, entry['bsp_sha256'], 'shape-selection-only'], folder, 'native')
        rows = [json.loads(line) for line in (folder/'shape-selection.jsonl').read_text().splitlines()]
        summary = json.loads((folder/'shape-selection-summary.json').read_text())
        if summary['attempts'] != len(rows) or len(rows) > 64:
            raise ValueError('Invalid stock shape audit row count')
        if any(row['primitive_known'] or row['emitting_geometry_exact'] for row in rows):
            raise ValueError('Unretained archived masks were silently certified')
        if len({row['fixture'] for row in rows}) != len(rows):
            raise ValueError('Duplicate fixture ID')
        samples = []
        for row in rows[:4]:
            samples.append({k: row[k] for k in ('fixture', 'key', 'material', 'primitive_known', 'limitation')})
            samples[-1]['ranked'] = [{k: v[k] for k in ('type', 'status', 'preference', 'reasons', 'uncertainties')}
                                     for v in row['ranked']]
        # Adding a ranking API must not change the existing admission path.
        admission = private/(name.replace('/', '_')+'-admission')
        admission.mkdir()
        admission_resource = run([tool, archive, admission, entry['bsp_sha256'], 'mounting-only'], admission, 'native')
        if semantic(admission/'mounting.json') != baseline[name]['current_admission']:
            raise ValueError('Existing mounted admission changed')
        if sha(admission/'mounting-witnesses.tsv') != baseline[name]['private_artifacts']['mounting-witnesses.tsv']:
            raise ValueError('Existing admission source/reason witnesses changed')
        reports[name] = dict(summary=summary, resources=resource, input=entry, samples=samples,
            admission_resources=admission_resource, admission_semantic_and_witness_match=True,
            private_artifacts={p.name: sha(p) for p in folder.iterdir() if p.is_file()})
        write(a.out/'stock.json', reports)
        print(name, summary['attempts'], summary['bound_only_top_preferences'], flush=True)
    repeat = private/'mp_duel4-repeat'
    repeat.mkdir()
    name = MAPS[0]
    run([tool, dataset(a.private, captures, name), repeat, baseline[name]['input']['bsp_sha256'],
         'shape-selection-only'], repeat, 'native')
    if any(sha(repeat/filename) != sha(private/'mp_duel4'/filename) for filename in
           ('shape-selection.jsonl', 'shape-selection-summary.json')):
        raise ValueError('Stock shape ranking changed on repeat')
    if any(sha(ROOT/name) != digest for name, digest in hashes.items()):
        raise ValueError('Sources changed during audit')
    if sha(tool) != protocol['native_sha256'] or sha(test) != protocol['controlled_sha256']:
        raise ValueError('Frozen executable changed')
    write(a.out/'manifest.json', dict(sources_unchanged_during_audit=True, stock_repeat_identical=True,
        controlled_repeat_identical=True, unchanged_solver_and_admission=True,
        canonical_storage_changed=False, runtime_changed=False, licensed_geometry_committed=False,
        artifacts={p.name: sha(p) for p in a.out.iterdir() if p.is_file()}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--baseline', type=Path, default=ROOT/'tools/rend2/emitter_support/results/asr18-v1')
    p.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_shape_selection_test.exe')
    main(p.parse_args())
