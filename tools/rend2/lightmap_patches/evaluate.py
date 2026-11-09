"""Freeze ASR14 descriptors before scoring; publish aggregate reproducible metrics.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import math
from pathlib import Path

from .extract import POLICY, combine, dot, extract, mul, stable, sub
from .fixtures import cases
from .run import Geometry, read_rows, sha, write

ROOT = Path(__file__).resolve().parents[3]
COMPILED = ('point_room', 'spot', 'panel', 'two_colour', 'door_occlusion', 'wall_leak', 'bounce_colour')


def matching(patches, labels, tolerance):
    """One-to-one footprint detection, independent authored targets and tolerance."""
    assigned = {}
    def augment(label, visited):
        choices = sorted((math.dist(p['maximum_world'], labels[label]['centre_world']), i)
                         for i, p in enumerate(patches))
        for error, i in choices:
            if error > tolerance or i in visited:
                continue
            visited.add(i)
            if i not in assigned or augment(assigned[i], visited):
                assigned[i] = label
                return True
        return False
    for label in range(len(labels)):
        augment(label, set())
    return dict(detections=len(patches), labelled_footprints=len(labels), matched=len(assigned),
                false_positives=len(patches)-len(assigned), false_negatives=len(labels)-len(assigned),
                precision=len(assigned)/len(patches) if patches else None,
                recall=len(assigned)/len(labels) if labels else None,
                tolerance_world=tolerance)


def controlled(output):
    records = []
    for case in cases():
        # Labels are held only by the controller, never passed to the extractor.
        report = extract(case['rows'])
        if report != extract(reversed(case['rows'])):
            raise AssertionError('Controlled extraction is not deterministic')
        path = output/'controlled'/(case['name']+'.json')
        write(path, report)
        patches, labels = report['patches'], case['labels']
        metric = matching(patches, labels, 16)
        errors, orientations = [], []
        for label in labels:
            if not patches:
                continue
            p = min(patches, key=lambda p: math.dist(p['maximum_world'], label['centre_world']))
            errors.append(math.dist(p['maximum_world'], label['centre_world']))
            if p['major_axis_world'] and label['footprint_angle_degrees'] is not None:
                angle = math.radians(label['footprint_angle_degrees'])
                axis = [math.cos(angle), math.sin(angle), 0]
                orientations.append(math.degrees(math.acos(min(1, abs(dot(axis, p['major_axis_world']))))))
        records.append(dict(name=case['name'], input_sha256=stable(case['rows']),
                            descriptor_sha256=sha(path), repeat_identical=True, metrics=metric,
                            maximum_error_world=errors, footprint_orientation_error_degrees=orientations,
                            conditional_radial_constraints=sum(p['constraint']['state'] == 'Conditional' for p in patches),
                            primary_constraint_false_positives=(sum(p['constraint']['state'] == 'Conditional' for p in patches)
                                                                if not case['direct_primary'] else None),
                            physical_emitters_published=0))
    return records


def bounce_increment(fixtures, suite, output):
    """Paired *compiled* bounce-on/off code increment, not measured irradiance.

    The controlled compiler profile has gamma=1, compensate=1, exposure=0.
    Censored pairs are removed. Quantization and separate bake filtering remain.
    This is derived GI evidence, explicitly distinct from an original capture.
    """
    names = ('bounce_colour', 'bounce_control')
    originals = [list(read_rows(fixtures/(n+'.rows.jsonl'))) for n in names]
    def key(r):
        return tuple(r[k] for k in ('image', 'page', 'x', 'y', 'surface', 'material', 'slot', 'style'))
    by_key = [{key(r): r for r in rows} for rows in originals]
    if set(by_key[0]) != set(by_key[1]):
        raise ValueError('Paired bounce captures have different receiver charts')
    rows, censored, negative = [], 0, 0
    for k in sorted(by_key[0]):
        a, b = [v[k] for v in by_key]
        if a['position'] != b['position'] or a['normal'] != b['normal']:
            raise ValueError('Paired bounce receiver geometry mismatch')
        if any(c == 255 for r in (a, b) for c in r['raw']):
            censored += 1
            continue
        negative += sum(x < y for x, y in zip(a['raw'], b['raw']))
        rows.append(dict(a, id=stable([a['id'], b['id'], 'positive-bounce-code-increment']),
                         raw=[max(0, x-y) for x, y in zip(a['raw'], b['raw'])]))
    report = extract(rows)
    report['derivation'] = 'Positive code increment of matched compiled bounce=1 minus bounce=0, censored pairs excluded'
    path = output/'compiled/bounce_increment.json'
    write(path, report)
    if report['patches'] != extract(rows)['patches']:
        raise AssertionError('Paired bounce extraction is not deterministic')
    # Verify evaluator-only compiler profiles after freezing the features.
    answers = [json.loads((suite/'answers'/(n+'.q3map-recipe.json')).read_text()) for n in names]
    for name, answer in zip(names, answers):
        profile = answer['ground_truth']
        if (profile['gamma'], profile['compensate'], profile['exposure']) != (1, 1, 0):
            raise ValueError('Bounce increment requires the declared controlled code profile')
        source = json.loads((fixtures/(name+'.source.json')).read_text())
        if answer['compiled_bsp_sha256'] != source['map_sha256']:
            raise ValueError('Bounce pair identity mismatch')
    if [a['ground_truth']['bounce_iterations'] for a in answers] != [1, 0]:
        raise ValueError('Expected one-bounce and direct-only paired bakes')
    if answers[0]['ground_truth']['lights'] != answers[1]['ground_truth']['lights']:
        raise ValueError('Direct-light controls differ')
    return dict(name='compiled_gi_code_increment', derivation=report['derivation'],
                original_rows_sha256=[sha(fixtures/(n+'.rows.jsonl')) for n in names],
                answer_sha256=[sha(suite/'answers'/(n+'.q3map-recipe.json')) for n in names],
                descriptor_sha256=sha(path), censored_pairs_excluded=censored,
                negative_channel_differences_clamped=negative,
                patches=len(report['patches']),
                primary_constraint_false_positives=sum(p['constraint']['state'] == 'Conditional' for p in report['patches']),
                physical_emitters_published=0, repeat_identical=True)


def compiled(fixtures, suite, output, geometry_tool=None):
    frozen = []
    for name in COMPILED:
        rows = fixtures/(name+'.rows.jsonl')
        source = fixtures/(name+'.source.json')
        metadata = json.loads(source.read_text(encoding='utf-8'))
        report = extract(read_rows(rows))
        if report != extract(read_rows(rows)):
            raise AssertionError('Compiled extraction is not deterministic')
        geometry = Geometry(geometry_tool, fixtures/(name+'.jkobs'), metadata['map_sha256']) if geometry_tool else None
        try:
            report['candidates'] = combine(report['patches'], geometry)
        finally:
            if geometry: geometry.close()
        path = output/'compiled'/(name+'.json')
        write(path, report)
        frozen.append((name, report, dict(rows_sha256=sha(rows), source_sha256=sha(source),
                                         descriptor_sha256=sha(path), map_sha256=metadata['map_sha256'])))
    # All estimates exist on disk before any compiler source positions are read.
    records = []
    for name, report, pins in frozen:
        answer_path = suite/'answers'/(name+'.q3map-recipe.json')
        answer = json.loads(answer_path.read_text(encoding='utf-8'))
        bsp = suite/'authoring/base/maps'/(name+'.bsp')
        if answer['compiled_bsp_sha256'] != pins['map_sha256'] or sha(bsp) != pins['map_sha256']:
            raise ValueError('Frozen fixture/compiler answer identity mismatch')
        truth = answer['ground_truth']
        sources = [s['position_world'] for s in truth['lights']]
        if truth.get('surface_emitter'):
            lo, hi = truth['surface_emitter']['bounds']
            sources.append([(a+b)/2 for a, b in zip(lo, hi)])
        patches = report['patches']
        scored = []
        for p in patches:
            c = p['constraint']
            projection_error = None
            if c['foot_world'] is not None and sources:
                def foot(s):
                    return sub(s, mul(p['normal_world'], dot(sub(s, p['frame']['origin']), p['normal_world'])))
                projection_error = min(math.dist(c['foot_world'], foot(s)) for s in sources)
            scored.append(dict(id=p['id'], surface=p['chart']['surface'], receivers=len(p['receiver_ids']),
                                constraint=c['state'], projection_error_world=projection_error,
                                foot_world=c['foot_world'], maximum_world=p['maximum_world'],
                                footprint_axis=p['major_axis_world'],
                                apparent_boundaries=len(p['apparent_boundaries'])))
        candidates = [dict(state=c['state'], position_error_world=min(math.dist(c['position_world'], s) for s in sources))
                      for c in report['candidates']] if sources else []
        # Floor footprint labels: point's normal projection; spot's central-axis
        # intersection. Label only when inside the known room and source above.
        floors = [p for p in patches if p['normal_world'][2] > .99]
        labels = []
        for s in truth['lights']:
            p = s['position_world'][:]
            if s['type'] == 'spot':
                d = s['axis_propagation']; t = -p[2]/d[2]
                p = [a+t*b for a, b in zip(p, d)]
            p[2] = 0
            if -176 <= p[0] <= 176 and -112 <= p[1] <= 112:
                labels.append(dict(centre_world=p))
        # Panel is evaluated on its closest, brightest receiver (ceiling z=256),
        # not against the below-threshold floor. Frozen in the evaluation policy.
        if name == 'panel':
            floors = [p for p in patches if p['normal_world'][2] < -.99]
            labels = [dict(centre_world=[0, 0, 256])]
        metric = matching(floors, labels, 48 if name == 'spot' else 32)
        records.append(dict(name=name, **pins, answer_sha256=sha(answer_path), repeat_identical=True,
                            patches=len(patches), receiver_footprint_metrics=metric,
                            bounce_iterations=truth['bounce_iterations'], constraints=scored,
                            candidate_metrics=candidates, emitter_orientation_available=False,
                            physical_emitters_published=0))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', type=Path, default=ROOT/'build/x02/compiled-fixtures-frozen')
    parser.add_argument('--suite', type=Path, default=ROOT/'build/q3map2-bake-verified')
    parser.add_argument('--geometry-tool', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if (args.out/'summary.json').exists():
        raise ValueError('Use a fresh evaluation output; frozen runs are immutable')
    args.out.mkdir(parents=True, exist_ok=True)
    numerical = controlled(args.out)
    compiler = compiled(args.fixtures, args.suite, args.out, args.geometry_tool)
    gi = bounce_increment(args.fixtures, args.suite, args.out)
    report = dict(schema='org.openjk.lightmap-patch-evaluation.v1', policy=POLICY,
                  controlled=numerical, compiled=compiler, compiled_gi=gi,
                  metric_scope='Detection precision is one-to-one receiver footprint matching, not primary-source certification',
                  compiled_labels='Floor normal projections; spot central-axis footprint (48 WU tolerance); panel ceiling footprint. Others 32 WU.',
                  controlled_labels='Authored code-field centres and anisotropic axes, 16 WU tolerance',
                  source_orientation_precision='Unavailable: patch axis does not identify lamp orientation',
                  geometry_tool_sha256=sha(args.geometry_tool) if args.geometry_tool else None,
                  sources={str(p.relative_to(ROOT)): sha(p) for p in sorted(Path(__file__).parent.glob('*.py'))})
    write(args.out/'summary.json', report)
    print(json.dumps(dict(controlled=[(r['name'], r['metrics']) for r in numerical],
                          compiled=[(r['name'], r['receiver_footprint_metrics']) for r in compiler])))


if __name__ == '__main__':
    main()
