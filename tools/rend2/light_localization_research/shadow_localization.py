#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic identifiability demonstrations, not a reconstruction solver.

Known synthetic geometry, isolated direct lighting, specified correspondences,
metres, and ideal inverse-square point emitters. No BSP, compiler emulator,
runtime API, automatic edge association, stochastic fitting or asset writer.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def unit(v):
    length = math.sqrt(dot(v, v))
    if not math.isfinite(length) or length <= 1e-14:
        raise ValueError("nondegenerate finite direction required")
    return tuple(x / length for x in v)


def point_response(source, receiver):
    """Unit-candela irradiance on a horizontal upward receiver, in lux."""
    d = sub(source, receiver)
    r2 = dot(d, d)
    if r2 <= 0:
        raise ValueError("no singular point-source evaluation")
    return max(0.0, d[2]) / r2 ** 1.5


def rgb8(value):
    """Declared fixture quantizer: nearest, half-up; not q3map ColorToBytes."""
    return min(255, max(0, math.floor(255 * value + 0.5)))


def shadow_point(source, caster, receiver_z=0.0):
    """Intersection of source->known caster line with a receiver plane.

    Source above caster above receiver. This is a specified 3D correspondence;
    an arbitrary point on a mesh silhouette is not automatically such a caster.
    """
    if not source[2] > caster[2] > receiver_z:
        raise ValueError("ordered source, caster and receiver heights required")
    t = (receiver_z - source[2]) / (caster[2] - source[2])
    return tuple(source[k] + t * (caster[k] - source[k]) for k in range(3))


def wall_blocks(source, receiver, wall_x, wall_height, half_y=None):
    """Independent segment/finite vertical rectangle intersection.

    The wall occupies x=wall_x, z in [0,wall_height], optionally |y|<=half_y.
    Boundaries count as hits in this ideal exact-geometry convention.
    """
    dx = receiver[0] - source[0]
    if abs(dx) < 1e-14:
        return False  # fixtures avoid coplanar rays; no production policy implied
    t = (wall_x - source[0]) / dx
    if not 0 < t < 1:
        return False
    y = source[1] + t * (receiver[1] - source[1])
    z = source[2] + t * (receiver[2] - source[2])
    return 0 <= z <= wall_height and (half_y is None or abs(y) <= half_y)


def height_band(boundary, tolerance):
    """Exact interval for source (0,0,H), caster (1,0,1), floor z=0.

    None upper endpoint means unbounded, NOT a Gaussian standard deviation.
    H=b/(b-1); b<=1 is the distant-limit/incompatible side.
    """
    if not all(math.isfinite(x) for x in (boundary, tolerance)) or tolerance < 0:
        raise ValueError("finite boundary and nonnegative tolerance required")
    lo, hi = boundary - tolerance, boundary + tolerance
    if hi <= 1:
        raise ValueError("no compatible finite source above the caster")
    return (hi / (hi - 1), None if lo <= 1 else lo / (lo - 1))


def solve_small(matrix, rhs, tolerance=1e-12):
    """Tiny pivoted elimination for the diagnostic linear systems only.

    Reject numerical singularity; never pretend the pseudoinverse's zero in a
    null direction is zero positional uncertainty. Not a general rank estimator.
    """
    n = len(rhs)
    a = [list(row) + [b] for row, b in zip(matrix, rhs)]
    scale = max(abs(v) for row in matrix for v in row)
    if scale == 0:
        return None
    for k in range(n):
        pivot = max(range(k, n), key=lambda j: abs(a[j][k]))
        if abs(a[pivot][k]) <= tolerance * scale:
            return None
        a[k], a[pivot] = a[pivot], a[k]
        divisor = a[k][k]
        a[k] = [v / divisor for v in a[k]]
        for j in range(n):
            if j != k:
                factor = a[j][k]
                a[j] = [x - factor * y for x, y in zip(a[j], a[k])]
    return tuple(row[-1] for row in a)


def intersect_caster_lines(correspondences):
    """Least squared perpendicular distance to known shadow->caster lines.

    A returned intersection still needs ray ordering and physical feasibility
    tests; this diagnostic deliberately does not certify a physical emitter.
    """
    matrix, rhs = [[0.0] * 3 for _ in range(3)], [0.0] * 3
    for caster, shadow in correspondences:
        d = unit(sub(caster, shadow))
        for i in range(3):
            for j in range(3):
                projection = float(i == j) - d[i] * d[j]
                matrix[i][j] += projection
                rhs[i] += projection * shadow[j]
    return solve_small(matrix, rhs)


def edge_plane(a, b, shadow):
    n = unit(cross(sub(b, a), sub(shadow, a)))
    return n, dot(n, a)


def intersect_edge_planes(records):
    """Edge association is supplied: (edge-start, edge-end, shadow point)."""
    rows, rhs = [], []
    for a, b, shadow in records:
        n, offset = edge_plane(a, b, shadow)
        rows.append(n)
        rhs.append(offset)
    # Exactly three independent planes, to make the identifiability requirement explicit.
    if len(rows) != 3:
        raise ValueError("this demonstration needs exactly three plane constraints")
    return solve_small(rows, rhs)


def penumbra_width(source_width, source_height, caster_height):
    """Full geometric support width, parallel emitter/floor in 2D cross section.

    No assumed PSF, radiance gradient, 10-90% threshold or bake blur inversion.
    """
    if source_width <= 0 or not source_height > caster_height > 0:
        raise ValueError("positive extent and ordered heights required")
    return source_width * caster_height / (source_height - caster_height)


