#!/usr/bin/env python3
"""Compile own-content JA rooms and compare source-derived kernels to BSP bytes.

No game assets, GL, solver, fit gain or renderer change. Python standard library.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from bake_model import (COMPILER_SHA256, SOURCE_REVISION, dot, sub, unit,
                        point_scalar, spot_factor, encode_color, normal_to_latlong, polygon_factor)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def entity(fields):
    return '{\n' + ''.join('"%s" "%s"\n' % item for item in fields.items()) + '}\n'


def brush(lo, hi, texture='white'):
    x,y,z = lo
    X,Y,Z = hi
    planes = [((x,y,Z),(X,y,Z),(X,Y,Z)), ((x,Y,z),(X,Y,z),(X,y,z)),
              ((x,y,z),(X,y,z),(X,y,Z)), ((X,Y,z),(x,Y,z),(x,Y,Z)),
              ((x,Y,z),(x,y,z),(x,y,Z)), ((X,y,z),(X,Y,z),(X,Y,Z))]
    return '{\n' + ''.join(' '.join('( %g %g %g )' % p for p in (t[0],t[2],t[1])) +
            ' bake_forward/%s 0 0 0 1 1 0 0 0\n' % texture for t in planes) + '}\n'


CASES = [
    dict(name='inverse_square'),
    dict(name='no_angle', flags=2, angle=False),
    dict(name='linear', flags=1, linear=True, angle=False),
    dict(name='linear_angle', flags=1, linear=True, keys={'_anglescale':'1'}),
    dict(name='linear_fade', flags=1, linear=True, angle=False, fade=0.5, keys={'fade':'0.5'}),
    dict(name='angle_scale', angle_scale=0.5, keys={'_anglescale':'0.5'}),
    dict(name='wolf_linear', linear=True, angle=False, options=['-wolf']),
    dict(name='wolf_inverse_square', flags=1, options=['-wolf']),
    dict(name='pointscale_half', pointscale=0.5, options=['-pointscale','0.5']),
    dict(name='extra_distance', extra=64, options=['-extradist','64']),
    dict(name='half_lambert', half_lambert=True, options=['-lightanglehl','1']),
    dict(name='spot', spot=True, flags=1),
    dict(name='colour_gamma', color=[0.2,0.5,1.0], gamma=2.2, options=['-gamma','2.2']),
    dict(name='colour_clip', color=[0.2,0.5,1.0], intensity=3000),
    dict(name='sun', sun=True),
    dict(name='multi_sun', sun=True, multi=True),
    dict(name='skylight', sun=True, skylight=True),
    dict(name='area', area=True),
    dict(name='ambient', ambient=20, no_emit=True),
    dict(name='bounce1', bounces=1),
    dict(name='bouncegrid1', bounces=1, options=['-bouncegrid']),
    dict(name='retained_light', bsp_options=['-keeplights']),
]


def author(base, case):
    name = case['name']
    shader = 'sky_'+name if case.get('sun') else 'panel' if case.get('area') else 'white'
    geometry = [brush((-400,-400,-16),(400,400,0)),
                brush((-400,-400,0),(-384,400,400)),
                brush((384,-400,0),(400,400,400)),
                brush((-384,-400,0),(384,-384,400)),
                brush((-384,384,0),(384,400,400)),
                brush((-400,-400,400),(400,400,416), shader)]
    world = '{\n"classname" "worldspawn"\n"gridsize" "64 64 64"\n' + (
        '"_ambient" "%s"\n'%case['ambient'] if case.get('ambient') else '') + ''.join(geometry) + '}\n'
    ents = [{'classname':'info_player_start','origin':'-160 -160 64'}]
    if not (case.get('sun') or case.get('area') or case.get('no_emit')):
        light = dict(classname='light', origin='0 0 192', light=str(case.get('intensity',300)),
                     _color=' '.join(map(str,case.get('color',[1,1,1]))), spawnflags=str(case.get('flags',0)))
        light.update(case.get('keys', {}))
        if case.get('spot'):
            light.update(target='spot_target',radius='64')
            ents.append(dict(classname='info_null',targetname='spot_target',origin='0 0 64'))
        ents.append(light)
    path = base/'maps'/(name+'.map')
    path.write_text('// Own-content forward-model fixture; GPL-2.0-or-later\n'+world+''.join(map(entity,ents)),encoding='ascii')
    return path


def make_assets(base):
    for folder in ['maps','shaders','textures/bake_forward']:
        (base/folder).mkdir(parents=True, exist_ok=True)
    header = struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,16,16,24,0x20)
    (base/'textures/bake_forward/white.tga').write_bytes(header + bytes([180]*3)*256)
    shader = '''textures/bake_forward/white
{
 qer_editorimage textures/bake_forward/white.tga
 { map $lightmap
 }
 { map textures/bake_forward/white.tga
   blendFunc GL_DST_COLOR GL_ZERO
 }
}
textures/bake_forward/panel
{
 q3map_surfacelight 200
 q3map_lightRGB 1 1 1
 q3map_lightSubdivide 4096
 q3map_backsplash 0 0
 { map textures/bake_forward/white.tga
 }
}
'''
    for case in CASES:
        if case.get('sun'):
            shader += '''textures/bake_forward/sky_%s
{
 surfaceparm sky
 surfaceparm nolightmap
 %s
 %s
 skyparms - 512 -
}
''' % (case['name'], 'q3map_lightRGB 1 1 1\n q3map_skyLight 120 3' if case.get('skylight') else
       'q3map_sun 1 1 1 120 0 90', 'q3map_sun 1 0 0 40 0 45' if case.get('multi') else '')
    (base/'shaders/bake_forward.shader').write_text(shader,encoding='ascii')
    (base/'shaders/shaderlist.txt').write_text('bake_forward\n',encoding='ascii')


def bsp(path):
    data = path.read_bytes()
    if data[:8] != b'RBSP\x01\0\0\0' or len(data)<152:
        raise ValueError('Expected JA RBSP v1')
    lumps=[]
    for i in range(18):
        offset,size=struct.unpack_from('<II',data,8+i*8)
        if offset>len(data) or size>len(data)-offset:
            raise ValueError('Out-of-bounds lump')
        lumps.append(data[offset:offset+size])
    for index,stride in [(7,40),(10,80),(11,4),(13,148),(14,128*128*3),(15,30),(17,2)]:
        if len(lumps[index]) % stride:
            raise ValueError('Incomplete BSP lump %d'%index)
    return lumps


def floor_samples(lumps, maximum_coordinate=272.):
    """Independent triangle barycentric mapping of texel centres to surface."""
    verts=[]
    for p in range(0,len(lumps[10]),80):
        verts.append((struct.unpack_from('<3f',lumps[10],p),struct.unpack_from('<2f',lumps[10],p+20),
                      struct.unpack_from('<3f',lumps[10],p+52)))
    indices=struct.unpack('<'+'i'*(len(lumps[11])//4),lumps[11])
    samples={}
    for p in range(0,len(lumps[13]),148):
        _,_,kind,fv,nv,fi,ni=struct.unpack_from('<7i',lumps[13],p)
        page=struct.unpack_from('<i',lumps[13],p+36)[0]
        if kind != 1 or page<0:
            continue
        if any(abs(v[0][2])>0.001 or v[2][2]<0.999 for v in verts[fv:fv+nv]):
            continue
        for t in range(fi,fi+ni,3):
            a,b,c=[verts[fv+indices[t+i]] for i in range(3)]
            av,bv,cv=a[1],b[1],c[1]
            e,f=sub(bv,av),sub(cv,av)
            det=e[0]*f[1]-e[1]*f[0]
            if abs(det)<1e-12:
                continue
            for y in range(max(0,int(min(av[1],bv[1],cv[1])*128)),min(128,int(max(av[1],bv[1],cv[1])*128)+1)):
                for x in range(max(0,int(min(av[0],bv[0],cv[0])*128)),min(128,int(max(av[0],bv[0],cv[0])*128)+1)):
                    q=sub(((x+.5)/128,(y+.5)/128),av)
                    u=(q[0]*f[1]-q[1]*f[0])/det
                    v=(e[0]*q[1]-e[1]*q[0])/det
                    if min(u,v,1-u-v)<-1e-6:
                        continue
                    world=[a[0][i]+u*(b[0][i]-a[0][i])+v*(c[0][i]-a[0][i]) for i in range(3)]
                    # Keep interior samples; the compiler nudges boundary samples.
                    if max(abs(world[0]),abs(world[1]))>maximum_coordinate:
                        continue
                    offset=(page*128*128+y*128+x)*3
                    deluxe_offset=offset+128*128*3
                    samples[(page,x,y)]=dict(world=world, rgb=list(lumps[14][offset:offset+3]),
                        deluxe_rgb=list(lumps[14][deluxe_offset:deluxe_offset+3]))
    if len(samples)<30:
        raise ValueError('Insufficient mapped floor samples')
    return list(samples.values())


def predict(case, receiver, surface=True):
    if case.get('no_emit'):
        # This revision calls SetupGrid before reading world ambient.
        return [case.get('ambient',0)]*3 if surface else [0,0,0]
    if case.get('area'):
        winding=[[-384,-384,400],[384,-384,400],[384,384,400],[-384,384,400]]
        return [150.0*abs(polygon_factor(receiver,[0,0,1],winding))]*3
    if case.get('sun'):
        if case.get('skylight'):
            directions=[]
            angle=0.0
            for elevation in (math.pi/12,math.pi/4):
                for j in range(8):
                    directions.append([math.cos(angle)*math.cos(elevation),math.sin(angle)*math.cos(elevation),math.sin(elevation)])
                    angle+=math.pi/4
                angle+=math.pi/8
            directions.append([0,0,1])
            value=0.0
            for u in directions:
                t=(400-receiver[2])/u[2]
                if max(abs(receiver[i]+t*u[i]) for i in range(2))<384:
                    value+=120.0/17*u[2]
            return [value]*3
        # Vertical rays reach sky; the oblique second sun may meet a side wall.
        extra = (40/math.sqrt(2) if case.get('multi') and
                 receiver[0] + (400-receiver[2]) < 384 else 0.0)
        return [120.0+extra,120.0,120.0] if surface else [120.0,120.0,120.0]
    delta=sub((0,0,192),receiver)
    distance=math.sqrt(dot(delta,delta))
    cosine=delta[2]/distance if surface and case.get('angle',True) else 1.0
    value=point_scalar(case.get('intensity',300),distance,cosine,
                       linear=case.get('linear',False) and not case.get('spot'),
                       scale=7500*case.get('pointscale',1),extra_distance=case.get('extra',0),
                       fade=case.get('fade',1),angle_scale=case.get('angle_scale',0) if surface else 0,
                       half_lambert=surface and case.get('half_lambert',False))
    if case.get('spot'):
        value*=spot_factor(receiver,(0,0,192),(0,0,-1),128)
        if value<=1:
            value=0.0
    return [value*c for c in case.get('color',[1,1,1])]


def metrics(case, lumps):
    rows=floor_samples(lumps)
    if case.get('bounces'):
        indices=struct.unpack('<'+'H'*(len(lumps[17])//2),lumps[17])
        resolved=b''.join(lumps[15][i*30:i*30+30] for i in indices)
        return dict(validation_kind='paired-bounce-behaviour; not exact bounce replay',
                    floor_mean=sum(r['rgb'][0] for r in rows)/len(rows),
                    resolved_grid_sha256=hashlib.sha256(resolved).hexdigest(),passed=None)
    errors=[]
    wrong_errors=[]
    examples=[]
    boundary_errors=[]
    deluxe_errors=[]
    for row in rows:
        receiver=[row['world'][0],row['world'][1],1.0]  # default sample push
        expected=encode_color(predict(case,receiver),gamma=case.get('gamma',1))
        # Analytic box visibility does not reproduce TraceTriangle edge epsilon.
        # Retain and report discrepancies in a 2-unit band around this one seam.
        if case.get('multi') and abs(receiver[0]+399-384)<2:
            boundary_errors.extend(abs(a-b) for a,b in zip(row['rgb'],expected))
            continue
        errors.extend(abs(a-b) for a,b in zip(row['rgb'],expected))
        if not (case.get('sun') or case.get('area')):
            color=[0,0,0] if case.get('no_emit') else predict(case,receiver)
            brightness=dot(color,[0.2989,0.5870,0.1140])/255.0
            incoming=unit(sub((0,0,192),receiver))
            ambient=max(case.get('ambient',0)*0.9999/255.0,1.0/256)
            direction=unit([incoming[0]*brightness,incoming[1]*brightness,incoming[2]*brightness+ambient])
            encoded=[int(127.5+127.5*x) for x in direction]
            deluxe_errors.extend(abs(a-b) for a,b in zip(row['deluxe_rgb'],encoded))
        if case['name'] in ('inverse_square','linear'):
            wrong=dict(case,linear=not case.get('linear',False))
            other=encode_color(predict(wrong,receiver))
            wrong_errors.extend((a-b)**2 for a,b in zip(row['rgb'],other))
        if abs(receiver[1])<0.01 and receiver[0]>=0:
            examples.append(dict(world=row['world'],raw_rgb8=row['rgb'],predicted_rgb8=expected))
    # Float compiler arithmetic and UV->world roundoff may move a truncation by 1.
    result=dict(surface_samples=len(rows),surface_checked_channels=len(errors),surface_max_byte_error=max(errors),
                surface_mean_abs_byte_error=sum(errors)/len(errors),examples=examples,
                unchecked_sky_wall_boundary_channels=len(boundary_errors),
                unchecked_sky_wall_boundary_max_error=max(boundary_errors,default=0),
                deluxe_checked_channels=len(deluxe_errors),deluxe_max_byte_error=max(deluxe_errors,default=0))
    if wrong_errors:
        result['wrong_falloff_rmse_without_refitting']=math.sqrt(sum(wrong_errors)/len(wrong_errors))
    # Interior grid points only: no random near-solid displacement or inhibited rays.
    indices=struct.unpack('<'+'H'*(len(lumps[17])//2),lumps[17])
    lo=struct.unpack_from('<3f',lumps[7],0)
    hi=struct.unpack_from('<3f',lumps[7],12)
    mins=[64*math.ceil(x/64) for x in lo]
    dims=[int((64*math.floor(hi[i]/64)-mins[i])/64)+1 for i in range(3)]
    if math.prod(dims)!=len(indices):
        raise ValueError('Unexpected grid dimensions')
    grid_errors=[]
    direction_errors=[]
    for ordinal,index in enumerate(indices):
        pos=[mins[0]+(ordinal%dims[0])*64,
             mins[1]+((ordinal//dims[0])%dims[1])*64,
             mins[2]+(ordinal//(dims[0]*dims[1]))*64]
        if max(abs(pos[0]),abs(pos[1]))>128 or pos[2] not in (64,128,256):
            continue
        if case.get('sun') or case.get('area'):
            continue  # multi-source grid reduction needs its own validation
        record=lumps[15][index*30:index*30+30]
        if len(record)!=30:
            raise ValueError('Bad grid index')
        color=predict(case,pos,False)
        directed=encode_color(color,gamma=case.get('gamma',1))
        ambient=[1,1,1] if any(color) else [0,0,0]
        expected=ambient+directed
        grid_errors.extend(abs(a-b) for a,b in zip(list(record[:3]+record[12:15]),expected))
        if any(color):
            encoded=normal_to_latlong(unit(sub((0,0,192),pos)))
            direction_errors.extend(min((a-b)%256,(b-a)%256) for a,b in zip(record[28:30],encoded))
    result.update(grid_checked_channels=len(grid_errors),grid_max_byte_error=max(grid_errors,default=0))
    result.update(grid_direction_checked_angles=len(direction_errors),grid_direction_max_bin_error=max(direction_errors,default=0))
    # RBSP AddLightGridLumps merges approximate records at LG_EPSILON=4.
    # This is a source-defined storage error budget, not a fitted tolerance.
    result['grid_storage_merge_epsilon']=4
    result['passed']=(result['surface_max_byte_error']<=1 and result['grid_max_byte_error']<=5
                      and result['deluxe_max_byte_error']<=1 and result['grid_direction_max_bin_error']<=5)
    if wrong_errors:
        result['passed'] &= result['wrong_falloff_rmse_without_refitting']>5
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--q3map2',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--case',action='append',choices=[c['name'] for c in CASES])
    args=parser.parse_args()
    root=args.output.resolve()
    if root.exists():
        parser.error('Use a fresh output directory; existing witnesses are never overwritten')
    root.mkdir(parents=True)
    base=root/'authoring/base'
    make_assets(base)
    compiler=args.q3map2.resolve()
    version=subprocess.run([str(compiler),'-help'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace').stdout
    (root/'compiler-version.txt').write_text(version,encoding='utf-8')
    report=dict(schema='org.openjk.bake-model-comparison',version=1,source_revision=SOURCE_REVISION,
                compiler_sha256=sha(compiler),compiler_version=version.splitlines()[:6],
                expected_binary=sha(compiler)==COMPILER_SHA256,scale_fitted=False,
                encoding='gamma/compensation/exposure explicitly recorded per case; RGB8 truncation',cases={})
    common=[str(compiler),'-game','ja','-fs_basepath',str(base.parent),'-fs_game','base','-threads','1']
    for case in CASES:
        if args.case and case['name'] not in args.case:
            continue
        path=author(base,case)
        stages=[['-meta','-leaktest','-noflares','-samplesize','16']+case.get('bsp_options',[]),['-vis'],
                ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0',
                 '-bounce',str(case.get('bounces',0)),'-super','1','-nocollapse']+case.get('options',[])]
        commands=[]
        for i,stage in enumerate(stages):
            command=common+stage+[str(path)]
            commands.append(command)
            completed=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
            (root/('%s-stage%d.log'%(case['name'],i))).write_text(completed.stdout,encoding='utf-8')
            if completed.returncode or re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked|Unknown (?:argument|option)',completed.stdout,re.I):
                raise RuntimeError('Compiler stage failed: '+str(command)+'\n'+completed.stdout[-2000:])
        raw=bsp(path.with_suffix('.bsp'))
        result=metrics(case,raw)
        retained=len(re.findall(rb'"classname"\s+"light"',raw[0]))
        result['retained_light_entities']=retained
        if not case.get('bounces'):
            result['passed'] &= retained==(1 if case.get('bsp_options') else 0)
        result.update(authored=case,argv=commands,map_sha256=sha(path),bsp_sha256=sha(path.with_suffix('.bsp')))
        report['cases'][case['name']]=result
        write_json(root/'result.json',report)
        if case.get('bounces'):
            print(case['name']+': compiled; paired check pending',flush=True)
        else:
            print('%s: %s; surface max=%d, grid max=%d'%(case['name'],'PASS' if result['passed'] else 'FAIL',result['surface_max_byte_error'],result['grid_max_byte_error']),flush=True)
    if any(c.get('bounces') for c in CASES if c['name'] in report['cases']):
        reference=root/'authoring/base/maps/inverse_square.bsp'
        if not reference.exists():
            raise ValueError('Bounce cases require --case inverse_square as a control')
        raw=bsp(reference)
        rows=floor_samples(raw)
        floor_mean=sum(r['rgb'][0] for r in rows)/len(rows)
        indices=struct.unpack('<'+'H'*(len(raw[17])//2),raw[17])
        grid_sha=hashlib.sha256(b''.join(raw[15][i*30:i*30+30] for i in indices)).hexdigest()
        for name in ('bounce1','bouncegrid1'):
            if name in report['cases']:
                case=report['cases'][name]
                case['floor_mean_increase']=case['floor_mean']-floor_mean
                case['grid_identical_to_direct_control']=case['resolved_grid_sha256']==grid_sha
                case['passed']=case['floor_mean_increase']>1 and case['grid_identical_to_direct_control']==(name=='bounce1')
                print('%s: %s; floor increase %.4f, grid identical=%s'%(name,'PASS' if case['passed'] else 'FAIL',case['floor_mean_increase'],case['grid_identical_to_direct_control']),flush=True)
    report['passed']=bool(report['cases']) and all(c['passed'] for c in report['cases'].values())
    report['profile_status']='controlled-kernels-verified' if report['passed'] and report['expected_binary'] else 'unverified'
    write_json(root/'result.json',report)
    if not report['passed']:
        raise SystemExit(1)


if __name__=='__main__':
    main()
