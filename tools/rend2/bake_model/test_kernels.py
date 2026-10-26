#!/usr/bin/env python3
"""Numerical discriminators for independent forward-model choices.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
import unittest
from bake_model import (point_scalar, spot_factor, encode_color, grid_reduce,
                        polygon_factor, normal_to_latlong)


class Kernels(unittest.TestCase):
    def test_distinct_falloffs_without_scale_fit(self):
        self.assertEqual([point_scalar(100,d,fast=False) for d in (64,128,256)],
                         [183.10546875,45.7763671875,11.444091796875])
        self.assertEqual([point_scalar(100,d,linear=True,fast=False) for d in (64,128,256)],
                         [29.75,0,0])
        # The angular factor acts on the linear intercept, before distance loss.
        self.assertEqual(point_scalar(300,64,.5,linear=True,fast=False),76.625)

    def test_clamp_softening_and_envelope(self):
        self.assertEqual(point_scalar(1,0,fast=False),7500/256)
        self.assertAlmostEqual(point_scalar(100,64,extra_distance=64,fast=False),750000/8192)
        self.assertEqual(point_scalar(100,math.sqrt(750000)),0)
        self.assertGreater(point_scalar(100,1000,fast=False),0)

    def test_spot_is_world_space_edge(self):
        # At the target plane, radius=64 becomes 80. Edge width is 32.
        values=[spot_factor((r,0,-128),(0,0,0),(0,0,-1),128) for r in (0,48,64,80)]
        self.assertEqual(values,[1,1,.5,0])
        self.assertEqual(spot_factor((0,0,128),(0,0,0),(0,0,-1),128),0)

    def test_coupled_rgb_clipping_and_truncation(self):
        self.assertEqual(encode_color([510,255,51]),[255,127,25])
        self.assertEqual(encode_color([510,255,51],compensate=2),[127,63,12])
        self.assertEqual(encode_color([0,0,0],nonzero=True),[1,1,1])
        self.assertEqual(encode_color([64,64,64],gamma=2),[127,127,127])
        self.assertEqual(encode_color([255,127.5,0],exposure=255),[161,80,0])

    def test_grid_reduction_and_versioned_fudge(self):
        a,d,u=grid_reduce([([100,100,100],[0,0,1])])
        self.assertEqual((a,d,u),([0,0,0],[100,100,100],[0,0,1]))
        a,_,_=grid_reduce([([100,100,100],[0,0,1])],directed_leak=.125)
        self.assertEqual(a,[12.5]*3)
        a,d,u=grid_reduce([([100]*3,[1,0,0]),([100]*3,[-1,0,0])])
        self.assertEqual((a,d,u),([50]*3,[0]*3,[0]*3))

    def test_signed_polygon_integral(self):
        square=[[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]]
        # Analytic axial square: four identical edge terms.
        expected=4*math.acos(1/3)/math.sqrt(2)*0.159154942
        actual=polygon_factor([0,0,0],[0,0,1],square)
        self.assertAlmostEqual(actual,expected,places=12)
        self.assertAlmostEqual(polygon_factor([0,0,0],[0,0,1],square[::-1]),-actual)

    def test_compiler_255_direction_encoding(self):
        self.assertEqual(normal_to_latlong([0,0,1]),[0,0])
        self.assertEqual(normal_to_latlong([0,0,-1]),[128,0])
        self.assertEqual(normal_to_latlong([1,0,0]),[63,0])
        self.assertEqual(normal_to_latlong([0,-1,0]),[63,193])


if __name__=='__main__':
    unittest.main()
