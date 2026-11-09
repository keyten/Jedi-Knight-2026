"""Verify repeat receipts and predeclared useful/negative regression limits.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path

from .run import sha, write


def verify(first, repeat):
    a = json.loads((first/'summary.json').read_text(encoding='utf-8'))
    b = json.loads((repeat/'summary.json').read_text(encoding='utf-8'))
    if a != b:
        raise AssertionError('ASR14 repeated summary differs')
    for p in first.rglob('*.json'):
        if sha(p) != sha(repeat/p.relative_to(first)):
            raise AssertionError('ASR14 repeated descriptors differ: '+str(p))
    controlled = {r['name']: r for r in a['controlled']}
    for name in ('point', 'spot', 'panel', 'multi'):
        metric = controlled[name]['metrics']
        if metric['precision'] != 1 or metric['recall'] != 1:
            raise AssertionError('Controlled footprint regression: '+name)
        if any(e > 2 for e in controlled[name]['footprint_orientation_error_degrees']):
            raise AssertionError('Controlled footprint orientation regression')
    for name in ('flat', 'ramp', 'material_seam'):
        if controlled[name]['metrics']['false_positives']:
            raise AssertionError('Negative-test patch false positive')
    if controlled['gi_only']['primary_constraint_false_positives'] != 1:
        raise AssertionError('Lost explicit primary/GI ambiguity counterexample')
    for c in a['compiled']:
        if c['physical_emitters_published'] or c['emitter_orientation_available']:
            raise AssertionError('Unsupported physical/orientation claim')
    point = next(c for c in a['compiled'] if c['name'] == 'point_room')
    errors = [c['projection_error_world'] for c in point['constraints'] if c['constraint'] == 'Conditional']
    if len(errors) < 3 or max(errors) > 1:
        raise AssertionError('Compiled point projection regression')
    return dict(schema='org.openjk.lightmap-patches-verification.v1',
                repeated_all_json_identical=True, controlled_cases=len(a['controlled']),
                compiled_cases=len(a['compiled']), compiled_gi_increment=True,
                compiled_point_maximum_projection_error_world=max(errors),
                physical_emitters_published=0, summary_sha256=sha(first/'summary.json'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first', type=Path)
    parser.add_argument('repeat', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.first, args.repeat)
    write(args.out, result)
    print(json.dumps(result))
