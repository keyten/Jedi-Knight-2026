import copy
import json
from pathlib import Path
import unittest
import numpy as np
from tools.rend2.idf_full.algorithm import Response,Field,fit,sphere
from tools.rend2.idf_full.fixtures import author


class Algorithm(unittest.TestCase):
    def test_sphere_is_closed_triangulation(self):
        vertices,faces=sphere(1)
        self.assertEqual((len(vertices),len(faces)),(42,80))
        np.testing.assert_allclose(np.linalg.norm(vertices,axis=1),1)
        edges={}
        for f in faces:
            for a,b in zip(f,np.roll(f,1)):
                k=tuple(sorted((a,b)));edges[k]=edges.get(k,0)+1
        self.assertTrue(all(v==2 for v in edges.values()))

    def test_nnls_and_joint_lm_recover_from_inexact_initial_positions(self):
        data,truth=author('linear_two',6);d=data['train']
        r=Response(d['positions'],d['normals'],[-1,-1,-1,1,1,1])
        policy=json.loads(Path(__file__).with_name('policy-v1.json').read_text())
        guess=[(np.array(s['position'])+[.08,-.06,.04]).tolist() for s in truth['sources']]
        result=fit(r,np.array(d['values']),guess,policy)
        self.assertLess(result['mse'],1e-12)
        np.testing.assert_allclose(result['positions'],[s['position'] for s in truth['sources']],atol=1e-5)

    def test_unknown_coefficient_is_not_a_zero_prediction(self):
        class Unknown:
            def query(self,p):return dict(free=True,states=[2,2])
        r=Response([[0,0,0],[0,0,0]],[[0,0,1]]*2,[-1,-1,-1,1,1,1],Unknown())
        p=json.loads(Path(__file__).with_name('policy-v1.json').read_text())
        self.assertIsNone(fit(r,np.ones((2,1)),[[0,0,.5]],p))

    def test_negative_residual_is_rejected(self):
        d,_=author('linear_two',3);train=d['train']
        r=Response(train['positions'],train['normals'],[-1,-1,-1,1,1,1])
        p=json.loads(Path(__file__).with_name('policy-v1.json').read_text());p['grid_divisions']=3;p['sphere_subdivisions']=0
        f=Field(r,p)
        with self.assertRaises(ValueError):f.propose(-np.ones((len(train['values']),1)))


if __name__=='__main__':unittest.main()
