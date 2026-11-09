"""Verify byte-repeatable controlled and optional original-capture runs.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from collections import Counter
from pathlib import Path

from tools.rend2.lightmap_patches.run import sha, write


def pair(a, b, schema):
    if Path(a).read_bytes() != Path(b).read_bytes():
        raise ValueError('ASR15 repeated outputs differ')
    result = json.loads(Path(a).read_text(encoding='utf-8'))
    if result['schema'] != schema or result['historical_source_certified']:
        raise ValueError('Unexpected schema or historical certification')
    return result


def verify(a, b, panel_a=None, panel_b=None):
    report = pair(a, b, 'org.openjk.fixture-association-validation.v1')
    for case in report['cases']:
        if case['selected'] != case['truth']:
            raise ValueError('Controlled association mismatch: '+case['name'])
    for method in ('nearest', 'nearest_colour', 'colour_only'):
        if report['aggregate']['association']['f1'] <= report['aggregate'][method]['f1']:
            raise ValueError('Association does not outperform '+method)
    result = dict(schema='org.openjk.fixture-association-verification.v1',
                  controlled_repeat_equal=True, controlled_sha256=sha(a),
                  implementation_sha256=report['implementation_sha256'],
                  aggregate=report['aggregate'], historical_source_certified=False)
    if bool(panel_a) != bool(panel_b):
        raise ValueError('Both original-capture repeats required')
    if panel_a:
        panel = pair(panel_a, panel_b, 'org.openjk.fixture-associations.v1')
        if panel['active_fixtures'] or len(panel['associations']) != 12:
            raise ValueError('Frozen compiled panel integration expectation changed')
        statuses = dict(Counter(e['status'] for e in panel['associations']))
        rays = dict(Counter(s['terms']['visibility']['reason'].rsplit(': ', 1)[-1]
                            for e in panel['associations'] for s in e['supports']))
        if statuses != dict(ambiguous=9, rejected=3) or rays != dict(unknown=161, blocked=103):
            raise ValueError('Frozen panel uncertainty accounting changed')
        result['compiled_panel'] = dict(repeat_equal=True, report_sha256=sha(panel_a),
            input=panel['input'], associations=12, selected=0, statuses=statuses, visibility=rays,
            adapter_diagnostics=panel['adapter_diagnostics'],
            unmatched_observations=len(panel['unmatched_observations']),
            interpretation='Existing owned compiled panel; integration test only, no recovery-accuracy claim')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('a', type=Path)
    p.add_argument('b', type=Path)
    p.add_argument('--panel-a', type=Path)
    p.add_argument('--panel-b', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    result = verify(args.a, args.b, args.panel_a, args.panel_b)
    write(args.out, result)
    print(json.dumps(dict(controlled_repeat_equal=True, compiled_panel_repeat_equal=bool(args.panel_a))))


if __name__ == '__main__':
    main()
