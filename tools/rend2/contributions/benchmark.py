#!/usr/bin/env python3
"""P06 owned numerical/query witnesses, resource measurements and determinism.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write


def run(tool, output):
    output.mkdir(parents=True, exist_ok=False)
    reports, resources = [], []
    for suffix in ('first', 'repeat'):
        path = output / (suffix + '.json')
        resource = execute([tool, path], output, suffix, seconds=180, memory=512*1024**2)
        if resource['exit_code'] or resource['state'] != 'Completed':
            raise ValueError('Native contribution tests failed: ' + str(resource))
        value = json.loads(path.read_text())
        if value['failures'] or value['checks'] < 100:
            raise ValueError('Missing/non-vacuous native assertions')
        reports.append(value)
        resources.append({k: v for k, v in resource.items() if k != 'command'})
    if reports[0] != reports[1]:
        raise ValueError('Identical P06 inputs changed semantic outputs')
    result = dict(schema='org.openjk.contribution-benchmark.v1',
                  protocol='Owned fixed-source attribution receipts + actual native S13 production fitting',
                  historical_stock_profile='UnknownProfile', stock_exact_replacement=False,
                  report=reports[0], semantic_repeat=True, resources=resources,
                  native_sha256=sha(tool),
                  base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())
    write(output / 'results.json', result)
    print('P06 native:', reports[0]['checks'], 'assertions; deterministic; no stock replacement permission')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_contributions_test.exe')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.tool.resolve(), a.output.resolve())
