"""Deterministic representation tests, not a game/runtime validation suite.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import math
from pathlib import Path
import unittest
import field_basis as f


class FieldBasis(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.result=f.run()

    def test_degree_counts_and_cardinal_signs(self):
        for L,count in ((1,4),(2,9),(4,25),(8,81)): self.assertEqual(len(f.basis((0.,0.,1.),L)),count)
        k=math.sqrt(3/(4*math.pi))
        self.assertAlmostEqual(f.basis((1.,0.,0.),1)[3],-k)
        self.assertAlmostEqual(f.basis((0.,1.,0.),1)[1],-k)
        self.assertAlmostEqual(f.basis((0.,0.,1.),1)[2],k)

    def test_orthonormal_basis(self):
        grid=[(f.basis(d,4),w) for d,w in f.sphere_grid(12)]
        for i in range(25):
            for j in range(25):
                self.assertAlmostEqual(sum(Y[i]*Y[j]*w for Y,w in grid),float(i==j),places=11)

    def test_canonical_diffuse_factors(self):
        expected=(math.pi,2*math.pi/3,math.pi/4,0.,-math.pi/24)
        for a,b in zip(f.factors(4),expected): self.assertAlmostEqual(a,b,places=12)

    def test_constant_radiance_e_over_pi(self):
        coeff=f.project([(d,w,(1.,2.,3.)) for d,w in f.sphere_grid(8)],4)
        for d in f.directions(9):
            irradiance=f.evaluate(coeff,d,4,True)
            for value,c in zip(irradiance,(1.,2.,3.)): self.assertAlmostEqual(value/math.pi,c,places=11)

    def test_diffuse_order3_does_not_add_order3_information(self):
        rows={r['degree']:r for r in self.result['comparison']['rows']}
        self.assertAlmostEqual(rows[2]['irradiance']['relative_rms'],rows[3]['irradiance']['relative_rms'],places=12)
        self.assertLess(rows[3]['radiance']['relative_rms'],rows[2]['radiance']['relative_rms'])

    def test_diffuse_is_easier_than_radiance_but_not_exact(self):
        rows={r['degree']:r for r in self.result['comparison']['rows']}
        self.assertLess(rows[2]['irradiance']['relative_rms'],.06)
        self.assertGreater(rows[2]['radiance']['relative_rms'],.8)
        self.assertLess(rows[4]['irradiance']['relative_rms'],rows[2]['irradiance']['relative_rms'])
        self.assertLess(rows[8]['irradiance']['relative_rms'],rows[4]['irradiance']['relative_rms'])

    def test_ringing_is_reported_before_clamp(self):
        first=self.result['delta_irradiance'][0]['metrics']
        self.assertLess(first['minimum'],-.2); self.assertGreater(first['negative_channels'],0)
        self.assertGreater(self.result['comparison']['rows'][0]['irradiance']['negative_channels'],0)

    def test_independent_quadrature_resolution(self):
        self.assertLess(self.result['comparison']['reference_48_vs_64_max_irradiance_difference'],1e-10)
        self.assertAlmostEqual(sum(w for _,w in f.sphere_grid(12)),4*math.pi,places=11)

    def test_sg_normalization_and_oracle_not_fitting(self):
        axis=f.unit((.2,.1,1.))
        integral=sum(w*f.sg(d,axis,32.) for d,w in f.sphere_grid(32))
        self.assertAlmostEqual(integral,1.,places=10)
        expected=(31+math.exp(-32))/(32*(1-math.exp(-64)))
        self.assertAlmostEqual(f.sg_irradiance(axis,axis),expected,places=11)
        oracle=self.result['comparison']['sg_oracle']
        self.assertEqual(oracle['radiance']['rmse'],0.)
        self.assertFalse(oracle['fitted_unknown_geometry'])

    def test_half_precision_error_separate(self):
        for row in self.result['comparison']['rows']:
            self.assertTrue(math.isfinite(row['fp16_coefficient_extra_rmse']))
            self.assertLess(row['fp16_coefficient_extra_rmse'],.001)

    def test_valid_black_not_missing(self):
        r=f.validity_control(); self.assertEqual(r['proper_linear_interpolation'],50.)
        self.assertEqual(r['reject_black_and_renormalize'],100.)
        self.assertIsNone(r['no_valid_support'])

    def test_invalid_directions_order_and_samples(self):
        for d,L in (((0.,0.,0.),2),((float('nan'),0.,1.),2),((0.,0.,1.),9)):
            with self.assertRaises(ValueError): f.basis(d,L)
        with self.assertRaises(ValueError): f.project([((0.,0.,1.),1.,(1.,float('inf'),1.))],2)
        with self.assertRaises(ValueError): f.sphere_grid(100)

    def test_deterministic_witness(self):
        self.assertEqual(self.result,f.run())
        path=Path(__file__).parent/'results.json'
        if path.exists(): self.assertEqual(json.loads(path.read_text()),json.loads(json.dumps(self.result)))


if __name__=='__main__': unittest.main()
