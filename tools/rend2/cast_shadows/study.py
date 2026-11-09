#!/usr/bin/env python3
# Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
"""ASR05 owned beam/wall experiment. No BSP, asset writer or runtime consumer.

Metres, +Z up, receiver wall y=0 with normal +Y. The oracle endpoint
correspondences are additional synthetic measurements, never detected truth.
Python 3.8+, standard library only; fixed grids and no random sampling.
"""
import argparse
from dataclasses import dataclass, replace
import hashlib
import itertools
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "light_localization_research"))
from shadow_localization import dot, sub, intersect_caster_lines, edge_plane


BEAM = ((-0.3, 1.9, 1.2), (0.3, 2.1, 1.6))
TRUTH = (0.2, 4.0, 2.8)
SECOND = (-0.6, 5.0, 3.2)
# Bottom silhouette endpoints, not arbitrary points along the same edge.
CASTERS = ((-0.3, 2.1, 1.2), (0.3, 2.1, 1.2))
LEFT_EDGE = ((-0.3, 2.1, 1.2), (-0.3, 2.1, 1.6))
FILL = 0.08  # explicitly synthetic, relative linear receiver signal
BAND_M = 0.025  # supplied receiver-space measurement bound, not a sigma
MAX_CANDIDATES = 10000


def distance(a, b):
    d = sub(a, b)
    return math.sqrt(dot(d, d))


def project(source, caster):
    """Project a known caster onto y=0; enforce receiver-caster-source ordering."""
    if not source[1] > caster[1] > 0:
        raise ValueError("source must be beyond the caster, beyond the wall")
    t = source[1] / (source[1] - caster[1])
    return tuple(source[k] + t * (caster[k] - source[k]) for k in range(3))


def trace(receiver, source, beam=BEAM, complete=True, opaque=True):
    """Independent finite segment/AABB slab reference; boundaries count as hits.

    Only the owned fixture declares an otherwise complete empty scene. An
    unsupported optical policy, incomplete scene or origin in solid is Unknown.
    This helper is not a substitute for the BSP visibility/domain API.
    """
    if not complete or not opaque:
        return "Unknown", None
    if beam is None:
        return "Visible", None
    low, high = beam
    for p in (receiver, source):
        if all(low[k] <= p[k] <= high[k] for k in range(3)):
            return "Unknown", None
    enter, leave = 0.0, 1.0
    delta = sub(source, receiver)
    for k in range(3):
        if abs(delta[k]) < 1e-14:
            if receiver[k] < low[k] or receiver[k] > high[k]:
                return "Visible", None
        else:
            a, b = sorted(((low[k] - receiver[k]) / delta[k],
                           (high[k] - receiver[k]) / delta[k]))
            enter, leave = max(enter, a), min(leave, b)
            if enter > leave:
                return "Visible", None
    return ("Blocked", "beam") if leave > 0 and enter < 1 else ("Visible", None)


def response(source, receiver):
    """Isotropic inverse-square unit response on +Y wall, relative / m^2."""
    d = sub(source, receiver)
    r = distance(source, receiver)
    if r <= 1e-12:
        raise ValueError("singular point source")
    return max(0.0, d[1]) / r ** 3


def code8(value):
    """Declared linear half-up fixture encoding; not JA ColorToBytes or lux."""
    return max(0, min(255, math.floor(255 * value + 0.5)))


def signal(receiver, sources=((TRUTH, 12.0),), beam=BEAM, angular_mask=False):
    total = FILL
    for source, amplitude in sources:
        # Counterexample: the SAME angular binary mask can be emitted without
        # an intervening caster. This is a constructed emitter distribution,
        # not a claim about JA spot-light implementations or material albedo.
        state, _ = trace(receiver, source, BEAM if angular_mask else beam)
        if state == "Unknown":
            raise ValueError("cannot render Unknown geometry as empty")
        total += amplitude * response(source, receiver) * (state == "Visible")
    return total


@dataclass(frozen=True)
class Sample:
    world: tuple
    code: int
    material: int = 1
    chart: int = 1
    style: int = 0
    normal: tuple = (0.0, 1.0, 0.0)
    mapped: bool = True


@dataclass(frozen=True)
class Patch:
    rows: tuple
    source_linear: bool = True  # only the declared fixture response is known
    filter_radius_m: object = 0.0
    mapping_error_m: float = 0.002
    noise_bound_codes: float = 0.5  # half-step fixture quantization only
    alternatives: tuple = ()


def patch():
    return Patch(tuple(tuple(Sample((x, 0.0, z), code8(signal((x, 0.0, z))))
                             for x in (-1.3 + i * 0.025 for i in range(45)))
                       for z in (0.05, 0.10, 0.15)))


def unknown(reason):
    return {"state": "Unknown", "reason": reason, "verified_cast_shadow": False}


