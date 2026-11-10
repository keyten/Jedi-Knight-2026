"""Guards against certifying unavailable geometry or post-selection score reuse."""
import copy
import unittest

from .run import POLICY, gate, split


class ProtocolTests(unittest.TestCase):
    def test_footprint_gap_excludes_boundary(self):
        r = dict(position=[17+256, 31+128, 47+128], footprint_radius=4)
        self.assertIsNone(split(r))
        r['position'][0] += 14
        self.assertIsNotNone(split(r))
        r['footprint_radius'] = 5
        self.assertIsNone(split(r))

    def test_split_ignores_rgb_and_never_splits_one_block(self):
        a = dict(position=[100, 100, 100], footprint_radius=1, raw=[0, 0, 0])
        b = dict(position=[110, 110, 110], footprint_radius=1, raw=[255, 255, 255])
        self.assertEqual(split(a), split(b))

    def test_planar_normal_extent_does_not_erase_whole_floor(self):
        r = dict(position=[100, 100, 0], normal=[0, 0, 1], footprint_radius=20)
        self.assertIsNotNone(split(r))
        r['position'][0] = 17+256+20
        self.assertIsNone(split(r))

    def fixture_result(self):
        return dict(selected=0, baseline=dict(heldout=dict(available=True, mse=100, robust=10)),
            configurations=[dict(available=True, converged=True, background_at_bound=False, validation=dict(available=True),
                heldout=dict(available=True, mse=50, robust=5),
                sources=[dict(placement='Conditional', source_side_certified=True,
                              relative_rgb=[1, 2, 3], power_at_bound=False)])])

    def test_gate_requires_substance_and_independent_geometry(self):
        good = self.fixture_result()
        witness = dict(known_static_agreeing_records=POLICY['minimum_grid_supports'])
        ablation = dict(available=True, rows_with_at_least_one_code_response=4, source_removal_worsens_mse=True)
        self.assertEqual(gate(good, witness, ablation)[0], 'AestheticCandidate')
        self.assertEqual(gate(good, witness)[0], 'Unresolved')
        self.assertEqual(gate(good, witness, dict(ablation, rows_with_at_least_one_code_response=0))[0], 'Rejected')
        self.assertEqual(gate(good, witness, dict(ablation, source_removal_worsens_mse=False))[0], 'Rejected')
        for key, value in [('placement', 'UnknownGeometry'), ('relative_rgb', [0, 0, 0]),
                           ('source_side_certified', False), ('power_at_bound', True)]:
            bad = copy.deepcopy(good)
            bad['configurations'][0]['sources'][0][key] = value
            self.assertEqual(gate(bad, witness)[0], 'Unresolved')
        self.assertEqual(gate(good, dict(known_static_agreeing_records=0))[0], 'Unresolved')

    def test_validation_and_heldout_failure_cannot_pass(self):
        r = self.fixture_result()
        witness = dict(known_static_agreeing_records=3)
        r['selected'] = -1
        self.assertEqual(gate(r, witness)[0], 'Rejected')
        r['selected'] = 0
        r['configurations'][0]['heldout']['mse'] = 110
        self.assertEqual(gate(r, witness)[0], 'Rejected')


if __name__ == '__main__':
    unittest.main()
