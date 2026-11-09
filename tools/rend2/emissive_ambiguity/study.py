#!/usr/bin/env python3
"""ASR06: conditional source-versus-reflector screening, never an asset writer.

Complete analytical rectangles in metres; a declared synthetic linear quantizer.
No BSP reader, historical compiler replay, trained ESR-NeRF or runtime consumer.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from dataclasses import asdict, dataclass, replace
import hashlib
import json
import math
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'inverse_gi'))
from transport import Edge, Transfer  # Existing passive power/reflectance guards.

ZERO = (0., 0., 0.)
POLICY = dict(quadrature_order=8, reference_order=24, encoded_gain=12.,
              maximum_emission=64., maximum_interval_rmse_bytes=.75,
              minimum_improvement_bytes=2., brightness_threshold_rgb8=180)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def unit(a):
    size = math.sqrt(dot(a, a))
    return tuple(v / size for v in a) if size > 1e-14 else None


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


@dataclass(frozen=True)
class Patch:
    identity: str
    center: tuple
    width: float = .8
    height: float = .8
    # All fixture patches face +X. These are exact rectangles, not fitted hulls.

    def samples(self, order):
        if not 1 <= order <= 32:
            raise ValueError('Bounded finite-area quadrature required')
        return tuple((self.center[0], self.center[1] + self.width * ((i + .5) / order - .5),
                      self.center[2] + self.height * ((j + .5) / order - .5))
                     for i in range(order) for j in range(order))

    @property
    def area(self):
        return self.width * self.height


@dataclass(frozen=True)
class Receiver:
    position: tuple
    normal: tuple = (0., 0., 1.)
    area: float = .04  # 0.2 m support; split separation exceeds footprint width.


@dataclass(frozen=True)
class Scene:
    name: str
    candidate: Patch
    surfaces: tuple
    reflector: Patch
    rho: tuple
    primary_position: tuple
    primary_rgb: tuple
    glow_stage: bool
    bake_emission_hint: bool
    provenance: tuple
    doorway: bool = False
    geometry_known: bool = True
    material_known: bool = True
    root_known: bool = True
    encoding_known: bool = True


@dataclass(frozen=True)
class Observations:
    receivers: tuple
    rgb8: tuple


@dataclass(frozen=True)
class Evidence:
    scene: Scene
    training: Observations
    appearance_rgb8: tuple  # Display/material appearance, NEVER an irradiance target.


@dataclass(frozen=True)
class Fixture:
    evidence: Evidence
    heldout: Observations  # The fit/decision interface cannot access this field.
    truth_candidate_emits: bool  # Scoring only; never supplied to diagnose().
    truth_emission: tuple
    ideal_grid_directions: tuple  # Evaluation-only synthetic proxy, not JA decode.


class Unavailable(ValueError):
    pass


def visible(scene, source, receiver):
    """Exact segment/screen-with-aperture fixture. Unknown is never open/zero."""
    if not scene.geometry_known:
        raise Unavailable('UnknownGeometry')
    if not scene.doorway:
        return True
    dx = receiver[0] - source[0]
    if abs(dx) < 1e-14:
        return True  # Parallel, disjoint segments in this fixture only.
    t = -source[0] / dx
    if not 0 < t < 1:
        return True  # Source/receiver support endpoints are not intervening walls.
    y = source[1] + t * (receiver[1] - source[1])
    z = source[2] + t * (receiver[2] - source[2])
    # Full screen at X=0, aperture |Y|<.65, 0<Z<2.8. Edges are opaque.
    return abs(y) < .65 and 0 < z < 2.8


def point_irradiance(scene, receiver):
    if not scene.root_known:
        raise Unavailable('MissingPrimary')
    d = sub(scene.primary_position, receiver.position)
    r2 = dot(d, d)
    if r2 <= 1e-12:
        raise ValueError('Singular fixture source')
    factor = max(0., dot(receiver.normal, d)) / r2 ** 1.5
    if not visible(scene, scene.primary_position, receiver.position):
        factor = 0.
    return tuple(factor * v for v in scene.primary_rgb)


def kernel(scene, sender, receiver):
    d = sub(sender, receiver.position)
    r2 = dot(d, d)
    if r2 <= 1e-12:
        raise ValueError('Singular fixture link')
    return (max(0., -d[0]) * max(0., dot(receiver.normal, d)) / r2 ** 2
            if visible(scene, sender, receiver.position) else 0.)


def emission_column(scene, receivers, order):
    samples = scene.candidate.samples(order)
    area = scene.candidate.area / len(samples)
    return tuple(area * sum(kernel(scene, p, r) for p in samples) for r in receivers)


def passive(scene, receivers, order):
    """Discretized G4/G7, using the existing reciprocal passive Transfer class.

    Reflectance and primary are fixed; no free bounce/emission amplitude. Black
    receivers terminate paths. Far-separated receiver supports use midpoint
    evaluation; sender integration is refined independently for the reference.
    """
    if not scene.material_known:
        raise Unavailable('UnknownBakeReflectance')
    points = scene.reflector.samples(order)
    count = len(points)
    area = scene.reflector.area / count
    areas = (area,) * count + tuple(r.area for r in receivers)
    edges = []
    for i, p in enumerate(points):
        for j, receiver in enumerate(receivers, count):
            k = kernel(scene, p, receiver) / math.pi
            if k:
                edges.extend((Edge(i, j, k * receiver.area, k * receiver.area),
                              Edge(j, i, k * area, k * area)))
    transfer = Transfer(areas, tuple(edges), 'ASR06-complete-black-receiver-network')
    incident = tuple(point_irradiance(scene, Receiver(p, (1., 0., 0.))) for p in points)
    incident += (ZERO,) * len(receivers)
    rho = (scene.rho,) * count + (ZERO,) * len(receivers)
    lower, upper = transfer.apply(incident, rho)
    if lower != upper:
        raise ValueError('Exact fixture transfer expected')
    return tuple(tuple(row) for row in lower[count:])


def predict(scene, receivers, order, emission=ZERO):
    direct = tuple(point_irradiance(scene, r) for r in receivers)
    bounce = passive(scene, receivers, order)
    column = emission_column(scene, receivers, order)
    return tuple(add(add(d, b), tuple(k * v for v in emission))
                 for d, b, k in zip(direct, bounce, column))


def encode(value, gain=POLICY['encoded_gain']):
    if not math.isfinite(value) or value < 0 or not math.isfinite(gain) or gain <= 0:
        raise ValueError('Nonnegative finite synthetic irradiance and gain required')
    return min(255, math.floor(value * gain + .5))


def interval(value):
    if not isinstance(value, int) or not 0 <= value <= 255:
        raise ValueError('Encoded byte required')
    gain = POLICY['encoded_gain']
    return (max(0., (value - .5) / gain),
            math.inf if value == 255 else (value + .5) / gain)


def distance(value, byte):
    lo, hi = interval(byte)
    return max(lo - value, value - hi, 0.)


def score(prediction, observations):
    if not observations.rgb8 or len(prediction) != len(observations.rgb8):
        raise ValueError('Nonempty matching observation span required')
    errors = [distance(v, byte) * POLICY['encoded_gain']
              for row, raw in zip(prediction, observations.rgb8) for v, byte in zip(row, raw)]
    return math.sqrt(sum(v * v for v in errors) / len(errors))


def fit_emission(base, column, observations):
    """Bounded convex interval least squares; training observations only.

    Select the smallest amplitude at a flat minimum, including censored data.
    This is a hypothesis amplitude, never recovered historical luminance.
    """
    if not column or len(base) != len(column) or len(column) != len(observations.rgb8):
        raise ValueError('Matching fit spans required')
    result = []
    for c in range(3):
        def derivative(amplitude):
            value = 0.
            for b, k, raw in zip(base, column, observations.rgb8):
                lo, hi = interval(raw[c])
                y = b[c] + k * amplitude
                value += k * (y - min(hi, max(lo, y)))
            return value
        lo, hi = 0., POLICY['maximum_emission']
        if derivative(lo) >= 0:
            result.append(0.)
            continue
        for _ in range(64):
            mid = (lo + hi) / 2
            if derivative(mid) < 0:
                lo = mid
            else:
                hi = mid
        result.append((lo + hi) / 2)
    return tuple(result)


def with_emission(base, column, emission):
    return tuple(add(b, tuple(k * v for v in emission)) for b, k in zip(base, column))


def diagnose(evidence):
    scene, obs = evidence.scene, evidence.training
    result = dict(decision='AmbiguousSourceOrTransport', reasons=[],
                  exact_historical_source=False, evidence_provenance=scene.provenance,
                  glow_stage=scene.glow_stage, bake_emission_hint=scene.bake_emission_hint,
                  appearance_peak_rgb8=max(evidence.appearance_rgb8),
                  surface_supported=scene.candidate in scene.surfaces,
                  metrics=None, fitted_additional_emission=None)
    missing = [name for name, known in (
        ('UnknownGeometry', scene.geometry_known), ('UnknownBakeReflectance', scene.material_known),
        ('MissingPrimary', scene.root_known), ('UnknownObservationOperator', scene.encoding_known)) if not known]
    if missing:
        result['reasons'] = missing
        return result
    order = POLICY['quadrature_order']
    direct = tuple(point_irradiance(scene, r) for r in obs.receivers)
    base = tuple(add(d, b) for d, b in zip(direct, passive(scene, obs.receivers, order)))
    column = emission_column(scene, obs.receivers, order)
    fitted = fit_emission(base, column, obs)
    direct_fitted = fit_emission(direct, column, obs)
    direct_error, passive_error, extra_error = (score(p, obs) for p in (
        direct, base, with_emission(base, column, fitted)))
    result['metrics'] = dict(direct_rmse_bytes=direct_error, passive_rmse_bytes=passive_error,
        direct_plus_emitter_rmse_bytes=score(with_emission(direct, column, direct_fitted), obs),
        passive_plus_emitter_rmse_bytes=extra_error,
        direct_plus_emitter_amplitude=direct_fitted,
        candidate_nonzero_response_receivers=sum(k > 0 for k in column),
        independent_support_receivers=sum(max(distance(d[c], raw[c]) for c in range(3)) *
            POLICY['encoded_gain'] > POLICY['minimum_improvement_bytes'] and k > 0
            for d, raw, k in zip(base, obs.rgb8, column)))
    result['fitted_additional_emission'] = fitted
    tolerance = POLICY['maximum_interval_rmse_bytes']
    if not result['surface_supported']:
        result['decision'] = 'ApertureOrUnsupportedSurface'
        result['reasons'] = ['NoActualEmittingSurface', 'PreserveExternalTransportHypothesis']
    elif passive_error <= tolerance:
        result['decision'] = 'PassiveTransportPlausible'
        result['reasons'] = ['NoAdditionalEmitterRequired', 'ConditionalOnFixedRootAndMaterial']
    elif (scene.bake_emission_hint and extra_error <= tolerance and
          passive_error - extra_error >= POLICY['minimum_improvement_bytes'] and
          result['metrics']['independent_support_receivers'] >= 2):
        result['decision'] = 'ConditionalEmitterCandidate'
        result['reasons'] = ['BakeEmissionHint', 'OriginalSurfaceSupport', 'IndependentReceiverResponse',
                             'AlternativesNotExhausted']
    else:
        result['reasons'] = ['InsufficientEmissionOrTransportEvidence']
    return result


def ideal_directions(scene, receivers, order, emission):
    """Ideal radiance-weighted directional moments, NOT JA directed RGB/angles.

    This optimistic proxy tests whether direction could distinguish a reflector
    from an emitter at the same last scattering surface. Mixtures can cancel.
    """
    points = scene.reflector.samples(order)
    area = scene.reflector.area / len(points)
    outgoing = [tuple(rho * e / math.pi for rho, e in zip(scene.rho,
                point_irradiance(scene, Receiver(p, (1., 0., 0.))))) for p in points]
    directions = []
    for r in receivers:
        d = sub(scene.primary_position, r.position)
        w = sum(scene.primary_rgb) / dot(d, d) if visible(scene, scene.primary_position, r.position) else 0.
        moment = tuple(w * v for v in unit(d))
        for p, reflected in zip(points, outgoing):
            d = sub(p, r.position)
            w = (area * max(0., -d[0]) / dot(d, d) ** 1.5 * sum(add(reflected, emission))
                 if visible(scene, p, r.position) else 0.)
            moment = add(moment, tuple(w * v for v in unit(d)))
        directions.append(unit(moment))
    return tuple(directions)


def receiver_sets():
    train = tuple(Receiver((x, y, 0.)) for x in (1., 1.6) for y in (-1., 0., 1.))
    held = tuple(Receiver((x, y, 0. if x == 3 else .5),
                         (0., 0., 1.) if x == 3 else (-1., 0., 0.))
                 for x in (3., 4.) for y in (-1., 0., 1.))
    return train, held


def fixture(kind, scale=1.):
    candidate = Patch('original-panel', (0., 0., 2.))
    reflector = candidate
    surfaces = (candidate,)
    rho, primary, position = (.5, .5, .5), (40.,) * 3, (2., 0., 3.5)
    emission, glow, hint, doorway = ZERO, False, False, False
    if kind == 'emissive_panel':
        emission, glow, hint = (12., 8., 4.), True, True
    elif kind == 'red_reflected_wall':
        rho, primary = (.8, .08, .04), (160.,) * 3
    elif kind == 'lit_doorway':
        candidate = Patch('opening-proposal', (0., 0., 1.6))
        reflector = Patch('original-back-wall', (-2., 0., 2.))
        surfaces, rho, primary, position = (reflector,), (.8,) * 3, (160.,) * 3, (-1., 0., 3.5)
        doorway = True
    elif kind == 'decorative_glow':
        glow = True  # Authored display effect, with NO bake-time surface-light hint.
    else:
        raise ValueError('Unknown fixture')
    primary = tuple(v * scale for v in primary)
    emission = tuple(v * scale for v in emission)
    raw = ('fixture.shader:glow' if glow else 'fixture.shader:diffuse',
           'fixture.shader:q3map_surfacelight 100' if hint else 'fixture.shader:no surfacelight',
           'fixture.reflectance:declared hypothesis; not inferred from current texture')
    scene = Scene(kind, candidate, surfaces, reflector, rho, position, primary, glow, hint, raw, doorway)
    train, held = receiver_sets()
    order = POLICY['reference_order']
    def observations(receivers):
        return Observations(receivers, tuple(tuple(encode(v) for v in row)
                            for row in predict(scene, receivers, order, emission)))
    # A separate preview tone-map. Decorative glow affects only this appearance.
    reflected = point_irradiance(scene, Receiver(reflector.center, (1., 0., 0.)))
    appearance = add(tuple(r * e / math.pi for r, e in zip(rho, reflected)), emission)
    if kind == 'decorative_glow':
        appearance = (20. * scale, 10. * scale, 3. * scale)
    rgb8 = tuple(math.floor(255 * v / (1 + v) + .5) for v in appearance)
    grid = ideal_directions(scene, held, POLICY['quadrature_order'], emission)
    evidence = Evidence(scene, observations(train), rgb8)
    return Fixture(evidence, observations(held), kind == 'emissive_panel', emission, grid)


def evaluate(f):
    evidence, scene = f.evidence, f.evidence.scene
    result = diagnose(evidence)  # Freeze training decision BEFORE held-out scoring.
    obs = f.heldout
    if result['metrics'] is not None:
        order = POLICY['quadrature_order']
        base = predict(scene, obs.receivers, order)
        column = emission_column(scene, obs.receivers, order)
        result['heldout_passive_rmse_bytes'] = score(base, obs)
        result['heldout_selected_rmse_bytes'] = score(with_emission(base, column,
            result['fitted_additional_emission'] if result['decision'] == 'ConditionalEmitterCandidate' else ZERO), obs)
        align = [dot(unit(sub(scene.candidate.center, r.position)), direction)
                 for r, direction in zip(obs.receivers, f.ideal_grid_directions) if direction is not None]
        result['ideal_direction_candidate_cosine_range'] = (min(align), max(align)) if align else None
        result['heldout_peak_rgb8'] = max(max(row) for row in obs.rgb8)
        result['heldout_channel_range_rgb8'] = tuple((min(row[c] for row in obs.rgb8),
                                                     max(row[c] for row in obs.rgb8)) for c in range(3))
        raw = evidence.training.rgb8
        result['training_tangent_gradient_rgb8_per_m'] = tuple((raw[4][c] - raw[1][c]) / .6 for c in range(3))
    return result


def equivalent_secondary():
    """Exact Le versus rho*E/pi ambiguity with the same outgoing surface field."""
    f = fixture('red_reflected_wall')
    scene, receivers = f.evidence.scene, f.heldout.receivers
    rho, incident = (.8, .08, .04), (20.,) * 3
    emitted_outgoing = (16. / math.pi, 1.6 / math.pi, .8 / math.pi)
    points = scene.candidate.samples(POLICY['quadrature_order'])
    def integrate(reflecting):
        values, moments = [], []
        for receiver in receivers:
            irradiance, moment = ZERO, ZERO
            for p in points:
                outgoing = (tuple(r * e / math.pi for r, e in zip(rho, incident))
                            if reflecting else emitted_outgoing)
                weight = kernel(scene, p, receiver) * scene.candidate.area / len(points)
                irradiance = add(irradiance, tuple(weight * v for v in outgoing))
                d = sub(p, receiver.position)
                direction = unit(d)
                angular_weight = (scene.candidate.area / len(points) * max(0., -d[0]) / dot(d, d) ** 1.5
                                  if visible(scene, p, receiver.position) else 0.)
                moment = add(moment, tuple(angular_weight * sum(outgoing) * v for v in direction))
            values.append(irradiance)
            moments.append(unit(moment))
        return values, moments
    reflected, reflected_directions = integrate(True)
    emitted, emitted_directions = integrate(False)
    return dict(reflector_rho=rho, incident=incident, equivalent_Le=emitted_outgoing,
                maximum_receiver_difference=max(abs(a - b) for x, y in zip(reflected, emitted) for a, b in zip(x, y)),
                encoded_equal=tuple(tuple(encode(v) for v in row) for row in reflected) ==
                              tuple(tuple(encode(v) for v in row) for row in emitted),
                maximum_direction_difference=max(abs(a - b) for x, y in zip(reflected_directions, emitted_directions)
                                                 for a, b in zip(x, y)),
                new_origin_information_from_direction=False)


def run():
    rows = []
    for kind in ('emissive_panel', 'red_reflected_wall', 'lit_doorway', 'decorative_glow'):
        for scale in (.5, 1., 2.):
            f = fixture(kind, scale)
            rows.append(dict(scene=kind, scale=scale, truth_candidate_emits=f.truth_candidate_emits,
                             result=evaluate(f)))
    metrics = {}
    for method in ('brightness', 'glow_stage', 'geometry_and_brightness', 'conservative'):
        predictions = []
        for row in rows:
            r = row['result']
            bright = r['appearance_peak_rgb8'] >= POLICY['brightness_threshold_rgb8']
            predictions.append(dict(brightness=bright, glow_stage=r['glow_stage'],
                geometry_and_brightness=bright and r['surface_supported'],
                conservative=r['decision'] == 'ConditionalEmitterCandidate')[method])
        truth = [row['truth_candidate_emits'] for row in rows]
        fp = sum(p and not t for p, t in zip(predictions, truth))
        tp = sum(p and t for p, t in zip(predictions, truth))
        metrics[method] = dict(false_sources=fp, nonemitting_proposals=truth.count(False),
            false_source_rate=fp / truth.count(False), true_sources_retained=tp,
            emitting_proposals=truth.count(True), recall=tp / truth.count(True))
    controls = {}
    f = fixture('emissive_panel')
    for field in ('geometry_known', 'material_known', 'root_known', 'encoding_known', 'bake_emission_hint'):
        evidence = replace(f.evidence, scene=replace(f.evidence.scene, **{field: False}))
        controls[field] = diagnose(evidence)
    return dict(schema='org.openjk.asr06-emissive-ambiguity/1', policy=POLICY, rows=rows,
        metrics=metrics, abstention_controls=controls, equivalent_secondary=equivalent_secondary(),
        scope='Analytical fixtures only; no stock JA discovery-rate or historical-source claim',
        implementation_lf_sha256=hashlib.sha256(Path(__file__).read_text(encoding='utf-8').encode()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=HERE / 'results.json')
    args = parser.parse_args()
    result = run()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(dict(output=str(args.output), metrics=result['metrics']), indent=2))


if __name__ == '__main__':
    main()
