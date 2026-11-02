"""E-only angular atlas controls. GPL-2.0-or-later."""
import math
import unittest
import octahedral as o


class Atlas(unittest.TestCase):
    def test_roundtrip_directions_and_seams(self):
        for n in o.reference.directions(257):
            m = o.decode(*o.encode(n))
            self.assertLess(sum((a-b)**2 for a, b in zip(n, m)), 1e-25)
        atlas = o.IrradianceAtlas(16, lambda n: (n[0]+1, n[1]+1, n[2]+1))
        for n in ((1., 0., 0.), (-1., 0., 0.), (0., 1., 0.), (0., -1., 0.), (0., 0., -1.)):
            self.assertTrue(all(math.isfinite(v) and v >= 0 for v in atlas.irradiance(n)))
        # Adjacent sides of the right atlas edge represent contiguous sphere directions.
        a = atlas.irradiance(o.decode(.999999, .3))
        b = atlas.irradiance(o.decode(.999999, -.3))
        self.assertLess(abs(a[0]-b[0]), 1e-12)
        self.assertLess(abs(a[1]+b[1]-2), 4*2**-23)  # two independent FP32 rounded texels

    def test_constant_units_not_e_over_pi(self):
        atlas = o.IrradianceAtlas(8, lambda n: (math.pi, 2*math.pi, 3*math.pi))
        for n in o.reference.directions(17):
            for c, value in enumerate(atlas.irradiance(n)):
                self.assertAlmostEqual(value, (c+1)*math.pi, places=6)

    def test_black_not_absent_and_no_inverse(self):
        atlas = o.IrradianceAtlas(8, lambda n: (0., 0., 0.))
        self.assertEqual(atlas.irradiance((0., 0., 1.)), (0., 0., 0.))
        with self.assertRaises(ValueError):
            atlas.incoming_radiance((0., 0., 1.))

    def test_rounding_overflow_and_determinism(self):
        a = o.IrradianceAtlas(8, lambda n: (.1, 1., 3.), 'e')
        b = o.IrradianceAtlas(8, lambda n: (.1, 1., 3.), 'e')
        self.assertEqual(a.payload, b.payload)
        self.assertEqual(len(a.payload), 8*8*3*2)
        with self.assertRaises((OverflowError, ValueError)):
            o.IrradianceAtlas(8, lambda n: (1e8, 0., 0.), 'e')
        with self.assertRaises(ValueError):
            o.IrradianceAtlas(8, lambda n: (-1., 0., 0.))


if __name__ == '__main__':
    unittest.main()
