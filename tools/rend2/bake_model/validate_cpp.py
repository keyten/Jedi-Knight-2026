#!/usr/bin/env python3
"""Trusted S01 controller: production C++ vs Python vs frozen compiled BSPs.

The C++ request contains only geometry and specified sources, never target RGB.
No source fitting or per-case gains. Thresholds fixed before execution: <=1 byte
for floating-point direct kernels, exactly 0 for the scoped 1225-sample replay.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time
import ctypes
from compare_bakes import CASES, floor_samples, predict, encode_color, sha
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'shadow_footprint'))
from compiler_boundary import load
from compiler_trace import Replay


def source(kind=0,position=(0,0,192),axis=(0,0,-1),rgb=(300,300,300),polygon=()):
    return dict(kind=kind,position=position,axis=axis,rgb=rgb,polygon=polygon,target=128,radius=64)


def sources_for(case):
    if case.get('no_emit'):return []
    if case.get('area'):return [source(2,(0,0,399),polygon=((-384,-384,400),(384,-384,400),(384,384,400),(-384,384,400)),rgb=(200,200,200))]
    if case.get('sun'):
        if case.get('skylight'):return [source(6,rgb=(120,120,120))]
        result=[source(4,rgb=(120,120,120))]
        if case.get('multi'):result.append(source(4,axis=(-2**-.5,0,-2**-.5),rgb=(40,0,0)))
        return result
    rgb=[case.get('intensity',300)*v for v in case.get('color',(1,1,1))]
    return [source(1 if case.get('spot') else 0,rgb=rgb)]


def request(path,lumps,casters,rows,case,sources,domain=0,strict=False):
    recipe=[int(case.get('linear',False)),int(not case.get('angle',True)) if not case.get('half_lambert') else 2,
            7500*case.get('pointscale',1),16,case.get('fade',1),case.get('extra',0),case.get('angle_scale',0),1,1,1,.75,
            case.get('gamma',1),1,0,0,*([case.get('ambient',0)]*3)]
    text=['JKBAKE2',' '.join(map(str,recipe)),f'{domain} {int(strict)}',str(len(sources))]
    for i,s in enumerate(sources):
        text.append(' '.join(map(str,[i+1,s['kind'],*s['position'],*s['axis'],*s['rgb'],s['target'],s['radius'],len(s['polygon'])])))
        text.extend(' '.join(map(str,v)) for v in s['polygon'])
    text.append(str(len(rows)))
    text.extend(' '.join(map(str,[i+1,r.get('surface',0)+1,*r['world'],*r.get('normal',[0,0,1])])) for i,r in enumerate(rows))
    text.append(sha(path))
    for index,stride,fmt,select in [(2,16,'<4f',lambda x:x),(3,36,'<9i',lambda x:x[:3]),(4,48,'<12i',lambda x:x[:1])]:
        text.append(str(len(lumps[index])//stride))
        text.extend(' '.join(map(str,select(struct.unpack_from(fmt,lumps[index],offset)))) for offset in range(0,len(lumps[index]),stride))
    text.append(str(len(casters)))
    for i,t in enumerate(casters):text.append(' '.join(map(str,[i+1,int(t.optical=='sky'),*(x for v in t.world for x in v)])))
    return '\n'.join(text)+'\n'


def run_tool(exe,work,name,text):
    p=work/(name+'.request');p.write_text(text,encoding='ascii')
    with subprocess.Popen([str(exe),str(p)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as process:
        stdout,stderr=process.communicate(timeout=120)
        if process.returncode:raise RuntimeError(name+': '+stderr)
        result=json.loads(stdout)
        if result.get('source_lock')!=hashlib.sha256((Path(__file__).parent/'source-lock.json').read_bytes().replace(b'\r\n',b'\n')).hexdigest():raise ValueError('Production source lock mismatch')
        if result.get('catalogue_key')!=hashlib.sha256((Path(__file__).parent/'profiles.json').read_bytes().replace(b'\r\n',b'\n')).hexdigest():raise ValueError('Production catalogue mismatch')
        if sys.platform=='win32':
            class Memory(ctypes.Structure):
                _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in ('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
            memory=Memory();memory.cb=ctypes.sizeof(memory)
            query=ctypes.WinDLL('psapi').GetProcessMemoryInfo
            query.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong];query.restype=ctypes.c_int
            if query(int(process._handle),ctypes.byref(memory),memory.cb):result['peak_process_rss_bytes']=memory.peak_ws
    (work/(name+'.cpp.json')).write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
    return result


def run(exe,base,work):
    work.mkdir(parents=True,exist_ok=True);frozen=json.loads((Path(__file__).parent/'validation.json').read_text())
    results={};started=time.perf_counter()
    for case in CASES:
        name=case['name'];path=base/(name+'.bsp')
        if sha(path)!=frozen['cases'][name]['bsp_sha256']:raise ValueError('Frozen compiler BSP mismatch '+name)
        lumps,_,casters=load(path,strict_receiver_uv=False);rows=floor_samples(lumps)
        result=run_tool(exe,work,name,request(path,lumps,casters,rows,case,sources_for(case),strict=bool(case.get('multi'))))
        if not all(r['available'] for r in result['rows']):raise AssertionError(name+' unavailable supported coverage')
        residuals=[a-b for r,t in zip(result['rows'],rows) for a,b in zip(r['packed'],t['rgb'])]
        python=[encode_color(predict(case,[r['world'][0],r['world'][1],1]),gamma=case.get('gamma',1)) for r in rows]
        cpp_python=[a-b for r,t in zip(result['rows'],python) for a,b in zip(r['packed'],t)]
        record=dict(bsp_sha256=sha(path),samples=len(rows),max_byte_error=max(map(abs,residuals)),
                    rmse=math.sqrt(sum(x*x for x in residuals)/len(residuals)),
                    cpp_python_max=max(map(abs,cpp_python)),seconds=result['seconds'],pairs=result['pairs'],rays=result['rays'],
                    triangle_tests=result['tests'],node_tests=result['node_tests'],profile_id=result['profile_id'],profile_key=result['profile_key'],trace_bytes=result['trace_bytes'],trace_key=result['trace_key'],peak_process_rss_bytes=result.get('peak_process_rss_bytes'))
        if case.get('bounces'):
            record.update(status='UnsupportedBounceReplay',meaning='Direct-only prediction on paired bounce control; discrepancy is not certified GI')
        else:
            limit=0 if case.get('multi') else 1
            record.update(status='Passed' if record['max_byte_error']<=limit else 'Failed',predeclared_tolerance=limit)
        if name in ('inverse_square','spot','ambient'):
            deluxe=run_tool(exe,work,name+'-deluxe',request(path,lumps,casters,rows,case,sources_for(case),domain=2))
            record['deluxe_max_byte_error']=max(abs(a-b) for r,t in zip(deluxe['rows'],rows) for a,b in zip(r['deluxe'],t['deluxe_rgb']))
            if record['deluxe_max_byte_error']>1:record['status']='Failed'
        if name in ('inverse_square','linear','spot'):
            indices=struct.unpack('<'+'H'*(len(lumps[17])//2),lumps[17])
            lo=struct.unpack_from('<3f',lumps[7]);hi=struct.unpack_from('<3f',lumps[7],12)
            mins=[64*math.ceil(v/64) for v in lo];dims=[int((64*math.floor(hi[k]/64)-mins[k])/64)+1 for k in range(3)]
            grid=[];targets=[]
            for ordinal,index in enumerate(indices):
                pos=[mins[0]+ordinal%dims[0]*64,mins[1]+ordinal//dims[0]%dims[1]*64,mins[2]+ordinal//(dims[0]*dims[1])*64]
                if max(abs(pos[0]),abs(pos[1]))<=128 and pos[2] in (64,128,256):
                    grid.append(dict(world=pos));targets.append(lumps[15][index*30:index*30+30])
            g=run_tool(exe,work,name+'-grid',request(path,lumps,casters,grid,case,sources_for(case),domain=1))
            packed=[encode_color(r['directed'],gamma=case.get('gamma',1)) for r in g['rows']]
            record['unpacked_grid_directed_vs_lossy_bsp_max_byte_error']=max(abs(a-b) for row,t in zip(packed,targets) for a,b in zip(row,t[12:15]))
            record['grid_comparison_scope']='Unpacked reduction only; no ordered packing replay. Existing LG_EPSILON4 plus quantization envelope=5.'
            if record['unpacked_grid_directed_vs_lossy_bsp_max_byte_error']>5:record['status']='Failed'
        results[name]=record;print(name,record['status'],record['max_byte_error'],flush=True)
    # A wrong profile cannot pass broad-distance positive observations.
    case=dict(name='inverse_square',linear=True,angle=False);path=base/'inverse_square.bsp';lumps,_,casters=load(path)
    rows=floor_samples(lumps);wrong=run_tool(exe,work,'wrong-linear',request(path,lumps,casters,rows,case,sources_for(case)))
    wrong_rmse=math.sqrt(sum((a-b)**2 for r,t in zip(wrong['rows'],rows) for a,b in zip(r['packed'],t['rgb']))/(3*len(rows)))
    black_rmse=math.sqrt(sum(v*v for r in rows for v in r['rgb'])/(3*len(rows)))
    assert wrong_rmse>5 and black_rmse>5
    same_angle=dict(linear=True,angle=True)
    wrong_falloff=run_tool(exe,work,'wrong-linear-same-angle',request(path,lumps,casters,rows,same_angle,sources_for(same_angle)))
    wrong_falloff_rmse=math.sqrt(sum((a-b)**2 for r,t in zip(wrong_falloff['rows'],rows) for a,b in zip(r['packed'],t['rgb']))/(3*len(rows)))
    assert wrong_falloff_rmse>5
    # Same immutable request, independent fresh process, identical values and
    # support/work counters; elapsed time/RSS are not deterministic quantities.
    repeated=run_tool(exe,work,'repeat-point',request(path,lumps,casters,rows,dict(name='inverse_square'),sources_for(dict(name='inverse_square'))))
    original=json.loads((work/'inverse_square.cpp.json').read_text())
    assert all(original[k]==repeated[k] for k in ('rows','pairs','rays','tests','trace_key'))
    report=dict(schema='org.openjk.bake-cpp-validation.v2',compiler_sha256=frozen['compiler_sha256'] if 'compiler_sha256' in frozen else '894049bd136e7828b0f068d12c932833153b3ada87b99655dac90e4a60c85ad4',
                cases=results,wrong_profile_rmse=wrong_rmse,wrong_attenuation_same_angular_rmse=wrong_falloff_rmse,black_rmse=black_rmse,wall_seconds=time.perf_counter()-started,
                repeated_process_deterministic=True,historical_raven='UnknownProfile',scope='Direct planar super1 kernels only; bounce replay unsupported; generic trace conditional')
    (work/'summary.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    if any(r['status']=='Failed' for r in results.values()):raise AssertionError('Numerical failures; see summary.json')
    return report


def x05(exe,manifest_path,compiler_dir,old_bsp_dir,work):
    """Pass production output through the existing frozen non-vacuous gates."""
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'evaluation/x05'))
    from acceptance import predictions, digest
    from score import score
    manifest=json.loads(manifest_path.read_text());recipes=json.loads((compiler_dir/'recipes.json').read_text())
    packet=dict(schema='org.openjk.forward-predictions.v1',manifest_sha256=digest(manifest_path),cases={})
    for entry in manifest['fixtures']:
        if not entry['required']:continue
        name=entry['id'];case=json.loads(Path(entry['private_case']).read_text());path=compiler_dir/'authoring/base/maps'/(name+'.bsp')
        if name in ('unknown_geometry','unknown_photometry','gi_operator_unavailable'):
            status=json.loads(subprocess.check_output([str(exe),'--status',name],text=True))
            if status['available']:raise AssertionError('Missing capability fabricated output')
            expected={'unknown_geometry':(4,-1),'unknown_photometry':(0,0),'gi_operator_unavailable':(0,5)}
            if (status['code'],status['status'])!=expected[name]:raise AssertionError('Unexpected C++ failure '+str(status))
            packet['cases'][name]=predictions(case['rows'],status='NotComparable' if name=='unknown_photometry' else 'Unknown' if name=='unknown_geometry' else 'Unsupported',reason='Production C++ capability result '+str(status))
            continue
        if name=='correct_encoding':
            from diagnostics import encoding_contract
            inputs=[r['input_relative'] for r in encoding_contract()['records']];path=work/'encoding.inputs';path.write_text(str(len(inputs))+'\n'+'\n'.join(' '.join(map(str,r)) for r in inputs))
            encoded=json.loads(subprocess.check_output([str(exe),'--encode',str(path)],text=True))
            packet['cases'][name]=predictions(case['rows'],encoded['rows']);continue
        settings={};sources=[]
        if name in ('spot_angular_edge','profile_kernel_linear','profile_kernel_no_angle','moving_shadow_boundary'):
            original={'spot_angular_edge':'spot','profile_kernel_linear':'linear','profile_kernel_no_angle':'no_angle','moving_shadow_boundary':'multi_sun'}[name]
            path=old_bsp_dir/(original+'.bsp');settings=next(c for c in CASES if c['name']==original);sources=sources_for(settings)
            if name=='moving_shadow_boundary':
                boundary=json.loads((Path(__file__).resolve().parents[1]/'shadow_footprint/results/boundary.json').read_text())
                positions={r['id']:r for r in boundary['records']}
                queryrows=[dict(world=positions[r['id']]['world'],normal=positions[r['id']]['geometric_normal'],surface=positions[r['id']]['surface']) for r in case['rows']]
            else:queryrows=[dict(world=r['position'],normal=r['normal'],surface=r['surface']) for r in case['rows']]
            lumps,_,casters=load(path,strict_receiver_uv=False)
            result=run_tool(exe,work,name,request(path,lumps,casters,queryrows,settings,sources,strict=name=='moving_shadow_boundary'))
            if not all(r['available'] for r in result['rows']):raise AssertionError('X05 diagnostics missing coverage '+name)
            packet['cases'][name]=predictions(case['rows'],[r['rgb'] for r in result['rows']]);continue
        if sha(path)!=entry['bsp_sha256']:raise ValueError('X05 map fingerprint')
        lumps,_,casters=load(path,strict_receiver_uv=False);rows=[dict(world=r['position'],normal=r['normal'],surface=r['surface']) for r in case['rows']]
        truth=recipes[name];profile=truth['profile'];settings={};sources=[]
        if profile=='area':sources=sources_for(dict(area=True))
        elif profile=='sun':sources=sources_for(dict(sun=True))
        elif profile=='sky17':sources=sources_for(dict(sun=True,skylight=True))
        else:sources=[source(position=s['origin'],rgb=[s['intensity']*x for x in s['rgb']]) for s in truth['sources']]
        result=run_tool(exe,work,name,request(path,lumps,casters,rows,settings,sources))
        if not all(r['available'] for r in result['rows']):raise AssertionError('X05 missing required coverage '+name)
        packet['cases'][name]=predictions(case['rows'],[r['rgb'] for r in result['rows']])
    packet_path=work/'x05-predictions.json';packet_path.write_text(json.dumps(packet,indent=2)+'\n');result=score(manifest_path,packet_path)
    (work/'x05-score.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['status']!='Passed':raise AssertionError('Frozen X05 failed; do not weaken thresholds')
    print('X05:',result['status'],flush=True);return result


def cross_map(exe,root,work):
    """Independent compiled two-colour, oblique spot, sky and thin-wall bakes."""
    results={}
    for name in ('multi_direct','spot_oblique','gamma_clipped','saturated_white','thin_door','sky_roof_v2','single_coincident','split_coincident'):
        truth=json.loads((root/'answers'/(name+'.json')).read_text());path=root/name/'authoring/base/maps'/(name+'.bsp')
        if sha(path)!=truth['bsp_sha256']:raise ValueError('X06 compiler witness changed')
        lumps,_,casters=load(path,strict_receiver_uv=False);rows=floor_samples(lumps)
        sources=[]
        for s in truth['sources']:
            args=dict(position=s['origin'],rgb=[s['intensity']*v for v in s['rgb']])
            if 'target' in s:
                delta=[a-b for a,b in zip(s['target'],s['origin'])];distance=math.sqrt(sum(v*v for v in delta));args.update(kind=1,axis=[v/distance for v in delta])
            item=source(**args)
            if 'target' in s:item.update(target=distance,radius=s['radius'])
            sources.append(item)
        if truth['case'].get('skylight'):sources=sources_for(dict(sun=True,skylight=True))
        result=run_tool(exe,work,name,request(path,lumps,casters,rows,dict(gamma=truth['gamma']),sources))
        unavailable=[(r,t) for r,t in zip(result['rows'],rows) if not r['available']]
        # The predeclared whole-fixture target remains <=1 byte. A failed
        # receiver-placement class is reported as partial unsupported, never
        # relabelled a successful complete fixture or removed from its count.
        if unavailable and (name!='thin_door' or any(r['status']!=5 or t['world'][0]!=0 for r,t in unavailable)):
            raise AssertionError(name+' unexpected missing coverage')
        errors=[abs(a-b) for r,t in zip(result['rows'],rows) if r['available'] for a,b in zip(r['packed'],t['rgb'])]
        record=dict(bsp_sha256=sha(path),samples=len(rows),max_byte_error=max(errors),rmse=math.sqrt(sum(v*v for v in errors)/len(errors)),predeclared_tolerance=1,
                    seconds=result['seconds'],rays=result['rays'],trace_bytes=result['trace_bytes'],trace_key=result['trace_key'])
        record['supported_samples']=len(rows)-len(unavailable);record['unsupported_receiver_placement']=len(unavailable)
        record['status']='PartialUnsupportedReceiverPlacement' if unavailable and max(errors)<=1 else 'Passed' if max(errors)<=1 else 'Failed'
        if unavailable:record['limitation']='Nominal UV positions inside one-unit wall; actual compiler nudge not replayed. Initial all-Available implementation failed 25 samples with max95. No tolerance changed.'
        results[name]=record;print('X06',name,record['status'],max(errors),flush=True)
    (work/'cross-map.json').write_text(json.dumps(results,indent=2,sort_keys=True)+'\n')
    if any(r['status']=='Failed' for r in results.values()):raise AssertionError('Independent compiled fixture failed')
    return results


def physical_comparison(path,work):
    """SI domain only. Never compare these lux values with legacy BSP bytes."""
    from geometry import rectangle
    from integrate import integrated, Source, Scene
    cpp=json.loads(path.read_text());footprint=rectangle(-1,1,-1,1)
    point=integrated(footprint,Source(position=(0,0,4),rgb=(100,100,100)),Scene(),tolerance=1e-8,limit=1000000)
    area=integrated(footprint,Source(kind='rectangle',position=(0,0,4),width=2,height=2),Scene(),source_order=8,tolerance=1e-6,limit=1000000)
    result=dict(schema='org.openjk.bake-physical-comparison.v2',unit='Lux',cpp=cpp,python_point=point,python_rectangle=area,
                point_absolute_difference=abs(cpp['point_integrated_lux']-point['contribution_upper'][0]),
                rectangle_absolute_difference=abs(cpp['finite_rectangle_lux']-area['contribution_upper'][0]),
                rectangle_scope='8x8 midpoint source quadrature vs 8x8 Gauss; observed difference, not a certified total error bound')
    if cpp['failures'] or result['point_absolute_difference']>1e-4:raise AssertionError('Physical reference comparison')
    (work/'physical-comparison.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n');return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--bsp-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--x05-manifest',type=Path);p.add_argument('--x05-compiler-dir',type=Path);p.add_argument('--cross-map-dir',type=Path)
    p.add_argument('--integral-result',type=Path)
    a=p.parse_args();run(a.exe.resolve(),a.bsp_dir.resolve(),a.output.resolve())
    if a.x05_manifest:x05(a.exe.resolve(),a.x05_manifest.resolve(),a.x05_compiler_dir.resolve(),a.bsp_dir.resolve(),a.output.resolve())
    if a.cross_map_dir:cross_map(a.exe.resolve(),a.cross_map_dir.resolve(),a.output.resolve())
    if a.integral_result:physical_comparison(a.integral_result.resolve(),a.output.resolve())
