"""Meaningful numerical, ambiguity and split-leakage regression tests for ASR13."""
import copy
import json
import math
from pathlib import Path
import unittest

from fixtures import Geometry, observations
from triangulate import discover, eigen, evaluate, norm, prepare, role, search_group, solve, sub, unit


class TriangulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.truth = json.loads((Path(__file__).with_name("frozen_truth.json")).read_text())
        cls.case = cls.truth["cases"][0]
        cls.prepared, _ = prepare(observations(cls.case), dict(map_sha256="analytic-v1"), "writer255_merge4")
        cls.training = [r for r in cls.prepared if r["role"] == "train"]
        cls.result = discover(cls.training, Geometry(), "writer255_merge4")

    def test_rotated_symmetric_solve(self):
        a = [[2., 1., 0.], [1., 2., 0.], [0., 0., 4.]]
        self.assertLess(norm(sub(solve(a, [3, 3, 4]), [1, 1, 1])), 1e-12)
        self.assertEqual(solve([[1., 0., 0.], [0., 0., 0.], [0., 0., 1.]], [1, 0, 1]), None)
        self.assertLess(abs(eigen(a)[0][0]-1), 1e-12)

    def test_blind_point_position(self):
        c = self.result["candidates"][0]
        self.assertEqual(c["status"], "conditional_location")
        self.assertLess(norm(sub(c["position"], self.case["lights"][0])), 24)
        self.assertLess(c["angular_rms_degrees"], 2)
        self.assertTrue(all(d > 0 for d in c["positive_ray_distances_world"]))

    def test_repeat_and_input_order(self):
        again = discover(list(reversed(self.training)), Geometry(), "writer255_merge4")
        # API normalizes by ordinal, so caller iteration order cannot alter cluster IDs/ranks.
        self.assertEqual(self.result, again)

    def test_heldout_cannot_enter_search(self):
        with self.assertRaises(ValueError): discover(self.prepared, Geometry(), "writer255_merge4")

    def test_evaluation_does_not_mutate_search(self):
        frozen = copy.deepcopy(self.result)
        modified = copy.deepcopy(self.prepared)
        for r in modified:
            if r["role"] != "train":
                r["direction"] = [0, 0, -1]
                r["colour"] = [1, 0, 0]
                r["position"] = [1e5, -1e5, 1e5]
        evaluate(self.result, modified, Geometry())
        self.assertEqual(frozen, self.result)
        self.assertEqual(frozen, discover([r for r in modified if r["role"] == "train"], Geometry(), "writer255_merge4"))

    def test_correlated_records_are_one_vote(self):
        rows = copy.deepcopy(self.training)
        for r in rows: r["correlation_key"] = "one_record"
        result = discover(rows, Geometry(), "writer255_merge4")
        self.assertFalse(result["candidates"])
        self.assertEqual(result["unknowns"][0]["independent_records"], 1)

    def test_unknown_geometry_is_not_free(self):
        result = discover(self.training, Geometry(unknown=True), "writer255_merge4")
        self.assertFalse(result["candidates"])
        self.assertTrue(result["unknowns"])
        self.assertGreater(result["connectivity"]["unknown"], 0)

    def test_blocked_regions_do_not_join(self):
        result = discover(self.training, Geometry(blocked=True), "writer255_merge4")
        self.assertFalse(result["candidates"])
        self.assertEqual(result["components"], len(self.training))

    def test_solid_intersection_rejected(self):
        candidates, unknown = search_group(self.training, Geometry(solid_above=200), "writer255_merge4", 0)
        self.assertFalse(candidates)
        self.assertTrue(unknown)

    def test_negative_ray_distances_rejected(self):
        rows = copy.deepcopy(self.training)
        for r in rows: r["direction"] = [-v for v in r["direction"]]
        candidates, unknown = search_group(rows, Geometry(), "writer255_merge4", 0)
        self.assertFalse(candidates)
        self.assertTrue(unknown)

    def test_parallel_depth_unknown(self):
        rows = copy.deepcopy(self.training)
        for r in rows: r["direction"] = [0., 0., 1.]
        candidates, unknown = search_group(rows, Geometry(), "writer255_merge4", 0)
        self.assertFalse(candidates)
        self.assertEqual(unknown[0]["reason"], "nearly_parallel_or_rank_deficient")
        self.assertIsNone(unknown[0]["region"]["depth_upper_world"])

    def test_dynamic_geometry_caps_claim(self):
        result = discover(self.training, Geometry(dynamic=True), "writer255_merge4")
        for c in result["candidates"]:
            self.assertIn("dynamic_bake_occupancy_unknown", c["reasons"])
            self.assertEqual(c["confidence"], "conditional_only")

    def test_unknown_compression_is_explicit(self):
        result = discover(self.training, Geometry(), "consumer256_unknown")
        self.assertTrue(result["candidates"])
        for c in result["candidates"]:
            self.assertFalse(c["region"]["compression_bound_known"])
            self.assertIn("compression_bound_unknown", c["reasons"])

    def test_styles_and_weak_signal_withheld(self):
        rows = observations(self.case)[:3]
        rows[0]["styles"] *= 2
        rows[1]["styles"][0]["weak"] = True
        rows[2]["space"] = "unknown"
        prepared, rejected = prepare(rows, dict(map_sha256="test"), "writer255_merge4")
        self.assertFalse(prepared)
        self.assertEqual(sum(rejected.values()), 3)
        self.assertEqual(discover([], Geometry(), "writer255_merge4")["unknowns"][0]["reason"], "no_training_observations")

    def test_record_partition_stable(self):
        for _ in range(20): self.assertEqual(role("same bytes", "same map"), role("same bytes", "same map"))

    def test_disagreeing_colours_are_alternative_components(self):
        rows = copy.deepcopy(self.training)
        for row in rows:
            left = row["position"][0] < 0
            row["colour"] = [1, 0, 0] if left else [0, 0, 1]
            row["direction"] = unit(sub([-60 if left else 60, 0, 220], row["position"]))
        result = discover(rows, Geometry(), "writer255_merge4")
        self.assertGreaterEqual(result["components"], 2)
        self.assertGreaterEqual(len(result["candidates"]), 2)
        self.assertGreater(result["connectivity"]["colour_or_style_disagreement"], 0)

    def test_weak_parallax_stays_depth_uncertain(self):
        rows = copy.deepcopy(self.training)
        for row in rows: row["direction"] = unit(sub([0, 0, 10000], row["position"]))
        candidates, unknown = search_group(rows, Geometry(solid_above=20000), "writer255_merge4", 0)
        self.assertFalse(candidates)
        self.assertEqual(unknown[0]["reason"], "nearly_parallel_or_rank_deficient")

    def test_aggregate_and_reflection_never_certified(self):
        # Identical data can come from an indirect virtual source; even a perfect fit is conditional.
        for c in self.result["candidates"]:
            self.assertFalse(c["single_emitter_certified"])
            self.assertFalse(c["historical_source_certified"])
            self.assertIn("reflected_or_aggregate_virtual_source_not_excluded", c["reasons"])
        from evaluate import metrics
        counted = metrics(dict(candidates=[dict(status="unknown", position=[0, 0, 0], rank=1, confidence="unknown")], unknowns=[]), [[0, 0, 0]], 24.)
        self.assertEqual(counted["recalled"], 1)
        self.assertEqual(counted["conditional_recalled"], 0)
        self.assertEqual(counted["false_candidates"], 0)


if __name__ == "__main__": unittest.main()
