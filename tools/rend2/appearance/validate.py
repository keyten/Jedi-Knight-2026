#!/usr/bin/env python3
"""Collect existing CPU/build/cache/disabled-renderer receipts; does not run games."""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.appearance.report import seal


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main(a):
    text = a.cpu.read_text(encoding='utf-8')
    assert '100% tests passed' in text
    count = int(re.search(r'out of (\d+)', text).group(1))
    seconds = float(re.search(r'Total Test time.*?=\s*([\d.]+)', text).group(1))
    controlled = load(a.controlled / 'summary.json')
    assert controlled['repeat_identical'] and controlled['semantic']['failures'] == 0
    stock = load(a.stock / 'stock.json')
    previous = load(a.previous / 'stock.json')
    retained = ('sample_manifest', 'fold_counts', 'topology_code', 'field_code', 'field_reason',
                'metrics', 'common_domain', 'groups', 'chunk_content_key', 'available_values')
    for name, entry in stock.items():
        assert entry['resources']['state'] == 'Completed' and entry['resources']['exit_code'] == 0
        for x, y in zip(entry['result']['rois'], previous[name]['result']['rois']):
            assert {k: x[k] for k in retained} == {k: y[k] for k in retained}
    python = a.python.read_text(encoding='utf-8')
    assert '\nOK' in python
    programs = load(a.shader)
    delta = load(a.delta)
    assert programs['gpu_compiled'] and delta['source_differences'] == 0
    dlls = {}
    for name in ('rdsp-rend2_x86_64.dll', 'rd-rend2_x86_64.dll'):
        path = ROOT / 'build/x16-renderers/Release' / name
        assert path.is_file() and name in a.renderers.read_text(encoding='utf-8', errors='replace')
        dlls[name] = dict(sha256=sha(path), bytes=path.stat().st_size)
    write(a.out / 'validation.json', dict(
        schema='org.openjk.appearance-validation/1',
        cpu=dict(passed=count, failed=0, seconds=seconds),
        controlled=controlled, python_tests=int(re.search(r'Ran (\d+) tests', python).group(1)),
        renderer_dlls=dlls, disabled_shader_current_vs_X17=delta,
        historical_shader_baseline=programs,
        receipts_only_change_preserved_v2_numerical_results=True,
        stock_repeat=load(a.stock / 'repeat.json'),
        statically_verified_query_links=sum(r['support_check_clear'] for e in stock.values() for r in e['result']['rois']),
        blocked_supported_links=sum(r['support_check_blocked'] for e in stock.values() for r in e['result']['rois']),
        unknown_supported_links=sum(r['support_check_unknown'] for e in stock.values() for r in e['result']['rois']),
        logs={p.name: sha(p) for p in (a.cpu, a.python, a.renderers)},
        game_launched=False, runtime_consumers_enabled=False))
    seal(a.out)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--out', type=Path, default=ROOT / 'tools/rend2/appearance/results/v1')
    p.add_argument('--stock', type=Path, default=ROOT / 'build/x18-stock-v3')
    p.add_argument('--previous', type=Path, default=ROOT / 'build/x18-stock-v2')
    p.add_argument('--controlled', type=Path, default=ROOT / 'build/x18-controlled-receipts')
    p.add_argument('--cpu', type=Path, default=ROOT / 'build/x18-ctest-final.log')
    p.add_argument('--python', type=Path, default=ROOT / 'build/x18-python-final.log')
    p.add_argument('--renderers', type=Path, default=ROOT / 'build/x18-renderers-final.log')
    p.add_argument('--shader', type=Path, default=ROOT / 'build/x18-disabled-gpu/report.json')
    p.add_argument('--delta', type=Path, default=ROOT / 'build/x18-disabled-delta/report.json')
    main(p.parse_args())
