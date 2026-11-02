#!/usr/bin/env python3
"""Link frozen X12 and X14 source IDs; no licensed coordinates are published.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main(a):
    old = list(csv.DictReader(a.before.open(), delimiter='\t'))
    new = {r['source']: r for r in csv.DictReader(a.after.open(), delimiter='\t')}
    rows = []
    for r in old:
        h = new.get(r['id'])
        rows.append(dict(source=r['id'], kind=int(r['kind']), matched_stable_id=h is not None,
                         geometry_coordinates_unchanged=bool(h) and all(r[k] == h[k] for k in ('x', 'y', 'z')),
                         frozen_constraint_ordinal=int(r['placement_state']),
                         new_control_constraint=h['old'] if h else None,
                         new_constraint=h['new'] if h else None,
                         reason=h['reason'] if h else 'Not admitted to the bounded comparison'))
    report = dict(schema='org.openjk.emitter-support-historical-witnesses.v1', map='mp/duel4',
                  before_sha256=sha(a.before), after_sha256=sha(a.after), observations_read=False,
                  claims_physical_source_truth=False, witnesses=rows)
    a.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    main(p.parse_args())
