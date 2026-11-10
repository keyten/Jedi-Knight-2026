"""ASR23 public-input-only proposal and training fitter. Never imports evaluator.

The common benchmark fitter operates in fixed forward-encoded appearance units,
using native modern transfer columns. It is not a change to the historical solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess
import time
import numpy as np

from tools.rend2.adaptive_hidden.run import adapt
from tools.rend2.hidden_candidates.run import request, semantic
from tools.rend2.grid_triangulation.triangulate import fit, residual, angle, sub, unit


def load(p):
    return json.loads(Path(p).read_text(encoding='utf-8'))


def write(p, x):
    Path(p).write_text(json.dumps(x, indent=2, sort_keys=True, allow_nan=False)+'\n', encoding='utf-8')


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def grid_proposals(raw, policy):
    """Bounded ASR13-style angular intersections; colors group original codes only.

    Grid RGB is never regressed against lightmap RGB or assigned irradiance units.
    Unknown geometry at a proposed intersection is checked by the native adapter.
    """
    rows = []
    seen = set()
    for r in raw:
        key = r['correlation_key']
        if (key in seen or not r['present'] or r['space'] != 'free' or r['boundary']
                or r['dynamic_possible'] or len(r['styles']) != 1 or r['styles'][0]['weak']):
            continue
        seen.add(key)
        rows.append(dict(position=r['position'], direction=unit(r['consumer256']),
                         colour=unit(r['styles'][0]['directed']), key=key))
    rows.sort(key=lambda r: r['key'])
    # Original ASR13 limits are retained; stable hash thinning is independent of RGB.
    rows = rows[:192]
    pairs = list(itertools.combinations(range(len(rows)), 2))
    cap = policy['grid_pair_cap']
    if len(pairs) > cap:
        pairs = [pairs[i*len(pairs)//cap] for i in range(cap)]
    found = []
    gate = math.radians(policy['grid_angle_degrees'])
    for i, j in pairs:
        a, b = rows[i], rows[j]
        if math.dist(a['colour'], b['colour']) > .35 or angle(a['direction'], b['direction']) < math.radians(8):
            continue
        p = fit([a, b])
        if p is None or not all(math.isfinite(v) and abs(v) < 100000 for v in p):
            continue
        support = [r for r in rows if residual(r, p) <= gate and math.dist(a['colour'], r['colour']) <= .45]
        if len(support) < policy['grid_minimum_records']:
            continue
        p = fit(support)
        if p is None:
            continue
        errors = [residual(r, p) for r in support]
        if max(errors) > gate:
            continue
        found.append((-len(support), sum(e*e for e in errors), p))
    result = []
    for support, error, p in sorted(found):
        if any(math.dist(p, c['position']) < 24 for c in result):
            continue
        result.append(dict(kind='point', position=p, cue='grid', support=-support,
                           angular_squared_error=error))
        if len(result) == 8:
            break
    return result


def proposal_line(c, ident):
    if c['kind'] in ('surface', 'surface_point'):
        return f"{c['kind']} {ident} {c['surface']}"
    values = [c['kind'], ident, *c['position']]
    if c['kind'] in ('spot', 'sun'):
        values += c['axis']
    if c['kind'] == 'spot':
        values += [math.cos(c['cone']*.8), math.cos(c['cone'])]
    return ' '.join(map(str, values))


def encode(x):
    # Fixed gamma=1, compensate=1, exposure=0 forward hypothesis. Coupled RGB
    # peak compression matches Bake::Encode; no inverse or linear calibration.
    return x / np.maximum(1., x.max(axis=1, keepdims=True)/255.)


def weights(rows):
    groups = {}
    for r in rows:
        key = (tuple(r['block']), int(np.argmax(np.abs(r['normal'])))*2 + int(min(r['normal']) < -.5))
        groups[key] = groups.get(key, 0.) + r['area']
    return np.asarray([r['area']/groups[(tuple(r['block']), int(np.argmax(np.abs(r['normal'])))*2 + int(min(r['normal']) < -.5))]
                       for r in rows], dtype=float)/len(groups)


def interval_error(pred, target):
    return np.minimum(0., pred-target)+np.where(target < 255, np.maximum(0., pred-target-1.), 0.)


def score(pred, target, w):
    e = interval_error(encode(pred), target)
    return float(np.sum(w[:, None]*e*e)/3)


def refit(matrix, target, w, indices, policy):
    """Deterministic nonnegative coordinate fit in an explicit appearance model.

    RGB8 is a quantized interval; saturated channels are lower bounds. Native
    columns are normalized for conditioning; no power has physical units.
    """
    a = np.column_stack([np.ones(len(target)), matrix[:, indices]])
    scale = np.maximum(np.sqrt(np.sum(w[:, None]*a*a, axis=0)), 1e-12)
    a = a/scale
    x = np.zeros((a.shape[1], 3))
    upper = np.full(a.shape[1], 1000000.)
    upper[0] = policy['background_upper_rgb8']*scale[0]
    predicted = a@x
    work = 0
    for sweep in range(policy['nnls_sweeps']):
        largest = 0.
        # Unsaturated coordinates use interval midpoints; censored coordinates
        # contribute only while below their bound. Peak-compressed rows are
        # handled by the exact forward objective in the deterministic line search.
        for j in range(a.shape[1]):
            old = x[j].copy()
            rest = predicted-a[:, j, None]*old
            desired = target+.5
            active = np.where(target == 255, predicted < 255, True)
            denominator = np.sum(w[:, None]*a[:, j, None]**2*active, axis=0)
            proposed = np.maximum(0., np.minimum(upper[j], np.sum(w[:, None]*a[:, j, None]*(desired-rest)*active, axis=0)/np.maximum(denominator, 1e-20)))
            before = score(predicted, target, w)
            best, best_loss = old, before
            for fraction in (1., .5, .25, .125):
                trial = old+(proposed-old)*fraction
                loss = score(rest+a[:, j, None]*trial, target, w)
                work += len(target)*3
                if loss < best_loss:
                    best, best_loss = trial, loss
            x[j] = best
            predicted = rest+a[:, j, None]*best
            largest = max(largest, float(np.max(np.abs(best-old))))
        if largest < 1e-7:
            break
    powers = x/scale[:, None]
    return dict(indices=list(indices), powers=powers[1:].tolist(), background=powers[0].tolist(),
                training_mse=score(predicted, target, w), fit_work=work, sweeps=sweep+1)


def train_path(matrix, train, policy):
    target = np.asarray([r['raw'] for r in train], dtype=float)
    w = weights(train)
    initial = refit(matrix, target, w, [], policy)
    path, chosen, fits = [initial], [], 1
    total_work = initial['fit_work']
    while len(chosen) < policy['maximum_sources']:
        rivals = []
        incumbent = path[-1]['training_mse'] + len(chosen)*policy['training_source_cost']
        for j in range(matrix.shape[1]):
            if j in chosen:
                continue
            if fits >= policy['maximum_model_fits']:
                break
            trial = refit(matrix, target, w, chosen+[j], policy)
            fits += 1
            total_work += trial['fit_work']
            objective = trial['training_mse']+len(trial['indices'])*policy['training_source_cost']
            rivals.append((objective, j, trial))
        if not rivals:
            break
        objective, _, best = min(rivals, key=lambda r: (r[0], r[1]))
        if objective >= incumbent-1e-6:
            break
        chosen = best['indices']
        path.append(best)
    return path, fits, total_work


def run(inputs, output, native, policy):
    output.mkdir(parents=True, exist_ok=False)
    public = load(inputs/'public.json')
    train, receivers = public['training'], public['receivers']
    by_cue = {'fixture': public['fixtures'], 'fixture_point': [dict(c, kind='surface_point', cue='fixture_point') for c in public['fixtures']],
              'grid': grid_proposals(public['grid'], policy), 'sun': public['solar']}
    bounds = public['bounds']
    resources = []
    for strategy in ('strict', 'adaptive'):
        text = request(train, public['map_sha256'], bounds, bounds, False)
        text = adapt(text, strategy)
        path = output/(strategy+'.request'); path.write_text(text, encoding='ascii')
        start = time.perf_counter()
        child = subprocess.run([str(native/'lightrecon_adaptive_tool.exe'), str(inputs/'geometry.jkobs'), str(path)],
                               capture_output=True, text=True, timeout=180)
        if child.returncode:
            raise RuntimeError(child.stderr)
        report = json.loads(child.stdout)
        write(output/(strategy+'.json'), semantic(report))
        resources.append(dict(stage=strategy, seconds=time.perf_counter()-start, rays=report['rays'],
                              forward_calls=report['forward_calls'], work=report['work'], code=report['code']))
        by_cue[strategy] = []
        for c in report['candidates']:
            item = dict(kind='point' if c['source_kind'] == 0 else 'spot', position=c['position'], cue=strategy)
            if item['kind'] == 'spot':
                item.update(axis=c['outward'], cone=max(.08, min(1.5, math.atan2(c['historical_radius'], c['historical_target_distance']))))
            by_cue[strategy].append(item)
    candidates = []
    for cue in ('fixture', 'fixture_point', 'grid', 'adaptive', 'strict', 'sun'):
        # Equal per-provider limit, frozen independently of fit/evaluator answers.
        for c in by_cue[cue][:8]:
            c = dict(c, cue=cue)
            candidates.append(c)
    candidates = candidates[:policy['maximum_candidates']]
    lines = ['JKBASIS23 '+str(len(candidates))]
    lines += [proposal_line(c, i+1) for i, c in enumerate(candidates)]
    lines += [str(len(receivers))]
    lines += [' '.join(map(str, [r['surface'], *r['position'], *r['normal']])) for r in receivers]
    path = output/'basis.request'; path.write_text('\n'.join(lines)+'\n', encoding='ascii')
    start = time.perf_counter()
    child = subprocess.run([str(native/'lightrecon_blind_basis_tool.exe'), str(inputs/'geometry.jkobs'), str(path)],
                           capture_output=True, text=True, timeout=180)
    if child.returncode:
        raise RuntimeError(child.stderr)
    basis = json.loads(child.stdout)
    write(output/'basis.json', basis)
    resources.append(dict(stage='basis', seconds=time.perf_counter()-start, rays=basis['rays'], work=basis['traversal_work']))
    admitted = []
    for i, (c, b) in enumerate(zip(candidates, basis['candidates'])):
        c.update(native=b, proposal_id=i+1)
        if b['admitted'] and all(v is not None and math.isfinite(v) for v in b['column']):
            admitted.append(c)
    lookup = {r['id']: i for i, r in enumerate(receivers)}
    indices = [lookup[r['id']] for r in train]
    results = {}
    for method, cues in policy['methods'].items():
        start = time.perf_counter()
        subset = [c for c in admitted if c['cue'] in cues]
        # Deduplicate same geometry across providers, preserving every cue tag.
        unique = []
        for c in subset:
            if any(c['native']['type'] == d['native']['type'] and math.dist(c['native']['position'], d['native']['position']) < 1 for d in unique):
                continue
            unique.append(c)
        subset = unique
        matrix = np.asarray([c['native']['column'] for c in subset], dtype=float).T if subset else np.zeros((len(receivers), 0))
        path, fits, fit_work = train_path(matrix[indices], train, policy)
        for state in path:
            state['predictions'] = encode(np.asarray(state['background'])[None, :]+matrix[:, state['indices']]@np.asarray(state['powers']).reshape((-1, 3))).tolist()
            state['sources'] = [dict(proposal_id=subset[i]['proposal_id'], position=subset[i]['native']['position'],
                                     type=subset[i]['native']['type'], cue=subset[i]['cue'],
                                     placement=subset[i]['native']['placement'], state='Candidate') for i in state['indices']]
        results[method] = dict(path=path, proposals=[dict(position=c['native']['position'], type=c['native']['type'], cue=c['cue']) for c in subset],
                               fits=fits, fit_work=fit_work, fit_seconds=time.perf_counter()-start, rejected_or_unknown=len(candidates)-len(admitted))
    write(output/'frozen-models.json', dict(schema='org.openjk.asr23-training-models.v1', methods=results,
           resources=resources, training_only=True, public_sha256=sha(inputs/'public.json'),
           geometry_sha256=sha(inputs/'geometry.jkobs'), receiver_ids=[r['id'] for r in receivers]))
    print(inputs.name, 'training models frozen', flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    for name in ('inputs', 'output', 'native', 'policy'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    run(a.inputs.resolve(), a.output.resolve(), a.native.resolve(), load(a.policy))
