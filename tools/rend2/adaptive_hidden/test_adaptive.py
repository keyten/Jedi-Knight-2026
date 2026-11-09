"""Input separation, fixture exclusion and negative-control reporting tests."""
import ast
import copy
from pathlib import Path
import unittest

from .evaluate import coverage
from .propose import explained_areas
from .run import POLICY, adapt, spatial


class AdaptiveTests(unittest.TestCase):
    def test_ambiguous_rejected_and_partial_anchors_do_not_hide_regions(self):
        report = dict(schema='org.openjk.fixture-associations.v1', input=dict(map_sha256='owned'), associations=[
            dict(observation_id='bright', selected=True, status='selected_conditional', score=.9, known_weight=.6,
                 supports=[dict(eligible=True)]*4)])
        mapping = {'1': ['bright'], '2': ['bright', 'unexplained'], '3': []}
        self.assertEqual(explained_areas(report, mapping, 'owned'), [1])
        for changes in (dict(status='ambiguous'), dict(selected=False), dict(score=None), dict(known_weight=.1),
                        dict(supports=[dict(eligible=True), dict(eligible=False)])):
            other = copy.deepcopy(report); other['associations'][0].update(changes)
            self.assertEqual(explained_areas(other, mapping, 'owned'), [])
        with self.assertRaises(ValueError): explained_areas(report, mapping, 'other-map')

    def test_exact_fixed_caps_and_native_request_rows_preserved(self):
        old = 'JKHIDDEN1 owned\n0 0 0 256 256 256\n384 4 2 1 0 1\n1\n1 0 0 0 256 256 256 0 0 0 256 256 256\n6\noriginal-training-rows\n2\n0 1\n'
        for strategy in ('strict', 'strict_geometry', 'adaptive'):
            lines = adapt(old, strategy, [1]).splitlines()
            self.assertIn('original-training-rows', lines)
            self.assertEqual(lines[2].split()[0], str(POLICY['max_nodes_per_area']))
            self.assertEqual(lines[3].split()[-3:], [str(POLICY[k]) for k in ('max_rays', 'max_forward_calls', 'max_work')])
            self.assertEqual(lines[3].split()[0], str(int(strategy == 'adaptive')))

    def test_spatial_selection_does_not_use_rgb(self):
        rows = [dict(id=str(i), position=[i, i%3, 0], raw=[i]*3) for i in range(20)]
        first = [r['id'] for r in spatial(rows, 8)]
        for r in rows: r['raw'] = [255-r['raw'][0]]*3
        self.assertEqual(first, [r['id'] for r in spatial(list(reversed(rows)), 8)])

    def test_gi_only_false_proposals_remain_in_the_denominator(self):
        c = dict(position=[0, 0, 128], shape_score=.01, source_kind=0, outward=[0, 0, -1])
        m = coverage([c], [], 32, .12)
        self.assertEqual(m['unmatched_alternatives'], 1)
        self.assertIsNone(m['recall'])
        m = coverage([dict(c, shape_score=None)], [], 32, .12)
        self.assertEqual(m['candidates'], 1)
        self.assertEqual(m['score_gated_candidates'], 0)

    def test_proposal_runner_does_not_import_answer_evaluator(self):
        path = Path(__file__).with_name('run.py')
        tree = ast.parse(path.read_text())
        imports = [n.module for n in ast.walk(tree) if isinstance(n, ast.ImportFrom)]
        self.assertFalse(any('evaluate' in (name or '') for name in imports))
        self.assertNotIn('source-answer.json', path.read_text())


if __name__ == '__main__':
    unittest.main()
