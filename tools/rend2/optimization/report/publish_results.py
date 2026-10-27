#!/usr/bin/env python3
"""Publish completed scalar research evidence, never game assets or fitted lights.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def read(path): return json.loads(Path(path).read_text(encoding='utf-8'))
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--repeat', type=Path, required=True)
    parser.add_argument('--measurement', type=Path, required=True)
    parser.add_argument('--repeat-measurement', type=Path, required=True)
    parser.add_argument('--distance', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); root = Path(__file__).resolve().parents[4]
    data = read(args.run/'result.json'); again = read(args.repeat/'result.json')
    for path, expected in data['source_sha256'].items():
        if digest(root/path) != expected: raise ValueError('Witness source no longer current: '+path)
    for name in ('result.json', 'metrics.csv'):
        if (args.run/name).read_bytes() != (args.repeat/name).read_bytes():
            raise ValueError('Nondeterministic semantic benchmark: '+name)
    if data != again: raise ValueError('Repeated semantic JSON mismatch')
    for name in ('two_colour', 'door_occlusion'):
        metadata = data['datasets'][name]
        if metadata['geometry_rays'] <= 0 or min(metadata['partitions'].values()) < 3:
            raise ValueError('Empty compiler-backed multi-source/shadow stage')
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('result.json', 'metrics.csv', 'summary.json', 'tables.md', 'timings.json'):
        shutil.copyfile(args.run/name, args.output/name)
    distance = read(args.distance)
    distance_source = Path(__file__).with_name('distance_control.py')
    if digest(distance_source) != distance['source_sha256']: raise ValueError('Supplementary distance control source mismatch')
    shutil.copyfile(args.distance, args.output/'distance.json')
    resources = read(args.resources)
    resource_source = Path(__file__).with_name('resource_matrix.py')
    if digest(resource_source) != resources['source_sha256']: raise ValueError('Resource-matrix source mismatch')
    if len(resources['records']) != 11 or any(r['measurement']['exit_code'] for r in resources['records']):
        raise ValueError('Incomplete per-method process resource measurements')
    shutil.copyfile(args.resources, args.output/'resources.json')
    measurement, repeat = read(args.measurement), read(args.repeat_measurement)
    if measurement['exit_code'] or repeat['exit_code']: raise ValueError('Unsuccessful measured run')
    validation = dict(schema='org.openjk.optimization-validation.v1', date='2026-10-27',
        semantic_repeat={name: digest(args.run/name) for name in ('result.json', 'metrics.csv')},
        source_hashes_current=True, measurement=measurement, repeat_measurement=repeat,
        supplemental_distance_records=len(distance['records']),
        supplemental_distance_source_sha256=digest(distance_source),
        per_method_resources_sha256=digest(args.resources),
        per_method_resource_source_sha256=digest(resource_source),
        tests=dict(optimization=30, evaluation=39, shadow_footprint=27, inverse_gi=15, cpu_ctest=17),
        original_compiler_checks=dict(cases=['inverse_square', 'linear', 'spot', 'colour_clip'],
                                     passed=True, new_bakes=False),
        real_archive_visibility={name: data['datasets'][name] for name in ('two_colour', 'door_occlusion')},
        postprocessor_sha256=digest(__file__),
        numerical_confidence='No source-existence posterior or physical reconstruction claim',
        limits=['No production solver/runtime/GPU integration or new SP/MP build',
            'No stock profile or calibrated stock intensity inferred',
            'Dense library-internal linear matvec counts unavailable',
            'Tiny geometry/indirect/area controls do not validate arbitrary BSP fitting',
            'BLAS one-thread environment requested; cross-platform last-bit reproducibility not claimed'],
        discarded_intermediate_runs=[
            dict(path='build/optimization-benchmark/complete', reason='Floor-only adapter admitted no X02 wall receivers for two_colour/door_occlusion; corrected, tested, rerun'),
            dict(path='build/optimization-benchmark/repeat', reason='Source changed during run; start/end hash guard rejected it')],
        approval_event=dict(action='Recursive ACL inheritance change for isolated SciPy install',
            outcome='Rejected by automatic review; no ACL changed; numerical commands executed under package owner context'))
    (args.output/'validation.json').write_text(json.dumps(validation, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print('PASS: semantic repeat, current source hashes, nonempty compiled stages; published', args.output)


if __name__ == '__main__': main()
