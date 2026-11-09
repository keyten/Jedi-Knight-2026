"""Positive, adversarial, missing-evidence and adapter/contract regressions."""
import copy
import tempfile
import unittest
from pathlib import Path

from . import adapters
from .associate import POLICY, associate
from .controls import Geometry, cases, fixture, region
from .evaluate import evaluate


class AssociationTests(unittest.TestCase):
    def test_controlled_assignments_and_baselines(self):
        result = evaluate()
        for case in result['cases']:
            with self.subTest(case=case['name']):
                self.assertEqual(case['truth'], case['selected'])
                self.assertFalse(case['report']['historical_source_certified'])
                self.assertTrue(all(not e['historical_source_certified'] for e in case['report']['associations']))
        self.assertGreater(result['aggregate']['association']['f1'], result['aggregate']['nearest']['f1'])
        self.assertGreater(result['aggregate']['association']['f1'], result['aggregate']['nearest_colour']['f1'])
        self.assertGreater(result['aggregate']['association']['f1'], result['aggregate']['colour_only']['f1'])

    def test_reordering_and_originals_immutable(self):
        c = cases()[4]
        fs, os = copy.deepcopy(c['fixtures']), copy.deepcopy(c['observations'])
        before = copy.deepcopy((fs, os))
        a = associate(fs, os, c['geometry'])
        self.assertEqual((fs, os), before)
        for o in os:
            o['samples'].reverse()
        self.assertEqual(a, associate(list(reversed(fs)), list(reversed(os)), c['geometry']))

    def test_unknown_geometry_and_unknown_terms_are_not_clear(self):
        c = cases()[0]
        for geometry in (None, Geometry(unknown=True)):
            r = associate(c['fixtures'], c['observations'], geometry)
            self.assertFalse(r['active_fixtures'])
            e = r['associations'][0]
            self.assertEqual(e['status'], 'ambiguous')
            self.assertTrue(all(s['terms']['visibility']['value'] is None for s in e['supports']))
            self.assertTrue(all(s['terms']['visibility']['state'] == 'unknown' for s in e['supports']))
            self.assertTrue(all('visibility' in s['unknown_terms'] for s in e['supports']))

    def test_completely_unknown_support_keeps_null_score(self):
        f = dict(fixture('unknown'), outward=None, radius_world=None, colour=None)
        o = dict(id='unsupported', samples=[dict(id='s', position=[0, 0, 0])])
        e = associate([f], [o])['associations'][0]
        self.assertIsNone(e['score'])
        self.assertEqual(e['known_weight'], 0.)
        self.assertEqual(e['unconstrained_score_bounds'], [0., 1.])
        self.assertEqual(e['status'], 'ambiguous')

    def test_occlusion_retained_as_rejected_direct_alternative(self):
        c = cases()[5]
        r = associate(c['fixtures'], c['observations'], c['geometry'])
        e = next(e for e in r['associations'] if e['fixture_id'] == 'near-colour-decoy')
        self.assertEqual(e['status'], 'rejected')
        self.assertFalse(e['selected'])
        self.assertTrue(any('occlusion' in why for why in e['reasons']))

    def test_tied_fixtures_kept_ambiguous_with_one_activation(self):
        f = fixture('a', x=48)
        other = dict(f, id='b')
        r = associate([other, f], [region('r', [(f['position'], 0)])], Geometry())
        self.assertEqual(r['active_fixtures'], ['a'])
        self.assertTrue(all(e['status'] == 'ambiguous' for e in r['associations']))
        self.assertEqual(r['associations'][0]['replacement_margin'], 0.)

    def test_orientation_size_and_colour_reasons(self):
        c = cases()[0]
        fs = copy.deepcopy(c['fixtures'])
        fs.append(dict(fs[0], id='reverse-face', outward=[0, 0, 1]))
        fs.append(dict(fs[0], id='unknown-size-colour', radius_world=None, colour=None, outward=None))
        r = associate(fs, c['observations'], Geometry())
        back = next(e for e in r['associations'] if e['fixture_id'] == 'reverse-face')
        self.assertEqual(back['status'], 'rejected')
        unknown = next(e for e in r['associations'] if e['fixture_id'] == 'unknown-size-colour')
        self.assertTrue(all(s['terms']['proximity']['value'] is None for s in unknown['supports']))
        self.assertTrue(all(s['terms']['colour']['value'] is None for s in unknown['supports']))

    def test_near_dark_matched_receiver_changes_compatibility(self):
        f = fixture('lamp', x=48)
        o = region('r', [(f['position'], 0)], grid=False)
        o['dark_samples'] = [dict(id='dark', position=[0, 22, 0], normal=[0, 0, 1],
                                  rgb8=[4, 4, 4], material='owned/uniform', style=0)]
        r = associate([f], [o], Geometry())
        # All four controls object, with the exact strength depending on the
        # independently known geometric exposure of each bright sample.
        self.assertTrue(all(s['terms']['dark_receiver']['value'] < .25 for s in r['associations'][0]['supports']))
        o['dark_samples'][0]['material'] = 'different-reflectance'
        r2 = associate([f], [o], Geometry())
        self.assertTrue(all(s['terms']['dark_receiver']['value'] is None for s in r2['associations'][0]['supports']))
        self.assertLess(r['associations'][0]['score'], r2['associations'][0]['score'])

    def test_complexity_penalty_can_leave_bright_region_unmatched(self):
        c = cases()[0]
        p = dict(POLICY, source_penalty=10.)
        r = associate(c['fixtures'], c['observations'], c['geometry'], p)
        self.assertEqual(r['unmatched_observations'], ['floor'])
        self.assertEqual(r['objective'], 0.)

    def test_correlation_group_duplication_does_not_create_extra_vote(self):
        c = cases()[0]
        os = copy.deepcopy(c['observations'])
        original = associate(c['fixtures'], os, c['geometry'])
        samples = copy.deepcopy(os[0]['samples'])
        for s in samples:
            s['id'] += '/duplicate'
        os[0]['samples'].extend(samples)
        again = associate(c['fixtures'], os, c['geometry'])
        self.assertAlmostEqual(original['objective'], again['objective'])
        self.assertEqual(original['active_fixtures'], again['active_fixtures'])

    def test_invalid_and_budget_failures(self):
        c = cases()[0]
        for key, value in [('max_work', 1), ('max_pair_samples', 1), ('max_samples', 1),
                           ('distance_scale_world', 0), ('minimum_score', 1), ('source_penalty', -1)]:
            with self.subTest(policy=key), self.assertRaises(ValueError):
                associate(c['fixtures'], c['observations'], c['geometry'], dict(POLICY, **{key: value}))
        fs = copy.deepcopy(c['fixtures'])
        fs[0]['position'][0] = float('nan')
        with self.assertRaises(ValueError): associate(fs, c['observations'])
        with self.assertRaises(ValueError): associate(c['fixtures']*2, c['observations'])

    def test_grid_adapter_original_record_and_interpretation_guards(self):
        row = dict(ordinal=4, position=[0, 0, 64], present=True, issues=0, space='free', boundary=False,
                   dynamic_possible=False, styles=[dict(id=0, weak=False, directed=[120, 60, 20])],
                   writer_possible=False, consumer256=[0, 0, 1], writer255=[.01, 0, .999], correlation_key='record/2')
        os, rejected = adapters.grid([row])
        self.assertFalse(rejected)
        self.assertEqual(os[0]['samples'][0]['incoming'], [0, 0, 1])
        self.assertEqual(os[0]['samples'][0]['correlation_key'], 'record/2')
        os, rejected = adapters.grid([row], 'writer255_merge4')
        self.assertFalse(os)
        self.assertIn('writer_interpretation_impossible', rejected)
        for changes in (dict(present=False), dict(space='unknown'), dict(dynamic_possible=True),
                        dict(styles=[row['styles'][0], row['styles'][0]])):
            self.assertFalse(adapters.grid([dict(row, **changes)])[0])

    def test_catalogue_adapters_keep_world_placement_and_source_identity(self):
        f = dict(id='bsp-lens', centroid=[0, 0, 128], model=0, static_eligible=True,
                 outward=[0, 0, -1], two_sided=False, emitting_footprint_known=True,
                 footprint=[[[-4, -4, 128], [4, -4, 128], [4, 4, 128], [-4, 4, 128]]],
                 layers=[dict(colour_prior=[1., .5, .2])], issues=[])
        catalogue = dict(schema='org.openjk.research.bsp-fixtures/1', fixtures=[f])
        self.assertTrue(adapters.fixtures(catalogue)[0]['static_eligible'])
        f['model'] = 1  # Old exports do not attest inline-model world placement.
        self.assertFalse(adapters.fixtures(catalogue)[0]['static_eligible'])
        f.update(model=0, two_sided=True)
        self.assertIsNone(adapters.fixtures(catalogue)[0]['outward'])
        self.assertEqual(adapters.fixtures(catalogue, [50, 50, 0, 100, 100, 256]), [])
        model = dict(schema='org.openjk.research.md3-emission/1',
                     regions=[dict(id='r', footprint=f['footprint'])], fixtures=[
            dict(id='instance-a', centroid=[0, 0, 128], region_ids=['r'], source_id='shared-asset',
                 colour_prior=[1., .5, .2], outward=[0, 0, -1], static_eligible=True, issues=[]),
            dict(id='instance-b', centroid=[20, 0, 128], region_ids=['r'], source_id='shared-asset',
                 colour_prior=[1., .5, .2], outward=[0, 0, -1], static_eligible=True, issues=[])])
        fs = adapters.fixtures(model)
        report = associate(fs, [region('lit', [([0, 0, 128], 0)])], Geometry())
        self.assertEqual(len({e['source_hypothesis_id'] for e in report['associations']}), 2)

    def test_actual_patch_adapter_and_input_identity(self):
        from tools.rend2.lightmap_patches.extract import extract
        from tools.rend2.lightmap_patches.test_patches import field
        from .run import run, write
        rows = field()
        patches = extract(rows)
        os, _ = adapters.patches(patches, rows)
        self.assertTrue(os)
        self.assertIsNotNone(os[0]['projection'])
        self.assertTrue(all(s['incoming'] is None for s in os[0]['samples']))
        self.assertTrue(all(s['gradient'][2] == 0 for s in os[0]['samples']))
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            f = fixture('lamp', x=16, y=8, z=96)
            evidence = dict(schema=adapters.EVIDENCE_SCHEMA, map_sha256='owned-map', fixtures=[f], observations=os)
            write(p/'evidence.json', evidence)
            first = run([], p/'a.json', evidence=p/'evidence.json')
            run([], p/'b.json', evidence=p/'evidence.json')
            self.assertEqual((p/'a.json').read_bytes(), (p/'b.json').read_bytes())
            self.assertFalse(first['active_fixtures'])  # Geometry is explicitly unavailable.
            wrong = dict(schema='org.openjk.research.bsp-fixtures/1', map_sha256='other-map', fixtures=[])
            write(p/'catalogue.json', wrong)
            with self.assertRaises(ValueError):
                run([p/'catalogue.json'], p/'mismatch.json', evidence=p/'evidence.json')
            self.assertFalse((p/'mismatch.json').exists())
            evidence['map_sha256'] = ''
            write(p/'bad.json', evidence)
            with self.assertRaises(ValueError): run([], p/'failure.json', evidence=p/'bad.json')
            self.assertFalse((p/'failure.json').exists())


if __name__ == '__main__':
    unittest.main()
