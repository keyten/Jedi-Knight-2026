"""Replay frozen public ASR23 scenes, then invoke the trusted evaluator.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import platform
import subprocess
import sys
import time
from .solver import load, write, sha
from .evaluate import check_frozen, evaluate
ROOT = Path(__file__).resolve().parents[3]


def replay(work, output, native):
    output.mkdir(parents=True, exist_ok=False)
    protocol = load(work/'protocol.json')
    check_frozen(work, protocol, ROOT, native)
    resources = []
    for scene in protocol['scenes']:
        name = scene['name']; start = time.perf_counter()
        command = [sys.executable, '-X', 'utf8', '-m', 'tools.rend2.blind_benchmark.solver',
                   '--inputs', str(work/'public'/name), '--output', str(output/name),
                   '--native', str(native), '--policy', str(work/'policy.json')]
        r = subprocess.run(command, capture_output=True, text=True, timeout=1200, cwd=ROOT)
        (output/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        if r.returncode:
            raise ValueError(name+': '+r.stderr[-2000:])
        resources.append(dict(name=name, seconds=time.perf_counter()-start, command=command))
        print(name, 'frozen', round(resources[-1]['seconds'],2), 's', flush=True)
    check_frozen(work, protocol, ROOT, native)
    write(output/'resources.json',dict(platform=platform.platform(),python=sys.version,scenes=resources))


def semantic_models(value):
    if isinstance(value,dict):
        return {k:semantic_models(v) for k,v in value.items() if k not in ('seconds','fit_seconds')}
    if isinstance(value,list):
        return [semantic_models(v) for v in value]
    return value


def verify(first, second, out):
    names = sorted(p.parent.name for p in first.glob('*/frozen-models.json'))
    other = sorted(p.parent.name for p in second.glob('*/frozen-models.json'))
    if not names or names != other:
        raise ValueError('Repeat scene set mismatch')
    checks = {name:semantic_models(load(first/name/'frozen-models.json')) == semantic_models(load(second/name/'frozen-models.json')) for name in names}
    write(out, dict(status='Pass' if all(checks.values()) else 'Fail', scenes=checks,
                    ignored_fields=['seconds','fit_seconds'], numerical_results_and_work_exact=True))
    if not all(checks.values()):
        raise ValueError('Repeat changed numerical models/work')


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('phase',choices=('replay','evaluate','verify'))
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--runs',type=Path)
    p.add_argument('--second',type=Path)
    p.add_argument('--native',type=Path,default=ROOT/'build/s06-gcc')
    a = p.parse_args()
    for key,value in vars(a).items():
        if isinstance(value,Path): setattr(a,key,value.resolve())
    if a.phase == 'replay': replay(a.work,a.out,a.native)
    elif a.phase == 'evaluate':
        protocol = load(a.work/'protocol.json'); check_frozen(a.work,protocol,ROOT,a.native)
        evaluate(a.work,a.runs,a.out)
    else: verify(a.runs,a.second,a.out)
