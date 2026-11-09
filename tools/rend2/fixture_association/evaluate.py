"""Frozen controlled association census and nearest/colour baseline comparison.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

from .associate import associate
from .controls import cases
from tools.rend2.lightmap_patches.run import write


def baseline(case, colour=False, colour_only=False):
    result = set()
    for o in case['observations']:
        def cost(f):
            distances = [math.dist(f['position'], s['position'])/256 for s in o['samples']]
            errors = []
            for s in o['samples']:
                a, b = f['colour'], s.get('colour')
                if a and b:
                    na, nb = math.sqrt(sum(v*v for v in a)), math.sqrt(sum(v*v for v in b))
                    errors.append(1-sum(x*y for x, y in zip(a, b))/(na*nb))
            chroma = sum(errors)/len(errors) if errors else 0.
            return chroma if colour_only else sum(distances)/len(distances)+(.5*chroma if colour else 0.)
        if case['fixtures']:
            f = min(case['fixtures'], key=lambda f: (cost(f), f['id']))
            result.add((f['id'], o['id']))
    return result


def metrics(predicted, truth):
    tp, fp, fn = len(predicted & truth), len(predicted-truth), len(truth-predicted)
    return dict(tp=tp, fp=fp, fn=fn, f1=2*tp/(2*tp+fp+fn) if tp+fp+fn else 1.)


def evaluate():
    rows = []
    for case in cases():
        report = associate(case['fixtures'], case['observations'], case['geometry'])
        selected = {(e['fixture_id'], e['observation_id']) for e in report['associations'] if e['selected']}
        rows.append(dict(name=case['name'], truth=sorted(case['truth']), selected=sorted(selected),
                         association=metrics(selected, case['truth']),
                         nearest=metrics(baseline(case), case['truth']),
                         nearest_colour=metrics(baseline(case, True), case['truth']),
                         colour_only=metrics(baseline(case, colour_only=True), case['truth']),
                         report=report))
    totals = {}
    for method in ('association', 'nearest', 'nearest_colour', 'colour_only'):
        sums = {k: sum(r[method][k] for r in rows) for k in ('tp', 'fp', 'fn')}
        sums['f1'] = 2*sums['tp']/(2*sums['tp']+sums['fp']+sums['fn'])
        totals[method] = sums
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Path(__file__).parent.glob('*.py'))}
    return dict(schema='org.openjk.fixture-association-validation.v1', implementation_sha256=hashes,
                cases=rows, aggregate=totals,
                domain='Owned analytical controls; no fresh compiler execution or stock historical truth',
                calibration='No irradiance calibration; source answers used only by evaluation',
                historical_source_certified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = evaluate()
    write(args.out, result)
    print(json.dumps(result['aggregate'], sort_keys=True))


if __name__ == '__main__':
    main()
