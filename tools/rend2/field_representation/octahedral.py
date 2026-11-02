#!/usr/bin/env python3
"""Optional irradiance-atlas capacity control, not an RBDOOM replay/GPU codec.
Own math, Python standard library, GPL-2.0-or-later.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'rbdoom_research'))
import field_basis as reference


def sign(v):
    return -1 if v < 0 else 1


def encode(n):
    if not all(math.isfinite(v) for v in n) or abs(sum(v*v for v in n)-1) > 1e-9:
        raise ValueError('Unit world normal required')
    x, y, z = n
    s = abs(x)+abs(y)+abs(z)
    x, y = x/s, y/s
    if z < 0:
        x, y = (1-abs(y))*sign(x), (1-abs(x))*sign(y)
    return x, y


def decode(x, y):
    if not all(math.isfinite(v) and -1 <= v <= 1 for v in (x, y)):
        raise ValueError('Octahedral coordinates')
    z = 1-abs(x)-abs(y)
    if z < 0:
        x, y = (1-abs(y))*sign(x), (1-abs(x))*sign(y)
    return reference.unit((x, y, z))


class IrradianceAtlas:
    def __init__(self, size, function, precision='f'):
        if size not in (8, 16, 32) or precision not in ('f', 'e'):
            raise ValueError('Atlas budget/precision')
        self.size, self.payload = size, bytearray()
        self.values = []
        for y in range(size):
            for x in range(size):
                rgb = function(decode(2*(x+.5)/size-1, 2*(y+.5)/size-1))
                if any(not math.isfinite(v) or v < 0 for v in rgb):
                    raise ValueError('Invalid irradiance, no hidden clamp')
                row = []
                for v in rgb:
                    packed = struct.pack('<'+precision, v)  # overflow is rejected
                    rounded = struct.unpack('<'+precision, packed)[0]
                    if not math.isfinite(rounded):
                        raise ValueError('Quantization overflow')
                    self.payload.extend(packed)
                    row.append(rounded)
                self.values.append(row)

    def tap(self, x, y):
        n = self.size
        # One-pixel virtual border folds across the opposite octahedral edge.
        if x < 0:
            x, y = -x-1, n-1-y
        if x >= n:
            x, y = 2*n-1-x, n-1-y
        if y < 0:
            x, y = n-1-x, -y-1
        if y >= n:
            x, y = n-1-x, 2*n-1-y
        return self.values[x+n*y]

    def irradiance(self, normal):
        u, v = encode(normal)
        x, y = (u+1)*self.size/2-.5, (v+1)*self.size/2-.5
        i, j = math.floor(x), math.floor(y)
        fx, fy = x-i, y-j
        return tuple(sum(self.tap(i+dx, j+dy)[c]*(fx if dx else 1-fx)*(fy if dy else 1-fy)
                         for dx in (0, 1) for dy in (0, 1)) for c in range(3))

    def incoming_radiance(self, direction):
        raise ValueError('Unsupported: irradiance does not identify incoming radiance')


def run():
    normals = reference.directions(257)
    scenes = {
        'uniform_hemisphere': lambda n: tuple(v*math.pi*.5*(1+n[2]) for v in (.5, .4, .3)),
        'previous_three_coloured_lobes': reference.irradiance_reference,
        'ideal_parallel_delta': lambda n: tuple(v*max(0., n[2]) for v in (1., .9, .7))}
    rows = []
    for name, function in scenes.items():
        truth = [function(n) for n in normals]
        for size in (8, 16, 32):
            unquantized = IrradianceAtlas(size, function)
            unq = [unquantized.irradiance(n) for n in normals]
            for precision in ('f', 'e'):
                atlas = IrradianceAtlas(size, function, precision)
                values = [atlas.irradiance(n) for n in normals]
                rows.append(dict(scene=name, size=size, precision='FP32' if precision == 'f' else 'FP16',
                                 payload_bytes=len(atlas.payload), stored_quantity='E, not E/pi',
                                 error=reference.errors(truth, values),
                                 extra_quantization_rmse=reference.errors(unq, values)['rmse']))
    return dict(schema='org.openjk.octahedral-irradiance-capacity.v1', rows=rows,
                semantics='Exact diffuse reference values are supplied at texel centres; this is capacity/interpolation/quantization, '
                          'not a matched-budget unknown-field estimator, not incoming radiance or GPU performance',
                border='CPU virtual octahedral folding; no claim of the pinned engine atlas margin/lookup replay',
                header_metadata_bytes_included=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.write_text(json.dumps(run(), indent=2, allow_nan=False)+'\n', encoding='utf-8')
