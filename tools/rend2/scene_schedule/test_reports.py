import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('x11_collect',Path(__file__).with_name('collect.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Reports(unittest.TestCase):
    def test_timing_is_not_a_numerical_witness(self):
        self.assertEqual(module.semantic(dict(resources=dict(wall_seconds=1),x=3)),
                         module.semantic(dict(resources=dict(wall_seconds=9),x=3)))

    def test_failure_and_missing_coverage_survive_normalization(self):
        value=dict(status='BudgetLimited',local_coverage_complete=False,final_evaluated=False)
        self.assertEqual(module.semantic(value),value)
        self.assertNotEqual(module.semantic(value),module.semantic(dict(status='CompleteScopedFit')))


if __name__ == '__main__':
    unittest.main()
