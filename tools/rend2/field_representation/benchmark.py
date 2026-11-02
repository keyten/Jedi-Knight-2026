#!/usr/bin/env python3
"""P03 native angular encoders, independent geometry support and frozen witnesses.
Standard library + existing owned geometry routines; GPL-2.0-or-later.
"""
import argparse
import csv
import itertools
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
for folder in ('ddgi_research', 'shadow_footprint'):
    sys.path.insert(0, str(ROOT / 'tools/rend2' / folder))
from integrate import Scene
from geometry import Triangle
from probe_weights import exact_weights, average


def unit(a):
    d = math.sqrt(sum(x*x for x in a))
    return [x/d for x in a]


def angle(a, b):
    return math.degrees(math.acos(max(-1., min(1., sum(x*y for x, y in zip(unit(a), unit(b)))))))


def spatial():
    """Exact triangle segments. Scalar constant fields isolate spatial vs angular error."""
    def wall(low=-10., high=10., optical='opaque'):
        v = ((1., low, -2.), (1., high, -2.), (1., high, 3.), (1., low, 3.))
        return tuple(Triangle('wall'+str(low)+str(i), i+1, tuple(v[j] for j in ix),
                              ((0., 0.), (1., 0.), (0., 1.)), ((1., 0., 0.),)*3, optical)
                     for i, ix in enumerate(((0, 1, 2), (0, 2, 3))))
    query = (1.5, 0., 1.)
    probes = ((0., 0., 1.), (2., 0., 1.))
    scenes = dict(sealed=Scene(wall()), doorway=Scene(wall(high=-.2)+wall(low=.2)),
                  alpha_unknown=Scene(wall(optical='alpha_unknown')),
                  missing_geometry=Scene(unbounded_unknown=True))
    rows = []
    for name, scene in scenes.items():
        for degree in (1, 2, 4, 8):
            weights, unknown = exact_weights(scene, query, probes)
            value = average((100., 0.), weights)
            rows.append(dict(scene=name, degree=degree, naive=50.,
                             supported_value=value,
                             status='Unknown' if unknown else 'Available' if value is not None else 'Unavailable',
                             segment_weights=weights,
                             field_physical_truth_known=False,
                             blocked_support_leak=50. if name == 'sealed' else None,
                             strict_blocked_support_leak=0. if name == 'sealed' else None))
    weights, unknown = exact_weights(scenes['sealed'], query, ((0., -.1, 1.), (0., .1, 1.)))
    return dict(rows=rows, no_admissible_support=dict(value=average((100., 100.), weights),
                                                    unknown=unknown, status='Unavailable'),
                semantics='Support selection, not an inverse emitter oracle; Unknown never becomes certified clear. '
                          'Different computational region IDs do not override a certified doorway segment.')


def sg_recovery(rows):
    """Evaluation-only truth; never passed to native fitting."""
    axes = [unit(a) for a in ((.2, .1, 1.), (-.9, .4, .1), (.2, -.9, .2))]
    result = []
    for row in rows:
        if row['scene'] != 'previous_three_coloured_lobes' or not row['representation'].startswith('SG'):
            continue
        lobes = [x for x in row['lobes'] if x['sharpness'] > 0]
        if len(lobes) < 3:
            result.append(dict(representation=row['representation'], matching='InsufficientLobes'))
            continue
        match = min(itertools.permutations(range(len(lobes)), 3),
                    key=lambda p: sum(angle(a, lobes[j]['axis'])**2 for a, j in zip(axes, p)))
        result.append(dict(representation=row['representation'], matching='EvaluationOnly',
                           angle_degrees=[angle(a, lobes[j]['axis']) for a, j in zip(axes, match)],
                           sharpness_error=[lobes[j]['sharpness']-32 for j in match],
                           extra_lobes=len(lobes)-3))
    return result


