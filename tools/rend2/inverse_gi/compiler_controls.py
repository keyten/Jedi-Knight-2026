#!/usr/bin/env python3
"""Paired real q3map2 bounce controls, reusing D09/X03 fixtures and source locks.

No compiler transport replay or physical GI accuracy claim. Measured differences
are encoded baked observations, not measured lux. Answers remain evaluator-only.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'bake_model'))
from bake_model import COMPILER_SHA256, SOURCE_REVISION, point_scalar
from compare_bakes import bsp, floor_samples, CASES, metrics
from hypotheses import continuous_encode
from identifiability import interval_residual
from experiment import write_json


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def payload_sha(data): return hashlib.sha256(data).hexdigest()


def resolved_grid(lumps):
    return b''.join(lumps[15][i*30:i*30+30] for i in struct.unpack('<'+'H'*(len(lumps[17])//2),lumps[17]))


def geometry_vertices(raw):
    # RBSP drawVert_t's final 16 bytes are baked vertex RGBA, expected to change
    # with bounce. Compare position, UVs and normal; leave the BSP untouched.
    return b''.join(raw[i:i+64] for i in range(0,len(raw),80))


def paired(zero,one):
    # Same source texel/geometry, not independent prefix arrays. Normalized UV,
    # normals and positions must match before any subtraction is interpreted.
    a,b=bsp(zero),bsp(one)
    if geometry_vertices(a[10])!=geometry_vertices(b[10]) or any(a[i]!=b[i] for i in (11,13)):
        raise ValueError('Pair geometry/mapping changed')
    x,y=floor_samples(a),floor_samples(b)
    if [r['world'] for r in x]!=[r['world'] for r in y]: raise ValueError('Sample identity changed')
    delta=[[q-p for p,q in zip(r['rgb'],s['rgb'])] for r,s in zip(x,y)]
    def region(predicate):
        chosen=[d for r,d in zip(x,delta) if predicate(r['world'])]
        return dict(samples=len(chosen),mean_delta_rgb8=[sum(d[c] for d in chosen)/len(chosen) for c in range(3)] if chosen else None)
    return dict(samples=len(x),floor_mean_delta_rgb8=[sum(d[c] for d in delta)/len(delta) for c in range(3)],
        max_absolute_delta=max(abs(v) for d in delta for v in d),
        negative_channels=sum(v<0 for d in delta for v in d),
        positive_channels=sum(v>0 for d in delta for v in d),
        left=region(lambda p:p[0]<-32),right=region(lambda p:p[0]>32),
        grid_identical=resolved_grid(a)==resolved_grid(b),
        zero_grid_sha256=payload_sha(resolved_grid(a)),one_grid_sha256=payload_sha(resolved_grid(b)),
        limitation='Paired byte differences; not a linear radiance subtraction or exact bounce replay')


def frozen_checks(root,witness_path=None):
    witness_path=witness_path or HERE.parent/'bake_model/validation.json'
    witness=json.loads(witness_path.read_text())
    if witness.get('compiler_sha256')!=COMPILER_SHA256 or witness.get('source_revision')!=SOURCE_REVISION or not witness.get('passed'):
        raise ValueError('Invalid forward reference provenance')
    results={}
    for case in CASES:
        path=root/'authoring/base/maps'/f"{case['name']}.bsp"
        expected=witness['cases'][case['name']]['bsp_sha256']
        if sha(path)!=expected: raise ValueError('Frozen compiler fixture hash mismatch: '+case['name'])
        results[case['name']]=metrics(case,bsp(path))
        if results[case['name']].get('passed') is False: raise ValueError('Existing kernel comparison failed')
        results[case['name']]['input_bsp_sha256']=expected
    return dict(reference_witness_sha256=sha(witness_path),cases=results,
        white_bounce=paired(root/'authoring/base/maps/inverse_square.bsp',root/'authoring/base/maps/bounce1.bsp'),
        white_bouncegrid=paired(root/'authoring/base/maps/inverse_square.bsp',root/'authoring/base/maps/bouncegrid1.bsp'))


def compile_control(compiler,datasets,work,name,bounces,grid=False,white=180):
    root=work/(name+f'-b{bounces}-g{int(grid)}-w{white}')
    base=root/'authoring/base'; (base/'maps').mkdir(parents=True,exist_ok=True)
    source=datasets/'authoring/base'
    recipe=json.loads((datasets/'answers'/f'{name}.q3map-recipe.json').read_text())
    if sha(source/'maps'/f'{name}.map')!=recipe['map_sha256']: raise ValueError('Source map no longer matches D09 recipe')
    for relative,digest in recipe['material_inputs'].items():
        if sha(source/relative)!=digest: raise ValueError('Source material no longer matches recipe')
    for folder in ('shaders','textures'):
        shutil.copytree(source/folder,base/folder,dirs_exist_ok=True)
    path=base/'maps'/f'{name}.map'; path.write_bytes((source/'maps'/path.name).read_bytes())
    if white!=180:
        texture=base/'textures/recon_offline/white.tga'; raw=texture.read_bytes()
        if len(raw)!=18+16*16*3: raise ValueError('Unexpected controlled texture')
        texture.write_bytes(raw[:18]+bytes([white]*3)*256)
    common=[str(compiler),'-game','ja','-fs_basepath',str(base.parent.resolve()),'-fs_game','base','-threads','1']
    stages=[['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],
        ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0',
         '-super','1','-nocollapse','-bounce',str(bounces)]+(['-bouncegrid'] if grid else [])]
    commands=[]; elapsed=[]
    for i,stage in enumerate(stages):
        argv=common+stage+[str(path.resolve())]; start=time.perf_counter()
        p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        elapsed.append(time.perf_counter()-start); text=p.stdout.decode('utf-8',errors='replace')
        (root/f'stage-{i}.log').write_text(text,encoding='utf-8')
        if p.returncode or re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked|Unknown (?:argument|option)',text,re.I):
            raise RuntimeError(text[-3000:])
        commands.append(argv)
    out=path.with_suffix('.bsp'); lumps=bsp(out)
    metadata=dict(map_sha256=sha(path),bsp_sha256=sha(out),bounces=bounces,bouncegrid=grid,white_texture_rgb8=white,
        recipe_override='-super 1 -nocollapse applied to BOTH sides to preserve dark-chart sampling; original D09 geometry/materials/primary lights unchanged',
        geometry_sha256=dict(vertices_without_baked_rgba=payload_sha(geometry_vertices(lumps[10])),
                            indices=payload_sha(lumps[11]),surfaces=payload_sha(lumps[13])),
        lightmap_sha256=payload_sha(lumps[14]),grid_sha256=payload_sha(resolved_grid(lumps)),
        material_sha256={str(p.relative_to(base)).replace('\\','/'):sha(p) for p in sorted(base.rglob('*')) if p.suffix in ('.tga','.shader','.txt')})
    write_json(root/'execution.json',dict(metadata=metadata,argv=commands,stage_seconds=elapsed))
    return out,metadata,sum(elapsed)


def profile_residuals(root):
    """Diagnostic attribution on actual white-room bakes. Independent smooth
    fill is a nuisance competitor, NEVER physical bounce. Correct NR inverse
    square, wrong NR linear/no-angle; both fit the SAME scalar intensity range
    and optionally a shared RGB ambient in [0,30] compiler units.
    """
    rows=[]
    for target in ('inverse_square','bounce1'):
        data=floor_samples(bsp(root/'authoring/base/maps'/f'{target}.bsp'))
        train=[r for r in data if r['world'][1]<=-96]; held=[r for r in data if r['world'][1]>=96]
        for profile in ('NR-inverse-square-cosine','NR-linear-no-angle'):
            for fitted in (False,True):
                for fill in (False,True):
                    def prediction(r,I,z):
                        x,y,_=r['world']; dz=191.; distance=math.sqrt(x*x+y*y+dz*dz)
                        linear=profile=='NR-linear-no-angle'
                        value=point_scalar(I,distance,1. if linear else dz/distance,linear=linear)
                        return continuous_encode([value+z]*3)
                    def loss(data,I,z):
                        return sum(interval_residual(p,b)**2 for r in data for p,b in zip(prediction(r,I,z),r['rgb']))/(3*len(data))
                    # Two-stage fixed deterministic grid: full identical intensity
                    # budget then 1-unit refinement; linear reach depends on I.
                    coarse=range(0,601,10) if fitted else (300,)
                    zs=range(0,31,2) if fill else (0,)
                    best=min((loss(train,I,z),I,z) for I in coarse for z in zs)
                    intensities=range(max(0,best[1]-10),min(600,best[1]+10)+1) if fitted else (300,)
                    fills=range(max(0,best[2]-2),min(30,best[2]+2)+1) if fill else (0,)
                    best=min((loss(train,I,z),I,z) for I in intensities for z in fills)
                    rows.append(dict(truth=target,profile=profile,intensity_fitted=fitted,fill_fitted=fill,
                        intensity_legacy_units=best[1],nuisance_fill_relative=best[2],
                        train_interval_rmse_rgb8=math.sqrt(best[0]),
                        heldout_interval_rmse_rgb8=math.sqrt(loss(held,best[1],best[2])),
                        train_samples=len(train),heldout_samples=len(held),spatial_gap_world=192,
                        fill_interpretation='unresolved nuisance; NEVER measured GI'))
    return rows


def grid_repeat_difference(a,b):
    left,right=resolved_grid(bsp(a)),resolved_grid(bsp(b))
    if len(left)!=len(right): raise ValueError('Grid dimensions changed')
    return [i//30 for i in range(0,len(left),30) if left[i:i+30]!=right[i:i+30]]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--q3map2',required=True,type=Path)
    p.add_argument('--forward-root',default=Path('build/bake-model-comparison-final'),type=Path)
    p.add_argument('--forward-witness',type=Path,help='Explicit new compare_bakes result.json; default is the checked repository witness, never implicit rebaselining')
    p.add_argument('--dataset-root',default=Path('build/q3map2-bake-verified'),type=Path)
    p.add_argument('--work',default=Path('build/inverse-gi-controls'),type=Path)
    p.add_argument('--output',default=HERE/'results/compiler.json',type=Path)
    args=p.parse_args(); compiler=args.q3map2.resolve()
    if sha(compiler)!=COMPILER_SHA256: raise ValueError('Pinned compiler hash mismatch')
    start=time.perf_counter(); frozen=frozen_checks(args.forward_root,args.forward_witness); frozen_seconds=time.perf_counter()-start
    start=time.perf_counter(); discrepancy=profile_residuals(args.forward_root); search_seconds=time.perf_counter()-start
    result=dict(schema='org.openjk.inverse-gi-compiler-controls',version=1,compiler_sha256=COMPILER_SHA256,
        source_revision=SOURCE_REVISION,units='relative encoded RGB8; historical stock profile UNKNOWN',
        frozen=frozen,controls={},profile_discrepancy=discrepancy)
    original=args.dataset_root/'authoring/base/maps'
    for name in ('bounce_control','bounce_colour'):
        expected=json.loads((args.dataset_root/'answers'/f'{name}.q3map-recipe.json').read_text())['compiled_bsp_sha256']
        if sha(original/f'{name}.bsp')!=expected: raise ValueError('Original D09 BSP changed')
    result['frozen_D09_colour_pair']=paired(original/'bounce_control.bsp',original/'bounce_colour.bsp')
    execution={}; total=0.
    for name,white in (('point_room',180),('bounce_colour',180),('door_occlusion',180),
                       ('wall_leak',180),('skylight',180),('point_room',90)):
        paths=[]; key=name+f'-white{white}'
        for bounce in (0,1):
            path,meta,seconds=compile_control(compiler,args.dataset_root,args.work,name,bounce,white=white)
            paths.append(path); execution[f'{key}-b{bounce}']=meta; total+=seconds
            print(key,'bounce',bounce,'compiled',flush=True)
        result['controls'][key]=paired(*paths)
    path,meta,seconds=compile_control(compiler,args.dataset_root,args.work,'bounce_colour',1,grid=True)
    total+=seconds; execution['bounce_colour-grid']=meta
    zero=args.work/'bounce_colour-b0-g0-w180/authoring/base/maps/bounce_colour.bsp'
    result['controls']['bounce_colour-bouncegrid']=paired(zero,path)
    # A same-settings repeat isolates compiler grid nondeterminism from bounce.
    repeat,meta,seconds=compile_control(compiler,args.dataset_root,args.work/'repeat','door_occlusion',0)
    total+=seconds; execution['door-zero-repeat']=meta
    first=args.work/'door_occlusion-b0-g0-w180/authoring/base/maps/door_occlusion.bsp'
    one=args.work/'door_occlusion-b1-g0-w180/authoring/base/maps/door_occlusion.bsp'
    result['door_grid_repeat']=dict(zero_vs_zero_different_cells=grid_repeat_difference(first,repeat),
        zero_vs_one_different_cells=grid_repeat_difference(first,one),
        repeated_lightmap_identical=bsp(first)[14]==bsp(repeat)[14],
        warning='An unrequested grid difference is NOT evidence that bouncegrid was enabled')
    low=args.work/'point_room-b0-g0-w90/authoring/base/maps/point_room.bsp'
    high=args.work/'point_room-b0-g0-w180/authoring/base/maps/point_room.bsp'
    result['material_control']=dict(direct_lightmap_identical=bsp(low)[14]==bsp(high)[14],
        note='Texture response changes next bounce, not the incident multiplier stored at this receiver')
    family={}
    for truth in ('inverse_square','bounce1'):
        alternatives=[r for r in discrepancy if r['truth']==truth and r['intensity_fitted'] and r['fill_fitted']]
        family[truth]=dict(allowed_profiles=[r['profile'] for r in alternatives],
            nuisance_fill_range=[min(r['nuisance_fill_relative'] for r in alternatives),max(r['nuisance_fill_relative'] for r in alternatives)],
            note='Conditional family, not a posterior or identified historical compiler; fill is unresolved, not physical GI')
    result['UnknownProfile']=family
    result['inputs_and_outputs']=execution
    result['source_sha256']=sha(Path(__file__))
    write_json(args.output,result)
    write_json(args.work/'timing.json',dict(total_compiler_stage_seconds=total,new_bakes=len(execution),stage_invocations=3*len(execution),
        frozen_check_seconds=frozen_seconds,profile_search_seconds=search_seconds))
    print('PASS: existing 22 fixtures checked;',len(execution),'new controlled bakes;',total,'compiler seconds')


if __name__=='__main__': main()
