#!/usr/bin/env python3
"""Conditional stock-map profile trials on X02 blind inputs and CPU ray masks.

Source geometry is a visible glow centroid POINT hypothesis, never recovered
ground truth. Unknown rays are omitted COMMONLY across compared configurations.
All unmodelled sky/other-source/transport effects remain discrepancy.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'datasets'))
from solver_view import TrainingView
from prepare_blind import assignment, rows as source_rows
from hypotheses import catalogue
from identifiability import Observation, Configuration, compare, retention, write, metrics


def run(root,ray_tool,output):
    registry=catalogue(); profiles=[registry[n] for n in ('NetRadiant-d9338309-JA/isq_cos',
        'NetRadiant-d9338309-JA/linear_noangle','Q3MapOriginal/direct-isq','Q3Map2-2.5.16-JA/direct-isq')]
    result={}
    for name,centre,radius,description in [('hoth3',(3320,700,123),1024.,'indoor local-fixture/wall region'),
        ('hoth2',(1728,-576,1040),1536.,'outdoor-map region; sun/sky NOT replayed by point hypotheses')]:
        base=root/name; views=root/(name+'-views')
        source=json.loads(base.with_suffix('.source.json').read_text())
        facts=json.loads(base.with_suffix('.facts.json').read_text())
        priors=sorted(facts['fixture_geometry_priors'],key=lambda p:(math.dist(p['position'],centre),p['id']))
        chosen=[]
        for p in priors:
            if not chosen or all(math.dist(p['position'],q['position'])>64 for q in chosen): chosen.append(p)
            if len(chosen)==2: break
        if len(chosen)<2:
            result[name]=dict(status='InsufficientFixturePriors',historical_profile='UnknownProfile'); continue
        rows={}
        train=TrainingView(views/'inputs',source['map_sha256'])
        # Nested X03 folds use ONLY X02 outer-training RGB. The untouched X02
        # validation/held-out target files are not opened at all in this trial.
        outer=[r for r in train.observations() if math.dist(r['position'],centre)<=radius]
        ids={r['id'] for r in outer}; metadata={}
        for r in source_rows(base.with_suffix('.rows.jsonl')):
            if r['id'] in ids:
                metadata[r['id']]=dict(footprint_radius=r['footprint_radius'],kind=r['kind'],
                    shading_normal=r['shading_normal'],normal=r['normal'])
        nested={'train':[],'validation':[],'heldout':[]}
        for r in outer:
            m=metadata[r['id']]
            if m['kind']!=0 or m['shading_normal'] is None: continue
            if sum(a*b for a,b in zip(m['shading_normal'],m['normal']))<.999: continue
            shifted=[x-o for x,o in zip(r['position'],(17.,31.,47.))]
            role=assignment(dict(position=shifted,footprint_radius=m['footprint_radius']),256.,4.,20261027)
            if role: nested[role].append(r)
        hints=output/(name+'-hints.tsv')
        dumped=subprocess.run([str(ray_tool),str(train.geometry_path),'--hints',str(hints)],capture_output=True,text=True)
        if dumped.returncode: raise RuntimeError(dumped.stderr)
        normals={line.split()[0]:tuple(map(float,line.split()[4:7])) for line in hints.read_text().splitlines()}
        for p in chosen:
            normal=normals.get(p['id'],(0.,0.,0.))
            p['geometry_centroid']=p['position'][:]
            p['assumed_outward_offset_world']=4.
            p['position']=[x+4*n for x,n in zip(p['position'],normal)]
        confs=[Configuration('fixture_'+str(i),(tuple(p['position']),),((0.,0.,-1.),)) for i,p in enumerate(chosen)]
        for role,stream in nested.items():
            selected={}
            for r in stream:
                if math.dist(r['position'],centre)>radius: continue
                # One sample per spatial cell; selection never examines RGB.
                cell=tuple(math.floor(v/64) for v in r['position'])
                if cell not in selected or r['id']<selected[cell]['id']: selected[cell]=r
            selected=sorted(selected.values(),key=lambda r:r['id'])[:48]
            rows[role]=selected
        query=output/(name+'-queries.tsv'); answer=output/(name+'-rays.tsv')
        with query.open('w') as stream:
            stream.write('map '+source['map_sha256']+'\n')
            for role,values in rows.items():
                for row in values:
                    for i,p in enumerate(chosen):
                        fields=[row['id'],str(i),*row['position'],*row['normal'],row['surface'],p['id'],*p['position']]
                        stream.write(' '.join(map(str,fields))+'\n')
        process=subprocess.run([str(ray_tool),str(train.geometry_path),str(query),str(answer)],capture_output=True,text=True)
        (output/(name+'-rays.log')).write_text(process.stdout+process.stderr)
        if process.returncode: raise RuntimeError(process.stderr)
        state={}
        for line in answer.read_text().splitlines():
            ident,index,value,*_=line.split(); state[(ident,int(index))]=None if value=='unknown' else value=='visible'
        subsets={}; masks={}; unknown={}
        for role,values in rows.items():
            usable=[r for r in values if all(state[(r['id'],i)] is not None for i in range(len(chosen)))]
            unknown[role]=len(values)-len(usable)
            subsets[role]=[Observation(r['id'],tuple(r['position']),tuple(r['normal']),tuple(r['raw']),
                ','.join(str(math.floor((v-o)/256)) for v,o in zip(r['position'],(17.,31.,47.))),
                role,metadata[r['id']]['footprint_radius']) for r in usable]
        trials={}
        ray_response={}
        for i,configuration in enumerate(confs):
            masks={role:[[state[(r.id,i)]] for r in data] for role,data in subsets.items()}
            ray_response[configuration.id]={role:dict(visible=sum(v[0] is True for v in masks[role]),
                blocked=sum(v[0] is False for v in masks[role])) for role in masks}
            if min(map(len,subsets.values()))<3:
                trials[configuration.id]=dict(status='InsufficientCertifiedCommonObservations'); continue
            if not any(v[0] for role in masks for v in masks[role]):
                trials[configuration.id]=dict(status='NoDirectResponseOnCertifiedSubset',ranking=[],
                    historical_profile='UnknownProfile',
                    diagnostic_black_prediction={role:metrics([[0.,0.,0.]]*len(data),data)
                                                 for role,data in subsets.items()},
                    explanation='All certified rays blocked; these fixed sources cannot distinguish attenuation. '
                                'Positive targets require other sources/transport, not a preferred compiler.')
                continue
            for label,recipes,fill in [('fixed_calibration',None,0.),('encoding_fill_nuisance',
                [dict(gamma=g,compensate=c,exposure=0.,srgb=False) for g,c in ((1.,1.),(2.2,1.),(1.,2.))],32.)]:
                fitted=compare(profiles,subsets['train'],subsets['validation'],subsets['heldout'],[configuration],
                    encodings=recipes,fill_max=fill,amplitude_max=10000.,passes=2,masks=masks)
                trials[configuration.id+'/'+label]=dict(comparison=fitted,retention=retention(fitted))
        result[name]=dict(status='ConditionalSimplifiedExperiments',description=description,
            partition_protocol='nested X03 blocks from X02 outer-training ONLY; origin=(17,31,47), block=256, gap=4, seed=20261027',
            outer_evaluation_targets_loaded=False,
            source_map_sha256=source['map_sha256'],geometry_priors=chosen,
            selected_counts={p:len(v) for p,v in rows.items()},
            common_certified_counts={p:len(v) for p,v in subsets.items()},unknown_rays_excluded=unknown,
            certified_ray_response=ray_response,
            configurations=[asdict(c) for c in confs],trials=trials,
            physical_source_position_error=None,historical_profile='UnknownProfile',
            limitations=['unknown transfer/primaries/absolute scale','point approximation of luminous geometry',
                         'unmodelled other lights, sun/sky, bounce and bake-time materials',
                         'geometry-selected common mask; low sample counts cannot identify profile'])
        write(output/(name+'.json'),result[name]); print(name,result[name]['common_certified_counts'],flush=True)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True); p.add_argument('--ray-tool',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    write(args.output/'result.json',run(args.root,args.ray_tool,args.output))
