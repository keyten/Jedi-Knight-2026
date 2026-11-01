import copy
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('topology_collect', Path(__file__).with_name('collect.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SemanticWitness(unittest.TestCase):
    def test_performance_is_not_a_numerical_witness(self):
        a = dict(resources=dict(wall_seconds=1), transport=dict(mesh_key='a', mesh_seconds=1))
        b = dict(resources=dict(wall_seconds=4), transport=dict(mesh_key='a', mesh_seconds=3))
        self.assertEqual(module.semantic(a), module.semantic(b))

    def test_missing_geometry_cannot_hide_in_performance(self):
        a = dict(transport=dict(mesh_key='a', unavailable_observations=0))
        b = copy.deepcopy(a)
        b['transport']['unavailable_observations'] = 1
        self.assertNotEqual(module.semantic(a), module.semantic(b))

    def test_budget_completeness_is_semantic(self):
        a = dict(transport=dict(local_budget_exceeded=False))
        b = dict(transport=dict(local_budget_exceeded=True))
        self.assertNotEqual(module.semantic(a), module.semantic(b))


if __name__ == '__main__':
    unittest.main()
