#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
import json
import math
import struct
import unittest

from shadow_localization import (candidate_demo, correspondence_demo, dot, edge_plane,
                                 extent_from_two_widths, footprint_demo, height_band, intersect_caster_lines,
                                 intersect_edge_planes, penumbra_width, point_response,
                                 report, rgb8, shadow_point, solve_small,
                                 source_separation_demo, visible_footprint, wall_blocks)


class LocalizationChecks(unittest.TestCase):
    def assertVectorClose(self, a, b, tolerance=1e-11):
        self.assertEqual(len(a), len(b))
        self.assertLessEqual(max(abs(x - y) for x, y in zip(a, b)), tolerance)

    def test_source_direction_and_inverse_square(self):
        self.assertAlmostEqual(point_response((0, 0, 4), (0, 0, 0)), 1 / 16)
        self.assertEqual(point_response((0, 0, -4), (0, 0, 0)), 0)

    def test_singularity_not_softened(self):
        with self.assertRaises(ValueError):
            point_response((0, 0, 0), (0, 0, 0))

    def test_three_rgb_observations_ambiguous(self):
        d = candidate_demo()
        self.assertEqual(d["rgb8"], [128, 128, 128])
        self.assertEqual(d["intensity_only_candidates"], 301)

    def test_shadow_reduces_but_not_eliminates_uncertainty(self):
        d = candidate_demo()
        self.assertEqual(d["shadow_band_candidates"], 9)
        lo, hi = d["height_band_m"]
        self.assertLess(lo, 4)
        self.assertGreater(hi, 4)
        self.assertGreater(hi - lo, 0.18)

    def test_exact_height_interval_endpoints(self):
        lo, hi = height_band(4 / 3, 0.01)
        self.assertAlmostEqual(shadow_point((0, 0, lo), (1, 0, 1))[0], 4 / 3 + 0.01)
        self.assertAlmostEqual(shadow_point((0, 0, hi), (1, 0, 1))[0], 4 / 3 - 0.01)

    def test_distant_limit_not_finite_error_bar(self):
        lo, hi = height_band(1.005, 0.01)
        self.assertGreater(lo, 60)
        self.assertIsNone(hi)

    def test_invalid_boundary_intervals(self):
        for b, t in ((1, 0), (0.5, 0.01), (1.3, -1), (math.inf, 0.1), (1.3, math.nan)):
            with self.assertRaises(ValueError):
                height_band(b, t)

    def test_independent_trace_matches_analytic_shadow(self):
        for h in (2, 4, 8):
            boundary = shadow_point((0, 0, h), (1, 0, 1))[0]
            self.assertTrue(wall_blocks((0, 0, h), (boundary - 1e-5, 0, 0), 1, 1))
            self.assertFalse(wall_blocks((0, 0, h), (boundary + 1e-5, 0, 0), 1, 1))

    def test_heldout_shadow_masks_reject_wrong_locations(self):
        masks = candidate_demo()["blocked_at_height_m"]
        self.assertEqual(masks["2.0"], [True, True, True])
        self.assertEqual(masks["4.0"], [True, False, False])
        self.assertEqual(masks["8.0"], [False, False, False])

    def test_finite_wall_opening(self):
        self.assertTrue(wall_blocks((1, 0, 3), (0, 0, 0), 0.5, 2, 0.5))
        self.assertFalse(wall_blocks((1, 0, 3), (0, 2, 0), 0.5, 2, 0.5))

    def test_shadow_casting_order(self):
        for source, caster in (((0, 0, 1), (1, 0, 2)), ((0, 0, 1), (1, 0, 1))):
            with self.assertRaises(ValueError):
                shadow_point(source, caster)

    def test_two_known_caster_correspondences_localize(self):
        self.assertLess(correspondence_demo()["line_error_m"], 1e-12)

    def test_one_line_has_unbounded_direction(self):
        self.assertIsNone(intersect_caster_lines([((1, 0, 1), (1, 0, 0))]))

    def test_parallel_lines_rejected(self):
        self.assertIsNone(intersect_caster_lines([((1, 0, 1), (1, 0, 0)), ((2, 0, 1), (2, 0, 0))]))

    def test_three_edge_planes_localize(self):
        self.assertLess(correspondence_demo()["plane_error_m"], 1e-12)

    def test_edge_plane_contains_truth_and_endpoints(self):
        source, a, b = (0.5, -0.25, 4), (1, -0.5, 1), (1, 0.5, 1)
        n, offset = edge_plane(a, b, shadow_point(source, (1, 0, 1)))
        for p in (source, a, b):
            self.assertAlmostEqual(dot(n, p), offset)

    def test_repeated_edge_is_not_three_constraints(self):
        a, b, s = (1, -0.5, 1), (1, 0.5, 1), (4 / 3, 0, 0)
        self.assertIsNone(intersect_edge_planes([(a, b, s)] * 3))

    def test_degenerate_edge_rejected(self):
        with self.assertRaises(ValueError):
            edge_plane((1, 0, 1), (1, 0, 1), (4 / 3, 0, 0))

    def test_intensities_not_separable_without_shadow(self):
        self.assertIsNone(source_separation_demo()["no_wall_solution"])

    def test_shadow_patterns_separate_two_intensities(self):
        self.assertVectorClose(source_separation_demo()["with_wall_intensities_cd"], (2, 5))

    def test_zero_matrix_unobservable(self):
        self.assertIsNone(solve_small([[0, 0], [0, 0]], [0, 0]))

    def test_penumbra_geometry(self):
        self.assertAlmostEqual(penumbra_width(0.6, 4, 1), 0.2)
        self.assertAlmostEqual(penumbra_width(0.6, 4, 2), 0.6)

    def test_one_penumbra_does_not_determine_extent(self):
        self.assertAlmostEqual(penumbra_width(0.6, 4, 1), penumbra_width(1.2, 7, 1))

    def test_two_caster_heights_resolve_ideal_extent(self):
        self.assertVectorClose(extent_from_two_widths(0.2, 1, 0.6, 2), (4, 0.6))

    def test_penumbra_distant_limit(self):
        self.assertIsNone(extent_from_two_widths(0.2, 1, 0.4, 2))

    def test_ray_offset_changes_edge_if_treated_as_measurement_position(self):
        original = shadow_point((0, 0, 4), (1, 0, 1))[0]
        shifted = shadow_point((0, 0, 4), (1, 0, 1), 0.03)[0]
        self.assertAlmostEqual(shifted - original, -0.01)

    def test_float_geometry_precision_not_recoverable_by_double_solver(self):
        rounded = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
        self.assertEqual(rounded(100000 + 0.001), rounded(100000))

    def test_declared_quantizer(self):
        self.assertEqual(rgb8(0), 0)
        self.assertEqual(rgb8(0.5), 128)
        self.assertEqual(rgb8(1), 255)

    def test_integrated_shadow_footprint(self):
        self.assertAlmostEqual(visible_footprint(4), 2 / 3)

    def test_boundary_term_matches_finite_differences(self):
        d = footprint_demo()
        self.assertAlmostEqual(d['analytical_d_visibility_d_height'],
                               d['finite_difference_d_visibility_d_height'], places=8)

    def test_frozen_discrete_samples_miss_boundary_derivative(self):
        d = footprint_demo()
        self.assertEqual(d['fixed_five_sample_derivative'], 0)
        self.assertGreater(d['analytical_d_visibility_d_height'], 1)

    def test_integrated_visibility_outside_transition(self):
        self.assertEqual(visible_footprint(2), 0)
        self.assertEqual(visible_footprint(8), 1)

    def test_report_is_finite_and_deterministic(self):
        self.assertEqual(json.dumps(report(), sort_keys=True, allow_nan=False),
                         json.dumps(report(), sort_keys=True, allow_nan=False))


if __name__ == "__main__":
    unittest.main()
