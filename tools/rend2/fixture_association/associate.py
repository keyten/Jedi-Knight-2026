"""ASR15: bounded offline fixture/illumination compatibility and assignment.

All colour and gradient comparisons are relative code-domain cues. Scores are
heuristics, not probabilities or irradiance fits. No historical lamp is certified.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math

SCHEMA = 'org.openjk.fixture-associations.v1'
WEIGHTS = dict(proximity=.08, orientation=.08, receiver_side=.06, colour=.10,
               gradient=.18, grid_direction=.18, projection=.12,
               visibility=.12, dark_receiver=.08)
POLICY = dict(distance_scale_world=256., direction_gate_degrees=45.,
              fixture_penalty=.04, source_penalty=.03, association_penalty=.02,
              minimum_score=.65, ambiguity_margin=.04, dark_radius_world=64.,
              max_fixtures=1024, max_observations=256, max_samples=4096,
              max_dark_samples=4096, max_pairs=16384, max_pair_samples=131072,
              max_work=4000000)
UNCERTAINTY = ['Aesthetic direct-source compatibility only; historical participation unknown',
               'RGB8, texture colour and tangent gradients are uncalibrated relative cues',
               'Reflectance, filtering, indirect transport and overlapping light remain alternatives',
               'Greedy penalized coverage is a bounded approximation, not an optimal or unique decomposition']


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def norm(a):
    return math.sqrt(dot(a, a))


def unit(a):
    return [x/norm(a) for x in a] if a is not None and norm(a) > 1e-10 else None


def clamp(x):
    return min(1., max(0., x))


def vector(v, name, optional=False, nonnegative=False):
    if v is None and optional:
        return
    if (not isinstance(v, (list, tuple)) or len(v) != 3 or
            any(isinstance(x, bool) or not isinstance(x, (int, float)) or
                not math.isfinite(x) or abs(x) > 1e7 or (nonnegative and x < 0) for x in v)):
        raise ValueError('Invalid '+name)


def identity(value):
    return isinstance(value, str) and bool(value)


class Budget:
    def __init__(self, maximum):
        self.maximum, self.work = maximum, 0

    def use(self, count=1):
        self.work += count
        if self.work > self.maximum:
            raise ValueError('ASR15 work limit; no partial assignment')


def validate(fixtures, observations, policy):
    if set(policy) != set(POLICY):
        raise ValueError('Unknown/missing ASR15 policy')
    for k, value in policy.items():
        if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(value) or value < 0 or
                (k.startswith('max_') and (not isinstance(value, int) or value < 1))):
            raise ValueError('Invalid ASR15 policy: '+k)
    if (policy['distance_scale_world'] <= 0 or policy['dark_radius_world'] <= 0 or
            not 0 < policy['direction_gate_degrees'] <= 180 or
            not 0 <= policy['minimum_score'] < 1 or policy['ambiguity_margin'] > 1):
        raise ValueError('Invalid ASR15 numerical policy')
    if len(fixtures) > policy['max_fixtures'] or len(observations) > policy['max_observations']:
        raise ValueError('ASR15 fixture/observation limit')
    if len(fixtures)*len(observations) > policy['max_pairs']:
        raise ValueError('ASR15 pair limit; select an explicit spatial ROI')
    if len({f['id'] for f in fixtures}) != len(fixtures) or len({o['id'] for o in observations}) != len(observations):
        raise ValueError('Duplicate fixture/observation identity')
    total, dark = 0, 0
    for f in fixtures:
        if not identity(f['id']):
            raise ValueError('Invalid fixture identity')
        vector(f['position'], 'fixture position')
        vector(f.get('outward'), 'fixture outward', True)
        vector(f.get('colour'), 'fixture colour', True, True)
        r = f.get('radius_world')
        if r is not None and (isinstance(r, bool) or not isinstance(r, (int, float)) or not math.isfinite(r) or not 0 <= r <= 1e7):
            raise ValueError('Invalid fixture size')
        if not isinstance(f.get('static_eligible'), bool):
            raise ValueError('Explicit static fixture eligibility required')
    for o in observations:
        if not identity(o['id']) or not o['samples']:
            raise ValueError('Invalid/empty observation')
        ids = set()
        for s in o['samples']+o.get('dark_samples', []):
            if not identity(s['id']) or s['id'] in ids:
                raise ValueError('Missing/duplicate support identity within observation')
            ids.add(s['id'])
            vector(s['position'], 'support position')
            for k in ('normal', 'gradient', 'incoming'):
                vector(s.get(k), k, True)
            vector(s.get('colour'), 'support colour', True, True)
            if s.get('rgb8') is not None:
                vector(s['rgb8'], 'RGB8', nonnegative=True)
                if any(not isinstance(c, int) or c > 255 for c in s['rgb8']):
                    raise ValueError('Expected original RGB8 codes')
            if not identity(s.get('correlation_key', s['id'])):
                raise ValueError('Invalid support correlation key')
        projection = o.get('projection')
        if projection is not None:
            vector(projection['foot'], 'projection foot')
            vector(projection['normal'], 'projection normal')
            if (not math.isfinite(projection['radius_world']) or not 0 < projection['radius_world'] <= 1e7 or
                    unit(projection['normal']) is None):
                raise ValueError('Invalid projection tolerance/normal')
        total += len(o['samples'])
        dark += len(o.get('dark_samples', []))
    if total > policy['max_samples'] or dark > policy['max_dark_samples']:
        raise ValueError('ASR15 support limit; no silent target-dependent crop')
    if total*len(fixtures) > policy['max_pair_samples']:
        raise ValueError('ASR15 pair-support limit')


def term(value, reason):
    return dict(value=value, state='unknown' if value is None else 'conditional', reason=reason)


def visibility(geometry, a, b, budget):
    budget.use()
    state = geometry.ray(a, b) if geometry is not None else 'unknown'
    if state not in ('clear', 'blocked', 'unknown'):
        raise ValueError('Invalid geometry state; cannot assume clear')
    return state


def dark_term(f, sample, observation, geometry, policy, budget):
    """Conditional contrast-order check on matched material/style controls only.

    Inverse square/cosine is a hypothetical geometric ordering, never an inverse
    of observed bytes. Only comparably exposed, nearby known controls can object.
    """
    rgb = sample.get('rgb8')
    if (rgb is None or max(rgb) == 255 or sum(rgb)/3 < 64 or
            not sample.get('material') or sample.get('style') is None or not unit(sample.get('normal'))):
        return term(None, 'No uncensored bright receiver with matched material/style')
    def response(s):
        d = sub(f['position'], s['position'])
        n = unit(s.get('normal'))
        return max(0., dot(n, unit(d))) / max(1., dot(d, d)) if n and unit(d) else 0.
    bright = response(sample)
    if bright <= 0:
        return term(None, 'No supported geometric exposure ordering')
    values = []
    for d in observation.get('dark_samples', []):
        budget.use()
        codes = d.get('rgb8')
        if (codes is None or max(codes) > 32 or d.get('material') != sample['material'] or
                d.get('style') != sample['style'] or not unit(d.get('normal')) or
                dot(unit(d['normal']), unit(sample['normal'])) < .99 or
                norm(sub(d['position'], sample['position'])) > policy['dark_radius_world']):
            continue
        ray = visibility(geometry, d['position'], f['position'], budget)
        if ray == 'blocked':
            values.append(1.)
        elif ray == 'clear':
            values.append(1-clamp((response(d)/bright-.4)/.6))
    return term(min(values) if values else None,
                'Matched nearby dark code controls under hypothetical exposure ordering' if values
                else 'No matched dark control with known visibility')


def compatibility(f, s, observation, geometry, policy, budget):
    budget.use()
    delta = sub(f['position'], s['position'])
    distance, direction = norm(delta), unit(delta)
    radius = f.get('radius_world')
    # Size only softens angular discrimination; unknown size is not zero size.
    slack = min(math.radians(30), math.asin(min(1., radius/max(distance, 1e-9)))) if radius is not None else 0.
    terms = {}
    terms['proximity'] = term(1/(1+(max(0., distance-radius)/policy['distance_scale_world'])**2)
                              if radius is not None else None,
                              'Distance to bounded luminous support' if radius is not None else 'Luminous extent unavailable')
    outward, normal = unit(f.get('outward')), unit(s.get('normal'))
    terms['orientation'] = term(clamp(-dot(outward, direction)) if outward and direction else None,
                                'Single outward fixture hypothesis' if outward else 'Emitter orientation unresolved/two-sided')
    terms['receiver_side'] = term(clamp(dot(normal, direction)) if normal and direction else None,
                                  'Receiver geometric normal half-space' if normal else 'No geometric receiver normal')
    a, b = unit(f.get('colour')), unit(s.get('colour'))
    terms['colour'] = term(clamp((dot(a, b)-.5)/.5) if a and b else None,
                           'Relative colour affinity; texture/transport/transfer ambiguity' if a and b else 'Colour prior unavailable')
    gradient = unit(s.get('gradient'))
    tangent = unit(sub(delta, [x*dot(delta, normal) for x in normal])) if normal else None
    g = max(0., math.acos(max(-1., min(1., dot(gradient, tangent))))-slack) if gradient and tangent else None
    terms['gradient'] = term(clamp(1-g/(math.pi/2)) if g is not None else None,
                             'Tangent code-gradient direction; radial/local-response assumption' if g is not None else 'Reliable tangent gradient unavailable')
    incoming = unit(s.get('incoming'))
    angle = max(0., math.acos(max(-1., min(1., dot(incoming, direction))))-slack) if incoming and direction else None
    terms['grid_direction'] = term(clamp(1-angle/math.radians(policy['direction_gate_degrees'])) if angle is not None else None,
                                   'Aggregate original grid incoming direction; mixture/packing ambiguity' if angle is not None else 'Independent original grid direction unavailable')
    projection = observation.get('projection')
    if projection:
        n = unit(projection['normal'])
        d = sub(f['position'], projection['foot'])
        error = norm(sub(d, [v*dot(d, n) for v in n]))
        tolerance = projection['radius_world']+(radius if radius is not None else 0.)
        terms['projection'] = term(1/(1+(error/tolerance)**2), 'Conditional ASR14 normal-line distance; depth unconstrained')
    else:
        terms['projection'] = term(None, 'No supported source projection')
    ray = visibility(geometry, s['position'], f['position'], budget)
    terms['visibility'] = term({'clear': 1., 'blocked': 0., 'unknown': None}[ray], 'Known static line of sight: '+ray)
    terms['dark_receiver'] = dark_term(f, s, observation, geometry, policy, budget)
    known = sum(WEIGHTS[k] for k, t in terms.items() if t['value'] is not None)
    weighted = sum(WEIGHTS[k]*t['value'] for k, t in terms.items() if t['value'] is not None)
    score = weighted/known if known else None
    objections = []
    if not f['static_eligible']:
        objections.append('Fixture not attested eligible static geometry')
    if ray == 'blocked':
        objections.append('Known occlusion contradicts this direct association; bounce remains possible')
    if outward and direction and -dot(outward, direction) < -.05:
        objections.append('Receiver behind known emitting face')
    if normal and direction and dot(normal, direction) < -.05:
        objections.append('Fixture behind geometric receiver half-space')
    discriminant = any(terms[k]['value'] is not None and terms[k]['value'] >= .65
                       for k in ('gradient', 'grid_direction', 'projection'))
    eligible = not objections and ray == 'clear' and discriminant and score >= policy['minimum_score']
    utility = known*max(0., (score-policy['minimum_score'])/(1-policy['minimum_score'])) if eligible else 0.
    return dict(id=s['id'], correlation_key=s.get('correlation_key', s['id']), terms=terms,
                score=score, known_weight=known, unconstrained_score_bounds=[weighted, weighted+1-known],
                unknown_terms=sorted(k for k, t in terms.items() if t['value'] is None),
                objections=objections, eligible=eligible, utility=utility,
                uncertainty=['Unknown luminous size weakens angular interpretation'] if radius is None else [])


def associate(fixtures, observations, geometry=None, policy=None):
    """Deterministic many-to-many greedy facility coverage; all pairs retained.

    Objective = sum_observation mean_group mean_support max_selected_link utility
                - active_fixture_cost - active_source_cost - selected_link_cost.
    Every physical fixture currently supplies one conditional source hypothesis.
    Asset-template source IDs are deliberately NOT shared between placements.
    """
    policy = dict(POLICY if policy is None else policy)
    validate(fixtures, observations, policy)
    budget = Budget(policy['max_work'])
    fixtures = sorted(fixtures, key=lambda f: f['id'])
    observations = sorted(observations, key=lambda o: o['id'])
    edges, weights = [], {}
    for o in observations:
        samples = sorted(o['samples'], key=lambda s: s['id'])
        groups = {}
        for s in samples:
            key = s.get('correlation_key', s['id'])
            groups[key] = groups.get(key, 0)+1
        for s in samples:
            weights[o['id'], s['id']] = 1/(len(groups)*groups[s.get('correlation_key', s['id'])])
        for f in fixtures:
            support = [compatibility(f, s, o, geometry, policy, budget) for s in samples]
            known = sum(weights[o['id'], s['id']]*s['known_weight'] for s in support)
            weighted = sum(weights[o['id'], s['id']]*s['known_weight']*s['score']
                           for s in support if s['score'] is not None)
            score = weighted/known if known else None
            edges.append(dict(fixture_id=f['id'], source_hypothesis_id=f['id']+'/conditional-emission',
                              observation_id=o['id'], score=score, known_weight=known,
                              unconstrained_score_bounds=[sum(weights[o['id'], s['id']]*s['unconstrained_score_bounds'][a]
                                                              for s in support) for a in (0, 1)],
                              unknown_terms=sorted({k for s in support for k in s['unknown_terms']}), supports=support,
                              selected=False, status='ambiguous', reasons=[],
                              uncertainty=sorted(set(UNCERTAINTY+f.get('uncertainty', [])+o.get('uncertainty', []))),
                              historical_source_certified=False))
    best = {k: 0. for k in weights}
    active, selected, objective = set(), [], 0.
    def gain(edge, current, activated):
        benefit = 0.
        for s in edge['supports']:
            budget.use()
            k = edge['observation_id'], s['id']
            benefit += weights[k]*max(0., s['utility']-current[k])
        cost = policy['association_penalty']
        if edge['fixture_id'] not in activated:
            cost += policy['fixture_penalty']+policy['source_penalty']
        return benefit-cost
    while True:
        choices = [(gain(e, best, active), e['fixture_id'], e['observation_id'], i)
                   for i, e in enumerate(edges) if not e['selected']]
        if not choices:
            break
        # Stable IDs settle exact ties without claiming physical uniqueness.
        value, _, _, index = min(choices, key=lambda c: (-c[0], c[1], c[2]))
        if value <= 1e-12:
            break
        e = edges[index]
        e['selected'] = True
        e['selection_gain'] = value
        objective += value
        active.add(e['fixture_id'])
        selected.append(index)
        for s in e['supports']:
            k = e['observation_id'], s['id']
            best[k] = max(best[k], s['utility'])
    # A local replacement margin exposes equal/near-equal physical alternatives.
    for i, e in enumerate(edges):
        available = any(s['eligible'] for s in e['supports'])
        if e['selected']:
            without = {k: 0. for k in weights}
            other_active = {edges[j]['fixture_id'] for j in selected if j != i}
            for j in selected:
                if j == i:
                    continue
                for s in edges[j]['supports']:
                    budget.use()
                    k = edges[j]['observation_id'], s['id']
                    without[k] = max(without[k], s['utility'])
            own = gain(e, without, other_active)
            alternatives = [gain(a, without, other_active) for a in edges
                            if not a['selected'] and a['observation_id'] == e['observation_id']]
            margin = own-max(alternatives) if alternatives else None
            e['replacement_margin'] = margin
            e['status'] = 'ambiguous' if margin is not None and margin <= policy['ambiguity_margin'] else 'selected_conditional'
            e['reasons'] = ['Positive penalized coverage gain', 'Near-equal replacement retained'] if e['status'] == 'ambiguous' else ['Positive penalized coverage gain']
        elif available:
            e['status'] = 'ambiguous'
            e['reasons'] = ['Compatible alternative does not add enough distinct support after complexity penalties']
        elif any(not s['objections'] and (s['unknown_terms'] or
                 (s['score'] is not None and s['score'] >= policy['minimum_score'])) for s in e['supports']):
            e['reasons'] = ['Insufficient independent structure or known line of sight for assignment']
        else:
            e['status'] = 'rejected'
            e['reasons'] = sorted({r for s in e['supports'] for r in s['objections']}) or ['Known compatibility below threshold']
    edges.sort(key=lambda e: (-(e['score'] if e['score'] is not None else -1.), e['fixture_id'], e['observation_id']))
    for rank, e in enumerate(edges, 1):
        e['rank'] = rank
    return dict(schema=SCHEMA, policy=policy, term_weights=dict(WEIGHTS), associations=edges,
                objective=objective, work=budget.work,
                active_fixtures=sorted(active), active_sources=sorted(f+'/conditional-emission' for f in active),
                unassociated_fixtures=[f['id'] for f in fixtures if f['id'] not in active],
                unmatched_observations=[o['id'] for o in observations if not any(e['selected'] and e['observation_id'] == o['id'] for e in edges)],
                unexplained_supports=[dict(observation_id=k[0], support_id=k[1]) for k in sorted(best) if best[k] == 0],
                uncertainty=list(UNCERTAINTY), historical_source_certified=False)
