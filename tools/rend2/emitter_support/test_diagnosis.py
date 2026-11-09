"""Owned numerical oracles for ASR17 explanations, not admission changes."""
from fractions import Fraction
import unittest
from tools.rend2.emitter_support.diagnose import clip, exact_distance, inspect, span


def world(boxes):
    w = dict(nodes=[], planes=[], brushes=[], sides=[], leaf_brushes=[], leaves=[])
    for lo, hi in boxes:
        first = len(w['sides'])
        for axis in range(3):
            for sign in (-1, 1):
                n = [0, 0, 0]
                n[axis] = sign
                w['sides'].append(len(w['planes']))
                w['planes'].append([n, -lo[axis] if sign < 0 else hi[axis]])
        w['leaf_brushes'].append(len(w['brushes']))
        w['brushes'].append([first, 6, 1, 1, 0])
    w['leaves'] = [[0, 0, 0, len(boxes)]]
    return w


def panel():
    return dict(kind=0, vertices=[[-1,-.5,0], [1,-.5,0], [1,.5,0], [-1,.5,0]],
                contact_known=1, contact_distance=0)


class Diagnosis(unittest.TestCase):
    def test_binary_rational_keeps_negative_distance(self):
        self.assertEqual(exact_distance([1,0,0], [-3004-2**-12,0,0], -3004), Fraction(-1,4096))

    def test_named_contact_and_opposite_brush_are_independent(self):
        w = world([([-4,-4,-4],[4,4,0]), ([-4,-4,0],[4,4,4])])
        a = inspect(w, panel(), [0,0,1])
        self.assertEqual(len(a['contacts']), 1)
        self.assertEqual([b['category'] for b in a['unresolved']], ['bsp-opposite-contact-boundary'])
        self.assertIsNone(a['unresolved'][0]['deep'])

    def test_edge_gap_and_real_overlap(self):
        a = inspect(world([([1,-4,-1],[4,4,4])]), panel(), [0,0,1])
        self.assertEqual(a['unresolved'][0]['category'], 'bsp-other-plane-boundary')
        self.assertIsNone(a['unresolved'][0]['deep'])
        self.assertFalse(inspect(world([([1.001,-4,-1],[4,4,4])]), panel(), [0,0,1])['unresolved'])
        a = inspect(world([([.5,-4,-1],[4,4,4])]), panel(), [0,0,1])
        self.assertEqual(a['unresolved'][0]['category'], 'bsp-penetration-witness')
        self.assertLess(a['unresolved'][0]['deep']['max_signed_distance'], 0)

    def test_exact_clipped_point_stays_on_oblique_source(self):
        vertices = [[0,0,0], [1,0,1], [0,1,1]]
        p = clip(vertices, [([1,0,0], .25), ([0,1,0], .25)])
        self.assertGreaterEqual(len(p), 3)
        self.assertTrue(all(exact_distance([-1,-1,1], v, 0) == 0 for v in p))
        self.assertTrue(all(v[0] <= Fraction(1,4) and v[1] <= Fraction(1,4) for v in p))

    def test_cylinder_low_is_actual_radius_not_vacuum_cube(self):
        s = dict(kind=1, centre=[0,0,.25], axis=[1,0,0], half_length=2, radius=.25)
        self.assertEqual(span(s, [0,0,1], 0), (0,.5))
        s['centre'][2] = .125
        self.assertEqual(span(s, [0,0,1], 0), (-.125,.375))


if __name__ == '__main__':
    unittest.main()
