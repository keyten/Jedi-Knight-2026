"""X07 numerical and adversarial boundaries. No Python lighting implementation."""
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from common import admission,loss,weights,eigenvalues,solve,load,write,digest,canonical
from client import Client,ForwardUnavailable,BudgetExceeded,FitCancelled

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent

def row(i,raw):
    return dict(id=str(i),raw=raw,area=1.,position=[i*128.,0,0],normal=[0,0,1],encoding=0,transfer=1,primaries=1,style=0)

class Numbers(unittest.TestCase):
    def test_interval_censoring_shared_peak_domain(self):
        rows=[row(0,[255,0,127])];w,_=weights(rows)
        self.assertEqual(loss(rows,[[255,.9,127.5]],w),(0.,0.))
        self.assertGreater(loss(rows,[[254,.9,127.5]],w)[0],0)
        with self.assertRaises(ValueError):loss(rows,[[510,0,255]],w)

    def test_dark_is_not_positive_success(self):
        self.assertEqual(admission([row(i,[0]*3)for i in range(4)])['status'],'InsufficientPositiveEvidence')

    def test_fully_saturated_is_unidentifiable(self):
        self.assertEqual(admission([row(i,[255]*3)for i in range(4)])['status'],'CensoredUnidentifiable')

    def test_unknown_domain_not_black(self):
        rows=[row(i,[30]*3)for i in range(4)];rows[0]['primaries']=0
        with self.assertRaises(ValueError):admission(rows)

    def test_nonfinite_geometry_and_wrong_transfer_rejected(self):
        rows=[row(i,[30]*3)for i in range(4)];rows[0]['position'][0]=math.inf
        with self.assertRaises(ValueError):admission(rows)
        rows[0]=row(0,[30]*3);rows[0]['transfer']=2
        with self.assertRaises(ValueError):admission(rows)

    def test_missing_heldout_predictions_rejected(self):
        with self.assertRaises(ValueError):loss([row(0,[10]*3)],[],[1.])

    def test_rank_deficiency_and_small_solve(self):
        self.assertAlmostEqual(eigenvalues([[1.,1.],[1.,1.]])[1],0)
        self.assertEqual(solve([[2.,0.],[0.,4.]],[4.,8.]),[2.,2.])
        with self.assertRaises(ValueError):solve([[1.,1.],[1.,1.]],[1.,1.])

