#!/usr/bin/env python3
"""Trusted X07 controller. Private answers/held-out colours NEVER enter worker inputs."""
import argparse
import copy
import ctypes
import json
import math
import re
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
for folder in ('datasets','bake_model','shadow_footprint','evaluation/x05'):
    sys.path.insert(0,str(HERE.parent/folder))
from prepare_blind import prepare
from solver_view import TrainingView
from evaluation_view import EvaluationView
from validate_cpp import request, source
from compiler_boundary import load as bsp_geometry
from common import PROTOCOL,SCHEMA,POLICY,load,write,digest,canonical,metrics,admission
from client import Client

FIXTURES=('point_room','two_colour','door_occlusion')
SPLIT=dict(block=128.,gap=1.,seed=20261030,origin=(17.,31.,47.))

def geometry_rows(rows):return [dict(world=r['position'],normal=r['normal'],surface=r['surface']) for r in rows]

def peak(handle):
    if sys.platform!='win32':return None
    class Memory(ctypes.Structure):
        _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in ('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
    m=Memory();m.cb=ctypes.sizeof(m);query=ctypes.WinDLL('psapi').GetProcessMemoryInfo;query.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
    return m.peak_ws if query(int(handle),ctypes.byref(m),m.cb) else None

def launch_worker(exe,inputs,work,denied_paths=()):
    work.mkdir(parents=True,exist_ok=True);start=time.perf_counter();child_handle=None;events=[]
    argv=[sys.executable,'-X','utf8',str(HERE/'worker.py'),'--inputs',str(inputs),'--work',str(work),'--exe',str(exe)]
    for path in denied_paths:
        if not path.is_file():raise ValueError('Private read probe must target an existing file')
        argv+=['--deny-probe',str(path)]
    with subprocess.Popen(argv,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as p:
        expired=[]
        def abort():
            expired.append(True)
            if child_handle and sys.platform=='win32':
                fn=ctypes.WinDLL('kernel32').TerminateProcess;fn.argtypes=[ctypes.c_void_p,ctypes.c_uint];fn(child_handle,124)
            if p.poll() is None:p.kill()
        watchdog=threading.Timer(120,abort);watchdog.daemon=True;watchdog.start()
        try:
            for line in p.stdout:
                event=json.loads(line);events.append(event)
                if event.get('event')=='forward_started' and sys.platform=='win32':
                    fn=ctypes.WinDLL('kernel32').OpenProcess;fn.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];fn.restype=ctypes.c_void_p
                    child_handle=fn(0x0400|0x0010|0x0001,False,event['pid'])
            _,stderr=p.communicate(timeout=15)
        finally:watchdog.cancel()
        worker_peak=peak(p._handle) if sys.platform=='win32' else None
        cpp_peak=peak(child_handle) if child_handle else None
        if child_handle:
            fn=ctypes.WinDLL('kernel32').CloseHandle;fn.argtypes=[ctypes.c_void_p];fn(child_handle)
        (work/'stderr.txt').write_text(stderr)
        if expired:raise RuntimeError('Worker/C++ wall-time budget exceeded (120 s); no accepted fit')
        if p.returncode:raise RuntimeError('Worker failed '+str(p.returncode)+' '+stderr+' '+str(events))
    result=load(work/'fit.json');return result,dict(wall_seconds=time.perf_counter()-start,wall_budget_seconds=120,worker_peak_rss_bytes=worker_peak,cpp_peak_rss_bytes=cpp_peak)

def create_case(name,root,archive_prefix,bsp_dir,recipe_dir,quality,tool):
    start=time.perf_counter();case=root/name;case.mkdir(parents=True);private=case/'private';private.mkdir()
    original=archive_prefix/name;bsp=bsp_dir/(name+'.bsp');answer=load(recipe_dir/(name+'.q3map-recipe.json'))
    if digest(bsp)!=answer['compiled_bsp_sha256']:raise ValueError('Frozen source BSP fingerprint mismatch')
    source_meta=load(original.with_suffix('.source.json'))
    if source_meta['map_sha256']!=digest(bsp):raise ValueError('Archive/BSP identity mismatch')
    witness=load(HERE.parent/'datasets/q3map2-validation.json')
    if digest(bsp)!=witness['maps'][name]['bsp_sha256'] or answer['binary_sha256']!=witness['compiler']['executable_sha256']:raise ValueError('Pinned compiler witness mismatch')
    mapfile=bsp.with_suffix('.map')
    if digest(mapfile)!=answer['map_sha256'] or digest(mapfile)!=witness['maps'][name]['source_map_sha256']:raise ValueError('Authored source-map witness mismatch')
    entities=[dict(re.findall(r'"([^\"]+)"\s+"([^\"]*)"',block)) for block in re.findall(r'\{[^{}]*"classname"\s+"light"[^{}]*\}',mapfile.read_text())]
    authored=answer['ground_truth']['lights']
    if len(entities)!=len(authored) or answer['ground_truth']['bounce_iterations']!=0:raise ValueError('Unsupported authored source recipe')
    for entity,truth in zip(entities,authored):
        if float(entity['light'])!=truth['authored_intensity'] or [float(v)for v in entity['_color'].split()]!=truth['rgb_parameter'] or [float(v)for v in entity['origin'].split()]!=truth['position_world'] or max(truth['rgb_parameter'])!=1:
            raise ValueError('Private answer does not match pinned source authoring')
    for argv in ([tool,'validate',original.with_suffix('.jkobs')],[quality,'public-check',original.with_suffix('.public.jkobs'),'unused']):
        r=subprocess.run(list(map(str,argv)),capture_output=True,text=True)
        if r.returncode:raise ValueError('Canonical dataset validation: '+r.stdout+r.stderr)
    # Fixed geometry-only criterion, declared before any fit/held-out scoring.
    selected=[]
    with original.with_suffix('.rows.jsonl').open() as original_rows:
        for line in original_rows:
            r=json.loads(line);p=r.get('position');n=r.get('normal')
            if (r['coverage']==2 and p and n and abs(p[2])<1e-5 and n[2]>.99999 and abs(p[0])<=176 and abs(p[1])<=112):selected.append(r)
    prefix=private/'subset';shutil.copyfile(original.with_suffix('.public.jkobs'),prefix.with_suffix('.public.jkobs'))
    source_meta.update(derived_complete=False,scope='X07 predeclared interior planar floor subset, not full-map coverage')
    write(prefix.with_suffix('.source.json'),source_meta)
    prefix.with_suffix('.rows.jsonl').write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in selected))
    report=prepare(prefix,case/'views',**SPLIT)
    roles={}
    for role in ('train','validation','heldout'):
        path=case/'views'/('inputs' if role=='train' else 'evaluator')/(role+'.jsonl');roles[role]=[json.loads(s) for s in path.read_text().splitlines()]
        if len(roles[role])<4:raise ValueError('Predeclared split lacks required spatial support '+name+' '+role)
    # Check immutable archive RGB against ORIGINAL bytes; no re-encoding/readback.
    lumps,_,casters=bsp_geometry(bsp,strict_receiver_uv=False)
    for rows in roles.values():
        for r in rows:
            image=min(p['image'] for p in source_meta['pages'] if p.get('page',p['image'])==r['page'] and not p['missing'])
            offset=(image*128*128+r['y']*128+r['x'])*3
            if list(lumps[14][offset:offset+3])!=r['raw']:raise ValueError('Original lightmap bytes mismatch')
    candidates=[dict(id=i+1,kind='Point',xyz=s['position_world']) for i,s in enumerate(answer['ground_truth']['lights'])]
    write(private/'answer.json',dict(rgb=[[s['authored_intensity']*v for v in s['rgb_parameter']] for s in answer['ground_truth']['lights']],bsp_sha256=digest(bsp)))
    return dict(name=name,case=case,bsp=bsp,lumps=lumps,casters=casters,roles=roles,candidates=candidates,
                selected=len(selected),partition_counts=report['partitions'],partition_exclusions=report['partition_exclusions'],
                answer=answer,source_capture_sha256=digest(original.with_suffix('.jkobs')),loading_seconds=time.perf_counter()-start)

