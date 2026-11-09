"""Counterexamples to vacuous aesthetic acceptance, independent of native gates."""
import json
from pathlib import Path
import unittest

from tools.rend2.aesthetic_baseline.acceptance import assess


class Acceptance(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = json.loads(Path(__file__).with_name('policy-v1.json').read_text())

    def witness(self):
        return dict(owned_cases_evaluated=3, minimum_positive_proposal_recall_32=1,
                    accepted_positive_scenes=2, accepted_local_sources=3,
                    accepted_fixture_coverage=1, source_fixture_consistency=1,
                    false_accepted_emitters=0, known_wall_false_clear=0,
                    required_predictions_missing=0, unresolved_required_geometry=0,
                    final_interval_mse_max=.5, final_black_mse_fraction_max=.01,
                    final_worst_block_rmse_max=2, positive_blocks_min=2,
                    positive_black_rmse_min=10, spatial_fold_integrity=1,
                    repeat_semantics_equal=1, required_stage_completeness=1, stock_scene_evaluated=1)

    def test_complete_positive_witness_is_aesthetic_only(self):
        r = assess(self.witness(), self.policy)
        self.assertEqual(r['scene_aesthetic_readiness'], 'Passed')
        self.assertEqual(r['historical_source_certification'], 'NotCertified')

    def test_empty_model_cannot_pass_perfect_rgb_and_controls(self):
        m = self.witness()
        m.update(accepted_positive_scenes=0, accepted_local_sources=0,
                 final_interval_mse_max=0, false_accepted_emitters=0)
        self.assertEqual(assess(m, self.policy)['scene_aesthetic_readiness'], 'Failed')

    def test_black_predictor_cannot_pass_positive_scene(self):
        m = self.witness()
        # Even fabricated accepted-source counts do not rescue black predictions.
        m['final_black_mse_fraction_max'] = 1
        self.assertEqual(assess(m, self.policy)['scene_aesthetic_readiness'], 'Failed')

    def test_missing_stock_is_blocked(self):
        m = self.witness(); m['stock_scene_evaluated'] = None
        r = assess(m, self.policy)
        self.assertEqual(r['scene_aesthetic_readiness'], 'Blocked')
        self.assertEqual(r['gates']['stock_scene_evaluated']['state'], 'Blocked')

    def test_unknown_geometry_or_missing_prediction_cannot_be_black(self):
        for field in ('unresolved_required_geometry', 'required_predictions_missing'):
            m = self.witness(); m[field] = 1
            self.assertEqual(assess(m, self.policy)['scene_aesthetic_readiness'], 'Failed')
            m[field] = None
            self.assertEqual(assess(m, self.policy)['scene_aesthetic_readiness'], 'Blocked')

    def test_wall_leak_and_false_emitter_fail_even_with_good_rgb(self):
        for field in ('known_wall_false_clear', 'false_accepted_emitters'):
            m = self.witness(); m[field] = 1
            self.assertEqual(assess(m, self.policy)['scene_aesthetic_readiness'], 'Failed')


if __name__ == '__main__':
    unittest.main()
