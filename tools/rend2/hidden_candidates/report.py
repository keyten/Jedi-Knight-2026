#!/usr/bin/env python3
"""Freeze S04 semantic witnesses and separate actual process measurements.
SPDX-License-Identifier: GPL-2.0-or-later. No licensed RGB/geometry is published.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import subprocess
from run import semantic,write

ROOT=Path(__file__).resolve().parents[3]
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('run','repeat','cpu','other-cpu','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True,help='Exact executable used for both frozen sweeps')
    a=p.parse_args();first=load(a.run/'result.json');second=load(a.repeat/'result.json')
    assert semantic(first['cases'])==semantic(second['cases']),'Repeated candidate geometry/IDs/relations changed'
    assert a.tool.stat().st_mtime <= min((a.run/'result.json').stat().st_mtime,(a.repeat/'result.json').stat().st_mtime),'Executable rebuilt after experiment; rerun before attesting it'
    cpu=load(a.cpu);other=load(a.other_cpu)
    assert cpu['failures']==other['failures']==0 and cpu['checks']==other['checks']
    assert len(cpu['scenes'])==len(other['scenes'])
    for x,y in zip(cpu['scenes'],other['scenes']):
        for key in x:
            if key=='nearest_truth_world':assert all(math.isclose(p,q,abs_tol=1e-6,rel_tol=1e-7)for p,q in zip(x[key],y[key]))
            else:assert x[key]==y[key],(x['name'],key,x[key],y[key])
    cases={};resources={}
    for name,c in first['cases'].items():
        d=c.get('diagnostic',{});records=d.get('candidates',[])
        assert not d.get('reconstructed',False)and d.get('runtime_sources',0)==0
        assert all(x['intensity']is None and x['existence_confidence']is None for x in records)
        stock='stock_truth'in c
        if stock:assert all(x['kind']!=0 for x in records),'Unknown stock transport cannot certify a physical source'
        cases[name]={k:c[k]for k in ('status','map_sha256','training_manifest_sha256','geometry_sha256','request_sha256','observations','receiver_surfaces','input_scope','search_geometry','trusted_answer_evaluation','stock_truth')if k in c}
        cases[name]['diagnostic']=dict(code=d.get('code'),reason=d.get('reason'),key=d.get('key'),geometry_global_complete=d.get('geometry_global_complete'),search_complete=d.get('search_complete'),
            geometry_bytes=d.get('geometry_bytes'),accounted_bytes=d.get('accounted_bytes'),rays=d.get('rays'),forward_calls=d.get('forward_calls'),pairs=d.get('pairs'),work=d.get('work'),areas=d.get('areas'),
            candidates=len(records),source_kinds=dict(Counter(str(x['source_kind'])for x in records)),classifications=dict(Counter(str(x['kind'])for x in records)),
            localization_evidence=('NoCandidates'if not records else'UnlocalizedNoKnownVisibleReceiverSupport'if not any(x['visible']for x in records)else'ConditionalUnderconstrainedCandidates'if all(x['underconstrained']for x in records)else'SurfaceSupportedCandidatePool_NotVerifiedSources'),
            profiles=dict(Counter(x['profile']for x in records)),region_assigned=sum(x['region']!=0 for x in records),region_unknown=sum(x['region']==0 for x in records),
            feasibility=dict(Counter(str(x['feasibility'])for x in records)),methods=dict(Counter(str(x['method'])for x in records)),
            underconstrained=sum(x['underconstrained']for x in records),unknown_relations=sum(x['unknown']for x in records),visible_relations=sum(x['visible']for x in records),blocked_relations=sum(x['blocked']for x in records),
            independent_surfaces_range=[min((x['surfaces']for x in records),default=0),max((x['surfaces']for x in records),default=0)],diagnostics=d.get('diagnostics'))
        resources[name]=dict(process=c.get('resources'),stages=d.get('resources'))
    files=[*Path(__file__).parent.glob('*.py'),ROOT/'tools/lightrecon_test/lightrecon_hidden_test.cpp',ROOT/'tools/lightrecon_test/lightrecon_hidden_tool.cpp',ROOT/'tools/lightrecon_test/CMakeLists.txt']
    files += [ROOT/'shared/rd-rend2/lightrecon'/n for n in('hidden_candidates.h','hidden_candidates.cpp','evidence.h','evidence.cpp','offline.cpp','CMakeLists.txt','bake_forward.h','bake_forward.cpp','visibility.cpp','regions.cpp')]
    files += [ROOT/'shared/rd-rend2/lightrecon/regions.h']
    hashes={p.resolve().relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest()for p in files}
    key=hashlib.sha256(json.dumps(semantic(first['cases']),sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    witness=dict(schema='org.openjk.s04-hidden-candidates-validation.v1',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        source_sha256_lf=hashes,executable_sha256=hashlib.sha256(a.tool.read_bytes()).hexdigest(),semantic_sha256=key,repeated_identical=True,cpu=cpu,cross_compiler_checked=True,cases=cases,
        scope=dict(reconstructed=False,runtime_sources=0,physical_intensity='Unavailable',confidence_probability='Unknown',stock_source_truth='Unavailable',stock_bake_profile='UnknownProfile'),
        enum_legend=dict(source_kind={'0':'point','1':'spot'},classification={'0':'physical emitter CANDIDATE, not verified','1':'ambiguous source/transport','2':'transport evidence'},feasibility={'0':'proven static free','1':'plausible unverified','2':'boundary','3':'region/visibility unknown'},methods={'0':'surface response','1':'direction intersection','2':'refinement'}),
        limitations=['Finite searched hypotheses, not a completed reconstruction','Two fixed probe amplitudes; no final photometric fit','Spot axis/cone initialization incomplete','Stock has no source recall or XYZ ground truth','Known source fixtures are development checks, not untouched held-out-scene generalization; bounds bug was diagnosed with development answers','Search/output and observation sampling caps remain explicit','No HDR candidate scoring or full inverse solver','Moving-state uncertainty is limited to supplied domain/region/visibility contracts; no dynamic scene inference'])
    a.output.mkdir(parents=True,exist_ok=True);write(a.output/'validation.json',witness)
    write(a.output/'measurements.json',dict(schema='org.openjk.s04-resource-measurements.v1',semantic_sha256=key,first=resources,
        second={n:dict(process=c.get('resources'),stages=c.get('diagnostic',{}).get('resources'))for n,c in second['cases'].items()},metric='Windows process peak working set; geometry/build and search are separate from accounted estimator bounds'))
    print(json.dumps(dict(semantic_sha256=key,repeated=True,cases=len(cases),cpu_checks=cpu['checks'])))
if __name__=='__main__':main()
