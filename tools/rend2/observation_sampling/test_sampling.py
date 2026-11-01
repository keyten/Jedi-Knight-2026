"""Public-geometry/training-only protocol and honest sampling reports."""
import copy
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('sampling_compiled', Path(__file__).with_name('compiled.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class View:
    _manifest = dict(map_sha256='ab'*32, block_world=256, gap_world=1, seed=7)

    def __init__(self):
        self.geo = [dict(id=f'{i:016x}'+'a'*48, surface=1, block='a'*64,
                         role=('train' if i < 3 else 'heldout'),
                         position=[i*20, 60, 80], normal=[0, 0, 1], area=16,
                         footprint_radius=2, encoding_known=False)
                    for i in range(1, 5)]
        self.train = [dict(id=g['id'], raw=[10, 20, 30], transfer=0, primaries=0)
                      for g in self.geo if g['role'] == 'train']

    def receiver_geometry(self):
        return iter(self.geo)

    def observations(self):
        return iter(self.train)


class SamplingProtocol(unittest.TestCase):
    def test_no_heldout_rgb_or_answers_in_protocol(self):
        view = View()
        text, geometry = module.protocol(view, 16, False)
        self.assertIn('JKSAMPLE1', text)
        self.assertEqual(len(geometry), 4)
        self.assertNotIn('answers', text)
        self.assertNotIn('candela', text)
        self.assertEqual(text.splitlines()[-3], '2')
        self.assertEqual({int(line.split()[0]) for line in text.splitlines()[-2:]}, {1, 2})

    def test_geometry_order_and_validation_never_affect_training_values(self):
        a = View(); b = copy.deepcopy(a)
        b.geo.reverse(); b.train.reverse()
        self.assertEqual(module.protocol(a, 16, False), module.protocol(b, 16, False))

    def test_alternative_is_not_count_recovery(self):
        result = dict(jobs=[dict(grid_radius_world=10, old=dict(candidates=[], rays=0),
                                hierarchical=dict(candidates=[dict(position=[0, 0, 0], source_kind=0),
                                                              dict(position=[1, 0, 0], source_kind=0)], rays=10))])
        truth = dict(ground_truth=dict(lights=[dict(position_world=[0, 0, 0])]))
        report = module.metrics(result, truth)
        self.assertEqual(report['hierarchical']['recall_at_grid_radius'], 1)
        self.assertEqual(report['hierarchical']['unmatched_proposals'], 1)
        self.assertEqual(report['hierarchical']['physical_emitters_published'], 0)

    def test_one_candidate_cannot_recall_two_lamps(self):
        result = dict(jobs=[dict(grid_radius_world=10, old=dict(candidates=[], rays=0),
                                hierarchical=dict(candidates=[dict(position=[0, 0, 0], source_kind=0)], rays=1))])
        truth = dict(ground_truth=dict(lights=[dict(position_world=[0, 0, 0]),
                                              dict(position_world=[1, 0, 0])]))
        report = module.metrics(result, truth)['hierarchical']
        self.assertEqual(report['recall_at_grid_radius'], .5)
        self.assertEqual(report['unmatched_proposals'], 0)


if __name__ == '__main__':
    unittest.main()
