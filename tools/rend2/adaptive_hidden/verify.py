"""Repeat verification and explicit improvement/failure assertions for ASR16.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path

from .evaluate import read
from tools.rend2.lightmap_patches.run import sha, write


def verify(a, b):
    first, second = read(a/'frozen.json'), read(b/'frozen.json')
    if first != second:
        raise ValueError('Frozen inputs, policies, counts or numerical output hashes differ')
    for job in first['jobs']:
        path = Path(job['name'])/(job['strategy']+'.json')
        if (a/path).read_bytes() != (b/path).read_bytes():
            raise ValueError('Numerical repeated report differs: '+str(path))
    metrics = read(a/'evaluation.json')
    cases = {c['name']: c['methods'] for c in metrics['cases']}
    v02 = cases['v02-direct']
    if v02['adaptive']['covered'] <= v02['strict']['covered']:
        raise ValueError('No V02 proposal-recall improvement at fixed caps')
    if not v02['adaptive']['spot_axis_diagnostics'][0]['position_candidates']:
        raise ValueError('Previously missed V02 spotlight was not proposed')
    gi = cases['compiled_gi_only_region']
    if gi['adaptive']['unmatched_alternatives'] == 0:
        raise ValueError('GI counterexample unexpectedly disappeared; inspect instead of certifying lamps')
    if any(m['physical_certifications'] for methods in cases.values() for m in methods.values()):
        raise ValueError('Source-existence certification from hypotheses is forbidden')
    return dict(schema='org.openjk.adaptive-hidden-verification.v1', repeat_equal=True,
                frozen_manifest_sha256=sha(a/'frozen.json'), evaluation_sha256=sha(a/'evaluation.json'),
                numerical_jobs=len(first['jobs']), fixed_budget_caps=first['policy'],
                v02_recall_improved=True, gi_only_counterexample_retained=True,
                oblique_spotlight_miss_retained=cases['spot']['adaptive']['covered'] == 0,
                mixed_source_miss_retained=cases['two_colour']['adaptive']['covered'] < 2,
                historical_source_certified=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('a', type=Path); p.add_argument('b', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args(); result = verify(args.a, args.b); write(args.out, result)
    print('ASR16: repeated numerical jobs identical; improvement and counterexamples retained')
