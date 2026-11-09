"""ASR14 source-agnostic features from original mapped receiver observations.

Only the existing offline luxel export enters this module. RGB8 is a code-domain
signal. A radial projection is conditional on one dominant radial contribution,
constant receiver response, monotone transfer and locally planar geometry.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import heapq
import json
import math
import statistics
from collections import Counter, defaultdict, deque

from tools.rend2.isophotes.prototype import circle, least_squares, solve

SCHEMA = 'org.openjk.lightmap-patches.v1'
POLICY = dict(max_rows=262144, max_chart_rows=8192, max_work=12000000,
              max_patches=256, scales=[1, 2, 4], minimum_samples=12,
              minimum_contrast_codes=10, minimum_relative_contrast=.12,
              normal_dot=.999, plane_tolerance_world=.05,
              minimum_colour_cosine=.85, contour_levels=[.35, .55, .75, .9],
              minimum_contour_points=12, minimum_angular_bins=7,
              maximum_circle_rms=.06, minimum_gradient_stencils=12)
ASSUMPTIONS = ['One dominant radially symmetric positive contribution',
               'Locally constant diffuse receiver response and geometric normal',
               'Common monotone channel response; no visibility boundary in fitted support',
               'Baked filtering, transport and multiple lights can mimic this feature']


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def add(a, b):
    return [x+y for x, y in zip(a, b)]


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def mul(a, s):
    return [x*s for x in a]


def unit(a):
    length = math.sqrt(dot(a, a))
    return mul(a, 1/length) if length > 1e-10 else None


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def quantile(values, q):
    values = sorted(values)
    return values[min(len(values)-1, int(q*(len(values)-1)))]


def stable(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode()).hexdigest()


class Budget:
    def __init__(self, limit):
        self.limit, self.work = limit, 0

    def use(self, n=1):
        self.work += n
        if self.work > self.limit:
            raise ValueError('ASR14 work limit; no partial report published')


def chart_key(r):
    # Image coordinates never connect different receiver/material/style charts.
    return tuple(r[k] for k in ('image', 'page', 'surface', 'material', 'slot', 'style'))


def prepare(rows, policy):
    groups, rejected, identities = defaultdict(list), Counter(), set()
    for count, row in enumerate(rows):
        if count >= policy['max_rows']:
            raise ValueError('ASR14 input row limit')
        r = dict(row)
        for key in ('id', 'image', 'page', 'x', 'y', 'surface', 'material', 'slot', 'style',
                    'coverage', 'issues', 'area', 'raw'):
            if key not in r:
                raise ValueError('Missing receiver field: '+key)
        if not isinstance(r['id'], str) or not r['id'] or r['id'] in identities:
            raise ValueError('Missing/duplicate receiver identity')
        identities.add(r['id'])
        if any(not isinstance(r[k], int) or r[k] < 0 for k in
               ('image', 'page', 'x', 'y', 'surface', 'slot', 'style', 'coverage', 'issues')):
            raise ValueError('Invalid receiver chart coordinate/identity')
        if len(r['raw']) != 3 or any(not isinstance(c, int) or not 0 <= c <= 255 for c in r['raw']):
            raise ValueError('Expected original RGB8 codes')
        if r.get('encoding', 0) != 0:
            rejected['non_rgb8_encoding'] += 1
            continue
        if r['coverage'] != 2 or not r.get('position') or not r.get('normal') or r['area'] <= 0:
            rejected['unmapped_or_missing_geometry'] += 1
            continue
        if not isinstance(r['material'], str) or not r['material']:
            rejected['missing_material_identity'] += 1
            continue
        if any(len(r[k]) != 3 or any(not math.isfinite(v) for v in r[k]) for k in ('position', 'normal')):
            raise ValueError('Nonfinite receiver geometry')
        if not math.isfinite(r['area']):
            raise ValueError('Nonfinite receiver area')
        # Preserve border/winding/basis/area approximation as uncertainty. Reject
        # ambiguous shared texels, normal seams and unsupported mapping outright.
        bad = sum(1 << i for i in (1, 2, 3, 8, 9, 10, 11, 12, 13))
        if r['issues'] & bad or not r.get('fit_candidate', False):
            rejected['ambiguous_or_unsupported_receiver'] += 1
            continue
        n = unit(r['normal'])
        if n is None or abs(dot(r['normal'], r['normal'])-1) > .01:
            rejected['invalid_geometric_normal'] += 1
            continue
        r['normal'] = n
        r['saturated'] = any(c == 255 for c in r['raw']) or any(f & 8 for f in r.get('channel_flags', []))
        groups[chart_key(r)].append(r)
    for key in groups:
        if len(groups[key]) > policy['max_chart_rows']:
            raise ValueError('ASR14 chart row limit; select an explicit ROI')
        groups[key].sort(key=lambda r: (r['y'], r['x'], r['id']))
    return groups, dict(rejected), len(identities)


def components(rows, policy, budget, rejected):
    grid = {}
    for r in rows:
        k = r['x'], r['y']
        if k in grid:
            raise ValueError('Ambiguous duplicate chart coordinate')
        grid[k] = r
    distances = []
    for (x, y), r in grid.items():
        for k in ((x+1, y), (x, y+1)):
            if k in grid:
                distances.append(math.dist(r['position'], grid[k]['position']))
    pitch = statistics.median(distances) if distances else 0
    if pitch <= 1e-8:
        rejected['charts_without_world_pitch'] += 1
        return []
    adjacent = {}
    for (x, y), r in grid.items():
        neighbours = []
        for k in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
            budget.use()
            if k not in grid:
                continue
            s = grid[k]
            delta = sub(s['position'], r['position'])
            if (.1*pitch <= math.sqrt(dot(delta, delta)) <= 2.1*pitch and
                    dot(r['normal'], s['normal']) >= policy['normal_dot'] and
                    abs(dot(delta, r['normal'])) <= policy['plane_tolerance_world']):
                neighbours.append(k)
        adjacent[x, y] = neighbours
    remaining, result = set(grid), []
    while remaining:
        seed = min(remaining)
        remaining.remove(seed)
        queue, keys = deque([seed]), []
        while queue:
            k = queue.popleft(); keys.append(k)
            for other in adjacent[k]:
                if other in remaining:
                    remaining.remove(other); queue.append(other)
        if len(keys) < policy['minimum_samples']:
            rejected['small_components'] += 1
            continue
        keys.sort()
        origin, n = grid[keys[0]]['position'], grid[keys[0]]['normal']
        if any(abs(dot(sub(grid[k]['position'], origin), n)) > policy['plane_tolerance_world'] or
               dot(n, grid[k]['normal']) < policy['normal_dot'] for k in keys):
            rejected['nonplanar_components'] += 1
            continue  # Curved receivers stay unsupported, never flattened.
        u = unit(cross(n, min(([1, 0, 0], [0, 1, 0], [0, 0, 1]), key=lambda a: abs(dot(a, n)))))
        v = cross(n, u)
        frame = dict(origin=origin, normal=n, u=u, v=v)
        for k in keys:
            delta = sub(grid[k]['position'], origin)
            grid[k]['uv_world'] = [dot(delta, u), dot(delta, v)]
        result.append((keys, grid, adjacent, frame, pitch))
    return result


def neighbourhood(seed, adjacency, radius, budget):
    seen, fringe = {seed}, {seed}
    for _ in range(radius):
        following = set()
        for k in sorted(fringe):
            budget.use(len(adjacency[k]))
            following.update(adjacency[k])
        fringe = following-seen
        seen.update(fringe)
    return sorted(seen)


def to_world(uv, frame):
    return add(frame['origin'], add(mul(frame['u'], uv[0]), mul(frame['v'], uv[1])))


def radial_features(keys, grid, adjacency, scalar, base, amplitude, frame, pitch, policy, budget):
    contours, accepted = [], []
    for fraction in policy['contour_levels']:
        level = base+fraction*amplitude
        points, edges = [], []
        for k in keys:
            for other in adjacency[k]:
                budget.use()
                if other <= k or other not in scalar:
                    continue
                a, b = scalar[k], scalar[other]
                if not (min(a, b) <= level < max(a, b)):
                    continue
                t = (level-a)/(b-a)
                points.append(add(grid[k]['uv_world'], mul(sub(grid[other]['uv_world'], grid[k]['uv_world']), t)))
                edges.append([grid[k]['id'], grid[other]['id']])
        fit = circle(points) if len(points) >= policy['minimum_contour_points'] else None
        passed = bool(fit and fit['relative_rms'] <= policy['maximum_circle_rms'] and
                      fit['angular_bins'] >= policy['minimum_angular_bins'])
        item = dict(fraction=fraction, level_codes=level, points_world=[to_world(p, frame) for p in points],
                    receiver_edges=edges, radial_fit=fit, radial_support=passed,
                    closed_not_assumed=True)
        contours.append(item)
        if passed:
            accepted.append(fit)
    equations, values, gradients = [], [], []
    for k in keys:
        ns = [s for s in adjacency[k] if s in scalar]
        if len(ns) < 4 or grid[k]['saturated'] or any(grid[s]['saturated'] for s in ns):
            continue
        uv = grid[k]['uv_world']
        displacements = [sub(grid[s]['uv_world'], uv) for s in ns]
        differences = [scalar[s]-scalar[k] for s in ns]
        g = least_squares(displacements, differences)
        if not g or math.hypot(*g)*pitch < 2:
            continue
        # Poor local affine fit suggests curvature/noise/edges, not a source ray.
        rms = math.sqrt(statistics.mean((dot(d, g)-q)**2 for d, q in zip(displacements, differences)))
        gradients.append(dict(receiver=grid[k]['id'], tangent_codes_per_world=g,
                              local_rms_codes=rms, position_world=grid[k]['position']))
        if rms > max(2, math.hypot(*g)*pitch*.4):
            continue
        length = math.hypot(*g)
        perpendicular = [-g[1]/length, g[0]/length]
        equations.append(perpendicular); values.append(dot(perpendicular, uv))
    foot, gradient_rms, rank = None, None, 0
    if len(equations) >= policy['minimum_gradient_stencils']:
        xx, yy = sum(r[0]**2 for r in equations), sum(r[1]**2 for r in equations)
        xy = sum(r[0]*r[1] for r in equations)
        rank = (xx+yy-math.hypot(xx-yy, 2*xy))/(2*(xx+yy))
        if rank >= .03:
            foot = least_squares(equations, values)
            # Fixed three Huber iterations limit isolated gradient contamination.
            for _ in range(3):
                residuals = [dot(r, foot)-q for r, q in zip(equations, values)]
                weights = [min(1, pitch/max(abs(e), 1e-9)) for e in residuals]
                foot = least_squares([mul(r, math.sqrt(w)) for r, w in zip(equations, weights)],
                                     [q*math.sqrt(w) for q, w in zip(values, weights)])
            gradient_rms = math.sqrt(statistics.mean((dot(r, foot)-q)**2 for r, q in zip(equations, values)))
    projection, spread = None, None
    if len(accepted) >= 2:
        candidate = [statistics.median(c['projection'][a] for c in accepted) for a in (0, 1)]
        spread = max(math.dist(c['projection'], candidate) for c in accepted)
        if (spread <= pitch and foot and gradient_rms <= pitch and
                math.dist(candidate, foot) <= 2*pitch):
            projection = candidate
    return contours, dict(samples=gradients, stencils=len(equations), rank_ratio=rank,
                          projection_uv_world=foot, line_rms_world=gradient_rms,
                          meaning='Receiver tangent code gradient; never an incoming 3D light vector'), projection, spread


def describe(seed, scales, keys, grid, adjacency, frame, pitch, background, policy, budget, basins):
    peak = grid[seed]
    colour = unit([max(0, c-b) for c, b in zip(peak['raw'], background)])
    if not colour:
        return None
    scalar = {k: dot(grid[k]['raw'], colour) for k in keys}
    base = quantile(list(scalar.values()), .2)
    amplitude = scalar[seed]-base
    if amplitude < policy['minimum_contrast_codes']:
        return None
    # Only the connected positive, compatible-colour basin supports this peak.
    support, queue = {seed}, deque([seed])
    while queue:
        k = queue.popleft()
        for other in adjacency[k]:
            budget.use()
            if other in support or basins[other] != seed or scalar[other] < base+.2*amplitude:
                continue
            excess = [max(0, c-b) for c, b in zip(grid[other]['raw'], background)]
            direction = unit(excess)
            if direction and dot(direction, colour) >= policy['minimum_colour_cosine']:
                support.add(other); queue.append(other)
    if len(support) < policy['minimum_samples']:
        return None
    support = sorted(support)
    weights = [max(0, scalar[k]-base) for k in support]
    total = sum(weights)
    centre = [sum(w*grid[k]['uv_world'][a] for k, w in zip(support, weights))/total for a in (0, 1)]
    xx, xy, yy = [sum(w*(grid[k]['uv_world'][a]-centre[a])*(grid[k]['uv_world'][b]-centre[b])
                      for k, w in zip(support, weights))/total for a, b in ((0, 0), (0, 1), (1, 1))]
    angle = .5*math.atan2(2*xy, xx-yy)
    major = [math.cos(angle), math.sin(angle)]
    eig = [(xx+yy+math.hypot(xx-yy, 2*xy))/2, (xx+yy-math.hypot(xx-yy, 2*xy))/2]
    boundary = [k for k in support if len(adjacency[k]) < 4]
    # Fit the peak with a local quadratic, withholding a nonconcave/outside fit.
    local = neighbourhood(seed, adjacency, 2, budget)
    origin = peak['uv_world']
    delta = [mul(sub(grid[k]['uv_world'], origin), 1/pitch) for k in local]
    fit = least_squares([[1, x, y, x*x, x*y, y*y] for x, y in delta], [scalar[k] for k in local])
    maximum, fit_rms = peak['position'], None
    if fit:
        design = [[1, x, y, x*x, x*y, y*y] for x, y in delta]
        for _ in range(3):
            residuals = [dot(row, fit)-scalar[k] for row, k in zip(design, local)]
            width = max(1, 1.4826*statistics.median(abs(r) for r in residuals))
            weights = [math.sqrt(min(1, 1.5*width/max(abs(r), 1e-9))) for r in residuals]
            fit = least_squares([mul(row, w) for row, w in zip(design, weights)],
                                [scalar[k]*w for k, w in zip(local, weights)])
        h = [[2*fit[3], fit[4]], [fit[4], 2*fit[5]]]
        offset = solve(h, [-fit[1], -fit[2]])
        fit_rms = math.sqrt(statistics.mean((dot([1, x, y, x*x, x*y, y*y], fit)-scalar[k])**2
                                          for k, (x, y) in zip(local, delta)))
        if (offset and h[0][0] < 0 and h[1][1] < 0 and h[0][0]*h[1][1] > h[0][1]**2 and
                math.hypot(*offset) <= 1.5 and fit_rms < .1*amplitude):
            maximum = to_world(add(origin, mul(offset, pitch)), frame)
    contours, gradients, projection, spread = radial_features(
        support, grid, adjacency, scalar, base, amplitude, frame, pitch, policy, budget)
    saturated = sum(grid[k]['saturated'] for k in support)
    shading_mismatch = any(grid[k].get('shading_normal') and
                           dot(grid[k]['normal'], grid[k]['shading_normal']) < policy['normal_dot'] for k in support)
    peak_clipped = len(adjacency[seed]) < 4 or any(len(adjacency[k]) < 4 for k in local)
    uncertainty = ['Unknown spatial reflectance', 'Unknown bake filter and correlated luxels',
                   'Primary light / indirect transport / overlapping light ambiguous']
    if boundary:
        uncertainty.append('Receiver/chart/ROI clipped support; partial contours only')
    if saturated:
        uncertainty.append('Saturated/censored RGB8; maximum and falloff unreliable')
    if any(grid[k]['issues'] for k in support):
        uncertainty.append('Mapping quality flags retained')
    if shading_mismatch:
        uncertainty.append('Shading and geometric normals disagree')
    if saturated or peak_clipped or shading_mismatch:
        projection = None
    identity = stable([chart_key(peak), peak['id']])
    # Strong local code jumps are measured boundaries, not certified shadows.
    edges = []
    for k in support:
        for other in adjacency[k]:
            budget.use()
            if other <= k:
                continue
            difference = scalar[other]-scalar[k]
            if abs(difference) > max(12, .3*amplitude):
                edges.append(dict(receivers=[grid[k]['id'], grid[other]['id']],
                                  midpoint_world=mul(add(grid[k]['position'], grid[other]['position']), .5),
                                  jump_codes=difference, interpretation='Apparent boundary; shadow/material/filter unresolved'))
    return dict(id=identity, chart=dict(zip(('image', 'page', 'surface', 'material', 'slot', 'style'), chart_key(peak))),
                component_receiver=grid[keys[0]]['id'], peak_receiver=peak['id'],
                basin_partition='Same-chart geodesic nearest multiscale maximum; no interpolation through holes',
                receiver_ids=sorted(grid[k]['id'] for k in support), normal_world=frame['normal'],
                frame=frame, scales_texels=sorted(scales), receiver_pitch_world=pitch,
                maximum_world=maximum, maximum_fit_rms_codes=fit_rms, centroid_world=to_world(centre, frame),
                extent_sigma_world=[math.sqrt(max(0, e)) for e in eig],
                major_axis_world=add(mul(frame['u'], major[0]), mul(frame['v'], major[1])) if eig[0] > 1.1*eig[1] else None,
                orientation_meaning='Receiver footprint axis; emitter orientation unavailable',
                colour_domain='OriginalEncodedRGB8_NoIrradianceCalibration',
                peak_rgb8=peak['raw'], background_rgb8=background,
                mean_rgb8=[statistics.mean(grid[k]['raw'][a] for k in support) for a in range(3)],
                excess_colour_direction=colour, contrast_codes=amplitude,
                coverage=dict(receivers=len(support), boundary_receivers=len(boundary), saturated_receivers=saturated),
                mapping_issue_counts=dict(Counter(str(grid[k]['issues']) for k in support)),
                isophotes=contours, gradients=gradients, apparent_boundaries=edges,
                constraint=dict(state='Conditional' if projection else 'Unavailable',
                                foot_world=to_world(projection, frame) if projection else None,
                                normal_world=frame['normal'], depth_world=None, emitter_orientation=None,
                                radius_world=max(pitch, spread or 0, gradients['line_rms_world'] or 0),
                                uncertainty_meaning='Heuristic geometric tolerance, not statistical confidence',
                                assumptions=ASSUMPTIONS),
                uncertainty=uncertainty, physical_source_certified=False)


def extract(rows, policy=None):
    policy = dict(POLICY if policy is None else policy)
    if (set(policy) != set(POLICY) or not policy['scales'] or
            any(not isinstance(s, int) or not 1 <= s <= 8 for s in policy['scales']) or
            not policy['contour_levels'] or any(not math.isfinite(v) or not 0 < v < 1 for v in policy['contour_levels']) or
            any(not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0
                for k, v in policy.items() if k not in ('scales', 'contour_levels'))):
        raise ValueError('Invalid ASR14 policy')
    budget = Budget(policy['max_work'])
    groups, rejected, count = prepare(rows, policy)
    rejected = Counter(rejected)
    patches, component_count = [], 0
    for key in sorted(groups):
        for keys, grid, adjacency, frame, pitch in components(groups[key], policy, budget, rejected):
            component_count += 1
            background = [quantile([grid[k]['raw'][a] for k in keys], .2) for a in range(3)]
            seeds = {}
            for scale in sorted(set(policy['scales'])):
                smoothed = {}
                for k in keys:
                    local = neighbourhood(k, adjacency, scale, budget)
                    # Channel-wise trimmed mean limits isolated noisy RGB maxima.
                    channels = [sorted(grid[s]['raw'][a] for s in local) for a in range(3)]
                    trim = len(local)//10
                    smoothed[k] = [statistics.mean(c[trim:len(c)-trim]) for c in channels]
                scalar = {k: math.sqrt(sum(max(0, c-b)**2 for c, b in zip(smoothed[k], background))) for k in keys}
                for k in keys:
                    value = scalar[k]
                    if value < max(policy['minimum_contrast_codes'],
                                   policy['minimum_relative_contrast']*math.sqrt(dot(smoothed[k], smoothed[k]))):
                        continue
                    local = neighbourhood(k, adjacency, max(2, scale), budget)
                    if any((scalar[s], grid[s]['id']) > (value, grid[k]['id']) for s in local if s != k):
                        continue
                    # A ramp terminating at an image boundary is not a local peak.
                    if len(adjacency[k]) < 4:
                        continue
                    close = [s for s in seeds if math.dist(grid[s]['position'], grid[k]['position']) <= 2*pitch]
                    seed = min(close, key=lambda s: grid[s]['id']) if close else k
                    seeds.setdefault(seed, set()).add(scale)
            if len(seeds) > policy['max_patches']:
                raise ValueError('ASR14 maximum-seed limit; select a smaller explicit ROI')
            # Partition a multi-maximum receiver before computing extent/contours.
            # Chart graph distance respects holes, seams and geometric barriers.
            queue = [(0., grid[s]['id'], s, s) for s in seeds]
            heapq.heapify(queue)
            basins, distances = {}, {}
            while queue:
                distance, identity, seed, k = heapq.heappop(queue)
                if k in basins:
                    continue
                basins[k] = seed
                for other in adjacency[k]:
                    budget.use()
                    length = distance+math.dist(grid[k]['position'], grid[other]['position'])
                    key = (length, identity)
                    if other not in basins and key < distances.get(other, (float('inf'), '')):
                        distances[other] = key
                        heapq.heappush(queue, (length, identity, seed, other))
            for seed in sorted(seeds):
                patch = describe(seed, seeds[seed], keys, grid, adjacency, frame, pitch, background, policy, budget, basins)
                if patch:
                    patches.append(patch)
                    if len(patches) > policy['max_patches']:
                        raise ValueError('ASR14 patch limit; select a smaller explicit ROI')
    patches.sort(key=lambda p: p['id'])
    return dict(schema=SCHEMA, policy=policy, input_rows=count, charts=len(groups), components=component_count,
                rejected=dict(rejected), work=budget.work, patches=patches,
                physical_source_certified=False, canonical_storage_modified=False)


def combine(patches, geometry=None):
    """Pair nonparallel normal-line constraints; never assume unknown space clear.

    geometry.point(position) returns free/solid/unknown, and geometry.ray(a,b)
    clear/blocked/unknown, incorporating static and possible dynamic occupancy.
    No source type, depth from intensity, or historical identity is inferred.
    """
    candidates = []
    eligible = [p for p in patches if p['constraint']['state'] == 'Conditional']
    if len(eligible) > POLICY['max_patches']:
        raise ValueError('ASR14 combination budget')
    for i, a in enumerate(eligible):
        for b in eligible[i+1:]:
            if (a['chart']['surface'] == b['chart']['surface'] or
                    a['chart']['style'] != b['chart']['style'] or
                    dot(a['excess_colour_direction'], b['excess_colour_direction']) < .95):
                continue
            na, nb = a['normal_world'], b['normal_world']
            if abs(dot(na, nb)) > .9:
                continue
            matrix, rhs = [[0.]*3 for _ in range(3)], [0.]*3
            for p in (a, b):
                n, foot = p['normal_world'], p['constraint']['foot_world']
                for x in range(3):
                    for y in range(3):
                        q = (1 if x == y else 0)-n[x]*n[y]
                        matrix[x][y] += q; rhs[x] += q*foot[y]
            position = solve(matrix, rhs)
            radius = max(p['constraint']['radius_world'] for p in (a, b))
            residual = max(math.sqrt(sum(t*t for t in sub(sub(position, p['constraint']['foot_world']),
                           mul(p['normal_world'], dot(sub(position, p['constraint']['foot_world']), p['normal_world'])))))
                           for p in (a, b))
            if residual > 2*radius or any(dot(sub(position, p['constraint']['foot_world']), p['normal_world']) <= radius for p in (a, b)):
                continue
            space = geometry.point(position) if geometry else 'unknown'
            rays = [geometry.ray(p['maximum_world'], position) if geometry else 'unknown' for p in (a, b)]
            if space not in ('free', 'solid', 'unknown') or any(r not in ('clear', 'blocked', 'unknown') for r in rays):
                raise ValueError('Invalid geometry evidence')
            status = ('RejectedGeometry' if space == 'solid' or 'blocked' in rays else
                      'ConditionalLocation' if space == 'free' and all(r == 'clear' for r in rays) else 'UnknownGeometry')
            candidates.append(dict(patches=[a['id'], b['id']], position_world=position,
                                   line_residual_world=residual, tolerance_world=2*radius,
                                   geometry_space=space, receiver_rays=rays, state=status,
                                   emitter_orientation=None, physical_source_certified=False,
                                   assumptions=ASSUMPTIONS))
    return candidates
