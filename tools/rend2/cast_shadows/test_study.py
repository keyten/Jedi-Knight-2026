#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
"""Numerical witnesses and false-positive/Unknown controls for the ASR05 study."""
from dataclasses import replace
import json
import math
import unittest

from study import (BAND_M, BEAM, CASTERS, FILL, LEFT_EDGE, SECOND, TRUTH, associate,
                   candidate_study, code8, detect, diagnostics, distance, edge_plane,
                   fine_grid, grid, intersect_caster_lines, patch, project, report,
                   response, retain, signal, trace, two_source_study)


class Geometry(unittest.TestCase):
    def test_wall_convention_and_response(self):
        self.assertAlmostEqual(response((0, 4, 0), (0, 0, 0)), 1 / 16)
        self.assertEqual(response((0, -4, 0), (0, 0, 0)), 0)
        with self.assertRaises(ValueError):
            response((0, 0, 0), (0, 0, 0))

    def test_projection_matches_independent_slab_boundary(self):
        # Midpoint of the bottom edge: neither a corner grazing nor plane-only test.
        q = project(TRUTH, (0, 2.1, 1.2))
        self.assertEqual(trace((q[0], 0, q[2] - 1e-6), TRUTH)[0], "Visible")
        self.assertEqual(trace((q[0], 0, q[2] + 1e-6), TRUTH), ("Blocked", "beam"))

    def test_vertical_silhouette_endpoints_have_finite_support(self):
        a, b = [project(TRUTH, c) for c in LEFT_EDGE]
        self.assertEqual(trace((a[0] - 1e-5, 0, (a[2] + b[2]) / 2), TRUTH)[0], "Visible")
        self.assertEqual(trace((a[0] + 1e-5, 0, (a[2] + b[2]) / 2), TRUTH)[0], "Blocked")
        self.assertEqual(trace((a[0] + 1e-5, 0, a[2] - 0.1), TRUTH)[0], "Visible")

    def test_segment_ends_and_optical_unknown(self):
        self.assertEqual(trace((0, 0, 0), (0, 1, 0))[0], "Visible")
        self.assertEqual(trace((0, 0, 0), TRUTH, complete=False)[0], "Unknown")
        self.assertEqual(trace((0, 0, 0), TRUTH, opaque=False)[0], "Unknown")
        self.assertEqual(trace((0, 2, 1.4), TRUTH)[0], "Unknown")
        self.assertEqual(trace((0, 0, 0), (0, 2, 1.4))[0], "Unknown")

    def test_receiver_caster_source_order(self):
        for source in ((0, 2.1, 3), (0, 1, 3), (0, -1, 3)):
            with self.assertRaises(ValueError):
                project(source, CASTERS[0])

    def test_one_line_and_repeated_edge_do_not_localize(self):
        c, s = CASTERS[0], project(TRUTH, CASTERS[0])
        self.assertIsNone(intersect_caster_lines([(c, s)]))
        self.assertIsNone(intersect_caster_lines([(c, s)] * 3))
        # Many observations along one straight edge define the SAME plane.
        a, b = LEFT_EDGE
        normals = [edge_plane(a, b, project(TRUTH, c))[0]
                   for c in (a, b, tuple((x + y) / 2 for x, y in zip(a, b)))]
        self.assertLess(max(distance(n, normals[0]) for n in normals), 1e-12)

    def test_two_source_geometry_and_nonblack_shadow(self):
        result = two_source_study()
        for row in result["sources"]:
            self.assertLess(row["error_m"], 1e-10)
        self.assertEqual(result["visibility"], ["Blocked", "Visible"])
        self.assertGreater(result["mixed_signal"], FILL + 0.1)


