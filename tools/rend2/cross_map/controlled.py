#!/usr/bin/env python3
"""Independent X06 bakes using existing authoring, kernels, UV mapping and metrics.
Controller-only sources/recipes; no inverse solver. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
import importlib.util
import math
from pathlib import Path
import re
import subprocess
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
for folder in ('evaluation/x05','evaluation','bake_model','inverse_gi'):
    sys.path.append(str(HERE.parent/folder))
from compiler_controls import paired
spec=importlib.util.spec_from_file_location('x05_fixture_adapter',HERE.parent/'evaluation/x05/fixtures.py')
x05=importlib.util.module_from_spec(spec);spec.loader.exec_module(x05)
floor_rows,visible,sky_directions=x05.floor_rows,x05.visible,x05.sky_directions
from compare_bakes import author,make_assets,entity,bsp
from bake_model import COMPILER_SHA256,point_scalar,spot_factor
from hypotheses import continuous_encode
from identifiability import line_minimum
from metrics import prediction_metrics


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def recipe(name):
    sources=[dict(origin=[0.,0.,192.],rgb=[1.,1.,1.],intensity=300.)];case={};gamma=1.;bounce=0;white=180
    if name.startswith('multi'):
        sources=[dict(origin=[-128.,-96.,192.],rgb=[1.,.25,.1],intensity=300.),dict(origin=[160.,96.,256.],rgb=[.15,.4,1.],intensity=250.)]
    if name=='spot_oblique':sources=[dict(origin=[96.,-64.,224.],rgb=[1.,.7,.3],intensity=400.,target=[32.,32.,64.],radius=96.)]
    if name=='gamma_clipped':sources=[dict(origin=[32.,-48.,224.],rgb=[.1,.7,1.],intensity=2400.)];gamma=2.2
    if name=='saturated_white':sources=[dict(origin=[0.,0.,192.],rgb=[1.,1.,1.],intensity=100000.)]
    if name in ('sky_offset','sky_roof_v2'):case=dict(sun=True,skylight=True);sources=[]
    if name=='thin_door':case=dict();sources=[dict(origin=[-160.,0.,192.],rgb=[1.,1.,1.],intensity=1200.)]
    if name=='split_coincident':sources=[dict(origin=[0.,0.,192.],rgb=[1.,1.,1.],intensity=150.) for _ in range(2)]
    if name=='multi_bounce':bounce=1
    if name=='multi_red_bounce':bounce=1;white='red'
    return dict(name=name,sources=sources,case=case,gamma=gamma,bounce=bounce,white=white)


def compile_one(compiler,work,r):
    folder=work/r['name'];base=folder/'authoring/base';make_assets(base)
    # Reuse the existing sealed-room or sky recipe; only declared source and
    # geometry changes below define new, independently compiled witnesses.
    case=dict(r['case'],name=r['name']);path=author(base,case);text=path.read_text()
    text=re.sub(r'\{\n(?=[^{}]*"classname" "light")[^{}]*\}\n','',text)
    for i,s in enumerate(r['sources']):
        fields=dict(classname='light',origin=' '.join(map(str,s['origin'])),light=str(s['intensity']),_color=' '.join(map(str,s['rgb'])))
        if 'target' in s:
            fields.update(target='axis'+str(i),radius=str(s['radius']),spawnflags='1')
            text+=entity(dict(classname='info_null',targetname='axis'+str(i),origin=' '.join(map(str,s['target']))))
        text+=entity(fields)
    if r['name']=='thin_door':
        from compare_bakes import brush
        insert=''.join([brush((-.5,-384,0),(.5,-80,400)),brush((-.5,80,0),(.5,384,400)),brush((-.5,-80,256),(.5,80,400))])
        text=text.replace('"gridsize" "64 64 64"','"gridsize" "64 64 64"\n'+insert)
        text+=entity(dict(classname='info_player_deathmatch',origin='128 64 64'))
    if r['name']=='sky_roof_v2':
        # A new alias must resolve to an actual authored sky declaration. The
        # first sky_offset attempt did not and remains an invalid dark control.
        text=text.replace('bake_forward/sky_sky_roof_v2','bake_forward/sky_skylight')
    path.write_text(text,encoding='ascii')
    if r['white']=='red':
        texture=base/'textures/bake_forward/white.tga';data=texture.read_bytes();texture.write_bytes(data[:18]+bytes([40,60,180])*256)
    argv=[]
    for i,stage in enumerate((['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],
        ['-light','-deluxe','-deluxemode','0','-gamma',str(r['gamma']),'-compensate','1','-exposure','0','-bounce',str(r['bounce']),'-super','1','-nocollapse'])):
        command=[str(compiler.resolve()),'-game','ja','-fs_basepath',str(base.parent.resolve()),'-fs_game','base','-threads','1',*stage,str(path.resolve())]
        p=subprocess.run(command,capture_output=True,text=True,errors='replace',timeout=120)
        (folder/f'stage{i}.log').write_text(p.stdout+p.stderr,encoding='utf-8')
        if p.returncode or re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked|Unknown (?:argument|option)',p.stdout+p.stderr,re.I):raise RuntimeError('Compiler failed '+r['name'])
        argv.append(command)
    result=dict(r,argv=argv,map_sha256=sha(path),bsp_sha256=sha(path.with_suffix('.bsp')),compiler_sha256=sha(compiler),
        shader_sha256=sha(base/'shaders/bake_forward.shader'),texture_sha256=sha(base/'textures/bake_forward/white.tga'))
    answers=work/'answers';answers.mkdir(exist_ok=True);write(answers/(r['name']+'.json'),result)
    return path.with_suffix('.bsp'),result


def forward(rows,triangles,r,law='isq',gain=1.,gamma=None,all_visible=False):
    output=[]
    for row in rows:
        p=[v+n for v,n in zip(row['position'],row['normal'])];rgb=[0.,0.,0.]
        if r['case'].get('skylight'):
            for d in sky_directions():
                if all_visible or visible(triangles,p,direction=d):rgb=[x+120./17*max(0.,sum(a*b for a,b in zip(row['normal'],d))) for x in rgb]
        for s in r['sources']:
            if not all_visible and not visible(triangles,p,s['origin']):continue
            delta=[a-b for a,b in zip(s['origin'],p)];distance=math.sqrt(sum(x*x for x in delta));cos=sum(a*b for a,b in zip(row['normal'],delta))/distance
            value=point_scalar(s['intensity']*gain,distance,1. if law=='linear' else max(0.,cos),linear=law=='linear')
            if 'target' in s:
                axis=[a-b for a,b in zip(s['target'],s['origin'])];length=math.sqrt(sum(x*x for x in axis));axis=[x/length for x in axis]
                value*=spot_factor(p,s['origin'],axis,length,s['radius'])
                if value<=1:value=0.
            for c in range(3):rgb[c]+=value*s['rgb'][c]
        output.append([min(255.,x) for x in continuous_encode(rgb,gamma=r['gamma'] if gamma is None else gamma)])
    return output


def score(rows,values,profile):
    targets=[dict(id=r['id'],weight=r['area'],domain='EncodedRGB8',primaries='EncodedSourceRGB',comparison_id=profile,lower=r['raw'],upper=[min(255,x+1) for x in r['raw']]) for r in rows]
    predictions=[dict(id=r['id'],rgb=v,domain='EncodedRGB8',primaries='EncodedSourceRGB',comparison_id=profile) for r,v in zip(rows,values)]
    return prediction_metrics(targets,predictions)


def run(compiler,work,names=None):
    if sha(compiler)!=COMPILER_SHA256:raise ValueError('Pinned compiler required')
    work.mkdir(parents=True,exist_ok=False);policy=json.loads((HERE.parent/'evaluation/x05/policy.json').read_text());results={};paths={}
    names=names or ['multi_direct','multi_bounce','multi_red_bounce','spot_oblique','gamma_clipped','saturated_white','thin_door','sky_roof_v2','single_coincident','split_coincident']
    for name in names:
        path,r=compile_one(compiler,work,recipe(name));paths[name]=path
        rows,(_,_,triangles)=floor_rows(path,policy);folds={role:[r for r in rows if r['partition']==role] for role in ('train','validation','heldout')}
        profile='X06-controlled-output-gamma'+str(r['gamma']);held=folds['heldout']
        alternatives={}
        for law in ('isq','linear'):
            # Same one-scalar budget; train only. Cached geometry is tiny here.
            objective=lambda gain:score(folds['train'],forward(folds['train'],triangles,r,law,gain),profile)['rgb_interval_rmse']['value']**2
            gain=line_minimum(objective,10.,1.,steps=12)
            alternatives[law]=dict(gain=gain,parameter_budget=1,validation=score(folds['validation'],forward(folds['validation'],triangles,r,law,gain),profile)['rgb_interval_rmse'],
                heldout=score(held,forward(held,triangles,r,law,gain),profile)['rgb_interval_rmse'])
        correct=score(held,forward(held,triangles,r),profile);black=score(held,[[0.]*3]*len(held),profile)
        best=min(a['validation']['value'] for a in alternatives.values())
        viable=[key for key,a in alternatives.items() if a['validation']['value']<=best+1.]
        results[name]=dict(bsp_sha256=r['bsp_sha256'],recipe_sha256=sha(work/'answers'/(name+'.json')),counts={k:len(v) for k,v in folds.items()},
            reference=correct,black=black,all_visible=score(held,forward(held,triangles,r,all_visible=True),profile),
            wrong_gamma=score(held,forward(held,triangles,r,gamma=1. if r['gamma']!=1. else 2.2),profile),fitted_profiles=alternatives,
            gamma=r['gamma'],bounce=r['bounce'],physical_units='Uncalibrated compiler-relative; no SI accuracy',
            source_semantics='MissingSourceShader/invalid positive sky fixture' if name=='sky_offset' else 'Declared controlled source inputs',
            validation_viable=viable,profile_outcome='NotAnAttenuationTest' if r['case'].get('skylight') else 'Ambiguous' if len(viable)>1 else 'ConditionalSingleton',
            saturated_channels=sum(v==255 for row in held for v in row['raw']),channel_denominator=3*len(held),
            interpretation='Known-source forward controls, never stock compiler identification; bounce target includes an unmodelled component')
        write(work/'result.json',results);print(name,'correct=',correct['rgb_interval_rmse']['value'],'black=',black['rgb_interval_rmse']['value'],flush=True)
    pairs={}
    if {'multi_direct','multi_bounce','multi_red_bounce'}<=set(paths):
        pairs.update(bounce=paired(paths['multi_direct'],paths['multi_bounce']),material_bounce=paired(paths['multi_bounce'],paths['multi_red_bounce']))
    if {'single_coincident','split_coincident'}<=set(paths):pairs['coincident_lightmap_equal']=bsp(paths['single_coincident'])[14]==bsp(paths['split_coincident'])[14]
    write(work/'pairs.json',pairs);return results,pairs


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--names',nargs='+');a=p.parse_args();run(a.compiler,a.output,a.names)
