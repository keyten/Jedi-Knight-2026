#!/usr/bin/env python3
"""Repeat the production S11 controller and score source answers independently.
Python standard library only; no forward-light formula or optimizer in this file.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.amplitude_fit.benchmark import peak
from tools.rend2.evaluation.metrics import detection

REQUIRED = {'two_colour','same_colour','point_spot_area','adjacent_panels',
            'tube_type_alternative','red_reflector','bright_plus_weak',
            'coincident_count_ambiguity','zero_signal','ambient_no_fake_emitter',
            'joint_position_refinement','missing_initial_candidate',
            'known_profile_alternative','censored_ldr','local_and_sun',
            'distinct_grid_deluxe_operators','invalid_unknown_and_resource_controls'}

def validate(witness):
    if witness.get('schema') != 'org.openjk.joint-tests.v1' or witness.get('failures') != 0 or witness.get('checks',0)<100:
        raise ValueError('Missing/failed production joint witness')
    cases={r['name']:r for r in witness['cases']}
    if not REQUIRED.issubset(cases) or len(cases)!=len(witness['cases']):
        raise ValueError('Missing/duplicate controlled scenes')
    for name in ('two_colour','same_colour','point_spot_area','bright_plus_weak',
                 'known_profile_alternative','missing_initial_candidate'):
        r=cases[name]
        if not 0<=r['heldout_mse']<.02*r['black_mse'] or r['black_mse']<=0:
            raise ValueError('Black/missing prediction or nonpositive acceptance: '+name)
        if r['physical_existence_certified']:
            raise ValueError('Numerical fit falsely certified source existence')
    for r in cases.values():
        if r.get('local_amplitude_rank',0)>r.get('local_amplitude_parameters',0):
            raise ValueError('Inherited/inconsistent parameter-rank diagnostic')
    if cases['bright_plus_weak']['preferred_sources']!=2:
        raise ValueError('Missed independently coloured weak source')
    if cases['ambient_no_fake_emitter']['preferred_sources']:
        raise ValueError('Invented ambient/GI point light')
    if cases['zero_signal']['state']!='InsufficientSignal':
        raise ValueError('Dark receiver falsely promoted')
    if cases['coincident_count_ambiguity']['state']!='Unidentifiable':
        raise ValueError('Equivalent count hypotheses falsely identified')
    json.dumps(witness,allow_nan=False)

def metrics(witness):
    policy=dict(position_world=.2,angle_degrees=10,cone_degrees=10,
                dimension_relative=.25,roll_degrees=15)
    result={}
    kinds={1:'point',2:'spot',3:'rectangle',4:'tube',5:'sun'}
    for case in witness['cases']:
        if 'truth' not in case:continue
        def source(s):
            # Sun is reported separately where the root metadata says so; the
            # physical finite-geometry matching below otherwise needs XYZ/type.
            kind='sun' if s.get('root')==1 else 'sky' if s.get('root')==2 else kinds[s['kind']]
            result=dict(kind=kind,position=s['position'],photometry=dict(
                unit='relative',domain='declared controlled profile',kind='LegacyRelative',
                value=max(s['rgb']),rgb=s['rgb'],primaries='Rec709Linear'))
            for name in ('direction','direction_convention','dimensions','tangent','cone_degrees'):
                if name in s:result[name]=s[name]
            return result
        truth=[source(s) for s in case['truth']]
        predicted=[source(s) for s in case['sources']]
        if case['name']=='unknown_absolute_scale':
            for s in predicted:s['photometry']['domain']='Unidentified global gain hypothesis'
        if case['name']=='coincident_count_ambiguity':
            for s in truth:s['identifiability']='ambiguous'
        # Thresholds are compact-fixture geometry tolerances; no stock-map XYZ truth.
        result[case['name']]=detection(truth,predicted,policy,1.)
        result[case['name']]['scope']='Controlled fixture; supplied geometry priors except named refinement/search cases'
    return result

def main():
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--tool',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--timeout',type=float,default=600)
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    files=['shared/rd-rend2/lightrecon/'+n for n in ('joint.h','joint.cpp','indirect.h','indirect.cpp',
        'emitter_shape.h','emitter_shape.cpp','bake_forward.cpp','point_refine.cpp','shadow_evidence.cpp')]
    files+=['tools/lightrecon_test/'+n for n in ('joint_fixture.h','lightrecon_joint_test.cpp')]
    files+=['tools/rend2/joint/benchmark.py']
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    sources={path:sha(ROOT/path) for path in files};runs=[];resources=[]
    for repeat in range(2):
        output=(args.out/f'cpu-{repeat}.json').resolve();start=time.perf_counter()
        with subprocess.Popen([str(args.tool.resolve()),str(output)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as child:
            try:stdout,stderr=child.communicate(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                child.kill();child.communicate();raise RuntimeError('Declared CPU time safety budget exceeded')
            rss=peak(child)
            (args.out/f'cpu-{repeat}.log').write_text(stdout+stderr)
            if child.returncode:raise RuntimeError(stdout+stderr)
        witness=json.loads(output.read_text());validate(witness);runs.append(witness)
        resources.append(dict(stage=f'CPU-{repeat}',seconds=time.perf_counter()-start,peak_process_rss_bytes=rss))
        print(json.dumps(resources[-1]),flush=True)
    if runs[0]!=runs[1]:raise ValueError('Joint semantic output is nondeterministic')
    if sources!={path:sha(ROOT/path) for path in files}:raise ValueError('Sources changed during experiment')
    report=dict(schema='org.openjk.s11-benchmark.v1',deterministic=True,owned=runs[0],
        source_metrics=metrics(runs[0]),source_sha256=sources,executable_sha256=sha(args.tool),
        limitations=['Controlled fixture attribution is conditional, not original stock emitter recovery',
                    'No posterior existence probabilities; no absolute legacy photometry',
                    'Unpacked grid/direct-only and native deluxe only where supported',
                    'Passive first bounce is not empirical compiler bounce replay'])
    (args.out/'numerical.json').write_text(json.dumps(report,sort_keys=True,indent=2,allow_nan=False)+'\n')
    (args.out/'resources.json').write_text(json.dumps(resources,sort_keys=True,indent=2)+'\n')

if __name__=='__main__':main()
