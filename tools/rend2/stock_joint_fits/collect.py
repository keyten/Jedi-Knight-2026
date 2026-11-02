#!/usr/bin/env python3
"""Publish aggregate X12 diagnostics; keep stock geometry and RGB private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.scene_schedule.collect import semantic


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def measurement(value):
    return {k: v for k, v in value.items() if k != 'command'}


def rejection(reason):
    if not reason:
        return 'Numerical baseline completed'
    if 'centre/path:' in reason:
        return reason.split('centre/path:')[1].split(' ')[0]
    if 'Coarse observation budget' in reason:
        return 'InvalidCoarseBlockBudget'
    if 'comparable training coverage' in reason:
        return 'UnavailableComparableRows'
    if 'Unsupported factor operation' in reason:
        return 'UnsupportedForwardCapability'
    return reason


def summarize(private, revision, name, case):
    folder = private / ('runs-' + revision) / name.replace('/', '_')
    result = dict(map=name, bsp_sha256=case['bsp_sha256'], native_sha256=case['native_sha256'],
                  resources=measurement(case['resources']), physical_source_truth='Unavailable',
                  historical_profile='UnknownProfile', absolute_scale='Unknown')
    for filename in ('census', 'geometry', 'folds', 'transport', 'ray-summary', 'sampling'):
        value = load(folder / (filename + '.json'))
        if filename == 'sampling':
            value = {k: v for k, v in value.items() if k not in ('jobs', 'selection_seconds')}
        if filename == 'folds':
            for row in value['folds']:
                # X10 changed Selected.weight to dimensionless correlation mass.
                # Preserve raw witness hash, correct its old diagnostic label here.
                if 'represented_stratum_area' in row:
                    row['statistical_weight_mass'] = row.pop('represented_stratum_area')
            value['weight_note'] = 'Dimensionless correlation mass; never physical reflector area'
        result[filename.replace('-', '_')] = value
    result['profiles'] = {}
    for profile in ('isq', 'linear'):
        report = case[profile]
        controller = load(folder / ('scheduler-' + profile + '-controller.json'))
        fit = load(folder / (profile + '.json'))
        units = report['units']
        branches = report['branches']
        reasons = Counter(rejection(a['reason']) for b in branches for a in b['attempts'])
        placements = []
        with (folder / (profile + '-candidate-placement.tsv')).open() as stream:
            for row in csv.DictReader(stream, delimiter='\t'):
                placements.append({k: row[k] for k in ('id', 'provider', 'kind', 'placement_state')})
        result['profiles'][profile] = dict(
            status=report['status'], graph_key=report['graph_key'],
            local_units=len(units), unit_statuses=dict(Counter(u['status'] for u in units)),
            units_proposing=sum(u['candidates'] > 0 for u in units),
            generated_candidate_records=sum(u['candidates'] for u in units),
            complete_units=sum(u['complete'] for u in units),
            work={k: report[k] for k in ('local_calls', 'joint_calls', 'rays', 'work', 'forward_calls',
                  'fitting_evaluations', 'fitting_models', 'accounted_bytes', 'candidate_inputs_deferred',
                  'candidates_unsupported', 'influence_links', 'influence_complete')},
            local_coverage_complete=report['local_coverage_complete'],
            candidate_coverage_complete=report['candidate_coverage_complete'],
            branches=branches, rejection_counts=dict(reasons), controller=controller,
            completed_nonempty_attempts=sum(a['sources'] > 0 and a['status'] in ('Converged', 'Partial')
                                            for b in branches for a in b['attempts']),
            accepted_sources=fit['sources'], latest_wave_placement_witnesses=placements,
            latest_wave_only=True, shadow_gradients=fit['shadow_observed_gradients'],
            possible_shadow_edges=fit['shadow_candidates'],
            preferred_model_available=any(b['preferred'] >= 0 for b in branches),
            heldout_interval_mse=fit['producer']['heldout_interval_mse'],
            stock_xyz_error=None, stock_count_error=None, photometric_si_error=None,
            gi_policy='Conditional direct-only; prepared partial transfer is not asserted zero',
            source_count_type_profile_ablations=('Unavailable: no accepted source model' if not fit['sources']
                                                else 'See native diagnostic'),
            strict_source_batches='Not separately instrumented; completion/coverage reported independently')
    debug = ('geometry.json', 'folds.json', 'transport.json', 'fixture-rays.tsv',
             'transport.obj', 'transport-patches.tsv', 'observation-transport.tsv',
             'isq-candidate-placement.tsv', 'linear-candidate-placement.tsv',
             'scheduler-isq.json', 'scheduler-linear.json', 'scheduler-isq-units.tsv',
             'scheduler-linear-units.tsv', 'selected-geometry.tsv')
    result['private_debug_sha256'] = {f: sha(folder / f) for f in debug if (folder / f).exists()}
    return result


def collect(private, revision, repeat, output):
    raw = load(private / ('runs-' + revision + '.json'))
    repeated = load(private / ('runs-' + repeat + '.json')) if repeat else {}
    result = dict(schema='org.openjk.stock-joint-results.v1', run=revision,
                  identity=load(private / ('identity-' + revision + '.json')),
                  maps=[summarize(private, revision, name, case) for name, case in raw.items()],
                  repeat_maps=[], numerical_success_is_physical_truth=False,
                  budget_scope='Bounded whole-map attempts; not exhaustive all-luxel reconstruction')
    result['accepted_stock_models'] = sum(p['preferred_model_available'] for m in result['maps']
                                         for p in m['profiles'].values())
    result['physical_emitters_published'] = sum(len(p['accepted_sources']) for m in result['maps']
                                              for p in m['profiles'].values()
                                              if p['controller']['canonical_asset_available'])
    for name, case in repeated.items():
        if semantic(raw[name]) != semantic(case):
            raise ValueError('Native semantic repeat mismatch: ' + name)
        result['repeat_maps'].append(dict(map=name, semantic_equal=True,
                                         resources=measurement(case['resources'])))
    # Fail collection rather than manufacture a successful report on a crashed child.
    if any(m['resources']['exit_code'] for m in result['maps']):
        raise ValueError('Native child failed; retain the private attempt and diagnose it')
    output.parent.mkdir(parents=True, exist_ok=True)
    write(output, result)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--revision', required=True)
    p.add_argument('--repeat')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    collect(a.private, a.revision, a.repeat, a.out)
