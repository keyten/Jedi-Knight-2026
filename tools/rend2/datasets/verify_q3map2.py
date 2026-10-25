#!/usr/bin/env python3
"""Independent original-BSP UV/geometry and baked-light behaviour checks.
Uses evaluator-only recipe/ground truth; no inverse reconstruction or physical calibration.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
from run_pipeline import run, write_json, sha


def sub(a,b): return [x-y for x,y in zip(a,b)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a):
    length=math.sqrt(dot(a,a))
    return [x/length for x in a] if length else a


def original_geometry(path):
    b=path.read_bytes()
    lumps=[struct.unpack_from('<II',b,8+i*8) for i in range(18)]
    vo,vb=lumps[10]; so,sb=lumps[13]; io,ib=lumps[11]
    verts=[]
    for p in range(vo,vo+vb,80):
        verts.append((struct.unpack_from('<3f',b,p), [struct.unpack_from('<2f',b,p+20+i*8) for i in range(4)],struct.unpack_from('<3f',b,p+52)))
    indices=struct.unpack_from('<'+'i'*(ib//4),b,io)
    surfaces=[]
    for p in range(so,so+sb,148):
        shader,_,kind,fv,nv,fi,ni=struct.unpack_from('<7i',b,p)
        surfaces.append(dict(shader=shader,kind=kind,vertices=verts[fv:fv+nv],indices=indices[fi:fi+ni],
                             styles=b[p+28:p+32],images=struct.unpack_from('<4i',b,p+36)))
    return surfaces


def check_mapping(row,surface):
    assert row['material']==surface['shader']
    slot=next(i for i in range(4) if surface['styles'][i]==row['style'] and surface['images'][i]>=0)
    uv=((row['x']+.5)/128,(row['y']+.5)/128)
    best=1e20
    for t in range(0,len(surface['indices']),3):
        a,b,c=[surface['vertices'][i] for i in surface['indices'][t:t+3]]
        av,bv,cv=[v[1][slot] for v in (a,b,c)]
        e,f,p=sub(bv,av),sub(cv,av),sub(uv,av)
        det=e[0]*f[1]-e[1]*f[0]
        if abs(det)<1e-14: continue
        u=(p[0]*f[1]-p[1]*f[0])/det; v=(e[0]*p[1]-e[1]*p[0])/det
        if min(u,v,1-u-v)<-1e-6: continue
        expected=[a[0][i]+u*(b[0][i]-a[0][i])+v*(c[0][i]-a[0][i]) for i in range(3)]
        best=min(best,math.sqrt(dot(sub(row['world'],expected),sub(row['world'],expected))))
        assert abs(dot(norm(row['normal']),norm(a[2])))>.9999
    assert best<.002,(row,best)
    assert 0<row['area_world2']<=256.01
    assert row['source_state']==1  # available source-relative linear candidate, never lux
    for encoded,linear in zip(row['raw_rgb8'],row['source_relative_rgb']):
        assert abs(encoded/255-linear)<1e-6
    return best


def mean(rows,channel,predicate=lambda r:True):
    chosen=[r for r in rows if predicate(r)]
    assert len(chosen)>=3,'Insufficient controlled sample group'
    weight=sum(r['area_world2'] for r in chosen)
    return sum(r['raw_rgb8'][channel]*r['area_world2'] for r in chosen)/weight


def rejection_checks(tool, root, suite):
    """Exercise the real standalone executable; failed admission must publish nothing."""
    source = root/'inputs/point_room.jkobs'
    destination = root/'reports/rejected-output.jkobs'
    assert not destination.exists()
    results = {}

    def reject(name, argv, diagnostic):
        completed = subprocess.run([str(tool)]+list(map(str, argv)), stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, errors='replace')
        assert completed.returncode != 0 and diagnostic in completed.stdout, completed.stdout
        assert not destination.exists(), 'Rejected admission published an output'
        results[name] = dict(exit_code=completed.returncode, diagnostic=completed.stdout.strip())

    reject('mismatched_source', ['audit-bsp', source, suite/'authoring/base/maps/tube.bsp'],
           'fingerprint mismatch')
    reject('private_answer_profile', ['benchmark', source, destination, 'test-version',
           suite/'answers/point_room.q3map-recipe.json'], 'controlled linear')
    public = json.loads((root/'reports/point_room.public-profile.json').read_text())
    public['entities'] = [{'classname':'light','origin':'ANSWER_ONLY_DO_NOT_EXPORT'}]
    path = root/'reports/rejected-public-profile.json'
    write_json(path, public)
    reject('contaminated_public_profile', ['benchmark', source, destination, 'test-version', path],
           'Ground-truth data')
    del public['entities']
    public['transfer'] = 'unknown'
    write_json(path, public)
    reject('unknown_transfer', ['benchmark', source, destination, 'test-version', path],
           'controlled linear')
    corrupt = bytearray(source.read_bytes()); corrupt[200] ^= 1
    path = root/'reports/rejected-corrupt.jkobs'
    path.write_bytes(corrupt)
    reject('corrupted_archive', ['validate', path], 'checksum')
    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--suite',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    args=p.parse_args(); root=args.runtime.resolve(); suite=args.suite.resolve()
    result=json.loads((root/'result.json').read_text()); geometry={};floor={};max_error=0;count=0
    for name,item in result['maps'].items():
        source=suite/f'authoring/base/maps/{name}.bsp'
        assert sha(source)==item['audit']['source_sha256']
        surfaces=original_geometry(source)
        rows=[];block_partition={}
        for part in ('train','validation','heldout'):
            for line in (root/f'reports/{name}.{part}.jsonl').read_text().splitlines():
                row=json.loads(line);rows.append(row)
                block=tuple(math.floor(x/128) for x in row['world'])
                assert block_partition.setdefault(block,part)==part
                max_error=max(max_error,check_mapping(row,surfaces[row['surface']]))
        assert rows
        count+=len(rows)
        geometry[name]=dict(samples=len(rows),source_surfaces=len(surfaces),spatial_partitions_disjoint=True)
        floor[name]=[r for r in rows if abs(r['world'][2])<.002 and r['normal'][2]>.9]
        assert floor[name]
    metrics={}
    a=floor['wall_leak']
    metrics['wall_left']=mean(a,0,lambda r:r['world'][0]<-48)
    metrics['wall_right']=mean(a,0,lambda r:r['world'][0]>48)
    assert metrics['wall_left']>metrics['wall_right']+10
    assert metrics['wall_right']<=1,'Baked light leaked into sealed room'
    a=floor['door_occlusion']
    metrics['door_lit']=mean(a,0,lambda r:80<=r['world'][0]<=160 and abs(r['world'][1])<=24)
    metrics['door_shadow']=mean(a,0,lambda r:80<=r['world'][0]<=160 and r['world'][1]>=80)
    assert metrics['door_lit']>metrics['door_shadow']+5
    a=floor['two_colour']; left=lambda r:r['world'][0]<-64;right=lambda r:r['world'][0]>64
    metrics['two_colour_left_r_b']=mean(a,0,left)/(mean(a,2,left)+.01)
    metrics['two_colour_right_r_b']=mean(a,0,right)/(mean(a,2,right)+.01)
    assert metrics['two_colour_left_r_b']>1.5 and metrics['two_colour_right_r_b']<.7
    metrics['one_point_mean']=mean(floor['point_room'],0)
    metrics['two_white_mean']=mean(floor['overlap_whites'],0)
    assert metrics['two_white_mean']>metrics['one_point_mean']*1.1
    gt=json.loads((suite/'answers/spot.q3map-recipe.json').read_text())['ground_truth']['lights'][0]
    axis=norm(gt['axis_propagation']);pos=gt['position_world']
    inside=lambda r:dot(norm(sub(r['world'],pos)),axis)>math.cos(math.radians(20))
    outside=lambda r:dot(norm(sub(r['world'],pos)),axis)<math.cos(math.radians(45))
    metrics['spot_inside']=mean(floor['spot'],0,inside);metrics['spot_outside']=mean(floor['spot'],0,outside)
    assert metrics['spot_inside']>metrics['spot_outside']+10 and metrics['spot_outside']<=1
    metrics['panel_mean']=mean(floor['panel'],0);metrics['tube_mean']=mean(floor['tube'],0)
    assert metrics['panel_mean']>1 and metrics['tube_mean']>1
    metrics['bounce_delta_rgb']=[mean(floor['bounce_colour'],c)-mean(floor['bounce_control'],c) for c in range(3)]
    assert metrics['bounce_delta_rgb'][0]>.2 and metrics['bounce_delta_rgb'][0]>metrics['bounce_delta_rgb'][2]
    traces={}
    for name,start,end,expected,sky in [
        ('wall_leak',[-80,0,80],[80,0,80],1,None),
        ('door_occlusion',[-80,0,80],[80,0,80],0,None),
        ('point_room',[0,0,1],[0,0,400],1,0),
        ('sun_sky',[0,0,1],[0,0,400],None,1),
        ('skylight',[0,0,1],[0,0,400],None,1),
        ('skylight',[100,0,1],[100,0,400],1,0)]:
        text=run([args.tool.resolve(),'trace',root/f'inputs/{name}.jkobs']+list(map(str,start+end))+
                 (['sky'] if sky is not None else []))
        if expected is not None: assert f'visibility={expected} ' in text,text
        if sky is not None: assert f'sky={sky} ' in text,text
        traces[name+str(start)]=text.strip()
    negative = rejection_checks(args.tool.resolve(), root, suite)
    report=dict(schema='org.openjk.q3map2-bake-observation-checks',version=1,
                independent_world_samples=count,maximum_world_mapping_error=max_error,
                maps=geometry,metrics=metrics,traces=traces,rejection_checks=negative,passed=True,
                claims='raw transport/geometry behaviour, not fitting accuracy or calibrated physical intensity')
    write_json(root/'verification.json',report)
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
