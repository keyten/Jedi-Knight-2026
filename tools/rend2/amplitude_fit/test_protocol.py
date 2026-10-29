"""S06 protocol/data-boundary regression checks against built production C++."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'fixed_geometry'))
from client import Client

class Protocol(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe=Path(os.environ.get('JK_S06_EXE',ROOT/'build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe'))
        cls.inputs=ROOT/'build/s06-compiled-full-a/point_room/correct/inputs'
        if not cls.exe.exists()or not cls.inputs.exists():raise unittest.SkipTest('Production executable/compiled fixture unavailable')

    def frame(self,raw=32,extra=''):
        with Client(self.exe,self.inputs/'request.txt',8)as client:
            count=client.hello['rows'];text='F 64 1200 8 1000 1 4 0 0 '+str(count)+' '+' '.join([f'{raw} {raw} {raw} 256 1']*count)+extra
            client.process.stdin.write(text+'\n');client.process.stdin.flush()
            return client._read()

    def test_nonnegative_bounded_production_result(self):
        r=self.frame();self.assertTrue(r['fitted']);self.assertEqual(r['code'],0)
        self.assertTrue(all(0<=v<=1200 for q in r['rgb']for v in q));self.assertEqual(r['coverage']['requested'],r['coverage']['used'])

    def test_training_only_denials_are_recorded(self):
        fit=json.loads((self.inputs.parent/'work/fit.json').read_text())
        self.assertEqual(len(fit['denied_private_reads']),3);self.assertEqual(fit['optimizer'],'ProductionCppAmplitudeFitV1')
        self.assertEqual(fit['production']['strict_difference'],0)

    def test_existing_compiled_positive_gate(self):
        r=json.loads((self.inputs.parents[2]/'summary.json').read_text())
        for case in r['cases'].values():self.assertTrue(case['models']['correct']['evaluation']['heldout_positive_prediction_gate'])

    def test_malformed_quantization_frame_fails(self):
        p=subprocess.run([str(self.exe),'--server',str(self.inputs/'request.txt')],input='F 64 1200 8 1000 1 4 0 0 1 999 0 0 1 1\n',text=True,capture_output=True,timeout=30)
        self.assertNotEqual(p.returncode,0);self.assertNotIn('"fitted":true',p.stdout)

if __name__=='__main__':unittest.main()
