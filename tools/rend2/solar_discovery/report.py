#!/usr/bin/env python3
"""Compact ASR07 witness; verify full semantic repeat before summarizing.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
from policy import semantic


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(first, repeat, cpu):
    if semantic(first) != semantic(repeat):
        raise AssertionError('Semantic inference repeat differs')
    if not all(first['checks'].values()) or cpu['failures']:
        raise AssertionError('Numerical/discovery checks failed')
    out = {k:v for k,v in first.items() if k != 'cases'}
    out.update(repeat_semantics_equal=True, cpu=cpu, cases={})
    for name, case in first['cases'].items():
        c = {k:v for k,v in case.items() if k != 'native'}
        n = case['native']; hypotheses = n['hypotheses']
        best = hypotheses[0] if hypotheses else None
        c['fit'] = dict(reason=n['reason'], training_coverage=n['training_coverage'],
                        validation_read_after_freeze=n['validation_read_after_freeze'],
                        frozen_key=n['frozen_key'], hypothesis_count=len(hypotheses),
                        best_hypothesis={k:v for k,v in best.items() if k not in ('receivers','environment_payload')} if best else None,
                        best_heldout=n['heldout'][0] if n['heldout'] else None,
                        decisions=n['decisions'], fit_work=n['fit_work'], validation_work=n['validation_work'])
        out['cases'][name] = c
    return out


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--first',type=Path,required=True)
    p.add_argument('--repeat',type=Path,required=True)
    p.add_argument('--cpu',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a = p.parse_args()
    result = summarize(*[json.loads(v.read_text()) for v in (a.first,a.repeat,a.cpu)])
    result['run_sha256'] = dict(first=digest(a.first),repeat=digest(a.repeat))
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x',encoding='utf-8') as stream:
        stream.write(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print('Verified repeat; wrote',a.output)
