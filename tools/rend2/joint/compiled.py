#!/usr/bin/env python3
"""Trusted S11 compiled-BSP controller. Native child never receives hidden RGB.
Reuses the X07 physically separated dataset views and original compiler locks.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import threading
import time
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'tools/rend2/datasets'))
from solver_view import TrainingView
from evaluation_view import EvaluationView
from tools.rend2.amplitude_fit.benchmark import peak

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path):return json.loads(Path(path).read_text())
def select(rows,maximum=24):
    # Geometry-only deterministic spatial strata; never sorted by target RGB.
    groups={}
    for r in rows:
        if r['normal'] is None or r['position'] is None:continue
        cell=tuple(math.floor(v/64) for v in r['position'])
        groups.setdefault(cell,[]).append(r)
    selected=[]
    for group in groups.values():group.sort(key=lambda r:r['id'])
    for index in range(max(map(len,groups.values()),default=0)):
        for cell in sorted(groups):
            if index<len(groups[cell]):selected.append(groups[cell][index])
            if len(selected)==maximum:return selected
    return selected

def score(rows,predictions):
    if len(predictions)!=len(rows) or not all(p['available'] for p in predictions):
        return dict(available=False,positive=False,required=False,mse=0,black=0,objective=0,worst=0)
    by_id={p['id']:p for p in predictions};groups={};positive=False
    for i,r in enumerate(rows):
        p=by_id[i+1] if i+1 in by_id else None
        if p is None:raise ValueError('Independent prediction identity mismatch')
        block=tuple(math.floor(v/128) for v in r['position']);g=groups.setdefault(block,[0.,0.,0.,0.])
        errors=[];black=[]
        for v,b in zip(p['rgb'],r['raw']):
            if not math.isfinite(v):raise ValueError('Nonfinite native prediction')
            e=min(0.,v-b)+max(0.,v-b-1.) if b<255 else min(0.,v-255.)
            errors.append(e);black.append(float(b));positive|=b>0
        g[0]+=r['area']*sum(e*e for e in errors)/3
        g[1]+=r['area']*sum(b*b for b in black)/3
        g[2]+=r['area']*sum(.5*e*e if abs(e)<=4 else 4*(abs(e)-2) for e in errors)/3
        g[3]+=r['area']
    mse=sum(g[0]/g[3] for g in groups.values())/len(groups)
    black=sum(g[1]/g[3] for g in groups.values())/len(groups)
    worst=max(math.sqrt(g[0]/g[3]) for g in groups.values())
    objective=sum(g[2]/g[3] for g in groups.values())/len(groups)+.25*max(g[2]/g[3] for g in groups.values())
    return dict(available=True,positive=positive,required=worst<=2,mse=mse,black=black,objective=objective,worst=worst)

def case(tool,views,bakes,name,out,wrong=False,publish=False):
    directory=views/name;answer=load(bakes/'answers'/f'{name}.q3map-recipe.json')
    bsp=bakes/'authoring/base/maps'/f'{name}.bsp';bsp_hash=sha(bsp)
    if answer['compiled_bsp_sha256']!=bsp_hash:raise ValueError('Pinned BSP mismatch')
    source_lock=load(ROOT/'tools/rend2/datasets/q3map2-validation.json')
    if source_lock['maps'][name]['bsp_sha256']!=bsp_hash or answer['binary_sha256']!=source_lock['compiler']['executable_sha256']:
        raise ValueError('Frozen compiler fixture identity changed')
    training=TrainingView(directory/'views/inputs',bsp_hash)
    evaluator=EvaluationView(directory/'views/evaluator',bsp_hash,permitted=('validation','heldout'))
    rows={role:select(training.observations() if role=='train' else evaluator.observations(role)) for role in ('train','validation','heldout')}
    if any(len(v)<8 for v in rows.values()):raise ValueError('Missing positive independent spatial coverage')
    sources=answer['ground_truth']['lights']
    # XYZ is an explicit fixed-geometry benchmark prior. RGB never enters child input.
    candidates=[s['position_world'] for s in sources]
    lines=['JKJOINT1',str(int(wrong)),str(len(candidates))]
    if publish:
        coverage=load(directory/'views/coverage.json');source=coverage['source']
        partitions=[sha(directory/'views'/('inputs' if role=='train' else 'evaluator')/(role+'.jsonl')) for role in rows]
        counts=[source['raw_pixels']//(128*128),source['raw_pixels'],coverage['derived_texel_denominator'],coverage['groups']['map']['counts'].get('mapped',0),int(coverage['all_pages_complete'])]
        # Mapped coverage uses existing categorized report; never call the fitting subset a full map.
        counts[3]=coverage['groups']['map']['counts'].get('mapped',0)
        lines=['JKJOINT13',' '.join(partitions),' '.join(map(str,counts)),str(int(wrong)),str(len(candidates))]
    for i,p in enumerate(candidates):lines.append(' '.join(map(str,[i+1,*p])))
    ids={};all_ids=set()
    for role in ('train','validation','heldout'):
        ids[role]={};lines.append(str(len(rows[role])))
        for index,r in enumerate(rows[role]):
            ident=int(r['id'][:15],16)+1
            if ident in all_ids:raise ValueError('Duplicate/overlapping partition sample')
            all_ids.add(ident);ids[role][ident]=index+1
            block=1+sum((math.floor(v/128)+1000)*(10000**k) for k,v in enumerate(r['position']))
            values=[ident,r['surface'],r['slot'],r['x'],r['y'],*r['position'],*r['normal'],r['area'],block,r['surface']+1]
            if role=='train':values+=r['raw']
            lines.append(' '.join(map(str,values)))
    request=out/(name+('-wrong' if wrong else '')+'.txt');request.write_text('\n'.join(lines)+'\n')
    events=[];start=time.perf_counter()
    command=[str(tool.resolve()),str(training.geometry_path),str(request.resolve())]
    if publish:command.append(str((out/(request.stem+'-cache')).resolve()))
    with subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1) as child:
        expired=[]
        def stop():
            expired.append(True)
            if child.poll() is None:child.kill()
        timer=threading.Timer(120,stop);timer.daemon=True;timer.start()
        try:
            for line in child.stdout:
                event=json.loads(line);events.append(event)
                if event['event'] in ('validation','final'):
                    role='validation' if event['event']=='validation' else 'heldout'
                    prediction=[dict(p,id=ids[role][p['id']]) for p in event['predictions']]
                    s=score(rows[role],prediction)
                    child.stdin.write(('SCORE ' if role=='validation' else 'FINAL ')+' '.join(map(str,[int(s['available']),int(s['positive']),int(s['required']),s['mse'],s['black'],s['objective'],s['worst']]))+'\n');child.stdin.flush()
            _,stderr=child.communicate(timeout=10)
            rss=peak(child)
        finally:timer.cancel()
        (out/(request.stem+'.stderr')).write_text(stderr)
        if expired or child.returncode:raise RuntimeError(f'{name}: native failure {stderr} {events[-1:]}')
    result=events[-1]
    if result['event']!='result':raise ValueError('Missing frozen result')
    if result['preferred_available']:
        predicted=[dict(p,id=ids['heldout'][p['id']]) for p in result.pop('heldout_predictions')]
        held=score(rows['heldout'],predicted)
    else:held=dict(available=False)
    private_parameters=[]
    for fitted in result['sources']:
        index=fitted['id']-1
        target=[sources[index]['authored_intensity']*c for c in sources[index]['rgb_parameter']]
        private_parameters.append(dict(candidate=fitted['id'],relative_rgb_error=math.dist(target,fitted['rgb'])/math.sqrt(sum(v*v for v in target)),true_compiler_rgb=target,fitted_compiler_rgb=fitted['rgb']))
    report=dict(name=name,wrong_profile=wrong,bsp_sha256=bsp_hash,compiler_sha256=answer['binary_sha256'],
        geometry_sha256=sha(training.geometry_path),request_sha256=sha(request),
        partition_sha256={role:sha(directory/'views'/('inputs' if role=='train' else 'evaluator')/(role+'.jsonl')) for role in rows},
        counts={role:len(values) for role,values in rows.items()},result=result,heldout=held,
        source_parameters=private_parameters,validation_calls=sum(e['event']=='validation' for e in events),
        protocol='Fixed XYZ prior + native Joint::Solve structural/amplitude search, no hidden RGB/answers in native inputs',
        historical_compiler_identified=False,producer_publication=publish)
    if not wrong and (not held.get('available') or held['mse']>1 or held['mse']>=held['black']*.02):
        raise ValueError('Required compiled positive held-out acceptance failed '+str(report))
    return report,dict(name=request.stem,seconds=time.perf_counter()-start,peak_process_rss_bytes=rss)

def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--views',type=Path,default=ROOT/'build/x07-verified');p.add_argument('--bakes',type=Path,default=ROOT/'build/q3map2-bake-verified')
    p.add_argument('--out',type=Path,required=True);p.add_argument('--only',choices=('point_room','two_colour','door_occlusion'))
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);cases=[];resources=[]
    for name in ((a.only,) if a.only else ('point_room','two_colour','door_occlusion')):
        for wrong in (False,True):
            r,w=case(a.tool,a.views,a.bakes,name,a.out,wrong);cases.append(r);resources.append(w)
            print(name,'wrong' if wrong else 'correct',r['result']['state'],r['heldout'],flush=True)
    files=['shared/rd-rend2/lightrecon/'+s for s in ('joint.h','joint.cpp','indirect.cpp','emitter_shape.cpp','bake_forward.cpp','visibility.cpp')]
    files+=['tools/lightrecon_test/lightrecon_joint_tool.cpp','tools/rend2/joint/compiled.py']
    (a.out/'compiled.json').write_text(json.dumps(dict(schema='org.openjk.s11-compiled.v1',cases=cases,executable_sha256=sha(a.tool),source_sha256={s:sha(ROOT/s) for s in files}),sort_keys=True,indent=2,allow_nan=False)+'\n')
    (a.out/'resources.json').write_text(json.dumps(resources,sort_keys=True,indent=2)+'\n')

if __name__=='__main__':main()
