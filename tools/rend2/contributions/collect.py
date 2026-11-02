#!/usr/bin/env python3
"""Freeze/verify P06 owned numerical witnesses; never copy map payloads.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import re

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def checks():
    logs = {'ctest': 'p06-ctest-final.log', 'final_subset': 'p06-ctest-sealed.log',
            'evaluation': 'p06-evaluation-tests.log', 'blind': 'p06-blind-tests.log',
            'positive': 'p06-positive-tests.log', 'controller': 'p06-controller-tests.log',
            'renderers': 'p06-renderers-sealed.log'}
    reports = {}
    for kind, name in logs.items():
        path = ROOT / 'build' / name
        value = path.read_text(encoding='utf-8', errors='replace')
        if kind in ('ctest', 'final_subset'):
            if '100% tests passed' not in value:
                raise ValueError('Failed CTest log: ' + name)
        elif kind == 'renderers':
            for target in ('rdsp-rend2_x86_64.dll', 'rd-rend2_x86_64.dll'):
                if target not in value:
                    raise ValueError('Missing Release target: ' + target)
        elif not re.search(r'\nOK\s*$', value):
            raise ValueError('Failed Python test log: ' + name)
        reports[kind] = dict(log=name, sha256=sha(path), tail=value.splitlines()[-7:])
    binaries = ['build/s06-gcc/lightrecon_contributions_test.exe',
                'build/s06-gcc/lightrecon_joint_tool.exe',
                'build/s06-gcc/lightrecon_bake_forward_tool.exe',
                'build/s13-renderers/Release/rdsp-rend2_x86_64.dll',
                'build/s13-renderers/Release/rd-rend2_x86_64.dll']
    return dict(schema='org.openjk.contribution-checks.v1', tests=reports,
                binaries={p: dict(sha256=sha(ROOT / p), bytes=(ROOT / p).stat().st_size)
                          for p in binaries},
                environment=dict(cpu='Intel family 6 model 165 stepping 2', logical_processors=12,
                                 os='Windows 10.0.19045', cpp='GCC 16.2.0 + MSVC 2019 x64 Release',
                                 cmake='4.4.2', python='3.12.14'),
                historical_failures=['Initial stale schema-6 disk witness updated explicitly to schema 7',
                                     'Compiled adapter omitted receiver footprint; preserved original UV-clipped geometry',
                                     'Historical disabled-shader baseline still differs; zero differences vs input commit'],
                game_launched=False, renderer_consumers_enabled=False)


def verify(folder):
    manifest = load(folder / 'manifest.json')
    for name, digest in manifest['source_sha256'].items():
        if sha(ROOT / name) != digest:
            raise ValueError('P06 source changed: ' + name)
    for name, digest in manifest['result_sha256'].items():
        if sha(folder / name) != digest:
            raise ValueError('P06 witness changed: ' + name)
    numerical = load(folder / 'native.json')
    if not numerical['semantic_repeat'] or numerical['report']['failures']:
        raise ValueError('Missing deterministic native acceptance')
    cases = {r['name']: r for r in numerical['report']['cases']}
    if (cases['point_opening']['predicted_r'] <= 0 or
            cases['point_opaque_wall']['predicted_r'] != 0 or
            cases['unsafe_replacement_adversaries']['negative_cases'] < 14 or
            cases['production_two_colour_observation_attribution']['maximum_root_reference_error'] > 1e-5):
        raise ValueError('Positive/negative/reference controls failed')
    compiled = load(folder / 'compiled.json')['cases']
    if len(compiled) != 6 or any(not r['result']['producer'] for r in compiled):
        raise ValueError('Incomplete production compiler controls')
    for r in compiled:
        if not r['wrong_profile'] and (not r['heldout']['available'] or
                r['heldout']['mse'] >= r['heldout']['black'] * .02):
            raise ValueError('Positive independent compiler prediction failed')
        if not r['wrong_profile'] and (r['result']['contribution_queries']['available'] == 0 or
                r['result']['contribution_queries']['maximum_roundtrip_error'] != 0):
            raise ValueError('Prepared compiled-data queries failed')
    print('P06 witness, positive controls and source identities verified')


def collect(a):
    a.out.mkdir(parents=True, exist_ok=False)
    files = [(a.native / 'results.json', 'native.json'),
             (a.compiled / 'compiled.json', 'compiled.json'),
             (a.compiled / 'resources.json', 'compiled-resources.json'),
             (a.forward / 'summary.json', 'forward.json'),
             (ROOT / 'build/p06-disabled-vs-input/report.json', 'disabled-path.json'),
             (ROOT / 'build/p06-architecture.json', 'architecture.json')]
    for source, name in files:
        shutil.copyfile(source, a.out / name)
    (a.out / 'checks.json').write_text(json.dumps(checks(), sort_keys=True, indent=2) + '\n')
    sources = list((ROOT / 'shared/rd-rend2/lightrecon').glob('*.cpp'))
    sources += list((ROOT / 'shared/rd-rend2/lightrecon').glob('*.h'))
    sources += list((ROOT / 'shared/rd-rend2').glob('tr_lightrecon*.cpp'))
    sources += list((ROOT / 'shared/rd-rend2').glob('tr_lightrecon*.h'))
    sources += [ROOT / 'shared/rd-rend2/lightrecon/CMakeLists.txt',
                ROOT / 'tools/lightrecon_test/CMakeLists.txt',
                ROOT / 'tools/lightrecon_test/lightrecon_contributions_test.cpp',
                ROOT / 'tools/lightrecon_test/lightrecon_joint_tool.cpp',
                ROOT / 'tools/lightrecon_test/lightrecon_disk_test.cpp',
                ROOT / 'tools/lightrecon_test/lightrecon_producer_test.cpp',
                ROOT / 'tools/rend2/all_stock/architecture.py']
    sources += list(Path(__file__).parent.glob('*.py'))
    manifest = dict(schema='org.openjk.contribution-witness.v1', canonical_schema=7,
                    producer_algorithm=4, membership_version=2,
                    base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    source_sha256={p.relative_to(ROOT).as_posix(): sha(p) for p in sources},
                    result_sha256={name: sha(a.out / name) for name in
                                   [name for _, name in files] + ['checks.json']},
                    acceptance=dict(prepared_queries='Operational', stock_exact_replacement=False,
                                    renderer_subtraction=False, compiler='Controlled feature subsets only'))
    (a.out / 'manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    verify(a.out)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--verify', type=Path)
    p.add_argument('--native', type=Path, default=ROOT / 'build/p06-native-sealed')
    p.add_argument('--compiled', type=Path, default=ROOT / 'build/p06-compiled-sealed')
    p.add_argument('--forward', type=Path, default=ROOT / 'build/p06-forward-final')
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    if a.verify:
        verify(a.verify)
    elif a.out:
        collect(a)
    else:
        p.error('Specify --out or --verify')
