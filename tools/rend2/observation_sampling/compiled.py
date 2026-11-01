#!/usr/bin/env python3
"""Trusted compiled-fixture controller. Every proposal uses production C++.
Only all-role GEOMETRY and TrainingView RGB enter the native sampler/provider.
Answers and evaluation targets are opened after proposals are frozen.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
from collections import Counter

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2/datasets'))
from prepare_blind import prepare
from solver_view import TrainingView, digest
from evaluation_view import EvaluationView
sys.path.insert(0, str(ROOT / 'tools/rend2/all_stock'))
from run import execute, write

FIXTURES = ('point_room', 'two_colour', 'spot', 'door_occlusion', 'wall_leak',
            'bounce_colour', 'bounce_colour_left', 'bounce_control', 'skylight')


def ident(value):
    return int(value[:16], 16) or 1


def protocol(view, budget, spots, geometry_filter=lambda r: True):
    geometry = sorted(view.receiver_geometry(), key=lambda x: x['id'])
    signals = sorted(view.observations(), key=lambda x: x['id'])
    geometry = [r for r in geometry if geometry_filter(r)]
    valid = {r['id'] for r in geometry}
    signals = [r for r in signals if r['id'] in valid]
    m = view._manifest  # controller-owned partition policy, not a hidden answer
    partition = hashlib.sha256(json.dumps({k: m.get(k) for k in
        ('block_world', 'gap_world', 'seed', 'partition_origin_world', 'footprint_filter_multiplier')},
        sort_keys=True).encode()).hexdigest()
    lines = [f'JKSAMPLE1 {m["map_sha256"]}', f'{partition} {budget} 4 {int(spots)}', str(len(geometry))]
    for r in geometry:
        role = ('train', 'validation', 'heldout', 'gap').index(r['role'])
        lines.append(' '.join(map(str, [ident(r['id']), r['surface'], ident(r['block']), role,
                                       *r['position'], *r['normal'], r['area'],
                                       r['footprint_radius'], int(r['encoding_known'])])))
    lines.append(str(len(signals)))
    for r in signals:
        lines.append(' '.join(map(str, [ident(r['id']), *r['raw'], int(bool(r['transfer'] and r['primaries']))])))
    return '\n'.join(lines) + '\n', geometry


def metrics(result, truth):
    sources = truth['ground_truth']['lights']
    output = {}
    for model in ('old', 'hierarchical'):
        candidates = [c for job in result['jobs'] for c in job[model]['candidates']]
        # Exact candidate duplicates across jobs are counted once for matching.
        seen = {}
        for c in candidates:
            seen[(tuple(c['position']), c['source_kind'])] = c
        candidates = list(seen.values())
        position = [s['position_world'] for s in sources if 'position_world' in s]
        radius = max((j['grid_radius_world'] for j in result['jobs']), default=0)
        nearest = [min((math.dist(p, c['position']) for c in candidates), default=None) for p in position]
        # Maximum-cardinality spatial matching: one proposal cannot recall two
        # nearby true lamps. This is candidate coverage, not source existence.
        def matching_count(tolerance):
            assigned = {}
            def augment(source, visited):
                choices = sorted((math.dist(position[source], c['position']), i)
                                 for i, c in enumerate(candidates))
                for distance, i in choices:
                    if distance > tolerance or i in visited:
                        continue
                    visited.add(i)
                    if i not in assigned or augment(assigned[i], visited):
                        assigned[i] = source
                        return True
                return False
            for source in range(len(position)):
                augment(source, set())
            return len(assigned)
        matched = matching_count(radius)
        output[model] = dict(candidates=len(candidates), source_count=len(position),
                             nearest_world=nearest, declared_grid_radius_world=radius,
                             recall_at_grid_radius=(matched / len(position)
                                                    if position else None),
                             recall_at_quarter_grid_radius=(matching_count(radius/4) / len(position)
                                                            if position else None),
                             unmatched_proposals=len(candidates) - matched,
                             false_source_risk='unmatched proposal hypotheses, not published false physical emitters',
                             physical_emitters_published=0,
                             rays=sum(j[model]['rays'] for j in result['jobs']))
    return output


def run(tool, fixtures, answers, output, budgets=(16, 48)):
    output.mkdir(parents=True, exist_ok=False)
    cases = []
    for name in FIXTURES:
        source_name = 'bounce_colour' if name == 'bounce_colour_left' else name
        prior = json.loads((fixtures / (source_name + '-views/inputs/manifest.json')).read_text())
        # Same existing manifest's block, gap and seed; no fold tuning for a pass.
        views = output / (name + '-views')
        prepare(fixtures / source_name, views, prior['block_world'], prior['gap_world'], prior['seed'],
                tuple(prior.get('partition_origin_world', (0., 0., 0.))), publish_geometry=True)
        view = TrainingView(views / 'inputs', prior['map_sha256'])
        for budget in budgets:
            folder = output / (name + '-' + str(budget)); folder.mkdir()
            geometry_filter = (lambda r: r['position'][0] < -32) if name == 'bounce_colour_left' else (lambda r: True)
            text, public = protocol(view, budget, name == 'spot', geometry_filter)
            request = folder / 'training.request'; request.write_text(text, encoding='ascii')
            native = execute([tool, view.geometry_path, request], folder, 'native', seconds=180, memory=512 * 1024**2)
            if native['exit_code']:
                result = dict(state='Unavailable', resources=native,
                              reason=(folder / 'native.log').read_text(errors='replace')[-2000:])
            else:
                result = json.loads((folder / 'native.log').read_text())
                result['resources'] = native
                # Private answers opened AFTER native completion; source XYZ never in request.
                truth = json.loads((answers / (source_name + '.q3map-recipe.json')).read_text())
                if truth['compiled_bsp_sha256'] != prior['map_sha256']:
                    raise ValueError('Fixture answer/BSP identity mismatch')
                result['trusted_source_metrics'] = metrics(result, truth)
                result['bounces'] = truth['ground_truth']['bounce_iterations']
                result['answer_sha256'] = digest(answers / (source_name + '.q3map-recipe.json'))
                result['local_source_truth'] = ([s for s in truth['ground_truth']['lights']
                                                 if s['position_world'][0] < -32]
                                                if name == 'bounce_colour_left' else None)
                if name == 'bounce_colour_left':
                    result['no_local_source_counterexample'] = dict(
                        selector='predeclared receiver x < -32 WU; no answer used for selection',
                        true_sources_inside=0,
                        proposed_inside={model: sum(c['position'][0] < -32 for j in result['jobs']
                                                    for c in j[model]['candidates'])
                                         for model in ('old', 'hierarchical')},
                        interpretation='proposal multiplicity/transport ambiguity, not verified local emitters')
                evaluation = EvaluationView(views / 'evaluator', prior['map_sha256'], permitted=('validation', 'heldout'))
                signal = {}
                for role in ('validation', 'heldout'):
                    rows = [r for r in evaluation.observations(role) if geometry_filter(r)]
                    frozen_ids = {v for job in result['jobs'] for v in job[role+'_geometry_ids']}
                    selected = [r for r in rows if ident(r['id']) in frozen_ids]
                    signal[role] = dict(receivers=len(rows), surfaces=len({r['surface'] for r in rows}),
                                        positive=sum(any(0 < c < 255 for c in r['raw']) for r in rows),
                                        censored=sum(any(c == 255 for c in r['raw']) for r in rows),
                                        black_interval_mse=sum(sum(max(0, c)**2 for c in r['raw'])/3 for r in rows)/len(rows) if rows else None,
                                        frozen_selected_receivers=len(selected),
                                        frozen_selected_positive=sum(any(0 < c < 255 for c in r['raw']) for r in selected),
                                        frozen_selected_surfaces=len({r['surface'] for r in selected}))
                result['nonvacuous_independent_signal'] = all(v['frozen_selected_positive'] >= 4
                                                             for v in signal.values())
                result['independent_evaluation_signal'] = signal
            result.update(name=name, budget=budget, bsp_sha256=prior['map_sha256'],
                          compiler_truth='known controlled recipe only; not stock Raven',
                          request_sha256=digest(request), view_manifest_sha256=digest(views / 'inputs/manifest.json'))
            write(folder / 'result.json', result); cases.append(result)
            print(name, budget, result.get('trusted_source_metrics', {}), flush=True)
    write(output / 'results.json', dict(schema='org.openjk.sampling-compiled-x10.v1', cases=cases,
                                       native_sha256=digest(tool), physical_recovery_claim=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_hidden_tool.exe')
    p.add_argument('--fixtures', type=Path, default=ROOT / 'build/x02/compiled-fixtures-frozen')
    p.add_argument('--answers', type=Path, default=ROOT / 'build/q3map2-bake-verified/answers')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.tool.resolve(), a.fixtures, a.answers, a.output)
