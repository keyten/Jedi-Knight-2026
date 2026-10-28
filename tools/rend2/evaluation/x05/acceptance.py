"""Manifest-driven forward-capability acceptance; no emitter fitting or renderer.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
import math
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import metrics

SCHEMA = 'org.openjk.forward-acceptance.v1'
PROFILE = 'NetRadiant-d9338309e-ColorToBytes-gamma1-compensate1-exposure0-v1'
KINDS={'PositiveLight','DarkBlockedNegative','EncodingQuantization','KernelConformance','ExpectedUnknown','ExpectedNotComparable'}


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path, limit=64*1024*1024):
    p = Path(path)
    if p.stat().st_size > limit: raise ValueError('JSON byte limit')
    return json.loads(p.read_text(encoding='utf-8'), parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))


def target(row, encoding=PROFILE):
    if encoding != PROFILE: raise ValueError('Unspecified output operator; no silent RGB domain conversion')
    raw = row['raw']
    if len(raw) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in raw): raise ValueError('Original RGB8 required')
    area = row['area']
    if not isinstance(area, (float,int)) or not math.isfinite(area) or area <= 0: raise ValueError('Positive receiver area')
    # Already-encoded output: 255 is the shared-peak endpoint. It does NOT mean
    # three independent unconstrained physical channel intensities >=255.
    return dict(id=row['id'], weight=area, domain='EncodedRGB8', primaries='EncodedSourceRGB',
        comparison_id=encoding, lower=raw, upper=[min(255., v+1.) for v in raw])


def predictions(rows, values=None, status='Available', reason=None):
    if values is None: values = [[0.,0.,0.]]*len(rows)
    if len(values) != len(rows): raise ValueError('Prediction span')
    out = []
    for r, v in zip(rows, values):
        p = dict(id=r['id'], status=status)
        if status == 'Available':
            p.update(rgb=list(v), domain='EncodedRGB8', primaries='EncodedSourceRGB', comparison_id=PROFILE)
        else: p['reason'] = reason or 'Unsupported capability'
        out.append(p)
    return out


def numeric_predictions(values):
    output = []; seen = set()
    for p in values:
        if p['id'] in seen: raise ValueError('Duplicate prediction')
        seen.add(p['id'])
        if p.get('status') == 'Available':
            if p.get('rgb') is None: raise ValueError('Available prediction without RGB')
            output.append(p)
        elif p.get('rgb') is not None:
            raise ValueError('Unknown/Unavailable must not hide a numerical zero')
    return output


def admission(rows, contract, policy):
    if contract['kind'] not in KINDS:raise ValueError('Unknown contract kind; no silent weakened gate')
    selected = [r for r in rows if contract['group'] in r.get('groups', [])]
    blocks = {r['block'] for r in selected}
    positive = [r for r in selected if max(r['raw']) >= policy['support']['minimum_positive_code']]
    zero = metrics.prediction_metrics([target(r) for r in selected], predictions(selected))
    base = zero['rgb_interval_rmse']['value']
    reasons = []
    status_only=contract['kind'] in ('ExpectedUnknown','ExpectedNotComparable')
    if len(selected) < (1 if status_only else policy['support']['minimum_receivers']): reasons.append('Insufficient receivers')
    if len(blocks) < (1 if status_only else policy['support']['minimum_blocks']): reasons.append('Insufficient independent spatial blocks')
    if contract['kind'] == 'PositiveLight':
        if len(positive) < policy['support']['minimum_receivers']: reasons.append('Insufficient meaningful positive channels/receivers')
        if len({r['block'] for r in positive}) < policy['support']['minimum_blocks']: reasons.append('Positive signal confined to one block')
        if base is None or base < policy['support']['minimum_zero_interval_rmse']: reasons.append('Zero baseline non-informative/weak')
    return dict(status='Admitted' if not reasons else 'InsufficientEvidence', reasons=reasons,
        count=len(selected), spatial_blocks=len(blocks), positive_receivers=len(positive),
        zero_interval_rmse=base, known_receiver_area=sum(r['area'] for r in selected))


def evaluate(rows, prediction, contracts, policy, encoding=PROFILE):
    if encoding != PROFILE:
        return dict(status='NotComparable', reason='Unknown/incompatible output profile', contracts=[], success=False)
    if len(rows) > 100000: raise ValueError('Forward test record limit')
    targets = [target(r) for r in rows]; nums = numeric_predictions(prediction)
    index = {p['id']: p for p in prediction}; allowed = {r['id'] for r in rows}
    if set(index)-allowed: raise ValueError('Cross-case/unrequested predictions')
    overall = metrics.prediction_metrics(targets, nums); results = []
    for c in contracts:
        selected = [r for r in rows if c['group'] in r.get('groups', [])]
        admitted = admission(rows, c, policy)
        expected_unknown = c['kind'] in ('ExpectedUnknown','ExpectedNotComparable')
        if expected_unknown:
            statuses=('NotComparable',) if c['kind']=='ExpectedNotComparable' else ('Unknown','Unavailable','Unsupported')
            ok = bool(selected) and all(index.get(r['id'], {}).get('status') in statuses and
                index.get(r['id'], {}).get('reason') for r in selected)
            results.append(dict(id=c['id'], admission=admitted, status='Passed' if ok else 'Failed',
                reason='Typed unknown required; a numerical black value cannot satisfy this contract'))
            continue
        ts = [target(r) for r in selected]
        ids = {r['id'] for r in selected}; ps = [p for p in nums if p['id'] in ids]
        score = metrics.prediction_metrics(ts, ps); base = admitted['zero_interval_rmse']; error = score['rgb_interval_rmse']['value']
        max_error = None
        if ps:
            by_id = {t['id']: t for t in ts}
            max_error = max(max(max(a-v,0.,v-b) for a,b,v in zip(by_id[p['id']]['lower'],by_id[p['id']]['upper'],p['rgb'])) for p in ps)
        covered = score['prediction_coverage']['value']; reasons = list(admitted['reasons'])
        if covered is None or covered < 1-policy['acceptance']['maximum_missing_area_fraction']: reasons.append('Incomplete/incomparable prediction coverage')
        if error is None or error > c.get('maximum_interval_rmse', policy['acceptance']['maximum_interval_rmse']): reasons.append('Interval error exceeds frozen budget')
        if max_error is None or max_error > c.get('maximum_channel_interval_error', policy['acceptance']['maximum_channel_interval_error']): reasons.append('Channel error exceeds frozen budget')
        relative = (base-error)/base if base and error is not None else None
        if c['kind'] == 'PositiveLight' and (relative is None or relative < policy['acceptance']['minimum_zero_relative_improvement']):
            reasons.append('No required positive-light improvement over zero')
        if c['kind'] == 'DarkBlockedNegative' and any(max(p['rgb']) > policy['acceptance']['maximum_blocked_prediction_code'] for p in ps):
            reasons.append('Light leaked into a known dark/blocked receiver')
        results.append(dict(id=c['id'], status='Passed' if not reasons else 'Failed', reasons=reasons,
            admission=admitted, score=score, maximum_channel_interval_error=max_error,
            absolute_zero_improvement=base-error if base is not None and error is not None else None,
            relative_zero_improvement=relative,
            relative_status='Available' if base else 'NonInformativeBaseline'))
    return dict(status='Passed' if results and all(r['status']=='Passed' for r in results) else 'Failed',
        success=bool(results) and all(r['status']=='Passed' for r in results), contracts=results,
        overall=overall, claim='Forward capability only; no emitter recovery or historical compiler identification')


def profile_gate(alternatives, correct, policy, expected='Discriminating'):
    measured = [r for r in alternatives if r.get('validation_rmse') is not None]
    if len(measured)!=len(alternatives) or not measured or not any(r['id']==correct for r in measured):
        return dict(status='Failed', reason='Missing required comparable profile predictions')
    if len({(r['parameter_budget'],r['split_hash'],r['comparison_id']) for r in measured}) != 1:
        raise ValueError('Unfair profile/nuisance/split comparison')
    ordered = sorted(measured,key=lambda r:(r['validation_rmse'],r['id'])); best = ordered[0]['validation_rmse']
    viable = [r['id'] for r in ordered if r['validation_rmse'] <= best+policy['profile']['viable_regret']]
    correct_error = next(r['validation_rmse'] for r in measured if r['id']==correct)
    if any(not math.isfinite(r['validation_rmse']) or r['validation_rmse']<0 for r in measured):raise ValueError('Invalid profile error')
    separation = min((r['validation_rmse']-correct_error for r in measured if r['id']!=correct),default=None)
    supported = len(viable)>1 if expected=='Ambiguous' else (
        viable==[correct] and separation is not None and separation>=policy['profile']['minimum_discriminating_separation'])
    held=[r for r in measured if r.get('heldout_rmse') is not None]
    if len(held)!=len(measured):supported=False
    held_best=min((r['heldout_rmse'] for r in held),default=None)
    held_viable=[r['id'] for r in held if r['heldout_rmse']<=held_best+policy['profile']['viable_regret']]
    if expected=='Ambiguous':supported=supported and correct in viable and len(held_viable)>1 and correct in held_viable
    else:
        correct_held=next((r['heldout_rmse'] for r in held if r['id']==correct),None)
        supported=supported and held_viable==[correct] and all(r['heldout_rmse']-correct_held>=policy['profile']['minimum_discriminating_separation'] for r in held if r['id']!=correct)
    supported=supported and correct_error<=policy['acceptance']['maximum_interval_rmse'] and next((r['heldout_rmse'] for r in held if r['id']==correct),math.inf)<=policy['acceptance']['maximum_interval_rmse']
    return dict(status='Ambiguous' if supported and expected=='Ambiguous' else 'Passed' if supported else 'Failed',
        expected=expected, viable=viable, heldout_viable=held_viable,separation=separation, alternatives=measured,
        posterior_probabilities=None, compiler_identity='Known controlled profile only; stock UnknownProfile unchanged')
