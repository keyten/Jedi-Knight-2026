"""Frozen code-domain fields and evaluator labels; no labels enter extraction.
These complement, and never impersonate, original q3map2 compiled fixtures.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
from .extract import stable


def cases():
    specs = dict(
        point=[([16, 8], [42, 42], 0, [180, 90, 45])],
        spot=[([32, -16], [48, 24], 30, [150, 110, 40])],
        panel=[([-16, 8], [56, 20], 65, [110, 160, 140])],
        multi=[([-64, 24], [24, 24], 0, [180, 20, 10]),
               ([64, -24], [24, 24], 0, [10, 40, 180])],
        flat=[], ramp=[], material_seam=[], gi_only=[([16, 8], [42, 42], 0, [180, 90, 45])])
    result = []
    for name, blobs in specs.items():
        rows = []
        for j, y in enumerate(range(-128, 129, 8)):
            for i, x in enumerate(range(-128, 129, 8)):
                rgb = [8.]*3
                for centre, size, angle, colour in blobs:
                    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
                    a, b = x-centre[0], y-centre[1]
                    u, v = a*c+b*s, -a*s+b*c
                    gain = math.exp(-.5*((u/size[0])**2+(v/size[1])**2))
                    rgb = [a+gain*b for a, b in zip(rgb, colour)]
                if name == 'ramp': rgb = [80+x*.5]*3
                if name == 'flat': rgb = [70]*3
                if name == 'material_seam': rgb = [180 if x > 0 else 30]*3
                rows.append(dict(id=stable([name, i, j]), image=0, page=0, x=i, y=j,
                                 surface=1, slot=0, style=0, coverage=2, issues=0,
                                 material=('left' if x < 0 else 'right') if name == 'material_seam' else 'uniform',
                                 area=64, raw=[round(c) for c in rgb], position=[x, y, 0],
                                 normal=[0, 0, 1], encoding=0, fit_candidate=True))
        labels = [dict(centre_world=[*b[0], 0], footprint_angle_degrees=b[2] if b[1][0] != b[1][1] else None)
                  for b in blobs]
        # GI features are positive patch truth but negative primary-lamp truth.
        result.append(dict(name=name, rows=rows, labels=labels,
                           direct_primary=name not in ('flat', 'ramp', 'material_seam', 'gi_only')))
    return result
