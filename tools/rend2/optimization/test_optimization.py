"""Numerical conformance, counter, ambiguity and boundary safeguards.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import math
from pathlib import Path
import tempfile
import unittest

from environment import np, HERE
from routines import (Objective, Work, WorkLimit, Cancelled, Infeasible, interval_residual,
                      solve_objective, linear_fit)
from problems import (Rows, compiler_rgb, contribution_matrix, original_intervals,
                      geometry_problem, shadow_value, shadow_problem, compiled22, compiled12)
from controls import derivative_controls
from experiments import METHODS, indirect_blocks, model_selection, censored_and_robust, shadow_hybrid
from analytic import rectangle_reference
from run import write


class LinearMethods(unittest.TestCase):
    def test_known_positive_rgb_solution(self):
        A = np.array([[1., .1], [.2, 1.], [.5, .7], [.9, .3]])
        x = np.array([[1., 2., 3.], [3., 2., 1.]])
        for method in ('nnls', 'bvls', 'trf_linear', 'coordinate', 'group_prox'):
            r = linear_fit(A, A@x, method, passes=2000, budget=30000)
            self.assertLess(np.max(abs(np.array(r['x'])-x)), 1e-6, method)
            self.assertEqual(r['rank'], 2)

    def test_nnls_kkt_with_negative_unconstrained_component(self):
        A = np.array([[1., 1.], [1., 0.]])
        Y = np.array([[0., 0., 0.], [1., 1., 1.]])
        r = linear_fit(A, Y, 'nnls')
        self.assertTrue(np.all(np.asarray(r['x']) >= 0)); self.assertLess(r['kkt_violation'], 1e-10)

    def test_rank_deficiency_is_not_numerical_failure(self):
        A = np.array([[1., 1.], [2., 2.], [3., 3.]])
        Y = np.array([[2., 2., 2.], [4., 4., 4.], [6., 6., 6.]])
        for method in ('nnls', 'coordinate', 'bvls'):
            r = linear_fit(A, Y, method)
            self.assertEqual(r['identification'], 'RankDeficient'); self.assertIsNone(r['condition'])
            self.assertLess(r['objective'], 1e-15)

    def test_zero_sources_and_response(self):
        A = np.array([[1., 0.], [2., 0.], [3., 0.]])
        r = linear_fit(A, np.zeros((3, 3)), 'nnls')
        self.assertEqual(r['x'], [[0.]*3, [0.]*3]); self.assertEqual(r['rank'], 1)

    def test_penalty_does_not_masquerade_as_nnls(self):
        A = np.eye(2); Y = np.ones((2, 3))
        raw = linear_fit(A, Y, 'group_prox', penalty=0.)
        sparse = linear_fit(A, Y, 'group_prox', penalty=3.)
        self.assertLess(raw['objective'], 1e-15); self.assertGreater(sparse['objective'], 1.)
        self.assertIsNone(sparse['kkt_violation'])

    def test_work_budget_retains_partial_solution(self):
        r = linear_fit(np.ones((4, 3)), np.ones((4, 3)), 'coordinate', budget=1)
        self.assertEqual(r['status'], 'WorkLimit'); self.assertTrue(np.all(np.asarray(r['x']) >= 0))

    def test_nonfinite_negative_and_shapes(self):
        for A in (np.array([[float('nan')]]), np.array([[-1.]])):
            with self.assertRaises(ValueError): linear_fit(A, np.ones((1, 3)), 'nnls')
        with self.assertRaises(ValueError): linear_fit(np.ones((2, 1)), np.ones((3, 3)), 'nnls')


class Objectives(unittest.TestCase):
    def scalar(self, budget=300, **kw):
        return Objective(lambda x, w: np.array([x[0]]), [0.], [1.], [.8], [.2], [.2], budget=budget, **kw)

    def test_local_methods_common_objective_and_bounds(self):
        for method in METHODS:
            r = solve_objective(self.scalar(), method, iterations=90)
            self.assertLess(abs(r['x'][0]-.2), .03 if method == 'adam' else 1e-5, method)
            self.assertTrue(0 <= r['x'][0] <= 1)

    def test_fd_counts_all_forward_calls(self):
        obj = self.scalar(); obj.jac([.8])
        self.assertEqual(obj.work.forward, 2); self.assertEqual(obj.work.derivative, 1)

    def test_exact_target_analytic_jacobian_not_censored(self):
        def jac(x, work): return np.array([x[0]]), np.array([[1.]])
        obj = self.scalar(jacobian=jac)
        self.assertEqual(obj.jac([.2])[0, 0], 1.)

    def test_work_exhaustion_and_cancel_restart(self):
        obj = self.scalar(budget=1); r = solve_objective(obj, 'trf')
        self.assertEqual(r['status'], 'WorkLimit'); self.assertIsNotNone(r['objective'])
        obj = self.scalar(); obj.cancelled = lambda: obj.work.forward >= 2
        r = solve_objective(obj, 'trf'); self.assertEqual(r['status'], 'Cancelled')
        obj = self.scalar(); obj.x0 = np.array(r['x'])
        resumed = solve_objective(obj, 'trf'); self.assertLess(abs(resumed['x'][0]-.2), 1e-5)

    def test_reproducible_numerics_not_wall_time(self):
        a, b = solve_objective(self.scalar(), 'trf'), solve_objective(self.scalar(), 'trf')
        for r in (a, b): r.pop('wall_seconds')
        self.assertEqual(a, b)

    def test_invalid_box_and_nonfinite_prediction(self):
        with self.assertRaises(Infeasible): Objective(lambda x, w: x, [1.], [0.], [.5], [1.], [1.])
        with self.assertRaises(ValueError): Objective(lambda x, w: x, [0.], [1.], [.5], [float('nan')], [1.])
        obj = self.scalar(); obj.model = lambda x, w: [float('inf')]
        with self.assertRaises(ValueError): obj.residual([.5])
        self.assertEqual(solve_objective(obj, 'trf')['status'], 'NumericalFailure')

    def test_robust_losses_are_not_censoring(self):
        for loss in ('linear', 'huber', 'cauchy'):
            obj = self.scalar(loss=loss, loss_scale=.1)
            value, psi = obj.loss_terms(np.array([100.]))
            self.assertGreater(value, 0.); self.assertGreater(psi[0], 0.)
        self.assertEqual(interval_residual([100.], [1.], [np.inf])[0], 0.)

    def test_original_truncation_and_black(self):
        lo, hi = original_intervals([[0, 128, 255]])
        self.assertEqual(lo.tolist(), [[0., 128., 255.]])
        self.assertEqual(hi[0, 1], 129.); self.assertTrue(math.isinf(hi[0, 2]))
        self.assertTrue(np.all(interval_residual([[.2, 128.5, 1000]], lo, hi) == 0))


class ForwardAndGeometry(unittest.TestCase):
    def rows(self):
        return Rows(np.array([[0., 0., 0.], [64., 0., 0.]]), np.array([[0., 0., 1.]]*2),
                    np.zeros((2, 3)), np.ones(2), ('train', 'heldout'))

    def test_known_kernel_point_matches_active_matrix(self):
        rows = self.rows(); q = np.array([[300., 100., 20.]])
        direct = compiler_rgb(rows, [[0, 0, 192]], q, encoded=False)
        self.assertTrue(np.allclose(direct, contribution_matrix(rows, [[0, 0, 192]])@q))

    def test_linear_is_not_gain_times_column(self):
        rows = self.rows(); origin = [[0, 0, 192]]
        a = compiler_rgb(rows, origin, [[300.]*3], 'linear', encoded=False)
        b = compiler_rgb(rows, origin, [[600.]*3], 'linear', encoded=False)
        self.assertFalse(np.allclose(2*a, b))

    def test_shared_peak_saturation_not_channel_clipping(self):
        rows = self.rows(); origin = [[0, 0, 192]]
        a = compiler_rgb(rows, origin, [[3000, 1500, 600]], encoded=True)
        b = compiler_rgb(rows, origin, [[6000, 3000, 1200]], encoded=True)
        self.assertTrue(np.allclose(a, b)); self.assertLess(a[0, 1], 200)

    def test_unknown_profile_and_unknown_visibility(self):
        from hypotheses import UnspecifiedModel
        rows = self.rows()
        with self.assertRaises(UnspecifiedModel): compiler_rgb(rows, [[0, 0, 192]], [[300]*3], 'UnknownProfile')
        rows.masks = np.full((2, 1), None, dtype=object)
        with self.assertRaises(UnspecifiedModel): compiler_rgb(rows, [[0, 0, 192]], [[300]*3])

    def test_coincident_receiver_rejected_near_distance_bounded(self):
        rows = self.rows()
        with self.assertRaises(ValueError): compiler_rgb(rows, [[0, 0, 0]], [[300]*3])
        self.assertTrue(np.all(np.isfinite(compiler_rgb(rows, [[0, 0, 1e-8]], [[300]*3]))))

    def test_analytic_ad_and_multiple_fd_steps(self):
        r = derivative_controls()
        self.assertAlmostEqual(r['analytic_value'], r['kernel_value'])
        self.assertLess(r['trials'][1]['max_error'], 1e-8)
        self.assertLess(r['trials'][2]['max_error'], 1e-7)

    def test_shadow_reference_and_polygon_agree(self):
        b = (1.3, 1.4, -.05, .05)
        a = shadow_value(b, 4., 'reference', Work()); p = shadow_value(b, 4., 'polygon', Work())
        self.assertAlmostEqual(a, p, places=8)
        h = 1e-4
        d = (shadow_value(b, 4+h, 'reference', Work())-shadow_value(b, 4-h, 'reference', Work()))/(2*h)
        self.assertAlmostEqual(d, rectangle_reference(b, (0, 0, 4))['jacobian'][2], places=7)

    def test_fixed_samples_zero_visibility_derivative(self):
        def visibility(H): return sum(1.3+(i+.5)*.1/5 >= H/(H-1) for i in range(5))/5
        self.assertEqual((visibility(4.00001)-visibility(3.99999))/.00002, 0.)
        self.assertGreater(rectangle_reference((1.3, 1.4, -.05, .05), (0, 0, 4))['visibility_jacobian'][2], 1.)

    def test_surrogate_warm_start_strict_refinement_and_budget(self):
        r = shadow_hybrid(.9, 180)
        self.assertLess(r['position_error_metres'], 1e-5)
        self.assertLessEqual(sum(r['phase_forward']), 180)
        self.assertEqual(r['phase_statuses'][1], 'Stationary')

    def test_spot_core_unidentifiable_not_perfect_source_geometry(self):
        obj, meta = geometry_problem('spot_core', [.3]*3)
        r = solve_objective(obj, 'trf')
        self.assertLess(r['objective'], 1e-20)
        self.assertGreater(np.linalg.norm(meta['decode'](r['x'])-meta['truth']), 1.)

    def test_censored_beats_naive_clipped_control(self):
        records = censored_and_robust([])
        naive, proper = records[:2]
        self.assertGreater(naive['truth_error'], 1.)
        self.assertLess(proper['truth_error'], .01)

    def test_constrained_transport_versus_flexible_collapse(self):
        r = indirect_blocks()
        self.assertEqual(r['flexible']['physical_source_recall'], 0)
        matched = next(v for v in r['passive_transport'] if v['physical_bounce_conditional'])
        self.assertLess(matched['intensity_error'], 1e-12); self.assertEqual(matched['false_wall_energy'], 0.)
        self.assertTrue(all(v['monotone'] for v in r['alternating']))

    def test_bounded_discrete_model_selection(self):
        r = model_selection([])
        self.assertEqual(len(r['subset_trials']), 15)
        self.assertEqual(r['selected']['active'], [0, 1])
        self.assertEqual(r['selected']['source_metrics']['true_positive'], 2)
        self.assertIsNone(r['source_existence_probability'])

    def test_compiler_fixture_if_available(self):
        root = HERE.parents[2]/'build/bake-model-comparison-final'
        if not root.exists(): self.skipTest('Original pinned compiler BSPs not present')
        for name in ('inverse_square', 'linear', 'spot', 'colour_clip'):
            r = compiled22(root, name)
            self.assertTrue(r['metadata']['kernel_replay_passed'])
            self.assertEqual(set(r['rows'].partition), {'train', 'validation', 'heldout'})

    def test_real_multisource_and_shadow_folds_are_not_empty(self):
        root = HERE.parents[2]
        inputs = root/'build/x02/compiled-fixtures-frozen'; recipes = root/'build/q3map2-bake-verified/answers'
        rays = root/'build/lightrecon-core-renderers/Release/lightrecon_profile_rays.exe'
        if not inputs.exists() or not rays.exists(): self.skipTest('Compiled archives/ray executable unavailable')
        with tempfile.TemporaryDirectory() as temp:
            for name in ('two_colour', 'door_occlusion'):
                data = compiled12(inputs, recipes, name, rays, Path(temp))
                self.assertGreater(data['metadata']['geometry_rays'], 0)
                for role in ('train', 'validation', 'heldout'): self.assertGreater(len(data['rows'].select(role).rgb), 2)
                if name == 'door_occlusion': self.assertGreater(data['metadata']['blocked_rays'], 0)


if __name__ == '__main__': unittest.main()
