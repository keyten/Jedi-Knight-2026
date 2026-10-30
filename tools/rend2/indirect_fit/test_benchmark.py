import copy
import json
from pathlib import Path
import unittest
from benchmark import validate

class Gates(unittest.TestCase):
    def witness(self):
        path=Path(__file__).parent/'results/numerical.json'
        if not path.exists():self.skipTest('Production witness not generated')
        return json.loads(path.read_text())['owned']
    def test_production(self):validate(self.witness())
    def test_black_missing_false_gi(self):
        for field,value in [('heldout_mse',1.),('heldout_mse',float('nan')),('false_emitters',1),('direct_only_mse',0)]:
            w=copy.deepcopy(self.witness());next(r for r in w['scenes'] if r['scene']=='red_reflector')[field]=value
            with self.assertRaises(ValueError):validate(w)
    def test_direct_roots_cannot_disappear(self):
        w=self.witness();next(r for r in w['scenes'] if r['scene']=='unresolved_fill')['fixed_direct_amplitudes_changed']=True
        with self.assertRaises(ValueError):validate(w)

if __name__=='__main__':unittest.main()
