"""Trusted post-freeze answer matching. Never imported by the proposal runner.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import math
import os
from pathlib import Path

from tools.rend2.lightmap_patches.run import sha, write


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def coverage(candidates, sources, gate, threshold):
    kept = [c for c in candidates if c['shape_score'] is not None and c['shape_score'] <= threshold]
    distances = [min((math.dist(s['position'], c['position']) for c in kept), default=None) for s in sources]
    matched = sum(d is not None and d <= gate for d in distances)
    unmatched = sum(not any(math.dist(s['position'], c['position']) <= gate for s in sources) for c in kept)
    typed = []
    for source in sources:
        if source.get('kind') != 'spot':
            continue
        alternatives = [c for c in kept if c['source_kind'] == 1 and math.dist(source['position'], c['position']) <= gate]
        angles = []
        for c in alternatives:
            a, b = source['axis'], c['outward']
            n = math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
            if n > 0:
                angles.append(math.degrees(math.acos(max(-1., min(1., sum(x*y for x, y in zip(a, b))/n)))))
        typed.append(dict(position=source['position'], position_candidates=len(alternatives),
                          minimum_axis_error_degrees=min(angles, default=None)))
    return dict(candidates=len(candidates), score_gated_candidates=len(kept), covered=matched, sources=len(sources),
                recall=matched/len(sources) if sources else None, nearest_world=distances,
                unmatched_alternatives=unmatched, spot_axis_diagnostics=typed)


def evaluate(folder, suite, bridge, gi):
    frozen = read(folder/'frozen.json')
    if frozen['private_answers_read'] or frozen['heldout_targets_read']:
        raise ValueError('Proposal input boundary violated')
    jobs = frozen['jobs']; policy = frozen['policy']
    # Pin every child output before opening any source answer.
    reports = {}
    for job in jobs:
        path = folder/job['name']/(job['strategy']+'.json')
        if sha(path) != job['report_sha256']:
            raise ValueError('Frozen output changed')
        r = read(path)
        if (r['rays'] > policy['max_rays'] or r['forward_calls'] > policy['max_forward_calls'] or
                sum(a['nodes'] for a in r['areas']) > policy['max_nodes_per_area']):
            raise ValueError('Fixed budget cap exceeded')
        reports[job['name'], job['strategy']] = r
    cases = []
    for name in ('point_room', 'spot', 'two_colour'):
        answer_path = suite/'answers'/(name+'.q3map-recipe.json'); answer = read(answer_path)
        if reports[name, 'strict']['map_sha256'] != answer['compiled_bsp_sha256']:
            raise ValueError('Compiler answer identity mismatch')
        sources = [dict(position=s['position_world'], kind=s['type'], axis=s.get('axis_propagation', [0, 0, -1]))
                   for s in answer['ground_truth']['lights']]
        cases.append((name, [name], sources, sha(answer_path)))
    bridge_answer = bridge/'bridge/private/source-answer.json'
    sources = read(bridge_answer)['sources']
    for variant in ('direct', 'passive_bounce1'):
        recipe = read(bridge/'bridge'/variant/'recipe.json')
        if recipe['answer_sha256'] != sha(bridge_answer):
            raise ValueError('V02 private answer identity mismatch')
        names = ['v02-'+variant+'-'+str(i) for i in range(1, 9)]
        if any(reports[name, 'strict']['map_sha256'] != recipe['bsp_sha256'] for name in names):
            raise ValueError('V02 original geometry identity mismatch')
        cases.append(('v02-'+variant, names, sources, sha(bridge_answer)))
    gi_answer = gi/'private-answer.json'
    cases.append(('compiled_gi_only_region', ['compiled_gi_only_region'], read(gi_answer)['sources'], sha(gi_answer)))
    results = []
    for name, members, sources, answer_hash in cases:
        methods = {}
        for strategy in ('strict', 'strict_geometry', 'adaptive'):
            rs = [reports[member, strategy] for member in members]
            candidates = [c for r in rs for c in r['candidates']]
            metrics = coverage(candidates, sources, policy['matching_world'], policy['shape_score_gate'])
            metrics.update(rays=sum(r['rays'] for r in rs), forward_calls=sum(r['forward_calls'] for r in rs),
                           nodes=sum(a['nodes'] for r in rs for a in r['areas']),
                           physical_certifications=sum(c['existence_confidence'] is not None for c in candidates),
                           scope='Conditional candidate-set coverage; unmatched alternatives are not certified false historical lamps')
            methods[strategy] = metrics
        results.append(dict(name=name, methods=methods, answer_sha256=answer_hash))
    return dict(schema='org.openjk.adaptive-hidden-evaluation.v1', policy=policy, cases=results,
                frozen_manifest_sha256=sha(folder/'frozen.json'), sources=frozen['sources'],
                jobs=jobs, compiler_gi_validation=read(gi/'validation.json'),
                answers_opened_after_all_proposals_frozen=True, no_parameter_fit_or_heldout_prediction_claim=True,
                historical_source_certified=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--suite', type=Path, default=Path('build/q3map2-bake-verified'))
    p.add_argument('--bridge', type=Path, default=Path(os.environ['TEMP'])/'jk-lightrecon-v02-bridge-v2')
    p.add_argument('--gi', type=Path, default=Path('build/asr16/compiled-gi-final'))
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); result = evaluate(a.input, a.suite, a.bridge, a.gi); write(a.out, result)
    for case in result['cases']:
        print(case['name'], [(m, r['covered'], r['sources'], r['candidates'], r['unmatched_alternatives'])
                              for m, r in case['methods'].items()])
