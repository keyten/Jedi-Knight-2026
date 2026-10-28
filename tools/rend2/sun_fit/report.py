#!/usr/bin/env python3
"""Freeze S02 numerical witnesses; keep time/RSS separate from semantic results.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def load(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,sort_keys=True,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def semantic(v):
    if isinstance(v,dict):return {k:semantic(x)for k,x in v.items()if k not in('resources','seconds','wall_seconds')}
    if isinstance(v,list):return [semantic(x)for x in v]
    return v
def compact(v):
    if isinstance(v,dict):
        out={k:compact(x)for k,x in v.items()if k!='receivers'}
        if 'receivers'in v:
            r=v['receivers'];out['receiver_evidence_summary']=dict(requested=len(r),excluded=sum(x['exclusion']!=0 for x in r),sky_response_positive=sum(x['exclusion']==0 and x['sky_response']>0 for x in r),sun_response_positive=sum(x['exclusion']==0 and x['sun_response']>0 for x in r),sun_facing_but_blocked=sum(x['exclusion']==0 and x['sun_facing'] and x['sun_response']==0 for x in r))
        return out
    if isinstance(v,list):return [compact(x)for x in v]
    return v
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,required=True);p.add_argument('--repeat',type=Path,required=True);p.add_argument('--cpu',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    first,second=load(a.run/'result.json'),load(a.repeat/'result.json');stable=semantic(first)
    if stable!=semantic(second):raise ValueError('Repeated frozen-input experiment changed numerical results/work/identity; no witness published')
    cpu=load(a.cpu)
    if cpu['failures']:raise ValueError('CPU failures must remain visible, not a passed witness')
    files=list(HERE.glob('*.py'))+[ROOT/p for p in('shared/rd-rend2/lightrecon/sun_fit.h','shared/rd-rend2/lightrecon/sun_fit.cpp','shared/rd-rend2/lightrecon/CMakeLists.txt','tools/lightrecon_test/solar_tool.h','tools/lightrecon_test/lightrecon_sun_test.cpp','tools/lightrecon_test/lightrecon_bake_forward_tool.cpp','tools/lightrecon_test/CMakeLists.txt')]
    files+=list((ROOT/'shared/rd-rend2/lightrecon').glob('bake_*.cpp'))+list((ROOT/'shared/rd-rend2/lightrecon').glob('bake_*.h'))
    files+=[HERE.parent/'bake_model'/n for n in('source-lock.json','profiles.json','validate_cpp.py','compare_bakes.py')]
    files+=[HERE.parent/'datasets/prepare_blind.py',HERE.parent/'shadow_footprint/compiler_boundary.py']
    hashes={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest()for p in sorted(files)}
    key=hashlib.sha256(json.dumps(stable,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    cases=stable['cases'];negative=('ambient_only','no_sun')
    summary=dict(schema='org.openjk.s02-sun-sky-validation.v1',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),working_tree_source_sha256=hashes,hash_policy='SHA256 LF-normalized source bytes',semantic_sha256=key,repeated_frozen_inputs_identical=True,
        compiler_suite=compact(stable),analytical_cpu=cpu,
        false_sun_controls=dict(cases=list(negative),published_suns=sum(bool(cases[n]['diagnostic']['hypotheses'])for n in negative),denominator=len(negative),scope='controlled true-no-sun negatives, not a stock-map false-positive rate'),
        unsupported_multi_source_control=dict(case='multi_directional',state=cases['multi_directional']['state'],accepted_single_sun_hypotheses=len(cases['multi_directional']['diagnostic']['hypotheses']),meaning='Two true suns; rejecting unsupported one-sun explanation is NOT a no-sun ground-truth result'),
        scope=dict(physical_lux='NotComparable',historical_ja_profile='UnknownProfile',angular_extent='Unavailable',confidence_probability='Uncalibrated/unknown',runtime_publication=False,heldout='Value-only Assess after training freeze and validation; never used to refit'),
        retained_failures=[dict(case='sky_aperture',status=cases['sky_aperture']['acceptance']['status']),dict(case='sun_shadow',status=cases['sun_shadow']['acceptance']['status'])])
    a.output.mkdir(parents=True,exist_ok=True);write(a.output/'validation.json',summary)
    measured=dict(schema='org.openjk.s02-resource-measurements.v1',semantic_sha256=key,metric='Windows GetProcessMemoryInfo peak working set, separate controller/C++ processes; no sum of asynchronous peaks',runs=[])
    for directory,s in((a.run,first),(a.repeat,second)):
        measured['runs'].append(dict(directory=directory.as_posix(),wall_seconds=s['wall_seconds'],cases={n:dict(resources=c['resources'],cpp_fit_validation_seconds=c['diagnostic']['seconds'])for n,c in s['cases'].items()}))
    write(a.output/'measurements.json',measured);print(json.dumps(dict(semantic_sha256=key,cases=len(cases),repeated=True)))
if __name__=='__main__':main()
