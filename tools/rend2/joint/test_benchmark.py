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
    def test_no_black_or_false_success(self):
        for field,value in [('heldout_mse',1e9),('heldout_mse',float('nan')),('black_mse',0),('physical_existence_certified',True)]:
            w=copy.deepcopy(self.witness());next(x for x in w['cases'] if x['name']=='two_colour')[field]=value
            with self.assertRaises(ValueError):validate(w)
    def test_weak_and_ambient(self):
        for name,count in [('bright_plus_weak',1),('ambient_no_fake_emitter',1)]:
            w=copy.deepcopy(self.witness());next(x for x in w['cases'] if x['name']==name)['preferred_sources']=count
            with self.assertRaises(ValueError):validate(w)
    def test_ambiguity_and_missing(self):
        w=self.witness();w['cases']=[r for r in w['cases'] if r['name']!='coincident_count_ambiguity']
        with self.assertRaises(ValueError):validate(w)

if __name__=='__main__':unittest.main()
