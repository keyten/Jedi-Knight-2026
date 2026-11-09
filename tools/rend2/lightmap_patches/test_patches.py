"""Deterministic geometry, seam, censoring, negative and ambiguity controls."""
import copy
import json
import math
import unittest

from .extract import POLICY, combine, dot, extract, mul, stable
from .fixtures import cases


def field(surface=1, frame='floor', value=None):
    rows = []
    for j, y in enumerate(range(-128, 129, 8)):
        for i, x in enumerate(range(-128, 129, 8)):
            p, n = ([x, y, 0], [0, 0, 1]) if frame == 'floor' else ([x, -96, y+96], [0, 1, 0])
            source = [16, 8, 96]
            d = [s-a for s, a in zip(source, p)]
            q = 160*96**2*max(0, dot(d, n))/max(1, dot(d, d))**1.5
            rgb = [round(8+q*c) for c in (1, .5, .25)] if value is None else value(x, y)
            rows.append(dict(id=stable([surface, i, j]), image=0, page=0, surface=surface,
                             material='uniform', slot=0, style=0, x=i, y=j, coverage=2,
                             issues=0, area=64, position=p, normal=n, fit_candidate=True,
                             raw=rgb, encoding=0, channel_flags=[1, 1, 1]))
    return rows


class KnownGeometry:
    def __init__(self, point='free', ray='clear'):
        self.space, self.visibility = point, ray

    def point(self, _):
        return self.space

    def ray(self, _a, _b):
        return self.visibility


class PatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = field()
        cls.report = extract(cls.rows)

    def test_coloured_localized_maximum_and_contours(self):
        self.assertEqual(len(self.report['patches']), 1)
        p = self.report['patches'][0]
        self.assertLess(math.dist(p['maximum_world'], [16, 8, 0]), 3)
        self.assertEqual(p['scales_texels'], [1, 2, 4])
        self.assertGreater(len(p['receiver_ids']), 30)
        self.assertEqual(p['constraint']['state'], 'Conditional')
        self.assertLess(math.dist(p['constraint']['foot_world'], [16, 8, 0]), 3)
        self.assertGreaterEqual(sum(c['radial_support'] for c in p['isophotes']), 2)
        self.assertIsNone(p['constraint']['depth_world'])
        self.assertIsNone(p['constraint']['emitter_orientation'])
        self.assertFalse(p['physical_source_certified'])

    def test_reordered_input_is_identical(self):
        self.assertEqual(self.report, extract(list(reversed(self.rows))))

    def test_flat_black_ramp_and_material_seam_negatives(self):
        for value in (lambda x, y: [0]*3, lambda x, y: [60]*3,
                      lambda x, y: [x//2+80]*3):
            self.assertEqual(extract(field(value=value))['patches'], [])
        rows = field(value=lambda x, y: [180 if x > 0 else 40]*3)
        for r in rows:
            r['material'] = 'right' if r['position'][0] > 0 else 'left'
        self.assertEqual(extract(rows)['patches'], [])

    def test_separate_chart_and_style_never_smooth(self):
        rows = field(value=lambda x, y: [180 if x > 0 else 40]*3)
        for dimension in ('surface', 'image', 'page', 'slot', 'style'):
            edited = copy.deepcopy(rows)
            for r in edited:
                r[dimension] += int(r['position'][0] > 0)
            self.assertEqual(extract(edited)['patches'], [], dimension)

    def test_saturation_and_missing_mapping_withhold_constraints(self):
        rows = copy.deepcopy(self.rows)
        for r in rows:
            r['raw'] = [min(255, c*3) for c in r['raw']]
        report = extract(rows)
        self.assertTrue(report['patches'])
        self.assertTrue(all(p['constraint']['state'] == 'Unavailable' for p in report['patches']))
        for bit in (1, 2, 3, 8, 9, 10, 11, 12, 13):
            report = extract([dict(r, issues=1 << bit) for r in self.rows])
            self.assertEqual(report['patches'], [], bit)
            self.assertEqual(report['rejected']['ambiguous_or_unsupported_receiver'], len(self.rows))

    def test_holes_are_not_interpolated(self):
        rows = [r for r in self.rows if r['x'] != 18]
        report = extract(rows)
        self.assertGreaterEqual(report['components'], 2)
        identities = {r['id']: r for r in rows}
        for p in report['patches']:
            xs = [identities[i]['x'] for i in p['receiver_ids']]
            self.assertFalse(min(xs) < 18 < max(xs))

    def test_unknown_geometry_and_independent_normals(self):
        patches = extract(self.rows+field(2, 'wall'))['patches']
        self.assertEqual(len(patches), 2)
        unknown = combine(patches)
        self.assertEqual(len(unknown), 1)
        self.assertEqual(unknown[0]['state'], 'UnknownGeometry')
        self.assertLess(math.dist(unknown[0]['position_world'], [16, 8, 96]), 4)
        self.assertEqual(combine(patches, KnownGeometry())[0]['state'], 'ConditionalLocation')
        self.assertEqual(combine(patches, KnownGeometry('solid'))[0]['state'], 'RejectedGeometry')
        self.assertEqual(combine(patches, KnownGeometry(ray='blocked'))[0]['state'], 'RejectedGeometry')
        self.assertEqual(combine(patches, KnownGeometry(ray='unknown'))[0]['state'], 'UnknownGeometry')
        self.assertEqual(combine(patches[:1]), [])

    def test_parallel_and_incompatible_colours_do_not_combine(self):
        patches = copy.deepcopy(self.report['patches']*2)
        patches[1]['id'] = 'second'
        patches[1]['chart']['surface'] = 2
        self.assertEqual(combine(patches), [])
        patches = extract(self.rows+field(2, 'wall'))['patches']
        patches[1]['excess_colour_direction'] = [0, 0, 1]
        self.assertEqual(combine(patches), [])

    def test_oblique_world_frame_and_large_offset(self):
        rows = copy.deepcopy(self.rows)
        for r in rows:
            x, y, z = r['position']
            r['position'] = [100000+x, 200000+y*.8-z*.6, 300000+y*.6+z*.8]
            r['normal'] = [0, -.6, .8]
        p = extract(rows)['patches'][0]
        self.assertLess(math.dist(p['constraint']['foot_world'], [100016, 200006.4, 300004.8]), 3)

    def test_transport_and_monotone_transfer_remain_conditional(self):
        rows = [dict(r, raw=[round(255*(c/255)**.6) for c in r['raw']]) for r in self.rows]
        p = extract(rows)['patches'][0]
        self.assertEqual(p['constraint']['state'], 'Conditional')
        self.assertLess(math.dist(p['constraint']['foot_world'], [16, 8, 0]), 4)
        self.assertIsNone(p['constraint']['depth_world'])
        # The same observed patch could be reflected light. No photometric test
        # is allowed to certify a primary historical lamp from these rows alone.
        self.assertTrue(any('transport' in s for s in p['uncertainty']))

    def test_budgets_invalid_values_and_duplicates_fail_explicitly(self):
        for edits in (dict(max_rows=4), dict(max_work=8), dict(max_chart_rows=4)):
            with self.assertRaisesRegex(ValueError, 'limit'):
                extract(self.rows, dict(POLICY, **edits))
        with self.assertRaisesRegex(ValueError, 'duplicate receiver'):
            extract(self.rows+self.rows[:1])
        with self.assertRaisesRegex(ValueError, 'Nonfinite'):
            extract([dict(self.rows[0], position=[float('nan'), 0, 0])])
        with self.assertRaisesRegex(ValueError, 'RGB8'):
            extract([dict(self.rows[0], raw=[-1, 20, 40])])

    def test_multiple_maxima_have_disjoint_connected_support(self):
        fixture = next(c for c in cases() if c['name'] == 'multi')
        # Also exercise equal-colour lights; RGB cannot perform the separation.
        for r in fixture['rows']:
            strength = max(r['raw'])
            r['raw'] = [strength]*3
        patches = extract(fixture['rows'])['patches']
        self.assertEqual(len(patches), 2)
        self.assertFalse(set(patches[0]['receiver_ids']) & set(patches[1]['receiver_ids']))

    def test_hard_boundary_is_apparent_not_certified_shadow(self):
        rows = [dict(r, raw=[0, 0, 0] if r['position'][0] > 0 else r['raw']) for r in self.rows]
        patches = extract(rows)['patches']
        self.assertTrue(patches)
        self.assertTrue(any(p['apparent_boundaries'] for p in patches))
        self.assertTrue(all(p['constraint']['state'] == 'Unavailable' for p in patches))
        self.assertTrue(all('unresolved' in e['interpretation'] for p in patches for e in p['apparent_boundaries']))

    def test_shading_normal_mismatch_withholds_position(self):
        rows = [dict(r, shading_normal=[.6, 0, .8]) for r in self.rows]
        p = extract(rows)['patches'][0]
        self.assertEqual(p['constraint']['state'], 'Unavailable')
        self.assertIn('Shading and geometric normals disagree', p['uncertainty'])

    def test_output_has_finite_serializable_diagnostic_contract(self):
        decoded = json.loads(json.dumps(self.report, allow_nan=False))
        self.assertEqual(decoded['schema'], 'org.openjk.lightmap-patches.v1')
        self.assertFalse(decoded['canonical_storage_modified'])
        for p in decoded['patches']:
            self.assertEqual(p['colour_domain'], 'OriginalEncodedRGB8_NoIrradianceCalibration')
            self.assertIn('not statistical confidence', p['constraint']['uncertainty_meaning'])


if __name__ == '__main__':
    unittest.main()
