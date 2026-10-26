#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
"""Small analytical counterexamples for the inverse-lighting research note.

Standard library only. This is not a reconstruction solver: no BSP/asset reader,
candidate search, runtime integration, fitted-light writer or production API.
"""
import argparse
import json
import math
from pathlib import Path


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def mass_lumped_triangle(vertex_values, area=1.0):
    """Thesis Eq. 3.7, degree-two exact three-point triangle quadrature.

    A single triangle; scalar piecewise-linear input. Each corner's associated
    area is area/3. A general mesh additionally sums incident triangles.
    """
    if len(vertex_values) != 3 or not math.isfinite(area) or area <= 0:
        raise ValueError("three vertex values and positive finite area required")
    samples = ((2 / 3, 1 / 6, 1 / 6), (1 / 6, 2 / 3, 1 / 6), (1 / 6, 1 / 6, 2 / 3))
    return [sum(dot(w, vertex_values) * w[i] * area / 3 for w in samples)
            / (area / 3) for i in range(3)]


def point_irradiance(position_world, receiver_world, normal, intensity_rgb,
                     world_units_per_metre=1.0):
    """Unoccluded far-field point, receiver normal and RGB candela supplied.

    Returns an RGB illuminance proxy; Y has lux semantics for Y-normalized
    physical RGB inputs. No finite-source approximation or radius clamp.
    """
    delta = [p - x for p, x in zip(position_world, receiver_world)]
    r2 = dot(delta, delta)
    if r2 <= 0 or not math.isfinite(r2) or world_units_per_metre <= 0:
        raise ValueError("positive finite separation and scale required")
    cosine = max(0.0, dot(normal, delta) / math.sqrt(r2))
    factor = cosine * world_units_per_metre ** 2 / r2
    return [factor * c for c in intensity_rgb]


def position_gradient(position, receiver, normal, intensity=1.0):
    """Analytical dE/dp in world coordinates on the lit, visibility-fixed side."""
    d = [p - x for p, x in zip(position, receiver)]
    r2 = dot(d, d)
    nd = dot(normal, d)
    if r2 <= 0 or nd <= 0:
        raise ValueError("derivative example requires the strictly lit side")
    return [intensity * (n / r2 ** 1.5 - 3 * nd * x / r2 ** 2.5)
            for n, x in zip(normal, d)]


def interval_residual(prediction, lower=None, upper=None):
    """Distance to a quantization/censoring interval, not to displayed RGB8."""
    if lower is not None and prediction < lower:
        return prediction - lower
    if upper is not None and prediction > upper:
        return prediction - upper
    return 0.0


def scalar_amplitude(response, observations):
    """Closed-form one-column NNLS diagnostic, not a multi-source optimizer."""
    denominator = dot(response, response)
    if denominator == 0:
        return None  # invisible source is unobservable, not intensity zero
    return max(0.0, dot(response, observations) / denominator)


def canonical_rgb(rgb):
    y = dot((0.2126, 0.7152, 0.0722), rgb)
    if y <= 0:
        return y, None  # black carries no chromaticity information
    return y, [v / y for v in rgb]


def original_q3_grid(contributions, directions, ambient=(0.0, 0.0, 0.0)):
    """Pre-byte-conversion q3map TraceGrid packing, NOT a universal JA model.

    All directions unit receiver-to-source. The original compiler uses RGB
    Euclidean magnitude to weight its single direction, not luminance.
    """
    moment = [sum(math.sqrt(dot(c, c)) * w[k]
                  for c, w in zip(contributions, directions)) for k in range(3)]
    magnitude = math.sqrt(dot(moment, moment))
    if magnitude <= 1e-15:
        return None  # implementation-specific zero-vector encoding is not evidence
    direction = [v / magnitude for v in moment]
    directed = [0.0] * 3
    packed_ambient = list(ambient)
    for c, w in zip(contributions, directions):
        d = max(0.0, dot(w, direction))
        for k in range(3):
            directed[k] += d * c[k]
            packed_ambient[k] += 0.25 * (1 - d) * c[k]
    for k in range(3):
        packed_ambient[k] += 0.25 * directed[k]
    return {"ambient": packed_ambient, "directed": directed, "toward_source": direction}


def report():
    at_one = point_irradiance((0, 0, 1), (0, 0, 0), (0, 0, 1), (1, 1, 1))
    at_two = point_irradiance((0, 0, 2), (0, 0, 0), (0, 0, 1), (4, 4, 4))
    position, receiver, normal = (0.3, -0.4, 2), (0, 0, 0), (0, 0, 1)
    analytical = position_gradient(position, receiver, normal)
    finite_difference = []
    h = 1e-5
    for k in range(3):
        plus, minus = list(position), list(position)
        plus[k] += h
        minus[k] -= h
        f = lambda p: point_irradiance(p, receiver, normal, (1, 1, 1))[0]
        finite_difference.append((f(plus) - f(minus)) / (2 * h))
    residuals = [interval_residual(v, 254.5 / 255, None) for v in (0.5, 1, 4)]
    return {
        "schema": "org.openjk.inverse-lighting-research-checks/1",
        "scope": "analytical checks only; no source reconstruction performed",
        "mass_lumped_linear_triangle": mass_lumped_triangle((0, 0, 1)),
        "single_sample_position_ambiguity": {"height1_cd1": at_one, "height2_cd4": at_two},
        "position_gradient_max_absolute_error": max(abs(a - b) for a, b in zip(analytical, finite_difference)),
        "saturated_lower_bound_residuals_at_0_5_1_4": residuals,
        "direct_only_q3_grid": original_q3_grid(((4, 2, 1),), ((0, 0, 1),)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(report(), indent=2, sort_keys=True, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8", newline="\n")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
