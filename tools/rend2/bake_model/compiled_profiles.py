"""A/B/C comparison using existing unmodified, hash-checked compiled BSP fixtures.
Ground-truth source configuration is controller-only. No new compiler invocation.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
from compare_bakes import CASES, bsp, metrics as verify_case, floor_samples
from hypotheses import catalogue
from identifiability import Observation, Configuration, compare, write, retention


def encoded_floor(lumps,name):
    output=[]
    for r in floor_samples(lumps):
        x,y,_=r['world']
        # True floor footprint is 16 WU. A 24-WU boundary gap excludes neighbouring
        # samples/filter support across blocks; all samples in a block share fold.
        X,Y=x+17,y+31
        if min(X%128,128-X%128,Y%128,128-Y%128)<24: continue
        block=(math.floor(X/128),math.floor(Y/128))
        key=json.dumps([20261032,block],separators=(',',':')).encode()
        n=int.from_bytes(hashlib.sha256(key).digest()[:8],'little')%5
        role=('train','train','train','validation','heldout')[n]
        output.append(Observation(name+'/'+str(r['world']), (x,y,1.),(0.,0.,1.),tuple(r['rgb']),str(block),role,math.sqrt(128)))
    # Keep only one receiver per 64-WU cell; no dense-patch pseudo-replication.
    unique={}
    for r in sorted(output,key=lambda v:v.position): unique.setdefault(tuple(math.floor(v/64) for v in r.position[:2]),r)
    return list(unique.values())


def run(root,output):
    root=Path(root); original=json.loads((root/'result.json').read_text())
    frozen=json.loads((Path(__file__).parent/'validation.json').read_text())
    registry=catalogue(); cases={c['name']:c for c in CASES}; verification={}; models={}
    for name,case in cases.items():
        file=root/'authoring/base/maps'/(name+'.bsp')
        digest=hashlib.sha256(file.read_bytes()).hexdigest()
        if digest!=original['cases'][name]['bsp_sha256'] or digest!=frozen['cases'][name]['bsp_sha256']:
            raise ValueError('Frozen compiler fixture hash mismatch: '+name)
        raw=bsp(file); checked=verify_case(case,raw)
        if not case.get('bounces'):
            if not checked['passed']: raise ValueError('Controlled kernel replay failed: '+name)
        else:
            # The old experiment establishes paired behavior, not a replay of GI.
            checked['paired_verification']=original['cases'][name]['passed']
            if not checked['paired_verification']: raise ValueError('Missing paired bounce witness')
        verification[name]=checked
        if name not in ('inverse_square','linear','linear_angle','no_angle','half_lambert','spot','colour_gamma','colour_clip'):
            continue
        data=encoded_floor(raw,name)
        parts=[[r for r in data if r.partition==p] for p in ('train','validation','heldout')]
        conf=Configuration('known_origin',((0.,0.,192.),),((0.,0.,-1.),))
        q=[case.get('intensity',300)*x for x in case.get('color',[1.,1.,1.])]
        candidates=[registry['NetRadiant-d9338309-JA/'+key] for key in
            ('isq_cos','linear_noangle','linear_cos','isq_noangle','isq_half_lambert','spot')]
        encoding=[dict(gamma=case.get('gamma',1.),compensate=1.,exposure=0.,srgb=False)]
        fixed=compare(candidates,*parts,[conf],encodings=encoding,fixed_amplitudes=q)
        fitted=compare(candidates,*parts,[conf],encodings=encoding)
        positions=[Configuration('z_'+str(z),((0.,0.,z),),conf.axes) for z in (128.,160.,192.,224.,256.)]
        variable=compare(candidates,*parts,positions,encodings=encoding,passes=1)
        models[name]=dict(partitions={p:len(d) for p,d in zip(('train','validation','heldout'),parts)},
            fixed=fixed,fitted_rgb=fitted,variable_height=variable,retained=retention(variable),
            source_answers_used='fixed protocol only; fitted protocols get geometry candidates and training RGB',
            physical_calibration='Unknown; compiler-relative authored values')
        print('compiled',name,'compared',flush=True)
    # Re-evaluate the paired control instead of pretending to reproduce transport.
    control=verification['inverse_square']
    for name in ('bounce1','bouncegrid1'):
        checked=verification[name]
        if checked['floor_mean']<=sum(r['rgb'][0] for r in floor_samples(bsp(root/'authoring/base/maps/inverse_square.bsp')))/len(floor_samples(bsp(root/'authoring/base/maps/inverse_square.bsp'))):
            raise ValueError('Bounce did not increase floor signal')
    result=dict(schema='org.openjk.compiled-profile-identifiability.v1',compiler_sha256=original['compiler_sha256'],
                verification=verification,comparisons=models,new_compiler_invocation=False,
                full_bounce_replay=False,stock_compiler_identified=False)
    write(output/'result.json',result); return result
