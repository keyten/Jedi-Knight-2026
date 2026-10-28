#!/usr/bin/env python3
"""Bounded search for informative stock direct-light hypotheses, training only.
Outer held-out and validation files are never opened. GPL-2.0-or-later.
"""
import argparse
from collections import Counter
from dataclasses import asdict
import itertools
import json
import math
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'datasets'));sys.path.insert(0,str(HERE.parent/'bake_model'))
from solver_view import TrainingView
from prepare_blind import assignment,rows as raw_rows
from hypotheses import catalogue
from identifiability import Observation,Configuration,compare,retention
from census import measured,write


def thin(values,cap):
    cells={}
    for row in values:
        n=row['normal'];axis=max(range(3),key=lambda i:abs(n[i]))
        key=(*(math.floor(v/64) for v in row['position']),axis,n[axis]>=0)
        if key not in cells or row['id']<cells[key]['id']:cells[key]=row
    # Round robin normal classes: do not let a floor-only lexical prefix dominate.
    bins={}
    for r in cells.values():
        n=r['normal'];a=max(range(3),key=lambda i:abs(n[i]));bins.setdefault((a,n[a]>=0),[]).append(r)
    bins=[sorted(v,key=lambda r:r['id']) for _,v in sorted(bins.items())]
    return [r for group in itertools.zip_longest(*bins) for r in group if r is not None][:cap]


