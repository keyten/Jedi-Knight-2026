"""X06 scope/admission/counterexample guards. No game, GL or fitting.
GPL-2.0-or-later.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile
import unittest

from capture import HERE,ROOT
from field_checks import run as fields
from stock_trial import thin
from prepare_blind import assignment


class CrossMap(unittest.TestCase):
    def test_decisions_frozen_before_evidence(self):
        baseline=json.loads((HERE/'baseline.json').read_text());policy=json.loads((HERE/'policy.json').read_text())
        self.assertEqual(set(baseline['decisions']),set('ABCD'))
        self.assertEqual(policy['baseline_sha256'],hashlib.sha256((HERE/'baseline.json').read_bytes()).hexdigest())
        self.assertIn('UnknownProfile',baseline['decisions']['A'])
        self.assertEqual(policy['budgets']['source_search_roi_per_map'],8)
        self.assertEqual(policy['direct_admission']['centroid_offsets_world'],[1,4])

    def test_availability_and_patch_override(self):
        r=json.loads((HERE/'availability.json').read_text())
        self.assertEqual(len(r['selected_new']),7)
        self.assertEqual(Path(r['available']['mp/ctf1']['archive']).name,'assets3.pk3')
        self.assertFalse(Path(r['private_root']).is_relative_to(ROOT))
        self.assertNotIn('hoth3',r['selected_new'])

    def test_geometry_thinning_keeps_normal_classes(self):
        rows=[dict(id=f'{i:04}',position=[i*100,0,0],normal=[0,0,1],raw=[i,i,i]) for i in range(100)]
        rows += [dict(id='wall',position=[0,100,0],normal=[1,0,0],raw=[0,0,0])]
        a=thin(rows,6);b=thin(list(reversed(rows)),6)
        self.assertEqual(a,b);self.assertIn('wall',[r['id'] for r in a])
        for r in rows:r['raw']=[255,0,255]
        self.assertEqual([r['id'] for r in a],[r['id'] for r in thin(rows,6)])

    def test_nested_split_is_spatial_and_deterministic(self):
        p=json.loads((HERE/'policy.json').read_text())['nested_split'];r=dict(position=[1000,1000,1000],footprint_radius=4)
        a=assignment(r,p['block_world'],p['gap_world'],p['seed'],p['origin_world'])
        self.assertEqual(a,assignment(r,p['block_world'],p['gap_world'],p['seed'],p['origin_world']))
        # Colours never enter assignment.
        self.assertEqual(a,assignment(dict(r,raw=[255,255,255]),p['block_world'],p['gap_world'],p['seed'],p['origin_world']))

    def test_new_field_cases_preserve_geometry_and_unavailability(self):
        r=fields();self.assertEqual(r,fields())
        s=r['probe_cases'];self.assertEqual(s['sealed']['weights'],[0.,1.]);self.assertEqual(s['doorway']['weights'],[1.,1.])
        self.assertTrue(s['unknown_grate']['unknown']);self.assertEqual(s['unknown_grate']['field_status'],'Unknown')
        self.assertIsNone(s['all_supports_blocked']['value']);self.assertEqual(s['all_supports_blocked']['moment_display_value'],100.)

    def test_passive_transport_not_free_light(self):
        t=fields()['passive_transport']
        self.assertEqual(t['sealed']['second_bounce_upper'][2],[0.,0.,0.])
        self.assertGreater(t['doorway']['second_bounce_lower'][2][0],0)
        self.assertEqual(t['unknown_opening']['second_bounce_lower'][2],[0.,0.,0.])
        self.assertGreater(t['unknown_opening']['second_bounce_upper'][2][0],0)

    def test_new_shadow_derivative_sign(self):
        for r in fields()['shadow_signs']:
            self.assertGreater(r['derivative'],0);self.assertLess(r['relative_error'],1e-6)

    def test_l4_improves_diffuse_but_is_not_source_inference(self):
        r=fields()['angular']
        for sharpness in (8.,48.):
            a,b=[x for x in r if x['sharpness']==sharpness]
            self.assertLess(b['diffuse_irradiance']['rmse'],a['diffuse_irradiance']['rmse'])
            self.assertFalse(b['source_inference']);self.assertGreater(b['incoming_radiance']['relative_rms'],b['diffuse_irradiance']['relative_rms'])

    def test_original_sources_unchanged(self):
        baseline=json.loads((HERE/'baseline.json').read_text())
        for f,h in baseline['sources'].items():
            if h is not None:self.assertEqual(hashlib.sha256((ROOT/f).read_bytes()).hexdigest(),h,f)

    def test_real_census_when_present(self):
        private=Path(json.loads((HERE/'availability.json').read_text())['private_root']);path=private/'census.json'
        if not path.exists():self.skipTest('Licensed local census not present')
        r=json.loads(path.read_text());complete=[v for v in r.values() if v['status']=='Complete']
        self.assertEqual(len(complete),6)
        for v in complete:
            self.assertEqual(v['source']['raw_pixels'],v['source']['rows'])
            self.assertEqual(v['historical_profile'],'UnknownProfile');self.assertIsNone(v['ground_truth_source_xyz'])
            self.assertEqual(v['area_quality']['qualities']['comparable_colour']['covered_area'],0)

    def test_new_compiler_profile_and_failure_controls(self):
        path=ROOT/'build/x06-controlled-final/result.json'
        if not path.exists():self.skipTest('Pinned new compiler bakes absent')
        r=json.loads(path.read_text());pairs=json.loads((path.parent/'pairs.json').read_text())
        for key in ('multi_direct','spot_oblique','thin_door','sky_roof_v2','gamma_clipped'):
            self.assertLess(r[key]['reference']['rgb_interval_rmse']['value'],1e-10)
            self.assertGreater(r[key]['black']['rgb_interval_rmse']['value'],4.)
        self.assertEqual(r['saturated_white']['profile_outcome'],'Ambiguous')
        self.assertTrue(pairs['coincident_lightmap_equal'])
        self.assertGreater(r['thin_door']['all_visible']['rgb_interval_rmse']['value'],2.)
        self.assertGreater(r['multi_bounce']['reference']['rgb_interval_rmse']['value'],1.)
        self.assertGreater(r['multi_bounce']['fitted_profiles']['isq']['gain'],1.)
        self.assertGreater(pairs['bounce']['floor_mean_delta_rgb8'][0],0.)
        self.assertLess(pairs['material_bounce']['floor_mean_delta_rgb8'][2],0.)

    def test_new_blind_controls_do_not_pass_on_black_or_missing_area(self):
        path=ROOT/'build/x06-evaluation-final/result.json'
        if not path.exists():self.skipTest('New compiled blind controller controls absent')
        r=json.loads(path.read_text())
        for name,item in r.items():
            if name=='gamma_clipped':
                self.assertEqual(item['controls']['status'],'NotComparableToGamma1Gate');continue
            self.assertFalse(item['controls']['black']['success'])
            self.assertFalse(item['controls']['low_coverage']['success'])
            self.assertFalse(item['all_pages_complete']) # intentional floor subset
            detection=item['metric_detection']
            self.assertEqual(detection['false_extra_same_rgb']['false_positive'],5)
            self.assertEqual(detection['indirect_only_same_rgb']['true_positive'],0)

    def test_stock_trials_keep_unknown_and_never_load_outer_answers(self):
        private=Path(json.loads((HERE/'availability.json').read_text())['private_root']);path=private/'stock-trials/result.json'
        if not path.exists():self.skipTest('Licensed stock hypothesis trials absent')
        r=json.loads(path.read_text())
        self.assertGreater(sum(v.get('ray_queries',0) for v in r.values()),10000)
        for item in r.values():
            self.assertNotEqual(item['status'],'Verified')
            if 'outer_evaluation_targets_opened' in item:self.assertFalse(item['outer_evaluation_targets_opened'])
            if 'historical_profile' in item:self.assertEqual(item['historical_profile'],'UnknownProfile')

    def test_independent_stock_geometry_no_false_clear(self):
        private=Path(json.loads((HERE/'availability.json').read_text())['private_root']);path=private/'stress/result.json'
        if not path.exists():self.skipTest('Licensed matched-input ray audit absent')
        r=json.loads(path.read_text());values=[x['report'] for v in r.values() for x in v.get('rois',[]) if 'report' in x]
        self.assertEqual(len(values),24);self.assertGreater(sum(x['reference_opaque_face_hits'] for x in values),1000)
        self.assertEqual(sum(x['reference_false_clear'] for x in values),0)
        self.assertTrue(all(x['reference_max_error_world']<=1e-4 for x in values))


if __name__=='__main__':unittest.main()
