import unittest
from PIL import Image
from generate_physicalization_shapes import measure


class ShapeTests(unittest.TestCase):
    def test_empty_mask_rejected(self):
        with self.assertRaises(ValueError):
            measure(Image.new('L', (8, 8), 0))

    def test_central_cloud_is_smaller_than_outer_ring(self):
        core, ring = Image.new('L', (16, 16)), Image.new('L', (16, 16))
        for y in range(16):
            for x in range(16):
                radius2 = (x - 7.5)**2 + (y - 7.5)**2
                if radius2 < 16:
                    core.putpixel((x, y), 255)
                if 49 < radius2 < 64:
                    ring.putpixel((x, y), 255)
        self.assertLess(measure(core)[1], measure(ring)[1])
        self.assertEqual(measure(core)[2], 0)
        self.assertGreater(measure(ring)[2], 0)

    def test_opacity_scales_fill_without_inventing_support(self):
        opaque = Image.new('L', (16, 16), 255)
        half = Image.new('L', (16, 16), 128)
        full_fill, full_radius, _ = measure(opaque)
        half_fill, half_radius, _ = measure(half)
        self.assertAlmostEqual(full_fill, 1)
        self.assertAlmostEqual(half_fill, 128/255)
        self.assertEqual(full_radius, half_radius)


if __name__ == '__main__':
    unittest.main()
