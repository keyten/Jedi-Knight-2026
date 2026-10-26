#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic numerical tests for the research note, no third-party modules."""
import json
import math
import unittest

from math_checks import (canonical_rgb, dot, interval_residual, mass_lumped_triangle,
                         original_q3_grid, point_irradiance, position_gradient,
                         report, scalar_amplitude)


class ResearchChecks(unittest.TestCase):
    def assertVectorClose(self, actual, expected, places=12):
        self.assertEqual(len(actual), len(expected))
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=places)

    def test_projection_constant(self):
        for area in (0.01, 1, 10000):
            self.assertVectorClose(mass_lumped_triangle((7, 7, 7), area), (7, 7, 7))

    def test_projection_does_not_preserve_vertex_peak(self):
        self.assertVectorClose(mass_lumped_triangle((0, 0, 1)), (0.25, 0.25, 0.5))

    def test_projection_integral_conservation(self):
        values, area = (1.2, 3.5, 0.1), 7.3
        projected = mass_lumped_triangle(values, area)
        self.assertAlmostEqual(sum(projected) * area / 3, sum(values) * area / 3)

    def test_projection_rejects_degenerate_area(self):
        for area in (0, -1, math.inf, math.nan):
            with self.assertRaises(ValueError):
                mass_lumped_triangle((1, 2, 3), area)

    def test_inverse_square(self):
        self.assertVectorClose(point_irradiance((0, 0, 2), (0, 0, 0), (0, 0, 1), (4, 8, 12)), (1, 2, 3))

    def test_incidence_and_back_face(self):
        self.assertEqual(point_irradiance((0, 0, -1), (0, 0, 0), (0, 0, 1), (4, 4, 4)), [0, 0, 0])
        self.assertAlmostEqual(point_irradiance((1, 0, 1), (0, 0, 0), (0, 0, 1), (1, 1, 1))[0], 1 / (2 * math.sqrt(2)))

    def test_world_units_converted_once(self):
        self.assertVectorClose(point_irradiance((0, 0, 200), (0, 0, 0), (0, 0, 1), (4, 4, 4), 100), (1, 1, 1))

    def test_geometry_scale_intensity_gauge(self):
        first = point_irradiance((0, 0, 2), (0, 0, 0), (0, 0, 1), (4, 4, 4), 1)
        second = point_irradiance((0, 0, 2), (0, 0, 0), (0, 0, 1), (1, 1, 1), 2)
        self.assertVectorClose(first, second)

    def test_position_derivative(self):
        self.assertLess(report()["position_gradient_max_absolute_error"], 1e-10)

    def test_derivative_out_of_domain(self):
        with self.assertRaises(ValueError):
            position_gradient((0, 0, -1), (0, 0, 0), (0, 0, 1))

    def test_zero_separation_is_not_clamped(self):
        with self.assertRaises(ValueError):
            point_irradiance((0, 0, 0), (0, 0, 0), (0, 0, 1), (1, 1, 1))

    def test_one_observation_cannot_locate_source(self):
        result = report()["single_sample_position_ambiguity"]
        self.assertEqual(result["height1_cd1"], result["height2_cd4"])

    def test_collocated_lights_not_identifiable(self):
        arguments = ((0, 0, 2), (0, 0, 0), (0, 0, 1))
        one = point_irradiance(*arguments, (5, 7, 9))
        a = point_irradiance(*arguments, (1, 2, 3))
        b = point_irradiance(*arguments, (4, 5, 6))
        self.assertVectorClose(one, [x + y for x, y in zip(a, b)])

    def test_gain_intensity_gauge(self):
        response = [0.25, 0.5, 0.75]
        self.assertVectorClose([2 * 3 * a for a in response], [4 * 1.5 * a for a in response])

    def test_rgb_canonicalization(self):
        intensity, colour = canonical_rgb((1, 2, 4))
        self.assertAlmostEqual(dot(colour, (0.2126, 0.7152, 0.0722)), 1)
        self.assertVectorClose([intensity * c for c in colour], (1, 2, 4))

    def test_black_chromaticity_unknown(self):
        self.assertEqual(canonical_rgb((0, 0, 0)), (0, None))

    def test_saturated_sample_is_lower_bound(self):
        self.assertEqual(interval_residual(4, 254.5 / 255, None), 0)
        self.assertLess(interval_residual(0.5, 254.5 / 255, None), 0)

    def test_unsaturated_quantization_interval(self):
        lo, hi = 127.5 / 255, 128.5 / 255
        self.assertEqual(interval_residual(128 / 255, lo, hi), 0)
        self.assertGreater(interval_residual(0.9, lo, hi), 0)

    def test_invisible_source_is_unknown(self):
        self.assertIsNone(scalar_amplitude((0, 0), (1, 2)))

    def test_single_column_nnls_diagnostic(self):
        self.assertAlmostEqual(scalar_amplitude((1, 2, 3), (2, 4, 6)), 2)
        self.assertEqual(scalar_amplitude((1, 2), (-1, -2)), 0)

    def test_free_indirect_can_explain_everything(self):
        target = (0.1, 0.9, 3.0)
        zero_direct, independent_indirect = (0, 0, 0), target
        self.assertVectorClose([a + b for a, b in zip(zero_direct, independent_indirect)], target)

    def test_shadow_step_locally_zero_gradient(self):
        loss = lambda p: float(p < 0)  # dark on one side, lit on the other
        p, h = -1.0, 1e-5
        self.assertEqual((loss(p + h) - loss(p - h)) / (2 * h), 0)
        self.assertLess(loss(1), loss(p))

    def test_grid_ambient_contains_direct_contribution(self):
        packed = original_q3_grid(((4, 2, 1),), ((0, 0, 1),))
        self.assertVectorClose(packed["ambient"], (1, 0.5, 0.25))
        self.assertVectorClose(packed["directed"], (4, 2, 1))
        self.assertVectorClose(packed["toward_source"], (0, 0, 1))

    def test_grid_direction_cancellation(self):
        self.assertIsNone(original_q3_grid(((1, 1, 1), (1, 1, 1)), ((1, 0, 0), (-1, 0, 0))))

    def test_report_reproducibility(self):
        self.assertEqual(json.dumps(report(), sort_keys=True), json.dumps(report(), sort_keys=True))


if __name__ == "__main__":
    unittest.main()
