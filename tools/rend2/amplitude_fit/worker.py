#!/usr/bin/env python3
"""Blind S06 controller: all optimization and predictions execute in production C++."""
import argparse
import json
from pathlib import Path
import sys
import time
HERE=Path(__file__).resolve().parent
FIXED=HERE.parent/'fixed_geometry';DATASETS=HERE.parent/'datasets'
sys.path[:0]=[str(FIXED),str(DATASETS)]
from solver_view import TrainingView
from common import SCHEMA,PROTOCOL,load,write,digest,canonical,validate_rows,admission
from client import Client,ForwardUnavailable
from guard import install

def main():
    p=argparse.ArgumentParser();p.add_argument('--inputs',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--exe',type=Path,required=True);p.add_argument('--deny-probe',type=Path,action='append',default=[])
    a=p.parse_args();inputs=a.inputs.resolve();work=a.work.resolve();exe=a.exe.resolve();work.mkdir(parents=True,exist_ok=True)
    install(inputs,work,exe,[Path(__file__),*[FIXED/n for n in ('client.py','common.py','guard.py')],DATASETS/'solver_view.py'])
    cfg=load(inputs/'fit-config.json');view=TrainingView(inputs,cfg['map_sha256']);rows=list(view.observations());validate_rows(rows)
    if cfg['schema']!=SCHEMA or cfg['model']not in ('emitters','ambient'):raise ValueError('Unexpected fit context')
    denied=[]
    for path in a.deny_probe:
        try:path.read_bytes();raise ValueError('Private read permitted')
        except PermissionError:denied.append('existing-private-file-'+str(len(denied)))
    geometry=inputs/cfg['geometry_request']
    if geometry.parent!=inputs or digest(geometry)!=cfg['geometry_sha256']:raise ValueError('Geometry identity mismatch')
    gate=admission(rows)
    if gate['status']!='Admitted':write(work/'fit.json',dict(status=gate['status'],fitted_rgb=None,admission=gate));return 0
    start=time.perf_counter()
    with Client(exe,geometry,8) as client:
        print(json.dumps(dict(event='forward_started',pid=client.process.pid)),flush=True)
        if client.hello['map_sha256']!=cfg['map_sha256']or client.hello['rows']!=len(rows):raise ValueError('Training correspondence')
        for i,(g,row)in enumerate(zip(client.hello['receiver_geometry'],rows)):
            expected=[i+1,row['surface']+1,*row['position'],*row['normal']]
            if len(g)!=8 or any(abs(x-y)>1e-4 for x,y in zip(g,expected)):raise ValueError('Receiver geometry mismatch')
        expected=[[c['id'],*c['xyz']]for c in cfg['candidate_geometry']]
        if client.hello['candidate_geometry']!=expected:raise ValueError('Candidate geometry mismatch')
        policy=cfg['solver'];frame=[policy['initial'],policy['upper'],24,10000,1,4,0,int(cfg['model']=='ambient'),len(rows)]
        for row in rows:frame += [*row['raw'],row['area'],1]
        client.process.stdin.write('F '+' '.join(format(v,'.17g')for v in frame)+'\n');client.process.stdin.flush();cpp=client._read()
        if cpp['type']!='production_fit'or cpp['code']or not cpp['fitted']:raise ForwardUnavailable('Production amplitude fit unavailable: '+str(cpp))
        fitted=cpp['rgb'];frozen=canonical([v for q in fitted for v in q])
        # Geometry-only independent strict replay AFTER C++ has frozen parameters.
        strict=client.predict([v for q in fitted for v in q],'A'if cfg['model']=='ambient'else'R')
        result=dict(schema=SCHEMA,protocol=PROTOCOL,status='FrozenDiagnosticFit',optimizer='ProductionCppAmplitudeFitV1',
            fitted_rgb=fitted,frozen_parameter_sha256=frozen,optimizer_stop=cpp['stop'],converged=cpp['converged'],
            amplitude_unit='CompilerRelativeRGB_NOT_candela',runtime_publication=False,physical_emitter_confidence=None,
            candidate_geometry=cfg['candidate_geometry'],training_ids_sha256=canonical([r['id']for r in rows]),
            partition_hashes=load(inputs/'manifest.json')['sha256'],denied_private_reads=denied,
            profile_key=cpp['profile_key'],production= {k:v for k,v in cpp.items()if k!='seconds'},
            training_objective=cpp['objective'],maximum_training_interval_error=cpp['maximum_interval_error'],
            strict_replay_max_difference=cpp['strict_difference'],admission=gate,
            conditional_feasible_slices=[dict(status='ConditionalFeasibleSlice'if s['available']else'Unavailable',lower=s['lower'],upper=s['upper'],reason='Linear conditional feasibility only; no nonlinear or joint CI')for s in cpp['conditional_slices']],
            timing=dict(worker_wall_seconds=time.perf_counter()-start,cpp_fitting_seconds=cpp['seconds'],cpp_replay_seconds=client.seconds),work=dict(fitting=cpp['work'],independent_replay=client.work))
    write(work/'fit.json',result);print(json.dumps(dict(event='finished',status=result['status'])),flush=True);return 0

if __name__=='__main__':raise SystemExit(main())