def install_model(case,model,out):
    if out.exists():raise ValueError('Use a fresh model/run directory')
    inputs=out/'inputs';shutil.copytree(case['case']/'views/inputs',inputs)
    candidates=copy.deepcopy(case['candidates']);settings={}
    if model=='wrong_profile':settings=dict(linear=True,angle=True)
    if model=='wrong_xyz':
        candidates[0]['xyz']=[candidates[0]['xyz'][0]+64,candidates[0]['xyz'][1]+48,candidates[0]['xyz'][2]]
    if model=='dropped_source':candidates=candidates[:1]
    if model=='near_duplicate':
        extra=copy.deepcopy(candidates[0]);extra['id']=len(candidates)+1;extra['xyz'][0]+=.01;candidates.append(extra)
    sources=[source(position=c['xyz'],rgb=(0,0,0)) for c in candidates]
    rows=case['roles']['train'];text=request(case['bsp'],case['lumps'],case['casters'],geometry_rows(rows),settings,sources)
    (inputs/'request.txt').write_text(text)
    config=dict(schema=SCHEMA,map_sha256=digest(case['bsp']),geometry_request='request.txt',geometry_sha256=digest(inputs/'request.txt'),
        candidate_geometry=candidates,model='ambient' if model=='ambient' else 'emitters',solver=POLICY,
        profile=dict(family='NetRadiant-d9338309e-subsets-v2',selector='linear-cosine' if model=='wrong_profile' else 'isq-cosine',
                     scope='Controlled direct style0 planar floor, gamma1, compensate1, exposure0, bounce0; general trace conditional'),
        provenance=dict(protocol=PROTOCOL,xyz='Allowed supplied fixed prior; not inferred',absolute_scale='Unknown legacy compiler scale'))
    write(inputs/'fit-config.json',config);return inputs,config,settings,sources

