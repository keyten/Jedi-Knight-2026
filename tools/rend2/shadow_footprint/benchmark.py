#!/usr/bin/env python3
"""Value/gradient/cost benchmark, with independent analytic targets where available.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import replace
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import time
from geometry import rectangle,patch_mesh,footprint
from integrate import integrated,sampled,Scene,Source,Node,source_nodes,response,halton
from adaptive import adaptive
from analytic import rectangle_reference,finite_rectangle_visibility,frozen_visibility_derivative
from fixtures import default,wall,doorway,tilted
from localize import experiment
from analytic import spot_orientation_ad


def write(path,value): path.write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def shifted(s,k,d):
    p=list(s.position); p[k]+=d; return replace(s,position=tuple(p))
def fd(function,source,k,h): return (function(shifted(source,k,h))-function(shifted(source,k,-h)))/(2*h)


def values():
    fp,s,scene=default(); exact=rectangle_reference((1.3,1.4,-.05,.05),s.position)
    output=[]; timing=[]
    methods=[('centre',lambda:sampled(fp,s,scene,n=1,method='centre'))]
    for method in ('grid','stratified','halton'):
        for n in (2,4,8,16,32): methods.append((f'{method}{n}',lambda m=method,n=n:sampled(fp,s,scene,n=n,method=m)))
    methods += [('adaptive',lambda:adaptive(fp,s,scene,tolerance=1e-4,max_depth=6)),
                ('boundary',lambda:integrated(fp,s,scene,tolerance=1e-11))]
    for name,call in methods:
        start=time.perf_counter(); result=call(); duration=time.perf_counter()-start
        output.append(dict(method=name,value=result['contribution_upper'][0],visibility=result['visibility_upper'],
            lux_error=result['contribution_upper'][0]-exact['value'],visibility_error=result['visibility_upper']-exact['visibility'],
            # Declared analytic fixture exposure, not a q3map photometric calibration.
            rgb8_error_under_fixed_4000_bytes_per_lux=4000*(result['contribution_upper'][0]-exact['value']),
            work=result['work'],numerical_error_estimate=result.get('numerical_error_estimate'),
            reference=exact['value']))
        timing.append(dict(method=name,seconds=duration))
    return output,timing


def derivatives():
    fp,s,scene=default(); bounds=(1.3,1.4,-.05,.05); result=[]
    for case,fp,bounds,scene in [('edge',fp,bounds,scene),
        ('clear',rectangle(1.3,1.4,-.05,.05),bounds,Scene()),
        ('blocked',rectangle(1.1,1.2,-.05,.05),(1.1,1.2,-.05,.05),scene)]:
        ref=rectangle_reference(bounds,s.position,wall=None if case=='clear' else (1.,1.))
        frozen=frozen_visibility_derivative(bounds,s.position) if case=='edge' else None
        for axis in range(3):
            for step in (.1,.01,.001,.0001,.00001):
                call=lambda source:integrated(fp,source,scene,tolerance=1e-11)['contribution_upper'][0]
                point=lambda source:sampled(fp,source,scene,n=5)['contribution_upper'][0]
                fresh_plus=sampled(fp,shifted(s,axis,step),scene,n=8,method='stratified',seed=17)['contribution_upper'][0]
                fresh_minus=sampled(fp,shifted(s,axis,-step),scene,n=8,method='stratified',seed=18)['contribution_upper'][0]
                value=fd(call,s,axis,step); sampled_derivative=fd(point,s,axis,step)
                result.append(dict(case=case,axis='xyz'[axis],step=step,analytic=ref['jacobian'][axis],
                    boundary_fd=value,error=value-ref['jacobian'][axis],fixed5_fd=sampled_derivative,
                    recomputed_samples_fd=(fresh_plus-fresh_minus)/(2*step),
                    frozen_visibility_ad=None if frozen is None else frozen[axis]))
    return result


def edge_location(method,n=4):
    s=Source(); scene=Scene(wall()); width=.1; lo,hi=1.2,1.5
    def value(x):
        fp=rectangle(x-width/2,x+width/2,-.05,.05)
        return integrated(fp,s,scene)['visibility_upper'] if method=='boundary' else sampled(fp,s,scene,n=n)['visibility_upper']
    locations=[]
    for threshold in (.1,.5,.9):
        a,b=lo,hi
        for _ in range(28):
            c=(a+b)/2
            if value(c)<threshold: a=c
            else: b=c
        locations.append((a+b)/2)
    return dict(method=method,n=n,edge_50=locations[1],edge_error=locations[1]-4/3,
                transition_10_90=locations[2]-locations[0],true_footprint_transition=.08,
                physical_point_penumbra=0.,locations=locations)


def finite_sources():
    fp,s,scene=default(); records=[]; gradient=[]
    for kind in ('rectangle','sun','spot'):
        source=replace(s,kind=kind,angular_radius=.03)
        if kind=='sun': source=tilted(source,-math.degrees(math.atan(1/3)))
        if kind=='spot': source=tilted(source,8.)
        for order in (2,4,8,16):
            r=integrated(fp,source,scene,source_order=order,tolerance=2e-7,limit=2000000,max_depth=3)
            records.append(dict(kind=kind,source_order=order,value=r['contribution_upper'][0],visibility=r['visibility_upper'],
                exact_visibility=finite_rectangle_visibility((1.3,1.4,-.05,.05),source.position,source.width) if kind=='rectangle' else None,
                work=r['work'],smooth_error_estimate=r['numerical_error_estimate']))
        # Extent and angle derivatives of the actual integration, compared across
        # emitter orders/steps. Their agreement is evidence, not analytic proof.
        parameter='width' if kind=='rectangle' else 'angular_radius' if kind=='sun' else 'orientation_radians'
        for order in (4,8,16):
            for h in (.01,.001,.0001):
                def change(delta):
                    return tilted(source,math.degrees(delta)) if kind=='spot' else replace(source,**{parameter:getattr(source,parameter)+delta})
                a=integrated(fp,change(h),scene,source_order=order,tolerance=2e-7,limit=2000000,max_depth=3)
                b=integrated(fp,change(-h),scene,source_order=order,tolerance=2e-7,limit=2000000,max_depth=3)
                gradient.append(dict(kind=kind,parameter=parameter,step=h,source_order=order,
                    contribution_fd=(a['contribution_upper'][0]-b['contribution_upper'][0])/(2*h),
                    visibility_fd=(a['visibility_upper']-b['visibility_upper'])/(2*h),
                    independent_exact_reference=None if kind!='rectangle' else (
                        finite_rectangle_visibility((1.3,1.4,-.05,.05),source.position,source.width+h)-
                        finite_rectangle_visibility((1.3,1.4,-.05,.05),source.position,source.width-h))/(2*h)))
    # Analytic geometric support widths (no luxel filter or receiver footprint).
    support=[dict(caster_height=h,rectangle_support=.6*h/(4-h),sun_support=2*h*math.tan(.03)) for h in (.5,1.,2.,3.)]
    # Spatial edge profiles with a narrower receiver footprint, including source
    # radiance/cosine/falloff weighting. Quantified rather than perfect edges only.
    profiles=[]
    for x in (1.15,1.22,1.28,1.33,1.38,1.44,1.52):
        r=integrated(rectangle(x-.01,x+.01,-.01,.01),replace(s,kind='rectangle'),scene,source_order=16,tolerance=1e-6,limit=500000)
        profiles.append(dict(x=x,visibility=r['visibility_upper'],lux=r['contribution_upper'][0]))
    # Independent joint 4D quasi-Monte Carlo integral (actual point rays), no
    # projected polygons or source Gauss rule reused. Error remains sampled.
    from geometry import add,mul
    joint=[]
    source=replace(s,kind='rectangle'); count=16384
    value=visibility=0.
    for i in range(1,count+1):
        p=(1.3+.1*halton(i,2),-.05+.1*halton(i,3),0.)
        pos=add(source.position,add(mul(source.right,(halton(i,5)-.5)*source.width),mul(source.up,(halton(i,7)-.5)*source.height)))
        node=Node(pos,(),mul(source.rgb,source.width*source.height),source.axis)
        lo,hi=scene.query(p,node)
        value+=response(p,(0.,0.,1.),node)[0]*lo/count; visibility+=lo/count
    return dict(convergence=records,derivatives=gradient,support=support,rectangle_edge_profile=profiles,
        rectangle_independent_4d_qmc=dict(samples=count,rays=count,value=value,visibility=visibility,
            numerical_error='Uncertified; cross-check analytic visibility and independent source orders'))


def uncertainty_cases():
    fp,s,scene=default(); result={}
    for kind in ('opaque','alpha_unknown','translucent','moving','unsupported'):
        result[kind]=integrated(fp,s,Scene(wall(optical=kind)))
    result['missing_unbounded']=integrated(fp,s,Scene(unbounded_unknown=True))
    result['known_wall_plus_missing']=integrated(fp,s,Scene(wall(height=3.),unbounded_unknown=True))
    result['narrow_opening']=integrated(rectangle(1.8,2.2,-.2,.2),s,Scene(doorway(.03)))
    # Source below top, behind a wall: projection crossing source height is
    # deliberately unsupported; ray sampling still returns actual blocked hits.
    low=replace(s,position=(0.,0.,.75))
    result['source_below_caster_reference']=integrated(fp,low,scene)
    result['source_below_caster_independent_ray']=sampled(fp,low,scene,n=8)
    for n in (2,4,8):
        controls=tuple((x/2+1.3,y/20-.05,.05 if x==y==1 else 0.) for y in range(3) for x in range(3))
        triangles,envelope=patch_mesh(controls,n)
        result['patch'+str(n)]=integrated(replace(footprint('curved',triangles,((0.,0.),(1.,1.))),geometry_envelope=envelope),s,scene,tolerance=1e-6,limit=500000)
    # Near-tangent independent segment test: actual hit, miss, and coplanar uncertainty.
    from integrate import Node
    result['near_tangent']=[dict(height=z,interval=Scene(wall()).query((1.35,0.,0.),Node((0.,0.,z),(),(1.,1.,1.)))) for z in (3.8571428,3.857142857142857,3.857143)]
    return result


def orientation_check():
    point=(1.35,0.,0.); source=Source(kind='spot'); theta=math.radians(8.)
    value,derivative=spot_orientation_ad(source.position,point,theta)
    result=[]
    for h in (.01,.001,.0001,.00001):
        a=response(point,(0.,0.,1.),source_nodes(tilted(source,math.degrees(theta+h)),1)[0])[0]
        b=response(point,(0.,0.,1.),source_nodes(tilted(source,math.degrees(theta-h)),1)[0])[0]
        result.append(dict(step=h,value=value,ad=derivative,fd=(a-b)/(2*h),error=(a-b)/(2*h)-derivative))
    return result


def multiple_sources():
    fp,source,scene=default(); bounds=(1.3,1.4,-.05,.05)
    sources=(source,replace(source,id='second',position=(.1,.02,4.2),rgb=(2.,2.,2.)))
    actual=sum(integrated(fp,s,scene,tolerance=1e-10)['contribution_upper'][0] for s in sources)
    refs=[rectangle_reference(bounds,s.position,intensity=s.rgb[0]) for s in sources]
    rows=[]
    for k in range(3):
        h=.0001
        a=sum(integrated(fp,shifted(s,k,h),scene,tolerance=1e-10)['contribution_upper'][0] for s in sources)
        b=sum(integrated(fp,shifted(s,k,-h),scene,tolerance=1e-10)['contribution_upper'][0] for s in sources)
        exact=sum(r['jacobian'][k] for r in refs)
        rows.append(dict(axis='xyz'[k],reference=exact,fd=(a-b)/(2*h),error=(a-b)/(2*h)-exact))
    return dict(value=actual,reference=sum(r['value'] for r in refs),derivatives=rows,
                convention='Both independent sources translated by the same differential; intensities fixed')


def peak_rss():
    try:
        import os
        if os.name=='nt':
            import ctypes
            from ctypes import wintypes
            class Counters(ctypes.Structure):
                _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(n,ctypes.c_size_t) for n in
                    ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage',
                     'QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage')]
            counter=Counters(); counter.cb=ctypes.sizeof(counter)
            kernel=ctypes.WinDLL('kernel32'); kernel.GetCurrentProcess.restype=wintypes.HANDLE
            memory=ctypes.WinDLL('psapi').GetProcessMemoryInfo
            memory.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
            memory.restype=wintypes.BOOL
            if not memory(kernel.GetCurrentProcess(),ctypes.byref(counter),counter.cb): return None
            return counter.PeakWorkingSetSize
        import resource
        import sys
        return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*(1 if sys.platform=='darwin' else 1024)
    except (ImportError,OSError,AttributeError): return None


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    starting={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob('*.py')}
    start=time.perf_counter(); value,timing=values(); print('value methods complete',flush=True)
    derivative=derivatives(); print('XYZ derivative checks complete',flush=True)
    finite=finite_sources(); print('finite sources complete',flush=True)
    uncertain=uncertainty_cases()
    result=dict(schema='org.openjk.shadow-footprint-benchmark.v1',units='metres/candela/nits/lux; analytic fixtures ONLY',
        values=value,derivatives=derivative,finite=finite,uncertainty=uncertain,
        edge_locations=[edge_location('boundary')]+[edge_location('grid',n) for n in (1,2,4,8,16)],
        localization=experiment(),smooth_orientation_ad=orientation_check(),multiple_sources=multiple_sources(),
        source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob('*.py')},
        claims=dict(compiler_replay='separate',historical_raven='Unknown',solver=False,runtime=False))
    if starting!=result['source_sha256']: raise RuntimeError('Research sources changed during the run; result not published')
    write(args.output/'result.json',result)
    write(args.output/'timing.json',dict(methods=timing,total_seconds=time.perf_counter()-start,peak_process_rss_bytes=peak_rss(),environment='Python standard-library offline reference; not native BVH throughput'))
    for name,rows in (('methods',value),('derivatives',derivative),('finite_derivatives',finite['derivatives'])):
        with (args.output/(name+'.csv')).open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    print('benchmark complete',flush=True)


if __name__=='__main__': main()
