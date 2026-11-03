#!/usr/bin/env python3
"""P05 frozen native geometric cache witness. No stock photons or Python light model.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import json
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.field_builder.run import execute


def semantic(x):
    result = json.loads(json.dumps(x))
    for key in ('cache_query_ns', 'raw_trace_ns', 'area_build_seconds'):
        result.pop(key)
    for row in result['scaling']:
        row.pop('seconds')
    return result


def main(args):
    args.out.mkdir(parents=True, exist_ok=False)
    core = ROOT / 'shared/rd-rend2'
    sources = sorted(set(list((core / 'lightrecon').glob('*.cpp')) +
                         list((core / 'lightrecon').glob('*.h')) +
                         list(core.glob('tr_lightrecon*')) +
                         [core / 'lightrecon/CMakeLists.txt',
                          ROOT / 'tools/lightrecon_test/lightrecon_shadow_cache_test.cpp',
                          ROOT / 'tools/lightrecon_test/lightrecon_api_test.cpp',
                          ROOT / 'tools/lightrecon_test/probe_fixtures.h',
                          ROOT / 'tools/lightrecon_test/CMakeLists.txt',
                          ROOT / 'tools/rend2/field_builder/run.py', Path(__file__)]))
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources if p.is_file()}
    write(args.out / 'protocol.json', dict(
        schema='org.openjk.shadow-cache-protocol.v1',
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        dirty_tree=True, sources=hashes, native_sha256=sha(args.test),
        compiler=subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
        environment=dict(os=platform.platform(), cpu=platform.processor(), python=platform.python_version()),
        method='sparse exact receiver/source identities; shared StaticVisibility BVH; separate X13 occupancy',
        geometry='owned walls, doorways, sky quad, curved patch, alpha unknown, mounting, moving model',
        operator='physical static visibility, not historical compiler trace replay',
        finite_source_reference='finite-thickness analytic slab intersection at 512x512 nodes, with 1024x1024 convergence check; separated X14-certified emitters',
        finite_source_rule=dict(area=16, refined_area=32, tube_sides=32, tube_segments=16,
                                refined_tube_sides=64, refined_tube_segments=32,
                                discrepancy_threshold=.02, forced_flag_test_threshold=0,
                                discrepancy_is_confidence_bound=False),
        acceptance=dict(false_clear=0, positive_opening=True, area_abs_error_max=.035,
                        tube_abs_error_max=.04, reference_resolution_difference_max=.003,
                        query_allocations=0),
        cache=dict(max_entries=100000, max_rays=4000000, max_work=128000000,
                   max_bytes=128*1024**2, source_batch_max=4096, scalar_storage=['float64', 'UNorm16'],
                   compact_bound=1/65535, query_fallback='Unavailable; no traces'),
        cases=['point', 'spot cone support', 'rectangle partial shadow', 'tube partial shadow',
               'sun', 'sky aperture/missing', 'thin wall', 'wall boundary', 'mount contact',
               'unknown alpha/translucent', 'curved/missing geometry', 'dynamic model',
               'open/closed bake scenario', 'same/different clusters', 'source-to-probe',
               'corruption/missing chunks', 'wrong map/generation', 'cancellation/work/memory',
               'RGB reuse', 'XYZ partial rebuild', 'quantization repeat'],
        source_scaling=dict(sources=[1, 8, 32], selected_receivers_per_source=32),
        process_limits=dict(seconds=180, bytes=512*1024**2),
        runtime_consumer=False, stock_physical_truth=None))
    runs, resources = [], []
    for label in ('first', 'repeat'):
        path = args.out / (label + '.json')
        resources.append(execute([args.test, path, args.out / (label + '.jklr')], args.out, label))
        runs.append(json.loads(path.read_text()))
    assert semantic(runs[0]) == semantic(runs[1]), 'Numerical nondeterminism'
    assert sha(args.out / 'first.jklr') == sha(args.out / 'repeat.jklr'), 'Dataset bytes differ'
    write(args.out / 'summary.json', dict(schema='org.openjk.shadow-cache-results.v1',
                                        semantic=semantic(runs[0]), resources=resources,
                                        semantic_identical=True, archive_identical=True))
    with (args.out / 'quality.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(['metric', 'value', 'scope'])
        for key, value in semantic(runs[0]).items():
            if not isinstance(value, (dict, list)):
                writer.writerow([key, value, 'controlled geometry; not physical source certification'])
    for p, h in hashes.items():
        assert sha(ROOT / p) == h, p
    write(args.out / 'manifest.json', dict(schema='org.openjk.shadow-cache-manifest.v1',
                                         sources=hashes, artifacts={p.name: sha(p) for p in args.out.iterdir() if p.is_file()},
                                         licensed_payloads=False))
    print(json.dumps(dict(semantic_identical=True, archive_identical=True, resources=resources)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--test', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_shadow_cache_test.exe')
    parser.add_argument('--compiler', default='g++')
    main(parser.parse_args())
