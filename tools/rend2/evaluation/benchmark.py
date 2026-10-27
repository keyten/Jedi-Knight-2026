#!/usr/bin/env python3
"""Trusted offline metric runner. Evaluates frozen predictions; never runs a solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import sys
import time

import metrics as m
from adapters import (load, digest, policy, analytical_answer, compiler_answer,
                      dataset_readiness, compiler_baselines, boundary_diagnostic, prior_profile_diagnostics)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SECTIONS = ('input_completeness', 'geometric_visibility', 'photometric_comparability',
            'profile_identification', 'source_accuracy', 'prediction_accuracy',
            'attribution', 'shadow_accuracy', 'uncertainty', 'performance_reproducibility')


def write(path, obj):
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')


def apply_prediction(case, prediction):
    if prediction['map_sha256'] != case['provenance']['map_sha256']: raise ValueError('Cross-map prediction')
    if set(prediction['outputs'])-{'predictions', 'surface_predictions', 'grid_predictions', 'solver_counters'}:
        raise ValueError('Prediction attempted to replace evaluator truth, policy or metadata')
    case.update(prediction['outputs'])
    return case


def evaluate(case):
    if case.get('schema') != 'org.openjk.lighting-evaluation-case.v1': raise ValueError('Case schema')
    truth, predictions = case.get('truth'), case.get('predictions', [])
    # A candidate/hypothesis is not a resolved physical source. Evaluate such hypotheses separately.
    resolved = [p for p in predictions if p.get('state', 'Resolved') == 'Resolved']
    report = {name: m.metric(reason='No evidence supplied for this evaluation layer') for name in SECTIONS}
    report['input_completeness'] = m.readiness(case.get('observations', []), case.get('receiver_area'))
    report['geometric_visibility'] = dict(rays=m.visibility_metrics(case.get('visibility', [])),
                                        placement=m.placement(case.get('placement', [])))
    if truth is not None:
        detected = m.detection(truth, resolved, case['match_policy'], case['units_per_metre'])
        report['source_accuracy'] = detected
        physical_truth = [s for s in truth if s['kind'] not in ('sky', 'transport')]
        physical_pred = [s for s in resolved if s['kind'] not in ('sky', 'transport')]
        matched = {r['prediction_index']: r['truth_index'] for r in detected['matches']}
        existence = []
        for j, p in enumerate(physical_pred):
            claim = p.get('existence')
            if not claim: continue
            label = int(j in matched)
            if any(t.get('identifiability') == 'ambiguous' and m.gate(t, p, case['match_policy'], True) is not None
                   for t in physical_truth): label = None
            existence.append(dict(claim, label=label))
        report['uncertainty'] = dict(existence=m.probability_calibration(existence),
            position_intervals=m.interval_coverage(case.get('position_intervals', [])),
            intensity_intervals=m.interval_coverage(case.get('intensity_intervals', [])),
            joint_position=m.joint_position_coverage(case.get('joint_position_intervals', [])),
            type=m.type_calibration(case.get('type_confidence', [])),
            alternative_truth_coverage=m.mean([int(r['truth_in_alternatives']) for r in case.get('alternative_sets', [])]),
            unresolved_underconstrained=m.mean([int(not r['asserted_unique']) for r in case.get('underconstrained', [])]))
        report['photometric_comparability'] = dict(pairs=[r['photometry'] for r in detected['matches']],
            scene_scale_feasible=m.scale_feasible_set(case.get('training_scale_constraints', [])))
    report['source_accuracy']['candidate_count_not_recovered'] = len(predictions)-len(resolved)
    if case.get('profiles'):
        report['profile_identification'] = m.profile_evidence(case['profiles'], case['profile_regret'],
            case.get('known_profile'), case.get('selected_profile'), case.get('asserted_unique_profile', False))
    report['prediction_accuracy'] = dict(
        surface=m.prediction_metrics(case.get('surface_targets', []), case.get('surface_predictions', [])),
        grid=m.prediction_metrics(case.get('grid_targets', []), case.get('grid_predictions', [])),
        directions=m.directional_metrics(case.get('directions', [])),
        reconstruction_coverage=m.reconstruction_coverage(case.get('coverage_regions', [])),
        information=m.information(**case['information']) if case.get('information') else
            dict(rank=m.metric(reason='No source-specific response/Jacobian supplied')))
    report['prediction_accuracy']['ablations'] = m.ablation_pairs(case.get('ablation_runs', []))
    report['attribution'] = m.attribution(case.get('components', []))
    report['shadow_accuracy'] = m.shadow_metrics(case.get('shadow_edges', []), case.get('units_per_metre'))
    counters = case.get('solver_counters', {})
    report['performance_reproducibility'] = {key: m.metric(counters.get(key),
        reason=None if key in counters else 'Solver not executed / counter unavailable') for key in
        ('optimization_seconds', 'peak_rss_bytes', 'trace_count', 'iterations', 'function_evaluations', 'result_bytes')}
    report['performance_reproducibility']['seed'] = case.get('seed')
    report['performance_reproducibility']['provenance'] = case.get('provenance', {})
    return dict(id=case['id'], sections=report, no_composite_score=True,
                evaluation_claim='Metric/framework fixture; not a production solver result')


def flatten(value, prefix=''):
    if isinstance(value, dict):
        if 'status' in value and 'value' in value:
            yield (prefix, value['status'], value['value'], value.get('unit'), value.get('reason'),
                   value.get('numerator'), value.get('denominator'))
        else:
            for k, v in sorted(value.items()): yield from flatten(v, prefix+'/'+str(k))
    elif isinstance(value, list):
        for i, v in enumerate(value): yield from flatten(v, prefix+'/'+str(i))
    elif isinstance(value, (int, float)):
        yield (prefix, 'Available', value, None, None, None, None)


def render(output, result):
    write(output/'metrics.json', result)
    with (output/'metrics.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream); writer.writerow(('path', 'status', 'value', 'unit', 'reason', 'numerator', 'denominator'))
        for row in flatten(result): writer.writerow(row)
    lines = ['# Inverse-lighting evaluation dashboard', '',
        'Readiness, source identity, prediction and uncertainty are separate. No aggregate success score.', '',
        '| Case | TP / FP / FN | Source recall | Surface interval RMSE | Prediction area coverage |',
        '|---|---|---|---|---|']
    for c in result['cases']:
        s = c['sections']['source_accuracy']; p = c['sections']['prediction_accuracy']['surface']
        lines.append(f'| {c["id"]} | {s.get("true_positive", "?")} / {s.get("false_positive", "?")} / '
            f'{s.get("false_negative", "?")} | {s.get("recall", {}).get("value")} | '
            f'{p["rgb_interval_rmse"]["value"]} | {p["prediction_coverage"]["value"]} |')
    titles = ('Input completeness', 'Geometric visibility confidence', 'Photometric comparability',
              'Profile identification', 'Source reconstruction accuracy', 'Prediction accuracy',
              'Direct/indirect attribution', 'Shadow accuracy', 'Uncertainty calibration',
              'Performance and deterministic reproducibility')
    for name, title in zip(SECTIONS, titles):
        lines.extend(['', '## '+title, '', 'The following paths reference `metrics.json`; null is never zero error.', '',
                      '| Case / metric | Status | Value |', '|---|---|---|'])
        for c in result['cases']:
            for path, status, value, _, reason, _, _ in flatten(c['sections'][name]):
                lines.append(f'| {c["id"]}{path} | {status} | {value if value is not None else reason or "Unavailable"} |')
    lines.extend(['', '## Existing-data checks', '',
                  'See `existing_data` in JSON for source hashes, full census, compiled controls and boundary diagnostics.', '',
                  'No stock source-position accuracy, historical compiler recovery or deployment calibration is claimed.'])
    (output/'dashboard.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')


def compare_regression(current, previous, rules):
    """Only predeclared metric paths/budgets; never automatically bless lower RGB."""
    now = {p: (s, v) for p, s, v, *_ in flatten(current)}
    old = {p: (s, v) for p, s, v, *_ in flatten(previous)}
    failures = []
    for rule in rules:
        if rule['direction'] not in ('lower', 'higher') or m.finite(rule['allowed_delta']) < 0:
            raise ValueError('Invalid frozen regression rule')
        path = rule['path']; n, o = now.get(path), old.get(path)
        if not n or not o or n[0] != 'Available' or o[0] != 'Available':
            failures.append(dict(path=path, reason='Missing/incomparable metric')); continue
        delta = n[1]-o[1]
        if (rule['direction'] == 'lower' and delta > rule['allowed_delta']) or (
            rule['direction'] == 'higher' and -delta > rule['allowed_delta']):
            failures.append(dict(path=path, previous=o[1], current=n[1], reason='Frozen regression budget exceeded'))
    return dict(evaluated_rules=len(rules), failures=failures,
                status='Failed' if failures else 'Passed' if rules else 'Unavailable')


def example_cases():
    import copy
    base = load(HERE/'examples'/'controller_case.json')
    oracle = copy.deepcopy(base); oracle['id'] = 'source-oracle-sanity'
    oracle['predictions'] = list(reversed(copy.deepcopy(base['truth'])))
    for i, p in enumerate(oracle['predictions']): p['id'] = 'prediction-unrelated-'+str(i)
    cases = [oracle]
    # Same observation RGB, multiple co-located weak lights: perfect fit does not imply source recovery.
    many = copy.deepcopy(base); many['id'] = 'many-lights-perfect-rgb'
    many['predictions'] = []
    for source in base['truth']:
        for j in range(3):
            p = copy.deepcopy(source); p['id'] = f'arbitrary-{len(many["predictions"])}'
            if 'photometry' in p: p['photometry']['value'] /= 3
            p['existence'] = dict(score=.99, meaning='probability', calibration_protocol='Deliberate incorrect probability test')
            many['predictions'].append(p)
    cases.append(many)
    indirect = copy.deepcopy(base); indirect['id'] = 'indirect-only-perfect-rgb'
    indirect['predictions'] = [dict(kind='transport', component='unresolved')]
    indirect['components'] = [dict(truth=dict(direct=10., bounce=2.), prediction=dict(direct=0., bounce=12.))]
    cases.append(indirect)
    partial = copy.deepcopy(oracle); partial['id'] = 'one-easy-observation-only'
    partial['surface_predictions'] = partial['surface_predictions'][:1]
    partial['predictions'] = partial['predictions'][:1]
    cases.append(partial)
    empty = copy.deepcopy(base); empty['id'] = 'valid-empty-asset-no-recovery'
    empty['predictions'] = []; empty['surface_predictions'] = []
    cases.append(empty)
    candidate = copy.deepcopy(oracle); candidate['id'] = 'candidates-not-recovered'
    for s in candidate['predictions']: s['state'] = 'Candidate'
    cases.append(candidate)
    return cases


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--manifest', type=Path, help='Controller-only manifest: cases [{truth,prediction}] paths')
    p.add_argument('--analytical-answers', type=Path)
    p.add_argument('--compiled-root', type=Path)
    p.add_argument('--compiled-answers', type=Path)
    p.add_argument('--stock-root', type=Path)
    p.add_argument('--previous', type=Path); p.add_argument('--regression-policy', type=Path)
    args = p.parse_args(); start = time.perf_counter()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = example_cases() if not args.manifest else []
    provenance = {}
    if args.manifest:
        manifest = load(args.manifest); provenance['manifest_sha256'] = digest(args.manifest)
        if manifest['schema'] != 'org.openjk.lighting-evaluation-manifest.v1': raise ValueError('Manifest schema')
        for entry in manifest['cases']:
            case_path = args.manifest.parent/entry['truth']; pred_path = args.manifest.parent/entry['prediction']
            if digest(case_path) != entry['truth_sha256'] or digest(pred_path) != entry['prediction_sha256']:
                raise ValueError('Frozen case/prediction integrity mismatch')
            case = load(case_path); prediction = load(pred_path)
            cases.append(apply_prediction(case, prediction))
    existing = {}
    if args.analytical_answers:
        existing['analytical'] = {}
        for path in sorted(args.analytical_answers.glob('*.json')):
            if path.name.endswith('.q3map-recipe.json'): continue
            a = analytical_answer(path)
            import copy
            pred = list(reversed(copy.deepcopy(a['sources'])))
            for source in pred: source.pop('id', None)
            existing['analytical'][a['scene']] = dict(answer_sha256=a['answer_sha256'],
                oracle_source_sanity=m.detection(a['sources'], pred, policy(), a['units_per_metre']),
                caveat='Copy-answer matching assertion; no source or RGB fitting executed')
    if args.compiled_root:
        existing['compiled'] = {}
        for path in sorted(args.compiled_root.glob('*.source.json')):
            prefix = path.with_suffix('').with_suffix('')
            record = dict(readiness=dataset_readiness(prefix), baselines=compiler_baselines(prefix.parent/(prefix.name+'-views')))
            if args.compiled_answers:
                answer = args.compiled_answers/(prefix.name+'.q3map-recipe.json')
                if answer.exists():
                    a = compiler_answer(answer)
                    if a['bsp_sha256'] != record['readiness']['source']['map_sha256']: raise ValueError('Compiler answer BSP mismatch')
                    record['source_answer_metadata'] = a
            existing['compiled'][prefix.name] = record
    if args.stock_root:
        existing['stock'] = {name: dataset_readiness(args.stock_root/name) for name in ('hoth3', 'hoth2', 'mp_ffa3')}
    existing['q3map2_boundary_35'] = boundary_diagnostic(HERE.parent/'shadow_footprint/results/boundary.csv')
    existing['prior_x03_profile_metrics'] = prior_profile_diagnostics(HERE.parent/'bake_model/x03/witness.json')
    result = dict(schema=m.VERSION, cases=[evaluate(c) for c in cases], existing_data=existing,
        source_sha256={str(path.relative_to(ROOT)).replace('\\', '/'): digest(path) for path in
                       sorted(HERE.glob('*.py'))+[HERE/'examples/controller_case.json']},
        provenance=provenance, renderer_modified=False, solver_executed=False)
    if args.previous or args.regression_policy:
        if not args.previous or not args.regression_policy: raise ValueError('Both regression arguments required')
        result['regression'] = compare_regression(result, load(args.previous), load(args.regression_policy)['rules'])
        result['provenance']['regression_policy_sha256'] = digest(args.regression_policy)
        result['provenance']['previous_report_sha256'] = digest(args.previous)
    render(args.output, result)
    write(args.output/'measurement.json', dict(evaluator_wall_seconds=time.perf_counter()-start,
        python=platform.python_version(), command=sys.argv,
        note='Evaluator timing, not optimizer/build time. Use datasets/measure.py for actual process peak RSS.'))
    print('Evaluated', len(cases), 'metric examples;', len(existing.get('analytical', {})), 'analytical answers;',
          len(existing.get('compiled', {})), 'compiled datasets;', len(existing.get('stock', {})), 'stock censuses')
    if result.get('regression', {}).get('status') == 'Failed': return 1
    return 0


if __name__ == '__main__': sys.exit(main())
