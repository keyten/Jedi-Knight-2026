#!/usr/bin/env python3
"""S12 production-C++ uncertainty and disjoint scene calibration witnesses.
No forward formula, optimizer, probability fitting or source answer in Python.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.amplitude_fit.benchmark import peak
from tools.rend2.evaluation.metrics import probability_calibration


def cpu_seconds(child):
    if sys.platform != 'win32':
        return None
    stamps=[ctypes.c_ulonglong() for _ in range(4)]
    query=ctypes.WinDLL('kernel32').GetProcessTimes
    query.argtypes=[ctypes.c_void_p]+[ctypes.c_void_p]*4
    if not query(int(child._handle), *(ctypes.byref(v) for v in stamps)):
        return None
    return (stamps[2].value+stamps[3].value)*1e-7


def dashboard(data):
    if data['schema'] != 'org.openjk.uncertainty-population.v1':
        raise ValueError('Wrong S12 population schema')
    families = data['families']
    if len({f['geometry'] for f in families}) != len(families):
        raise ValueError('Repeated geometry cannot be attested as an independent scene draw')
    cal = {f['index'] for f in families if f['split'] == 'calibration'}
    evaluation = [f for f in families if f['split'] == 'evaluation']
    if cal & {f['index'] for f in evaluation} or not cal or not evaluation:
        raise ValueError('Calibration and evaluation must be nonempty and disjoint')
    rows = [(f, t) for f in evaluation for t in f['trials'] if t['event'] == 0]
    if any(t['rate_known'] and not t.get('calibration_eligible',True) for _,t in rows):
        raise ValueError('Calibrated rate published outside frozen training/validation admission')
    true_count = sum(f['true_sources'] for f in evaluation)
    missed = sum(f['missed'] for f in evaluation)
    positive = sum(t['label'] for _, t in rows if t['label_known'])
    matched_truth = true_count - missed
    report = dict(
        population=data['population'],
        scope='Independent procedural planar rooms with supplied candidate positions; not stock JA calibration',
        calibration_families=len(cal), evaluation_families=len(evaluation),
        candidate_records=len(rows), true_sources=true_count, matched_truth=matched_truth,
        missed_truth=missed, false_candidates=sum(not t['label'] for _, t in rows),
        candidate_truth_coverage=matched_truth/true_count if true_count else None,
        candidate_precision=positive/len(rows) if rows else None,
        uncalibrated_candidates=sum(not t['rate_known'] for _, t in rows),
        calibration_admitted_families=len({f['index'] for f in families if f['split']=='calibration'
            and any(t.get('calibration_eligible',True) and t['event']==0 for t in f['trials'])}),
        joint_configuration_coverage=dict(
            covered=sum(f['joint_covered'] for f in evaluation if f['true_sources']),
            denominator=sum(bool(f['true_sources']) for f in evaluation),
            meaning='One retained complete configuration matches every truth injectively; not nominal 95% coverage'),
        risk_coverage=[], reliability=[], strata=[])
    # Ordinary row-weighted diagnostic for compatibility with the existing
    # evaluator. Correlated candidates in one scene do NOT receive Wilson CIs.
    report['existing_metric_diagnostic'] = probability_calibration([
        dict(score=t['rate'], label=int(t['label']), meaning='probability',
             calibration_protocol=data['calibration_key'])
        for _, t in rows if t['rate_known'] and t['label_known']], independent_trials=False)
    # Primary diagram gives each independent family equal weight within a
    # frozen calibration bin. It is separate from the row-weighted diagnostic.
    for b in data['bins']:
        if b['event'] != 0:
            continue
        grouped = {}
        for f, t in rows:
            if t['rate_known'] and t['label_known'] and t['bin']==b['bin']:
                grouped.setdefault(f['index'], []).append(t)
        if not grouped:
            continue
        rates = [sum(t['label'] for t in ts)/len(ts) for ts in grouped.values()]
        actual = math.fsum(rates)/len(rates)
        radius = math.sqrt(math.log(40)/(2*len(rates)))
        report['reliability'].append(dict(bin=b['bin'], predicted_rate=b['rate'],
            calibration_families=b['families'], evaluation_families=len(rates),
            observed_family_rate=actual,
            evaluation_family_hoeffding95=[max(0, actual-radius), min(1, actual+radius)],
            calibration_rate_interval95=[b['lower'], b['upper']]))
    for mode in ('rate', 'lower'):
        for threshold in (0, .25, .5, .75, .9, 1.01):
            chosen = [(f,t) for f,t in rows if t['rate_known'] and t[mode] >= threshold]
            correct = sum(t['label'] for _, t in chosen)
            report['risk_coverage'].append(dict(mode=mode, threshold=threshold,
                accepted_candidates=len(chosen), false_accepted=len(chosen)-correct,
                candidate_coverage=len(chosen)/len(rows) if rows else None,
                risk=(len(chosen)-correct)/len(chosen) if chosen else None,
                truth_recall=correct/true_count if true_count else None,
                missed_or_abstained_truth=true_count-correct))
    for stratum in sorted({f['stratum'] for f in evaluation}):
        scenes = [f for f in evaluation if f['stratum'] == stratum]
        rs = [t for f in scenes for t in f['trials'] if t['event'] == 0]
        report['strata'].append(dict(stratum=stratum, scenes=len(scenes),
            true_sources=sum(f['true_sources'] for f in scenes),
            missed=sum(f['missed'] for f in scenes),
            false_candidates=sum(not t['label'] for t in rs),
            calibrated_records=sum(t['rate_known'] for t in rs)))
    json.dumps(report, allow_nan=False)
    return report


def svg(report):
    # Scientific diagnostic artifact, deterministic SVG; no chart dependencies.
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="560" height="440" viewBox="0 0 560 440">',
         '<rect width="560" height="440" fill="white"/>',
         '<text x="65" y="24" font-family="sans-serif" font-size="16">S12 independent scene reliability (existence event)</text>',
         '<path d="M70 40V365H395M70 365L395 40" fill="none" stroke="#888"/>',
         '<text x="110" y="413" font-family="sans-serif" font-size="13">Frozen-bin empirical rate (not source posterior)</text>',
         '<text x="16" y="350" transform="rotate(-90 16 350)" font-family="sans-serif" font-size="13">Held-out family success rate</text>']
    for row in report['reliability']:
        x = 70+325*row['predicted_rate']; y = 365-325*row['observed_family_rate']
        lo,hi = row['evaluation_family_hoeffding95']
        s += [f'<path d="M{x:.4f} {365-325*hi:.4f}V{365-325*lo:.4f}" stroke="#1d6093"/>',
              f'<circle cx="{x:.4f}" cy="{y:.4f}" r="5" fill="#1d6093"><title>bin {row["bin"]}, {row["evaluation_families"]} evaluation families</title></circle>']
    s += ['<text x="70" y="390" font-family="sans-serif" font-size="12">0</text>',
          '<text x="385" y="390" font-family="sans-serif" font-size="12">1</text>',
          '<text x="410" y="70" font-family="sans-serif" font-size="11">Error bars: family</text>',
          '<text x="410" y="86" font-family="sans-serif" font-size="11">Hoeffding 95% bounds.</text>',
          '<text x="410" y="115" font-family="sans-serif" font-size="11">Stock JA: uncalibrated.</text>', '</svg>']
    return '\n'.join(s)+'\n'


def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--tests', type=Path, required=True)
    p.add_argument('--population', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--timeout', type=float, default=600)
    a=p.parse_args();a.out.mkdir(parents=True, exist_ok=False)
    if sys.platform == 'win32':
        ctypes.windll.kernel32.SetErrorMode(0x8007)
    sources = ['shared/rd-rend2/lightrecon/'+n for n in (
        'uncertainty.cpp','uncertainty.h','joint.cpp','joint.h','disk.cpp')]
    sources += ['shared/rd-rend2/tr_lightrecon_types.'+e for e in ('h','cpp')]
    sources += ['tools/lightrecon_test/'+n for n in (
        'lightrecon_uncertainty_test.cpp','lightrecon_uncertainty_population.cpp','joint_test_scene.h','joint_fixture.h')]
    sources += ['tools/rend2/uncertainty/benchmark.py']
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    pinned={s:sha(ROOT/s) for s in sources}
    resources=[];witnesses={}
    for label,tool in (('tests',a.tests),('population',a.population)):
        runs=[]
        for repeat in range(2):
            target=(a.out/f'{label}-{repeat}.json').resolve();start=time.perf_counter()
            with subprocess.Popen([str(tool.resolve()),str(target)],stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE,text=True) as child:
                try:stdout,stderr=child.communicate(timeout=a.timeout)
                except subprocess.TimeoutExpired:
                    child.kill();child.communicate();raise RuntimeError('S12 process time safety limit')
                rss=peak(child);cpu=cpu_seconds(child)
                (a.out/f'{label}-{repeat}.log').write_text(stdout+stderr)
                if child.returncode:raise RuntimeError(stdout+stderr)
            runs.append(json.loads(target.read_text()))
            resources.append(dict(stage=label,repeat=repeat,seconds=time.perf_counter()-start,
                                  process_cpu_seconds=cpu,peak_process_rss_bytes=rss,input_bytes=target.stat().st_size))
            print(json.dumps(resources[-1]),flush=True)
        if runs[0]!=runs[1]:raise ValueError(f'{label} semantic results differ across identical runs')
        witnesses[label]=runs[0]
    if witnesses['tests']['failures'] or witnesses['tests']['checks']<60:
        raise ValueError('Missing/failed production uncertainty tests')
    if pinned != {s:sha(ROOT/s) for s in sources}:raise ValueError('Source changed during benchmark')
    report=dashboard(witnesses['population'])
    data=dict(schema='org.openjk.s12-benchmark.v1',deterministic=True,
        source_sha256=pinned,executable_sha256={str(t.name):sha(t) for t in (a.tests,a.population)},
        tests=witnesses['tests'],dashboard=report,
        limitations=['Conditional sampled sets are not nominal statistical position intervals',
                     'Known relative fixture semantics; no historical stock compiler identity',
                     'No positive absolute-scale/exact-use calibration in this population',
                     'Final evaluation labels never train the calibration table'])
    (a.out/'numerical.json').write_text(json.dumps(data,indent=2,sort_keys=True,allow_nan=False)+'\n')
    (a.out/'resources.json').write_text(json.dumps(resources,indent=2,sort_keys=True)+'\n')
    (a.out/'reliability.svg').write_text(svg(report))
    fields=('mode','threshold','accepted_candidates','false_accepted','candidate_coverage','risk','truth_recall','missed_or_abstained_truth')
    (a.out/'risk-coverage.csv').write_text(','.join(fields)+'\n'+'\n'.join(','.join('' if r[f] is None else str(r[f]) for f in fields) for r in report['risk_coverage'])+'\n')


if __name__=='__main__':main()
