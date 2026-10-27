#!/usr/bin/env python3
"""Run independent numerical A--F lighting benchmarks, not a production solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import time
from collections import defaultdict

from environment import HERE, ROOT, np, scipy, versions
from problems import compiled22, compiled12
from experiments import (fixed_geometry, uncertain_profiles, continuous, shadow_experiments,
                         censored_and_robust, model_selection, indirect_blocks, colour_constraints, compiled_positions)
from controls import derivative_controls, scaling_schedules, sampling, numerical_geometry_controls


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')


def all_records(value):
    if isinstance(value, dict):
        if 'problem' in value and 'method' in value: yield value
        else:
            for v in value.values(): yield from all_records(v)
    elif isinstance(value, list):
        for v in value: yield from all_records(v)


def report(output, result, timings):
    write(output/'result.json', result)
    keys = ('problem', 'method', 'status', 'objective', 'position_error_metres', 'parameter_error', 'strict_heldout_rmse',
            'strict_heldout_interval_rmse', 'heldout_interval_rmse', 'heldout_rmse', 'validation_interval_rmse', 'validation_rmse', 'relative_amplitude_error', 'rank',
            'rank_relative_1e6', 'condition', 'forward', 'derivative', 'scalar_responses', 'rays', 'matvec')
    records = list(all_records(result['experiments']))
    with (output/'metrics.csv').open('w', encoding='utf-8', newline='') as file:
        writer = csv.DictWriter(file, keys); writer.writeheader()
        for row in records:
            values = dict(row); values.update(row.get('work', {})); writer.writerow({k: values.get(k) for k in keys})
    write(output/'timings.json', timings)
    groups = defaultdict(list)
    for r in records: groups[(r['problem'], r['method'])].append(r)
    timing_groups = defaultdict(list)
    for r in timings: timing_groups[(r['problem'], r['method'])].append(r['wall_seconds'])
    lines = ['# Numerical optimization benchmark', '',
        'Independent objectives, not a complete inverse solver. Kernel/compiler, physical SI and algebraic controls are separate.', '',
        'Medians below summarize starts/seeds/noise. Different parameter counts and different evaluator costs are not interchangeable.', '',
        '| Problem | Method | Runs | Held-out error median | Parameter error median | Forward median | Rays median | CPU seconds median |',
        '|---|---|---:|---:|---:|---:|---:|---:|']
    summary = []
    for (problem, method), rows in sorted(groups.items()):
        def median(keys):
            values = [next((r.get(k) for k in keys if r.get(k) is not None), None) for r in rows]
            values = [v for v in values if v is not None]
            return float(np.median(values)) if values else None
        entry = dict(problem=problem, method=method, runs=len(rows),
            heldout=median(('strict_heldout_interval_rmse', 'strict_heldout_rmse', 'heldout_interval_rmse', 'heldout_rmse', 'clean_heldout_rmse')),
            parameter_error=median(('position_error_metres', 'position_error', 'parameter_error', 'truth_error', 'relative_amplitude_error')),
            forward=float(np.median([r.get('work', {}).get('forward', 0) for r in rows])),
            rays=float(np.median([r.get('work', {}).get('rays', 0) for r in rows])),
            wall_seconds=float(np.median(timing_groups[(problem, method)])) if timing_groups[(problem, method)] else None,
            statuses={s: sum(r['status'] == s for r in rows) for s in sorted(set(r['status'] for r in rows))})
        summary.append(entry)
        lines.append('| '+' | '.join(str(entry[k]) for k in ('problem', 'method', 'runs', 'heldout', 'parameter_error', 'forward', 'rays', 'wall_seconds'))+' |')
    write(output/'summary.json', summary)
    (output/'tables.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--compiled22', type=Path)
    p.add_argument('--compiled12', type=Path)
    p.add_argument('--recipes12', type=Path)
    p.add_argument('--ray-tool', type=Path)
    p.add_argument('--quick', action='store_true')
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=False)
    source_paths = list(sorted(HERE.glob('*.py')))+[HERE/'requirements.txt', HERE/'dependencies.json',
        HERE.parent/'bake_model/bake_model.py', HERE.parent/'bake_model/hypotheses.py',
        HERE.parent/'shadow_footprint/analytic.py', HERE.parent/'shadow_footprint/integrate.py',
        HERE.parent/'evaluation/metrics.py']
    def hashes():
        return {str(path.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
    start_hashes = hashes()
    start = time.perf_counter(); datasets = []; metadata = {}; missing = []
    if args.compiled22:
        for name in ('inverse_square', 'linear', 'spot', 'colour_clip'):
            d = compiled22(args.compiled22, name); datasets.append(d); metadata[name] = d['metadata']
    else: missing.append('Locked 22-case compiler fixtures not supplied')
    if args.compiled12 and args.recipes12 and args.ray_tool:
        for name in ('two_colour', 'door_occlusion'):
            d = compiled12(args.compiled12, args.recipes12, name, args.ray_tool.resolve(), args.output/'rays')
            datasets.append(d); metadata[name] = d['metadata']
    else: missing.append('Multiple-source/shadow compiled archives or CPU ray tool not supplied')
    seeds, noises = ((11,), (0.,)) if args.quick else ((11, 37), (0., .5))
    timings = []; experiments = {}
    def stage(name, function):
        tick = time.perf_counter(); experiments[name] = function()
        print(name, 'completed seconds', round(time.perf_counter()-tick, 3), flush=True)
    stage('A_fixed_geometry', lambda: fixed_geometry(datasets, seeds, timings))
    stage('B_discrete_profiles', lambda: uncertain_profiles(datasets, timings, 400 if args.quick else 700))
    stage('C_continuous_geometry', lambda: continuous(seeds, noises, timings, 400 if args.quick else 700))
    stage('C_compiled_positions', lambda: compiled_positions(datasets, timings, 400 if args.quick else 700))
    stage('D_visibility', lambda: shadow_experiments(timings, 80 if args.quick else 180))
    stage('losses_censoring', lambda: censored_and_robust(timings))
    stage('colour_parameterization', lambda: colour_constraints(timings))
    stage('E_discrete_sources', lambda: model_selection(timings))
    stage('F_direct_indirect', indirect_blocks)
    stage('derivatives', derivative_controls)
    stage('scaling_schedules', lambda: scaling_schedules(timings))
    stage('sampling', lambda: sampling(timings))
    stage('stability_geometry', lambda: numerical_geometry_controls(timings))
    if hashes() != start_hashes: raise RuntimeError('Benchmark source changed while running; discard this run and repeat')
    result = dict(schema='org.openjk.optimization-benchmark.v1', versions=versions(), seeds=list(seeds),
        noises=list(noises), quick=args.quick, datasets=metadata, unavailable=missing,
        source_sha256=start_hashes,
        experiments=experiments, production_solver=False, renderer_modified=False,
        stock_profile='UnknownProfile', confidence_claim=None)
    report(args.output, result, timings)
    write(args.output/'measurement.json', dict(wall_seconds=time.perf_counter()-start, platform=platform.platform(),
        purpose='Benchmark process timing, not production reconstruction',
        nonlinear_work_budget='Forward + derivative calls counted; additional 200000 scalar/ray/projection cap per run',
        scipy_internal_linear_matvec='Unavailable, not fabricated; dimensions and wall time reported'))
    print('DONE records', len(list(all_records(experiments))), flush=True)


if __name__ == '__main__': main()
