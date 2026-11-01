#!/usr/bin/env python3
"""Verify public X09 aggregate evidence. Licensed assets are not needed.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
from pathlib import Path


def verify(root=None):
    root = root or Path(__file__).parent / 'results/v1'
    manifest = json.loads((root / 'manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise ValueError('Changed numerical witness: ' + name)
    tests = json.loads((root / 'controlled.json').read_text())
    assert tests['failures'] == 0 and tests['checks'] >= 100
    by_name = {x['case']: x for x in tests['cases']}
    assert by_name['duplicate_source_triangles']['physical_area'] == 1
    assert by_name['opposite_sheet_sides']['physical_area'] == 2
    assert by_name['crossing_local_exclusion']['unsupported_area'] > 0
    finite = by_name['finite_square_reference']
    assert 0 < finite['conductance'] < 1
    assert finite['absolute_error'] < .00025
    maps = json.loads((root / 'maps.json').read_text())['maps']
    assert len(maps) == 3
    for m in maps:
        assert not m['physical_source_recovery']
        assert m['historical_profile'] == 'UnknownProfile'
        assert m['after']['transfer_code'] == 0
        assert m['after']['maximum_outgoing_fraction'] <= 1 + 1e-7
        assert m['after']['unavailable_observations'] >= 0
        assert m['contacts'] and all(c['code'] == 0 for c in m['contacts'])
        assert all(c['maximum_outgoing_fraction'] <= 1 + 1e-7 for c in m['contacts'])
    validation = json.loads((root / 'validation.json').read_text())
    assert validation['controlled_repeat_identical'] and validation['stock_repeat_identical']
    print('X09 public witnesses verified; no claim of stock GI completeness/source recovery')


if __name__ == '__main__':
    verify()
