#!/usr/bin/env python3
"""P01 native field-builder witness; no game assets, fitting or Python light formula.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import ctypes
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from measure import peak_memory


def cpu_seconds(child):
    if os.name != 'nt':
        return None
    values = [ctypes.c_uint64() for _ in range(4)]
    fn = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    fn.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
    if fn(int(child._handle), *(ctypes.byref(x) for x in values)):
        return (values[2].value + values[3].value) / 1e7
    return None


def execute(command, out, label):
    start, peak, state = time.perf_counter(), 0, 'Completed'
    with (out / (label + '.log')).open('w', encoding='utf-8') as stream:
        child = subprocess.Popen(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT)
        while child.poll() is None:
            peak = max(peak, peak_memory(child) or 0)
            if peak > 512 * 1024**2:
                state = 'ProcessMemoryBudget'
            elif time.perf_counter() - start > 180:
                state = 'ProcessTimeBudget'
            if state != 'Completed':
                child.terminate()  # Only this owned native child.
                child.wait(timeout=15)
                break
            time.sleep(.02)
        peak = max(peak, peak_memory(child) or 0)
        result = dict(state=state, exit_code=child.returncode,
                      wall_seconds=time.perf_counter() - start,
                      cpu_seconds=cpu_seconds(child), peak_rss_bytes=peak)
    write(out / (label + '-resources.json'), result)
    if state != 'Completed' or child.returncode:
        raise RuntimeError(f'{label}: {result}')
    return result


def main(args):
    args.out.mkdir(parents=True, exist_ok=False)
    core = ROOT / 'shared/rd-rend2'
    sources = sorted(set(list((core / 'lightrecon').glob('*.cpp')) +
                         list((core / 'lightrecon').glob('*.h')) +
                         list(core.glob('tr_lightrecon*')) +
                         [core / 'lightrecon/CMakeLists.txt',
                          ROOT / 'tools/lightrecon_test/lightrecon_field_builder_test.cpp',
                          ROOT / 'tools/lightrecon_test/probe_fixtures.h',
                          ROOT / 'tools/lightrecon_test/CMakeLists.txt', Path(__file__)]))
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources if p.is_file()}
    compiler = subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0]
    policy = dict(schema='org.openjk.field-builder-protocol.v1',
                  commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  dirty_tree=True, source_hashes=hashes, native_sha256=sha(args.test),
                  compiler=compiler, scalar_geometry='double; canonical world Vec3=float',
                  angular='P03 real orthonormal SH; receiver-to-light world directions',
                  source_profile='physical-emission/1; no historical Q conversion',
                  cases=['point', 'two-colour', 'spot', 'rectangle', 'tube', 'sky', 'sun+sky',
                         'passive-red-bounce', 'sealed-wall', 'doorway', 'partial-area',
                         'conditional-relative', 'scene-equivalent', 'appearance', 'failures'],
                  spatial_reference='60 independent positions x 30 normals; no missing-to-black conversion',
                  passive_reference='30 independent positions, upward normal, independent dense area quadrature',
                  default_options=dict(degree=2, angular_order=12, reference_order=20,
                                       max_relative_angular_rmse=.08, direct_projection=False,
                                       max_bytes=128*1024**2, max_rays=4000000, max_work=128000000),
                  owned_test_options=dict(degree=4, angular_order=8, reference_order=12,
                                         max_relative_angular_rmse=.08, area_divisions=4,
                                         tube_sides=12, tube_segments=4, partial_area_divisions=16,
                                         fine_passive_spacing_world=.5,
                                         sky_irradiance_rmse_budget=.2, sky_radiance_rmse_budget=.6),
                  gates='predeclared native CHECK assertions; direct-grid spatial error is diagnostic, not acceptance',
                  process_limits=dict(seconds=180, bytes=512*1024**2),
                  units='authored Lux/Nits; relative and encoded appearance use opt-in typed API',
                  stock_photometric_truth=None, runtime_consumer=False,
                  environment=dict(os=platform.platform(), cpu=platform.processor(),
                                   python=platform.python_version()))
    write(args.out / 'protocol.json', policy)
    semantics, resources = [], []
    for label in ('first', 'repeat'):
        output = args.out / (label + '.json')
        resources.append(execute([args.test, output, args.out / (label + '.jklr')], args.out, label))
        semantic = json.loads(output.read_text())
        for k in ('point_build_seconds', 'query_nanoseconds'):
            semantic.pop(k)
        semantics.append(semantic)
    if semantics[0] != semantics[1]:
        raise RuntimeError('Deterministic semantic comparison failed')
    if sha(args.out / 'first.jklr') != sha(args.out / 'repeat.jklr'):
        raise RuntimeError('Canonical dataset bytes differ')
    write(args.out / 'summary.json', dict(schema='org.openjk.field-builder-results.v1',
                                        semantic=semantics[0], resources=resources,
                                        repeat_identical=True, missing_is_zero=False))
    with (args.out / 'quality.csv').open('w', newline='', encoding='utf-8') as stream:
        w = csv.writer(stream)
        w.writerow(['metric', 'value', 'scope'])
        for k, v in semantics[0].items():
            w.writerow([k, v, 'owned controlled CPU fixtures; not stock recovery'])
    for p, h in hashes.items():
        if sha(ROOT / p) != h:
            raise RuntimeError(f'Source changed during frozen witness: {p}')
    write(args.out / 'manifest.json', dict(schema='org.openjk.field-builder-manifest.v1',
                                         sources=hashes, artifacts={p.name: sha(p) for p in args.out.iterdir() if p.is_file()},
                                         licensed_payloads=False))
    print(json.dumps(dict(repeat_identical=True, resources=resources)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--test', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_field_builder_test.exe')
    parser.add_argument('--compiler', default='g++')
    main(parser.parse_args())
