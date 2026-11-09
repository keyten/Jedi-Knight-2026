"""Discovery identification/abstention and a finite-distance counterexample.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import math
import unittest
from policy import decide, controlled_authored_sun


def directional():
    rows = [dict(id=i+1,world=p) for i,p in enumerate(((-300,0,0),(0,150,0),(300,0,0)))]
    h = dict(model=2,to_source=[0,0,1],sun_compiler_rgb=[120,100,80],sky_compiler_rgb=[0,0,0],
             sampled_angular_ambiguity_radians=.03,
             receivers=[dict(sample=r['id'],exclusion=0,sun_response=1.) for r in rows])
    return dict(code=0,state='SunHypotheses',hypotheses=[h]), rows


class Policy(unittest.TestCase):
    def test_unused_shader_declaration_is_not_sun_provenance(self):
        unused = 'textures/unused {\n q3map_sun 1 1 1 120 0 90\n}\n'
        self.assertFalse(controlled_authored_sun(unused))
        self.assertFalse(controlled_authored_sun(unused+'textures/bake_forward/asr07sky {\n skyparms - - -\n}'))
        self.assertTrue(controlled_authored_sun(unused+'textures/bake_forward/asr07sky {\n q3map_sun 1 1 1 120 0 90\n}'))
    def test_sun_requires_baseline_sky_and_provenance(self):
        result, rows = directional()
        self.assertEqual(decide(result,rows,True,True)['state'],'SunPresent')
        for authored, known in ((False,True),(True,False)):
            self.assertEqual(decide(result,rows,authored,known)['state'],'SunUncertain')
        for r in result['hypotheses'][0]['receivers']:
            r['sun_response'] = 0
        self.assertEqual(decide(result,rows,True,True)['state'],'SunUncertain')

    def test_dense_small_roi_cannot_replace_baseline(self):
        result, rows = directional()
        for r in rows:
            r['world'] = [r['id']*.01,0,0]
        self.assertEqual(decide(result,rows,True,True)['state'],'SunUncertain')

    def test_failures_and_ambiguity_are_never_absence(self):
        result, rows = directional()
        for changed in (dict(code=1),dict(hypotheses=[]),dict(state='AmbiguousSun')):
            r = copy.deepcopy(result); r.update(changed)
            d = decide(r,rows,True,True)
            self.assertEqual(d['state'],'SunUncertain')
            self.assertIsNone(d['sunlight_upper_bound'])

    def test_sky_only_is_conditional_no_sun_explanation(self):
        result = dict(code=0,state='SkyOnly',hypotheses=[dict(model=0)])
        d = decide(result,[],False,True)
        self.assertEqual(d['state'],'SunAbsent')
        self.assertIsNone(d['sunlight_upper_bound'])
        self.assertIsNone(d['relative_strength'])

    def test_distant_point_is_byte_indistinguishable_over_long_baseline(self):
        # Complete unobstructed analytic scene, explicitly linear toy quantizer;
        # not an encoded stock irradiance assumption. Sun and finite lamp give
        # the same bytes, including varied receiver normals and 600 WU baseline.
        distance = 1e8
        normals = ((0,0,1),(.6,0,.8),(0,.8,.6))
        solar, point = [], []
        for x in (-300,0,300):
            for n in normals:
                v = (-x,0,distance)
                r = math.sqrt(sum(c*c for c in v))
                solar.append(int(120.5*n[2]))
                point.append(int(120.5*distance**2*max(0,sum(a*b for a,b in zip(n,v))/r)/r**2))
        self.assertEqual(solar,point)
        result, rows = directional()
        self.assertEqual(decide(result,rows,False,True)['state'],'SunUncertain')


if __name__ == '__main__':
    unittest.main()