class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe'
        cls.fixture=Path(os.environ.get('JK_X07_FIXTURE_ROOT',str(ROOT/'build/x07-final-a')))/'point_room/correct/inputs'
        if not cls.exe.exists() or not cls.fixture.exists():raise unittest.SkipTest('Build adapter and run the documented X07 fixtures first; no numerical pass claimed')

    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='x07-test-',dir=ROOT/'build');self.root=Path(self.tmp.name).resolve()
        self.assertIn((ROOT/'build').resolve(),self.root.parents) # verified cleanup target
        self.inputs=self.root/'inputs';shutil.copytree(self.fixture,self.inputs)

    def tearDown(self):self.tmp.cleanup()

    def worker(self,probe=None,denied_paths=()):
        args=[sys.executable,'-X','utf8',str(HERE/'worker.py'),'--inputs',str(self.inputs),'--work',str(self.root/'work'),'--exe',str(self.exe)]
        if probe:args+=['--probe',probe]
        for path in denied_paths:args+=['--deny-probe',str(path)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=30)
        path=self.root/'work/fit.json';self.assertTrue(path.exists(),p.stderr);return p,load(path)

    def test_actual_cancel_work_unknown_and_feature(self):
        for command,error in [('C','FitCancelled'),('B','BudgetExceeded'),('V','ForwardUnavailable'),('U','ForwardUnavailable')]:
            p,result=self.worker(command);self.assertEqual(p.returncode,2,p.stderr);self.assertEqual(result['status'],error)
            self.assertIsNone(result['fitted_rgb']);self.assertFalse(result['runtime_publication'])

    def test_corrupt_dataset_rejected_before_prediction(self):
        p=self.inputs/'geometry.jkobs';data=bytearray(p.read_bytes());data[200]^=1;p.write_bytes(data)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('integrity',result['reason']);self.assertIsNone(result['fitted_rgb'])

    def test_true_amplitude_field_not_admitted(self):
        p=self.inputs/'fit-config.json';cfg=load(p);cfg['true_amplitudes']=[[300,300,300]];write(p,cfg)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('private config',result['reason']);self.assertIsNone(result['fitted_rgb'])

    def test_actual_training_view_no_heldout_backing(self):
        sys.path.insert(0,str(HERE.parent/'datasets'));from solver_view import TrainingView
        view=TrainingView(self.inputs,load(self.inputs/'fit-config.json')['map_sha256'])
        self.assertFalse(hasattr(view,'heldout'));self.assertFalse((self.inputs/'heldout.jsonl').exists())
        with self.assertRaises(TypeError):list(view.observations('heldout'))

    def test_existing_hidden_rgb_and_answer_reads_denied(self):
        private=self.root/'private';private.mkdir()
        hidden=[private/'heldout.jsonl',private/'validation.jsonl',private/'answer.json']
        for path in hidden:path.write_text('{"secret": "must never enter solver"}')
        _,result=self.worker(denied_paths=hidden)
        self.assertEqual(result['status'],'FrozenDiagnosticFit')
        self.assertEqual(sum(s.startswith('existing-private-file-') for s in result['denied_private_reads']),3)

    def test_actual_positive_fit_defeats_black(self):
        _,result=self.worker();self.assertEqual(result['status'],'FrozenDiagnosticFit')
        rows=[json.loads(s) for s in (self.inputs/'train.jsonl').read_text().splitlines()];w,_=weights(rows)
        with Client(self.exe,self.inputs/'request.txt',2) as client:
            fitted=client.predict([v for rgb in result['fitted_rgb'] for v in rgb],'R')['rgb']
            black=client.predict([0.,0.,0.],'R')['rgb']
        fitted_loss,_=loss(rows,fitted,w);black_loss,_=loss(rows,black,w)
        self.assertGreater(black_loss,16.)
        self.assertLess(fitted_loss,black_loss*.25)
        self.assertEqual(result['strict_replay_max_difference'],0.)
        self.assertEqual(result['numerical_support']['prediction_status_names'],['AvailableConditional'])
        self.assertEqual(result['numerical_support']['absolute_photometry'],'NotComparable')

    def test_repeated_fit_has_identical_semantics(self):
        _,a=self.worker();_,b=self.worker()
        self.assertEqual({k:v for k,v in a.items() if k!='timing'},{k:v for k,v in b.items() if k!='timing'})

    def test_truncated_training_stream_rejected(self):
        p=self.inputs/'train.jsonl';p.write_text(p.read_text().splitlines()[0]+'\n')
        manifest=self.inputs/'manifest.json';m=load(manifest);m['sha256']['train.jsonl']=digest(p);write(manifest,m)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('Truncated',result['reason']);self.assertIsNone(result['fitted_rgb'])

    def test_zero_seed_geometry_protocol_only(self):
        lines=(self.inputs/'request.txt').read_text().splitlines();count=int(lines[3]);self.assertEqual(count,1)
        self.assertEqual([float(v)for v in lines[4].split()[8:11]],[0.,0.,0.])
        self.assertFalse(any(p.suffix=='.bsp' or 'answer' in p.name or 'heldout' in p.name for p in self.inputs.iterdir()))

    def test_cross_map_and_corrupt_request(self):
        p=self.inputs/'fit-config.json';cfg=load(p);cfg['map_sha256']='f'*64;write(p,cfg)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('map mismatch',result['reason'])

    def test_budget_exhaustion_publishes_no_fit(self):
        p=self.inputs/'fit-config.json';cfg=load(p);cfg['solver']['max_calls']=2;write(p,cfg)
        _,result=self.worker();self.assertEqual(result['status'],'BudgetExceeded');self.assertIsNone(result['fitted_rgb'])

    def test_unknown_profile_not_modern_default(self):
        p=self.inputs/'fit-config.json';cfg=load(p);cfg['profile']['selector']='UnknownProfile';write(p,cfg)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('UnknownProfile',result['reason']);self.assertIsNone(result['fitted_rgb'])

    def test_fully_saturated_training_not_exact_amplitude(self):
        p=self.inputs/'train.jsonl';rows=[json.loads(s)for s in p.read_text().splitlines()]
        for r in rows:r['raw']=[255,255,255];r['source']=[1.,1.,1.];r['channel_flags']=[9,9,9]
        p.write_text(''.join(json.dumps(r)+'\n'for r in rows));manifest=self.inputs/'manifest.json';m=load(manifest);m['sha256']['train.jsonl']=digest(p);write(manifest,m)
        _,result=self.worker();self.assertEqual(result['status'],'CensoredUnidentifiable');self.assertIsNone(result['fitted_rgb'])

    def test_changed_receiver_correspondence_rejected(self):
        p=self.inputs/'request.txt';lines=p.read_text().splitlines();first=6 # one source with zero polygon, then receiver count
        fields=lines[first].split();fields[2]=str(float(fields[2])+1);lines[first]=' '.join(fields);p.write_text('\n'.join(lines)+'\n')
        c=self.inputs/'fit-config.json';cfg=load(c);cfg['geometry_sha256']=digest(p);write(c,cfg)
        _,result=self.worker();self.assertEqual(result['status'],'ValueError');self.assertIn('correspondence',result['reason'])

    def test_target_colour_mutation_changes_prediction_request_never(self):
        # Raw/decoded training aliases may change in adversarial fixtures, but
        # they have no representation in the geometry-only C++ request.
        before=digest(self.inputs/'request.txt');p=self.inputs/'train.jsonl';rows=[json.loads(s)for s in p.read_text().splitlines()]
        for r in rows:r['raw']=[0,0,0];r['source']=[0.,0.,0.]
        p.write_text(''.join(json.dumps(r)+'\n'for r in rows));manifest=self.inputs/'manifest.json';m=load(manifest);m['sha256']['train.jsonl']=digest(p);write(manifest,m)
        _,result=self.worker();self.assertEqual(result['status'],'InsufficientPositiveEvidence');self.assertIsNone(result['fitted_rgb']);self.assertEqual(digest(self.inputs/'request.txt'),before)

if __name__=='__main__':unittest.main()