class Rejection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = candidate_study()

    def test_correspondences_reduce_admissible_set(self):
        r = self.result
        self.assertGreater(r["photometry"]["count"], 8000)
        self.assertLess(r["one_endpoint"]["count"], r["photometry"]["count"] / 100)
        self.assertLess(r["two_endpoints"]["count"], r["one_endpoint"]["count"])
        self.assertLess(r["one_endpoint"]["max_truth_distance_m"], r["photometry"]["max_truth_distance_m"])

    def test_selected_source_improves_independent_heldout(self):
        r = self.result
        self.assertGreater(r["baseline_error_m"], 1)
        self.assertLess(r["selected_error_m"], 1e-10)
        self.assertGreater(r["baseline_heldout"]["visibility_mismatches"], 10)
        self.assertEqual(r["selected_heldout"]["visibility_mismatches"], 0)
        self.assertLess(r["selected_heldout"]["rgb8_mse"], r["baseline_heldout"]["rgb8_mse"])

    def test_fine_grid_and_perturbation_preserve_uncertainty(self):
        r = self.result
        self.assertGreater(r["local_fine_grid"]["count"], 1)
        self.assertGreater(r["local_fine_grid"]["max_truth_distance_m"], 0.05)
        self.assertGreater(r["perturbed_endpoints"]["count"], 1)
        self.assertTrue(r["truth_retained_after_perturbation"])

    def test_wrong_correspondence_can_exclude_the_truth(self):
        c, s = CASTERS[0], project(TRUTH, CASTERS[1])
        self.assertEqual(retain([TRUTH], [(c, s)]), [])

    def test_limits_and_error_bands(self):
        oracle = [(c, project(TRUTH, c)) for c in CASTERS]
        with self.assertRaisesRegex(ValueError, "WorkLimit"):
            retain(grid(), oracle, max_candidates=10)
        for band in (-1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                retain([TRUTH], oracle, band=band)
        self.assertIn(TRUTH, retain(fine_grid(), oracle, BAND_M))


class Admission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = patch()
        cls.d = detect(cls.p)
        cls.results = diagnostics()

    def transform(self, fn):
        return replace(self.p, rows=tuple(tuple(fn(s, i) for i, s in enumerate(row)) for row in self.p.rows))

    def test_contrast_never_certifies_a_shadow(self):
        self.assertEqual(self.d["state"], "PossibleContrast")
        self.assertFalse(self.d["verified_cast_shadow"])
        self.assertEqual(len(self.d["strips"]), 3)
        self.assertTrue(all(abs(s["world_m"][0] - project(TRUTH, LEFT_EDGE[0])[0]) < s["band_m"]
                            for s in self.d["strips"]))

    def test_identical_bytes_without_a_caster(self):
        self.assertTrue(self.results["counterexample_byte_identical"])
        for row in self.p.rows:
            for s in row:
                self.assertEqual(s.code, code8(signal(s.world, beam=None, angular_mask=True)))
        self.assertEqual(self.results["cases"]["no_caster_angular_mask"]["state"], "Incompatible")

    def test_only_supplied_correspondence_can_veto(self):
        r = self.results["cases"]["no_correspondence_no_caster"]
        self.assertEqual(r["state"], "Unknown")
        self.assertFalse(r["hard_veto"])
        self.assertEqual(associate(TRUTH, self.d, caster_edge=None)["state"], "Unknown")

    def test_projection_catches_wrong_source_that_pair_rays_cannot(self):
        r = self.results["cases"]["wrong_source"]
        self.assertTrue(all(ray["bright"][0] == "Visible" and ray["dark"][0] == "Blocked" for ray in r["rays"]))
        self.assertEqual(r["state"], "Incompatible")
        self.assertTrue(r["hard_veto"])
        self.assertGreater(min(r["projected_residuals_m"]), 0.05)

    def test_unknowns_cannot_reject_source(self):
        for name in ("missing_geometry", "unknown_optics", "ray_budget", "sample_budget",
                     "unknown_transfer", "unknown_filter", "wide_filter", "seam", "material",
                     "normal", "saturation", "quantization_only", "smooth_falloff",
                     "moving_caster", "indirect_unresolved"):
            with self.subTest(name=name):
                r = self.results["cases"][name]
                self.assertEqual(r["state"], "Unknown")
                self.assertFalse(r.get("hard_veto", False))

    def test_geometric_match_remains_conditional(self):
        r = self.results["cases"]["actual_beam"]
        self.assertEqual(r["state"], "Compatible")
        self.assertFalse(r["verified_cast_shadow"])

    def test_mapping_and_style_controls(self):
        cases = [self.transform(lambda s, i: replace(s, mapped=False) if i == 0 else s),
                 self.transform(lambda s, i: replace(s, style=1) if i > 20 else s),
                 self.transform(lambda s, i: replace(s, world=(s.world[0], 0.1, s.world[2]))),
                 self.transform(lambda s, i: replace(s, normal=(0, 0, 1))),
                 self.transform(lambda s, i: replace(s, world=(math.nan, 0, 0))),
                 replace(self.p, noise_bound_codes=math.nan)]
        self.assertTrue(all(detect(p)["state"] == "Unknown" for p in cases))

    def test_multiple_edges_and_wide_error_budget(self):
        p = self.transform(lambda s, i: replace(s, code=80 if i < 10 or i > 30 else 160))
        self.assertEqual(detect(p)["state"], "Unknown")
        self.assertEqual(detect(replace(self.p, mapping_error_m=0.1))["state"], "Unknown")

    def test_all_diagnostics_have_no_historical_certification(self):
        for r in self.results["cases"].values():
            self.assertIs(r["verified_cast_shadow"], False)

    def test_report_finite_deterministic_and_diagnostic_only(self):
        a = json.dumps(report(), sort_keys=True, allow_nan=False)
        b = json.dumps(report(), sort_keys=True, allow_nan=False)
        self.assertEqual(a, b)
        self.assertIn("no BSP/compiler", a)


if __name__ == "__main__":
    unittest.main()