def extent_from_two_widths(width1, z1, width2, z2):
    ratio = width1 / width2
    denominator = ratio * z2 - z1
    if abs(denominator) < 1e-12:
        return None
    height = z1 * z2 * (ratio - 1) / denominator
    extent = width1 * (height - z1) / z1
    return height, extent


def candidate_demo():
    true_height, peak = 4.0, 128 / 255
    intensity = peak * true_height * true_height
    caster, source = (1, 0, 1), (0, 0, true_height)
    probes = [(-0.05, 0, 0), (0, 0, 0), (0.05, 0, 0)]
    observed = [rgb8(intensity * point_response(source, x)) for x in probes]
    boundary = shadow_point(source, caster)[0]
    candidates, retained = [], []
    for k in range(301):
        h = 2 + k * 0.02
        amplitude = peak * h * h
        p = (0, 0, h)
        codes = [rgb8(amplitude * point_response(p, x)) for x in probes]
        if codes == observed:
            candidates.append(h)
            if abs(shadow_point(p, caster)[0] - boundary) <= 0.01:
                retained.append(h)
    heldout = [(1.2, 0, 0), (1.4, 0, 0), (1.6, 0, 0)]
    masks = {}
    for h in (2.0, 4.0, 8.0):
        masks[str(h)] = [wall_blocks((0, 0, h), x, 1, 1) for x in heldout]
    return {"rgb8": observed, "intensity_only_candidates": len(candidates),
            "shadow_band_candidates": len(retained), "height_band_m": height_band(boundary, 0.01),
            "boundary_m": boundary, "heldout_receiver_x_m": [x[0] for x in heldout],
            "blocked_at_height_m": masks}


def correspondence_demo():
    source = (0.5, -0.25, 4.0)
    casters = ((1, 0, 1), (-1, 0.5, 1.5))
    lines = [(q, shadow_point(source, q)) for q in casters]
    recovered_lines = intersect_caster_lines(lines)
    edges = (((1, -0.5, 1), (1, 0.5, 1)),
             ((-0.5, 1, 1.5), (0.5, 1, 1.5)),
             ((-1, -0.5, 2), (-0.5, 0, 2.5)))
    records = [(a, b, shadow_point(source, tuple((x + y) / 2 for x, y in zip(a, b))))
               for a, b in edges]
    recovered_planes = intersect_edge_planes(records)
    error = lambda p: math.sqrt(dot(sub(p, source), sub(p, source)))
    return {"truth_m": source, "two_caster_lines_m": recovered_lines,
            "three_edge_planes_m": recovered_planes,
            "line_error_m": error(recovered_lines), "plane_error_m": error(recovered_planes)}


def source_separation_demo():
    sources = ((-1, 0, 3), (1, 0, 3))
    receivers = ((0, 0, 0), (0, 2, 0))
    no_wall = [[point_response(p, x) for p in sources] for x in receivers]
    wall = [[0 if wall_blocks(p, x, 0.5, 2, 0.5) else point_response(p, x)
             for p in sources] for x in receivers]
    true_intensities = (2, 5)
    observed = [dot(row, true_intensities) for row in wall]
    return {"no_wall_matrix": no_wall, "with_wall_matrix": wall,
            "no_wall_solution": solve_small(no_wall, [dot(row, true_intensities) for row in no_wall]),
            "with_wall_intensities_cd": solve_small(wall, observed)}


def visible_footprint(height, lo=1.3, hi=1.4):
    """Exact normalized visibility integral for the isolated 1D wall shadow.

    This is the visibility component, not irradiance with changing falloff.
    """
    if height <= 1 or hi <= lo:
        raise ValueError("source above caster and nonempty footprint required")
    boundary = height / (height - 1)
    return min(1.0, max(0.0, (hi - boundary) / (hi - lo)))


def footprint_demo():
    height, step = 4.0, 1e-5
    finite_difference = (visible_footprint(height + step) - visible_footprint(height - step)) / (2 * step)
    analytical = 1 / ((height - 1) ** 2 * (1.4 - 1.3))
    def fixed_samples(h):
        boundary = h / (h - 1)
        return sum(x > boundary for x in (1.31, 1.33, 1.35, 1.37, 1.39)) / 5
    return {"integrated_visibility": visible_footprint(height),
            "analytical_d_visibility_d_height": analytical,
            "finite_difference_d_visibility_d_height": finite_difference,
            "fixed_five_sample_derivative": (fixed_samples(height + step) - fixed_samples(height - step)) / (2 * step)}


def report():
    return {"schema": "org.openjk.light-localization-research/1",
            "scope": "ideal synthetic demonstrations, not BSP fitting or emitter certification",
            "candidate_height": candidate_demo(), "known_correspondences": correspondence_demo(),
            "two_source_separation": source_separation_demo(),
            "footprint_boundary_derivative": footprint_demo(),
            "penumbra": {"width_at_caster_z1_m": penumbra_width(0.6, 4, 1),
                         "width_at_caster_z2_m": penumbra_width(0.6, 4, 2),
                         "recovered_height_extent_m": extent_from_two_widths(0.2, 1, 0.6, 2)},
            "unbounded_height_band": height_band(1.005, 0.01)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = (json.dumps(report(), indent=2, sort_keys=True, allow_nan=False) + "\n").encode()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print("SHA256", hashlib.sha256(data).hexdigest())
    else:
        print(data.decode(), end="")


if __name__ == "__main__":
    main()