def evaluate_model(exe,case,model,out,fit,cfg,settings,sources):
    frozen=digest(out/'work/fit.json');x=[v for s in fit['fitted_rgb'] for v in s];evaluation={};work=[]
    for role in ('train','validation','heldout'):
        if role=='train':rows=list(TrainingView(out/'inputs',digest(case['bsp'])).observations())
        else:rows=list(EvaluationView(case['case']/'views/evaluator',digest(case['bsp']),permitted=(role,)).observations(role))
        path=out/(role+'.request');path.write_text(request(case['bsp'],case['lumps'],case['casters'],geometry_rows(rows),settings,sources))
        with Client(exe,path,4) as client:
            result=client.predict(x,'A' if model=='ambient' else 'R');zero=client.predict([0.]*len(x),'A' if model=='ambient' else 'R')
            score=metrics(rows,result['rgb']);black=metrics(rows,zero['rgb']);score.update(black_interval_rmse=black['area_interval_rmse'],improvement_over_black=1-score['area_interval_rmse']/black['area_interval_rmse'] if black['area_interval_rmse'] else None,admission=admission(rows),visibility=client.hello['visibility'],requested_count=len(rows),comparable_count=len(rows),unavailable_count=0,production_prediction_status_codes=sorted(set(result['status'])))
            evaluation[role]=score;work.append(client.work)
    if digest(out/'work/fit.json')!=frozen:raise ValueError('Fitted parameters changed during trusted evaluation')
    truth=load(case['case']/'private/answer.json')['rgb'];parameters=[]
    for candidate,estimated in zip(cfg['candidate_geometry'],fit['fitted_rgb']):
        index=candidate['id']-1
        if model=='ambient' or index>=len(truth):parameters.append(dict(status='NotComparable',reason='Ambient/extra candidate, not a true source'));continue
        target=truth[index];relative=math.dist(estimated,target)/math.sqrt(sum(v*v for v in target));a=math.sqrt(sum(v*v for v in estimated));b=math.sqrt(sum(v*v for v in target))
        angle=math.degrees(math.acos(max(-1.,min(1.,sum(u*v for u,v in zip(estimated,target))/(a*b))))) if a*b else None
        parameters.append(dict(candidate=candidate['id'],fitted_compiler_rgb=estimated,true_compiler_rgb=target,relative_rgb_l2_error=relative,compiler_chromaticity_angle_degrees=angle,absolute_photometry='NotComparable'))
    held=evaluation['heldout'];success=held['admission']['status']=='Admitted' and held['admission']['positive_spatial_blocks']>=2 and held['prediction_coverage']==1 and held['area_interval_rmse']<=1 and held['maximum_interval_error']<=1 and held['improvement_over_black']>=.5
    extra={}
    if model=='correct':
        # Independent oracle audit AFTER freezing. This is never sent to the worker.
        oracle=[v for source in truth for v in source];target_rows=case['roles']['heldout']
        path=out/'heldout.request'
        with Client(exe,path,2) as client:
            replay=client.predict(oracle,'R');extra['true_amplitudes_strict_cpp_heldout']=metrics(target_rows,replay['rgb']);work.append(client.work)
        coverage=[]
        for i,v in enumerate(oracle):
            interval=fit['conditional_feasible_slices'][i]
            coverage.append(interval.get('status')=='ConditionalFeasibleSlice' and interval['lower']<=v<=interval['upper'])
        extra['truth_in_conditional_parameter_slices']=coverage
        extra['slice_warning']='Conditional slices hold OTHER fitted coordinates fixed; they are not joint confidence intervals or calibrated posterior probabilities'
    if model=='near_duplicate':
        alternative=copy.deepcopy(fit['fitted_rgb']);alternative[0]=[a+b for a,b in zip(alternative[0],alternative[-1])];alternative[-1]=[0.,0.,0.]
        with Client(exe,out/'heldout.request',2) as client:
            replay=client.predict([v for rgb in alternative for v in rgb],'R');extra['joint_alternative']=dict(rgb=alternative,heldout=metrics(case['roles']['heldout'],replay['rgb']),meaning='Near-null source allocation direction, not a second verified emitter');work.append(client.work)
    return dict(prediction=evaluation,source_parameters=parameters,heldout_positive_prediction_gate=success,
                supplied_candidate_count=len(cfg['candidate_geometry']),hidden_true_source_count=len(truth),source_detection_accuracy='NotEstimated: candidate XYZ/count/type supplied, not discovered',
                evaluation_work=work,frozen_fit_sha256=frozen,hidden_parameters_read_only_by_controller=True,**extra)

