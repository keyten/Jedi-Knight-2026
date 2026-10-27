"""Baked-light objectives reusing verified kernels and X04 footprint references.
All sources/answers are controller-owned. No production estimator or renderer hook.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import subprocess

from environment import np, HERE, ROOT
from routines import Objective, Work, checked, Infeasible
from bake_model import point_scalar, spot_factor, encode_color
from hypotheses import continuous_encode, UnspecifiedModel
from compiled_profiles import encoded_floor
from compare_bakes import bsp, CASES, metrics as compiler_verify
from analytic import rectangle_reference, smooth_point_ad
from integrate import gauss, integrated, sampled, Source, Scene
from geometry import rectangle
from fixtures import wall
from adaptive import adaptive


@dataclass
class Rows:
    position: object
    normal: object
    rgb: object
    weight: object
    partition: tuple
    masks: object = None
    identity: tuple = ()

    def select(self, role):
        ids = np.array([i for i, p in enumerate(self.partition) if p == role], dtype=int)
        return Rows(self.position[ids], self.normal[ids], self.rgb[ids], self.weight[ids],
                    tuple(self.partition[i] for i in ids), self.masks[ids] if self.masks is not None else None,
                    tuple(self.identity[i] for i in ids) if self.identity else ())


def original_intervals(raw, nearest=False):
    # q3map output casts/truncates: [b,b+1), not nearest +/-0.5. Closed upper
    # endpoint is a conservative interval relaxation for optimization.
    raw = checked(raw)
    if np.any(raw < 0) or np.any(raw > 255): raise ValueError('RGB8 domain')
    lo = np.maximum(0., raw-(.5 if nearest else 0.))
    hi = np.where(raw == 255, np.inf, raw+(.5 if nearest else 1.))
    return lo, hi


def compiler_rgb(rows, origins, amplitudes, profile='isq', axes=None, radii=None, work=None, encoded=True):
    origins, amplitudes = checked(origins), checked(amplitudes)
    if origins.shape != amplitudes.shape or origins.shape[1] != 3 or np.any(amplitudes < 0):
        raise ValueError('Source XYZ/RGB span')
    if profile not in ('isq', 'linear', 'spot', 'noangle'): raise UnspecifiedModel('UnknownProfile rule')
    out = np.zeros((len(rows.position), 3))
    if work: work.scalar_responses += len(origins)*len(rows.position)
    for i, (p, n) in enumerate(zip(rows.position, rows.normal)):
        for j, (origin, q) in enumerate(zip(origins, amplitudes)):
            if rows.masks is not None:
                if rows.masks[i, j] is None: raise UnspecifiedModel('Unknown optical visibility')
                if not rows.masks[i, j]: continue
            I = float(max(q))
            if not I: continue
            delta = origin-p; r = float(np.linalg.norm(delta))
            if r <= 1e-10: raise ValueError('Coincident source and receiver')
            cosine = float(np.dot(n, delta))/r
            if cosine <= 0: continue
            if profile in ('linear', 'noangle'): cosine = 1.
            value = point_scalar(I, r, cosine, linear=profile == 'linear')
            if profile == 'spot':
                axis = (0., 0., -1.) if axes is None else axes[j]
                radius = 64. if radii is None else radii[j]
                value *= spot_factor(p, origin, axis, 128., radius)
                if value <= 1.: value = 0.
            out[i] += value*q/I
    return np.asarray([continuous_encode(v) for v in out]) if encoded else out


def contribution_matrix(rows, origins):
    # Linear point response on an explicitly active smooth branch. No claim that
    # the compiler's intensity-dependent fast cutoff is a global linear operator.
    columns = []
    for j, origin in enumerate(origins):
        col = []
        for i, (p, n) in enumerate(zip(rows.position, rows.normal)):
            d = np.asarray(origin)-p; r = float(np.linalg.norm(d))
            if r <= 1e-10: raise ValueError('Coincident source')
            v = 7500*max(0., float(np.dot(n, d))/r)/max(16., r)**2
            if rows.masks is not None and not rows.masks[i, j]: v = 0.
            col.append(v)
        columns.append(col)
    return np.asarray(columns).T


def compiled22(root, name):
    root = Path(root); witness = json.loads((HERE.parent/'bake_model/validation.json').read_text())
    path = root/'authoring/base/maps'/(name+'.bsp')
    h = hashlib.sha256(path.read_bytes()).hexdigest()
    if h != witness['cases'][name]['bsp_sha256']: raise ValueError('Compiled BSP content mismatch '+name)
    c = next(c for c in CASES if c['name'] == name); lumps = bsp(path)
    replay = compiler_verify(c, lumps)
    if not replay['passed']: raise ValueError('Compiler kernel verification failed '+name)
    observations = encoded_floor(lumps, name)
    # The locked fixture is a convex box and all prescribed source boxes are
    # interior. Masks certify this known clear experiment, not stock geometry.
    rows = Rows(np.array([r.position for r in observations]), np.array([r.normal for r in observations]),
        np.array([r.rgb for r in observations], dtype=float), np.full(len(observations), 256.),
        tuple(r.partition for r in observations), np.ones((len(observations), 1), dtype=bool),
        tuple(r.id for r in observations))
    q = np.array([[c.get('intensity', 300)*v for v in c.get('color', (1., 1., 1.))]])
    return dict(name=name, rows=rows, origins=np.array([[0., 0., 192.]]), amplitudes=q,
        profile='spot' if c.get('spot') else 'linear' if c.get('linear') else 'isq',
        metadata=dict(bsp_sha256=h, compiler_sha256=witness['compiler_sha256'],
            kernel_replay_passed=True, units='Compiler relative, NOT SI', new_compiler_run=False,
            visibility='Convex sealed own-content room; bounded interior source, no blockers',
            partitions={p: sum(r.partition == p for r in observations) for p in ('train', 'validation', 'heldout')},
            sample_origin='BSP floor +1 WU compiler push'))


def compiled12(root, recipes, name, ray_tool, output):
    root = Path(root); source = json.loads((root/(name+'.source.json')).read_text())
    recipe = json.loads((Path(recipes)/(name+'.q3map-recipe.json')).read_text())
    if recipe['compiled_bsp_sha256'] != source['map_sha256']: raise ValueError('Fixture/answer BSP mismatch')
    sources = recipe['ground_truth']['lights']
    origins = np.array([s['position_world'] for s in sources]); q = np.array([
        [s['authored_intensity']*v for v in s['rgb_parameter']] for s in sources])
    if not len(sources): raise ValueError('This compiler block needs point/spot truth')
    stream = []; partitions = []
    for role, directory, filename in [('train', 'inputs', 'train.jsonl'), ('validation', 'evaluator', 'validation.jsonl'),
                                     ('heldout', 'evaluator', 'heldout.jsonl')]:
        path = root/(name+'-views')/directory/filename
        records = [json.loads(line) for line in path.read_text().splitlines()]
        selected = {}
        for r in records:
            if r['normal'] is None or abs(np.linalg.norm(r['normal'])-1.) > 1e-5: continue
            key = tuple(math.floor(v/32) for v in r['position'])
            selected.setdefault(key, r)
        kept = [selected[k] for k in sorted(selected)][:64]
        if len(kept) < 3: raise ValueError('Insufficient compiled '+name+' '+role+' receivers; no silent skipped benchmark')
        stream += kept; partitions += [role]*len(kept)
    output.mkdir(parents=True, exist_ok=True); query = output/(name+'-queries.tsv'); answer = output/(name+'-rays.tsv')
    text = ['map '+source['map_sha256']]
    for i, r in enumerate(stream):
        for j, o in enumerate(origins):
            text.append(' '.join(map(str, [str(i), str(j), *r['position'], *r['normal'], r['surface'], 0, *o])))
    query.write_text('\n'.join(text)+'\n', encoding='utf-8')
    command = [str(ray_tool), str(root/(name+'-views')/'inputs/geometry.jkobs'), str(query), str(answer)]
    r = subprocess.run(command, capture_output=True, text=True)
    if r.returncode: raise ValueError('CPU visibility failed: '+r.stderr)
    (output/(name+'-command.json')).write_text(json.dumps(command, indent=2)+'\n', encoding='utf-8')
    masks = np.full((len(stream), len(sources)), None, dtype=object)
    for line in answer.read_text().splitlines():
        i, j, value, *_ = line.split(); masks[int(i), int(j)] = None if value == 'unknown' else value == 'visible'
    known = [i for i in range(len(stream)) if all(v is not None for v in masks[i])]
    data = [stream[i] for i in known]
    rows = Rows(np.array([np.array(r['position'])+np.array(r['normal']) for r in data]), np.array([r['normal'] for r in data]),
        np.array([r['raw'] for r in data], dtype=float), np.array([r['area'] for r in data]),
        tuple(partitions[i] for i in known), masks[known].astype(bool), tuple(r['id'] for r in data))
    return dict(name=name, rows=rows, origins=origins, amplitudes=q, profile='spot' if name == 'spot' else 'isq',
        metadata=dict(bsp_sha256=source['map_sha256'], compiler_sha256=recipe['binary_sha256'],
            geometry_rays=len(stream)*len(sources), unknown_observations_excluded=len(stream)-len(known),
            ray_witness_sha256=hashlib.sha256(answer.read_bytes()).hexdigest(),
            ray_command=[str(v).replace(str(output), '<run>/rays') for v in command],
            partitions={role: sum(p == role for p in rows.partition) for role in ('train', 'validation', 'heldout')},
            visible_rays=int(np.sum(rows.masks)), blocked_rays=int(rows.masks.size-np.sum(rows.masks)),
            receiver_sampling='All available oriented receiver types; assumed +1WU geometric-normal compiler push',
            units='Compiler relative, NOT SI', new_compiler_run=False))


def rgb_objective(rows, origins, initial, profile, budget=800, censored=True):
    lo, hi = original_intervals(rows.rgb) if censored else (rows.rgb, rows.rgb)
    def model(u, work): return compiler_rgb(rows, origins, np.asarray(u).reshape(-1, 3)*4000., profile, work=work)
    return Objective(model, np.zeros(len(initial)), np.ones(len(initial)), np.asarray(initial)/4000.,
        lo, hi, np.repeat(rows.weight[:, None], 3, axis=1), scale=64., budget=budget)


def smooth_rows(multi=True, near=False):
    points = []; normals = []; parts = []
    for i in range(9):
        for j in range(7):
            x, y = (i-4)*48., (j-3)*40.
            if near: x, y = x*.03, y*.03
            points.append((x, y, 1.)); normals.append((0., 0., 1.)); parts.append(('train', 'validation', 'heldout')[(i+2*j)%3])
    if multi:
        for i in range(7):
            for j in range(5):
                points.append((-256., (i-3)*40., 32+j*40.)); normals.append((1., 0., 0.)); parts.append(('train', 'validation', 'heldout')[(i+j)%3])
    return Rows(np.array(points), np.array(normals), np.zeros((len(points), 3)), np.ones(len(points)), tuple(parts))


def geometry_problem(kind, initial, noise=0., seed=0, budget=800, raw_scale=False):
    rows = smooth_rows(multi=kind != 'single_plane', near=kind == 'distance_ambiguity')
    truth = np.array([16., -24., 192.])
    if kind in ('xyz', 'single_plane', 'distance_ambiguity', 'xyz_intensity'):
        lower, upper = np.array([-128., -128., 96.]), np.array([128., 128., 300.])
        if kind in ('distance_ambiguity', 'xyz_intensity'):
            truth = np.r_[truth, 300.]; lower = np.r_[lower, 50.]; upper = np.r_[upper, 1500.]
        def response(p, rr, work):
            q = 300. if len(p) == 3 else p[3]
            return compiler_rgb(rr, [p[:3]], [[q, q, q]], work=work, encoded=False)
    elif kind in ('spot_axis_cone', 'spot_core'):
        truth = np.array([.15, -.1, 64.]); lower = np.array([-.5, -.5, 20.]); upper = np.array([.5, .5, 128.])
        if kind == 'spot_core':
            rows = smooth_rows(multi=False, near=True)
        def response(p, rr, work):
            axis = np.array([p[0], p[1], -1.]); axis /= np.linalg.norm(axis)
            return compiler_rgb(rr, [[0, 0, 192]], [[300, 300, 300]], 'spot', [axis], [p[2]], work, encoded=False)
    elif kind in ('rectangle', 'area_far'):
        truth = np.array([72., 32.]); lower = np.array([16., 8.]); upper = np.array([144., 128.])
        height = 192. if kind == 'rectangle' else 1920.
        if kind == 'area_far':
            truth = np.r_[truth, 50.]; lower = np.r_[lower, 1.]; upper = np.r_[upper, 500.]
        nodes = gauss(8)
        def response(p, rr, work):
            xs = np.array([(2*u-1)*p[0] for u, _ in nodes]); ys = np.array([(2*u-1)*p[1] for u, _ in nodes])
            sources = np.array([(x, y, height) for x in xs for y in ys]); weights = np.array([a*b for _, a in nodes for _, b in nodes])
            delta = sources[None, :, :]-rr.position[:, None, :]; r = np.linalg.norm(delta, axis=2)
            receiver_cos = np.maximum(0., np.sum(delta*rr.normal[:, None, :], axis=2)/r)
            light_cos = np.maximum(0., delta[:, :, 2]/r)
            intensity = p[2] if len(p) == 3 else 50.
            value = np.sum(weights[None, :]*receiver_cos*light_cos/r**2, axis=1)*4*p[0]*p[1]*intensity
            work.scalar_responses += len(rr.position)*64
            return np.repeat(value[:, None], 3, axis=1)
    else: raise ValueError('Geometry benchmark kind')
    rng = np.random.default_rng(seed); reference = response(truth, rows, Work())
    rows.rgb = np.maximum(0., reference+rng.normal(0., noise, reference.shape))
    training = rows.select('train')
    x0 = lower+np.asarray(initial)*(upper-lower)
    bounds = (lower, upper, x0) if raw_scale else (np.zeros(len(truth)), np.ones(len(truth)), np.asarray(initial))
    decode = (lambda x: np.asarray(x)) if raw_scale else (lambda x: lower+np.asarray(x)*(upper-lower))
    def model(x, work): return response(decode(x), training, work)
    objective = Objective(model, *bounds, training.rgb, training.rgb,
        scale=64. if kind not in ('rectangle', 'area_far') else 1., budget=budget)
    return objective, dict(kind=kind, truth=truth, decode=decode, rows=rows, response=response,
                           lower=lower, upper=upper, physical_units='Known compiler kernel-relative' if kind not in ('rectangle', 'area_far') else 'Synthetic nits/irradiance')


def shadow_value(bounds, height, method, work):
    source = (0., 0., float(height)); fp = rectangle(*bounds); src = Source(position=source); scene = Scene(wall())
    if method == 'reference':
        work.projections += 1; work.scalar_responses += 4
        return rectangle_reference(bounds, source)['value']
    if method in ('centre', 'grid4'):
        r = sampled(fp, src, scene, n=1 if method == 'centre' else 4,
                    method='centre' if method == 'centre' else 'grid')
    elif method == 'adaptive': r = adaptive(fp, src, scene, tolerance=1e-6, max_depth=3)
    elif method == 'polygon': r = integrated(fp, src, scene, tolerance=1e-8)
    elif method == 'surrogate':
        # Deliberate smooth optimization surrogate; target stays reference.
        x = (bounds[0]+bounds[1])/2; y = (bounds[2]+bounds[3])/2
        boundary = height/(height-1); visibility = 1/(1+math.exp(max(-50., min(50., -(x-boundary)/.025))))
        work.scalar_responses += 1; work.visibility_tests += 1
        return height/(height*height+x*x+y*y)**1.5*visibility
    else: raise ValueError('Visibility evaluator')
    for key in ('rays', 'responses', 'projections'):
        setattr(work, 'scalar_responses' if key == 'responses' else key,
                getattr(work, 'scalar_responses' if key == 'responses' else key)+r['work'][key])
    return r['contribution_upper'][0]


def shadow_problem(method, start=.5, budget=800):
    bounds = [(x-.045, x+.045, -.04, .04) for x in (1.25, 1.3, 1.35, 1.4)]
    target = np.array([shadow_value(b, 4., 'reference', Work()) for b in bounds])
    def model(x, work): return np.array([shadow_value(b, 2.+6*x[0], method, work) for b in bounds])
    obj = Objective(model, [0.], [1.], [start], target, target, scale=.02, budget=budget)
    return obj, dict(bounds=bounds, height_truth=4., target=target)
