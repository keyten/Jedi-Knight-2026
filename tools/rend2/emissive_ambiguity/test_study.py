"""Deterministic false-source, transport, blindness and abstention controls.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import replace
import json
import math
from pathlib import Path
import unittest

import study as s


class AmbiguityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = s.run()

    def test_false_sources_and_recall_have_explicit_denominators(self):
        metrics = self.result['metrics']
        self.assertEqual(metrics['brightness']['false_sources'], 9)
        self.assertEqual(metrics['glow_stage']['false_sources'], 3)
        self.assertEqual(metrics['geometry_and_brightness']['false_sources'], 6)
        conservative = metrics['conservative']
        self.assertEqual(conservative['nonemitting_proposals'], 9)
        self.assertEqual(conservative['false_sources'], 0)
        self.assertEqual(conservative['emitting_proposals'], 3)
        # The dim source is deliberately NOT rescued by adjusting thresholds.
        self.assertEqual(conservative['true_sources_retained'], 2)
        self.assertAlmostEqual(conservative['recall'], 2 / 3)

    def test_red_reflector_fools_a_free_direct_emitter_fit(self):
        rows = [r for r in self.result['rows'] if r['scene'] == 'red_reflected_wall']
        for row in rows:
            result = row['result']
            metrics = result['metrics']
            self.assertEqual(result['decision'], 'PassiveTransportPlausible')
            self.assertGreater(metrics['direct_plus_emitter_amplitude'][0], 2.)
            self.assertLess(metrics['direct_plus_emitter_rmse_bytes'], 1e-10)
            self.assertEqual(metrics['passive_rmse_bytes'], 0.)
            self.assertEqual(result['heldout_selected_rmse_bytes'], 0.)

    def test_aperture_is_not_an_emitting_polygon(self):
        f = s.fixture('lit_doorway')
        result = s.evaluate(f)
        self.assertEqual(result['decision'], 'ApertureOrUnsupportedSurface')
        self.assertFalse(result['surface_supported'])
        # Optical support through the hole is not occupied emitting support.
        self.assertEqual(result['metrics']['candidate_nonzero_response_receivers'], 6)
        scene = f.evidence.scene
        self.assertTrue(s.visible(scene, (-2., 0., 2.), (1., 0., 0.)))
        self.assertFalse(s.visible(scene, (-2., 2., 2.), (1., 2., 0.)))
        self.assertGreater(max(v for row in s.passive(scene, f.heldout.receivers, 8) for v in row), 0.)

    def test_glow_provenance_survives_without_promoting_a_lamp(self):
        f = s.fixture('decorative_glow')
        before = f.evidence.scene.provenance
        result = s.diagnose(f.evidence)
        self.assertTrue(result['glow_stage'])
        self.assertFalse(result['bake_emission_hint'])
        self.assertEqual(result['decision'], 'PassiveTransportPlausible')
        self.assertEqual(result['evidence_provenance'], before)
        self.assertEqual(f.evidence.scene.provenance, before)

    def test_emitter_can_also_reflect(self):
        f = s.fixture('emissive_panel')
        bounced = s.passive(f.evidence.scene, f.evidence.training.receivers, 8)
        self.assertGreater(max(v for row in bounced for v in row), 0.)
        result = s.evaluate(f)
        self.assertEqual(result['decision'], 'ConditionalEmitterCandidate')
        self.assertFalse(result['exact_historical_source'])
        self.assertLess(result['heldout_selected_rmse_bytes'], .1)

    def test_dim_source_abstention_is_reported_as_a_miss(self):
        f = s.fixture('emissive_panel', .5)
        self.assertTrue(f.truth_candidate_emits)
        result = s.diagnose(f.evidence)
        self.assertEqual(result['decision'], 'AmbiguousSourceOrTransport')
        self.assertGreater(result['metrics']['passive_rmse_bytes'], .75)

    def test_unknowns_never_become_zero_or_empty_space(self):
        controls = self.result['abstention_controls']
        for field in ('geometry_known', 'material_known', 'root_known', 'encoding_known'):
            self.assertEqual(controls[field]['decision'], 'AmbiguousSourceOrTransport')
            self.assertIsNone(controls[field]['metrics'])
            self.assertIsNone(controls[field]['fitted_additional_emission'])
        f = s.fixture('red_reflected_wall')
        unknown = replace(f.evidence.scene, geometry_known=False)
        with self.assertRaises(s.Unavailable):
            s.visible(unknown, (0., 0., 2.), (1., 0., 0.))
        with self.assertRaises(s.Unavailable):
            s.predict(unknown, f.heldout.receivers, 8)

    def test_missing_emission_hint_is_not_invented_from_brightness(self):
        result = self.result['abstention_controls']['bake_emission_hint']
        self.assertEqual(result['decision'], 'AmbiguousSourceOrTransport')
        self.assertLess(result['metrics']['passive_plus_emitter_rmse_bytes'], 1e-10)

    def test_training_does_not_read_heldout_answers_or_directions(self):
        f = s.fixture('emissive_panel')
        before = s.evaluate(f)
        mutated = replace(f, heldout=replace(f.heldout, rgb8=((0, 0, 0),) * 6),
                          truth_candidate_emits=False, truth_emission=(1e6,) * 3,
                          ideal_grid_directions=(None,) * 6)
        after = s.evaluate(mutated)
        for field in ('decision', 'reasons', 'metrics', 'fitted_additional_emission'):
            self.assertEqual(before[field], after[field])
        self.assertGreater(after['heldout_selected_rmse_bytes'], before['heldout_selected_rmse_bytes'])

    def test_appearance_is_not_used_as_calibrated_irradiance(self):
        f = s.fixture('red_reflected_wall')
        result = s.diagnose(f.evidence)
        changed = s.diagnose(replace(f.evidence, appearance_rgb8=(255, 255, 255)))
        self.assertEqual(result['decision'], changed['decision'])
        self.assertEqual(result['metrics'], changed['metrics'])
        self.assertEqual(result['fitted_additional_emission'], changed['fitted_additional_emission'])

    def test_spatial_split_and_receiver_normals(self):
        train, held = s.receiver_sets()
        self.assertGreater(min(math.dist(a.position, b.position) for a in train for b in held), .2)
        self.assertGreater(len({r.normal for r in train + held}), 1)
        for receiver in train + held:
            self.assertAlmostEqual(s.dot(receiver.normal, receiver.normal), 1.)

    def test_saturation_is_one_sided_and_not_exact_linear_light(self):
        self.assertTrue(math.isinf(s.interval(255)[1]))
        self.assertEqual(s.distance(1e6, 255), 0.)
        self.assertEqual(s.interval(0)[0], 0.)
        obs = s.Observations((s.Receiver((1., 0., 0.)),), ((255, 255, 255),))
        fitted = s.fit_emission((s.ZERO,), (1.,), obs)
        for value in fitted:
            self.assertAlmostEqual(value, 254.5 / s.POLICY['encoded_gain'])
        for value in (-1., float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                s.encode(value)
        with self.assertRaises(ValueError):
            s.interval(256)

    def test_equal_outgoing_reflection_and_emission_are_indistinguishable(self):
        result = self.result['equivalent_secondary']
        self.assertTrue(result['encoded_equal'])
        self.assertLess(result['maximum_receiver_difference'], 1e-14)
        self.assertLess(result['maximum_direction_difference'], 1e-14)
        self.assertFalse(result['new_origin_information_from_direction'])

    def test_reference_is_refined_independently_of_fitting(self):
        f = s.fixture('red_reflected_wall')
        scene, receivers = f.evidence.scene, f.heldout.receivers
        reference = s.passive(scene, receivers, 24)
        errors = []
        for order in (4, 8, 16):
            predicted = s.passive(scene, receivers, order)
            errors.append(max(abs(a - b) * s.POLICY['encoded_gain']
                              for x, y in zip(predicted, reference) for a, b in zip(x, y)))
        self.assertGreater(errors[0], errors[1])
        self.assertGreater(errors[1], errors[2])
        self.assertLess(errors[1], .01)

    def test_frozen_witness_and_repeat(self):
        second = s.run()
        self.assertEqual(self.result, second)
        witness = Path(__file__).parent / 'results.json'
        self.assertTrue(witness.is_file(), 'Checked ASR06 result witness is required')
        self.assertEqual(json.loads(witness.read_text(encoding='utf-8')),
                         json.loads(json.dumps(second)))


if __name__ == '__main__':
    unittest.main()