def detect(p, max_samples=2048, max_rows=8):
    """Bounded horizontal-strip demonstrator, NOT a full 2D map detector.

    Contrast only proposes an edge. Three rows must agree, each with one sharp
    transition and smooth shoulders. All metadata admission is conservative.
    """
    if len(p.rows) > min(max_rows, 8) or sum(map(len, p.rows)) > min(max_samples, 2048):
        return unknown("WorkLimit")
    if len(p.rows) < 3 or any(len(row) < 9 for row in p.rows):
        return unknown("InsufficientSupport")
    if not p.source_linear:
        return unknown("UnknownTransfer")
    if p.filter_radius_m is None:
        return unknown("UnknownFilter")
    if any(not math.isfinite(v) or v < 0 for v in (p.filter_radius_m, p.mapping_error_m)):
        return unknown("InvalidErrorBudget")
    if not math.isfinite(p.noise_bound_codes) or p.noise_bound_codes < 0:
        return unknown("UnknownNoise")
    if p.alternatives:
        return unknown("Alternative:" + ",".join(p.alternatives))
    anchor = p.rows[0][0]
    strips = []
    for row in p.rows:
        for s in row:
            if not s.mapped or not all(math.isfinite(v) for v in s.world + s.normal):
                return unknown("UnmappedGeometry")
            if (s.material, s.chart, s.style) != (anchor.material, anchor.chart, anchor.style):
                return unknown("MaterialChartStyleSeam")
            if (abs(dot(s.normal, s.normal) - 1) > 1e-6
                    or dot(s.normal, anchor.normal) < math.cos(0.05)
                    or s.normal[1] < math.cos(0.05)):
                return unknown("NormalEdge")
            if not isinstance(s.code, int) or not 2 < s.code < 253:
                return unknown("ClippingOrInvalidCode")
            if abs(s.world[1]) > 1e-9 or abs(s.world[2] - row[0].world[2]) > 1e-9:
                return unknown("UnsupportedReceiver")
        steps = [row[i + 1].world[0] - row[i].world[0] for i in range(len(row) - 1)]
        step = steps[0]
        if step <= 0 or max(abs(v - step) for v in steps) > 1e-9:
            return unknown("IrregularMapping")
        differences = [row[i + 1].code - row[i].code for i in range(len(row) - 1)]
        threshold = max(8, 8 * p.noise_bound_codes)
        edges = [i for i, v in enumerate(differences) if abs(v) >= threshold]
        if len(edges) != 1:
            return unknown("NoUniqueSharpTransition")
        i = edges[0]
        if i < 3 or i + 4 >= len(row):
            return unknown("InsufficientShoulders")
        jump = differences[i]
        shoulders = differences[i - 2:i] + differences[i + 1:i + 3]
        if max(map(abs, shoulders)) > abs(jump) / 4:
            return unknown("SmoothFalloffOrBroadTransition")
        band = step / 2 + p.mapping_error_m + p.filter_radius_m
        if band >= 2 * step:
            return unknown("UnresolvedFootprint")
        left, right = row[i - 2].world, row[i + 3].world
        centre = tuple((a + b) / 2 for a, b in zip(row[i].world, row[i + 1].world))
        strips.append({"world_m": centre, "band_m": band, "jump_codes": jump,
                       "bright_m": right if jump > 0 else left,
                       "dark_m": left if jump > 0 else right})
    if (max(s["world_m"][0] for s in strips) - min(s["world_m"][0] for s in strips) > 0.06
            or len({s["jump_codes"] > 0 for s in strips}) != 1):
        return unknown("IncoherentRows")
    return {"state": "PossibleContrast", "strips": strips, "verified_cast_shadow": False,
            "alternatives": ["SpotCone", "IndirectTransition", "OverlappingSources", "AuthoredGain"]}


