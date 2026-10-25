#!/usr/bin/env python3
"""Dependency-free guards for the external-bake comparison/reporting contract.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import struct
import tempfile
import unittest
from pathlib import Path
from run_pipeline import bsp_counts, compare_bakes


def bsp(grid=b'\x01' * 30, side_ref=0, light=b'\x12', vertex=b'\x10'):
    sections = [b''] * 18
    sections[9] = struct.pack('<III', 1, 2, side_ref)
    sections[10] = vertex
    sections[14] = light
    sections[15] = grid
    sections[17] = b'\0\0'
    header = bytearray(b'RBSP\x01\0\0\0' + b'\0' * 144)
    payload = bytearray()
    for i, value in enumerate(sections):
        struct.pack_into('<II', header, 8 + i * 8, len(header) + len(payload), len(value))
        payload += value
    return bytes(header + payload)


class BakeReportTests(unittest.TestCase):
    def test_exact_repeat(self):
        report = compare_bakes(bsp(), bsp())
        self.assertTrue(report['repeated_bsp_identical'])
        self.assertTrue(report['repeated_lighting_geometry_identical'])
        self.assertFalse(report['bsp_bytes_modified_after_compiler'])

    def test_ancillary_difference_is_reported(self):
        report = compare_bakes(bsp(), bsp(side_ref=1000000))
        self.assertFalse(report['repeated_bsp_identical'])
        self.assertEqual(report['varying_ancillary_brushside_surface_refs'], [0])
        self.assertTrue(report['repeated_lighting_geometry_identical'])

    def test_grid_variability_is_not_called_reproducible(self):
        report = compare_bakes(bsp(), bsp(grid=b'\x02' * 30))
        self.assertFalse(report['repeated_lighting_geometry_identical'])
        self.assertFalse(report['repeated_lightgrid_identical'])
        self.assertEqual(report['varying_logical_grid_cells'], [0])
        self.assertTrue(report['repeated_surface_lighting_geometry_identical'])

    def test_real_lighting_or_geometry_difference_fails(self):
        for other in (bsp(light=b'\x13'), bsp(vertex=b'\x11')):
            with self.assertRaises(RuntimeError):
                compare_bakes(bsp(), other)

    def test_invalid_grid_reference_fails(self):
        with self.assertRaises(RuntimeError):
            compare_bakes(bsp(), bsp(grid=b''))

    def test_truncated_or_empty_bake_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'bad.bsp'
            for content in (b'RBSP', bsp()):
                path.write_bytes(content)
                with self.assertRaises(RuntimeError):
                    bsp_counts(path)


if __name__ == '__main__':
    unittest.main()
