"""Numerical and access-boundary tests for X03; standard library only."""
from dataclasses import replace
import math
import random
import unittest
from bake_model import encode_color
from hypotheses import catalogue, continuous_encode, evaluate, evaluate_many, UnspecifiedModel
from identifiability import (Observation, Configuration, Prepared, own_scene, compare,
                            interval_residual, retention, feature_assessment)
from confounding import analytic_experiments, paired_bootstrap


class Hypotheses(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles=catalogue()
        cls.inv=cls.profiles['NetRadiant-d9338309-JA/isq_cos']
        cls.lin=cls.profiles['NetRadiant-d9338309-JA/linear_noangle']

    def test_scoped_verification_and_unknown(self):
        self.assertEqual(self.inv.digest(),catalogue()[self.inv.stable_id].digest())
        self.assertEqual(self.inv.rule('attenuation').knowledge,'VerifiedSubset')
        self.assertEqual(self.inv.rule('sampling_visibility').knowledge,'Unknown')
        self.assertEqual(self.inv.rule('absolute_calibration').knowledge,'Unknown')
        outputs=evaluate_many([self.profiles['UnknownProfile'],self.inv],(0,0,0),(0,0,1),
                              [((0,0,192),(300,300,300),(0,0,-1))],[True])
        self.assertIsNone(outputs[0]['rgb']); self.assertEqual(outputs[0]['status'],'UnspecifiedModel')
        self.assertFalse(outputs[1]['compiler_identity_verified'])
        self.assertNotIn('verified',outputs[1])

    def test_encoding_is_existing_compiler_stage(self):
        rnd=random.Random(507)
        for _ in range(128):
            rgb=[rnd.random()*900 for _ in range(3)]
            for gamma,compensate,exposure in ((1,1,0),(2.2,1,0),(1,2,0),(2.2,2,255)):
                continuous=continuous_encode(rgb,gamma,compensate,exposure)
                self.assertEqual([int(v) for v in continuous],encode_color(rgb,gamma=gamma,compensate=compensate,exposure=exposure))
        self.assertEqual(continuous_encode([510,255,51]),[255,127.5,25.5])

    def test_linear_amplitude_changes_reach(self):
        def f(q): return evaluate(self.lin,(0,0,0),(0,0,1),[((0,0,64),(q,q,q),(0,0,-1))],[True])[0]
        self.assertEqual(f(100),29.75)
        self.assertEqual(f(200),123.5)
        self.assertNotEqual(f(200),2*f(100))

    def test_linear_cutoff_plateau_does_not_hide_narrow_fit(self):
        model=self.profiles['NetRadiant-d9338309-JA/linear_cos']
        rows,c,q=own_scene('cutoff',self.inv,'narrow')
        self.assertGreater(max(v.rgb[0] for v in rows),0)
        parts=[[r for r in rows if r.partition==p] for p in ('train','validation','heldout')]
        result=compare([model],*parts,[c],passes=1)
        self.assertLess(result['results'][0]['heldout']['interval_rmse'],1.)

    def test_intervals_and_censoring(self):
        self.assertEqual(interval_residual(127.3,127),0)
        self.assertAlmostEqual(interval_residual(126.4,127),.6)
        self.assertEqual(interval_residual(10000,255),0)
        self.assertEqual(interval_residual(250,255),5)
        with self.assertRaises(ValueError): Observation('bad',(0,0,0),(0,0,1),(.5,.5,.5),'b','train',domain='LinearSourceRelative')
        with self.assertRaises(UnspecifiedModel): evaluate(self.inv,(0,0,0),(0,0,1),[((0,0,64),(100,100,100),(0,0,-1))],[None])

    def test_patch_grouping_and_fitting_boundary(self):
        data,configuration,q=own_scene('test',self.inv,'narrow')
        groups={}
        for row in data: groups.setdefault(row.block,set()).add(row.partition)
        self.assertTrue(all(len(v)==1 for v in groups.values()))
        for a in data:
            for b in data:
                if a.partition!=b.partition: self.assertGreater(math.dist(a.position,b.position),2*a.footprint+4)
        train=[r for r in data if r.partition=='train']; val=[r for r in data if r.partition=='validation']; held=[r for r in data if r.partition=='heldout']
        one=compare([self.inv,self.lin],train,val,held,[configuration],passes=1)
        two=compare([self.inv,self.lin],train,val,[replace(r,rgb=(255,255,255)) for r in held],[configuration],passes=1)
        self.assertEqual(one['ranking'],two['ranking'])
        self.assertEqual([r['parameters'] for r in one['results']],[r['parameters'] for r in two['results']])
        changed=compare([self.inv,self.lin],train,[replace(r,rgb=(0,0,0)) for r in val],held,[configuration],passes=1)
        self.assertEqual([r['parameters'] for r in one['results']],[r['parameters'] for r in changed['results']])
        self.assertGreaterEqual(len(retention(one)['regret_band_sweep']['1.0']),2)
        with self.assertRaises(ValueError): compare([self.inv],val,val,held,[configuration])

    def test_broad_data_discriminates_known_amplitudes(self):
        data,c,q=own_scene('broad',self.inv,'broad')
        parts=[[r for r in data if r.partition==p] for p in ('train','validation','heldout')]
        result=compare([self.inv,self.lin],*parts,[c],fixed_amplitudes=q)
        self.assertEqual(result['ranking'][0],self.inv.stable_id)
        self.assertLess(result['results'][0]['heldout']['interval_rmse'],1e-9)
        self.assertGreater(result['results'][1]['heldout']['interval_rmse'],5)

    def test_prepared_matches_public_kernel(self):
        recipe=dict(gamma=1.,compensate=1.,exposure=0.,srgb=False)
        for suffix in ('isq_cos','linear_noangle','linear_cos','isq_noangle','isq_half_lambert','isq_scaled_angle','spot'):
            model=self.profiles['NetRadiant-d9338309-JA/'+suffix]
            row=Observation('a',(32.,48.,1.),(0.,0.,1.),(0,0,0),'a','train')
            conf=Configuration('c',((0.,0.,192.),),((0.,0.,-1.),))
            q=[400.,200.,100.]
            batch=Prepared(model,[row],conf).predict(q,recipe)[0]
            public=evaluate(model,row.position,row.normal,[(conf.origins[0],q,conf.axes[0])],[True])
            self.assertEqual(batch,public)

    def test_invalid_and_unsupported_are_not_black(self):
        invalid=replace(self.inv,rules=tuple(replace(r,selector='future') if r.feature=='angle' else r for r in self.inv.rules))
        result=evaluate_many([invalid],(0,0,0),(0,0,1),[((0,0,192),(300,300,300),(0,0,-1))],[True])
        self.assertEqual(result[0]['status'],'UnspecifiedModel'); self.assertIsNone(result[0]['rgb'])
        for position,normal in (((math.nan,0,0),(0,0,1)),((0,0,0),(0,0,0))):
            with self.assertRaises(ValueError): Observation('bad',position,normal,(0,0,0),'b','train')
        with self.assertRaises(ValueError): Configuration('bad',((0,0,0),),())

    def test_exact_gauges_and_sampling_uncertainty(self):
        a=analytic_experiments()
        self.assertLess(a['linear_inverse_square_tangent']['narrow']['rmse'],.01)
        self.assertGreater(a['linear_inverse_square_tangent']['broad']['rmse'],10)
        self.assertLess(a['gamma_compensation_gauge_error'],1e-10)
        self.assertEqual(a['albedo_intensity_exact_error'],0)
        self.assertEqual(a['gaussian_shadow_filter_extent'][0]['curve'],a['gaussian_shadow_filter_extent'][2]['curve'])
        self.assertEqual(paired_bootstrap({'x':1},{'x':2})['status'],'InsufficientIndependentBlocks')
        values={str(i):float(i) for i in range(8)}
        self.assertEqual(paired_bootstrap(values,values),paired_bootstrap(values,values))

    def test_conditional_features_do_not_identify_compiler(self):
        rows,c,q=own_scene('x',self.inv,'narrow')
        parts=[[r for r in rows if r.partition==p] for p in ('train','validation','heldout')]
        first=compare([self.inv,self.lin],*parts,[c],passes=1)
        second=compare([self.inv,self.lin],*parts,[c],passes=1)
        self.assertEqual(first,second)
        assessed=feature_assessment(first,self.profiles)
        self.assertEqual(assessed['historical_profile'],'UnknownProfile')
        self.assertIsNone(assessed['profile_posterior'])
        self.assertGreater(len(assessed['conditional_feature_sets']['1.0']['attenuation']),1)


if __name__=='__main__': unittest.main()
