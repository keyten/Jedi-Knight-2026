"""Non-vacuous S02 controller admission. Numerical tests live in C++.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import unittest
from run import acceptance

def positive():
    return dict(code=0,state='SunHypotheses',hypotheses=[dict(canonical_suns=1)],heldout=[dict(code=0,rmse=.1,maximum_error=.5,coverage=dict(used=32,requested=32,black_rmse=40,positive_blocks=3))])

class Gates(unittest.TestCase):
    def test_positive_control(self):self.assertEqual(acceptance('sun_oriented',positive(),.5)['status'],'Passed')
    def test_black_and_missing_do_not_pass(self):
        for changes in (dict(code=1,hypotheses=[]),dict(heldout=[dict(code=0,rmse=40,maximum_error=90,coverage=dict(used=32,requested=32,black_rmse=40,positive_blocks=3))])):
            r=positive();r.update(changes)
            with self.assertRaises(AssertionError):acceptance('sun_oriented',r,.5)
    def test_unavailable_is_not_zero(self):
        r=positive();r['heldout'][0]['code']=1;r['heldout'][0]['rmse']=0
        with self.assertRaises(AssertionError):acceptance('sun_oriented',r,.5)
    def test_incomplete_prediction_cannot_hide(self):
        r=positive();r['heldout'][0]['coverage']['used']=16
        a=acceptance('sun_oriented',r,.5);self.assertEqual(a['status'],'PartialUnsupportedVisibility');self.assertFalse(a['strict_complete_coverage_gate'])
    def test_empty_skylight_and_wrong_sun_rejected(self):
        r=positive();r['state']='SkyOnly'
        with self.assertRaises(AssertionError):acceptance('sky_aperture_positive_v2',r,None)
        r['hypotheses']=[]
        with self.assertRaises(AssertionError):acceptance('sky_aperture_positive_v2',r,None)
    def test_no_false_sun_on_negative_controls(self):
        for name in ('ambient_only','no_sun','multi_directional'):
            with self.assertRaises(AssertionError):acceptance(name,positive(),.5)
    def test_low_signal_not_positive(self):
        r=positive();r['heldout'][0]['coverage']['black_rmse']=0
        with self.assertRaises(AssertionError):acceptance('sun_oriented',r,.5)
    def test_angular_tolerance(self):
        with self.assertRaises(AssertionError):acceptance('sun_oriented',positive(),15)
    def test_positive_sky_only(self):
        r=positive();r.update(state='SkyOnly',hypotheses=[dict(canonical_suns=0)])
        self.assertEqual(acceptance('sky_aperture_positive_v2',r,None)['status'],'Passed')

if __name__=='__main__':unittest.main()
