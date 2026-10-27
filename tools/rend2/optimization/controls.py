"""Scaling, derivatives, sampling, constraints and schedule controls.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
from environment import np
from routines import Objective, Work, linear_fit, solve_objective, Cancelled, Infeasible
from problems import geometry_problem, smooth_rows, compiler_rgb, contribution_matrix, shadow_value
from analytic import smooth_point_ad
from experiments import rmse, record_problem


def derivative_controls():
    source = np.array([16., -24., 192.]); receiver = np.array([-64., 48., 1.]); q = 300.
    value, ad = smooth_point_ad(source, receiver)
    ad = np.array(ad)*7500*q
    rows = smooth_rows(False); rows.position = np.array([receiver]); rows.normal = np.array([[0., 0., 1.]])
    trials = []
    for step in (1e-6, 1e-4, .01, 1.):
        numeric = []
        for k in range(3):
            a, b = source.copy(), source.copy(); a[k]+=step; b[k]-=step
            f = lambda p: compiler_rgb(rows, [p], [[q]*3], encoded=False)[0, 0]
            numeric.append((f(a)-f(b))/(2*step))
        trials.append(dict(step_world=step, finite_difference=numeric, analytic_ad=ad.tolist(),
            max_error=float(np.max(abs(np.array(numeric)-ad)))))
    return dict(trials=trials, analytic_value=value*7500*q,
        kernel_value=float(compiler_rgb(rows, [source], [[q]*3], encoded=False)[0, 0]),
        scope='Existing three-variable forward dual numbers; no AD framework; fixed active/visibility branch')


def scaling_schedules(timings):
    output = []
    for raw in (False, True):
        for method in ('adam', 'projected_lm', 'trf'):
            obj, meta = geometry_problem('xyz_intensity', [.7]*4, budget=700, raw_scale=raw)
            r = solve_objective(obj, method); p = meta['decode'](r['x']); hold = meta['rows'].select('heldout')
            r.update(parameter_error=float(np.linalg.norm(p-meta['truth'])),
                heldout_rmse=rmse(meta['response'](p, hold, Work()), hold.rgb))
            output.append(record_problem('scaling/raw_vs_unit_box', obj, method, r, dict(raw=raw), timings))
    # Fixed-amplitude warm starts across nearby *data*, then a changed response
    # matrix. Reuse X only; never reuse an obsolete geometry-dependent matrix.
    rng = np.random.default_rng(73); A = rng.uniform(.1, 1., (80, 4)); true = rng.uniform(0., 3., (4, 3))
    previous = None; warm = []
    for step in range(5):
        matrix = A+step*.002; Y = np.maximum(0., matrix@true+rng.normal(0., .001, (80, 3)))
        for state in ('cold', 'warm'):
            r = linear_fit(matrix, Y, 'coordinate', x0=previous if state == 'warm' else None)
            warm.append(record_problem('warm/data_and_matrix_change', None, 'coordinate/'+state, r, dict(step=step), timings))
            if state == 'warm': previous = np.array(r['x'])
    # Coarse geometry-only receiver prefix, then full independent training stream.
    obj, meta = geometry_problem('xyz', [.7]*3, budget=700)
    full = solve_objective(obj, 'trf')
    coarse, info = geometry_problem('xyz', [.7]*3, budget=200)
    original_model = coarse.model; ids = np.arange(0, len(coarse.lo), 3)
    coarse.model = lambda x, work: original_model(x, work)[ids]
    coarse.lo, coarse.hi = coarse.lo[ids], coarse.hi[ids]
    coarse.factor = coarse.factor[ids]*math.sqrt(3.)
    first = solve_objective(coarse, 'trf'); fine, _ = geometry_problem('xyz', first['x'], budget=500)
    second = solve_objective(fine, 'trf')
    stages = []
    for label, problem, r in [('full', obj, full), ('coarse', coarse, first), ('fine', fine, second)]:
        stages.append(record_problem('schedule/coarse_fine', problem, label, r,
            dict(position_error=float(np.linalg.norm(info['decode'](r['x'])-info['truth']))), timings))
    # A simple alternate position/intensity block uses training residual only.
    # Each scalar coordinate step is bounded; the shared forward budget counts all
    # work. This is not production variable projection with exact inner NNLS.
    joint, jm = geometry_problem('xyz_intensity', [.7]*4, budget=700)
    x = joint.x0.copy(); start = __import__('time').perf_counter()
    from scipy.optimize import minimize_scalar
    status = 'SweepLimit'
    try:
        for sweep in range(8):
            old = x.copy()
            for k in (3, 0, 1, 2):
                def fun(t):
                    trial = x.copy(); trial[k] = t
                    return joint.loss_terms(joint.residual(trial))[0]
                r = minimize_scalar(fun, bounds=(0., 1.), method='bounded', options=dict(maxiter=15, xatol=1e-5))
                x[k] = r.x
            if np.max(abs(x-old)) < 1e-5: status = 'Stationary'; break
    except RuntimeError: status = 'WorkLimit'
    best = joint.best
    block = dict(x=best[1].tolist(), objective=best[0], status=status, iterations=sweep+1,
        work=joint.work.__dict__.copy(), wall_seconds=__import__('time').perf_counter()-start,
        parameter_error=float(np.linalg.norm(jm['decode'](best[1])-jm['truth'])))
    output.append(record_problem('schedule/alternating_position_intensity', joint, 'bounded_coordinate', block, {}, timings))
    return dict(scaling=output, warm_starts=warm, coarse_fine=stages)


def sampling(timings):
    out = []; rng = np.random.default_rng(911)
    template, meta = geometry_problem('xyz', [.7]*3, noise=.5, seed=41, budget=600)
    rows = meta['rows']; train = rows.select('train'); hold = rows.select('heldout'); n = len(train.rgb); count = 10
    area = np.where(train.normal[:, 0] > .5, 4., 1.)
    selections = dict(uniform=rng.choice(n, count, replace=False),
        area_weighted=rng.choice(n, count, replace=False, p=area/area.sum()),
        stratified=np.linspace(0, n-1, count).astype(int),
        normal_diversity=np.r_[np.where(train.normal[:, 2] > .5)[0][:5], np.where(train.normal[:, 0] > .5)[0][:5]],
        region_local=np.where(train.position[:, 0] > 0)[0][:count],
        saturation_aware=np.argsort(np.max(train.rgb, axis=1))[:count])
    for label, ids in selections.items():
        subset = type(train)(train.position[ids], train.normal[ids], train.rgb[ids], area[ids], tuple('train' for _ in ids))
        decode = meta['decode']
        def model(x, work): return meta['response'](decode(x), subset, work)
        obj = Objective(model, [0.]*3, [1.]*3, [.7]*3, subset.rgb, subset.rgb,
                        np.repeat(subset.weight[:, None], 3, axis=1), scale=64., budget=600)
        r = solve_objective(obj, 'trf'); p = decode(r['x'])
        r.update(position_error=float(np.linalg.norm(p-meta['truth'])),
            heldout_rmse=rmse(meta['response'](p, hold, Work()), hold.rgb),
            unique_receivers=len(set(map(int, ids))), normal_classes=len(set(map(tuple, subset.normal))))
        out.append(record_problem('sampling/receiver_subset', obj, 'trf/'+label, r, {}, timings))
    # Shadow sampling comparison: same eight observations, independent held-out
    # footprints, 4 selected by spatial prefix or by candidate-boundary proximity.
    bounds = [(x-.03, x+.03, -.03, .03) for x in np.linspace(1.1, 1.65, 12)]
    target = np.array([shadow_value(b, 4., 'reference', Work()) for b in bounds])
    for label, ids in [('uniform', [0, 3, 7, 11]), ('candidate_boundary', [4, 5, 6, 7])]:
        def model(x, work): return np.array([shadow_value(bounds[i], 2+6*x[0], 'reference', work) for i in ids])
        obj = Objective(model, [0.], [1.], [.7], target[ids], target[ids], scale=.02, budget=400)
        r = solve_objective(obj, 'trf'); r['position_error_metres'] = abs(2+6*r['x'][0]-4.)
        out.append(record_problem('sampling/shadow_footprints', obj, 'trf/'+label, r,
            dict(selector='Known candidate shadow location for this controlled experiment, not an oracle-free deployment guarantee'), timings))
    # Stochastic batches with shared random indices within each FD gradient.
    x = np.array([.7]*3); momentum = x*0; variance = x*0; work = Work(); best = None
    import time
    start = time.perf_counter()
    for k in range(60):
        ids = rng.choice(n, 10, replace=False)
        subset = type(train)(train.position[ids], train.normal[ids], train.rgb[ids], train.weight[ids], tuple('train' for _ in ids))
        obj = Objective(lambda x, w: meta['response'](meta['decode'](x), subset, w), [0.]*3, [1.]*3, x,
                        subset.rgb, subset.rgb, scale=64., budget=16)
        f, g = obj.fg(x)
        momentum = .9*momentum+.1*g; variance = .999*variance+.001*g*g
        x = np.clip(x-.025*(momentum/(1-.9**(k+1)))/(np.sqrt(variance/(1-.999**(k+1)))+1e-8), 0., 1.)
        for key, value in obj.work.__dict__.items(): setattr(work, key, getattr(work, key)+value)
        if k%5 == 0:
            e = rmse(meta['response'](meta['decode'](x), train, work), train.rgb)
            if best is None or e < best[0]: best = (e, x.copy())
    p = meta['decode'](best[1])
    r = dict(x=best[1].tolist(), status='IterationLimit', objective=best[0], iterations=60,
             position_error=float(np.linalg.norm(p-meta['truth'])),
             heldout_rmse=rmse(meta['response'](p, hold, Work()), hold.rgb),
             work=work.__dict__.copy(), wall_seconds=time.perf_counter()-start)
    out.append(record_problem('sampling/stochastic_common_random_batch', None, 'adam', r, dict(seed=911), timings))
    return out


def numerical_geometry_controls(timings):
    results = []
    # A certified thin solid slab. Unknown region membership adds NO hard barrier.
    # A wall-mounted/recessed prior can legitimately touch the support plane.
    def make(mode):
        target = np.array([.5])
        model = lambda x, w: np.array([x[0]])
        feasible = lambda x: not (.45 < x[0] < .55)
        if mode == 'hard_solid': return Objective(model, [0.], [1.], [.2], target, target, feasible=feasible)
        if mode == 'soft_prior':
            def soft(x, w): return np.array([x[0], .1*(x[0]-.2)])
            return Objective(soft, [0.], [1.], [.2], [.5, 0.], [.5, 0.])
        return Objective(model, [0.], [1.], [.2], target, target, feasible=feasible)
    for label in ('hard_solid', 'soft_prior', 'unknown_region'):
        obj = make(label); r = solve_objective(obj, 'trf')
        r.update(in_solid=.45 < r['x'][0] < .55, calibrated_position_confidence=None)
        results.append(record_problem('geometry/'+label, obj, 'trf', r, {}, timings))
    # Recessed/wall supports: constraints are intervals, not a forced exact point.
    geometry_policy = dict(wall_fixture_support=[.19, .23], recessed_support=[.17, .22],
        unknown_blocker='Unavailable, not transparent', unknown_region='No region-based rejection',
        warning='Synthetic 1D feasible-set controls, not a BSP domain implementation; use existing CPU geometry API')
    for scale in (1e-9, 1., 1e9):
        A = np.array([[1., .1], [.2, 1.], [.5, .7]])*scale; truth = np.array([[1., 2., 3.], [3., 2., 1.]])/scale
        r = linear_fit(A, A@truth, 'nnls')
        r['rescaled_solution_error'] = float(np.max(abs(np.array(r['x'])*scale-truth*scale)))
        results.append(record_problem('stability/scale', None, 'nnls', r, dict(scale=scale), timings))
    # SciPy NNLS adapters deliberately solve in float64. This separate NumPy
    # diagnostic actually retains each arithmetic dtype and permits signed LS
    # coefficients; it is NOT clipped/advertised as a nonnegative estimator.
    import time
    x = np.linspace(.1, 1., 40)
    base = np.column_stack((x, x*(1+1e-7*np.linspace(-1, 1, 40))))
    true = np.array([1., 2.])
    for dtype in (np.float32, np.float64):
        A = base.astype(dtype); y = (base@true).astype(dtype); tick = time.perf_counter()
        solution, _, rank, singular = np.linalg.lstsq(A, y, rcond=1e-12)
        r = dict(x=solution.tolist(), arithmetic_dtype=str(solution.dtype), status='RankDeficient' if rank < 2 else 'LocalFullRank',
            objective=float(np.linalg.norm(A@solution-y)**2), rank=int(rank), singular_values=singular.tolist(),
            signed_coefficients=True, coefficient_error=float(np.linalg.norm(solution-true)),
            work=Work().__dict__.copy(), wall_seconds=time.perf_counter()-tick)
        results.append(record_problem('stability/float_arithmetic', None, str(dtype), r, {}, timings))
    obj, meta = geometry_problem('xyz', [.7]*3, budget=100)
    obj.cancelled = lambda: obj.work.forward >= 12
    stopped = solve_objective(obj, 'trf')
    resumed, _ = geometry_problem('xyz', stopped['x'], budget=500)
    r = solve_objective(resumed, 'trf')
    stopped['position_error'] = float(np.linalg.norm(meta['decode'](stopped['x'])-meta['truth']))
    r['position_error'] = float(np.linalg.norm(meta['decode'](r['x'])-meta['truth']))
    results.append(record_problem('cancel/restart', obj, 'cancelled-trf', stopped, {}, timings))
    results.append(record_problem('cancel/restart', resumed, 'restarted-trf', r, {}, timings))
    return dict(runs=results, geometry_policy=geometry_policy,
        restart_semantics='Restart from best feasible parameters with cleared optimizer history, not exact checkpoint resume')