def validate(report):
    rows = report['rows']
    required = {'SH_L1', 'SH_L2', 'SH_L3', 'SH_L4', 'SH_L8', 'SG_1', 'SG_3', 'SG_6', 'SG_3_SH_L2', 'Explicit_SH_L2'}
    if set(x['representation'] for x in rows) != required or len(set(x['scene'] for x in rows)) != 18:
        raise ValueError('Missing representation/scene')
    if not report['timing_sink_finite']:
        raise ValueError('Nonfinite native evaluation')
    groups = {}
    for row in rows:
        groups.setdefault(row['scene'], {})[row['representation']] = row
        if any(not math.isfinite(v) for q in ('irradiance', 'radiance') if row[q]
               for v in row[q].values() if isinstance(v, float)):
            raise ValueError('Nonfinite metric')
        if row['fit']['budget_limited']:
            raise ValueError('Frozen default unexpectedly exhausted work budget')
    for scene, group in groups.items():
        if abs(group['SH_L2']['irradiance']['rgb_rmse']-group['SH_L3']['irradiance']['rgb_rmse']) > 1e-10:
            raise ValueError('Odd-order cosine null space broken: '+scene)
    # Independent predeclared positive controls; no universal quality pass threshold.
    control = groups['previous_three_coloured_lobes']
    if control['SH_L2']['irradiance']['relative_rms'] >= .06 or control['SH_L4']['irradiance']['relative_rms'] >= .02:
        raise ValueError('Existing analytical reference regression')
    if control['SG_3']['radiance']['relative_rms'] >= .15:
        raise ValueError('Fitted unknown-axis SG did not capture the owned three-lobe control')
    for scene in ('coloured_local_points', 'ideal_parallel_delta'):
        group = groups[scene]
        if group['SH_L2']['irradiance']['relative_rms'] <= .005:
            raise ValueError('Delta SH control became vacuous')
        if group['Explicit_SH_L2']['irradiance']['relative_rms'] >= .001:
            raise ValueError('Given exact explicit geometry does not preserve delta contribution')
        if group['SH_L2']['radiance'] is not None:
            raise ValueError('Pointwise delta radiance must be NotComparable')
    return groups


def run(tool, test_tool, output):
    output.mkdir(parents=True, exist_ok=False)
    resources = []
    r = execute([test_tool], output, 'native-tests', seconds=120, memory=256*1024**2)
    if r['exit_code'] or r['state'] != 'Completed':
        raise ValueError('CPU reference tests failed')
    resources.append(r)
    reports = []
    timings = []
    for suffix in ('first', 'repeat'):
        path = output / (suffix+'.json')
        r = execute([tool, path], output, suffix, seconds=900, memory=512*1024**2)
        if r['exit_code'] or r['state'] != 'Completed':
            raise ValueError('Angular benchmark failed: '+str(r))
        resources.append(r)
        report = json.loads(path.read_text())
        validate(report)
        timing = []
        for row in report['rows']:
            timing.append(dict(scene=row['scene'], representation=row['representation'], **row.pop('timing')))
        reports.append(report)
        timings.append(timing)
    if reports[0] != reports[1]:
        raise ValueError('Angular semantic outputs changed across identical native runs')
    report = reports[0]
    write(output / 'quality.json', report)
    write(output / 'performance.json', dict(resources=resources, timings=timings))
    write(output / 'spatial.json', spatial())
    write(output / 'sg-recovery.json', sg_recovery(report['rows']))
    with (output / 'quality.csv').open('w', newline='', encoding='utf-8') as stream:
        w = csv.writer(stream)
        w.writerow(['scene', 'representation', 'radiance_relative_rms', 'irradiance_relative_rms',
                    'negative_radiance_fraction', 'energy_relative_bias', 'fp32_bytes'])
        for row in report['rows']:
            w.writerow([row['scene'], row['representation'], row['radiance']['relative_rms'] if row['radiance'] else 'NotComparable',
                        row['irradiance']['relative_rms'], row['radiance']['negative_fraction'] if row['radiance'] else 'NotComparable',
                        row['irradiance']['energy_vector_relative_bias'], row['quantization'][0]['bytes']])
    source_files = ['shared/rd-rend2/lightrecon/field_basis.'+ext for ext in ('h', 'cpp')]
    source_files += ['tools/lightrecon_test/lightrecon_field_basis_'+name+'.cpp' for name in ('test', 'tool')]
    source_files += ['tools/rend2/field_representation/benchmark.py']
    write(output / 'manifest.json', dict(schema='org.openjk.field-representation-manifest.v1',
          base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
          source_sha256={p: sha(ROOT/p) for p in source_files},
          executable_sha256=sha(tool), repeat_semantics=True,
          input='Owned analytic angular/geometry fixtures, no JA pixels, no stock calibration',
          field_registration=False, renderer_changes=False,
          limits=dict(native_seconds=900, process_bytes=512*1024**2, max_sg_kernel_evaluations=100000000),
          artifacts={p.name: sha(p) for p in output.iterdir() if p.suffix in ('.json', '.csv') and p.name not in ('first.json', 'repeat.json', 'manifest.json')}))
    print('P03:', len(report['rows']), 'quality rows; repeated numerical semantics identical')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--test-tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.tool.resolve(), args.test_tool.resolve(), args.output.resolve())
