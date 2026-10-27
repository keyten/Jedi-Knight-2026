"""Dependency-free guards on attribution, energy, held-out and optical semantics.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import math
from pathlib import Path
import unittest

from transport import Edge, Transfer, solve, nnls
import experiment as ex


class TransportTests(unittest.TestCase):
    def test_invalid_data_and_budget(self):
        for areas,edges in (((0.,1.),()),((1.,1.),(Edge(0,1,-.1,.2),)),
                            ((1.,1.),(Edge(0,1,.8,1.1),)),
                            ((1.,1.),(Edge(0,1,.2,.2),Edge(0,1,.2,.2))),
                            ((1.,1.,1.),(Edge(0,1,.6,.6),Edge(0,2,.6,.6)))):
            with self.assertRaises(ValueError): Transfer(areas,edges,'bad')
        t=Transfer((1.,1.),(Edge(0,1,.2,.2),),'valid')
        for value in (float('nan'),float('inf'),-.1):
            with self.assertRaises(ValueError): t.apply([[value]*3,[0.]*3],[[.5]*3,[.5]*3])
        with self.assertRaises(ValueError): t.apply([[1.]*3,[0.]*3],[[1.1]*3,[.5]*3])
        with self.assertRaises(ValueError): t.bounces([[1.]*3,[0.]*3],[[.5]*3,[.5]*3],9)

    def test_power_and_reciprocity(self):
        data=ex.network(); t=data['transfer']; edge={(e.sender,e.receiver):e.upper for e in t.edges}
        for (i,j),value in edge.items(): self.assertAlmostEqual(t.areas[i]*value,t.areas[j]*edge[j,i],places=14)
        low,high=t.apply(data['D'],data['reflectance'])
        incoming=sum(a*sum(E) for a,E in zip(t.areas,data['D']))
        outgoing=sum(a*sum(E) for a,E in zip(t.areas,high))
        self.assertLessEqual(outgoing,incoming)
        self.assertEqual(low,high)

    def test_two_bounces(self):
        result=ex.bounce_orders(); layers=result['layers_lux']
        self.assertAlmostEqual(layers[0][1][0],1.)
        self.assertAlmostEqual(layers[1][0][0],.1)
        self.assertLess(result['tail_power_after_four_bound_lumens'],.00012)

    def test_bounds_are_not_probabilities_and_smoothness_leaks(self):
        result=ex.uncertainty_and_leakage(); bounds=result['bounded_material']
        self.assertEqual(bounds['heldout_channels'],bounds['channels_enclosed'])
        self.assertIsNone(result['unknown_profile_posterior'])
        self.assertGreater(result['thin_wall']['naive_smooth_dark_side'],.3)
        self.assertEqual(result['thin_wall']['visibility_constrained_dark_side'],0.)

    def test_uncertain_transport_encloses_truth(self):
        d=ex.censoring(); self.assertEqual(d['unknown_transport_lower'],[0.]*3)
        for value in d['unknown_transport_upper']: self.assertAlmostEqual(value,.3)
        self.assertIsNone(d['intervals'][-1][1]); self.assertGreater(d['point_value_rmse'],.4)

    def test_small_fit_and_singular_not_fabricated(self):
        self.assertAlmostEqual(nnls([[1.,2.]],[3.,6.])[0],3.)
        with self.assertRaises(ValueError): solve([[1.,1.],[1.,1.]],[1.,1.])

    def test_emitter_collapse_and_heldout(self):
        result=ex.emitter_suppression(); rows={r['model']:r for r in result['rows']}
        self.assertEqual(rows['free_per_sample']['cd'],0.)
        self.assertEqual(rows['free_per_sample']['train_rmse_lux'],0.)
        self.assertGreater(rows['free_per_sample']['heldout_rmse_lux'],1.)
        self.assertFalse(rows['smooth_plus_sparse_sources']['source_recalled'])
        self.assertTrue(rows['smooth_no_source_penalty']['source_recalled'])
        self.assertAlmostEqual(rows['smooth_no_source_penalty']['cd'],3.,delta=.002)
        self.assertAlmostEqual(rows['transport_energy_coupled']['cd'],3.)
        self.assertLess(rows['transport_energy_coupled']['heldout_rmse_lux'],1e-12)

    def test_blind_spatial_split(self):
        d=ex.network(); train=ex.indices(d,'train'); held=ex.indices(d,'heldout')
        self.assertFalse(set(train)&set(held)); self.assertTrue(ex.indices(d,'gap'))
        # Toy receiver footprint is 0.2 m wide. 0.6 m centre separation leaves
        # >=0.4 m support gap; deliberately do not reuse RGB between views.
        separation=min(math.dist(d['positions'][i],d['positions'][j]) for i in train for j in held)
        self.assertGreaterEqual(separation,.6-1e-12)
        target=ex.combine(d['D'],d['G'],3.)
        before=ex.constrained(d,target,train)[0]
        for i in held: target[i]=[1e9]*3
        self.assertEqual(ex.constrained(d,target,train)[0],before)

    def test_wall_door_mask_missing_sky(self):
        d=ex.geometry_cases()
        self.assertEqual(d['doorway']['direct_visibility'],(0.,0.))
        self.assertEqual(d['doorway']['possible_one_bounce_path'],[1.,1.])
        self.assertEqual(d['sealed_wall']['possible_one_bounce_path'],[0.,0.])
        self.assertEqual(d['unknown_mask']['direct_visibility'],(0.,1.))
        self.assertEqual(d['unbounded_missing']['direct_visibility'],(0.,0.))
        self.assertEqual(d['unbounded_missing']['possible_one_bounce_path'],[0.,1.])
        self.assertEqual(d['sky_only']['local_physical_emitters'],0)

    def test_material_ambiguity(self):
        result=ex.reflectance_ambiguity()
        self.assertEqual(result['incident_ratio_max_min'],4.)
        for row in result['alternatives']: self.assertAlmostEqual(row['outgoing_cd_m2'],result['observed_outgoing_cd_m2'])

    def test_sparse_censored_source_interval(self):
        result=ex.sparse_clipped_fit(); lo,hi=result['feasible_cd_interval']
        self.assertLessEqual(lo,3.); self.assertGreaterEqual(hi,3.)
        self.assertGreater(result['saturated_training_samples'],0)
        self.assertLess(result['treating_clipped_as_exact_cd'],2.9)
        self.assertLess(result['heldout_interval_rmse_bytes'],.5)
        self.assertEqual(result['heldout_intervals_covered'],result['heldout_samples'])
        self.assertIsNone(result['all_clipped_intensity_upper'])

    def test_compiler_control_witness(self):
        # Validates the checked numerical evidence; running q3map2 is a separate
        # explicit command, never silently replaced with synthetic observations.
        path=Path(__file__).parent/'results/compiler.json'
        if not path.exists(): self.skipTest('Compiler witness unavailable; no claimed compiler execution')
        d=json.loads(path.read_text()); self.assertEqual(len(d['frozen']['cases']),22)
        self.assertAlmostEqual(d['frozen']['white_bounce']['floor_mean_delta_rgb8'][0],5.331428571428572)
        self.assertFalse(d['frozen']['white_bouncegrid']['grid_identical'])
        c=d['controls']; self.assertEqual(c['wall_leak-white180']['right']['mean_delta_rgb8'],[0.]*3)
        self.assertGreater(c['door_occlusion-white180']['right']['mean_delta_rgb8'][0],3.)
        colour=c['bounce_colour-white180']['floor_mean_delta_rgb8']; self.assertGreater(colour[0],colour[2])
        self.assertTrue(d['material_control']['direct_lightmap_identical'])
        self.assertEqual(d['UnknownProfile']['inverse_square']['nuisance_fill_range'],[0,14])

    def test_wrong_profile_is_not_automatically_gi(self):
        result=ex.stability()
        for row in result['rows']:
            if row['profile']=='inverse_square' and row['material']=='known':
                self.assertAlmostEqual(row['fitted_cd_per_channel'],3.)
                self.assertLess(row['heldout_rmse_lux'],1e-12)
        false=result['zero_bounce_false_attribution']
        self.assertEqual(false['wrong_profile_independent_gi_gain'],0.)
        self.assertEqual(false['free_residual_direct_gain'],0.)
        self.assertGreater(false['free_residual_mean_lux'],0.)

    def test_quadrature_convergence_and_near_field(self):
        rows=ex.finite_reflector_reference()
        self.assertLess(max(r['order24_difference'] for r in rows),2e-5)
        self.assertLess(rows[0]['midpoint_relative_error'],.005)
        self.assertGreater(max(r['midpoint_relative_error'] for r in rows),1.)

    def test_repeat_witness(self):
        # Excludes timings. Source and method identities are part of the result.
        first=ex.run(); self.assertEqual(first,ex.run())
        reference=Path(__file__).parent/'results/analytic.json'
        if reference.exists(): self.assertEqual(json.loads(reference.read_text()),json.loads(json.dumps(first)))


if __name__=='__main__': unittest.main()
