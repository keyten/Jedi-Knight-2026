"""Trusted ASR23 validation selection and post-freeze held-out/source scoring.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import itertools
import math
from pathlib import Path
import numpy as np
from .solver import load, write, sha, weights, interval_error


def metric(rows, predictions):
    if not rows or len(rows) != len(predictions) or any(len(p) != 3 or any(not math.isfinite(x) for x in p) for p in predictions):
        return dict(status='Unavailable', mse=None, black_mse=None, black_fraction=None, worst_block_rmse=None)
    target, pred = np.asarray([r['raw'] for r in rows]), np.asarray(predictions)
    w = weights(rows)
    e = interval_error(pred, target)
    mse = float(np.sum(w[:,None]*e*e)/3)
    black = float(np.sum(w[:,None]*target*target)/3)
    blocks = {}
    for r, err in zip(rows, e):
        blocks.setdefault(tuple(r['block']), []).append(float(np.mean(err*err)))
    return dict(status='Pass', mse=mse, black_mse=black, black_fraction=mse/black if black > 0 else None,
                worst_block_rmse=max(math.sqrt(sum(v)/len(v)) for v in blocks.values()), rows=len(rows), blocks=len(blocks))


def match(truth, predicted, gate):
    """Maximum-cardinality one-to-one position matching; type scored separately."""
    choices = []
    def walk(i, used, pairs):
        if i == len(truth):
            choices.append(list(pairs)); return
        walk(i+1, used, pairs)
        for j, p in enumerate(predicted):
            if j not in used and math.dist(truth[i]['position'], p['position']) <= gate:
                walk(i+1, used | {j}, pairs+[(i,j)])
    walk(0,set(),[])
    best = min(choices, key=lambda pairs:(-len(pairs), sum(math.dist(truth[i]['position'], predicted[j]['position']) for i,j in pairs)))
    return dict(truth_count=len(truth), predicted_count=len(predicted), matched=len(best),
                recall=len(best)/len(truth) if truth else None,
                precision=len(best)/len(predicted) if predicted else (1. if not truth else None),
                false_positives=len(predicted)-len(best),
                type_matches=sum(truth[i]['kind'] == predicted[j]['type'].lower() for i,j in best),
                position_errors_world=[math.dist(truth[i]['position'], predicted[j]['position']) for i,j in best])


def source_truth(answer):
    if 'ground_truth' not in answer:
        sources = [dict(kind=s['kind'],position=s['position']) for s in answer['sources']]
        if len(sources) == 3:  # Preserved V02 has an additional finite emitting prism.
            sources.append(dict(kind='rectangle',position=[-464,-168,314]))
        return sources
    gt = answer['ground_truth']
    sources = [dict(kind=s['type'],position=s['position_world']) for s in gt['lights']]
    surface = gt.get('surface_emitter')
    if surface:
        lo,hi = surface['bounds']
        sources.append(dict(kind='rectangle', position=[(a+b)/2 for a,b in zip(lo,hi)],
                            approximation=surface.get('native_analytic_tube') is False))
    return sources


def check_frozen(work, protocol, root, native):
    if any(sha(root/name) != digest for name,digest in protocol['sources'].items()):
        raise ValueError('Frozen code/policy changed')
    if any(sha(native/(name+'.exe')) != digest for name,digest in protocol['binaries'].items()):
        raise ValueError('Frozen native binary changed')
    for scene in protocol['scenes']:
        name = scene['name']
        files = [(work/'public'/name/'public.json','public_sha256'),
                 (work/'public'/name/'geometry.jkobs','geometry_sha256'),
                 (work/'private'/(name+'-answer.json'),'answer_sha256'),
                 (work/'private'/(name+'-folds.json'),'evaluator_sha256')]
        if any(sha(p) != scene[key] for p,key in files):
            raise ValueError('Frozen input/answer changed '+name)


def evaluate(work, runs, out):
    out.mkdir(parents=True, exist_ok=False)
    protocol = load(work/'protocol.json'); policy = protocol['policy']
    # Verify every child output has frozen before opening a single source answer.
    frozen = {s['name']: sha(runs/s['name']/'frozen-models.json') for s in protocol['scenes']}
    selections = {}
    for scene in protocol['scenes']:
        name = scene['name']; result = load(runs/name/'frozen-models.json')
        private = load(work/'private'/(name+'-folds.json'))
        ids = {ident:i for i,ident in enumerate(result['receiver_ids'])}
        val = private['validation']
        methods = {}
        for method,data in result['methods'].items():
            candidates = []
            for index,state in enumerate(data['path']):
                scores = metric(val, [state['predictions'][ids[r['id']]] for r in val])
                if scores['status'] == 'Unavailable':
                    continue
                candidates.append((scores['mse']+len(state['sources'])*policy['validation_source_cost'],len(state['sources']),index,scores))
            if not candidates:
                if name == 'empty' and len(data['path']) == 1 and not data['path'][0]['sources']:
                    methods[method] = dict(status='Pass',selected_path=0,validation=dict(status='Unavailable'),
                        reason='Solid-page training-only empty control abstained; no independent illumination score')
                else:
                    methods[method] = dict(status='Unavailable', reason='Missing validation coverage')
                continue
            best = min(candidates)
            baseline = candidates[0][0]
            if best[0] > baseline-policy['minimum_validation_gain']:
                best = candidates[0]
            methods[method] = dict(status='Pass', selected_path=best[2], validation=best[3])
        selections[name] = methods
    write(out/'validation-selections.json', dict(selections=selections, training_output_hashes=frozen,
          heldout_scored=False, source_answers_opened=False))
    selection_hash = sha(out/'validation-selections.json')
    # Everything that can select a source is frozen above. Held-out and source
    # answers below can only score it; they cannot rerank or change thresholds.
    scenes = []
    for scene in protocol['scenes']:
        name = scene['name']; result = load(runs/name/'frozen-models.json')
        private = load(work/'private'/(name+'-folds.json'))
        answer = load(work/'private'/(name+'-answer.json'))
        if answer.get('compiled_bsp_sha256', scene['map_sha256']) != scene['map_sha256']:
            raise ValueError('Answer/BSP identity mismatch')
        truth = source_truth(answer)
        held = private['heldout']; ids = {ident:i for i,ident in enumerate(result['receiver_ids'])}
        methods = {}
        for method,data in result['methods'].items():
            selected = selections[name][method]
            if selected['status'] == 'Unavailable':
                methods[method] = selected; continue
            state = data['path'][selected['selected_path']]
            local = [s for s in state['sources'] if s['type'].lower() != 'sun']
            proposals = [s for s in data['proposals'] if s['type'].lower() != 'sun']
            selected_metric = match(truth, local, policy['position_gate_world'])
            proposal_metric = match(truth, proposals, policy['position_gate_world'])
            pred = [state['predictions'][ids[r['id']]] for r in held]
            score = metric(held, pred)
            geometry_violations = sum(s['placement'] not in ('Free','SkyBoundary') for s in state['sources'])
            # Raw darkness alone is not wall evidence. Restrict this diagnostic
            # to the independently authored sealed-wall test and its dark fold.
            leaks = [max(p) for r,p in zip(held,pred) if name == 'wall_leak' and max(r['raw']) <= 1]
            wall = dict(status='Pass' if max(leaks,default=0) <= policy['maximum_wall_leak_rgb8'] else 'Fail',
                        maximum_rgb8=max(leaks), rows=len(leaks)) if leaks else dict(status='Unavailable',maximum_rgb8=None,rows=0)
            success = (len(truth) >= 2 and selected_metric['matched'] == len(truth) and
                       selected_metric['false_positives'] == 0 and selected_metric['type_matches'] == len(truth) and
                       score['black_fraction'] is not None and score['black_fraction'] <= policy['heldout_black_fraction_gate'] and
                       geometry_violations == 0)
            methods[method] = dict(selection=selected, proposals=proposal_metric, selected=selected_metric,
                sources=state['sources'], source_count=len(state['sources']), local_source_count=len(local),
                solar_source_count=len(state['sources'])-len(local), heldout=score, wall_leaks=wall,
                geometry_violations=geometry_violations, blind_multisource_success=success,
                fitting=dict(fits=data['fits'], seconds=data['fit_seconds'],
                    fit_work=data['fit_work'], retained_path_work=sum(s['fit_work'] for s in data['path'])),
                status='Pass' if success else 'Fail' if len(truth) >= 2 else
                       'Unavailable' if score['status'] == 'Unavailable' else
                       'Pass' if selected_metric['matched'] == len(truth) and selected_metric['false_positives'] == 0 else 'Fail')
        scenes.append(dict(name=name, map_sha256=scene['map_sha256'], answer_sha256=scene['answer_sha256'],
                           counts=scene['counts'], grid_train=scene['grid_train'], source_count=len(truth),
                           methods=methods, native_resources=result['resources']))
    aggregate = {}
    for method in policy['methods']:
        values = [s['methods'][method] for s in scenes if 'heldout' in s['methods'][method]]
        valid = [r['heldout']['black_fraction'] for r in values if r['heldout']['black_fraction'] is not None]
        selected_truth = sum(r['selected']['truth_count'] for r in values)
        selected_predictions = sum(r['selected']['predicted_count'] for r in values)
        matches = sum(r['selected']['matched'] for r in values)
        aggregate[method] = dict(scenes=len(values), truth_sources=selected_truth, matched_sources=matches,
            selected_sources=selected_predictions, recall=matches/selected_truth if selected_truth else None,
            precision=matches/selected_predictions if selected_predictions else None,
            false_positives=sum(r['selected']['false_positives'] for r in values),
            proposal_matches=sum(r['proposals']['matched'] for r in values),
            proposal_count=sum(r['proposals']['predicted_count'] for r in values),
            mean_heldout_black_fraction=float(np.mean(valid)) if valid else None,
            blind_multisource_successes=sum(r['blind_multisource_success'] for r in values),
            geometry_violations=sum(r['geometry_violations'] for r in values),
            fit_seconds=sum(r['fitting']['seconds'] for r in values), fit_count=sum(r['fitting']['fits'] for r in values))
    all_cues, baseline = aggregate['all_cues'], aggregate['ja_baseline']
    controls = [s for s in scenes if s['name'] in ('empty','skylight','sun_sky')]
    gates = dict(
        blind_multisource=dict(status='Pass' if all_cues['blind_multisource_successes'] >= 1 else 'Fail',
                               value=all_cues['blind_multisource_successes'], minimum=1),
        recall_improves=dict(status='Pass' if all_cues['recall'] > baseline['recall'] else 'Fail',
                             baseline=baseline['recall'], all_cues=all_cues['recall']),
        heldout_improves=dict(status='Pass' if all_cues['mean_heldout_black_fraction'] < baseline['mean_heldout_black_fraction'] else 'Fail',
                              baseline=baseline['mean_heldout_black_fraction'], all_cues=all_cues['mean_heldout_black_fraction']),
        geometry=dict(status='Pass' if all_cues['geometry_violations'] == 0 else 'Fail', value=all_cues['geometry_violations'],maximum=0),
        negative_local_sources=dict(status='Pass' if sum(s['methods']['all_cues']['local_source_count'] for s in controls) == 0 else 'Fail',
                                    value=sum(s['methods']['all_cues']['local_source_count'] for s in controls),maximum=0),
        stock_source_positions=dict(status='Unavailable', reason='True stock source positions are missing; visual quality is not source truth'))
    if sha(out/'validation-selections.json') != selection_hash or any(sha(runs/name/'frozen-models.json') != digest for name,digest in frozen.items()):
        raise ValueError('Model selection changed during held-out audit')
    write(out/'results.json', dict(schema='org.openjk.asr23-results.v1', gates=gates, aggregate=aggregate, scenes=scenes,
           validation_selections_sha256=selection_hash, answers_opened_after_all_training_models_frozen=True,
           protocol_sha256=sha(work/'protocol.json'), interpretation='Conditional aesthetic source discovery; not historical or SI certification'))
    lines = ['# ASR23 frozen blind benchmark', '', '| Method | Recall | Precision | Proposal matches | False positives | Blind multisource successes | Mean held-out / black MSE | Fit seconds |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for name,r in aggregate.items():
        lines.append(f"| {name} | {r['recall']:.3f} | {r['precision'] if r['precision'] is not None else 'Unavailable'} | {r['proposal_matches']}/{r['truth_sources']} | {r['false_positives']} | {r['blind_multisource_successes']} | {r['mean_heldout_black_fraction']:.5f} | {r['fit_seconds']:.2f} |")
    lines += ['', '| Gate | Status |', '|---|---|']+[f"| {name} | {r['status']} |" for name,r in gates.items()]
    (out/'tables.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    return load(out/'results.json')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    for name in ('work','runs','out'):
        p.add_argument('--'+name,type=Path,required=True)
    a = p.parse_args(); evaluate(a.work,a.runs,a.out)
