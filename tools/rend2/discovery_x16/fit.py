#!/usr/bin/env python3
"""Training-only kind/shard admission -> native S05 and Joint.

Private source answers are deliberately absent from this controller. Final
values are read only for frozen predictions; never used for admission/settings.
"""
import argparse
from collections import defaultdict,Counter
import json
import math
from pathlib import Path
import subprocess
import sys
import threading
import time
import shutil
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.validation.bridge import TrainingView,EvaluationView,select,score
from tools.rend2.all_stock.run import write,sha
from tools.rend2.amplitude_fit.benchmark import peak


def frozen_geometry(path):
    """Public V02 discovered-candidate request; contains no oracle XYZ.

    Reuse its already frozen role geometry without opening evaluation RGB.
    """
    lines=path.read_text().splitlines()
    at=3+int(lines[2]);folds={}
    for role in ('train','validation','heldout'):
        count=int(lines[at]);at+=1;rows=[]
        for line in lines[at:at+count]:
            v=line.split();ident=int(v[0])
            row=dict(id=format(ident-1,'015x'),surface=int(v[1]),slot=int(v[2]),x=int(v[3]),y=int(v[4]),
                position=list(map(float,v[5:8])),normal=list(map(float,v[8:11])),area=float(v[11]))
            if role=='train':row['raw']=list(map(int,v[14:17]))
            rows.append(row)
        folds[role]=rows;at+=count
    return folds


def admit(folder,label,profile,cap=16):
    if label=='baseline' and (folder/'legacy_adapter').exists():label='legacy_adapter'
    groups=defaultdict(list)
    for shard in range(1,9):
        for c in json.loads((folder/label/f'search-{shard}.json').read_text())['candidates']:
            # Pure transport evidence is not a local physical-emitter model.
            # Ambiguous source/transport hypotheses remain conditional alternatives.
            if c.get('kind',1)==2:continue
            linear='/linear/' in c['profile']
            if linear!=bool(profile):continue
            groups[(shard,c['source_kind'])].append(c)
    for group in groups.values():group.sort(key=lambda c:(
        c['shape_score'] is None,c['shape_score'] if c['shape_score'] is not None else math.inf,c['id']))
    chosen=[];seen=set()
    for rank in range(2):
        for key in sorted(groups):
            group=groups[key]
            if rank>=len(group):continue
            c=group[rank]
            if c['id'] in seen:continue
            seen.add(c['id']);chosen.append(dict(c,shard=key[0]))
            if len(chosen)==cap:return chosen
    return chosen


