#!/usr/bin/env python3
"""Seal X12 checks/known-compiler witnesses after actual tests, never invent passes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.all_stock.architecture import audit


def load(path):
    return json.loads(path.read_text())


def seal():
    output = Path(__file__).parent / 'results/v1'
    cpu = load(ROOT / 'build/x12-cpu.json')
    renderers = load(ROOT / 'build/x12-renderers-retry.json')
    log = (ROOT / 'build/x12-cpu.log').read_text()
    count = re.search(r'100% tests passed(?:, 0 tests failed)? out of (\d+)', log)
    if cpu['exit_code'] or renderers['exit_code'] or not count or int(count[1]) < 35:
        raise ValueError('Native CPU/build acceptance not complete')
    compiled = load(ROOT / 'build/x12-compiled-b/compiled.json')
    previous = load(ROOT / 'build/x12-compiled-a/compiled.json')
    if compiled['cases'] != previous['cases'] or compiled['repeat_archive_sha256'] != previous['repeat_archive_sha256']:
        raise ValueError('Known-compiler repeat changed semantic output/archive')
    write(output / 'compiled.json', compiled)
    write(output / 'compiled-resources.json', load(ROOT / 'build/x12-compiled-b/resources.json'))
    write(output / 'forward.json', load(ROOT / 'build/x12-forward/summary.json'))
    write(output / 'motion-witness.json', load(ROOT / 'build/x12-dynamic-scope.json'))
    suites = [
        ('tools/rend2/datasets', 'test_blind_views.py', 7),
        ('tools/rend2/bake_model', 'test_cpp_forward.py', 6),
        ('tools/rend2/all_stock', 'test_audit.py', 4),
        ('tools/rend2/scene_schedule', 'test_reports.py', 2),
        ('tools/rend2/stock_joint_fits', 'test_reports.py', 4)]
    tests = []
    import os
    env = dict(os.environ, JK_BAKE_FORWARD_EXE=str(ROOT / 'build/s06-gcc/lightrecon_bake_forward_tool.exe'))
    for directory, pattern, expected in suites:
        command = [sys.executable, '-X', 'utf8', '-m', 'unittest', 'discover', '-s', directory, '-p', pattern, '-v']
        p = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True, timeout=60)
        text = p.stdout + p.stderr
        match = re.search(r'Ran (\d+) tests?', text)
        if p.returncode or not match or int(match[1]) != expected or 'skipped' in text.lower():
            raise ValueError('Unperformed/failed test suite: ' + text)
        path = ROOT / 'build' / ('x12-' + pattern + '.log')
        path.write_text(text)
        tests.append(dict(command=command, passed=expected, log_sha256=sha(path)))
    architecture = audit()
    if not architecture['dependency_check_pass']:
        raise ValueError('Renderer dependency introduced')
    checks = dict(schema='org.openjk.stock-joint-checks.v1',
                  cpu=dict(passed=int(count[1]), failed=0, measurement=cpu,
                           rss_scope='CTest driver only; native map/fixture RSS measured separately'),
                  builds=dict(sp_release='Passed', mp_release='Passed', measurement=renderers,
                              rss_scope='Build driver only; not compiler process peaks'),
                  python=tests, architecture=architecture,
                  disabled_path=load(ROOT / 'build/x12-disabled-vs-input/report.json'),
                  native_log_sha256={p: sha(ROOT / p) for p in
                                     ('build/x12-cpu.log', 'build/x12-renderers-retry.log')},
                  initial_execution_issues=[
                      'First renderer build wrapper lacked cmake in PATH; no compiler started; retry passed',
                      'Initial all_stock test_reports.py pattern ran zero tests; corrected test_audit.py ran all four',
                      'Historical shader baseline still has 3870 difference lines; all 27 current programs compile and input-revision comparison has zero differences'],
                  unperformed=['New complex compiled discovery bridge', 'Private stock rebake',
                               'Accepted stock final heldout/source ablations: no supported source model',
                               'Runtime marker screenshots: no game launched'])
    write(output / 'checks.json', checks)
    files = sorted(p for p in (Path(__file__).parent / 'results').rglob('*') if p.is_file() and p.name != 'manifest.json')
    write(output / 'manifest.json', dict(schema='org.openjk.stock-joint-witness.v1',
          base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
          artifacts={p.relative_to(Path(__file__).parent / 'results').as_posix(): sha(p) for p in files},
          source_files={p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(Path(__file__).parent.glob('*.py'))},
          conclusion='Native fixed-XYZ compiler fitting works; no accepted original-stock source model'))


if __name__ == '__main__':
    seal()
