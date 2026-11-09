#!/usr/bin/env python3
"""ASR13 conditional single-dominant-emitter discovery from ORIGINAL grid bytes.

Standard library only. Diagnostic JSON is not a canonical lighting asset.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess

VERSION = "org.openjk.original-grid-triangulation.v1"
# Frozen search policy, independent of evaluator truth and validation observations.
POLICY = dict(min_records=4, fit_gate_degrees=6., neighbour_degrees=70.,
              colour_chord=.35, max_condition=100., max_pairs=512,
              max_candidates_per_component=4, max_search_records=192,
              max_components=128, min_consensus=.60, split="sha256-record-v1")


def add(a, b): return [x+y for x, y in zip(a, b)]
def sub(a, b): return [x-y for x, y in zip(a, b)]
def mul(a, s): return [x*s for x in a]
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def norm(a): return math.sqrt(dot(a, a))
def unit(a): return mul(a, 1/norm(a)) if norm(a) > 1e-12 else [0., 0., 0.]
def angle(a, b): return math.acos(max(-1., min(1., dot(unit(a), unit(b)))))
def mean(values): return sum(values)/len(values) if values else None
def rms(values): return math.sqrt(mean([v*v for v in values])) if values else None
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False)+"\n", encoding="utf-8")


def eigen(matrix):
    """Symmetric 3x3 Jacobi eigenpairs, ascending; deterministic tie order."""
    a = [r[:] for r in matrix]
    v = [[float(i == j) for j in range(3)] for i in range(3)]
    for _ in range(40):
        p, q = max(((0, 1), (0, 2), (1, 2)), key=lambda ij: abs(a[ij[0]][ij[1]]))
        if abs(a[p][q]) <= 1e-15*max(1., max(abs(a[i][i]) for i in range(3))):
            break
        theta = .5*math.atan2(2*a[p][q], a[q][q]-a[p][p])
        c, s = math.cos(theta), math.sin(theta)
        for k in range(3):
            if k not in (p, q):
                ap, aq = a[k][p], a[k][q]
                a[k][p] = a[p][k] = c*ap-s*aq
                a[k][q] = a[q][k] = s*ap+c*aq
        ap, aq, off = a[p][p], a[q][q], a[p][q]
        a[p][p], a[q][q] = c*c*ap-2*c*s*off+s*s*aq, s*s*ap+2*c*s*off+c*c*aq
        a[p][q] = a[q][p] = 0.
        for k in range(3):
            vp, vq = v[k][p], v[k][q]
            v[k][p], v[k][q] = c*vp-s*vq, s*vp+c*vq
    return sorted([(a[i][i], [v[k][i] for k in range(3)]) for i in range(3)], key=lambda e: e[0])


def solve(a, b):
    ev = eigen(a)
    if ev[0][0] <= 1e-10*max(ev[-1][0], 1e-30):
        return None
    x = [0., 0., 0.]
    for value, axis in ev:
        x = add(x, mul(axis, dot(axis, b)/value))
    return x


def normal(rows, weights=None):
    a, b = [[0.]*3 for _ in range(3)], [0.]*3
    for n, row in enumerate(rows):
        d, p = row["direction"], row["position"]
        w = weights[n] if weights else 1.
        for i in range(3):
            for j in range(3):
                q = w*(float(i == j)-d[i]*d[j])
                a[i][j] += q
                b[i] += q*p[j]
    return a, b


def fit(rows):
    a, b = normal(rows)
    x = solve(a, b)
    if x is None:
        return None
    # Angular IRLS: distance normalization prevents distant observations dominating.
    for _ in range(6):
        errors = [angle(sub(x, r["position"]), r["direction"]) for r in rows]
        weights = [min(1., math.radians(3)/max(e, 1e-9))/max(1., norm(sub(x, r["position"])))**2
                   for e, r in zip(errors, rows)]
        a, b = normal(rows, weights)
        update = solve(a, b)
        if update is None:
            break
        x = update
    return x


def residual(row, position):
    ray = sub(position, row["position"])
    return angle(ray, row["direction"]) if dot(ray, row["direction"]) > 1e-5 else math.pi


def role(key, map_key):
    bucket = int(hashlib.sha256(("asr13-split-v1/"+map_key+"/"+key).encode()).hexdigest()[:8], 16) % 10
    return "heldout" if bucket == 0 else "validation" if bucket == 1 else "train"


def prepare(rows, metadata, interpretation):
    result, rejected = [], Counter()
    for raw in rows:
        why = None
        if not raw["present"]: why = "missing_original_record"
        elif raw.get("issues", 0) & 64: why = "malformed_style_gap"
        elif raw["space"] != "free" or raw["boundary"]: why = "geometry_not_known_free"
        elif len(raw["styles"]) != 1: why = "shared_direction_multiple_or_missing_styles"
        elif raw["styles"][0]["weak"]: why = "weak_directed"
        elif interpretation == "writer255_merge4" and not raw["writer_possible"]: why = "unsupported_writer_code"
        if why:
            rejected[why] += 1
            continue
        row = dict(raw)
        row["direction"] = unit(raw["writer255" if interpretation == "writer255_merge4" else "consumer256"])
        row["colour"] = unit(raw["styles"][0]["directed"])
        row["relative_strength"] = sum(raw["styles"][0]["directed"])/(3*255.)
        row["role"] = role(raw["correlation_key"], metadata["map_sha256"])
        result.append(row)
    return sorted(result, key=lambda r: r["ordinal"]), dict(rejected)


class Bridge:
    def __init__(self, tool, capture):
        self.process = subprocess.Popen([str(Path(tool).resolve()), str(Path(capture).resolve())],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.metadata = json.loads(self.process.stdout.readline())
        except Exception:
            self.close()
            raise RuntimeError("Original-grid geometry bridge failed to load capture")
        self.cache = {}

    def query(self, command, *values):
        line = command+" "+" ".join(format(float(v), ".17g") for v in values)
        if line not in self.cache:
            self.process.stdin.write(line+"\n")
            self.process.stdin.flush()
            answer = self.process.stdout.readline()
            if not answer:
                raise RuntimeError("Geometry bridge failed for "+command)
            self.cache[line] = json.loads(answer)
        return self.cache[line]

    def ray(self, a, b): return self.query("ray", *a, *b)
    def point(self, p): return self.query("point", *p)
    def box(self, lo, hi): return self.query("box", *lo, *hi)

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
            finally:
                self.process.stdin.close()
                self.process.wait(timeout=30)
        if self.process.returncode:
            raise RuntimeError("Geometry bridge exited with "+str(self.process.returncode))


def components(training, geometry):
    """Only training directions/colors/positions participate in connectivity."""
    index = {tuple(r["xyz"]): i for i, r in enumerate(training)}
    parents = list(range(len(training)))
    def root(i):
        while i != parents[i]:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
    edges = Counter()
    for i, row in enumerate(training):
        for offset in itertools.product((-1, 0, 1), repeat=3):
            xyz = tuple(a+b for a, b in zip(row["xyz"], offset))
            j = index.get(xyz, -1)
            if j <= i:
                continue
            other = training[j]
            if angle(row["direction"], other["direction"]) > math.radians(POLICY["neighbour_degrees"]):
                edges["direction_disagreement"] += 1
                continue
            if norm(sub(row["colour"], other["colour"])) > POLICY["colour_chord"] or row["styles"][0]["id"] != other["styles"][0]["id"]:
                edges["colour_or_style_disagreement"] += 1
                continue
            path = geometry.ray(row["position"], other["position"])
            edges[path["static_state"]] += 1
            if path["static_state"] == "clear":
                a, b = root(i), root(j)
                parents[max(a, b)] = min(a, b)
    groups = {}
    for i, row in enumerate(training): groups.setdefault(root(i), []).append(row)
    return list(groups.values()), dict(edges)


def independent(rows):
    # One representative per byte-identical record, never one independent vote per reference.
    chosen = {}
    for row in rows: chosen.setdefault(row["correlation_key"], row)
    ordered = sorted(chosen.values(), key=lambda r: r["correlation_key"])
    limit = POLICY["max_search_records"]
    # Stable even coverage of record hashes, explicitly reported if limited.
    return ordered if len(ordered) <= limit else [ordered[i*len(ordered)//limit] for i in range(limit)]


def uncertain_region(rows, reason, component):
    centre = mul([sum(r["position"][k] for r in rows) for k in range(3)], 1/len(rows))
    incoming = unit([sum(r["direction"][k] for r in rows) for k in range(3)])
    return dict(component=component, status="unknown", reason=reason, position=None,
                region=dict(kind="positive_direction_depth_unknown", origin=centre, direction=incoming,
                            depth_lower_world=0, depth_upper_world=None, scope="diagnostic_axis_only_not_certified_volume"),
                independent_records=len(rows))


def search_group(group, geometry, interpretation, component):
    rows = independent(group)
    if len(rows) < POLICY["min_records"]:
        return [], [uncertain_region(rows, "insufficient_independent_records", component)]
    a, _ = normal(rows)
    ev = eigen(a)
    condition = ev[-1][0]/max(ev[0][0], 1e-30)
    if condition > POLICY["max_condition"]:
        unknown = uncertain_region(rows, "nearly_parallel_or_rank_deficient", component)
        unknown["normal_condition"] = condition
        return [], [unknown]
    pairs = list(itertools.combinations(range(len(rows)), 2))
    if len(pairs) > POLICY["max_pairs"]:
        pairs = [pairs[i*len(pairs)//POLICY["max_pairs"]] for i in range(POLICY["max_pairs"])]
    seeds = [fit(rows)]
    for i, j in pairs:
        if angle(rows[i]["direction"], rows[j]["direction"]) > math.radians(8):
            seeds.append(fit([rows[i], rows[j]]))
    gate = math.radians(POLICY["fit_gate_degrees"])
    proposals, initial_supports = {}, set()
    for position in seeds:
        if position is None or not all(math.isfinite(x) and abs(x) < 1e7 for x in position):
            continue
        support = tuple(i for i, row in enumerate(rows) if residual(row, position) <= gate)
        if len(support) < POLICY["min_records"]:
            continue
        if support in initial_supports:
            continue
        initial_supports.add(support)
        selected = [rows[i] for i in support]
        position = fit(selected)
        if position is None:
            continue
        support = tuple(i for i, row in enumerate(rows) if residual(row, position) <= gate)
        if len(support) < POLICY["min_records"]:
            continue
        errors = [residual(rows[i], position) for i in support]
        key = (-len(support), rms(errors), tuple(position))
        if support not in proposals or key < proposals[support][0]:
            proposals[support] = (key, position)
    candidates, unknowns = [], []
    used = set()
    for support, (_, position) in sorted(proposals.items(), key=lambda item: item[1][0]):
        # Suppress the same solution; keep disjoint/outlier alternatives separately.
        if any(len(set(support) & set(s))/min(len(support), len(s)) > .7 for s in used):
            continue
        selected = [rows[i] for i in support]
        errors = [residual(r, position) for r in selected]
        if any(e > gate for e in errors):
            continue
        point = geometry.point(position)
        paths = [geometry.ray(r["position"], position) for r in selected]
        path_counts = dict(Counter(p["static_state"] for p in paths))
        if point["space"] == "solid" or path_counts.get("blocked", 0):
            continue
        h, _ = normal(selected, [1/max(norm(sub(position, r["position"])), 1.)**2/len(selected) for r in selected])
        eig = eigen(h)
        cond = eig[-1][0]/max(eig[0][0], 1e-30)
        # Conservative angular-code allowance, NOT a stochastic covariance.
        bound = (12*2*math.pi/255 if interpretation == "writer255_merge4" else 2*2*math.pi/256+.02)
        axes = [bound/math.sqrt(max(v, 1e-30)) for v, _ in eig]
        median_distance = sorted(norm(sub(position, r["position"])) for r in selected)[len(selected)//2]
        depth_unknown = cond > POLICY["max_condition"] or axes[0] >= median_distance
        extent = [math.sqrt(sum((length*axis[k])**2 for length, (_, axis) in zip(axes, eig))) for k in range(3)]
        region_space = geometry.box(sub(position, extent), add(position, extent))["space"] if not depth_unknown else "unknown"
        consensus = len(support)/len(rows)
        reasons = ["single_emitter_is_hypothesis", "reflected_or_aggregate_virtual_source_not_excluded",
                   "compiler_sample_nudge_and_interpolation_unknown"]
        if interpretation != "writer255_merge4": reasons.append("compression_bound_unknown")
        if any(p["dynamic_possible"] for p in paths): reasons.append("dynamic_bake_occupancy_unknown")
        if point["space"] != "free" or point["boundary"] or path_counts.get("unknown", 0): reasons.append("candidate_geometry_unknown")
        if consensus < POLICY["min_consensus"]: reasons.append("single_emitter_consensus_failed")
        if depth_unknown: reasons.append("depth_uncertain_under_angle_allowance")
        status = "conditional_location" if not depth_unknown and "candidate_geometry_unknown" not in reasons and consensus >= POLICY["min_consensus"] else "unknown"
        candidates.append(dict(component=component, status=status, confidence="conditional_only" if status == "conditional_location" else "unknown",
                               position=position, single_emitter_certified=False, historical_source_certified=False,
                               reasons=reasons, angular_rms_degrees=math.degrees(rms(errors)),
                               angular_max_degrees=math.degrees(max(errors)), normal_condition=condition,
                               component_angular_rms_degrees=math.degrees(rms([residual(r, position) for r in rows])),
                               angular_condition=cond, positive_ray_distances_world=[dot(sub(position, r["position"]), r["direction"]) for r in selected],
                               independent_records=len(selected), component_records=len(rows), consensus_fraction=consensus,
                               fit_ordinals=[r["ordinal"] for r in selected], static_path_counts=path_counts,
                               relative_colour_residual=rms([norm(sub(r["colour"], unit([sum(s["colour"][k] for s in selected) for k in range(3)]))) for r in selected]),
                               encoded_mean_rgb_range=[min(r["relative_strength"] for r in selected), max(r["relative_strength"] for r in selected)],
                               region=dict(kind="unbounded_depth" if depth_unknown else "local_linearized_angular_envelope",
                                           axes_world=None if depth_unknown else axes, axes_directions=[axis for _, axis in eig],
                                           angle_allowance_degrees=math.degrees(bound), compression_bound_known=interpretation == "writer255_merge4",
                                           uncertainty_scope="encoding_and_conditional_merge_only" if interpretation == "writer255_merge4" else "encoding_only_compression_unknown",
                                           full_uncertainty_known=False, global_containment_certified=False, box_space=region_space)))
        used.add(support)
        if len(candidates) == POLICY["max_candidates_per_component"]:
            break
    if not candidates:
        unknowns.append(uncertain_region(rows, "no_positive_coherent_geometry_supported_intersection", component))
    return candidates, unknowns


def discover(prepared_training, geometry, interpretation):
    if any(r["role"] != "train" for r in prepared_training):
        raise ValueError("Search accepts training observations only")
    prepared_training = sorted(prepared_training, key=lambda r: r["ordinal"])
    groups, edges = components(prepared_training, geometry)
    candidates, unknowns = [], []
    if not prepared_training:
        unknowns.append(dict(status="unknown", reason="no_training_observations", position=None))
    for i, group in enumerate(groups[:POLICY["max_components"]]):
        found, missing = search_group(group, geometry, interpretation, i)
        candidates += found
        unknowns += missing
    if len(groups) > POLICY["max_components"]:
        unknowns.append(dict(status="unknown", reason="component_budget_exhausted",
                             skipped_components=len(groups)-POLICY["max_components"]))
    candidates.sort(key=lambda c: (c["status"] != "conditional_location", -c["consensus_fraction"],
                                    -c["independent_records"], c["angular_rms_degrees"], c["component"], c["position"]))
    for i, candidate in enumerate(candidates): candidate["rank"] = i+1
    return dict(candidates=candidates, unknowns=unknowns, connectivity=edges,
                components=len(groups), component_limit_reached=len(groups) > POLICY["max_components"],
                search_record_limit_reached=any(len({r["correlation_key"] for r in g}) > POLICY["max_search_records"] for g in groups),
                status="conditional_candidates" if any(c["status"] == "conditional_location" for c in candidates) else "unknown")


def evaluate(frozen, prepared, geometry):
    """Separate post-search residuals. Never changes fit, rank, selection or confidence."""
    result = []
    for candidate in frozen["candidates"]:
        roles = {}
        fit_ids = set(candidate["fit_ordinals"])
        support = [r for r in prepared if r["ordinal"] in fit_ids]
        colour = unit([sum(r["colour"][k] for r in support) for k in range(3)])
        # Evaluation has a frozen, generous spatial support box; no nearest-candidate assignment.
        lo = [min(r["position"][k] for r in support)-128 for k in range(3)]
        hi = [max(r["position"][k] for r in support)+128 for k in range(3)]
        for partition in ("validation", "heldout"):
            selected = independent([r for r in prepared if r["role"] == partition and
                                    all(lo[k] <= r["position"][k] <= hi[k] for k in range(3))])
            paths = [geometry.ray(r["position"], candidate["position"]) for r in selected]
            errors = [residual(r, candidate["position"]) for r in selected]
            roles[partition] = dict(records=len(selected), angular_rms_degrees=math.degrees(rms(errors)) if errors else None,
                                    angular_max_degrees=math.degrees(max(errors)) if errors else None,
                                    colour_chord_rms=rms([norm(sub(r["colour"], colour)) for r in selected]),
                                    nominal_direction_gate="fails" if errors and math.degrees(rms(errors)) > POLICY["fit_gate_degrees"] else
                                    "insufficient_records" if len(selected) < POLICY["min_records"] else "passes_conditionally",
                                    static_path_counts=dict(Counter(p["static_state"] for p in paths)))
        result.append(dict(rank=candidate["rank"], residuals=roles))
    return result


def run(bridge, roi, interpretation):
    raw = bridge.query("cells", *roi)["rows"]
    prepared, rejected = prepare(raw, bridge.metadata, interpretation)
    frozen = discover([r for r in prepared if r["role"] == "train"], bridge, interpretation)
    # Hash records before any evaluator code is called, making leakage auditable.
    frozen_hash = hashlib.sha256(json.dumps(frozen, sort_keys=True, allow_nan=False).encode()).hexdigest()
    evaluation = evaluate(frozen, prepared, bridge)
    assert frozen_hash == hashlib.sha256(json.dumps(frozen, sort_keys=True, allow_nan=False).encode()).hexdigest()
    return dict(schema=VERSION, policy=POLICY, source=bridge.metadata, roi_world=roi,
                interpretation=interpretation, colour_space="encoded_rgb8_relative", absolute_calibration_known=False,
                generated_probes=0, input_cells=len(raw), eligible_cells=len(prepared), rejected=rejected,
                roles=dict(Counter(r["role"] for r in prepared)), frozen_search_sha256=frozen_hash,
                search=frozen, evaluation=evaluation), prepared


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tool", required=True, type=Path)
    p.add_argument("--capture", required=True, type=Path)
    p.add_argument("--roi", nargs=6, required=True, type=float, metavar=("X0", "Y0", "Z0", "X1", "Y1", "Z1"))
    p.add_argument("--interpretation", choices=("consumer256_unknown", "writer255_merge4"), default="consumer256_unknown")
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--diagnostic", type=Path, help="SVG geometry/direction projections; includes private map geometry")
    a = p.parse_args()
    if not all(math.isfinite(v) for v in a.roi) or any(a.roi[k] >= a.roi[k+3] for k in range(3)):
        p.error("ROI must have finite positive extents")
    bridge = Bridge(a.tool, a.capture)
    try:
        report, prepared = run(bridge, a.roi, a.interpretation)
        report["capture_sha256"], report["tool_sha256"] = sha(a.capture), sha(a.tool)
        if a.diagnostic:
            from diagnostic import render
            render(a.diagnostic, report, prepared, bridge.query("mesh", *a.roi))
        write(a.output, report)
    finally:
        bridge.close()
    print(json.dumps(dict(status=report["search"]["status"], candidates=len(report["search"]["candidates"]),
                          unknowns=len(report["search"]["unknowns"]))))


if __name__ == "__main__":
    main()
