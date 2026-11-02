#!/usr/bin/env python3
"""Publish owned P03 numeric receipts, hashes and build verification. GPL-2.0-or-later."""
import argparse
import json
import os
import platform
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write


def freeze(run, output):
    manifest = json.loads((run/'manifest.json').read_text())
    if not manifest['repeat_semantics']:
        raise ValueError('Missing deterministic comparison')
    for name, digest in manifest['source_sha256'].items():
        if sha(ROOT/name) != digest:
            raise ValueError('Changed measured source: '+name)
    ctest = (ROOT/'build/p03-ctest.log').read_text()
    renderer = (ROOT/'build/p03-renderers.log').read_text()
    if '100% tests passed out of 37' not in ctest:
        raise ValueError('Missing complete CPU suite')
    if 'rdsp-rend2_x86_64.dll' not in renderer or 'rd-rend2_x86_64.dll' not in renderer:
        raise ValueError('Missing SP/MP build')
    output.mkdir(parents=True, exist_ok=False)
    for name in ('quality.json', 'performance.json', 'spatial.json', 'sg-recovery.json', 'quality.csv'):
        shutil.copyfile(run/name, output/name)
    shutil.copyfile(ROOT/'build/p03-octahedral.json', output/'octahedral.json')
    python_log = (ROOT/'build/p03-python-tests.log').read_text()
    if 'Ran 10 tests' not in python_log or 'OK' not in python_log or 'skipped=' in python_log:
        raise ValueError('P03 Python/atlas tests did not pass completely')
    checks = dict(ctest=dict(passed=37, failed=0, log_sha256=sha(ROOT/'build/p03-ctest.log')),
                  native_log=(run/'native-tests.log').read_text().strip(),
                  python=dict(passed=10, log_sha256=sha(ROOT/'build/p03-python-tests.log')),
                  renderers=dict(sp=True, mp=True, log_sha256=sha(ROOT/'build/p03-renderers.log')),
                  ddgi_tests=dict(passed=12, log_sha256=sha(ROOT/'build/p03-ddgi-tests.log')),
                  rbdoom_tests=dict(passed=13, log_sha256=sha(ROOT/'build/p03-rbdoom-tests.log')),
                  renderer_launched=False, hardware_gpu_cost_measured=False)
    write(output/'checks.json', checks)
    architecture = json.loads((ROOT/'build/p03-architecture.json').read_text())
    if not architecture['dependency_check_pass']:
        raise ValueError('Renderer dependency introduced')
    write(output/'architecture.json', architecture)
    files = ['tools/rend2/field_representation/'+name for name in ('freeze.py', 'test_benchmark.py', 'octahedral.py', 'test_octahedral.py')]
    files += ['shared/rd-rend2/lightrecon/CMakeLists.txt', 'tools/lightrecon_test/CMakeLists.txt',
              'tools/rend2/all_stock/architecture.py']
    manifest['source_sha256'].update({p: sha(ROOT/p) for p in files})
    manifest['reference_sources'] = {p: sha(ROOT/p) for p in (
        'tools/rend2/ddgi_research/sources.json', 'tools/rend2/rbdoom_research/sources.json',
        'tools/rend2/rbdoom_research/field_basis.py')}
    manifest['artifacts'] = {p.name: sha(p) for p in output.iterdir()}
    manifest['scope'] = 'Angular representation decision, not field generation/renderer integration/stock light recovery'
    manifest['environment'] = dict(os=platform.platform(), processor=platform.processor(),
                                   logical_processors=os.cpu_count(), python=sys.version,
                                   compiler='GCC 16.2.0 / C++11 Release; SP/MP MSVC 2019 Release')
    write(output/'manifest.json', manifest)


def verify(folder):
    manifest = json.loads((folder/'manifest.json').read_text())
    for name, digest in manifest['artifacts'].items():
        if sha(folder/name) != digest:
            raise ValueError('Changed public receipt: '+name)
    for name, digest in manifest['source_sha256'].items():
        if sha(ROOT/name) != digest:
            raise ValueError('Changed implementation: '+name)
    print('P03 source and numerical witness hashes verified')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--run', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--verify', type=Path)
    a = p.parse_args()
    if a.verify:
        verify(a.verify)
    elif a.run and a.output:
        freeze(a.run, a.output)
    else:
        p.error('Use --run/--output or --verify')
