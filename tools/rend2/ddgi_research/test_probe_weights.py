"""Deterministic guards on the approximation/geometry boundary. Standard library.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import math
from pathlib import Path
import unittest
import probe_weights as p


class ProbeWeights(unittest.TestCase):
    def test_analytic_inequality_and_nonprobability_cube(self):
        result=p.equal_moments(); a,b=result['moments']; self.assertEqual(a,b)
        for r in result['queries']:
            self.assertTrue(all(v<=r['upper']+1e-12 for v in r['true_angular_survival']))
        last=result['queries'][-1]
        self.assertLess(last['cubed'],last['true_angular_survival'][0])

    def test_mean_does_not_certify_a_ray(self):
        m=p.Moments(2.,5.); self.assertEqual(p.cantelli_upper(1.8,m),1.)
        self.assertEqual(p.equal_moments()['queries'][0]['true_angular_survival'],[.5,.2])

    def test_nonfinite_invalid_and_zero_variance(self):
        for mean,second in ((float('nan'),1.),(1.,float('inf')),(-1.,2.),(2.,3.)):
            with self.assertRaises(ValueError): p.Moments(mean,second)
        self.assertEqual(p.cantelli_upper(2.,p.Moments(1.,1.)),0.)
        self.assertGreater(p.display_weight(0.),0.)
        with self.assertRaises(ValueError): p.moments([1.,2.],[0.,0.])
        with self.assertRaises(ValueError): p.moments([1.]*4097)

    def test_opaque_wall_still_blocks(self):
        r=p.leakage()[0]; self.assertEqual(r['exact_geometry_supported'],0.)
        self.assertGreater(r['moment_linear'],0.); self.assertGreater(r['moment_perceptual'],0.)
        self.assertLess(r['moment_linear'],r['naive_linear'])

    def test_all_blocked_normalization_fallback(self):
        r=p.leakage()[1]
        self.assertAlmostEqual(r['moment_linear'],100.)
        self.assertAlmostEqual(r['moment_perceptual'],100.)
        self.assertIsNone(r['exact_geometry_supported']); self.assertEqual(r['exact_status'],'Unavailable')

    def test_finite_partition_mixed_moments(self):
        r=p.leakage()[2]; self.assertEqual(r['exact_geometry_supported'],0.)
        self.assertGreater(r['moments'][0]['variance'],0.)
        self.assertGreater(r['moments'][0]['trace']['capped'],0)

    def test_opening_and_dynamic_state(self):
        r=p.unknown_and_dynamic()
        self.assertEqual(r['door_open']['segment'],(1.,1.))
        self.assertEqual(r['door_closed']['segment'],(0.,0.))
        self.assertEqual(r['stale_open_moments_on_closed_door']['angular_weight'],1.)

    def test_unknown_is_not_transparent_or_sky(self):
        r=p.unknown_and_dynamic()
        self.assertEqual(r['unknown_alpha']['segment'],(0.,1.))
        self.assertEqual(r['missing_geometry']['segment'],(0.,1.))
        self.assertEqual(r['missing_sky_boundary'],(0.,1.))
        self.assertEqual(r['explicit_sky'],(1.,1.))
        with self.assertRaises(ValueError): p.directional_samples(p.Scene((),unbounded_unknown=True),(0.,0.,1.),(1.,0.,0.))

    def test_normalization_and_nonlinear_mix(self):
        r=p.run()['codec']; self.assertAlmostEqual(r['restored_irradiance'],math.pi*r['constant_radiance'])
        self.assertEqual(r['linear_mix_0_100'],50.)
        self.assertEqual(r['perceptual_mix_0_100'],25.)
        self.assertIsNone(p.average((1.,2.),(0.,0.)))

    def test_memory_accounting(self):
        r=p.storage(); self.assertEqual(r['cpu_fp32_rgb_moments_metadata_bytes_per_probe'],2064)
        self.assertEqual(r['illustrative_gpu_like_r10_rg16_rgba16_border_bytes_per_probe'],1288)

    def test_classification_is_not_solid_certification(self):
        r=p.placement(); self.assertTrue(r['heuristic_inactive']); self.assertTrue(r['analytic_sheet_point_free'])
        self.assertGreater(r['fixed_backface_hits']/r['fixed_rays'],.25)
        self.assertGreater(r['relocation_counterexample']['stale_value_relative_error'],.3)
        self.assertEqual(p.run()['source_ambiguity']['normal_incidence_lux'],[1.,1.])

    def test_deterministic_witness(self):
        a=p.run(); self.assertEqual(a,p.run())
        path=Path(__file__).parent/'results.json'
        if path.exists(): self.assertEqual(json.loads(path.read_text()),json.loads(json.dumps(a)))


if __name__=='__main__': unittest.main()
