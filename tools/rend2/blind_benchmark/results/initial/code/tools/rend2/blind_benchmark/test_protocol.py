"""Blindness, source matching, censored forward fitting and negative controls.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np
from .solver import encode, interval_error, train_path, load, write, grid_proposals
from .evaluate import match, metric
from .run import semantic_models, verify

POLICY = load(Path(__file__).with_name('policy-v1.json'))


class Protocol(unittest.TestCase):
    def rows(self, values):
        return [dict(id=str(i), raw=list(v), block=[i//2,0,0], normal=[0,0,1], area=1) for i,v in enumerate(values)]

    def test_one_to_one_position_and_type(self):
        truth = [dict(kind='point',position=[0,0,0]),dict(kind='spot',position=[1,0,0])]
        p = [dict(type='spot',position=[0,0,0])]
        r = match(truth,p,32)
        self.assertEqual((r['matched'],r['false_positives'],r['type_matches']),(1,0,0))
        self.assertEqual(match([],p,32)['false_positives'],1)

    def test_censored_intervals_and_coupled_forward_encoding(self):
        values = np.asarray([[510.,255.,0.],[1.,2.,3.]])
        self.assertEqual(encode(values).tolist(),[[255.,127.5,0.],[1.,2.,3.]])
        e = interval_error(np.asarray([[300.,10.5,8.]]),np.asarray([[255.,10.,10.]]))
        self.assertEqual(e.tolist(),[[0.,0.,-2.]])

    def test_empty_model_cannot_pass_multisource(self):
        r = match([dict(kind='point',position=[0,0,0])],[],32)
        self.assertEqual(r['recall'],0)
        self.assertIsNone(r['precision'])
        self.assertEqual(metric([],[])['status'],'Unavailable')

    def test_training_only_path_and_dark_control(self):
        a = np.asarray([[10,0],[0,10],[10,5],[5,10],[20,2],[2,20]],dtype=float)
        target = np.floor(a@np.asarray([[2,1,0],[0,1,3]],dtype=float))
        path, fits, work = train_path(a,self.rows(target),POLICY)
        self.assertEqual(len(path[-1]['sources']) if 'sources' in path[-1] else len(path[-1]['indices']),2)
        self.assertLess(path[-1]['training_mse'],.1)
        self.assertLessEqual(fits,POLICY['maximum_model_fits'])
        self.assertGreater(work,0)
        dark,_,_ = train_path(a,self.rows(np.zeros_like(target)),POLICY)
        self.assertEqual(dark[-1]['indices'],[])

    def test_unknown_grid_is_not_free(self):
        row = dict(correlation_key='x',present=True,space='unknown',boundary=False,dynamic_possible=False,
                   styles=[dict(weak=False,directed=[100,0,0])],position=[0,0,0],consumer256=[0,0,1])
        self.assertEqual(grid_proposals([row]*8,POLICY),[])

    def test_repeat_checks_work_and_parameters(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for n,seconds in [('a',1),('b',10)]:
                (root/n/'scene').mkdir(parents=True)
                write(root/n/'scene/frozen-models.json',dict(powers=[1,2,3],work=100,fit_seconds=seconds))
            verify(root/'a',root/'b',root/'repeat.json')
            self.assertEqual(load(root/'repeat.json')['status'],'Pass')
            write(root/'b/scene/frozen-models.json',dict(powers=[1,2,3],work=101))
            with self.assertRaises(ValueError): verify(root/'a',root/'b',root/'changed.json')


if __name__ == '__main__': unittest.main()
