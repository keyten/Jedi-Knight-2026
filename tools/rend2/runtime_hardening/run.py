#!/usr/bin/env python3
"""X19 paired V02 native workload; never launches a game."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.validation import runtime_mp


def main(a):
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=False)
    binaries = {}
    for name in ('before', 'after'):
        target = a.out / (name + '.exe')
        shutil.copyfile(getattr(a, name), target)
        binaries[name] = target
    sources = [ROOT / x for x in (
        'shared/rd-rend2/tr_lightrecon_api.h', 'shared/rd-rend2/tr_lightrecon_api.cpp',
        'shared/rd-rend2/lightrecon/contributions.cpp',
        'tools/lightrecon_test/lightrecon_validation_tool.cpp',
        'tools/lightrecon_test/lightrecon_api_test.cpp',
        'tools/lightrecon_test/lightrecon_contributions_test.cpp',
        'tools/rend2/validation/runtime_mp.py', 'tools/rend2/validation/mp_control.py')]
    policy = dict(schema='org.openjk.runtime-hardening-protocol/1',
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  before_revision='07241cc39 (frozen pre-X19 native executable)',
                  executables={name: sha(path) for name, path in binaries.items()},
                  sources={p.relative_to(ROOT).as_posix(): sha(p) for p in sources},
                  samples=4096, batch=8, warmup=128, repetitions=3,
                  contribution_calls=32768, max_success_allocations=0,
                  p95_soft_budget_ns=250000, known_source_inputs=True,
                  mp_launch_authorized=False, mp_processes=0,
                  environment=dict(os=platform.platform(), cpu=platform.processor(),
                                   compiler='GCC 16.2 C++11 Release -O3 -DNDEBUG'))
    write(a.out / 'protocol.json', policy)
    results = []
    for repetition in range(3):
        pair = {}
        for label in ('before', 'after'):
            stem = label + '-' + str(repetition + 1)
            path = a.out / (stem + '.json')
            work = execute([binaries[label], path], a.out, stem, seconds=120, memory=512*1024**2)
            if work['state'] != 'Completed' or work['exit_code']:
                raise RuntimeError(stem + ': native workload failed')
            data = json.loads(path.read_text())
            row = data['queries']['contribution']
            assert row['calls'] == row['ok'] == 32768 and row['unavailable'] == 0
            if label == 'after':
                assert row['allocations'] == 0 and row['p95_ns'] <= 250000
            pair[label] = dict(result=data, resources=work)
        for name, x in pair['before']['result']['queries'].items():
            y = pair['after']['result']['queries'][name]
            assert (x['ok'], x['unavailable'], x['calls']) == (y['ok'], y['unavailable'], y['calls'])
        results.append(pair)
    write(a.out / 'queries.json', dict(schema='org.openjk.runtime-hardening-queries/1',
                                      paired=results, query_status_counts_preserved=True,
                                      mp_control_classification_tests_are_not_real_AA=True))
    # Explicitly record missing launch permission without reading installations,
    # preparing assets, creating network sockets or spawning any game executable.
    from types import SimpleNamespace
    runtime_mp.run(SimpleNamespace(output=a.out / 'runtime-mp',
                                  maps=['mp/duel4', 'mp/ffa3'], profiles=['legacy', 'modern'],
                                  allow_mp_launch=False))
    for path, checksum in policy['sources'].items():
        assert sha(ROOT / path) == checksum, path
    write(a.out / 'manifest.json', dict(artifacts={p.name: sha(p) for p in a.out.iterdir()
                                                if p.is_file() and p.suffix == '.json'},
                                       no_game_launch=True))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_validation_tool.exe')
    p.add_argument('--out', type=Path, required=True)
    main(p.parse_args())