def attempt(name,prefix,views,folder,policy):
    folder.mkdir(parents=True,exist_ok=False);meta=json.loads(prefix.with_suffix('.source.json').read_text())
    view=TrainingView(views/'inputs',meta['map_sha256']);train=list(view.observations())
    tool=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_profile_rays.exe';hintfile=folder/'hints.tsv'
    measured([tool,view.geometry_path,'--hints',hintfile],folder/'hints.log')
    hints=[]
    for line in hintfile.read_text().splitlines():
        f=line.split();p=list(map(float,f[1:4]));n=list(map(float,f[4:7]));hints.append(dict(id=f[0],position=p,normal=n))
    if len(hints)<2:return dict(status='InsufficientLegitimateGlowPriors',hints=len(hints),outer_train=len(train),historical_profile='UnknownProfile')
    # Only requested outer-training IDs' GEOMETRY metadata is projected by the
    # controller. No full archive/image, heldout RGB or source entity enters fit.
    wanted={r['id'] for r in train};geometry={}
    for r in raw_rows(prefix.with_suffix('.rows.jsonl')):
        if r['id'] in wanted:geometry[r['id']]={k:r[k] for k in ('footprint_radius','kind','shading_normal')}
    nested={p:[] for p in ('train','validation','heldout')};split=policy['nested_split']
    for r in train:
        g=geometry[r['id']]
        if g['kind']!=0 or g['shading_normal'] is None or sum(a*b for a,b in zip(r['normal'],g['shading_normal']))<.999:continue
        role=assignment(dict(position=r['position'],footprint_radius=g['footprint_radius']),split['block_world'],split['gap_world'],split['seed'],split['origin_world'])
        if role:nested[role].append(r)
    # Score potential neighbourhoods using only genuine TRAIN colour + geometry.
    # Grid of hinted source neighbourhoods avoids a quadratic source/luxel scan.
    groups={}
    for h in hints:groups.setdefault(tuple(math.floor(v/768) for v in h['position']),[]).append(h)
    ranked=[]
    for key in groups:
        centre=[(x+.5)*768 for x in key]
        rs=[r for r in nested['train'] if math.dist(r['position'],centre)<=768]
        pos=[r for r in rs if max(r['raw'])>=policy['direct_admission']['training_positive_minimum_code']]
        normal_bins={(max(range(3),key=lambda i:abs(r['normal'][i])),tuple(round(x,1) for x in r['normal'])) for r in pos}
        ranked.append((len(normal_bins),len(pos),key,centre))
    chosen=[]
    for _,_,key,centre in sorted(ranked,key=lambda x:(-x[0],-x[1],x[2])):
        if all(math.dist(centre,c)>512 for c in chosen):chosen.append(centre)
        if len(chosen)==policy['budgets']['source_search_roi_per_map']:break
    trials=[];queries=0
    for index,centre in enumerate(chosen):
        near=sorted(hints,key=lambda h:(math.dist(h['position'],centre),int(h['id'])));selected=[]
        for h in near:
            if math.dist(h['position'],centre)>1024:break
            if not selected or all(math.dist(h['position'],s['position'])>64 for s in selected):selected.append(h)
            if len(selected)==4:break
        candidates=[]
        for h in selected:
            for offset in policy['direct_admission']['centroid_offsets_world']:
                candidates.append(dict(h,offset=offset,position=[v+offset*n for v,n in zip(h['position'],h['normal'])]))
        folds={p:thin([r for r in rs if math.dist(r['position'],centre)<=768],policy['budgets']['samples_per_nested_role']) for p,rs in nested.items()}
        qpath=folder/f'roi{index}-queries.tsv';rpath=folder/f'roi{index}-rays.tsv'
        with qpath.open('w') as stream:
            stream.write('map '+meta['map_sha256']+'\n')
            for role,values in folds.items():
                for r in values:
                    for j,h in enumerate(candidates):stream.write(' '.join(map(str,[r['id'],j,*r['position'],*r['normal'],r['surface'],h['id'],*h['position']]))+'\n')
        count=sum(len(v) for v in folds.values())*len(candidates)
        if queries+count>policy['budgets']['max_ray_queries_per_map']:trials.append(dict(status='RayBudgetExhausted',centre=centre));break
        queries+=count;measurement=measured([tool,view.geometry_path,qpath,rpath],folder/f'roi{index}-rays.log')
        states={(f[0],int(f[1])):f[2] for line in rpath.read_text().splitlines() if (f:=line.split())}
        admitted=[];pair_counts=[]
        for a,b in itertools.combinations(range(len(candidates)),2):
            if candidates[a]['id']==candidates[b]['id']:continue
            visible={p:[r for r in values if all(states[r['id'],j]=='visible' for j in (a,b))] for p,values in folds.items()}
            counts={p:len(rs) for p,rs in visible.items()}
            blocks={p:len({tuple(math.floor((v-o)/split['block_world']) for v,o in zip(r['position'],split['origin_world'])) for r in rs}) for p,rs in visible.items()}
            positive=sum(max(r['raw'])>=8 for r in visible['train']);normals=len({tuple(round(v,1) for v in r['normal']) for r in visible['train']})
            distances=[math.dist(r['position'],candidates[j]['position']) for r in visible['train'] for j in (a,b)];ratio=max(distances)/min(distances) if distances and min(distances)>0 else 0
            if min(counts.values(),default=0)>=8 and min(blocks.values(),default=0)>=2 and positive>=12 and normals>=2 and ratio>=1.25:admitted.append((sum(counts.values()),a,b))
            pair_counts.append(dict(a=a,b=b,visible=counts,blocks=blocks,training_positive=positive,training_normals=normals,distance_ratio=ratio))
        trial=dict(centre=centre,candidates=candidates,fold_counts={p:len(rs) for p,rs in folds.items()},ray_states=dict(Counter(states.values())),
            pair_support=pair_counts,query_measurement=measurement,admitted_pairs=len(admitted),status='Admitted' if admitted else 'InsufficientCertifiedPositiveSupport')
        if admitted:
            _,a,b=max(admitted);pair=(a,b);common={p:[r for r in rs if all(states[r['id'],j]!='unknown' for j in pair)] for p,rs in folds.items()}
            observations={p:[Observation(r['id'],tuple(r['position']),tuple(r['normal']),tuple(r['raw']),','.join(str(math.floor((v-o)/split['block_world'])) for v,o in zip(r['position'],split['origin_world'])),p,geometry[r['id']]['footprint_radius']) for r in rs] for p,rs in common.items()}
            configs=[Configuration('glow_point_'+str(j),(tuple(candidates[j]['position']),),((0.,0.,-1.),)) for j in pair]
            registry=catalogue();profiles=[registry[k] for k in ('NetRadiant-d9338309-JA/isq_cos','NetRadiant-d9338309-JA/linear_noangle','Q3MapOriginal/direct-isq')]
            # Same two fixed position hypotheses, RGB nuisance and no GI/fill.
            fits=[]
            for config,j in zip(configs,pair):
                masks={p:[[states[r.id,j]=='visible'] for r in rs] for p,rs in observations.items()}
                fit=compare(profiles,observations['train'],observations['validation'],observations['heldout'],[config],
                    amplitude_max=10000.,passes=1,fill_max=0.,masks=masks)
                fits.append(dict(configuration=asdict(config),comparison=fit,retention=retention(fit)))
            trial.update(fits=fits,historical_profile='UnknownProfile',claim='Conditional point/glow hypothesis, known rays and assumed output family; no stock source/compiler ground truth')
        trials.append(trial);write(folder/'trials.json',trials)
        if admitted:break # first admitted in PREDECLARED order, never best heldout fit
    return dict(status='ConditionalComparison' if any(t.get('admitted_pairs') for t in trials) else 'NoAdmittedRegionWithinBudget',
        map_sha256=meta['map_sha256'],outer_train=len(train),glow_hints=len(hints),nested_counts={p:len(rs) for p,rs in nested.items()},
        attempted_rois=len(trials),ray_queries=queries,trials=trials,outer_evaluation_targets_opened=False,
        historical_profile='UnknownProfile',source_xyz_error=None,limitation='Unknown bake transfer/materials; glow is not guaranteed bake emitter; shadow/GI discrepancy remains unresolved')


def run(private,output=None):
    private=Path(private);c=json.loads((private/'census.json').read_text());policy=json.loads((HERE/'policy.json').read_text());out=Path(output) if output else private/'stock-trials'
    if out.resolve().is_relative_to(ROOT):raise ValueError('Licensed query outputs must remain external')
    out.mkdir(exist_ok=True);result={}
    for name,item in c.items():
        if item['status']!='Complete':result[name]=dict(status='Unavailable',reason=item.get('reason',item['status']));continue
        folder=out/name.replace('/','_');record=folder/'result.json'
        if record.exists():result[name]=json.loads(record.read_text());continue
        result[name]=attempt(name,Path(item['prefix']),Path(item['views']),folder,policy);write(record,result[name]);write(out/'result.json',result)
        print(name,result[name]['status'],'rois=',result[name].get('attempted_rois'),flush=True)
    write(out/'result.json',result);return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--output',type=Path);a=p.parse_args();run(a.private,a.output)