def semantic_fit(fit):
    return {k:v for k,v in fit.items() if k!='timing'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--only',choices=FIXTURES)
    p.add_argument('--exe',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe')
    p.add_argument('--quality',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe')
    p.add_argument('--dataset-tool',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_tool.exe')
    p.add_argument('--datasets',type=Path,default=ROOT/'build/x02/compiled-fixtures-final');p.add_argument('--bakes',type=Path,default=ROOT/'build/q3map2-bake-verified')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);summary=dict(schema=SCHEMA,protocol=PROTOCOL,cases={},policy=POLICY,split=SPLIT,runtime_publication=False)
    experiment_start=time.perf_counter();experiment_cpu=time.process_time()
    for name in ((a.only,) if a.only else FIXTURES):
        case=create_case(name,out,a.datasets.resolve(),a.bakes.resolve()/'authoring/base/maps',a.bakes.resolve()/'answers',a.quality.resolve(),a.dataset_tool.resolve())
        records={}
        models=['correct','ambient','wrong_profile','wrong_xyz']+(['dropped_source'] if name=='two_colour' else [])+(['near_duplicate'] if name=='point_room' else [])
        for model in models:
            folder=case['case']/model;inputs,cfg,settings,sources=install_model(case,model,folder)
            denied=[case['case']/'views/evaluator/heldout.jsonl',case['case']/'views/evaluator/validation.jsonl',case['case']/'private/answer.json']
            fit,resources=launch_worker(a.exe.resolve(),inputs,folder/'work',denied)
            record=dict(fit=fit,resources=resources)
            if fit['status']=='FrozenDiagnosticFit':record['trusted_evaluation']=evaluate_model(a.exe.resolve(),case,model,folder,fit,cfg,settings,sources)
            records[model]=record;write(folder/'diagnostic.json',record)
            print(name,model,fit['status'],record.get('trusted_evaluation',{}).get('prediction',{}).get('heldout',{}).get('area_interval_rmse'),flush=True)
        # Second complete blind fit; no previous amplitudes are given as a warm start.
        repeat,repeat_resources=launch_worker(a.exe.resolve(),case['case']/'correct/inputs',case['case']/'repeat/work',denied)
        if semantic_fit(repeat)!=semantic_fit(records['correct']['fit']):raise ValueError('Repeated fit differs semantically')
        records['correct']['repeat_identical']=True;records['correct']['repeat_resources']=repeat_resources
        if not records['correct'].get('trusted_evaluation',{}).get('heldout_positive_prediction_gate'):
            raise ValueError('Required supported positive-light coverage/prediction gate failed '+name)
        summary['cases'][name]=dict(bsp_sha256=digest(case['bsp']),source_capture_sha256=case['source_capture_sha256'],selected_geometry_receivers=case['selected'],partition_counts=case['partition_counts'],partition_exclusions=case['partition_exclusions'],loading_seconds=case['loading_seconds'],models=records)
        correct_error=records['correct']['trusted_evaluation']['prediction']['heldout']['area_interval_rmse']
        summary['cases'][name]['control_separation']={m:dict(heldout_error_above_correct=records[m]['trusted_evaluation']['prediction']['heldout']['area_interval_rmse']-correct_error,
            status='Discriminated' if records[m]['trusted_evaluation']['prediction']['heldout']['area_interval_rmse']-correct_error>=2 else 'ConditionallyIndistinguishable') for m in models if m not in ('correct','near_duplicate')}
        summary['resources']=dict(controller_peak_rss_bytes=peak(-1) if sys.platform=='win32' else None,wall_seconds=time.perf_counter()-experiment_start,controller_cpu_seconds=time.process_time()-experiment_cpu)
        write(out/'summary.json',summary)
    return 0

if __name__=='__main__':raise SystemExit(main())
