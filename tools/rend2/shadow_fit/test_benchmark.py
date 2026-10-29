import copy
import json
from pathlib import Path
import unittest
from benchmark import validate


class Gates(unittest.TestCase):
    def witness(self):
        path = Path(__file__).parent/'results/numerical.json'
        if not path.exists():
            self.skipTest('Frozen production witness unavailable')
        return json.loads(path.read_text())['owned']

    def test_production_witness(self):
        validate(self.witness())

    def test_black_missing_nan_or_no_improvement(self):
        for field, value in [('heldout_mse', .136286097328), ('heldout_mse', float('nan')),
                             ('position_error_world', 20), ('black_mse', 0)]:
            w = copy.deepcopy(self.witness())
            next(c for c in w['ablations'] if c['mode'] == 4)[field] = value
            with self.assertRaises(ValueError):
                validate(w)
        w = self.witness()
        w['ablations'].pop()
        with self.assertRaises(ValueError):
            validate(w)

    def test_no_verified_gradient(self):
        w = self.witness()
        w['edge_candidate_confusion'][0]['verified_cast_shadow'] = True
        with self.assertRaises(ValueError):
            validate(w)


if __name__ == '__main__':
    unittest.main()
