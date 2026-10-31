"""S13 controller controls: no interpretation of null/empty as physical success."""
import json,unittest
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.rend2.joint.compiled import score,select
class Controls(unittest.TestCase):
    def test_missing_is_unavailable(self):
        row=dict(raw=[80,40,20],position=[0,0,0],area=1)
        self.assertFalse(score([row],[])['available'])
    def test_black_fails_positive(self):
        row=dict(raw=[80,40,20],position=[0,0,0],area=1)
        s=score([row],[dict(id=1,available=True,rgb=[0,0,0])])
        self.assertTrue(s['positive']);self.assertGreater(s['mse'],0);self.assertFalse(s['required'])
    def test_censoring(self):
        row=dict(raw=[255,0,10],position=[0,0,0],area=1)
        self.assertEqual(score([row],[dict(id=1,available=True,rgb=[500,.5,10.5])])['mse'],0)
    def test_geometry_selection_ignores_targets(self):
        rows=[dict(id=str(i),position=[i*128,0,0],normal=[0,0,1],raw=[0,0,0]) for i in range(10)]
        a=[r['id'] for r in select(rows,4)]
        for r in rows:r['raw']=[255,2,90]
        self.assertEqual(a,[r['id'] for r in select(rows,4)])
if __name__=='__main__':unittest.main()
