#!/usr/bin/env python3
"""Freeze, execute and evaluate ASR02; truth is absent from proposal selection.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha
from tools.rend2.hidden_candidates.run import choose, request, semantic, TrainingView
from tools.rend2.idf.prototype import Oracle, response, search
from tools.rend2.evaluation.metrics import detection

HERE = Path(__file__).resolve().parent


def write(path, value):
    with Path(path).open('w', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, indent=2, sort_keys=True, allow_nan=False); f.write('\n')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def pin(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=sha(path))


def receiver_file(path, rows):
    with path.open('w', newline='\n') as f:
        f.write(str(len(rows))+'\n')
        for r in rows:
            f.write(' '.join(map(str, [r['surface'], *r['position']]))+'\n')


def passive_control(rows, oracle, policy):
    patch = policy['reflection_control']; b = patch['bounds']; nx, ny = patch['quadrature']
    area = (b[3]-b[0])*(b[4]-b[1])/nx/ny
    energy = [0.]*len(rows)
    for x, y in itertools.product(range(nx), range(ny)):
        p = [b[0]+(x+.5)*(b[3]-b[0])/nx, b[1]+(y+.5)*(b[4]-b[1])/ny, b[2]]
        # Fixed 1-WU visibility launch below the known ceiling, not unknown-as-empty.
        g = oracle.query([p[0], p[1], p[2]-1])
        if not g['free'] or any(v == 2 for v in g['states']):
            raise ValueError('Passive control authoring visibility unavailable')
        for i, row in enumerate(rows):
            d = math.dist(p, row['position'])
            cosine = max(0., (p[2]-row['position'][2])/d) if d else 0.
            if g['states'][i] == 0:
                energy[i] += patch['outgoing_radiance_codes']*area*cosine*response(row, p)
    return [dict(r, raw=[min(255, int(e))]*3) for r, e in zip(rows, energy)]


def freeze(a):
    policy = read(HERE/'policy-v3.json'); a.work.mkdir(parents=True, exist_ok=False)
    native = a.work/'native'; native.mkdir()
    tools = {}
    for label, source in [('geometry', a.geometry_tool), ('hidden', a.hidden_tool)]:
        target = native/source.name; shutil.copyfile(source, target); tools[label] = pin(target)
    p = dict(schema='org.openjk.idf-protocol/1', policy=pin(HERE/'policy-v3.json'), tools=tools,
             revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(), cases={},
             sources={f.relative_to(ROOT).as_posix(): sha(f) for f in sorted(HERE.iterdir()) if f.suffix in ('.py','.cpp')},
             dependencies={name: pin(ROOT/name) for name in ('tools/rend2/hidden_candidates/run.py',
                'tools/rend2/evaluation/metrics.py','tools/rend2/datasets/solver_view.py',
                'build/s06-gcc/core/librend2_lightrecon_core.a',
                'tools/rend2/aesthetic_baseline/results/v1/protocol.json',
                'docs/rend2-light-localization-research.md')},
             papers={name: pin(ROOT/'build/light-localization-research'/name) for name in ('zhang2018.pdf','zhang2018-supplement.pdf')})
    for name in policy['cases']:
        stock = name == 'hoth3_roi'; fixture = 'point_room' if name == 'reflected_patch' else name
        sky_aperture = name == 'sky_aperture'
        inputs = (ROOT/'build/x05/frozen-final/x05_sky_aperture_v1/views/inputs' if sky_aperture else
                  a.stock if stock else a.fixtures/(fixture+'-views/inputs'))
        if not inputs.exists():
            p['cases'][name] = dict(state='Blocked', reason='Predeclared input not mounted'); continue
        if not stock and not sky_aperture:
            from tools.rend2.datasets.prepare_blind import prepare
            view = a.work/(name+'-views')
            split = policy['split']
            prepare(a.fixtures/fixture,view,block=split['block_world'],gap=split['gap_world'],
                    seed=split['seed'],origin=split['origin'])
            inputs = view/'inputs'
        m = read(inputs/'manifest.json'); training = TrainingView(inputs, m['map_sha256'])
        all_rows = list(training.observations())
        rows = choose(training, (3320,700,123) if stock else None, 512, policy['samples'])
        if len(rows) < 6:
            raise ValueError('Frozen sample budget unavailable: '+name)
        bounds = (policy['hoth3_roi'] if stock else
                  [*[min(r['position'][i] for r in all_rows) for i in range(3)],
                   *[max(r['position'][i] for r in all_rows) for i in range(3)]])
        folder = a.work/name; folder.mkdir(); receiver_file(folder/'receivers.txt', rows)
        bounds_oracle = Oracle(tools['geometry']['path'], training.geometry_path, folder/'receivers.txt')
        if not stock: bounds = bounds_oracle.header['bounds']
        bounds_oracle.close()
        if name == 'reflected_patch':
            oracle = Oracle(tools['geometry']['path'], training.geometry_path, folder/'receivers.txt')
            try: rows = passive_control(rows, oracle, policy)
            finally: oracle.close()
        write(folder/'rows.json', rows)
        # Exactly one conditional inverse-square profile for both proposal methods.
        text = request(rows, m['map_sha256'], bounds, bounds, name in ('point_room','two_colour','spot','door_occlusion'))
        lines = text.splitlines(); lines[-2:] = ['1','0']
        with (folder/'hidden.request').open('w', newline='\n') as f: f.write('\n'.join(lines)+'\n')
        c = dict(state='Frozen', stock=stock, geometry=pin(training.geometry_path), bounds=bounds,
                 input_manifest=pin(inputs/'manifest.json'), train=pin(inputs/'train.jsonl'),
                 rows=pin(folder/'rows.json'), receivers=pin(folder/'receivers.txt'), hidden_request=pin(folder/'hidden.request'),
                 map_sha256=m['map_sha256'], partition_counts=m['counts'], selected_ids=[r['id'] for r in rows])
        c['sealed_evaluation_hashes'] = {role: pin(inputs.parent/'evaluator'/(role+'.jsonl')) for role in ('validation','heldout')}
        if sky_aperture:
            c['control_definition'] = pin(ROOT/'tools/rend2/evaluation/x05/fixtures.py')
            c['control_recipe'] = pin(ROOT/'build/x05/compiler-final/x05_sky_aperture_v1.recipe.json')
        if name not in ('hoth3_roi','reflected_patch','sky_aperture'):
            c['answer'] = pin(a.answers/(name+'.q3map-recipe.json'))
        p['cases'][name] = c
    write(a.work/'protocol.json', p)
    print('Protocol frozen before discovery:', a.work/'protocol.json', flush=True)


def select_hidden(d, policy):
    chosen = []
    pool = [c for c in d['candidates'] if c['source_kind'] == 0]
    for c in sorted(pool, key=lambda c: (c['shape_score'] is None, c['shape_score'] or 0., c['id'])):
        if all(math.dist(c['position'], x['position']) >= policy['proposal_separation_world'] for x in chosen):
            chosen.append(c)
        if len(chosen) == policy['proposal_cap']: break
    return chosen


def matches(truth, candidates, tolerance, typed=False):
    t = [dict(kind=s.get('kind','point') if typed else 'point', position=s['position']) for s in truth]
    c = [dict(kind='point',position=s['position']) for s in candidates]
    result = detection(t,c,dict(position_world=tolerance,angle_degrees=10,cone_degrees=10,
                               dimension_relative=.25,roll_degrees=15),1.)
    return {k: result[k] for k in ('true_positive','false_positive','false_negative','precision','recall','unmatched_truth')}


def evaluate(a):
    p = read(a.work/'protocol.json'); policy = read(p['policy']['path'])
    for item in [p['policy'],*p['tools'].values(),*p['dependencies'].values(),*p['papers'].values()]:
        if sha(item['path']) != item['sha256']: raise ValueError('Frozen dependency changed')
    for name, digest in p['sources'].items():
        if sha(ROOT/name) != digest: raise ValueError('Frozen source changed: '+name)
    for c in p['cases'].values():
        if c['state'] == 'Frozen':
            for item in [v for v in c.values() if isinstance(v,dict) and 'sha256' in v] + list(c['sealed_evaluation_hashes'].values()):
                if sha(item['path']) != item['sha256']: raise ValueError('Frozen case changed')
    a.out.mkdir(parents=True, exist_ok=False); results = {}; timings = []
    # Complete ALL blind proposals before opening ANY source answers.
    for name, c in p['cases'].items():
        if c['state'] != 'Frozen': results[name] = c; continue
        folder = a.work/name; rows = read(c['rows']['path']); outputs = {}
        for mode in policy['modes']:
            repeated = []
            for repeat in range(2):
                start = time.perf_counter(); oracle = Oracle(p['tools']['geometry']['path'],c['geometry']['path'],c['receivers']['path'])
                try: output = search(rows,c['bounds'],mode,policy,oracle)
                finally: oracle.close()
                timings.append(dict(case=name,mode=mode,repeat=repeat,seconds=time.perf_counter()-start))
                write(folder/(mode+'-'+str(repeat)+'.json'),output); repeated.append(output)
            if repeated[0] != repeated[1]: raise ValueError('IDF semantic repeat changed')
            outputs[mode] = {k:v for k,v in repeated[0].items() if k not in ('samples','cells')}
            outputs[mode].update(repeat_equal=True,semantic_sha256=sha(folder/(mode+'-0.json')))
        hidden = []
        for repeat in range(2):
            r = execute([p['tools']['hidden']['path'],c['geometry']['path'],c['hidden_request']['path']],folder,'hidden-'+str(repeat),seconds=180)
            timings.append(dict(case=name,mode='hidden',repeat=repeat,resources=r))
            if r['exit_code'] or r['state'] != 'Completed': raise RuntimeError('Native Hidden failed')
            hidden.append(read(folder/('hidden-'+str(repeat)+'.log')))
        if semantic(hidden[0]) != semantic(hidden[1]): raise ValueError('Hidden semantic repeat changed')
        d = hidden[0]; outputs['hidden'] = dict(candidates=select_hidden(d,policy),full_pool_count=len(d['candidates']),
                counters={k:d[k] for k in ('rays','forward_calls','work')},areas=d['areas'],
                diagnostics=d['diagnostics'],code=d['code'],repeat_equal=True,complete=d['search_complete'])
        results[name] = dict(state='Evaluated',methods=outputs,positive_training_rows=sum(max(r['raw'])>=8 for r in rows),
                             saturated_rows=sum(max(r['raw'])==255 for r in rows), selected_rows=len(rows))
        print(name, {n:len(v['candidates']) for n,v in outputs.items()},flush=True)
    for name, r in results.items():
        if r['state'] != 'Evaluated': continue
        c = p['cases'][name]
        if c['stock']:
            r['source_truth'] = 'Unavailable'; continue
        truth = []
        if 'answer' in c:
            answer = read(c['answer']['path'])
            if answer['compiled_bsp_sha256'] != c['map_sha256']: raise ValueError('Answer identity mismatch')
            truth = [dict(kind=s['type'],position=s['position_world']) for s in answer['ground_truth']['lights']]
        r['truth_count'] = len(truth)
        r['truth_source_ids'] = [c['map_sha256']+'/local-light/'+str(i) for i in range(len(truth))]
        pool = read(a.work/name/'hidden-0.log')['candidates']
        r['methods']['hidden']['full_pool_metrics_32'] = matches(truth,pool,32)
        r['methods']['hidden']['full_pool_nearest_world'] = [min((math.dist(s['position'],x['position']) for x in pool),default=None) for s in truth]
        for method, d in r['methods'].items():
            d['metrics'] = {str(t):matches(truth,d['candidates'],t) for t in policy['tolerances_world']}
            d['typed_metrics_32'] = matches(truth,d['candidates'],32,True)
            d['nearest_world'] = [min((math.dist(s['position'],x['position']) for x in d['candidates']),default=None) for s in truth]
    # Stock geometry/observations remain private. Counts and diagnostic IDs are exportable.
    for name, r in results.items():
        if p['cases'][name].get('stock') and r['state'] == 'Evaluated':
            for d in r['methods'].values():
                d['candidates'] = [dict(position_sha256=hashlib.sha256(str(c['position']).encode()).hexdigest()) for c in d['candidates']]
    positives = ('point_room','two_colour','door_occlusion')
    additions = {}
    for mode in policy['modes']:
        additions[mode] = sum(sum(d is not None and d<=32 and (h is None or h>32) for d,h in
            zip(results[n]['methods'][mode]['nearest_world'],results[n]['methods']['hidden']['nearest_world'])) for n in positives)
    controls = ('reflected_patch','skylight','sky_aperture')
    verdict = {}
    for mode in policy['modes']:
        recall = min(results[n]['methods'][mode]['metrics']['32']['recall']['value'] for n in positives)
        false = sum(len(results[n]['methods'][mode]['candidates']) for n in controls)
        verdict[mode] = dict(minimum_positive_recall_32=recall,additional_roots_32=additions[mode],
                             additional_roots_over_full_pool_32=sum(sum(d is not None and d<=32 and (h is None or h>32) for d,h in
                                 zip(results[n]['methods'][mode]['nearest_world'],results[n]['methods']['hidden']['full_pool_nearest_world'])) for n in positives),
                             control_point_proposals=false,integration='Go' if recall>=.8 and additions[mode]>=1 and false==0 else 'NoGo')
    write(a.out/'results.json',dict(cases=results,verdict=verdict,timings=timings,
          protocol_sha256=sha(a.work/'protocol.json'),accepted_sources=0,
          historical_certification='NotCertified',heldout_rgb_opened=False,source_answers_opened_after_all_proposals=True))
    shutil.copyfile(a.work/'protocol.json',a.out/'protocol.json')
    write(a.out/'manifest.json',dict(artifacts={f.name:sha(f) for f in a.out.iterdir() if f.is_file()},
                                   licensed_payloads=False,production_modified=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase',choices=('freeze','evaluate')); parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--out',type=Path)
    parser.add_argument('--fixtures',type=Path,default=ROOT/'build/x02/compiled-fixtures-frozen')
    parser.add_argument('--answers',type=Path,default=ROOT/'build/q3map2-bake-verified/answers')
    parser.add_argument('--stock',type=Path,default=ROOT/'build/x02/hoth3-views/inputs')
    parser.add_argument('--geometry-tool',type=Path,default=ROOT/'build/asr02-tools/idf_geometry.exe')
    parser.add_argument('--hidden-tool',type=Path,default=ROOT/'build/s06-gcc/lightrecon_hidden_tool.exe')
    args = parser.parse_args(); args.work = args.work.resolve()
    if args.phase == 'evaluate' and args.out is None: parser.error('evaluate requires --out')
    (freeze if args.phase=='freeze' else evaluate)(args)
