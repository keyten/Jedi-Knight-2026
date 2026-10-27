"""Offline evaluation primitives. No fitting, renderer, truth in solver inputs or dependencies.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
from collections import Counter, defaultdict

VERSION = 'org.openjk.lighting-metrics.v1'
PHYSICAL = {'candela', 'nits', 'lux'}
KINDS = {'point', 'spot', 'rectangle', 'tube', 'sun', 'sky', 'transport'}


def metric(value=None, *, status=None, reason=None, unit=None, **details):
    out = dict(status=status or ('Available' if value is not None else 'Unavailable'), value=value)
    if reason: out['reason'] = reason
    if unit: out['unit'] = unit
    out.update(details)
    return out


def ratio(a, b, **details):
    return metric(a/b if b else None, numerator=a, denominator=b,
                  reason=None if b else 'Empty denominator', **details)


def finite(x):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x):
        raise ValueError('Finite numeric input required')
    return x


def vector(v):
    if len(v) != 3: raise ValueError('Expected XYZ/RGB')
    return tuple(finite(x) for x in v)


def norm(v): return math.sqrt(sum(x*x for x in v))
def distance(a, b): return norm([x-y for x, y in zip(vector(a), vector(b))])


def angle(a, b, axial=False):
    a, b = vector(a), vector(b); na, nb = norm(a), norm(b)
    if min(na, nb) <= 1e-12: raise ValueError('Zero direction')
    c = sum(x*y for x, y in zip(a, b))/(na*nb)
    return math.degrees(math.acos(max(-1., min(1., abs(c) if axial else c))))


def mean(values, unit=None):
    return metric(math.fsum(values)/len(values) if values else None, unit=unit, count=len(values),
                  reason=None if values else 'No comparable records')


def hungarian(cost):
    """Rectangular minimum-cost assignment; n <= m. O(n*m*n), no ID lookups."""
    n = len(cost); m = len(cost[0]) if n else 0
    if n > m or any(len(row) != m for row in cost): raise ValueError('Assignment dimensions')
    u, v, p, way = [0.]*(n+1), [0.]*(m+1), [0]*(m+1), [0]*(m+1)
    for i in range(1, n+1):
        p[0] = i; j0 = 0; mins, used = [math.inf]*(m+1), [False]*(m+1)
        while True:
            used[j0] = True; i0 = p[j0]; delta = math.inf; j1 = 0
            for j in range(1, m+1):
                if used[j]: continue
                c = cost[i0-1][j-1]-u[i0]-v[j]
                if c < mins[j]: mins[j], way[j] = c, j0
                if mins[j] < delta: delta, j1 = mins[j], j
            for j in range(m+1):
                if used[j]: u[p[j]] += delta; v[j] -= delta
                else: mins[j] -= delta
            j0 = j1
            if p[j0] == 0: break
        while True:
            j1 = way[j0]; p[j0] = p[j1]; j0 = j1
            if j0 == 0: break
    return [(p[j]-1, j-1) for j in range(1, m+1) if p[j]]


def emitter_errors(t, p):
    out = {}
    if t['kind'] not in ('sun', 'sky', 'transport') and 'position' in t and 'position' in p:
        out['position_world'] = distance(t['position'], p['position'])
        out['xyz_world'] = [b-a for a, b in zip(t['position'], p['position'])]
    if 'direction' in t and 'direction' in p:
        # Sun uses receiver->source; spot/rectangle uses emission propagation.
        # Tube axis has no distinguished sign. Do not compare different conventions.
        if t.get('direction_convention') != p.get('direction_convention'):
            out['direction_status'] = 'NotComparable'
        else:
            out['direction_degrees'] = angle(t['direction'], p['direction'],
                axial=t['kind'] == 'tube' or t.get('two_sided', False))
    if 'cone_degrees' in t and 'cone_degrees' in p:
        out['cone_degrees'] = abs(finite(t['cone_degrees'])-finite(p['cone_degrees']))
    if 'dimensions' in t and 'dimensions' in p:
        a, b = t['dimensions'], p['dimensions']
        if len(a) != len(b) or any(finite(x) <= 0 for x in a+b): raise ValueError('Invalid dimensions')
        # Full rectangle side lengths are unordered. Tube = [length, radius].
        pairs = [b, b[::-1]] if t['kind'] == 'rectangle' else [b]
        out['dimension_relative'] = min(max(abs(x-y)/x for x, y in zip(a, bs)) for bs in pairs)
    if 'tangent' in t and 'tangent' in p:
        # A rectangle's in-plane roll is important, even with a correct normal.
        roll = angle(t['tangent'], p['tangent'], axial=True)
        if t.get('dimensions') and p.get('dimensions'):
            td, pd = t['dimensions'], p['dimensions']
            # Swapping side names and rotating 90 degrees describes the same rectangle.
            same = max(abs(x-y)/x for x, y in zip(td, pd))
            swapped = max(abs(x-y)/x for x, y in zip(td, pd[::-1]))
            if swapped < same or (td[0] == td[1] and pd[0] == pd[1]): roll = min(roll, abs(90-roll))
        out['roll_degrees'] = roll
    return out


def gate(t, p, policy, ignore_type=False):
    if not ignore_type and t['kind'] != p['kind']: return None
    errors = emitter_errors(t, p); terms = []
    required = [('position_world', 'position_world'), ('direction_degrees', 'angle_degrees'),
                ('cone_degrees', 'cone_degrees'), ('dimension_relative', 'dimension_relative'),
                ('roll_degrees', 'roll_degrees')]
    for field, option in required:
        if field not in errors: continue
        limit = finite(t.get('tolerances', {}).get(option, policy[option]))
        if limit <= 0: raise ValueError('Positive match tolerance required')
        if errors[field] > limit: return None
        terms.append(errors[field]/limit)
    if t['kind'] not in ('sky', 'transport') and not terms: return None
    # Known truth geometry missing from prediction cannot pass on type alone.
    for field in ('position', 'direction', 'cone_degrees', 'dimensions', 'tangent'):
        if field in t and field not in p: return None
    if errors.get('direction_status'): return None
    # Sky/transport records are never physical emitter matches. Separate domain only.
    if t['kind'] in ('sky', 'transport') and t.get('component') != p.get('component'): return None
    return sum(terms)/max(1, len(terms))


def match(truth, predictions, policy, ignore_type=False):
    if max(len(truth), len(predictions)) > 512: raise ValueError('Emitter matching budget (512)')
    for s in truth+predictions:
        if s['kind'] not in KINDS: raise ValueError('Unsupported source kind')
    n, m = len(truth), len(predictions)
    # A valid edge costs <=1, unmatched costs n+1: cardinality first, then error.
    penalty = n+1
    edges = [[gate(t, p, policy, ignore_type) for p in predictions] for t in truth]
    costs = [[x if x is not None else 4*penalty for x in row]+[penalty]*n for row in edges]
    return [(i, j) for i, j in hungarian(costs) if j < m and edges[i][j] is not None]


def photometry(t, p):
    result = {}; a, b = t.get('photometry'), p.get('photometry')
    unknown = metric(status='NotComparable', reason='No shared unit and calibration/relative domain')
    if not a or not b: return dict(relative_intensity=unknown, absolute_intensity=unknown, chromaticity=unknown)
    comparable = (a.get('unit') == b.get('unit') and a.get('domain') and a.get('domain') == b.get('domain'))
    if comparable and a.get('value') is not None and b.get('value') is not None:
        x, y = finite(a['value']), finite(b['value'])
        if min(x, y) < 0: raise ValueError('Negative photometry')
        result['relative_intensity'] = metric(abs(y-x)/x if x else None,
            reason=None if x else 'Zero truth intensity: report absolute error instead')
        physical = a.get('kind') == b.get('kind') == 'PhysicallyAuthored' and a['unit'] in PHYSICAL
        result['absolute_intensity'] = (metric(abs(y-x), unit=a['unit']) if physical else
            metric(status='NotComparable', reason='Not jointly physically authored/calibrated'))
    else: result.update(relative_intensity=unknown, absolute_intensity=unknown)
    if a.get('rgb') is not None and b.get('rgb') is not None and a.get('primaries') == b.get('primaries') == 'Rec709Linear':
        x, y = vector(a['rgb']), vector(b['rgb'])
        if min(x+y) < 0: raise ValueError('Negative linear colour')
        X, Y = sum(x), sum(y)
        result['chromaticity'] = (metric(norm([i/X-j/Y for i, j in zip(x, y)]), unit='L2 normalized RGB simplex')
                                   if min(X, Y) > 0 else metric(reason='Black has no chromaticity'))
    else: result['chromaticity'] = metric(status='NotComparable', reason='Unknown/mismatched RGB primaries')
    return result


def detection(truth, predictions, policy, units_per_metre):
    finite(units_per_metre)
    if units_per_metre <= 0: raise ValueError('World scale')
    # Environment and transport must not improve physical-emitter detection.
    t = [x for x in truth if x['kind'] not in ('sky', 'transport')]
    p = [x for x in predictions if x['kind'] not in ('sky', 'transport')]
    pairs = match(t, p, policy); used_t = {i for i, _ in pairs}; used_p = {j for _, j in pairs}
    localized = match(t, p, policy, True)
    confusion = Counter(f'{t[i]["kind"]}->{p[j]["kind"]}' for i, j in localized)
    details = []
    for i, j in pairs:
        error = emitter_errors(t[i], p[j]); error['photometry'] = photometry(t[i], p[j])
        if 'position_world' in error:
            error['position_metres'] = error['position_world']/units_per_metre
            error['xyz_metres'] = [x/units_per_metre for x in error['xyz_world']]
        details.append(dict(truth_index=i, prediction_index=j, **error))
    # Geometric neighbourhoods diagnose duplicates/splits and merges; no extra TP credit.
    neighbours = [[j for j, source in enumerate(p) if gate(source_t, source, policy, True) is not None] for source_t in t]
    inverse = [[i for i, js in enumerate(neighbours) if j in js] for j in range(len(p))]
    return dict(matches=details, truth_count=len(t), prediction_count=len(p),
        geometric_summary={key: mean([r[key] for r in details if key in r], unit) for key, unit in
            (('position_world', 'world'), ('position_metres', 'metres'), ('direction_degrees', 'degrees'),
             ('cone_degrees', 'degrees'), ('dimension_relative', 'fraction'), ('roll_degrees', 'degrees'))},
        by_kind={kind: dict(truth=sum(s['kind'] == kind for s in t), predicted=sum(s['kind'] == kind for s in p),
            precision=ratio(sum(t[i]['kind'] == kind for i, _ in pairs), sum(s['kind'] == kind for s in p)),
            recall=ratio(sum(t[i]['kind'] == kind for i, _ in pairs), sum(s['kind'] == kind for s in t)))
            for kind in sorted(set(s['kind'] for s in t+p))},
        true_positive=len(pairs), false_positive=len(p)-len(pairs), false_negative=len(t)-len(pairs),
        precision=ratio(len(pairs), len(p)), recall=ratio(len(pairs), len(t)),
        f1=ratio(2*len(pairs), len(t)+len(p)), confusion=dict(sorted(confusion.items())),
        split_neighbourhoods=[dict(truth_index=i, predictions=js) for i, js in enumerate(neighbours) if len(js)>1],
        merge_neighbourhoods=[dict(prediction_index=j, truths=ii) for j, ii in enumerate(inverse) if len(ii)>1],
        unmatched_truth=sorted(set(range(len(t)))-used_t), unmatched_predictions=sorted(set(range(len(p)))-used_p),
        ambiguous_truth_count=sum(x.get('identifiability') == 'ambiguous' for x in t),
        environment_and_transport=dict(truth=len(truth)-len(t), predicted=len(predictions)-len(p)),
        claim='Conditional source matching, not an automatic reconstruction-success decision')


def probability_calibration(records, bins=5, independent_trials=False):
    if not 1 <= bins <= 20: raise ValueError('Calibration bin budget')
    accepted = []; scores = []
    for r in records:
        value = finite(r['score'])
        if not 0 <= value <= 1: raise ValueError('Confidence range')
        if r.get('label') not in (0, 1, None): raise ValueError('Binary outcome required')
        if r.get('meaning') != 'probability' or not r.get('calibration_protocol'):
            scores.append(r); continue
        if r.get('label') is not None: accepted.append((value, r['label']))
    table = []
    for k in range(bins):
        rows = [(p, y) for p, y in accepted if min(bins-1, int(p*bins)) == k]
        n = len(rows); success = sum(y for _, y in rows)
        if n:
            z = 1.959963984540054; phat = success/n; d = 1+z*z/n
            centre = (phat+z*z/(2*n))/d
            radius = z*math.sqrt(phat*(1-phat)/n+z*z/(4*n*n))/d
            table.append(dict(lower=k/bins, upper=(k+1)/bins, n=n, average_probability=sum(p for p, _ in rows)/n,
                              observed_rate=phat,
                              wilson95=[max(0., centre-radius), min(1., centre+radius)] if independent_trials else None))
        else: table.append(dict(lower=k/bins, upper=(k+1)/bins, n=0, observed_rate=None))
    brier = mean([(p-y)**2 for p, y in accepted])
    ece = metric(sum(r['n']*abs(r['average_probability']-r['observed_rate']) for r in table if r['n'])/len(accepted)
                 if accepted else None, reason=None if accepted else 'No probability claims with known labels')
    return dict(brier=brier, expected_calibration_error=ece, bins=table, scored=len(accepted),
                heuristic_scores_not_probabilities=len(scores),
                independence_assumed=independent_trials,
                warning='Reliability diagnostic; finite fixtures do not establish deployment calibration')


def interval_coverage(records):
    covered = []; widths = []; unknown = 0; levels = defaultdict(list)
    for r in records:
        if r.get('truth') is None or r.get('interval') is None:
            unknown += 1; continue
        lo, hi = r['interval']; x = r['truth']
        lo, hi, x = finite(lo), finite(hi), finite(x)
        if lo > hi: raise ValueError('Reversed uncertainty interval')
        covered.append(lo <= x <= hi); widths.append(hi-lo)
        if r.get('nominal') is not None:
            level = finite(r['nominal'])
            if not 0 < level < 1: raise ValueError('Interval nominal probability')
            levels[str(level)].append(lo <= x <= hi)
    return dict(coverage=ratio(sum(covered), len(covered)), mean_width=mean(widths), unavailable=unknown,
                nominal_levels=sorted(set(r['nominal'] for r in records if r.get('nominal') is not None)),
                by_nominal={k: dict(coverage=ratio(sum(v), len(v)), empirical_minus_nominal=sum(v)/len(v)-float(k))
                            for k, v in sorted(levels.items())},
                caveat='Separate levels/methods/scenes in controller; feasible bounds are not statistical intervals')


def eigenvalues_symmetric(matrix):
    """Small deterministic Jacobi eigensolver; PSD Gram matrices, no numpy dependency."""
    n = len(matrix)
    if n > 32 or any(len(row) != n for row in matrix): raise ValueError('Information matrix budget')
    a = [list(map(finite, row)) for row in matrix]
    for _ in range(max(1, 64*n*n)):
        if n < 2: break
        i, j = max(((i, j) for i in range(n) for j in range(i+1, n)), key=lambda ij: abs(a[ij[0]][ij[1]]))
        if abs(a[i][j]) <= 1e-14*max(1., max(abs(a[k][k]) for k in range(n))): break
        phi = .5*math.atan2(2*a[i][j], a[j][j]-a[i][i]); c, s = math.cos(phi), math.sin(phi)
        x, y, b = a[i][i], a[j][j], a[i][j]
        for k in range(n):
            if k not in (i, j): a[k][i], a[k][j] = c*a[k][i]-s*a[k][j], s*a[k][i]+c*a[k][j]
        for k in range(n): a[i][k], a[j][k] = a[k][i], a[k][j]
        a[i][i], a[j][j], a[i][j], a[j][i] = c*c*x-2*s*c*b+s*s*y, s*s*x+2*s*c*b+c*c*y, 0., 0.
    return sorted(max(0., a[i][i]) for i in range(n))


def information(rows, scales, singular_relative_cutoff=1e-6, noise_whitened=False):
    if not rows or not scales: return dict(rank=metric(reason='No source-specific response/Jacobian'))
    if len(rows) > 100000 or len(scales) > 32: raise ValueError('Information work budget')
    if not 0 < finite(singular_relative_cutoff) < 1: raise ValueError('Relative rank cutoff')
    if any(finite(x) <= 0 for x in scales): raise ValueError('Positive parameter scales required')
    n = len(scales); gram = [[0.]*n for _ in range(n)]
    for row in rows:
        if len(row) != n: raise ValueError('Jacobian shape')
        r = [finite(x)*s for x, s in zip(row, scales)]
        for i in range(n):
            for j in range(n): gram[i][j] += r[i]*r[j]
    singular = [math.sqrt(x) for x in eigenvalues_symmetric(gram)]
    cut = singular[-1]*singular_relative_cutoff
    rank = sum(x > max(1e-14, cut) for x in singular)
    return dict(rank=metric(rank), columns=n, rows=len(rows), singular_values=singular,
        condition=metric(singular[-1]/singular[0] if rank == n else None,
            reason=None if rank == n else 'Rank deficient; condition is infinite'),
        parameter_scales=scales, cutoff=singular_relative_cutoff, noise_whitened=noise_whitened,
        claim='Local scaled response sensitivity; not global identifiability or calibrated covariance')


def profile_evidence(hypotheses, regret, known_profile=None, selected=None, asserted_unique=False):
    if finite(regret) < 0: raise ValueError('Negative profile regret budget')
    valid = [r for r in hypotheses if r.get('validation_error') is not None]
    if not valid: return dict(status='Unavailable', reason='No comparable held-out profile predictions')
    domains = {(r['domain'], r['parameter_budget'], r['partition_hash']) for r in valid}
    if len(domains) != 1: raise ValueError('Unfair profile comparison (domain/budget/partition)')
    valid = sorted(valid, key=lambda r: (finite(r['validation_error']), r['id']))
    best = valid[0]['validation_error']; viable = [r['id'] for r in valid if r['validation_error'] <= best+regret]
    return dict(status='Available', viable=viable, viable_count=len(viable), best=valid[0]['id'],
        rejected=[r['id'] for r in valid if r['id'] not in viable],
        validation_separation=metric(valid[1]['validation_error']-best if len(valid)>1 else None),
        hypotheses=valid, known_profile_recovered=metric(selected == known_profile if selected and known_profile else None,
            status=None if known_profile else 'NotComparable', reason=None if known_profile else 'Stock profile has no ground truth'),
        false_unique_claim=metric(bool(asserted_unique and selected != known_profile) if known_profile and selected else None),
        unsupported_unique_claim=bool(asserted_unique and len(viable)>1),
        truth_in_viable=metric(known_profile in viable if known_profile else None,
            status=None if known_profile else 'NotComparable'), regret_allowance=regret,
        claim='Conditional evidence under fixed hypotheses, not posterior probabilities or historical compiler identity')


def prediction_metrics(targets, predictions):
    """Area-weighted interval-distance loss, no gain fitting or implicit domain conversion.

    Intervals are in the declared target domain. Saturated upper=null means +inf.
    Missing predictions are reported as coverage, never replaced with black.
    """
    index = {}
    for p in predictions:
        if p['id'] in index: raise ValueError('Duplicate predicted observation')
        values = vector(p['rgb'])
        if min(values) < 0 or (p.get('domain') == 'EncodedRGB8' and max(values) > 255):
            raise ValueError('Lighting RGB outside declared comparison domain')
        index[p['id']] = p
    seen = set(); area = used = predicted = error = lum_error = weighted_channels = 0.
    raw = []; failures = Counter(); groups = defaultdict(lambda: [0., 0.])
    censored = total_channels = 0; luminance_weight = 0.
    for t in targets:
        if t['id'] in seen: raise ValueError('Duplicate target/alias must be deduplicated by controller')
        seen.add(t['id']); w = finite(t['weight'])
        if w <= 0: raise ValueError('Positive area/volume target weight')
        area += w; p = index.get(t['id']); total_channels += 3
        if t.get('used_in_fit'): used += w
        if p is None: failures['missing_prediction'] += 1; continue
        if (not t.get('comparison_id') or p.get('comparison_id') != t['comparison_id'] or
            p.get('domain') != t['domain'] or p.get('primaries') != t['primaries'] or
            (t['domain'] != 'EncodedRGB8' and t.get('primaries') in (None, 'Unknown'))):
            failures['not_comparable'] += 1; continue
        predicted += w; lo, hi = t['lower'], t['upper']; residual = []
        for x, a, b in zip(vector(p['rgb']), lo, hi):
            finite(a)
            if b is not None and finite(b) < a: raise ValueError('Invalid target interval')
            d = max(a-x, 0., x-b if b is not None else 0.)
            residual.append(d); censored += b is None
        if len(lo) != 3 or len(hi) != 3: raise ValueError('RGB interval shape')
        err = sum(d*d for d in residual)
        error += w*err; weighted_channels += 3*w
        # Interval of Y for nonnegative Rec709 coefficients; encoded RGB has no linear luminance metric.
        if t['domain'] != 'EncodedRGB8' and t['primaries'] == 'Rec709Linear':
            coeff = (.2126, .7152, .0722); y = sum(c*x for c, x in zip(coeff, p['rgb']))
            yl = sum(c*x for c, x in zip(coeff, lo)); yh = None if None in hi else sum(c*x for c, x in zip(coeff, hi))
            lum_error += w*max(yl-y, 0., y-yh if yh is not None else 0.)**2
            luminance_weight += w
        raw.append((w, err))
        for category in ('region', 'lighting', 'brightness'):
            key = category+'/'+str(t.get(category, 'Unknown')); groups[key][0] += w*err; groups[key][1] += 3*w
    domains = set(t['domain'] for t in targets)
    if len(domains) > 1: raise ValueError('Do not aggregate different photometric domains')
    if len({(t.get('comparison_id'), t['primaries']) for t in targets}) > 1:
        raise ValueError('Do not aggregate different calibration/colour spaces')
    return dict(rgb_interval_rmse=metric(math.sqrt(error/weighted_channels) if weighted_channels else None),
        linear_luminance_interval_rmse=metric(math.sqrt(lum_error/luminance_weight) if luminance_weight else None,
            status='NotComparable' if domains == {'EncodedRGB8'} else None,
            reason=None if luminance_weight else 'Luminance requires comparable linear Rec709 predictions'),
        prediction_coverage=ratio(predicted, area), observations_used_coverage=ratio(used, area),
        weight_unit='Declared receiver area for surfaces; declared volume/equal-record weight for separate grid stream',
        censored_channels_scored=censored, channels_in_requested_targets=total_channels,
        failures=dict(failures), extra_prediction_ids=len(set(index)-seen),
        breakdown={k: metric(math.sqrt(e/w), denominator=w) for k, (e, w) in sorted(groups.items())},
        claim='Error conditional on comparable predictions; coverage is an independent mandatory result')


def readiness(rows, reference_area=None):
    """Stream explicit quality predicates; unknowns are neither failures nor successes.
    Known-footprint area denominator cannot establish coverage of unknown area.
    """
    keys = ('raw_rgb', 'mapped', 'normal', 'footprint', 'comparable_colour', 'material_style',
            'visibility', 'region', 'grid_direction', 'deluxe', 'censored', 'unsupported', 'ambiguous')
    count = 0; processed = 0; total = 0.; unknown_area = 0; duplicate = 0; seen = set()
    groups = defaultdict(lambda: dict(count=0, area=0., true=Counter(), known=Counter(),
                                      true_count=Counter(), known_count=Counter()))
    normals = [[0.]*3 for _ in range(3)]; norm_weight = 0.; bounds = None; surfaces = set()
    for r in rows:
        processed += 1
        if processed > 4_000_000: raise ValueError('Readiness record limit')
        identity = r['id']
        if identity in seen: duplicate += 1; continue
        seen.add(identity); count += 1
        a = r.get('area'); a = finite(a) if a is not None else 0.
        if a < 0: raise ValueError('Negative area')
        if not a: unknown_area += 1
        total += a
        groups_to_add = ['map']+[f'{k}/{r[k]}' for k in ('region', 'roi', 'surface_type', 'material_style', 'brightness', 'page', 'height') if k in r]
        for key in groups_to_add:
            g = groups[key]; g['count'] += 1; g['area'] += a
            for quality in keys:
                value = r.get('quality', {}).get(quality)
                if value is not None:
                    if not isinstance(value, bool): raise ValueError('Tri-state quality predicate required')
                    g['known'][quality] += a
                    g['known_count'][quality] += 1
                    if value:
                        g['true'][quality] += a
                        g['true_count'][quality] += 1
        if r.get('position') is not None:
            p = vector(r['position'])
            if bounds is None: bounds = [list(p), list(p)]
            for i in range(3): bounds[0][i], bounds[1][i] = min(bounds[0][i], p[i]), max(bounds[1][i], p[i])
        if r.get('normal') is not None and a:
            n = vector(r['normal']); length = norm(n)
            if length <= 1e-12: raise ValueError('Invalid normal')
            for i in range(3):
                for j in range(3): normals[i][j] += a*n[i]*n[j]/(length*length)
            norm_weight += a
        if r.get('surface') is not None: surfaces.add(r['surface'])
    report = {}
    for key, g in sorted(groups.items()):
        report[key] = dict(count=g['count'], known_footprint_area=g['area'], qualities={
            q: dict(covered_area=g['true'][q], classified_area=g['known'][q],
                    true_count=g['true_count'][q], classified_count=g['known_count'][q],
                    fraction_of_known_footprint=ratio(g['true'][q], g['area']),
                    unknown_predicate_area=g['area']-g['known'][q]) for q in keys})
    if reference_area is not None and finite(reference_area) <= 0: raise ValueError('Reference area')
    return dict(candidate_count=count, duplicate_records=duplicate, samples_without_area=unknown_area,
        known_footprint_area=total, map_area_coverage=ratio(total, reference_area) if reference_area else
            metric(status='NotComparable', reason='Full receiver-area denominator unavailable; do not invent padding area'),
        groups=report, normal_second_moment_eigenvalues=eigenvalues_symmetric(
            [[x/norm_weight if norm_weight else 0. for x in row] for row in normals]),
        receiver_bounds=bounds, distinct_surfaces=len(surfaces),
        information_claim='Geometry proxies only; no source-specific Fisher information or independence claim')


def visibility_metrics(records):
    confusion = Counter(); leak = 0.; known_weight = 0.; unknown = 0
    for r in records:
        if r['truth'] not in ('Visible', 'Blocked', 'Unknown') or r['predicted'] not in ('Visible', 'Blocked', 'Unknown'):
            raise ValueError('Three-state visibility required')
        confusion[r['truth']+'->'+r['predicted']] += 1
        if r['truth'] == 'Unknown' or r['predicted'] == 'Unknown': unknown += 1; continue
        w = finite(r.get('weight', 1.)); known_weight += w
        if r['truth'] == 'Blocked': leak += max(0., finite(r.get('predicted_energy', 0.)))*w
    correct = sum(confusion[k+'->'+k] for k in ('Visible', 'Blocked'))
    return dict(confusion=dict(confusion), accuracy_on_known=ratio(correct, len(records)-unknown),
        unknown_count=unknown, wall_leaked_energy=metric(leak if known_weight else None),
        caveat='Region/PVS connectivity is never accepted as a visibility label')


def shadow_metrics(records, units_per_metre=None):
    edges = []; width = []; association = []; boundary = []
    for r in records:
        if r.get('edge_truth') is not None and r.get('edge_predicted') is not None:
            edges.append(abs(finite(r['edge_predicted'])-finite(r['edge_truth'])))
        if r.get('width_truth') is not None and r.get('width_predicted') is not None:
            width.append(abs(finite(r['width_predicted'])-finite(r['width_truth'])))
        if r.get('association_correct') is not None: association.append(not r['association_correct'])
        if r.get('boundary_interval') is not None:
            boundary.append(dict(truth=r.get('edge_truth'), interval=r['boundary_interval'], nominal=r.get('nominal')))
    if units_per_metre is not None and finite(units_per_metre) <= 0: raise ValueError('World scale')
    return dict(edge_error=mean(edges, 'world'), penumbra_width_error=mean(width, 'world'),
                edge_error_metres=mean([x/units_per_metre for x in edges] if units_per_metre else [], 'metres'),
                false_association_rate=ratio(sum(association), len(association)),
                boundary_intervals=interval_coverage(boundary),
                missing_edges=sum(r.get('edge_predicted') is None for r in records))


def attribution(records):
    names = ('direct', 'sun', 'sky', 'bounce', 'ambient', 'discrepancy')
    errors = defaultdict(list); duplicate_roots = 0
    for r in records:
        roots = r.get('included_roots', [])
        duplicate_roots += len(roots)-len(set(roots))
        for key in names:
            if key in r.get('truth', {}) and key in r.get('prediction', {}):
                errors[key].append(abs(finite(r['truth'][key])-finite(r['prediction'][key])))
    return dict(component_absolute_error={k: mean(errors[k], 'Declared common linear quantity') for k in names},
                duplicate_root_references=duplicate_roots,
                claim='Requires component truth; similar RGB never establishes energy attribution')


def scale_feasible_set(records):
    """Intersection of positive gain constraints, NOT a statistical confidence interval.
    Never fit a gain on held-out observations or silently erase intensity mismatch.
    """
    lower, upper = 0., math.inf
    if not records: return metric(reason='No training-only gain constraints')
    for r in records:
        a = finite(r['prediction'])
        if a <= 0: raise ValueError('Positive scale reference required')
        lo, hi = map(finite, r['target_interval'])
        if lo < 0 or lo > hi: raise ValueError('Invalid gain constraint')
        lower, upper = max(lower, lo/a), min(upper, hi/a)
    return metric([lower, upper] if lower <= upper else [], unit='scene gain',
                  meaning='Deterministic feasible bounds under declared interval errors, not physical calibration')


def directional_metrics(records):
    errors = []; excess = []; missing = 0
    for r in records:
        if r.get('truth') is None or r.get('prediction') is None:
            missing += 1; continue
        if r.get('meaning') not in ('receiver_to_source', 'propagation'):
            raise ValueError('Explicit direction convention required')
        e = angle(r['truth'], r['prediction']); errors.append(e)
        excess.append(max(0., e-finite(r.get('quantization_bound_degrees', 0.))))
    return dict(angular_error=mean(errors, 'degrees'), above_encoding_bound=mean(excess, 'degrees'),
                missing=missing, claim='Observation operator evidence, not physical source direction certification')


def reconstruction_coverage(regions):
    states = ('physical_resolved', 'approximate_indirect_only', 'unresolved')
    area = sum(finite(r['area']) for r in regions)
    if any(r['area'] < 0 or r['state'] not in states for r in regions): raise ValueError('Coverage state/area')
    return {state: ratio(sum(r['area'] for r in regions if r['state'] == state), area) for state in states}


def placement(records):
    return dict(certified_solid_sources=sum(r.get('space') == 'Solid' for r in records),
        unknown_space_sources=sum(r.get('space') in (None, 'Unknown') for r in records),
        wrong_receiver_room=sum(r.get('wrong_room_proven', False) for r in records),
        near_wall_clearance_error=mean([abs(r['truth_clearance']-r['predicted_clearance']) for r in records
            if r.get('truth_clearance') is not None and r.get('predicted_clearance') is not None], 'world'),
        caveat='Only certified solids / externally proven wrong room; disconnected regions alone are not failures')


def joint_position_coverage(records):
    covered, widths = [], []; unresolved = 0
    for r in records:
        if r.get('truth') is None or r.get('estimate') is None or r.get('radius_world') is None:
            unresolved += 1; continue
        radius = finite(r['radius_world'])
        if radius < 0: raise ValueError('Negative position bound')
        covered.append(distance(r['truth'], r['estimate']) <= radius); widths.append(2*radius)
    return dict(joint_ball_coverage=ratio(sum(covered), len(covered)), diameter_world=mean(widths),
                unresolved=unresolved, supported_truth_count=len(records),
                resolved_fraction=ratio(len(covered), len(records)),
                nominal_levels=sorted(set(r['nominal'] for r in records if r.get('nominal') is not None)))


def type_calibration(records):
    scores = []; correct = 0; n = 0; unavailable = 0
    for r in records:
        if r.get('meaning') != 'probability' or not r.get('calibration_protocol') or r.get('truth') is None:
            unavailable += 1; continue
        probs = r['probabilities']
        if not probs or any(finite(v) < 0 or v > 1 for v in probs.values()) or abs(sum(probs.values())-1) > 1e-9:
            raise ValueError('Categorical probabilities must sum to one')
        scores.append(sum((v-int(k == r['truth']))**2 for k, v in probs.items()) + (0 if r['truth'] in probs else 1))
        correct += max(probs, key=probs.get) == r['truth']; n += 1
    return dict(multiclass_brier=mean(scores), top_type_accuracy=ratio(correct, n), unavailable=unavailable)


def ablation_pairs(runs):
    """Compare records from an external solver harness, never run/fabricate ablations."""
    controls = ('lightgrid', 'materials', 'visibility', 'shadows', 'indirect', 'emissive_priors')
    pairs = []
    for i, a in enumerate(runs):
        for b in runs[i+1:]:
            if any(a.get(k) != b.get(k) for k in ('dataset_hash', 'split_hash', 'policy_hash', 'seed', 'resource_budget')):
                continue
            changed = [key for key in controls if a['features'][key] != b['features'][key]]
            if len(changed) == 1:
                pairs.append(dict(a=a['id'], b=b['id'], changed=changed[0],
                    metrics_a=a['metrics'], metrics_b=b['metrics']))
    return dict(status='Available' if pairs else 'Unavailable', pairs=pairs,
                missing_comparisons=sorted(set(controls)-{p['changed'] for p in pairs}),
                claim='Paired feature ablations under identical data/splits/budgets; not causal evidence across unmatched runs')
