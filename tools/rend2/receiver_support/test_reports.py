"""X15 frozen manifest and stock diagnostic assertions. GPL-2.0-or-later."""
import json
from collections import Counter
from pathlib import Path
import unittest

ROOT=Path(__file__).parent/'results'/'v4'


class ReceiverSupportReports(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT/'stock.json').exists():raise unittest.SkipTest('Run X15 first')
        cls.stock=json.loads((ROOT/'stock.json').read_text())

    def test_common_domain_is_intersection(self):
        for path in ROOT.glob('*-manifest.json'):
            m=json.loads(path.read_text())
            if 'entries' not in m:continue
            ids=[e['sample'] for e in m['entries']]
            self.assertEqual(len(set(ids)),len(ids))
            self.assertEqual(sum(e['common'] for e in m['entries']),m['common'])
            for e in m['entries']:
                self.assertEqual(e['common'],all(r=='Supported' for r in e['reasons']))
                self.assertIn(e['role'],(0,1,2))
                self.assertIn('geometry_key',e)
                self.assertNotIn('rgb',e)
                self.assertNotIn('intensity',e)

    def test_academy_normal_witness(self):
        witness=self.stock['academy1']['census']['academy_witness']
        self.assertEqual(witness['sample'],'7804426648716081')
        self.assertEqual(witness['surface'],308)
        self.assertEqual(witness['reason'],'DistinctNormals')
        self.assertEqual(witness['capability'],'DistinctNormals')
        self.assertFalse(witness['normal_replaced'])

    def test_original_nine_preserved_and_fixed_origin_exclusions(self):
        previous=json.loads((ROOT.parent/'v2'/'stock.json').read_text())['mp/duel4']
        manifest=json.loads((ROOT/'mp_duel4-isq-joint-manifest.json').read_text())
        entries={e['sample']:e for e in manifest['entries']}
        for branch in ('isq','linear'):
            attempts=[a for b in previous['profiles'][branch]['branches'] for a in b['attempts']]
            failing=[a for a in attempts if a['coverage_admitted']==64 and a['coverage_used']==55]
            self.assertTrue(failing)
            for a in failing:
                self.assertEqual(a['unknown_visibility'],9)
                fixed=Counter()
                for row in a['unavailable_receivers']:
                    self.assertIn('visibility_issues',row)
                    entry=entries[row['id']]
                    reason=entry['reasons'][0]
                    if row['visibility_issues']==2:self.assertEqual(reason,'ReceiverOriginSolid')
                    elif row['visibility_issues']==4:self.assertEqual(reason,'ReceiverOriginBoundary')
                    elif row['visibility_issues']==64:self.assertTrue(entry['common'])
                    fixed[reason]+=1
                self.assertEqual(fixed['ReceiverOriginSolid'],4)
                self.assertEqual(fixed['ReceiverOriginBoundary'],3)
                self.assertEqual(fixed['Supported'],2)

    def test_stock_not_physically_certified(self):
        for record in self.stock.values():
            self.assertEqual(record['census']['stock_profile'],'UnknownProfile')
            self.assertFalse(record['census']['census_uses_target_rgb'])
            self.assertFalse(record['census']['candidate_xyz_used'])
            self.assertGreater(record['census']['mapped_area'],0)
            for profile in record['profiles'].values():
                self.assertIsNone(profile['stock_source_truth'])

    def test_native_controls(self):
        native=json.loads((ROOT/'native.json').read_text())
        self.assertEqual(native['result']['failures'],0)
        self.assertTrue(native['repeat_semantics'])
        self.assertGreater(native['result']['black_mse'],native['result']['positive_mse'])


if __name__=='__main__':unittest.main()
