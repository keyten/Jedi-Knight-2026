#!/usr/bin/env python3
"""Export X19 existing native/build/control receipts; never launch a game."""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.appearance.report import seal


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    q = read(a.queries / 'queries.json')
    protocol = read(a.queries / 'protocol.json')
    for path, checksum in protocol['sources'].items():
        assert sha(ROOT / path) == checksum, path
    cpu = a.cpu.read_text(encoding='utf-8')
    assert '100% tests passed' in cpu
    native = read(a.controls / 'results.json')
    assert native['semantic_repeat'] and native['report']['failures'] == 0
    python = a.python.read_text(encoding='utf-8')
    assert '\nOK' in python
    delta = read(a.delta / 'report.json')
    assert delta['source_differences'] == 0 and delta['gpu_compiled']
    rows = []
    for i, pair in enumerate(q['paired']):
        b = pair['before']['result']['queries']['contribution']
        n = pair['after']['result']['queries']['contribution']
        assert n['allocations'] == 0 and n['ok'] == n['calls'] == 32768
        rows.append(dict(repetition=i+1, before=b, after=n))
    runtime = read(a.queries / 'runtime-mp/results.json')
    assert all(x['status'] == 'Unperformed' for x in runtime)
    dlls = {}
    build = a.renderers.read_text(encoding='utf-8', errors='replace')
    for name in ('rdsp-rend2_x86_64.dll', 'rd-rend2_x86_64.dll'):
        path = ROOT / 'build/x16-renderers/Release' / name
        assert path.exists() and name in build
        dlls[name] = sha(path)
    point = read(a.point / 'result.json') if (a.point / 'result.json').exists() else None
    point_cases = read(a.point / 'cases.csv.resources.json') if (a.point / 'cases.csv.resources.json').exists() else None
    result = dict(schema='org.openjk.runtime-hardening-results/1', status='Partial',
                  query_heap_allocations='Passed', source_metadata_lifetime='Passed',
                  actual_mp_frozen_world_AA='Unperformed',
                  pending='Explicit user permission required for any MP process; synthetic control tests are not actual runtime validation',
                  query_comparison=rows, source_controls=native,
                  cpu=dict(passed=int(re.search(r'out of (\d+)', cpu).group(1)),
                           seconds=float(re.search(r'Total Test time.*?=\s*([\d.]+)', cpu).group(1))),
                  point_refine_repeat=point, point_refine_case_work=point_cases,
                  python_tests=int(re.search(r'Ran (\d+) tests', python).group(1)),
                  renderer_dlls=dlls, disabled_path=delta,
                  historical_shader_baseline=read(a.gpu / 'report.json'),
                  runtime_mp=runtime, network_or_firewall_changes=False,
                  logs={p.name: sha(p) for p in (a.cpu, a.python, a.renderers)},
                  development_fixture_assertion=dict(failed_checks=1,
                      reason='New lifetime check initially expected calibration 1; the owned input fixture declares 2. Corrected to compare the immutable input record.',
                      receipt=read(ROOT / 'build/x19-contribution-controls/first.json')))
    write(a.out / 'results.json', result)
    write(a.out / 'protocol.json', protocol)
    write(a.out / 'queries.json', q)
    write(a.out / 'runtime-mp.json', runtime)
    table = ['| Repeat | Before allocations | After allocations | Before p50/p95/p99 ns | After p50/p95/p99 ns |',
             '|---|---:|---:|---:|---:|']
    for row in rows:
        b, n = row['before'], row['after']
        table.append(f"| {row['repetition']} | {b['allocations']} | {n['allocations']} | "
                     f"{b['p50_ns']}/{b['p95_ns']}/{b['p99_ns']} | {n['p50_ns']}/{n['p95_ns']}/{n['p99_ns']} |")
    (a.out / 'tables.md').write_text('\n'.join(table)+'\n', encoding='utf-8')
    seal(a.out)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--queries', type=Path, default=ROOT / 'build/x19-queries-final')
    p.add_argument('--controls', type=Path, default=ROOT / 'build/x19-contribution-controls-v2')
    p.add_argument('--cpu', type=Path, default=ROOT / 'build/x19-ctest.log')
    p.add_argument('--python', type=Path, default=ROOT / 'build/x19-python-final.log')
    p.add_argument('--renderers', type=Path, default=ROOT / 'build/x19-renderers.log')
    p.add_argument('--delta', type=Path, default=ROOT / 'build/x19-disabled-delta')
    p.add_argument('--gpu', type=Path, default=ROOT / 'build/x19-disabled-gpu')
    p.add_argument('--point', type=Path, default=ROOT / 'build/x19-point-timing')
    p.add_argument('--out', type=Path, required=True)
    main(p.parse_args())