def associate(source, detected, beam=BEAM, complete=True, opaque=True,
              correspondence=False, max_rays=128, caster_edge=LEFT_EDGE):
    """Check observed bright/dark ordering and hit identity, conditionally.

    A failed supplied correspondence can reject that hypothesis. An automatic
    unassigned edge is Unknown and cannot veto a source or all configurations.
    """
    if detected["state"] != "PossibleContrast":
        return unknown(detected.get("reason", "NoEdge"))
    strips = detected["strips"]
    if 2 * len(strips) > min(max_rays, 128):
        return unknown("WorkLimit")
    if caster_edge is None:
        return unknown("UnassignedCaster")
    try:
        a, b = (project(source, c) for c in caster_edge)
    except ValueError:
        return dict(unknown("WrongOrdering"), hard_veto=False)
    rays, mismatch, unresolved, residuals = [], False, False, []
    for s in strips:
        bright = trace(s["bright_m"], source, beam, complete, opaque)
        dark = trace(s["dark_m"], source, beam, complete, opaque)
        rays.append({"bright": bright, "dark": dark})
        unresolved |= "Unknown" in (bright[0], dark[0])
        mismatch |= bright[0] != "Visible" or dark != ("Blocked", "beam")
        # Finite projected edge, not just an infinite source/caster plane.
        ab = sub(b, a)
        denom = dot(ab, ab)
        if denom <= 1e-20:
            return unknown("DegenerateCaster")
        t = max(0.0, min(1.0, dot(sub(s["world_m"], a), ab) / denom))
        nearest = tuple(a[k] + t * ab[k] for k in range(3))
        residuals.append(distance(nearest, s["world_m"]))
        mismatch |= residuals[-1] > s["band_m"]
    if unresolved:
        return dict(unknown("UnknownGeometryOrOptics"), rays=rays)
    state = "Incompatible" if correspondence and mismatch else "Unknown" if mismatch else "Compatible"
    return {"state": state, "reason": "WrongPatternOrCaster" if mismatch else "ConditionalPattern",
            "rays": rays, "projected_residuals_m": residuals,
            "verified_cast_shadow": False, "hard_veto": correspondence and mismatch}


def grid():
    return [tuple(round(v, 8) for v in p) for p in itertools.product(
        (-0.8 + i * 0.1 for i in range(17)),
        (3.0 + i * 0.1 for i in range(31)),
        (2.0 + i * 0.1 for i in range(17)))]


def fine_grid():
    # Local uncertainty illustration, not an exhaustive global proof.
    return [tuple(round(v, 8) for v in p) for p in itertools.product(
        (0.1 + i * 0.01 for i in range(21)),
        (3.8 + i * 0.02 for i in range(21)),
        (2.6 + i * 0.02 for i in range(21)))]


def retain(candidates, correspondences, band=BAND_M, max_candidates=MAX_CANDIDATES):
    if len(candidates) > min(max_candidates, MAX_CANDIDATES):
        raise ValueError("WorkLimit: candidate enumeration incomplete")
    if not math.isfinite(band) or band < 0:
        raise ValueError("invalid receiver-space band")
    if not 1 <= len(correspondences) <= 8:
        raise ValueError("bounded nonempty oracle correspondence list required")
    kept = []
    for source in candidates:
        try:
            predictions = [project(source, c) for c, _ in correspondences]
        except ValueError:
            continue
        if all(distance(p, s) <= band + 1e-12 for p, (_, s) in zip(predictions, correspondences)):
            kept.append(source)
    return kept


def summary(candidates, truth):
    if not candidates:
        return {"count": 0}
    return {"count": len(candidates), "max_truth_distance_m": max(distance(p, truth) for p in candidates),
            "bounds_m": [[min(p[k] for p in candidates), max(p[k] for p in candidates)] for k in range(3)]}


def candidate_study():
    # Intentionally weak shading coverage: amplitude is fitted at the centre;
    # two neighbouring observations use a fixed one-byte tolerance.
    probes = ((-1.51, 0, 1), (-1.5, 0, 1), (-1.49, 0, 1))
    observed = [code8(signal(p)) for p in probes]
    candidates, amplitudes = [], {}
    for source in grid():
        if any(trace(p, source)[0] != "Visible" for p in probes):
            continue
        amplitude = (observed[1] / 255 - FILL) / response(source, probes[1])
        codes = [code8(FILL + amplitude * response(source, p)) for p in probes]
        if max(abs(a - b) for a, b in zip(codes, observed)) <= 1:
            candidates.append(source)
            amplitudes[source] = amplitude
    oracle = [(c, project(TRUTH, c)) for c in CASTERS]
    one = retain(candidates, oracle[:1])
    two = retain(candidates, oracle)
    # Parameter selection uses only training measurements. Freeze before heldout.
    baseline = candidates[0]  # explicit deterministic tie, not a claim about a fitter
    best = min(two, key=lambda p: sum(distance(project(p, c), s) ** 2 for c, s in oracle))
    heldout = [(x, 0.0, z) for x in (-1.2, -0.8, -0.4, 0, 0.4, 0.8, 1.2)
               for z in (-0.7, -0.3, 0.1, 0.5, 0.9)]
    def heldout_error(source):
        mismatches = sum(trace(p, source)[0] != trace(p, TRUTH)[0] for p in heldout)
        mse = sum((code8(FILL + amplitudes[source] * response(source, p)
                         * (trace(p, source)[0] == "Visible")) - code8(signal(p))) ** 2
                  for p in heldout) / len(heldout)
        return {"visibility_mismatches": mismatches, "rgb8_mse": mse}
    # Perturbed endpoint measurements remain bands, not zero-error assertions.
    perturbed = [(c, (s[0] + 0.008, s[1], s[2] - 0.006)) for c, s in oracle]
    noisy = retain(candidates, perturbed)
    fine = retain(fine_grid(), oracle)
    return {"observed_training_codes": observed, "oracle": oracle, "band_m": BAND_M,
            "grid_count": len(grid()), "photometry": summary(candidates, TRUTH),
            "one_endpoint": summary(one, TRUTH), "two_endpoints": summary(two, TRUTH),
            "perturbed_endpoints": summary(noisy, TRUTH), "truth_retained_after_perturbation": TRUTH in noisy,
            "local_fine_grid": summary(fine, TRUTH), "local_grid_spacing_m": [0.01, 0.02, 0.02],
            "baseline_tie_m": baseline, "baseline_error_m": distance(baseline, TRUTH),
            "selected_m": best, "selected_error_m": distance(best, TRUTH),
            "heldout_count": len(heldout), "baseline_heldout": heldout_error(baseline),
            "selected_heldout": heldout_error(best)}