def fit(a,variant,label,profile,geometry,finite,out):
    original=a.bridge/'bridge'/variant
    m=json.loads((original/'views/inputs/manifest.json').read_text())
    training=TrainingView(original/'views/inputs',m['map_sha256'])
    folds=frozen_geometry(original/'discovered/request.txt')
    targets={}
    final_allowed=False
    def target_rows(role):
        if role=='heldout' and not final_allowed:raise ValueError('Final targets remain sealed')
        if role not in targets:
            evaluator=EvaluationView(original/'views/evaluator',m['map_sha256'],permitted=(role,))
            by_id={int(r['id'][:15],16)+1:r for r in evaluator.observations(role)}
            targets[role]=[by_id[int(r['id'],16)+1] for r in folds[role]]
        return targets[role]
    candidates=admit(a.proposals/variant,label,profile)
    out.mkdir(parents=True);write(out/'admitted.json',candidates)
    pool_label='legacy_adapter' if label=='baseline' else label
    chosen={c['id'] for c in candidates};deferred=[]
    for shard in range(1,9):
        for c in json.loads((a.proposals/variant/pool_label/f'search-{shard}.json').read_text())['candidates']:
            if ('/linear/' in c['profile'])==bool(profile) and c['id'] not in chosen:
                deferred.append(dict(c,shard=shard,deferral='Transport evidence, not a physical emitter' if c.get('kind',1)==2 else
                    'Bounded shard/kind admission; not rejected existence'))
    write(out/'deferred.json',deferred)
    lines=['JKJOINT16',str(profile),f'{int(geometry)} {int(finite)}',str(len(candidates))]
    for c in candidates:
        # Same sampled XYZ point/spot records are numerical alternatives only,
        # not an assertion that nearby lamps share physical identity.
        family=int(sha_bytes(str((c['shard'],c['position'])))[:15],16)+1
        lines.append(' '.join(map(str,[c['id'],c['source_kind'],family,c['shard'],*c['position'],
            *c['outward'],c.get('historical_target_distance') or 128,c.get('historical_radius') or 64])))
    indices={}
    for role in ('train','validation','heldout'):
        indices[role]={};lines.append(str(len(folds[role])))
        for i,r in enumerate(folds[role]):
            ident=int(r['id'][:15],16)+1;indices[role][ident]=i+1
            block=1+sum((math.floor(v/128)+1000)*10000**k for k,v in enumerate(r['position']))
            row=[ident,r['surface'],r['slot'],r['x'],r['y'],*r['position'],*r['normal'],r['area'],block,r['surface']+1]
            if role=='train':row+=r['raw']
            lines.append(' '.join(map(str,row)))
    path=out/'request.txt';path.write_text('\n'.join(lines)+'\n')
    events=[];receipts=[];started=time.perf_counter();expired=[]
    with (out/'stderr.log').open('w') as stderr:
        with subprocess.Popen([str(a.tool),str(training.geometry_path),str(path)],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=stderr,text=True,bufsize=1) as child:
            def stop():
                expired.append(True)
                if child.poll() is None:child.kill()
            timer=threading.Timer(600,stop);timer.start()
            try:
                for line in child.stdout:
                    event=json.loads(line);events.append(event)
                    if event['event'] in ('validation','final'):
                        role='validation' if event['event']=='validation' else 'heldout'
                        prediction=[dict(p,id=indices[role][p['id']]) for p in event['predictions']]
                        if role=='heldout':final_allowed=True
                        receipt=score(target_rows(role),prediction);receipts.append(dict(role=role,score=receipt))
                        child.stdin.write(('SCORE ' if role=='validation' else 'FINAL ')+
                            ' '.join(map(str,[int(receipt['available']),int(receipt['positive']),int(receipt['required']),
                                            receipt['mse'],receipt['black'],receipt['objective'],receipt['worst']]))+'\n')
                        child.stdin.flush()
                child.wait(timeout=10);memory=peak(child)
            finally:timer.cancel()
    write(out/'events.json',events)
    if expired or (child.returncode and (not events or events[-1].get('event')!='result')):
        record=dict(status='Budget' if expired else 'Failed',seconds=time.perf_counter()-started,
                    last_events=len(events),reason=(out/'stderr.log').read_text()[-1500:],
                    budget_scope='OS wall-time safety' if expired else None,
                    peak_rss_bytes=memory,validation=receipts,heldout=None,
                    private_answers_read=False,heldout_used_for_fit=False,heldout_targets_opened=final_allowed,
                    public_request_sha256=sha(path),deferred_count=len(deferred))
        write(out/'result.json',record);return record
    result=events[-1];heldout=None
    if result.get('preferred_available'):
        final_allowed=True
        predictions=[dict(p,id=indices['heldout'][p['id']]) for p in result['heldout_predictions']]
        heldout=score(target_rows('heldout'),predictions)
    diagnostics=[]
    if result.get('preferred_available'):
        for frozen in result.get('frozen_alternative_diagnostics',[]):
            predictions=[dict(p,id=indices['heldout'][p['id']]) for p in frozen['heldout_predictions']]
            diagnostics.append(dict(key=frozen['key'],validation_rank=frozen['validation_rank'],
                sources=frozen['sources'],heldout=score(target_rows('heldout'),predictions),
                post_freeze_rays=frozen['post_freeze_rays'],post_freeze_evaluations=frozen['post_freeze_evaluations'],
                selection_changed=False))
    record=dict(status='Budget' if result.get('state')=='Budget' else 'Completed' if result.get('preferred_available') else 'Unavailable',
        result=result,heldout=heldout,validation=receipts,seconds=time.perf_counter()-started,
        peak_rss_bytes=memory,admitted_types=dict(Counter(c['source_kind'] for c in candidates)),
        deferred_count=len(deferred),final_target_access='Only after frozen preferred model',
        profile=profile,geometry=geometry,finite=finite,public_request_sha256=sha(path),
        private_answers_read=False,heldout_used_for_fit=False,
        heldout_targets_opened=final_allowed,
        public_fold_geometry_sha256=sha(original/'discovered/request.txt'))
    record['frozen_alternative_diagnostics']=diagnostics
    write(out/'result.json',record);return record


def sha_bytes(value):
    import hashlib
    return hashlib.sha256(value.encode()).hexdigest()


def run(a):
    a.output.mkdir(parents=True,exist_ok=False)
    frozen=a.output/'native';frozen.mkdir()
    shutil.copy2(a.tool,frozen/a.tool.name);a.tool=frozen/a.tool.name
    manifest=dict(policy_sha256=sha(Path(__file__).with_name('policy-v6.json')),tool_sha256=sha(a.tool),
        proposals_manifest_sha256=sha(a.proposals/'manifest.json'),cases=[])
    # Same settings/nuisance policy in every catalog comparison. The finite
    # planar-face option is an independently labelled structural alternative.
    for variant in a.variants:
        for label,geometry,finite in [('baseline',False,False),('new',False,False),('new',True,True)]:
            if a.cases and ('refine' if geometry else label) not in a.cases:continue
            name=f'{variant}-{label}-geometry{int(geometry)}-finite{int(finite)}-profile{a.profile}'
            result=fit(a,variant,label,a.profile,geometry,finite,a.output/name)
            manifest['cases'].append(dict(name=name,summary=result));write(a.output/'results.json',manifest)
            print(name,result['status'],result.get('result',{}).get('state'),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bridge','proposals','tool','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--variants',nargs='+',default=['direct','passive_bounce1'])
    p.add_argument('--profile',type=int,choices=(0,1),default=0)
    p.add_argument('--cases',nargs='+',choices=('baseline','new','refine'),
                   help='Explicit subset, same frozen settings and admission; never chosen from final scores')
    a=p.parse_args()
    for n in ('bridge','proposals','tool','output'):setattr(a,n,getattr(a,n).resolve())
    run(a)
