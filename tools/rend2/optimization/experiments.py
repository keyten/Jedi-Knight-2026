"""Independent A--F experiments; no production session/source discovery.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import itertools
import math
from environment import np, optimize
from routines import Objective, Work, linear_fit, solve_objective, interval_residual
from problems import (Rows, compiler_rgb, contribution_matrix, original_intervals, rgb_objective,
                      geometry_problem, shadow_problem, shadow_value, smooth_rows)
from analytic import rectangle_reference, smooth_point_ad, frozen_visibility_derivative
from transport import Transfer, Edge, fit_smooth
from metrics import detection, angle

METHODS = ('trf', 'projected_lm', 'lbfgsb', 'adam', 'powell')


def rmse(a, b):
    a, b = np.asarray(a), np.asarray(b)
    return float(np.sqrt(np.mean((a-b)**2))) if a.size else None


def encoded_error(prediction, target):
    lo, hi = original_intervals(target)
    return rmse(interval_residual(prediction, lo, hi), np.zeros_like(target))


def record_problem(label, obj, method, result, metadata, timings):
    elapsed = result.pop('wall_seconds'); timings.append(dict(problem=label, method=method, wall_seconds=elapsed))
    return dict(problem=label, method=method, **metadata, **result)


def fixed_geometry(datasets, seeds, timings):
    output = []
    for data in datasets:
        if data['profile'] != 'isq': continue
        train, hold = data['rows'].select('train'), data['rows'].select('heldout')
        if not len(train.rgb) or not len(hold.rgb): continue
        # Known gamma=1 is insufficient if ColorToBytes has normalized a saturated
        # RGB peak. Admit only uncensored samples to this linear-block comparison.
        original_count = len(train.rgb); ids = np.where(np.max(train.rgb, axis=1) < 254)[0]
        if not len(ids): continue
        train = Rows(train.position[ids], train.normal[ids], train.rgb[ids], train.weight[ids],
            tuple('train' for _ in ids), train.masks[ids] if train.masks is not None else None)
        A = contribution_matrix(train, data['origins']); B = contribution_matrix(hold, data['origins'])
        rootw = np.sqrt(train.weight/train.weight.sum())
        norms = np.maximum(1e-16, np.linalg.norm(A*rootw[:, None], axis=0))
        An = A*rootw[:, None]/norms
        for seed in seeds:
            rng = np.random.default_rng(seed)
            Y = np.maximum(0., train.rgb+rng.normal(0., .25, train.rgb.shape))*rootw[:, None]
            for method in ('nnls', 'bvls', 'trf_linear', 'coordinate', 'group_prox'):
                result = linear_fit(An, Y, method, penalty=.02 if method == 'group_prox' else 0.)
                q = np.array(result['x'])/norms[:, None]
                strict = compiler_rgb(hold, data['origins'], q, work=Work())
                result['strict_heldout_interval_rmse'] = encoded_error(strict, hold.rgb)
                result['affine_heldout_rmse'] = rmse(B@q, hold.rgb)
                result['relative_amplitude_error'] = float(np.linalg.norm(q-data['amplitudes'])/max(1e-20, np.linalg.norm(data['amplitudes'])))
                result['units'] = 'Compiler relative; least-squares active-branch approximation, strict encoded reevaluation'
                result['recovered_rgb_parameters'] = q.tolist()
                result['uncensored_training_samples'] = len(train.rgb)
                result['peak_censored_training_samples_excluded_from_linear_block'] = original_count-len(train.rgb)
                result['training_branch_discrepancy'] = rmse(A@q, compiler_rgb(train, data['origins'], q, encoded=False))
                output.append(record_problem('A/'+data['name'], None, method, result, dict(seed=seed), timings))
    # Algebraic controls diagnose indistinguishability without pretending to be bakes.
    x = np.linspace(.1, 1., 48); columns = np.column_stack((x, x*(1+1e-8*np.linspace(-1, 1, 48))))
    true = np.array([[2., .1, .1], [.1, .1, 2.]])
    for label, A in [('near_correlated', columns), ('co_located', np.column_stack((x, x))), ('zero_column', np.column_stack((x, x*0)))]:
        for dtype in (np.float64, np.float32):
            matrix = A.astype(dtype); Y = (A@true).astype(dtype)
            for method in ('nnls', 'coordinate', 'bvls'):
                r = linear_fit(matrix, Y, method)
                r['allocation_error'] = float(np.linalg.norm(np.array(r['x'])-true))
                output.append(record_problem('A/control/'+label, None, method, r, dict(dtype=str(dtype)), timings))
    return output


def uncertain_profiles(datasets, timings, budget=700):
    output = []
    for data in datasets:
        if data['name'] not in ('inverse_square', 'linear', 'spot', 'colour_clip', 'two_colour'): continue
        train, val, hold = (data['rows'].select(p) for p in ('train', 'validation', 'heldout'))
        if min(len(train.rgb), len(val.rgb), len(hold.rgb)) == 0: continue
        for profile, initial_amplitude in itertools.product(('isq', 'linear', 'spot'), (80., 240., 600.)):
            q0 = np.full(data['amplitudes'].size, initial_amplitude)
            obj = rgb_objective(train, data['origins'], q0, profile, budget=budget)
            r = solve_objective(obj, 'trf')
            q = np.array(r['x']).reshape(-1, 3)*4000
            r['validation_interval_rmse'] = encoded_error(compiler_rgb(val, data['origins'], q, profile), val.rgb)
            r['heldout_interval_rmse'] = encoded_error(compiler_rgb(hold, data['origins'], q, profile), hold.rgb)
            r['correct_formula_family'] = profile == data['profile']
            r['parameter_budget'] = int(q.size); r['fitted_compiler_rgb'] = q.tolist()
            r['historical_stock_identity'] = 'UnknownProfile'
            output.append(record_problem('B/'+data['name'], obj, 'trf/'+profile, r,
                dict(initial_amplitude=initial_amplitude), timings))
    return output


def continuous(seeds, noises, timings, budget=700):
    output = []
    for kind in ('xyz', 'xyz_intensity', 'single_plane', 'distance_ambiguity', 'spot_axis_cone', 'spot_core', 'rectangle', 'area_far'):
        dimension = 4 if kind in ('xyz_intensity', 'distance_ambiguity') else 2 if kind == 'rectangle' else 3
        starts = (np.full(dimension, .3), np.full(dimension, .7))
        for seed, noise, start in itertools.product(seeds, noises, starts):
            for method in METHODS:
                obj, meta = geometry_problem(kind, start, noise, seed, budget)
                result = solve_objective(obj, method, iterations=100)
                p = meta['decode'](result['x']); hold = meta['rows'].select('heldout')
                predicted = meta['response'](p, hold, Work())
                truth = meta['response'](meta['truth'], hold, Work())
                result.update(parameters=p.tolist(), strict_heldout_rmse=rmse(predicted, hold.rgb),
                    noiseless_heldout_rmse=rmse(predicted, truth),
                    parameter_error=float(np.linalg.norm((p-meta['truth'])/(meta['upper']-meta['lower']))))
                if kind in ('xyz', 'xyz_intensity', 'single_plane', 'distance_ambiguity'):
                    result['position_error_world'] = float(np.linalg.norm(p[:3]-meta['truth'][:3]))
                    result['position_error_metres'] = result['position_error_world']/32.
                if kind in ('spot_axis_cone', 'spot_core'):
                    result['axis_error_degrees'] = angle([*p[:2], -1.], [*meta['truth'][:2], -1.])
                    result['cone_error_degrees'] = abs(math.degrees(math.atan((p[2]+16)/128)-math.atan((meta['truth'][2]+16)/128)))
                if kind in ('rectangle', 'area_far'):
                    result['dimension_relative_error'] = float(np.max(abs(p[:2]-meta['truth'][:2])/meta['truth'][:2]))
                # Unweighted physical response sensitivity, NOT derivative of a
                # fully satisfied quantization interval or a posterior covariance.
                J = []
                for k in range(len(p)):
                    h = (meta['upper'][k]-meta['lower'][k])*1e-5; a, b = p.copy(), p.copy(); a[k]+=h; b[k]-=h
                    J.append(((meta['response'](a, hold, Work())-meta['response'](b, hold, Work()))/(2*h)).ravel())
                s = np.linalg.svd(np.column_stack(J)*(meta['upper']-meta['lower']), compute_uv=False)
                result['scaled_response_singular_values'] = s.tolist()
                result['rank_relative_1e6'] = int(sum(s > max(1e-12, s[0]*1e-6)))
                output.append(record_problem('C/'+kind, obj, method, result,
                    dict(seed=seed, noise=noise, start=start.tolist(), units=meta['physical_units']), timings))
    return output


def compiled_positions(datasets, timings, budget=700):
    """Actual BSP RGB targets, fixed known intensity/profile; XYZ is the only block.
    Distinct from generated smooth targets and unknown-amplitude production fitting.
    """
    out = []
    lower, upper = np.array([-128., -128., 96.]), np.array([128., 128., 300.])
    for data in datasets:
        if data['name'] not in ('inverse_square', 'spot', 'colour_clip'): continue
        train, hold = (data['rows'].select(p) for p in ('train', 'heldout'))
        lo, hi = original_intervals(train.rgb)
        for start in (.3, .7):
            for method in METHODS:
                def model(x, work):
                    return compiler_rgb(train, [lower+np.asarray(x)*(upper-lower)], data['amplitudes'], data['profile'], work=work)
                obj = Objective(model, [0.]*3, [1.]*3, [start]*3, lo, hi, scale=64., budget=budget)
                r = solve_objective(obj, method, iterations=100)
                p = lower+np.array(r['x'])*(upper-lower)
                r.update(position_error_world=float(np.linalg.norm(p-data['origins'][0])),
                    position_error_metres=float(np.linalg.norm(p-data['origins'][0]))/32.,
                    strict_heldout_interval_rmse=encoded_error(compiler_rgb(hold, [p], data['amplitudes'], data['profile']), hold.rgb),
                    fixed_photometry_protocol='Known compiler RGB, not fitted source intensities',
                    source_geometry_confidence=None)
                out.append(record_problem('C/compiled/'+data['name'], obj, method, r, dict(start=start), timings))
    return out


def shadow_experiments(timings, budget=250):
    output = []; derivatives = []
    bounds = (1.3, 1.4, -.05, .05); source = (0., 0., 4.)
    ref = rectangle_reference(bounds, source)
    for evaluation in ('reference', 'centre', 'grid4', 'adaptive', 'polygon', 'surrogate'):
        for h in (1e-4, .001, .01, .1):
            w = Work(); numeric = (shadow_value(bounds, 4+h, evaluation, w)-shadow_value(bounds, 4-h, evaluation, w))/(2*h)
            derivatives.append(dict(evaluator=evaluation, step=h, derivative=numeric,
                reference_derivative=ref['jacobian'][2], error=abs(numeric-ref['jacobian'][2]), work=w.__dict__.copy()))
        for start in (.12, .6, .9):
            # Same source-height parameter, targets, bounds and forward budget.
            for method in ('trf', 'projected_lm', 'lbfgsb', 'powell'):
                obj, meta = shadow_problem(evaluation, start, budget)
                r = solve_objective(obj, method, iterations=80)
                height = 2+6*r['x'][0]
                # Final scoring is always independent stricter physical integral.
                held = [(1.2, 1.28, -.03, .03), (1.31, 1.39, -.03, .03), (1.4, 1.48, -.03, .03)]
                predicted = [shadow_value(b, height, 'reference', Work()) for b in held]
                target = [shadow_value(b, 4., 'reference', Work()) for b in held]
                r.update(height_metres=height, position_error_metres=abs(height-4.),
                         strict_heldout_rmse=rmse(predicted, target), evaluator=evaluation)
                output.append(record_problem('D/moving_edge/'+evaluation, obj, method, r, dict(start=start), timings))
    for start in (.12, .6, .9):
        result = shadow_hybrid(start, budget)
        output.append(record_problem('D/moving_edge/hybrid', None, 'trf_surrogate_then_reference', result,
                                     dict(start=start), timings))
    h = 1e-5
    def fixed_visibility(height):
        edge = height/(height-1)
        return sum(1.3+(i+.5)*.1/5 >= edge for i in range(5))/5
    return dict(runs=output, derivatives=derivatives,
        fixed_visibility_derivative=(fixed_visibility(4+h)-fixed_visibility(4-h))/(2*h),
        reference_visibility_derivative=ref['visibility_jacobian'][2],
        frozen_visibility_response_derivative=frozen_visibility_derivative(bounds, source, n=16)[2],
        claim='Physical SI planar integral controls, not historical compiler boundary replay')


def shadow_hybrid(start, budget=180):
    coarse, _ = shadow_problem('surrogate', start, budget//2)
    first = solve_objective(coarse, 'trf')
    exact, _ = shadow_problem('reference', first['x'][0], budget-budget//2)
    second = solve_objective(exact, 'trf')
    height = 2+6*second['x'][0]
    held = [(1.2, 1.28, -.03, .03), (1.31, 1.39, -.03, .03), (1.4, 1.48, -.03, .03)]
    second.update(height_metres=height, position_error_metres=abs(height-4.),
        strict_heldout_rmse=rmse([shadow_value(b, height, 'reference', Work()) for b in held],
                                 [shadow_value(b, 4., 'reference', Work()) for b in held]),
        phase_statuses=[first['status'], second['status']], phase_forward=[first['work']['forward'], second['work']['forward']])
    second['work'] = {k: first['work'][k]+second['work'][k] for k in first['work']}
    second['wall_seconds'] += first['wall_seconds']
    return second


def censored_and_robust(timings):
    output = []
    # Independent per-channel LDR fixture. This operator differs explicitly from
    # q3map shared-peak ColorToBytes, tested separately by the compiler colour_clip case.
    response = np.array([.2, .4, .7, 1., 1.4, 2.]); truth = 2.; raw = np.floor(np.minimum(1., truth*response)*255+.5)
    lo = np.maximum(0., (raw-.5)/255); hi = np.where(raw == 255, np.inf, (raw+.5)/255)
    for label, lower, upper in [('naive_l2_on_clipped', raw/255, raw/255), ('censored', lo, hi)]:
        def model(x, work): work.scalar_responses += len(response); return x[0]*4*response
        obj = Objective(model, [0.], [1.], [.2], lower, upper, budget=500)
        r = solve_objective(obj, 'trf'); r['amplitude'] = r['x'][0]*4; r['truth_error'] = abs(r['amplitude']-truth)
        output.append(record_problem('loss/'+label, obj, 'trf', r, {}, timings))
    x = np.linspace(.1, 1., 40); clean = x*2
    for noise, seed in itertools.product((.0, .03), (11, 37, 71)):
        rng = np.random.default_rng(seed); target = clean+rng.normal(0., noise, len(x)); target[::9] += 2.
        weights = np.ones(len(x)); weights[-10:] = .5
        for loss in ('linear', 'huber', 'cauchy'):
            def model(p, work): work.scalar_responses += len(x); return x*p[0]*4
            obj = Objective(model, [0.], [1.], [.2], target, target, weights, budget=500,
                            loss=loss, loss_scale=.02)
            r = solve_objective(obj, 'trf'); r['amplitude'] = r['x'][0]*4
            r['clean_heldout_rmse'] = rmse(r['amplitude']*np.linspace(.15, .95, 17), 2*np.linspace(.15, .95, 17))
            output.append(record_problem('loss/outliers', obj, 'trf/'+loss, r, dict(noise=noise, seed=seed), timings))
    return output


def model_selection(timings):
    rows = smooth_rows(); origins = np.array([[-80., 0., 160.], [80., 0., 160.], [-79., 0., 160.], [0., 0., 200.]])
    A = contribution_matrix(rows, origins); true = np.array([[250., 20., 0.], [0., 30., 200.], [0, 0, 0], [0, 0, 0]])
    rows.rgb = A@true; train, val, held = (rows.select(p) for p in ('train', 'validation', 'heldout'))
    matrices = [contribution_matrix(rr, origins) for rr in (train, val, held)]
    norms = np.linalg.norm(matrices[0], axis=0); X = matrices[0]/norms
    trials = []
    # Exhaustive bounded discrete model selection; exactly 16 tested source masks.
    for n in range(1, 5):
        for active in itertools.combinations(range(4), n):
            r = linear_fit(X[:, active], train.rgb, 'nnls'); q = np.zeros((4, 3)); q[list(active)] = np.array(r['x'])/norms[list(active), None]
            validation = rmse(matrices[1]@q, val.rgb)
            score = validation**2+.001*len(active)
            r.update(active=list(active), validation_rmse=validation, selection_score=score, q=q.tolist())
            trials.append(record_problem('E/subsets', None, 'nnls', r, {}, timings))
    chosen = min(trials, key=lambda r: (r['selection_score'], len(r['active'])))
    sparse = []
    for strength in (0., .01, .1, 1.):
        r = linear_fit(X, train.rgb, 'group_prox', penalty=strength, passes=800)
        q = np.array(r['x'])/norms[:, None]; active = [i for i, row in enumerate(q) if np.linalg.norm(row) > 1e-3]
        r.update(active=active, q=q.tolist(), validation_rmse=rmse(matrices[1]@q, val.rgb))
        sparse.append(record_problem('E/group_sparse', None, 'group_prox', r, dict(strength=strength), timings))
    truth_sources = [dict(kind='point', position=list(origins[i])) for i in (0, 1)]
    for r in [chosen]+sparse:
        r['heldout_rmse'] = rmse(matrices[2]@np.array(r['q']), held.rgb)
        r['source_metrics'] = detection(truth_sources, [dict(kind='point', position=list(origins[i])) for i in r['active']],
            dict(position_world=8., angle_degrees=5., cone_degrees=3., dimension_relative=.1, roll_degrees=5.), 32.)
    # Type is discrete: same spot/point geometry, cone-edge receivers held out.
    data = smooth_rows(); q = np.array([[300., 300., 300.]])
    data.rgb = compiler_rgb(data, [[0, 0, 192]], q, 'spot')
    types = []
    for profile in ('isq', 'spot'):
        rr = data.select('train'); obj = rgb_objective(rr, [[0, 0, 192]], [240]*3, profile, budget=500)
        r = solve_objective(obj, 'trf'); amplitude = np.array(r['x']).reshape(1, 3)*4000
        valid = data.select('validation'); heldout = data.select('heldout')
        r.update(profile=profile, validation_rmse=encoded_error(compiler_rgb(valid, [[0, 0, 192]], amplitude, profile), valid.rgb),
            heldout_rmse=encoded_error(compiler_rgb(heldout, [[0, 0, 192]], amplitude, profile), heldout.rgb))
        types.append(record_problem('E/type', obj, 'trf/'+profile, r, {}, timings))
    return dict(subset_trials=trials, selected={k: v for k, v in chosen.items() if k not in ('problem', 'method')}, sparse=sparse, type_trials=types,
                source_existence_probability=None, claim='Candidate geometry fixed before data; discrete complexity, not confidence')


def colour_constraints(timings):
    rows = smooth_rows(); origins = np.array([[-80., 0., 160.], [80., 0., 160.]])
    colours = np.array([[1., .12, .03], [.04, .2, 1.]])
    amplitudes = np.array([250., 300.]); A = contribution_matrix(rows, origins)
    rows.rgb = A@(amplitudes[:, None]*colours)
    train, hold = rows.select('train'), rows.select('heldout')
    T, H = contribution_matrix(train, origins), contribution_matrix(hold, origins)
    out = []
    for seed, noise in itertools.product((11, 37), (.5, 2.)):
        Y = np.maximum(0., train.rgb+np.random.default_rng(seed).normal(0., noise, train.rgb.shape))
        for mode in ('independent_rgb', 'known_chroma', 'wrong_white_prior'):
            if mode == 'independent_rgb':
                result = linear_fit(T, Y, 'nnls'); q = np.array(result['x']); dof = 6
            else:
                chroma = colours if mode == 'known_chroma' else np.ones_like(colours)
                D = np.column_stack([(T[:, j, None]*chroma[j]).ravel() for j in range(2)])
                import time
                tick = time.perf_counter(); values = optimize.nnls(D, Y.ravel())[0]
                q = values[:, None]*chroma; dof = 2
                result = dict(x=values.tolist(), objective=float(np.mean((T@q-Y)**2)), status='Stationary',
                              work=Work().__dict__.copy(), wall_seconds=time.perf_counter()-tick)
            result.update(heldout_rmse=rmse(H@q, hold.rgb), parameter_budget=dof,
                source_chromaticity_error=float(np.mean(np.linalg.norm(q/np.maximum(1e-30, q.sum(axis=1))[:, None]-
                                                colours/colours.sum(axis=1)[:, None], axis=1))))
            out.append(record_problem('colour/parameterization', None, mode, result, dict(seed=seed, noise=noise), timings))
    return out


def indirect_blocks():
    network = Transfer((1.,)*5, (Edge(0, 1, .2, .2), Edge(1, 2, .15, .15), Edge(0, 3, .1, .1)), 'bounded-own-scene-v1')
    D = np.array([1., .15, .02, .4, 0.]); rho = .5
    bounce = np.array(network.apply([[v]*3 for v in D], [[rho]*3]*5)[0])[:, 0]
    target = 3.*(D+bounce); train = [0, 1, 3]; held = [2, 4]
    flexible_I, z = fit_smooth(D[train].tolist(), target[train].tolist(), [0., 1., 3.], source_penalty=.02, smoothness=0.)
    outcomes = []
    for label, direct in [('correct_profile', D), ('wrong_profile', D*np.array([1., .5, 2., .6, 1.]))]:
        for assumed_rho in (.2, .5, .8):
            transfer = np.array(network.apply([[v]*3 for v in direct], [[assumed_rho]*3]*5)[0])[:, 0]
            response = direct+transfer
            I = max(0., float(response[train]@target[train]/(response[train]@response[train])))
            outcomes.append(dict(profile=label, assumed_reflectance=assumed_rho, intensity=I,
                intensity_error=abs(I-3.), train_rmse=rmse(I*response[train], target[train]),
                heldout_rmse=rmse(I*response[held], target[held]), false_wall_energy=float(I*response[4]),
                physical_bounce_conditional=label == 'correct_profile' and assumed_rho == .5))
    # Actual alternating updates of a passive transfer fraction and direct I.
    schedules = []
    base_transfer = bounce/rho
    for start in (.1, .9):
        I, r = 1., start; history = []
        for _ in range(40):
            response = D+r*base_transfer
            I = max(0., float(response[train]@target[train]/(response[train]@response[train])))
            Z = I*base_transfer[train]
            r = float(np.clip(Z@(target[train]-I*D[train])/max(1e-20, Z@Z), .05, .95))
            history.append(float(np.mean((I*(D+r*base_transfer)[train]-target[train])**2)))
        schedules.append(dict(start=start, intensity=I, reflectance=r, final_mse=history[-1],
                              monotone=all(b <= a+1e-12 for a, b in zip(history, history[1:])), iterations=40))
    return dict(known_intensity=3., known_reflectance=rho,
        flexible=dict(intensity=flexible_I, train_residual=z, physical_source_recall=int(flexible_I>.01),
            physical_attribution=False, claim='Exact flexible residual suppression, not a valid physical GI solution'),
        passive_transport=outcomes, alternating=schedules,
        unknown_optics_result='Unavailable transport edge; no invented transparent/opaque value',
        unknown_profile_policy='Separate direct-model discrepancy; do not relabel wrong-profile residual as GI')