def two_source_study():
    results = []
    for source in (TRUTH, SECOND):
        oracle = [(c, project(source, c)) for c in CASTERS]
        p = intersect_caster_lines(oracle)
        results.append({"truth_m": source, "intersection_m": p, "error_m": distance(p, source),
                        "grid": summary(retain(grid(), oracle), source)})
    # Fully illuminated by the second lamp, inside the first lamp's shadow.
    receiver = (-0.5, 0.0, 0.1)
    return {"oracle_source_labels_supplied": True, "sources": results,
            "mixed_receiver_m": receiver,
            "visibility": [trace(receiver, source)[0] for source in (TRUTH, SECOND)],
            "mixed_signal": signal(receiver, ((TRUTH, 12.0), (SECOND, 7.0))), "fill": FILL}


def diagnostics():
    p = patch()
    detection = detect(p)
    mutate = lambda f: replace(p, rows=tuple(tuple(f(s, i) for i, s in enumerate(row)) for row in p.rows))
    cases = {
        "actual_beam": associate(TRUTH, detection, correspondence=True),
        "wrong_source": associate((0.8, 5.8, 3.6), detection, correspondence=True),
        "no_caster_angular_mask": associate(TRUTH, detection, beam=None, correspondence=True),
        "no_correspondence_no_caster": associate(TRUTH, detection, beam=None),
        "missing_geometry": associate(TRUTH, detection, complete=False, correspondence=True),
        "unknown_optics": associate(TRUTH, detection, opaque=False, correspondence=True),
        "ray_budget": associate(TRUTH, detection, max_rays=5, correspondence=True),
        "unknown_transfer": detect(replace(p, source_linear=False)),
        "unknown_filter": detect(replace(p, filter_radius_m=None)),
        "wide_filter": detect(replace(p, filter_radius_m=0.1)),
        "seam": detect(mutate(lambda s, i: replace(s, chart=2 if i > 20 else 1))),
        "material": detect(mutate(lambda s, i: replace(s, material=2 if i > 20 else 1))),
        "normal": detect(mutate(lambda s, i: replace(s, normal=(0, 0, 1) if i > 20 else s.normal))),
        "saturation": detect(mutate(lambda s, i: replace(s, code=255 if i == 0 else s.code))),
        "quantization_only": detect(mutate(lambda s, i: replace(s, code=100 + i % 2))),
        "smooth_falloff": detect(mutate(lambda s, i: replace(s, code=80 + i))),
        "moving_caster": detect(replace(p, alternatives=("MovingCaster",))),
        "indirect_unresolved": detect(replace(p, alternatives=("IndirectTransition",))),
        "sample_budget": detect(p, max_samples=10),
    }
    # Same source distribution except angular emission mask, no beam present.
    # Compare all three rows, not merely the two samples at the contrast edge.
    identical = all(s.code == code8(signal(s.world, beam=None, angular_mask=True))
                    for row in p.rows for s in row)
    return {"detection": detection, "cases": cases, "counterexample_byte_identical": identical,
            "sample_count": sum(map(len, p.rows)),
            "caveat": "Compatible means conditional geometry agreement, never a verified historical shadow"}


def report():
    return {"schema": "org.openjk.asr05-cast-shadow-study/1", "units": "metres, relative synthetic signal",
            "scope": "owned analytical fixture; no BSP/compiler or historical lamp certification",
            "beam_bounds_m": BEAM, "truth_m": TRUTH, "single_source": candidate_study(),
            "two_sources": two_source_study(), "diagnostics": diagnostics()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = (json.dumps(report(), sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print("SHA256", hashlib.sha256(data).hexdigest())
    else:
        print(data.decode("utf-8"), end="")


if __name__ == "__main__":
    main()
