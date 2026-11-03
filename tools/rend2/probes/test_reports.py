"""P02 stock geometry is NOT stock lighting accuracy. GPL-2.0-or-later."""
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).parent/'results'/'v3'

class ProbeReports(unittest.TestCase):
    def test_controlled(self):
        x=json.loads((ROOT/'controlled.json').read_text())
        self.assertTrue(x['repeat_identical'])
        s=x['semantic'];self.assertEqual(s['failures'],0)
        self.assertEqual(s['wall_leak_links'],0)
        self.assertGreater(s['wall_links_checked'],0)
        self.assertTrue(s['doorway_cross_links'])
        self.assertLess(s['point_irradiance_refined_rmse'],s['point_irradiance_coarse_rmse'])
        self.assertEqual(s['query_rays'],0)

    def test_stock_has_no_invented_light(self):
        stock=json.loads((ROOT/'stock.json').read_text())
        self.assertEqual(set(stock),{'hoth3','vjun2','mp/duel4'})
        for case in stock.values():
            self.assertEqual(case['resources']['exit_code'],0)
            r=case['geometry'];self.assertIsNone(r['photometric_truth'])
            self.assertEqual(r['lighting_values'],0);self.assertFalse(r['rgb_used'])
            self.assertEqual(len(r['rois']),3)
            for roi in r['rois']:
                self.assertTrue(roi['completed'])
                self.assertGreaterEqual(roi['static_support_points'],roi['strict_support_points'])
                self.assertEqual(roi['reference_blocked_paths'],0)
                self.assertEqual(roi['reference_unknown_paths'],0)
                self.assertLessEqual(roi['static_supported_volume'],roi['analysis_volume'])
                self.assertTrue(roi['roundtrip'])

    def test_determinism(self):
        repeat=json.loads((ROOT/'repeat.json').read_text())
        self.assertTrue(repeat['matching_topology_keys'])

if __name__=='__main__':unittest.main()
