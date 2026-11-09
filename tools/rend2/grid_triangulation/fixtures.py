"""Evaluator-side analytic fixtures; truth is never passed to discover().
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import itertools
import math

from triangulate import add, angle, dot, mul, norm, sub, unit


class Geometry:
    """Explicit controlled world geometry, independent of source truth."""
    def __init__(self, solid_above=300, unknown=False, blocked=False, dynamic=False):
        self.solid_above, self.unknown, self.blocked, self.dynamic = solid_above, unknown, blocked, dynamic

    def point(self, p):
        return dict(space="unknown" if self.unknown else "solid" if p[2] >= self.solid_above else "free", boundary=False)

    def box(self, lo, hi):
        return dict(space="unknown" if self.unknown or hi[2] >= self.solid_above else "free", boundary=False)

    def ray(self, a, b):
        state = "unknown" if self.unknown else "blocked" if self.blocked or max(a[2], b[2]) >= self.solid_above else "clear"
        return dict(static_state=state, dynamic_possible=self.dynamic, issues=0, span_space="unknown" if self.unknown else "free")


def encode(direction):
    d = unit(direction)
    if abs(d[0])+abs(d[1]) < 1e-12:
        return (0 if d[2] > 0 else 128), 0
    return int(math.acos(d[2])*255/(2*math.pi)) & 255, int(math.atan2(d[1], d[0])*255/(2*math.pi)) & 255


def decode(codes, divisor):
    polar, azimuth = codes
    if divisor == 255 and polar == 128 and azimuth == 0:
        return [0., 0., -1.]
    if divisor == 255 and azimuth > 127: azimuth -= 256
    p, a = polar*2*math.pi/divisor, azimuth*2*math.pi/divisor
    return [math.cos(a)*math.sin(p), math.sin(a)*math.sin(p), math.cos(p)]


def observations(case):
    rows = []
    for ordinal, xyz in enumerate(itertools.product(range(5), range(5), range(3))):
        p = [xyz[0]*64-128, xyz[1]*64-128, xyz[2]*64+32]
        contributions = []
        for i, light in enumerate(case["lights"]):
            incoming = sub(light, p)
            if case["kind"] == "spot" and angle(mul(incoming, -1), case["axis"]) > math.radians(case["half_angle_degrees"]):
                continue
            strength = min(180., 1.8e6/max(1., norm(incoming))**2)
            colour = ([1., .12, .08] if i == 0 else [.08, .15, 1.]) if case["kind"] == "coloured" else [1., .9, .8]
            contributions.append((unit(incoming), mul(colour, strength)))
        if case["kind"] == "reflected":
            contributions = [(unit(sub(case["virtual_position"], p)), [65., 60., 52.])]
        if case["kind"] in ("sun", "sun_local"):
            contributions.append((unit(case["sun_incoming"]), [100., 100., 96.]))
        vector, rgb = [0., 0., 0.], [0., 0., 0.]
        for d, c in contributions:
            vector = add(vector, mul(d, norm(c)))
            rgb = add(rgb, c)
        codes = encode(vector if norm(vector) > 0 else [0, 0, 1])
        rgb = [min(254, int(v)) for v in rgb]
        key = hashlib.sha256(bytes(rgb+list(codes))).hexdigest()
        rows.append(dict(ordinal=ordinal, xyz=list(xyz), position=p, space="free", boundary=False,
                         dynamic_possible=False, present=True, correlation_key=key, consumer256=decode(codes, 256),
                         writer255=decode(codes, 255), writer_possible=True,
                         styles=[dict(id=0, directed=rgb, weak=max(rgb) <= 2)]))
    return rows
