"""X12 report regression: do not turn a completed child into lamp recovery."""
from pathlib import Path
import copy
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.stock_joint_fits.collect import rejection
from tools.rend2.scene_schedule.collect import semantic


class Reports(unittest.TestCase):
    def test_precise_failure_classes(self):
        self.assertEqual(rejection('Physically invalid source support/path/bounds: source=1 centre/path:Dynamic boundary=7 swept-extent=Unknown'), 'Dynamic')
        self.assertEqual(rejection('Unsupported factor operation: distinct shading normals'), 'UnsupportedForwardCapability')
        self.assertEqual(rejection('Joint comparable training coverage missing: used=55 unavailable=9'), 'UnavailableComparableRows')

    def test_missing_prediction_never_equals_zero_error(self):
        report = json.loads((Path(__file__).parent / 'results/v1/stock.json').read_text())
        self.assertEqual(len(report['maps']), 6)
        for case in report['maps']:
            self.assertEqual(case['resources']['exit_code'], 0)
            self.assertEqual(case['physical_source_truth'], 'Unavailable')
            for p in case['profiles'].values():
                self.assertGreater(p['work']['joint_calls'], 0)
                self.assertIsNone(p['heldout_interval_mse'])
                self.assertFalse(p['preferred_model_available'])
                self.assertEqual(p['controller']['final_calls'], 0)
                self.assertEqual(p['accepted_sources'], [])

    def test_geometry_minimum_and_capability_remain_explicit(self):
        report = json.loads((Path(__file__).parent / 'results/v2/stock.json').read_text())
        for p in report['maps'][0]['profiles'].values():
            c = p['controller']
            self.assertGreater(c['training_evidence_blocks'], 64)
            self.assertGreaterEqual(c['coarse_rows'], c['training_evidence_blocks'])
            self.assertGreater(c['validation_calls'], 0)
            self.assertIn('UnsupportedForwardCapability', p['rejection_counts'])
            self.assertEqual(c['final_calls'], 0)

    def test_semantic_repeat_preserves_failures(self):
        value = dict(resources=dict(seconds=1), failure='Unavailable', mse=None)
        other = copy.deepcopy(value)
        other['resources']['seconds'] = 2
        self.assertEqual(semantic(value), semantic(other))
        other['mse'] = 0
        self.assertNotEqual(semantic(value), semantic(other))


if __name__ == '__main__':
    unittest.main()
