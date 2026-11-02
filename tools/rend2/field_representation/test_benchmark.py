"""P03 geometry support and frozen numerical receipt validation. GPL-2.0-or-later."""
import json
from pathlib import Path
import unittest
import benchmark as b


class Comparison(unittest.TestCase):
    def test_spatial_basis_does_not_remove_a_wall(self):
        rows = b.spatial()['rows']
        for row in rows:
            if row['scene'] == 'sealed':
                self.assertEqual(row['blocked_support_leak'], 50)
                self.assertEqual(row['strict_blocked_support_leak'], 0)
                self.assertEqual(row['supported_value'], 0)

    def test_aperture_is_not_a_region_barrier(self):
        for row in b.spatial()['rows']:
            if row['scene'] == 'doorway':
                self.assertEqual(row['segment_weights'], [1, 1])
                self.assertEqual(row['supported_value'], 50)

    def test_unknown_is_not_clear(self):
        for row in b.spatial()['rows']:
            if row['scene'] in ('alpha_unknown', 'missing_geometry'):
                self.assertEqual(row['status'], 'Unknown')

    def test_no_support_is_unavailable(self):
        result = b.spatial()['no_admissible_support']
        self.assertEqual(result['status'], 'Unavailable')
        self.assertIsNone(result['value'])

    def test_receipt(self):
        path = Path(__file__).parent / 'results/v1/quality.json'
        if not path.exists():
            path = b.ROOT / 'build/p03-comparison-final/quality.json'
        if not path.exists():
            self.skipTest('Run native frozen comparison first')
        b.validate(json.loads(path.read_text()))

    def test_geometry_deterministic(self):
        self.assertEqual(b.spatial(), b.spatial())


if __name__ == '__main__':
    unittest.main()
