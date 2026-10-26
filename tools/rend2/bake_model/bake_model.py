#!/usr/bin/env python3
"""Small source-derived q3map2 kernels, NOT a complete compiler/evaluator.

All RGB/intensities use compiler-relative units. See docs/rend2-bake-model.md.
Source: NetRadiant d9338309ea8fab75027b7382efaabe034302b862.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math

SOURCE_REVISION = 'd9338309ea8fab75027b7382efaabe034302b862'
COMPILER_SHA256 = '894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4'


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def unit(a):
    length = math.sqrt(dot(a, a))
    return [x / length for x in a] if length else [0.0, 0.0, 0.0]


def point_scalar(intensity, distance, cosine=1.0, linear=False, fade=1.0,
                 scale=7500.0, extra_distance=0.0, angle_scale=0.0,
                 half_lambert=False, fast=True, tolerance=1.0):
    """light.c CreateEntityLights/LightContributionToSample, positive inputs.

    Caller handles geometry, PVS, sidedness and envelope bounds. Grid callers
    pass cosine=1, no angle_scale and no half_lambert. The scalar threshold is
    applied before multiplying RGB. The distance envelope uses raw distance.
    """
    photons = intensity * scale
    envelope = ((photons / 8000.0 - tolerance) / fade if linear else
                math.sqrt(photons / tolerance)) if fast else (
                    photons / 8000.0 / fade if linear else math.inf)
    if distance >= envelope:
        return 0.0
    d = max(16.0, math.sqrt(distance * distance + extra_distance * extra_distance))
    angle = cosine
    if half_lambert:
        angle = (min(angle, 1.0) * 0.5 + 0.5) ** 2 if angle > 0.001 else 0.0
    if angle_scale:
        angle = min(1.0, angle / angle_scale)
    value = max(0.0, angle * photons / 8000.0 - d * fade if linear else
                photons * angle / (d * d))
    return 0.0 if fast and value <= tolerance else value


def spot_factor(receiver, origin, axis, target_distance, radius=64.0):
    """World-space cone with the historical radius+16 and 32-unit edge."""
    delta = sub(receiver, origin)
    axial = dot(delta, axis)
    if axial < 0:
        return 0.0
    cone_radius = (radius + 16.0) / target_distance * axial
    radial = math.sqrt(sum((delta[i] - axial * axis[i]) ** 2 for i in range(3)))
    return max(0.0, min(1.0, (cone_radius - radial) / 32.0))


def polygon_factor(receiver, normal, winding):
    """Signed edge integral; winding order is significant. No horizon clipping.

    light.c PointToPolygonFormFactor. Caller handles plane push and sidedness.
    """
    dirs = [unit(sub(p, receiver)) for p in winding]
    total = 0.0
    for a, b in zip(dirs, dirs[1:] + dirs[:1]):
        cross = [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
        if math.sqrt(dot(cross, cross)) < 0.0001:
            continue
        total += dot(normal, unit(cross)) * math.acos(max(-1.0, min(1.0, dot(a, b))))
        if abs(total) > 6.3:
            return 0.0
    return total * 0.159154942


def encode_color(rgb, scale=1.0, gamma=1.0, compensate=1.0, exposure=0.0,
                 srgb=False, nonzero=False):
    """light_ydnar.c ColorToBytes, restricted to defined byte-cast domain.

    The C code does not provide a safe clamp for compensate<1 out-of-range
    floats. Reject that domain here instead of inventing wrapping semantics.
    """
    value = [255.0 * (max(0.0, c * (scale if scale > 0 else 1.0)) / 255.0) ** (1.0/gamma) for c in rgb]
    peak = max(value)
    gain = ((255.0 / peak if peak > 255 else 1.0) if exposure == 0 else
            (255.0 * (1.0 - math.exp(-peak / exposure)) / peak if peak else 0.0))
    value = [c * gain / compensate for c in value]
    if srgb:
        value = [math.floor(255.0 * (12.92 * (c/255.0) if c/255.0 <= 0.0031308 else
                         1.055 * (c/255.0) ** (1.0/2.4) - 0.055) + 0.5) for c in value]
    if any(not math.isfinite(c) or c < 0 or c > 255.00001 for c in value):
        raise ValueError('ColorToBytes input outside supported C byte conversion domain')
    return [max(1 if nonzero else 0, int(min(255, c))) for c in value]


def grid_reduce(contributions, ambient=(0, 0, 0), directed_leak=0.0):
    """Single-pass, one-style TraceGrid reduction with default directionality.

    contributions = [(RGB, to_source), ...]. No persistent bounce accumulator,
    minlight, flood, quantization or style-overflow policy is hidden here.
    directed_leak=.125 is the 2.5.16 first-pass ambient fudge; 2022 uses zero.
    """
    direction = unit([sum(math.sqrt(dot(c, c)) * u[i] for c, u in contributions) for i in range(3)])
    directed = [0.0, 0.0, 0.0]
    result = list(ambient)
    for color, incoming in contributions:
        d = max(0.0, dot(incoming, direction))
        for i in range(3):
            directed[i] += d * color[i]
            result[i] += 0.25 * (1.0-d) * color[i]
    return [result[i] + directed_leak*directed[i] for i in range(3)], directed, direction


def normal_to_latlong(normal):
    """libs/mathlib/mathlib.c uses 255, while the engine decodes with 256."""
    if normal[0] == 0 and normal[1] == 0:
        return [0 if normal[2] > 0 else 128, 0]
    return [int(math.acos(max(-1.0, min(1.0, normal[2]))) * 255.0/(2*math.pi)) & 255,
            int(math.atan2(normal[1], normal[0]) * 255.0/(2*math.pi)) & 255]
