"""Adversarial acceptance/access tests; standard library, no source fitting.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest

from acceptance import load,digest,target,predictions,evaluate,profile_gate,PROFILE
from fixtures import HERE,ROOT,definition,floor_rows,load_geometry,write
from diagnostics import encoding_contract,unknown_contracts
from prepare_blind import assignment,prepare
from solver_view import TrainingView,FIELDS
from evaluation_view import EvaluationView
from test_blind_views import sample
from score import score
from compiler_boundary import load as strict_geometry

POLICY=load(HERE/'policy.json')


def records(black=False):
    return [dict(id=str(i),raw=[0 if black else 32]*3,area=1.+i,block=str(i//2),groups=['test']) for i in range(8)]


class Acceptance(unittest.TestCase):
    def test_positive_rejects_black_and_abstention(self):
        rs=records();contracts=[dict(id='positive',group='test',kind='PositiveLight')]
        for pred in (predictions(rs),[],predictions(rs,status='Unavailable'),predictions(rs[:1],[[32]*3])):
            self.assertFalse(evaluate(rs,pred,contracts,POLICY)['success'])
        good=evaluate(rs,predictions(rs,[[32.5]*3]*8),contracts,POLICY)
        self.assertTrue(good['success']);self.assertEqual(good['contracts'][0]['relative_zero_improvement'],1)

    def test_dark_is_valid_but_not_positive(self):
        rs=records(True);negative=[dict(id='negative',group='test',kind='DarkBlockedNegative')]
        out=evaluate(rs,predictions(rs),negative,POLICY)
        self.assertTrue(out['success']);self.assertIsNone(out['contracts'][0]['relative_zero_improvement'])
        self.assertEqual(out['contracts'][0]['relative_status'],'NonInformativeBaseline')
        self.assertFalse(evaluate(rs,predictions(rs),[dict(negative[0],kind='PositiveLight')],POLICY)['success'])
        self.assertFalse(evaluate(rs,predictions(rs,[[4]*3]*8),negative,POLICY)['success'])

    def test_independent_support_not_just_rgb(self):
        rs=records();c=[dict(id='positive',group='test',kind='PositiveLight')]
        for r in rs:r['block']='one'
        self.assertFalse(evaluate(rs,predictions(rs,[[32]*3]*8),c,POLICY)['success'])
        with self.assertRaises(ValueError):evaluate(rs,predictions(rs),[dict(c[0],kind='PositivLight')],POLICY)

    def test_area_denominator_cannot_shrink(self):
        rs=records();c=[dict(id='positive',group='test',kind='PositiveLight')]
        result=evaluate(rs,predictions(rs[:1],[[32]*3]),c,POLICY)
        self.assertEqual(result['overall']['prediction_coverage']['denominator'],36)
        self.assertAlmostEqual(result['overall']['prediction_coverage']['value'],1/36)
        self.assertFalse(result['success'])

    def test_domain_nonfinite_alias_and_cross_map_rejection(self):
        rs=records();c=[dict(id='positive',group='test',kind='PositiveLight')]
        self.assertEqual(evaluate(rs,[],c,POLICY,encoding='Unknown')['status'],'NotComparable')
        for pred in (predictions(rs,[[math.nan]*3]*8),predictions(rs,[[256]*3]*8),
                     predictions(rs)+predictions(rs[:1]),[dict(id='other-map',status='Unavailable')]):
            with self.assertRaises(ValueError):evaluate(rs,pred,c,POLICY)
        p=predictions(rs,status='Unknown');p[0]['rgb']=[0]*3
        with self.assertRaises(ValueError):evaluate(rs,p,c,POLICY)
        for rgb in ([math.inf,0,0],[-1,0,0],[256,0,0]):
            with self.assertRaises(ValueError):target(dict(rs[0],raw=rgb))

    def test_truncation_and_shared_peak(self):
        r=dict(id='clipped',area=1,raw=[255,127,51]);t=target(r)
        self.assertEqual(t['lower'],[255,127,51]);self.assertEqual(t['upper'],[255.,128.,52.])
        self.assertEqual(encoding_contract()['status'],'Passed')

    def test_unknown_is_not_numerical_black(self):
        r=unknown_contracts(POLICY)
        self.assertTrue(all(x['status']=='Passed' for x in r))
        self.assertFalse(r[0]['black']['success']);self.assertFalse(r[-1]['positive_forward_pass'])

    def test_fair_profile_budget_and_heldout_confirmation(self):
        base=dict(parameter_budget=1,split_hash='same',comparison_id=PROFILE)
        alternatives=[dict(base,id='correct',validation_rmse=0.,heldout_rmse=0.),dict(base,id='wrong',validation_rmse=8.,heldout_rmse=8.)]
        self.assertEqual(profile_gate(alternatives,'correct',POLICY)['status'],'Passed')
        alternatives[1]['heldout_rmse']=0.
        self.assertEqual(profile_gate(alternatives,'correct',POLICY)['status'],'Failed')
        alternatives[1]['validation_rmse']=.1
        self.assertEqual(profile_gate(alternatives,'correct',POLICY,'Ambiguous')['status'],'Ambiguous')
        alternatives[1]['parameter_budget']=2
        with self.assertRaises(ValueError):profile_gate(alternatives,'correct',POLICY)

    def test_missing_profile_cannot_disappear(self):
        alternatives=[dict(id='correct',parameter_budget=1,split_hash='same',comparison_id=PROFILE,validation_rmse=0.,heldout_rmse=0.),dict(id='missing',validation_rmse=None)]
        self.assertEqual(profile_gate(alternatives,'correct',POLICY)['status'],'Failed')

    def test_fixture_truth_stays_in_private_recipe(self):
        text,truth=definition('x05_doorway_pair_v3')
        self.assertIn('"light" "1200.0"',text)
        self.assertEqual(truth['compiler_photometry'],'Legacy relative; no SI calibration')
        self.assertEqual(truth['sources'][0]['intensity'],1200.)


class BlindBoundary(unittest.TestCase):
    def test_origin_is_geometry_only_and_zero_origin_compatible(self):
        for i in range(20):
            r=sample(i,[512+1024*i,512,512])
            self.assertEqual(assignment(r,1024,8,7),assignment(r,1024,8,7,(0,0,0)))
        r=sample(0,[81,95,0]);r['footprint_radius']=math.sqrt(128)
        self.assertIsNone(assignment(r,128,1,20261028))
        self.assertIsNotNone(assignment(r,128,1,20261028,[17,31,47]))
        for origin in ([0,0],[0,0,math.nan]):
            with self.assertRaises(ValueError):assignment(r,128,1,1,origin)

    def test_no_hidden_fields_or_neighbour_leakage(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);prefix=root/'fixture';identity='ab'*32
            rs=[sample(i,[17+128*i+64,95,111]) for i in range(40)]
            for r in rs:r.update(groups=['PRIVATE_POSITIVE'],visibility_pairs=['PRIVATE_SOURCE'],source_secret='ANSWER_ONLY')
            write(prefix.with_suffix('.source.json'),dict(map_sha256=identity,pages=[dict(image=0,width=40,height=1,missing=0)],derived_complete=False))
            prefix.with_suffix('.public.jkobs').write_bytes(b'geometry placeholder; actual archives checked by C++ in real run')
            prefix.with_suffix('.rows.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rs))
            prepare(prefix,root/'one',128,1,20261028,[17,31,47]);prepare(prefix,root/'two',128,1,20261028,[17,31,47])
            view=TrainingView(root/'one/inputs',identity);training=list(view.observations())
            self.assertTrue(training);self.assertTrue(all(set(r)==FIELDS for r in training))
            self.assertFalse(hasattr(view,'heldout'));self.assertFalse(hasattr(view,'answers'))
            for member in (root/'one/inputs').iterdir():
                self.assertEqual(member.read_bytes(),(root/'two/inputs'/member.name).read_bytes())
                if member.suffix in ('.json','.jsonl'):
                    self.assertNotIn('PRIVATE_',member.read_text());self.assertNotIn('ANSWER_ONLY',member.read_text())
            private=EvaluationView(root/'one/evaluator',identity)
            with self.assertRaises(PermissionError):list(private.observations('heldout'))
            held=list(EvaluationView(root/'one/evaluator',identity,permitted=('heldout',)).observations('heldout'))
            self.assertFalse({r['id'] for r in held}&{r['id'] for r in training})
            self.assertGreater(min(math.dist(a['position'],b['position']) for a in training for b in held),6+2)
            with self.assertRaises(ValueError):TrainingView(root/'one/evaluator',identity)

    def test_degenerate_chart_does_not_delete_caster(self):
        path=ROOT/'build/q3map2-bake-verified/authoring/base/maps/door_occlusion.bsp'
        if not path.exists():self.skipTest('Original controlled BSP absent')
        with self.assertRaises(ValueError):strict_geometry(path)
        lumps,surfaces,casters=load_geometry(path)
        self.assertGreater(sum(s.get('unsupported_receiver_uv_triangles',0) for s in surfaces),0)
        expected=sum(struct.unpack_from('<i',lumps[13],i+24)[0]//3 for i in range(0,len(lumps[13]),148) if struct.unpack_from('<i',lumps[13],i+8)[0] in (1,3))
        self.assertEqual(len(casters),expected)


class CompletedControls(unittest.TestCase):
    def test_local_real_compiler_contracts(self):
        path=Path(os.environ.get('X05_RUN','build/x05/final'))/'result.json'
        if not path.exists():self.skipTest('Run the pinned compiler/controller workflow first')
        result=load(path);self.assertEqual(result['acceptance']['status'],'Passed')
        self.assertEqual(len(result['original_audit']),34)
        for name,item in result['cases'].items():
            for key in ('unavailable','missing','partial'):self.assertFalse(item['controls'][key]['success'])
            if name=='x05_sky_sealed_v1':self.assertTrue(item['controls']['black']['success'])
            else:self.assertFalse(item['controls']['black']['success'])
        self.assertEqual(result['profiles']['narrow']['status'],'Ambiguous')
        self.assertGreater(len(result['profiles']['broad']['partitions']['heldout']['normal_direction_counts']),1)

    def test_external_packet_cannot_skip_required_cases_or_change_truth(self):
        folder=Path(os.environ.get('X05_RUN','build/x05/final'));manifest=folder/'manifest.json'
        if not manifest.exists():self.skipTest('Pinned controller workflow absent')
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'pred.json';packet=dict(schema='org.openjk.forward-predictions.v1',manifest_sha256=digest(manifest),cases={})
            write(p,packet);self.assertEqual(score(manifest,p)['status'],'Failed')
            packet['truth']={};write(p,packet)
            with self.assertRaises(ValueError):score(manifest,p)

    def test_controller_only_target_copy_is_plumbing_not_recovery(self):
        folder=Path(os.environ.get('X05_RUN','build/x05/final'));manifest=folder/'manifest.json'
        if not manifest.exists():self.skipTest('Pinned controller workflow absent')
        with tempfile.TemporaryDirectory() as t:
            packet=dict(schema='org.openjk.forward-predictions.v1',manifest_sha256=digest(manifest),cases={})
            for f in load(manifest)['fixtures']:
                if not f['required']:continue
                case=load(f['private_case']);kind=case['contracts'][0]['kind']
                packet['cases'][f['id']]=(predictions(case['rows'],[r['raw'] for r in case['rows']])
                    if kind not in ('ExpectedUnknown','ExpectedNotComparable') else predictions(case['rows'],status='NotComparable' if kind=='ExpectedNotComparable' else 'Unknown',reason='Controller-only status sanity'))
            p=Path(t)/'controller-only-copy.json';write(p,packet)
            self.assertEqual(score(manifest,p)['status'],'Passed')
            # Even perfect easy RGB cannot omit the dedicated 35-texel boundary.
            if 'moving_shadow_boundary' in packet['cases']:
                del packet['cases']['moving_shadow_boundary'];write(p,packet)
                self.assertEqual(score(manifest,p)['status'],'Failed')


if __name__=='__main__':unittest.main()
