"""Regression checks for V02 result classification, not lighting equations."""
import unittest
import subprocess
import sys
from pathlib import Path
from tools.rend2.validation.report import gate,angular_summary
from tools.rend2.validation.bridge import hidden_request
from tools.rend2.validation.run import DEFAULT_PHASES


class ValidationContract(unittest.TestCase):
    def test_missing_is_not_black_success(self):
        self.assertEqual(gate(None,0)['status'],'Unavailable')
        self.assertEqual(gate(0,0,False)['status'],'Unavailable')
        self.assertEqual(gate(0,0)['status'],'Passed')
        self.assertEqual(gate(1,0)['status'],'Failed')

    def test_sky_rejection_and_contribution_allocations_remain_visible(self):
        self.assertEqual(gate(0,250000,False)['status'],'Unavailable')
        self.assertEqual(gate(65536,0,True)['status'],'Failed')

    def test_diffuse_success_cannot_certify_radiance(self):
        row=dict(scene='sharp',representation='SH_L2',irradiance=dict(relative_rms=.01),
                 radiance=dict(relative_rms=.9),quantization=[dict(bytes=108)],fit={})
        result=angular_summary(dict(rows=[row]))['SH_L2']
        self.assertEqual(result['diffuse_mean'],.01)
        self.assertEqual(result['radiance_mean'],.9)
        row['radiance']=None
        result=angular_summary(dict(rows=[row]))['SH_L2']
        self.assertIsNone(result['radiance_mean'])
        self.assertEqual(result['radiance_comparable'],0)

    def test_discovery_request_omits_private_answer_and_hidden_rgb(self):
        # Only an already admitted training observation and geometry enter
        # the native protocol; controller-private answers have no argument.
        policy=dict(bounds_world=[-1,-1,-1,1,1,1],
                    hidden=dict(max_nodes_per_area=32,coarse_divisions=2,refinement_rounds=0))
        train=dict(id='1'*64,surface=3,position=[0,0,0],normal=[0,0,1],
                   raw=[1,2,3],area=1,private_answer=[999,888,777],heldout_rgb=[231,232,233])
        request=hidden_request([train],'a'*64,[(1,[-1,-1,-1,1,1,1])],policy,False)
        for hidden in ('999','888','777','231','232','233'):
            self.assertNotIn(hidden,request)
        self.assertIn('1 2 3 1 1',request)

    def test_default_benchmark_never_launches_games(self):
        self.assertFalse(any(p.startswith('runtime_') for p in DEFAULT_PHASES))
        # This must exit before filesystem prep, let alone spawning an engine.
        tool=Path(__file__).with_name('runtime_mp.py')
        args=[sys.executable,str(tool)]
        for n in ('installation','binaries','renderer','mp-binaries','mp-renderer','output'):
            args+=['--'+n,'unavailable-test-input']
        result=subprocess.run(args,capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,2)
        self.assertIn('MP launch is opt-in',result.stderr)


if __name__=='__main__':
    unittest.main()
