"""Deterministic metric assertions and adversarial evaluation-input tests.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import itertools
import json
import math
from pathlib import Path
import tempfile
import unittest

import metrics as m
import adapters as a
import benchmark as b
from solver_view import TrainingView
from evaluation_view import EvaluationView
from prepare_blind import prepare, assignment
from test_blind_views import sample


def point(x=0., **kw): return dict(kind='point', position=[x, 0., 0.], **kw)


def photo(value, unit='candela', kind='PhysicallyAuthored', domain='reference'):
    return dict(value=value, unit=unit, kind=kind, domain=domain, rgb=[1., 1., 1.], primaries='Rec709Linear')


def target(i='one', w=1.):
    return dict(id=i, weight=w, domain='SourceLinearRelative', primaries='Rec709Linear',
                comparison_id='declared-linear-test-domain',
                lower=[1., 1., 1.], upper=[1., 1., 1.])


def prediction(i='one', rgb=None):
    return dict(id=i, domain='SourceLinearRelative', primaries='Rec709Linear', rgb=rgb or [1., 1., 1.],
                comparison_id='declared-linear-test-domain')


class Matching(unittest.TestCase):
    def test_permutation_and_unrelated_ids(self):
        t = [point(0, id='secret'), point(30, id='other')]
        for pp in itertools.permutations([point(1, id='anything'), point(29, id='else')]):
            report = m.detection(t, list(pp), a.policy(2.), 32.)
            self.assertEqual((report['true_positive'], report['false_positive'], report['false_negative']), (2, 0, 0))
            self.assertAlmostEqual(report['matches'][0]['position_metres'], 1/32)
            self.assertEqual(report['geometric_summary']['position_world']['value'], 1)

    def test_maximum_cardinality_not_greedy(self):
        # t0 could take either; t1 can only take p0. Greedy nearest loses a match.
        pairs = m.match([point(0), point(2)], [point(.9), point(-1)], a.policy(1.2))
        self.assertEqual(set(pairs), {(0, 1), (1, 0)})

    def test_hungarian_matches_brute_force(self):
        cost = [[4, 2, 1, 8], [2, 0, 5, 1], [3, 6, 2, 4]]
        got = sum(cost[i][j] for i, j in m.hungarian(cost))
        exact = min(sum(cost[i][j] for i, j in enumerate(cols)) for cols in itertools.permutations(range(4), 3))
        self.assertEqual(got, exact)

    def test_type_error_not_successful_detection(self):
        r = m.detection([point()], [dict(point(), kind='spot')], a.policy(), 32.)
        self.assertEqual(r['true_positive'], 0); self.assertEqual(r['confusion']['point->spot'], 1)

    def test_split_and_merge_no_extra_true_positives(self):
        split = m.detection([point()], [point(-1), point(1), point(2)], a.policy(), 32.)
        self.assertEqual(split['false_positive'], 2); self.assertEqual(len(split['split_neighbourhoods']), 1)
        merge = m.detection([point(-1), point(1)], [point()], a.policy(), 32.)
        self.assertEqual(merge['false_negative'], 1); self.assertEqual(len(merge['merge_neighbourhoods']), 1)

    def test_empty_and_transport_not_physical(self):
        empty = m.detection([], [], a.policy(), 32.)
        self.assertIsNone(empty['precision']['value']); self.assertIsNone(empty['recall']['value'])
        r = m.detection([point()], [dict(kind='transport', component='bounce')], a.policy(), 32.)
        self.assertEqual(r['recall']['value'], 0.); self.assertEqual(r['environment_and_transport']['predicted'], 1)

    def test_position_gate_and_typed_missing(self):
        self.assertEqual(len(m.match([point()], [point(9)], a.policy(8))), 0)
        self.assertEqual(len(m.match([point()], [dict(kind='point')], a.policy())), 0)
        with self.assertRaises(ValueError): m.detection([point()], [point(float('nan'))], a.policy(), 32.)

    def test_direction_and_cone(self):
        spot = dict(kind='spot', position=[0, 0, 0], direction=[0, 0, -1], direction_convention='propagation', cone_degrees=25)
        wrong = dict(spot, cone_degrees=30)
        self.assertFalse(m.match([spot], [wrong], a.policy()))
        self.assertAlmostEqual(m.angle([0, 0, 1], [1, 0, 0]), 90.)
        self.assertFalse(m.match([spot], [dict(spot, direction_convention='receiver_to_source')], a.policy()))
        with self.assertRaises(ValueError): m.angle([0, 0, 0], [1, 0, 0])

    def test_rectangle_axis_symmetries_and_roll(self):
        t = dict(kind='rectangle', position=[0, 0, 0], direction=[0, 0, -1], direction_convention='propagation',
                 dimensions=[10, 20], tangent=[1, 0, 0])
        p = dict(t, dimensions=[20, 10], tangent=[0, 1, 0])
        self.assertTrue(m.match([t], [p], a.policy()))
        self.assertFalse(m.match([t], [dict(t, tangent=[1, 1, 0])], a.policy()))
        tube = dict(kind='tube', position=[0, 0, 0], dimensions=[100, 2], direction=[1, 0, 0], direction_convention='axis')
        self.assertTrue(m.match([tube], [dict(tube, direction=[-1, 0, 0])], a.policy()))


class PhotometryAndFidelity(unittest.TestCase):
    def test_physical_vs_legacy_vs_unknown_scale(self):
        t, p = point(photometry=photo(10)), point(photometry=photo(12))
        r = m.photometry(t, p)
        self.assertAlmostEqual(r['relative_intensity']['value'], .2)
        self.assertEqual(r['absolute_intensity']['value'], 2.)
        t['photometry'] = photo(10, 'q3map units', 'UncalibratedRelative')
        p['photometry'] = photo(12, 'q3map units', 'UncalibratedRelative')
        self.assertEqual(m.photometry(t, p)['absolute_intensity']['status'], 'NotComparable')
        self.assertAlmostEqual(m.photometry(t, p)['relative_intensity']['value'], .2)
        p['photometry']['domain'] = 'unknown-scale'
        self.assertIsNone(m.photometry(t, p)['relative_intensity']['value'])

    def test_chromaticity_unknown_and_black(self):
        t, p = point(photometry=photo(10)), point(photometry=photo(20))
        self.assertEqual(m.photometry(t, p)['chromaticity']['value'], 0.)
        p['photometry']['primaries'] = 'Unknown'
        self.assertEqual(m.photometry(t, p)['chromaticity']['status'], 'NotComparable')
        p['photometry'] = dict(photo(20), rgb=[0, 0, 0])
        self.assertIsNone(m.photometry(t, p)['chromaticity']['value'])

    def test_area_weighting_not_sample_count(self):
        r = m.prediction_metrics([target('a', 1), target('b', 9)],
                                 [prediction('a'), prediction('b', [2, 2, 2])])
        self.assertAlmostEqual(r['rgb_interval_rmse']['value'], math.sqrt(.9))

    def test_partial_prediction_cannot_hide_coverage(self):
        r = m.prediction_metrics([target('a', 1), target('b', 9)], [prediction('a')])
        self.assertEqual(r['rgb_interval_rmse']['value'], 0.)
        self.assertEqual(r['prediction_coverage']['value'], .1)
        self.assertEqual(r['failures']['missing_prediction'], 1)

    def test_saturation_is_lower_bound_not_exact(self):
        t = dict(target(), lower=[1, 0, 0], upper=[None, 0, 0])
        r = m.prediction_metrics([t], [prediction(rgb=[100, 0, 0])])
        self.assertEqual(r['rgb_interval_rmse']['value'], 0.)
        self.assertEqual(r['censored_channels_scored'], 1)
        p = a.encoded_target(dict(id='one', area=1, raw=[255, 128, 0]))
        self.assertIsNone(p['upper'][0]); self.assertEqual(p['lower'][1], 127.5)

    def test_incomparable_domains_unknown_primaries(self):
        t = dict(target(), primaries='Unknown')
        r = m.prediction_metrics([t], [dict(prediction(), primaries='Unknown')])
        self.assertIsNone(r['rgb_interval_rmse']['value'])
        self.assertEqual(r['failures']['not_comparable'], 1)
        with self.assertRaises(ValueError): m.prediction_metrics([target(), target()], [])
        with self.assertRaises(ValueError): m.prediction_metrics([target()], [prediction(rgb=[float('inf'), 0, 0])])

    def test_scale_bounds_not_physical_calibration(self):
        r = m.scale_feasible_set([dict(prediction=2, target_interval=[3, 5]), dict(prediction=1, target_interval=[2, 4])])
        self.assertEqual(r['value'], [2, 2.5]); self.assertIn('not physical calibration', r['meaning'])

    def test_separate_grid_and_deluxe_meanings(self):
        r = m.directional_metrics([dict(truth=[0, 0, 1], prediction=[0, 1, 0], meaning='receiver_to_source', quantization_bound_degrees=2),
                                   dict(truth=None, prediction=None)])
        self.assertEqual(r['angular_error']['value'], 90.)
        self.assertEqual(r['above_encoding_bound']['value'], 88.)
        self.assertEqual(r['missing'], 1)


class ReadinessAndUncertainty(unittest.TestCase):
    def test_area_taxonomy_unknowns(self):
        r = m.readiness([dict(id='one', area=1, quality=dict(visibility=True, raw_rgb=True)),
                         dict(id='two', area=9, quality=dict(visibility=None, raw_rgb=True)),
                         dict(id='padding', area=None, quality=dict(mapped=False))])
        q = r['groups']['map']['qualities']['visibility']
        self.assertEqual(q['fraction_of_known_footprint']['value'], .1)
        self.assertEqual(q['unknown_predicate_area'], 9)
        self.assertEqual(r['map_area_coverage']['status'], 'NotComparable')
        self.assertEqual(r['samples_without_area'], 1)

    def test_duplicate_readiness_not_independent_area(self):
        row = dict(id='same', area=4)
        r = m.readiness([row, row]); self.assertEqual(r['known_footprint_area'], 4)
        self.assertEqual(r['duplicate_records'], 1)

    def test_padding_metadata_and_partial_census(self):
        row = sample(0, [512, 512, 512]); row.update(material=None, coverage=0, area=0, position=None, normal=None)
        self.assertEqual(a.quality_row(row, 'ab'*32)['material_style'], 'Unknown/0')
        with tempfile.TemporaryDirectory() as t:
            prefix = Path(t)/'partial'
            meta = dict(map_sha256='ab'*32, rows=2, derived_complete=1, grid_cells=0,
                        grid_present=0, grid_directions_quantized_uncalibrated=0, grid_assigned=0)
            prefix.with_suffix('.source.json').write_text(json.dumps(meta))
            prefix.with_suffix('.rows.jsonl').write_text(json.dumps(row)+'\n')
            r = a.dataset_readiness(prefix)
            self.assertTrue(r['all_pages_declared_complete'])
            self.assertFalse(r['derived_record_count_consistent']); self.assertFalse(r['complete_derived_stream'])

    def test_response_rank_and_condition(self):
        singular = m.information([[1, 1], [2, 2]], [1, 1])
        self.assertEqual(singular['rank']['value'], 1); self.assertIsNone(singular['condition']['value'])
        full = m.information([[1, 0], [0, 2]], [1, 1])
        self.assertEqual(full['rank']['value'], 2); self.assertAlmostEqual(full['condition']['value'], 2.)
        self.assertEqual(m.information([[1, 0], [0, 2]], [2, 1])['condition']['value'], 1.)

    def test_heuristic_not_calibrated_probability(self):
        r = m.probability_calibration([dict(score=.9, meaning='heuristic', label=1)])
        self.assertIsNone(r['brier']['value']); self.assertEqual(r['heuristic_scores_not_probabilities'], 1)

    def test_confident_wrong_worse_than_uncertain(self):
        def record(p): return dict(score=p, label=0, meaning='probability', calibration_protocol='fixture only')
        self.assertGreater(m.probability_calibration([record(.99)])['brier']['value'],
                           m.probability_calibration([record(.5)])['brier']['value'])
        correct = m.probability_calibration([record(0.)], independent_trials=True)
        self.assertEqual(correct['brier']['value'], 0.)
        self.assertGreater(correct['bins'][0]['wilson95'][1], 0.)
        self.assertIsNone(m.probability_calibration([record(0.)])['bins'][0]['wilson95'])

    def test_interval_coverage_and_unknown(self):
        r = m.interval_coverage([dict(truth=1, interval=[0, 2]), dict(truth=3, interval=[0, 2]), dict(truth=None)])
        self.assertEqual(r['coverage']['value'], .5); self.assertEqual(r['unavailable'], 1)
        with self.assertRaises(ValueError): m.interval_coverage([dict(truth=1, interval=[2, 0])])

    def test_joint_position_type_and_missing_hypotheses(self):
        r = m.joint_position_coverage([dict(truth=[0, 0, 0], estimate=[1, 0, 0], radius_world=2),
                                      dict(truth=[0, 0, 0], estimate=None)])
        self.assertEqual(r['joint_ball_coverage']['value'], 1.)
        self.assertEqual(r['resolved_fraction']['value'], .5)
        record = dict(truth='point', probabilities=dict(point=.8, spot=.2), meaning='probability', calibration_protocol='fixture')
        self.assertAlmostEqual(m.type_calibration([record])['multiclass_brier']['value'], .08)
        with self.assertRaises(ValueError): m.type_calibration([dict(record, probabilities=dict(point=.8))])

    def test_paired_ablation_cannot_change_splits(self):
        base = dict(id='all', dataset_hash='x', split_hash='y', policy_hash='z', seed=1, resource_budget=20,
            features=dict(lightgrid=True, materials=True, visibility=True, shadows=True, indirect=True, emissive_priors=True), metrics={})
        off = copy.deepcopy(base); off['id'] = 'no-shadow'; off['features']['shadows'] = False
        self.assertEqual(m.ablation_pairs([base, off])['pairs'][0]['changed'], 'shadows')
        off['split_hash'] = 'other'
        self.assertEqual(m.ablation_pairs([base, off])['status'], 'Unavailable')

    def test_profile_ambiguity_and_unfair_budget(self):
        rows = [dict(id=k, validation_error=v, domain='same', parameter_budget=1, partition_hash='same')
                for k, v in [('isq', 1.), ('linear', 1.01)]]
        r = m.profile_evidence(rows, .02)
        self.assertEqual(r['viable_count'], 2); self.assertEqual(r['known_profile_recovered']['status'], 'NotComparable')
        r = m.profile_evidence(rows, .001, known_profile='linear', selected='isq', asserted_unique=True)
        self.assertTrue(r['false_unique_claim']['value']); self.assertFalse(r['truth_in_viable']['value'])
        with self.assertRaises(ValueError): m.profile_evidence([rows[0], dict(rows[1], parameter_budget=2)], .1)

    def test_visibility_unknown_not_transparency(self):
        r = m.visibility_metrics([dict(truth='Blocked', predicted='Visible', predicted_energy=2),
                                  dict(truth='Unknown', predicted='Visible')])
        self.assertEqual(r['wall_leaked_energy']['value'], 2.)
        self.assertEqual(r['accuracy_on_known']['value'], 0.); self.assertEqual(r['unknown_count'], 1)

    def test_shadow_edge_width_attribution_and_placement(self):
        r = m.shadow_metrics([dict(edge_truth=1, edge_predicted=4, width_truth=2, width_predicted=3, association_correct=False)])
        self.assertEqual(r['edge_error']['value'], 3); self.assertEqual(r['penumbra_width_error']['value'], 1)
        self.assertEqual(r['false_association_rate']['value'], 1)
        e = m.attribution([dict(truth=dict(direct=5, bounce=1), prediction=dict(direct=0, bounce=6), included_roots=['a', 'a'])])
        self.assertEqual(e['component_absolute_error']['direct']['value'], 5)
        self.assertEqual(e['duplicate_root_references'], 1)
        self.assertEqual(m.placement([dict(space='Solid'), dict(space='Unknown')])['certified_solid_sources'], 1)


class RunnerAndBoundary(unittest.TestCase):
    def test_bad_rgb_perfect_models_not_source_success(self):
        reports = {c['id']: b.evaluate(c)['sections'] for c in b.example_cases()}
        many = reports['many-lights-perfect-rgb']
        self.assertEqual(many['prediction_accuracy']['surface']['rgb_interval_rmse']['value'], 0.)
        self.assertEqual(many['source_accuracy']['false_positive'], 10)
        self.assertAlmostEqual(many['source_accuracy']['precision']['value'], 1/3)
        self.assertEqual(reports['indirect-only-perfect-rgb']['source_accuracy']['recall']['value'], 0.)
        self.assertEqual(reports['candidates-not-recovered']['source_accuracy']['true_positive'], 0)
        self.assertEqual(reports['valid-empty-asset-no-recovery']['source_accuracy']['true_positive'], 0)
        self.assertEqual(reports['one-easy-observation-only']['prediction_accuracy']['surface']['prediction_coverage']['value'], .1)

    def test_reproducible_reports(self):
        self.assertEqual(b.evaluate(b.example_cases()[0]), b.evaluate(b.example_cases()[0]))

    def test_coincident_sources_remain_uncertain(self):
        case = b.example_cases()[0]
        case['truth'] = [point(identifiability='ambiguous'), point(identifiability='ambiguous')]
        claim = dict(score=.99, meaning='probability', calibration_protocol='deliberate ambiguous claim')
        case['predictions'] = [point(existence=claim), point(existence=claim), point(existence=claim)]
        result = b.evaluate(case)['sections']
        self.assertEqual(result['source_accuracy']['ambiguous_truth_count'], 2)
        self.assertIsNone(result['uncertainty']['existence']['brier']['value'])

    def test_unknown_material_profile_scale_no_fake_photometry(self):
        t = point(photometry=photo(1, 'relative', 'UncalibratedRelative', 'unknown-a'))
        p = point(photometry=photo(999, 'relative', 'UncalibratedRelative', 'unknown-b'))
        result = m.detection([t], [p], a.policy(), 32.)
        self.assertEqual(result['true_positive'], 1)
        self.assertEqual(result['matches'][0]['photometry']['relative_intensity']['status'], 'NotComparable')

    def test_prediction_comparison_id_required(self):
        self.assertIsNone(m.prediction_metrics([target()], [dict(prediction(), comparison_id='other')])['rgb_interval_rmse']['value'])
        t = target(); t.pop('comparison_id')
        self.assertIsNone(m.prediction_metrics([t], [prediction()])['rgb_interval_rmse']['value'])

    def test_shadow35_diagnostic_not_global_rmse(self):
        r = a.boundary_diagnostic(a.HERE.parent/'shadow_footprint/results/boundary.csv')
        self.assertEqual(r['boundary_texels'], 35); self.assertEqual(r['channels'], 105)
        self.assertEqual(r['max_byte_error'], 28)
        self.assertEqual(r['source_epsilon_replay']['rgb_interval_rmse']['value'], 0.)
        self.assertGreater(r['old_analytic']['rgb_interval_rmse']['value'], 0.)

    def test_frozen_regression_missing_not_zero(self):
        previous = dict(x=m.metric(1)); rules = [dict(path='/x', direction='lower', allowed_delta=.1)]
        self.assertEqual(b.compare_regression(dict(x=m.metric(2)), previous, rules)['status'], 'Failed')
        self.assertEqual(b.compare_regression(dict(x=m.metric()), previous, rules)['status'], 'Failed')
        self.assertEqual(b.compare_regression(dict(x=m.metric(.5)), previous, rules)['status'], 'Passed')

    def test_solver_cannot_override_truth_or_map(self):
        case = b.example_cases()[0]
        with self.assertRaises(ValueError): b.apply_prediction(case, dict(map_sha256='other', outputs={}))
        with self.assertRaises(ValueError): b.apply_prediction(case, dict(map_sha256=case['provenance']['map_sha256'], outputs=dict(truth=[])))

    def test_blind_api_no_answers_targets_or_recipe(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); prefix = root/'f'
            prefix.with_suffix('.source.json').write_text(json.dumps(dict(map_sha256='ab'*32,
                pages=[dict(image=0, width=40, height=1, missing=0)], derived_complete=1)))
            prefix.with_suffix('.public.jkobs').write_bytes(b'Geometry stub; real compiled public-check is separate')
            prefix.with_suffix('.rows.jsonl').write_text(''.join(json.dumps(sample(i, [512+1024*i, 512, 512]))+'\n' for i in range(40)))
            prepare(prefix, root/'published', seed=7)
            directory = root/'published/inputs'
            report = a.audit_training(directory, 'ab'*32); self.assertGreater(report['train'], 0)
            view = TrainingView(directory, 'ab'*32)
            for hidden in ('heldout', 'validation', 'truth', 'lights', 'recipe', 'compiler'):
                self.assertFalse(hasattr(view, hidden))
            with self.assertRaises(TypeError): list(view.observations('heldout'))
            with self.assertRaises(ValueError): TrainingView(root/'published/evaluator', 'ab'*32)
            evaluator = EvaluationView(root/'published/evaluator', 'ab'*32)
            with self.assertRaises(PermissionError): list(evaluator.observations('heldout'))
            with self.assertRaises(ValueError): a.audit_training(directory, 'cd'*32)
            (directory/'answer.json').write_text('{"hidden_position":[1,2,3]}')
            with self.assertRaises(ValueError): a.audit_training(directory, 'ab'*32)
            (directory/'answer.json').unlink()
            manifest_path = directory/'manifest.json'; manifest = json.loads(manifest_path.read_text())
            manifest['compiler_recipe'] = 'hidden'
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): a.audit_training(directory, 'ab'*32)

    def test_filtered_neighbour_gap(self):
        for x in range(500, 1600):
            a0 = assignment(sample(0, [x, 512, 512]), 1024, 8, 7)
            a1 = assignment(sample(1, [x+4, 512, 512]), 1024, 8, 7)
            self.assertTrue(a0 is None or a1 is None or a0 == a1)


if __name__ == '__main__': unittest.main()
