#!/usr/bin/env python3
"""S02 own-content q3map2 controls -> blind production C++ solar fit.

The trusted controller owns authored sun answers. No target sun direction/RGB
is supplied to the fit unless --authored-prior is explicitly requested.
Standard library only. SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
for name in ('bake_model','shadow_footprint','datasets'):
    sys.path.insert(0,str(HERE.parent/name))
from compare_bakes import brush,entity,make_assets,sha
from bake_model import COMPILER_SHA256
from compiler_boundary import load
from prepare_blind import assignment
from validate_cpp import request,source
from geometry import unit,dot

NAMES=('sun_oriented','sun_shadow','sky_aperture','sky_aperture_positive_v2','sun_with_lamp','ambient_only','no_sun','multi_directional')
SPLIT=dict(block=128.,gap=1.,seed=20261030,origin=(64.,64.,64.))
def write(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')
def peak(handle):
    if sys.platform!='win32':return None
    class Memory(ctypes.Structure):
        _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t)for n in('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
    m=Memory();m.cb=ctypes.sizeof(m);q=ctypes.WinDLL('psapi').GetProcessMemoryInfo;q.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
    return m.peak_ws if q(int(handle),ctypes.byref(m),m.cb)else None

def acceptance(name,result,angle):
    hypotheses=result['hypotheses'];positive_sun=name in('sun_oriented','sun_shadow','sun_with_lamp')
    if name=='sky_aperture' and not hypotheses:
        return dict(status='InsufficientPositiveValidation',strict_complete_coverage_gate=False,reason='Retained original v1 failed positive fixture: validation signal occupies one block; not a passed skylight benchmark')
    if positive_sun:
        if not hypotheses or result['code'] or angle is None or angle>2:raise AssertionError(name+' supported direct-sun angular/coverage gate')
    elif name in('sky_aperture','sky_aperture_positive_v2'):
        if result['state']!='SkyOnly'or not hypotheses or any(h['canonical_suns']for h in hypotheses):raise AssertionError('Sky must not become direct sun')
    elif hypotheses:raise AssertionError(name+' must not certify the unsupported/negative explanation')
    if positive_sun or name in('sky_aperture','sky_aperture_positive_v2'):
        held=result['heldout'][0]
        if held['code']or held['rmse']>1 or held['maximum_error']>3 or held['coverage']['black_rmse']<4 or held['coverage']['positive_blocks']<2:raise AssertionError(name+' non-vacuous untouched held-out gate')
        if held['coverage']['used']!=held['coverage']['requested']:
            return dict(status='PartialUnsupportedVisibility',strict_complete_coverage_gate=False,unsupported=held['coverage']['requested']-held['coverage']['used'],
                        reason='Original complete-coverage gate remains FAILED; unknown receivers stay unknown. Error metrics apply only to explicitly reported supported area; no tolerance changed',angular_limit_degrees=2 if positive_sun else None,heldout_interval_rmse_limit=1)
    return dict(status='Passed',strict_complete_coverage_gate=True,angular_limit_degrees=2 if positive_sun else None,heldout_interval_rmse_limit=1 if positive_sun or name in('sky_aperture','sky_aperture_positive_v2')else None,
                scope='Supported single-sun/sky controlled classes; negative rejection is not recovery of missing source count or GI')

def compile_one(compiler,root,name):
    folder=root/name;base=folder/'authoring/base';make_assets(base)
    sun=name in ('sun_oriented','sun_shadow','sun_with_lamp','multi_directional')
    sky=name in('sky_aperture','sky_aperture_positive_v2')
    skylight=240 if name=='sky_aperture_positive_v2' else 120
    shader=base/'shaders/bake_forward.shader'
    declaration=('q3map_sun 1 1 1 120 30 55\n' if sun else '')+('q3map_sun 1 0 0 70 160 35\n' if name=='multi_directional' else '')+(f'q3map_lightRGB 1 1 1\n q3map_skyLight {skylight} 3\n'if sky else '')
    shader.write_text(shader.read_text()+f'\ntextures/bake_forward/s02sky\n{{\n surfaceparm sky\n surfaceparm nolightmap\n {declaration} skyparms - 512 -\n}}\n',encoding='ascii',newline='\n')
    geometry=[brush((-400,-400,-16),(400,400,0)),brush((-400,-400,0),(-384,400,432)),brush((384,-400,0),(400,400,432)),brush((-384,-400,0),(384,-384,432)),brush((-384,384,0),(384,400,432))]
    if sky:
        opening=272 if name=='sky_aperture_positive_v2'else 192
        geometry += [brush((-384,-384,400),(-opening,384,416)),brush((opening,-384,400),(384,384,416)),brush((-opening,-384,400),(opening,-opening,416)),brush((-opening,opening,400),(opening,384,416)),brush((-400,-400,432),(400,400,448),'s02sky')]
    else:geometry+=[brush((-400,-400,400),(400,400,416),'white' if name=='no_sun'else's02sky')]
    if name in ('sun_shadow','sun_with_lamp'):geometry += [brush((-4,-384,0),(4,120,240))]
    world={'classname':'worldspawn','gridsize':'64 64 64'}
    if name=='ambient_only':world['_ambient']='50'
    text=entity(world)[:-2]+''.join(geometry)+'}\n'+entity({'classname':'info_player_start','origin':'-160 -160 64'})
    if name=='sun_with_lamp':text+=entity({'classname':'light','origin':'-160 -160 180','light':'300','_color':'1 .2 .1'})
    mp=base/'maps'/(name+'.map');mp.write_text(text,encoding='ascii',newline='\n')
    argv=[]
    for i,stage in enumerate((['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0','-bounce','0','-super','1','-nocollapse'])):
        cmd=[str(compiler),'-game','ja','-fs_basepath',str(base.parent.resolve()),'-fs_game','base','-threads','1',*stage,str(mp.resolve())]
        p=subprocess.run(cmd,capture_output=True,text=True,errors='replace',timeout=180)
        (folder/f'stage{i}.log').write_text(p.stdout+p.stderr,encoding='utf-8')
        if p.returncode or 'Entity leaked'in p.stdout or 'ERROR'in p.stdout:raise RuntimeError('Compiler failed '+name)
        argv.append(cmd)
    az,el=map(math.radians,(30,55));direction=[math.cos(az)*math.cos(el),math.sin(az)*math.cos(el),math.sin(el)]
    truth=dict(name=name,bsp_sha256=sha(mp.with_suffix('.bsp')),map_sha256=sha(mp),compiler_sha256=sha(compiler),shader_sha256=sha(shader),argv=argv,
               sun_to_source=direction if sun else None,sun_compiler_rgb=[120]*3 if sun else None,sky_compiler_rgb=[skylight]*3 if sky else None,absolute_photometry='Unknown compiler-relative scale',bounce=0,
               source_count=2 if name=='multi_directional'else 1 if sun else 0,local_lamp_known=name=='sun_with_lamp')
    write(folder/'private-recipe.json',truth);return mp.with_suffix('.bsp'),truth

def observations(path):
    lumps,surfaces,casters=load(path,strict_receiver_uv=False);rows={}
    for s in surfaces:
        if s['page']<0 or s['shader']['flags']&0x2000:continue
        for tri in s['triangles']:
            for y in range(max(0,int(min(v[1]for v in tri.uv)*128)),min(128,int(max(v[1]for v in tri.uv)*128)+1)):
                for x in range(max(0,int(min(v[0]for v in tri.uv)*128)),min(128,int(max(v[0]for v in tri.uv)*128)+1)):
                    uv=((x+.5)/128,(y+.5)/128)
                    if min(tri.bary(uv))<1e-5:continue
                    p,n=tri.evaluate(uv)
                    if max(abs(p[0]),abs(p[1]))>384.001 or p[2]<-.001 or p[2]>352:continue
                    # Avoid brush corners and newly compiled receiver nudge classes.
                    if abs(n[2])>.9 and (abs(p[0])>320 or abs(p[1])>320):continue
                    if abs(n[2])<.9 and (p[2]<48 or (abs(n[0])>.9 and abs(p[1])>320)or(abs(n[1])>.9 and abs(p[0])>320)):continue
                    key=(s['ordinal'],s['page'],x,y);radius=max(math.dist(p,tri.evaluate((uv[0]+du/256,uv[1]+dv/256))[0])for du in(-1,1)for dv in(-1,1))
                    row=dict(world=p,position=p,normal=n,surface=s['ordinal'],page=s['page'],x=x,y=y,area=tri.jacobian/(128*128),footprint_radius=radius)
                    row['partition']=assignment(row,**SPLIT)
                    if row['partition']is None:continue
                    offset=(s['page']*128*128+y*128+x)*3;row['rgb']=list(lumps[14][offset:offset+3]);rows[key]=row
    # Geometry-only deterministic round-robin across normal and spatial blocks.
    folds={}
    for role in ('train','validation','heldout'):
        groups={}
        for r in rows.values():
            if r['partition']!=role:continue
            key=tuple(round(v,3)for v in r['normal'])+tuple(math.floor((v-o)/128)for v,o in zip(r['world'],SPLIT['origin']))
            groups.setdefault(key,[]).append(r)
        ordered=[sorted(v,key=lambda r:(r['surface'],r['page'],r['y'],r['x']))for _,v in sorted(groups.items())];selected=[]
        for i in range(max(map(len,ordered),default=0)):
            for group in ordered:
                if i<len(group)and len(selected)<72:selected.append(group[i])
        if len(selected)<8:raise ValueError('Insufficient geometry-only fold '+role)
        folds[role]=selected
    allrows=[]
    for role in folds:
        for r in folds[role]:r['id']=len(allrows)+1;allrows.append(r)
    roles=list(folds)
    for i,a in enumerate(roles):
        for b in roles[i+1:]:
            for r in folds[a]:
                for s in folds[b]:
                    block=lambda row:tuple(math.floor((v-o)/SPLIT['block'])for v,o in zip(row['world'],SPLIT['origin']))
                    if block(r)==block(s)or math.dist(r['world'],s['world'])<=3*(r['footprint_radius']+s['footprint_radius'])+SPLIT['gap']:raise ValueError('Spatial/filter leakage across '+a+'/'+b)
    return lumps,casters,folds,allrows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compiler',type=Path,default=ROOT/'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe')
    p.add_argument('--tool',type=Path,default=ROOT/'build/lightrecon-disk-standalone/lightrecon_bake_forward_tool.exe')
    p.add_argument('--names',nargs='+',choices=NAMES,default=NAMES);p.add_argument('--reuse',type=Path,help='Reuse own-content pinned bakes from a previous run; still verify recipe/BSP hashes')
    p.add_argument('--authored-prior',action='store_true',help='Explicit allowed sun direction prior; no intensity supplied')
    a=p.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=False);compiler=a.compiler.resolve();tool=a.tool.resolve()
    if sha(compiler)!=COMPILER_SHA256:raise ValueError('Pinned compiler required')
    report=dict(schema='org.openjk.s02-compiled-validation.v1',compiler_sha256=sha(compiler),tool_sha256=sha(tool),split=SPLIT,cases={});start=time.perf_counter()
    for name in a.names:
        case_start=time.perf_counter()
        if a.reuse:
            baked=a.reuse.resolve()/name;truth=json.loads((baked/'private-recipe.json').read_text());path=baked/'authoring/base/maps'/(name+'.bsp')
            if sha(path)!=truth['bsp_sha256']or truth['compiler_sha256']!=COMPILER_SHA256:raise ValueError('Frozen bake fingerprint')
        else:path,truth=compile_one(compiler,root,name)
        folder=root/(name+'-fit');folder.mkdir();lumps,casters,folds,rows=observations(path)
        priors=[]
        if a.authored_prior and truth['sun_to_source']:priors=[source(4,axis=[-v for v in truth['sun_to_source']],rgb=(0,0,0))]
        geometry=folder/'geometry.request'
        other={r['id']:[0,0,0]for r in rows}
        unsupported=[]
        if name=='sun_with_lamp':
            # Explicit fixed local-lamp nuisance from production C++, never RGB8 subtraction.
            fixed=folder/'local-lamp.request';fixed.write_text(request(path,lumps,casters,rows,{},[source(position=(-160,-160,180),rgb=(300,60,30))],domain=3))
            pp=subprocess.run([str(tool),str(fixed)],capture_output=True,text=True,timeout=120);pp.check_returncode();pred=json.loads(pp.stdout)
            for row,value in zip(rows,pred['rows']):
                if not value['available']:unsupported.append(dict(sample=row['id'],partition=row['partition'],status=value['status'],reason='Unsupported original nominal receiver placement in known local-lamp operator'))
                else:other[row['id']]=value['rgb']
            # Explicit supported subset, same for all models/roles. A solid-cell
            # placement is never made black or "fixed" by a renderer nudge.
            rejected={r['sample']for r in unsupported};supported=[];new_other={}
            for role,values in folds.items():
                folds[role]=[r for r in values if r['id']not in rejected]
                for row in folds[role]:
                    old=row['id'];row['id']=len(supported)+1;supported.append(row);new_other[row['id']]=other[old]
            rows,other=supported,new_other
        geometry.write_text(request(path,lumps,casters,rows,dict(ambient=50 if name=='ambient_only'else 0),priors),encoding='ascii')
        for role,values in folds.items():
            text=['JKSOLAR1',str(len(values))]+[' '.join(map(str,[r['id'],*r['rgb'],r['area'],r['footprint_radius'],*other[r['id']]]))for r in values]
            (folder/(role+'.txt')).write_text('\n'.join(text)+'\n',encoding='ascii')
        cmd=[str(tool),'--solar',str(geometry),*[str(folder/(role+'.txt'))for role in('train','validation','heldout')]]
        with subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)as process:
            stdout,stderr=process.communicate(timeout=120)
            cpp_peak=peak(process._handle)if sys.platform=='win32'else None
            if process.returncode:raise RuntimeError(name+': '+stderr)
        result=json.loads(stdout);write(folder/'diagnostic.json',result);(folder/'stderr.txt').write_text(stderr)
        accepted=result['hypotheses'];best=accepted[0]if accepted else None
        angle=None
        if best and best['model']==2 and truth['sun_to_source']:angle=math.degrees(math.acos(max(-1,min(1,dot(unit(best['to_source']),truth['sun_to_source'])))))
        sun_error=math.dist(best['sun_compiler_rgb'],truth['sun_compiler_rgb'])/math.sqrt(dot(truth['sun_compiler_rgb'],truth['sun_compiler_rgb']))if best and best['model']==2 and truth['sun_compiler_rgb']else None
        sky_error=math.dist(best['sky_compiler_rgb'],truth['sky_compiler_rgb'])/math.sqrt(dot(truth['sky_compiler_rgb'],truth['sky_compiler_rgb']))if best and truth['sky_compiler_rgb']else None
        report['cases'][name]=dict(bsp_sha256=truth['bsp_sha256'],source_map_sha256=truth['map_sha256'],recipe_sha256=sha((a.reuse.resolve()/name if a.reuse else root/name)/'private-recipe.json'),
            folds={role:dict(count=len(v),positive=sum(max(r['rgb'])>=8 for r in v),normal_orientations=len(set(tuple(round(x,3)for x in r['normal'])for r in v)),sha256=sha(folder/(role+'.txt')))for role,v in folds.items()},
            unsupported_original_receivers=unsupported,supplied_sun_prior=a.authored_prior,hidden_sun_direction_used_only_by_trusted_evaluator=True,state=result['state'],code=result['code'],angular_error_degrees=angle,
            sun_rgb_relative_l2_error=sun_error,sky_rgb_relative_l2_error=sky_error,resources=dict(cpp_peak_rss_bytes=cpp_peak,controller_peak_rss_bytes=peak(-1)if sys.platform=='win32'else None,total_case_wall_seconds=time.perf_counter()-case_start),
            acceptance=acceptance(name,result,angle),diagnostic=result,actual_historical_ja_profile='UnknownProfile',absolute_photometry='NotComparable')
        write(root/'result.json',report);print(name,result['state'],'angular',angle,'validation',best['validation_rmse']if best else None,'heldout',result['heldout'][0]['rmse']if result['heldout']else None,flush=True)
    report['wall_seconds']=time.perf_counter()-start;write(root/'result.json',report)

if __name__=='__main__':main()
