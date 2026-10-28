#!/usr/bin/env python3
"""Training-geometry-only ROI selection; existing matched-input X01 audit.
Licensed debug OBJ/cells and queries stay external. GPL-2.0-or-later.
"""
import argparse
import json
import math
from pathlib import Path
import sys
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'datasets'))
from solver_view import TrainingView
from census import measured,write


def choose(views,facts,number=4):
    manifest=json.loads((views/'inputs/manifest.json').read_text());cells={}
    for r in TrainingView(views/'inputs',manifest['map_sha256']).observations():
        key=tuple(math.floor(v/256) for v in r['position']);c=cells.setdefault(key,dict(count=0,surfaces=set(),normals=set(),up=0,uncertain=0))
        c['count']+=1;c['surfaces'].add(r['surface']);n=r['normal'];axis=max(range(3),key=lambda i:abs(n[i]));c['normals'].add((axis,n[axis]>=0))
        c['up']+=n[2]>.8;c['uncertain']+=r['material_uncertainty']!=0
    eligible=[(k,c) for k,c in cells.items() if c['count']>=12]
    if not eligible:return []
    hints=facts.get('fixture_geometry_priors',[])
    centre=lambda k:[(x+.5)*256 for x in k]
    specs=[('multi_normal',lambda k,c:(len(c['normals']),len(c['surfaces']),c['count'])),
        ('upward_sky_candidate',lambda k,c:(c['up'],c['count'])),
        ('uncertain_optics_candidate',lambda k,c:(c['uncertain'],len(c['surfaces']))),
        ('fixture_neighbourhood',lambda k,c:(-min((math.dist(centre(k),h['position']) for h in hints),default=1e30),c['count']))]
    selected=[]
    for label,score in specs[:number]:
        ordered=sorted(eligible,key=lambda x:(tuple(-a for a in score(*x)),x[0]))
        k,c=next((x for x in ordered if all(math.dist(centre(x[0]),s['centre'])>384 for s in selected)),ordered[0])
        selected.append(dict(name=label,centre=centre(k),radius=256,training_count=c['count'],surfaces=len(c['surfaces']),
            normals=len(c['normals']),selection='Predeclared geometry/training metadata; no validation/heldout RGB inspected',
            semantic_label_verified=False))
    return selected


def run(private,regression=False):
    private=Path(private);census=json.loads((private/'census.json').read_text());out=private/'stress';out.mkdir(exist_ok=True)
    if regression:
        census={};out=private/'regression';out.mkdir(exist_ok=True)
        a=json.loads((HERE/'availability.json').read_text())
        for name in ('hoth3','hoth2','mp/ffa3'):
            slug=name.replace('/','_');source=a['available'][name];bsp=out/(slug+'.bsp')
            with zipfile.ZipFile(source['archive']) as z:bsp.write_bytes(z.read(source['member']))
            census[name]=dict(status='Complete',prefix=str(ROOT/'build/x02/final'/slug),views=str(ROOT/'build/x02/final'/(slug+'-views')),
                dataset=str(ROOT/'build/x02/runtime/run/home/rend2/lightrecon/v1/observations'/(slug+'_a.jkobs')),bsp=str(bsp))
    audit=ROOT/'build/lightrecon-disk-standalone/lightrecon_regions_audit.exe';results={}
    for name,item in census.items():
        if item['status']!='Complete':results[name]=dict(status='Unavailable',reason=item.get('reason',item['status']));continue
        slug=name.replace('/','_');folder=out/slug;folder.mkdir(exist_ok=True);prefix=Path(item['prefix']);views=Path(item['views'])
        facts=json.loads(prefix.with_suffix('.facts.json').read_text());selection=choose(views,facts,1 if regression else 4);write(folder/'selection.json',selection)
        rois=[]
        for s in selection:
            dest=folder/s['name'];record=dest.with_suffix('.json');measurement=dest.with_suffix('.measurement.json')
            if record.exists() and measurement.exists():rois.append(dict(selection=s,report=json.loads(record.read_text()),measurement=json.loads(measurement.read_text())));continue
            argv=[audit,item['dataset'],item['bsp'],dest,*map(str,s['centre']),256,128,32,4,8,'raw']
            try:
                m=measured(argv,dest.with_suffix('.log'));write(measurement,m);rois.append(dict(selection=s,report=json.loads(record.read_text()),measurement=m))
            except Exception as e:rois.append(dict(selection=s,status='Failed',reason=str(e)))
        results[name]=dict(status='Measured',rois=rois,scope=f'{len(selection)} training-geometry-selected stress ROIs; not certified semantic rooms',native_mp_materials=False if name.startswith('mp/') else None)
        write(out/'result.json',results);print(name,'ROIs',len(rois),flush=True)
    write(out/'result.json',results);return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--regression',action='store_true');a=p.parse_args();run(a.private,a.regression)
