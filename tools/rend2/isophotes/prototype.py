"""Bounded planar scalar-field cues. No truth, world ray queries or renderer API.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import itertools
import math
import statistics


def solve(a, b):
    """Tiny pivoted linear solve; rank failure stays unavailable."""
    n = len(b)
    m = [list(map(float, row))+[float(value)] for row, value in zip(a, b)]
    scale = max((abs(v) for row in a for v in row), default=0)
    for k in range(n):
        pivot = max(range(k, n), key=lambda i: abs(m[i][k]))
        if abs(m[pivot][k]) <= max(1e-12, scale*1e-12):
            return None
        m[k], m[pivot] = m[pivot], m[k]
        q = m[k][k]
        m[k] = [v/q for v in m[k]]
        for i in range(n):
            if i != k:
                q = m[i][k]
                m[i] = [v-q*w for v, w in zip(m[i], m[k])]
    return [m[i][-1] for i in range(n)]


def least_squares(rows, values):
    if not rows:
        return None
    n = len(rows[0])
    return solve([[sum(r[i]*r[j] for r in rows) for j in range(n)] for i in range(n)],
                 [sum(r[i]*v for r, v in zip(rows, values)) for i in range(n)])


def circle(points):
    # Center coordinates before fitting to avoid large world-coordinate offsets.
    ox = statistics.mean(p[0] for p in points)
    oy = statistics.mean(p[1] for p in points)
    local = [(x-ox, y-oy) for x, y in points]
    fit = least_squares([[2*x, 2*y, 1] for x, y in local], [x*x+y*y for x, y in local])
    if fit is None or fit[2]+fit[0]**2+fit[1]**2 <= 0:
        return None
    a, b = fit[0]+ox, fit[1]+oy
    radius = math.sqrt(fit[2]+fit[0]**2+fit[1]**2)
    rms = math.sqrt(statistics.mean((math.hypot(x-a, y-b)-radius)**2 for x, y in points))
    bins = {int(((math.atan2(y-b, x-a)+math.pi)/(2*math.pi)*12)) % 12 for x, y in points}
    return dict(projection=[a, b], radius=radius, curvature_world_inverse=1/radius,
                relative_rms=rms/radius, angular_bins=len(bins))


def response(uv, source):
    a, b, h = source
    return h/((uv[0]-a)**2+(uv[1]-b)**2+h*h)**1.5


def prediction_error(train, heldout, source):
    coeff = [response(r['uv'], source) for r in train]
    gain = max(0, sum(q*r['value'] for q, r in zip(coeff, train))/sum(q*q for q in coeff))
    span = max(r['value'] for r in train)-min(r['value'] for r in train)
    rms = math.sqrt(statistics.mean((r['value']-gain*response(r['uv'], source))**2 for r in heldout))
    return rms/max(1e-12, span)


def gradient(grid, us, vs, policy):
    equations, values, log_samples = [], [], []
    for (i, j), row in grid.items():
        keys = [(i-1, j), (i+1, j), (i, j-1), (i, j+1)]
        if not all(k in grid for k in keys):
            continue
        left, right, down, up = [grid[k]['value'] for k in keys]
        if min(left, right, down, up, row['value']) <= 0:
            continue
        gx, gy = (right-left)/(us[i+1]-us[i-1]), (up-down)/(vs[j+1]-vs[j-1])
        length = math.hypot(gx, gy)
        noise = math.hypot(1/(us[i+1]-us[i-1]), 1/(vs[j+1]-vs[j-1])) if row.get('quantized') else 0
        if length <= max(1e-10, noise*policy['gradient_quantization_error_multiple']):
            continue
        # The gradient is a tangent-plane vector, not a 3D incoming-light ray.
        nx, ny = -gy/length, gx/length
        u, v = row['uv']
        equations.append([nx, ny]); values.append(nx*u+ny*v)
        log_samples.append((u, v, gx/row['value'], gy/row['value']))
    xx, xy, yy = (sum(r[i]*r[j] for r in equations) for i, j in [(0, 0), (0, 1), (1, 1)])
    trace = xx+yy
    eigen_ratio = (trace-math.sqrt((xx-yy)**2+4*xy*xy))/(2*trace) if trace else 0
    projection = least_squares(equations, values)
    if projection is None or len(equations) < policy['minimum_gradient_stencils'] or eigen_ratio < policy['minimum_gradient_eigenvalue_ratio']:
        return dict(state='Unavailable', reason='Gradient direction rank/contrast')
    heights = []
    for u, v, gx, gy in log_samples:
        dx, dy = projection[0]-u, projection[1]-v
        height2 = 3*(dx*gx+dy*gy)/(gx*gx+gy*gy)-dx*dx-dy*dy
        if height2 > 0:
            heights.append(math.sqrt(height2))
    height = statistics.median(heights) if heights else None
    if height is not None and not policy['height_world'][0] < height < policy['height_world'][1]:
        height = None
    return dict(state='Conditional', projection=projection, height=height, stencils=len(equations),
                minimum_eigenvalue_over_trace=eigen_ratio,
                height_assumption='Single unoccluded inverse-square point; linear scalar response')


def isophotes(grid, policy):
    low, high = min(r['value'] for r in grid.values()), max(r['value'] for r in grid.values())
    contours, rejected = [], []
    for fraction in policy['isophote_levels']:
        level = low+fraction*(high-low)
        points = []
        for (i, j), r in grid.items():
            for other in [(i+1, j), (i, j+1)]:
                if other not in grid:
                    continue
                s = grid[other]
                if (r['value'] <= level < s['value']) or (s['value'] <= level < r['value']):
                    t = (level-r['value'])/(s['value']-r['value'])
                    points.append([a+t*(b-a) for a, b in zip(r['uv'], s['uv'])])
        if len(points) < policy['minimum_contour_points']:
            continue
        c = circle(points)
        if c:
            item = dict(level=level, points=len(points), **c)
            if c['relative_rms'] > policy['maximum_circle_relative_rms'] or c['angular_bins'] < policy['minimum_angular_bins_of_12']:
                rejected.append(item)
            else:
                contours.append(item)
    if len(contours) < policy['minimum_contours']:
        return dict(state='Unavailable', reason='Insufficient isophote support', contours=contours, rejected_contours=rejected)
    projection = [statistics.mean(c['projection'][k] for c in contours) for k in range(2)]
    spread = max(math.dist(c['projection'], projection) for c in contours)
    # Radius/intensity relation supplies depth only under the additional physical law.
    fit = least_squares([[c['radius']**2, 1] for c in contours], [c['level']**(-2/3) for c in contours])
    height = math.sqrt(fit[1]/fit[0]) if fit and fit[0] > 0 and fit[1] > 0 else None
    if height is not None and not policy['height_world'][0] < height < policy['height_world'][1]:
        height = None
    return dict(state='Conditional', projection=projection, height=height, contours=contours, rejected_contours=rejected,
                center_spread=spread, height_assumption='Known linear inverse-square radial intensity law')


def forward_fit(train, policy):
    bounds = [[min(r['uv'][k] for r in train)-policy['projection_search_margin_world'],
               max(r['uv'][k] for r in train)+policy['projection_search_margin_world']] for k in range(2)]
    bounds.append(policy['height_world'])
    count = policy['forward_coarse_axis']
    steps = [(hi-lo)/(count-1) for lo, hi in bounds]
    best, work = None, 0

    def trial(source):
        nonlocal best, work
        if not all(lo <= s <= hi for s, (lo, hi) in zip(source, bounds)):
            return
        work += 1
        if work > policy['forward_max_evaluations']:
            raise ValueError('Forward-fit work limit')
        q = [response(r['uv'], source) for r in train]
        gain = max(0, sum(a*r['value'] for a, r in zip(q, train))/sum(a*a for a in q))
        error = sum((r['value']-gain*a)**2 for r, a in zip(train, q))
        key = (error, tuple(source))
        if best is None or key < best[0]:
            best = (key, list(source))

    for indices in itertools.product(range(count), repeat=3):
        trial([bounds[k][0]+indices[k]*steps[k] for k in range(3)])
    for _ in range(policy['forward_refinement_rounds']):
        steps = [s/2 for s in steps]
        center = best[1][:]
        for offsets in itertools.product((-1, 0, 1), repeat=3):
            trial([center[k]+offsets[k]*steps[k] for k in range(3)])
    return dict(state='Conditional', projection=best[1][:2], height=best[1][2], evaluations=work,
                bound_hit=any(abs(s-lo) < 1e-8 or abs(s-hi) < 1e-8 for s, (lo, hi) in zip(best[1], bounds)),
                search_complete=False)


def analyze(rows, policy):
    if not rows or len(rows) > policy['max_samples']:
        raise ValueError('Receiver count unavailable/outside bounded study')
    if any(not math.isfinite(v) for r in rows for v in [*r['uv'], r['value']]) or any(r['value'] < 0 for r in rows):
        raise ValueError('Invalid receiver values')
    us, vs = [sorted({r['uv'][k] for r in rows}) for k in range(2)]
    uindex, vindex = {x: i for i, x in enumerate(us)}, {y: j for j, y in enumerate(vs)}
    train, heldout, grid = [], [], {}
    for r in rows:
        i, j = uindex[r['uv'][0]], vindex[r['uv'][1]]
        if (i//2+j//2) % 4 == 0:
            heldout.append(r)
        else:
            train.append(r); grid[i, j] = r
    if len(grid) != len(train):
        raise ValueError('Ambiguous duplicate receiver coordinate')
    result = dict(training=len(train), heldout=len(heldout), methods={}, domain='RelativeOrCodeProxy_NoCalibration')
    if len(train) < policy['min_training_samples'] or not heldout:
        return dict(result, state='Unavailable', reason='Insufficient held-out sample support')
    # Admission metadata/geometry gates do not inspect held-out target values.
    reasons = []
    if len({r['material'] for r in rows}) != 1: reasons.append('Material seam')
    if any(r.get('normal_mismatch', False) for r in rows): reasons.append('Changing normals')
    if any(r.get('saturated', False) for r in train): reasons.append('Saturated training sample')
    low, high = min(r['value'] for r in train), max(r['value'] for r in train)
    if (high-low)/max(high, 1e-12) < policy['minimum_relative_contrast']: reasons.append('Insufficient contrast')
    pitch = statistics.median([b-a for axis in (us, vs) for a, b in zip(axis, axis[1:])])
    for name, fn in [('gradient', lambda: gradient(grid, us, vs, policy)),
                     ('isophote', lambda: isophotes(grid, policy)),
                     ('forward', lambda: forward_fit(train, policy))]:
        m = fn(); failures = reasons[:]
        if m.get('projection') is not None and m.get('height') is not None:
            m['heldout_relative_rms'] = prediction_error(train, heldout, [*m['projection'], m['height']])
            if m['heldout_relative_rms'] > policy['maximum_heldout_relative_rms']:
                failures.append('Single-point held-out discrepancy')
        if name == 'isophote' and 'center_spread' in m:
            if m['center_spread'] > pitch*policy['maximum_center_spread_pitches']:
                failures.append('Level-dependent centers')
            if any(c['relative_rms'] > policy['maximum_circle_relative_rms'] or
                   c['angular_bins'] < policy['minimum_angular_bins_of_12'] for c in m['contours']):
                failures.append('Noncircular/short isophote arcs')
        if m.get('bound_hit'): failures.append('Search bound reached')
        m['admission'] = 'Unavailable' if failures or m['state'] == 'Unavailable' else 'Conditional'
        m['failures'] = failures
        # Height remains a model-conditioned numerical diagnostic even when
        # its small residual passes. Gamma and transport counterexamples pass too.
        m['physical_depth_available'] = False
        m['type_or_orientation_available'] = False
        m['primary_or_transport_ambiguous'] = True
        result['methods'][name] = m
    return dict(result, state='Measured', receiver_pitch=pitch,
                physical_source_certified=False, orientation_available=False)
