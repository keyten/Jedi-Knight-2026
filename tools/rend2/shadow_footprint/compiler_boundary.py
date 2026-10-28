#!/usr/bin/env python3
"""Reproduce the unchanged 35-texel witness and inspect source-derived trace rules.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'bake_model'))
from compare_bakes import bsp, metrics, floor_samples, predict, CASES
from bake_model import encode_color
from geometry import Triangle, footprint, ray_triangle, unit, sub
from compiler_trace import Replay


def load(path, strict_receiver_uv=True):
    lumps=bsp(path); vertices=[]; shaders=[]; surfaces=[]; casters=[]
    for off in range(0,len(lumps[1]),72):
        shaders.append(dict(name=lumps[1][off:off+64].split(b'\0')[0].decode(),flags=struct.unpack_from('<I',lumps[1],off+64)[0]))
    for off in range(0,len(lumps[10]),80):
        vertices.append((struct.unpack_from('<3f',lumps[10],off),struct.unpack_from('<2f',lumps[10],off+20),struct.unpack_from('<3f',lumps[10],off+52)))
    indices=struct.unpack('<'+'i'*(len(lumps[11])//4),lumps[11])
    for off in range(0,len(lumps[13]),148):
        shader,_,kind,fv,nv,fi,ni=struct.unpack_from('<7i',lumps[13],off)
        page=struct.unpack_from('<i',lumps[13],off+36)[0]; ordinal=off//148
        if kind not in (1,3): continue
        surf=dict(ordinal=ordinal,page=page,shader=shaders[shader],triangles=[])
        for index in range(fi,fi+ni,3):
            v=[vertices[fv+indices[index+j]] for j in range(3)]
            # JA/Raven SURF_SKY is 0x2000, NOT the Quake III 0x4 flag.
            optical='sky' if shaders[shader]['flags']&0x2000 else 'opaque'
            t=Triangle(f'{ordinal}/{(index-fi)//3}',ordinal,tuple(p[0] for p in v),
                       ((0.,0.),(1.,0.),(0.,1.)),tuple(p[2] for p in v),optical)
            casters.append(t)
            if page>=0:
                try:
                    surf['triangles'].append(Triangle(t.id,t.surface,t.world,tuple(p[1] for p in v),t.normals,optical))
                except ValueError:
                    if strict_receiver_uv: raise
                    # A degenerate receiving chart is not missing caster geometry.
                    # The independently validated world triangle above is retained.
                    surf['unsupported_receiver_uv_triangles']=surf.get('unsupported_receiver_uv_triangles',0)+1
        surfaces.append(surf)
    return lumps,surfaces,tuple(casters)


def classify(casters,origin,direction,epsilon=0.):
    hits=[]
    for t in casters:
        hit=ray_triangle(origin,direction,t,epsilon,minimum=.5,det_epsilon=.25 if epsilon else 1e-12)
        if hit: hits.append(dict(triangle=t.id,optical=t.optical,distance=hit[0],barycentric=[1-hit[1]-hit[2],hit[1],hit[2]],
                                world=[origin[i]+hit[0]*direction[i] for i in range(3)]))
    hits.sort(key=lambda h:(h['distance'],h['triangle']))
    physical=bool(hits) and hits[0]['optical']=='sky'
    # Restricted opaque/sky testAll replay: sky does not terminate traversal.
    replay=any(h['optical']=='sky' for h in hits) and not any(h['optical']=='opaque' for h in hits)
    return physical,replay,hits


def run(path,output):
    frozen=json.loads((Path(__file__).resolve().parents[1]/'bake_model/validation.json').read_text())
    catalogue=Path(__file__).resolve().parents[1]/'bake_model'
    lock=json.loads((catalogue/'source-lock.json').read_text()); source_checks=[]
    for version in lock['versions']:
        for entry in version['files']:
            source=Path(entry['inspection_path'])
            if not source.exists(): source_checks.append(dict(path=str(source),status='Unavailable')); continue
            actual=hashlib.sha256(source.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
            if actual!=entry['lf_sha256']: raise ValueError('Source lock mismatch '+str(source))
            source_checks.append(dict(path=str(source),sha256=actual,status='MatchesLock'))
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if digest!=frozen['cases']['multi_sun']['bsp_sha256']: raise ValueError('Original frozen BSP hash required')
    lumps,surfaces,casters=load(path); case=next(c for c in CASES if c['name']=='multi_sun')
    tree=Replay(lumps,casters)
    unchanged=metrics(case,lumps)
    if (unchanged['unchecked_sky_wall_boundary_channels'],unchanged['unchecked_sky_wall_boundary_max_error'])!=(105,28):
        raise ValueError('Original discrepancy not reproduced; no tolerance change authorized')
    floor=next(s for s in surfaces if s['page']>=0 and s['triangles'] and all(abs(p[2])<1e-5 for t in s['triangles'] for p in t.world))
    records=[]; all_errors=[]; variants={str(e):[] for e in (0.,.001,.005,.01)}
    for row in floor_samples(lumps):
        p=tuple(row['world']); origin=(p[0],p[1],1.); old=encode_color(predict(case,origin))
        direction=unit((1.,0.,1.)); visible,hits=tree.trace(origin,direction,.01)
        replay=encode_color([120+40/math.sqrt(2)*visible,120.,120.])
        all_errors.extend(abs(a-b) for a,b in zip(row['rgb'],replay))
        if abs(origin[0]+399-384)>=2: continue
        t=next(t for t in floor['triangles'] if min(t.bary(t.to_uv(p)))>=-1e-7)
        uv=t.to_uv(p); x=int(round(uv[0]*128-.5)); y=int(round(uv[1]*128-.5))
        fp=footprint(f'{digest}/{floor["page"]}/{x}/{y}',floor['triangles'],((x/128,y/128),((x+1)/128,(y+1)/128)))
        for epsilon in (0.,.001,.005,.01):
            v,_=tree.trace(origin,direction,epsilon)
            expected=encode_color([120+40/math.sqrt(2)*v,120.,120.])
            variants[str(epsilon)].extend(abs(a-b) for a,b in zip(row['rgb'],expected))
        records.append(dict(id=fp.id,page=floor['page'],x=x,y=y,surface=floor['ordinal'],world=p,
            geometric_normal=t.geometric_normal,shading_normal=t.evaluate(uv)[1],compiler_sample_origin=origin,
            footprint_area=fp.world_area,coverage=fp.coverage,
            footprint_pieces=[dict(triangle=p.triangle.id,uv=p.polygon,world=[p.triangle.evaluate(u)[0] for u in p.polygon]) for p in fp.pieces],
            incoming_direction=direction,vertical_sun=dict(direction=[0,0,1],compiler_brightness=120),
            oblique_sun_compiler_brightness=40,unshifted_physical_boundary_x=-16.,compiler_pushed_analytic_boundary_x=-15.,
            compiler_rgb8=row['rgb'],old_analytic_rgb8=old,residual_compiler_minus_old=[a-b for a,b in zip(row['rgb'],old)],
            epsilon_replay_rgb8=replay,expanded_hits=hits))
    result=dict(schema='org.openjk.shadow-boundary-x04.v1',bsp_sha256=digest,old_metrics=unchanged,
        records=records,exact_affected_count=len(records),replay_all_floor_max_error=max(all_errors),
        source_checks=source_checks,compiler_recipe=frozen['cases']['multi_sun']['authored'],
        compiler_sha256='894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4',
        trace_nodes=tree.node_count,trace_triangles=tree.triangle_count,
        epsilon_sweep={k:dict(max_error=max(v),sum_absolute_error=sum(v)) for k,v in variants.items()},
        omitted_band_rgb8_absolute_energy=sum(abs(x) for r in records for x in r['residual_compiler_minus_old']),
        geometric_scene=[dict(id=t.id,world=t.world,optical=t.optical) for t in casters],
        replay_scope='opaque/sky clipped trace-tree subset, supplied original sample origin; NOT instrumented compiler or general replay',
        historical_raven='UnknownProfile')
    output.mkdir(parents=True,exist_ok=False)
    (output/'boundary.json').write_text(json.dumps(result,sort_keys=True,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('exact_affected_count','replay_all_floor_max_error','epsilon_sweep','omitted_band_rgb8_absolute_energy')},indent=2))
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bsp',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); run(args.bsp,args.output)
