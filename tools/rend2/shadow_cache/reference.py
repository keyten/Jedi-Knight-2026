#!/usr/bin/env python3
"""Independent closed-form reference for the owned v2/v3 panel/slab fixture.
No production cache predictions are computed here. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def omega(x0, x1, y0, y1, height):
    # Mixed derivative of this corner primitive is h/(x*x+y*y+h*h)^(3/2).
    def corner(x, y):
        return math.atan2(x*y, height*math.sqrt(x*x+y*y+height*height))
    return corner(x1, y1)-corner(x0, y1)-corner(x1, y0)+corner(x0, y0)


def main(args):
    native = json.loads(args.native.read_text())
    wall_exit_x = struct.unpack('<f', struct.pack('<f', .01))[0]
    # Receiver (-2,0,1), source z=5, wall z>=3. A segment can hit the slab
    # until its far x face; this determines the visible source-x subrectangle.
    boundary_x = -2 + (wall_exit_x+2)*(5-1)/(3-1)
    total = omega(3, 5, -1, 1, 4)
    visible = omega(boundary_x+2, 5, -1, 1, 4)
    fraction = visible/total
    result = dict(schema='org.openjk.shadow-cache-analytic-reference.v1',
                  scope='owned separated panel only; response integral without receiver cosine',
                  native_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),
                  receiver=[-2, 0, 1], panel_center=[2, 0, 5], half_dimensions=[1, 1],
                  slab_min=[-wall_exit_x, -4, 3], slab_max=[wall_exit_x, 4, 8],
                  source_shadow_boundary_x=boundary_x,
                  total_solid_angle=total, visible_solid_angle=visible,
                  reference_fraction=fraction, cache_fraction=native['area_fraction'],
                  cache_absolute_error=abs(native['area_fraction']-fraction),
                  dense512_absolute_error=abs(native['area_reference']-fraction),
                  refinement_difference_is_error_bound=False,
                  unit='dimensionless visibility response ratio; not lux',
                  stock_evidence=False)
    assert result['cache_absolute_error'] < .035  # same frozen acceptance margin
    assert result['dense512_absolute_error'] < .003
    args.out.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    main(parser.parse_args())
