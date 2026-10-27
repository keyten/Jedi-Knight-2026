"""Deterministic independent reference, boundary and uncertainty regression tests.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import replace
import math
import unittest
from geometry import (Triangle,footprint,rectangle,patch_mesh,area,intersect,subtract,
                      ray_triangle,unit,sub)
from integrate import (Scene,Source,Node,integrated,sampled,gauss,source_nodes,response,
                       BudgetError)
from analytic import rectangle_reference,finite_rectangle_visibility,smooth_point_ad,spot_orientation_ad
from fixtures import default,wall,quad,doorway,tilted
from api import evaluate,position_jacobian,Policy
from adaptive import adaptive
from localize import experiment


class FootprintTests(unittest.TestCase):
    def test_quadrature(self):
        for n in (1,2,4,8):
            self.assertAlmostEqual(sum(w for _,w in gauss(n)),1.,places=12)
            self.assertAlmostEqual(sum(x*w for x,w in gauss(n)),.5,places=12)

    def test_uv_not_nominal_square(self):
        t=Triangle('a',1,((0.,0.,0.),(4.,0.,0.),(1.,2.,0.)),((0.,0.),(1.,0.),(.25,1.)),((0.,0.,1.),)*3)
        fp=footprint('a',[t],((0.,0.),(1.,1.)))
        self.assertAlmostEqual(fp.coverage,.5); self.assertAlmostEqual(fp.world_area,4.)
        self.assertIn('PartialCoverage',fp.issues)

    def test_two_triangles_one_energy_sample(self):
        fp,_,_=default()
        self.assertEqual(len(fp.pieces),2); self.assertAlmostEqual(fp.world_area,.01)
        self.assertAlmostEqual(fp.coverage,1.)

    def test_duplicate_overlap_padding(self):
        t=Triangle('a',1,((0.,0.,0.),(1.,0.,0.),(0.,1.,0.)),((0.,0.),(1.,0.),(0.,1.)),((0.,0.,1.),)*3)
        fp=footprint('a',[t,t],((0.,0.),(1.,1.)))
        self.assertAlmostEqual(fp.coverage,.5); self.assertIn('DuplicateReference',fp.issues)
        other=replace(t,id='b',world=((0.,0.,1.),(1.,0.,1.),(0.,1.,1.)))
        with self.assertRaises(ValueError): footprint('bad',[t,other],((0.,0.),(1.,1.)))
        pad=footprint('padding',[t],((2.,2.),(3.,3.)))
        with self.assertRaises(ValueError): integrated(pad,Source(),Scene())

    def test_degenerate_inputs(self):
        with self.assertRaises(ValueError): rectangle(1.,1.,0.,1.)
        with self.assertRaises(ValueError): Source(width=-1.)
        with self.assertRaises(ValueError): Source(position=(math.nan,0.,1.))
        with self.assertRaises(ValueError): gauss(0)

    def test_polygon_union_no_double_blocking(self):
        p=((0.,0.),(1.,0.),(1.,1.),(0.,1.)); q=((.25,0.),(.75,0.),(.75,1.),(.25,1.))
        remain=subtract(p,q)
        self.assertAlmostEqual(sum(map(area,remain)),.5)
        remain=[s for r in remain for s in subtract(r,q)]
        self.assertAlmostEqual(sum(map(area,remain)),.5)

    def test_analytic_value_and_visibility(self):
        fp,s,scene=default(); r=integrated(fp,s,scene,tolerance=1e-11)
        exact=rectangle_reference((1.3,1.4,-.05,.05),s.position)
        self.assertAlmostEqual(r['visibility_lower'],2/3,places=11)
        self.assertAlmostEqual(r['contribution_lower'][0],exact['value'],places=10)
        self.assertEqual(r['contribution_lower'],r['contribution_upper'])

    def test_multiple_normals_preserved(self):
        fp,s,scene=default()
        fp=replace(fp,pieces=tuple(replace(p,triangle=replace(p.triangle,normals=((0.,.6,.8),)*3)) for p in fp.pieces))
        estimate=integrated(fp,s,scene)
        self.assertLess(estimate['contribution_upper'][0],.8*.04)

    def test_visibility_not_response_product(self):
        fp=rectangle(1.1,2.,-.1,.1); s=Source(position=(0.,0.,2.5)); scene=Scene(wall())
        actual=integrated(fp,s,scene)
        centre=(1.55,0.,0.); g=response(centre,(0.,0.,1.),source_nodes(s,1)[0])[0]
        self.assertGreater(abs(g*actual['visibility_upper']-actual['contribution_upper'][0]),1e-4)

    def test_xyz_derivative_closed_form(self):
        fp,s,scene=default(); exact=rectangle_reference((1.3,1.4,-.05,.05),s.position)
        h=1e-4
        for k in range(3):
            p=list(s.position); p[k]+=h; plus=integrated(fp,replace(s,position=tuple(p)),scene,tolerance=1e-11)
            p[k]-=2*h; minus=integrated(fp,replace(s,position=tuple(p)),scene,tolerance=1e-11)
            derivative=(plus['contribution_upper'][0]-minus['contribution_upper'][0])/(2*h)
            self.assertAlmostEqual(derivative,exact['jacobian'][k],places=7)

    def test_fixed_five_visibility_zero_gradient(self):
        fp,s,scene=default(); h=1e-5
        def v(z): return sampled(fp,replace(s,position=(0.,0.,z)),scene,n=5)['visibility_upper']
        self.assertEqual((v(4+h)-v(4-h))/(2*h),0.)
        self.assertGreater(rectangle_reference((1.3,1.4,-.05,.05),s.position)['visibility_jacobian'][2],1.)

    def test_smooth_ad_independent_fd(self):
        s=(0.,.2,4.); p=(1.35,0.,0.); value,g=smooth_point_ad(s,p)
        for k in range(3):
            a=list(s); a[k]+=1e-5; b=list(s); b[k]-=1e-5
            fd=(response(p,(0.,0.,1.),Node(tuple(a),(),(1.,1.,1.)))[0]-response(p,(0.,0.,1.),Node(tuple(b),(),(1.,1.,1.)))[0])/2e-5
            self.assertAlmostEqual(fd,g[k],places=10)

    def test_orientation_ad_smooth_not_visibility_derivative(self):
        s=Source(kind='spot'); p=(1.35,0.,0.); theta=math.radians(8.)
        value,g=spot_orientation_ad(s.position,p,theta)
        def f(t): return response(p,(0.,0.,1.),source_nodes(tilted(s,math.degrees(t)),1)[0])[0]
        self.assertAlmostEqual(value,f(theta),places=12)
        self.assertAlmostEqual(g,(f(theta+1e-5)-f(theta-1e-5))/2e-5,places=8)

    def test_clear_blocked_narrow_door(self):
        fp,s,_=default()
        self.assertAlmostEqual(integrated(fp,s,Scene())['visibility_upper'],1.)
        self.assertEqual(integrated(fp,s,Scene(wall(height=3.)))['visibility_upper'],0.)
        near=rectangle(1.8,2.2,-.2,.2)
        result=integrated(near,s,Scene(doorway(.03)))
        self.assertGreater(result['visibility_upper'],0.)
        self.assertLess(result['visibility_upper'],1.)

    def test_unknown_optics_never_clear(self):
        fp,s,_=default()
        for optical in ('alpha_unknown','translucent','moving','unsupported'):
            r=integrated(fp,s,Scene(wall(optical=optical)))
            self.assertLess(r['visibility_lower'],r['visibility_upper'])
            self.assertIn(optical,r['geometry_uncertainty'])
        r=integrated(fp,s,Scene(wall(height=3.),unbounded_unknown=True))
        self.assertEqual(r['visibility_upper'],0.) # known wall remains opaque
        r=integrated(fp,s,Scene(unbounded_unknown=True))
        self.assertEqual(r['visibility_lower'],0.); self.assertAlmostEqual(r['visibility_upper'],1.)

    def test_sky_not_empty(self):
        fp,s,_=default(); sun=Source(kind='sun',angular_radius=0.)
        self.assertEqual(integrated(fp,sun,Scene(require_sky=True))['visibility_lower'],0.)
        sky=quad(((-10.,-10.,10.),(10.,-10.,10.),(10.,10.,10.),(-10.,10.,10.)),'sky','sky')
        r=integrated(fp,sun,Scene(sky,require_sky=True,sky_encloses_casters=True))
        self.assertAlmostEqual(r['visibility_lower'],1.)

    def test_no_universal_bias_thin_wall(self):
        s=Source(position=(2.,0.,1.)); p=(0.,0.,0.); n=source_nodes(s,1)[0]
        scene=Scene(wall(x=1.,height=2.))
        self.assertEqual(scene.query(p,n),(0.,0.))
        # The receiver is not physically displaced by the integration API.
        self.assertEqual(scene.query(p,n,bias=1e-6),(0.,0.))
        plane=quad(((-1.,-1.,0.),(1.,-1.,0.),(1.,1.,0.),(-1.,1.,0.)))
        self.assertEqual(Scene(plane).query(p,Node((.5,0.,0.),(),(1.,1.,1.))),(0.,1.))
        self.assertEqual(Scene(plane).query(p,Node((0.,0.,1.),(),(1.,1.,1.))),(1.,1.))
        fp=rectangle(-.1,.1,-.1,.1)
        uncertain=integrated(fp,Source(position=(.5,0.,0.)),Scene(plane))
        self.assertEqual(uncertain['visibility_lower'],0.)
        self.assertAlmostEqual(uncertain['visibility_upper'],1.)

    def test_rectangle_extent_visibility(self):
        fp,_,scene=default(); s=Source(kind='rectangle')
        value=integrated(fp,s,scene,source_order=16,tolerance=1e-7,limit=1000000)
        exact=finite_rectangle_visibility((1.3,1.4,-.05,.05),s.position,s.width)
        self.assertLess(abs(value['visibility_upper']-exact),.002)

    def test_sun_finite_cone_normalization(self):
        fp,_,_=default(); s=Source(kind='sun',rgb=(2.,1.,.5),angular_radius=.1)
        r=integrated(fp,s,Scene(),source_order=4,limit=100000)
        self.assertAlmostEqual(r['visibility_upper'],1.,places=12)
        self.assertAlmostEqual(r['contribution_upper'][0],2.,places=11)
        dark=integrated(fp,replace(s,rgb=(0.,0.,0.)),Scene(),source_order=2)
        self.assertAlmostEqual(dark['visibility_upper'],1.)

    def test_spot_frame_and_movement_key(self):
        fp,s,scene=default(); s=replace(s,kind='spot')
        self.assertNotEqual(s.key(),tilted(s,15).key())
        self.assertNotEqual(s.key(),replace(s,width=.8).key())
        self.assertGreater(integrated(fp,tilted(s,-15),scene)['contribution_upper'][0],0.)

    def test_patch_envelope_not_confidence(self):
        controls=tuple((x/2,y/2,.2 if x==y==1 else 0.) for y in range(3) for x in range(3))
        mesh,error=patch_mesh(controls,4); fp=footprint('patch',mesh,((0.,0.),(1.,1.)))
        self.assertGreater(error,0.)
        r=integrated(replace(fp,geometry_envelope=error),Source(),Scene(),tolerance=1e-6)
        self.assertEqual(r['status'],'ReceiverGeometryApproximation')
        self.assertIsNone(r['contribution_upper']); self.assertEqual(r['visibility_lower'],0.)

    def test_bounded_work_and_determinism(self):
        fp,s,scene=default()
        with self.assertRaises(BudgetError): integrated(fp,s,scene,limit=1)
        self.assertEqual(integrated(fp,s,scene),integrated(fp,s,scene))
        self.assertEqual(sampled(fp,s,scene,method='halton'),sampled(fp,s,scene,method='halton'))

    def test_api_unavailable_no_fake_bake(self):
        fp,s,scene=default()
        r=evaluate(fp,s,scene,profile='UnknownProfile')
        self.assertFalse(r['available']); self.assertIsNone(r['contribution'])
        self.assertEqual(evaluate(fp,s,scene,policy=Policy(work_limit=1))['status'],'WorkLimit')
        self.assertEqual(position_jacobian(fp,s,Scene(unbounded_unknown=True),1e-4)['derivative_status'],'UnavailableForGeometryInterval')

    def test_adaptive_sampling_not_completeness(self):
        fp=rectangle(1.8,2.2,-.2,.2); s=Source(); scene=Scene(doorway(.0001,height=3.))
        ref=integrated(fp,s,scene)
        approximation=adaptive(fp,s,scene,max_depth=2,min_depth=0)
        self.assertGreater(ref['visibility_upper'],0.)
        self.assertEqual(approximation['visibility_upper'],0.)
        self.assertFalse(approximation['numerical_error_is_certified_bound'])

    def test_multiple_source_contributions(self):
        fp,s,scene=default(); one=integrated(fp,s,scene)
        two=integrated(fp,replace(s,rgb=(2.,2.,2.)),scene)
        self.assertAlmostEqual(two['contribution_upper'][0],2*one['contribution_upper'][0],places=10)

    def test_localization_success_and_basin_failure_both_reported(self):
        r=experiment()['results']; edge=[x for x in r if x['method']=='explicit_boundary']
        self.assertTrue(all(x['position_error']<1e-6 for x in edge))
        self.assertTrue(any(x['position_error']>1 for x in r if x['method']=='integrated_visibility'))


if __name__=='__main__': unittest.main()
