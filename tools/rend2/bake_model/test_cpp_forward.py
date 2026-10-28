"""Production C++ interface tests; explicit skip if the standalone binary is absent.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from validate_cpp import load, request, sources_for, floor_samples

ROOT=Path(__file__).resolve().parents[3]


class ProductionForward(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe=Path(os.environ.get('JK_BAKE_FORWARD_EXE',ROOT/'build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe'))
        if not cls.exe.exists():raise unittest.SkipTest('Build lightrecon_bake_forward_tool first; no numerical pass claimed')

    def call(self,*args):
        return subprocess.run([str(self.exe),*map(str,args)],capture_output=True,text=True,timeout=30)

    def test_missing_capabilities_are_not_black(self):
        for name,code,status in [('unknown_geometry',4,-1),('unknown_photometry',0,0),('gi_operator_unavailable',0,5)]:
            p=self.call('--status',name);self.assertEqual(p.returncode,0,p.stderr);r=json.loads(p.stdout)
            self.assertEqual((r['available'],r['code'],r['status']),(False,code,status));self.assertNotIn('rgb',r)

    def test_encoding_and_channel_coupling(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'values';path.write_text('3\n510 255 0\n255 255 255\n0 0 0\n')
            p=self.call('--encode',path);self.assertEqual(p.returncode,0,p.stderr)
            self.assertEqual(json.loads(p.stdout)['rows'],[[255,127.5,0],[255,255,255],[0,0,0]])

    def test_negative_encoding_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'values';path.write_text('1\n-1 0 0\n')
            self.assertNotEqual(self.call('--encode',path).returncode,0)

    def test_truncated_protocol_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'request';path.write_text('JKBAKE2\n0 0\n')
            self.assertNotEqual(self.call(path).returncode,0)

    def fixture(self,name):
        path=ROOT/'build/bake-model-comparison-final/authoring/base/maps'/(name+'.bsp')
        if not path.exists():self.skipTest('Pinned compiled BSP unavailable; see documented compiler recipe')
        return path

    def test_scoped_boundary_and_geometry_only_request(self):
        path=self.fixture('multi_sun');lumps,_,casters=load(path);rows=floor_samples(lumps)
        case=dict(sun=True,multi=True);text=request(path,lumps,casters,rows,case,sources_for(case),strict=True)
        # Changing every observed colour cannot change the evaluator request.
        stripped=[dict(r,rgb=[1,2,3],deluxe_rgb=[3,2,1]) for r in rows]
        self.assertEqual(text,request(path,lumps,casters,stripped,case,sources_for(case),strict=True))
        with tempfile.TemporaryDirectory() as d:
            q=Path(d)/'request';q.write_text(text);a=self.call(q);b=self.call(q)
            self.assertEqual(a.returncode,0,a.stderr);self.assertEqual(b.returncode,0,b.stderr)
            a,b=json.loads(a.stdout),json.loads(b.stdout);self.assertEqual(a['rows'],b['rows'])
            self.assertEqual(len(a['rows']),1225)
            self.assertTrue(all(r['available'] and r['fidelity']==2 and r['packed']==t['rgb'] for r,t in zip(a['rows'],rows)))

    def test_scoped_replay_does_not_generalize_by_name(self):
        path=self.fixture('inverse_square');lumps,_,casters=load(path);rows=floor_samples(lumps)[:4];case={}
        with tempfile.TemporaryDirectory() as d:
            q=Path(d)/'request';q.write_text(request(path,lumps,casters,rows,case,sources_for(case),strict=True))
            p=self.call(q);self.assertEqual(p.returncode,0,p.stderr)
            self.assertTrue(all(not r['available'] and r['status']==5 for r in json.loads(p.stdout)['rows']))


if __name__=='__main__':unittest.main()
