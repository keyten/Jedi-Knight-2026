"""Counterexamples for vacuous coverage, stale provenance and licensed-data export."""
import unittest
from unittest.mock import patch

from .collect import cause, public_map, region_counts
from .run import roi_design


class AuditTests(unittest.TestCase):
    def item(self):
        return dict(map='mp/example', mode='mp', sha256='a'*64, bytes=123,
                    archive='private/assets0.pk3', member='maps/mp/example.bsp')

    def test_blocked_fit_is_unavailable_not_empty_success(self):
        r = dict(status='Blocked', capture_available=False, accepted_lights=None,
                 blockers=['Input: licensed original observation capture absent'])
        result = public_map(self.item(), r, {})
        self.assertIsNone(result['accepted_lights'])
        self.assertIsNone(result['accepted_source_coverage'])
        self.assertEqual(result['blocker_classes'], ['MissingOriginalCapture'])

    def test_observed_pixels_never_become_accepted_source_coverage(self):
        r = dict(status='NoSupportedCandidate', capture_available=True, accepted_lights=0,
                 independent_roi_coverage=[dict(mapped=1200)], blockers=[])
        old = dict(bsp_sha256='a'*64,census=dict(coverage=[0,0,9999]))
        result = public_map(self.item(), r, old)
        self.assertEqual(result['archived_observation_census']['coverage'][2],9999)
        self.assertIsNone(result['accepted_source_coverage'])
        stale = dict(old,bsp_sha256='b'*64)
        self.assertNotIn('archived_observation_census',public_map(self.item(),r,stale))

    def test_private_coordinates_and_raw_pixels_are_not_exported(self):
        region = dict(name='r', status='Unresolved', reason='Unknown',
                      position=[12,13,14],roi_world=[1,2,3,4,5,6],raw=[99,22,33],
                      selection=dict(available=False,roi=[1,2,3,4,5,6],counts={'heldout':7}),
                      sources=[dict(position=[12,13,14])])
        result = region_counts(region)
        self.assertNotIn('position',result)
        self.assertNotIn('raw',result)
        self.assertNotIn('sources',result)
        self.assertNotIn('roi',result['selection'])

    def test_roi_design_is_geometry_only_and_spatially_spread(self):
        surfaces = [dict(ordinal=i, model=0, kind=1, vertices=[dict(p=p)], plane_normal=[0,0,1])
                    for i,p in enumerate(([0,0,0],[1,0,0],[1000,0,0],[0,1000,0]))]
        with patch('tools.rend2.stock_audit.run.read_bsp',return_value=surfaces):
            a = roi_design(b'original', 'a'*64, 3)
            b = roi_design(b'poisoned RGB payload', 'a'*64, 3)
        self.assertEqual(a,b)
        self.assertEqual(len({p['surface'] for p in a}),3)
        self.assertTrue(any(p['centre'][0] == 1000 for p in a))
        self.assertTrue(any(p['centre'][1] == 1000 for p in a))

    def test_failure_classes_preserve_budget_and_geometry_refusals(self):
        self.assertEqual(cause('Fixture work limit exceeded; no partial catalogue published'),'CatalogueWorkBudget')
        self.assertEqual(cause('This bounded follow-up requires matching original static-map geometry'),'DynamicMapUnsupported')
        self.assertEqual(cause('ROI exceeds 4096 original cells'),'GridQueryBudget')


if __name__ == '__main__':
    unittest.main()
