#!/usr/bin/env python3
"""Independent scalar moment/interpolation counterexamples; NOT an RTXGI port.

Uses X04's tiny CPU geometry reference. No graphics APIs, shader compilation,
SDK code, historical-observation mutation or physical emitter inference.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'shadow_footprint'))
from geometry import dot, sub, length, unit, cross, ray_triangle
from fixtures import wall
from integrate import Scene, Node


def finite(value):
    if not math.isfinite(value): raise ValueError('Nonfinite value')
    return value


@dataclass(frozen=True)
class Moments:
    mean: float
    second: float

    def __post_init__(self):
        finite(self.mean); finite(self.second)
        if self.mean<0 or self.second<self.mean*self.mean-1e-12:
            raise ValueError('Invalid distance moments; do not create variance with abs()')

    @property
    def variance(self): return max(0.,self.second-self.mean*self.mean)


def moments(distances,weights=None):
    if not 1<=len(distances)<=4096: raise ValueError('Moment sample budget')
    weights=list(weights) if weights is not None else [1.]*len(distances)
    if len(weights)!=len(distances) or any(finite(v)<0 for v in (*weights,*distances)) or sum(weights)<=0:
        raise ValueError('Moment sample span/range')
    total=sum(weights)
    return Moments(sum(w*d for w,d in zip(weights,distances))/total,
                   sum(w*d*d for w,d in zip(weights,distances))/total)


def cantelli_upper(distance,m):
    """Upper bound for the corresponding angular distance RANDOM VARIABLE,
    not transmittance of a selected deterministic geometric segment.
    """
    if finite(distance)<0: raise ValueError('Negative query distance')
    if distance<=m.mean: return 1.
    return m.variance/(m.variance+(distance-m.mean)**2)


def display_weight(bound,normal_dot=0.,trilinear=.5):
    """Mathematical scalar reduction of published display policy. No octahedral
    lookup, codec, temporal accumulation, volume indexing or SDK implementation.
    """
    if not 0<=finite(bound)<=1 or not -1<=finite(normal_dot)<=1 or not 0<=finite(trilinear)<=1:
        raise ValueError('Weight domain')
    wrapped=(.5*(normal_dot+1))**2+.2
    uncrushed=max(1e-6,wrapped*max(.05,bound**3))
    return (uncrushed if uncrushed>=.2 else uncrushed**3/.2**2)*trilinear


def average(values,weights,perceptual=False):
    if len(values)!=len(weights) or not values: raise ValueError('Interpolation span')
    if any(finite(v)<0 for v in (*values,*weights)): raise ValueError('Negative energy/weight')
    total=sum(weights)
    if total==0: return None # unavailable, not a black field
    if perceptual: return (sum(w*math.sqrt(v) for w,v in zip(weights,values))/total)**2
    return sum(w*v for w,v in zip(weights,values))/total


def exact_weights(scene,query,probes):
    weights=[]; unknown=False
    for p in probes:
        low,high=scene.query(query,Node(p,(),(1.,1.,1.)))
        weights.append(1. if low==high==1. else 0.)
        unknown |= low!=high
    return weights,unknown


def directional_samples(scene,probe,direction,spacing=(1.,1.,1.),exponent=50.):
    """A small angular-bin fixture, not the SDK's full sphere/Fibonacci update.
    All rays here hit known geometry. An unknown blocker aborts moment creation.
    Misses are not quietly changed to sky or a certified hit distance.
    """
    if scene.unbounded_unknown or any(t.optical!='opaque' for t in scene.triangles):
        raise ValueError('Unresolved optical geometry; moments unavailable')
    base=math.atan2(direction[1],direction[0]); distances=[]; weights=[]
    limit=1.5*length(spacing); uncapped=[]
    for angle in (-.6,-.45,-.3,-.15,0.,.15,.3,.45,.6):
        d=(math.cos(base+angle),math.sin(base+angle),0.)
        hits=[hit[0] for t in scene.triangles if (hit:=ray_triangle(probe,d,t,minimum=1e-9))]
        if not hits: raise ValueError('Fixture ray miss; no implicit sky or unknown-distance fill')
        hit=min(hits); uncapped.append(hit); distances.append(min(hit,limit)); weights.append(math.cos(angle)**exponent)
    return moments(distances,weights),dict(rays=len(distances),capped=sum(a>b for a,b in zip(uncapped,distances)),
        distances=distances,weights=weights,cap=limit)


def field_case(scene,query,probes,values,name):
    metric=[]; detail=[]
    for probe in probes:
        m,trace=directional_samples(scene,probe,unit(sub(query,probe)))
        r=length(sub(query,probe)); bound=cantelli_upper(r,m)
        metric.append(display_weight(bound)); detail.append(dict(mean=m.mean,second=m.second,
            variance=m.variance,query_distance=r,angular_upper=bound,display_weight=metric[-1],trace=trace))
    exact,unknown=exact_weights(scene,query,probes)
    supported=average(values,exact)
    return dict(name=name,query=query,probes=probes,probe_values=values,moments=detail,
        naive_linear=average(values,[1.]*len(values)),
        moment_linear=average(values,metric),moment_perceptual=average(values,metric,True),
        exact_geometry_supported=supported,exact_unknown=unknown,
        exact_status='Unavailable' if supported is None else 'SupportedInterpolationFixture',
        note='Values are declared synthetic field samples, not recovered emitters or a source-fitting visibility API')


def leakage():
    # Camera-independent known fixture in metres, opaque two-sided test triangles.
    # Closed walls are large enough for every deterministic cone ray used here.
    sealed=Scene(wall(x=0.,height=3.,y0=-5.,y1=5.)+
                 wall(x=-3.,height=3.,y0=-5.,y1=5.)+wall(x=3.,height=3.,y0=-5.,y1=5.))
    finite_partition=Scene(wall(x=0.,height=3.,y0=-.1,y1=.1)+
                           wall(x=-3.,height=3.,y0=-5.,y1=5.)+wall(x=3.,height=3.,y0=-5.,y1=5.))
    query=(.4,0.,1.)
    return [field_case(sealed,query,((-.5,0.,1.),(.5,0.,1.)),(100.,0.),'sealed_bright_dark'),
            field_case(sealed,query,((-.5,-.1,1.),(-.5,.1,1.)),(100.,100.),'all_probes_behind_wall'),
            field_case(finite_partition,query,((-.5,0.,1.),(.5,0.,1.)),(100.,0.),'thin_finite_partition')]


def equal_moments():
    a=((1.,3.),(.5,.5)); b=((1.5,4.),(.8,.2)); ma=moments(*a); mb=moments(*b)
    tests=[]
    for query in (1.8,2.5,2.9):
        actual=[sum(w for d,w in zip(ds,ws) if d>=query) for ds,ws in (a,b)]
        bound=cantelli_upper(query,ma)
        tests.append(dict(distance=query,true_angular_survival=actual,upper=bound,cubed=bound**3))
    return dict(distributions=[dict(distances=ds,weights=ws) for ds,ws in (a,b)],
        moments=[dict(mean=m.mean,second=m.second) for m in (ma,mb)],queries=tests,
        note='Identical first two moments do not identify the CDF, much less one geometric ray')


def unknown_and_dynamic():
    q=(.5,0.,1.); p=(-.5,0.,1.)
    door=Scene(wall(x=0,height=3,y0=-5,y1=-.2)+wall(x=0,height=3,y0=.2,y1=5))
    closed=Scene(wall(x=0,height=3,y0=-5,y1=5))
    alpha=Scene(wall(x=0,height=3,y0=-5,y1=5,optical='alpha_unknown'))
    missing=Scene((),unbounded_unknown=True)
    result={}
    for name,scene in (('door_open',door),('door_closed',closed),('unknown_alpha',alpha),('missing_geometry',missing)):
        result[name]=dict(segment=scene.query(q,Node(p,(),(1.,1.,1.))))
    result['stale_open_moments_on_closed_door']=dict(old_mean=5.,query_distance=1.,
        angular_weight=cantelli_upper(1.,Moments(5.,25.)),exact_closed=(0.,0.))
    result['missing_sky_boundary']=Scene((),require_sky=True).query((0.,0.,0.),Node((),(0.,0.,1.),(1.,1.,1.)))
    result['explicit_sky']=Scene(wall(x=1.,height=3.,y0=-5.,y1=5.,optical='sky'),require_sky=True).query(
        (0.,0.,1.),Node((),(1.,0.,0.),(1.,1.,1.)))
    return result


def storage(irradiance=6,distance=14):
    # Proposed illustrative CPU record; not a required JA schema or SDK default.
    cpu=irradiance**2*3*4+distance**2*2*4+64
    with_borders=(irradiance+2)**2*4+(distance+2)**2*4+8
    return dict(angular_interior_sizes=[irradiance,distance],
        cpu_fp32_rgb_moments_metadata_bytes_per_probe=cpu,
        illustrative_gpu_like_r10_rg16_rgba16_border_bytes_per_probe=with_borders,
        ray_scratch_fp32x2_256_per_probe=256*8,
        probes10000_cpu_bytes=cpu*10000,probes100000_cpu_bytes=cpu*100000,
        note='Excludes BVH, sparse indices, extra validity masks, variability and allocator overhead; persistent ray buffers are optional')


def placement():
    # Independent analytic zero-thickness sheet: points with x!=0 are not inside
    # a solid. A two-sided sheet nevertheless has a back side in mesh winding.
    probe=(-.5,0.,500.); sheet=wall(x=0.,height=1000.,y0=-100.,y1=100.)
    rays=32; backfaces=0; hits=0
    for i in range(rays):
        z=1-2*(i+.5)/rays; radius=math.sqrt(1-z*z); angle=i*math.pi*(3-math.sqrt(5))
        direction=(radius*math.cos(angle),radius*math.sin(angle),z)
        candidates=[(hit[0],t) for t in sheet if (hit:=ray_triangle(probe,direction,t,minimum=1e-9))]
        if not candidates: continue
        _,triangle=min(candidates,key=lambda v:v[0]); hits+=1
        n=cross(sub(triangle.world[1],triangle.world[0]),sub(triangle.world[2],triangle.world[0]))
        backfaces+=dot(n,direction)>0
    return dict(probe=probe,fixed_backface_hits=backfaces,total_hits=hits,fixed_rays=rays,threshold=.25,
        heuristic_inactive=backfaces/rays>.25,analytic_sheet_point_free=probe[0]!=0.,
        no_nearby_hits='Inactive for update relevance does not mean Solid or unlit',
        historical_observation_relocation='Forbidden: retain sample position/RGB; create a separate field support point and reevaluate',
        relocation_counterexample=dict(old_position=0.,new_position=.2,source_position=1.,
            old_field_lux=1.,correct_new_field_lux=1/.8**2,stale_value_relative_error=.36))


def run():
    result=dict(schema='org.openjk.ddgi-research',version=1,reference_commit='f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6',
        units='metres for synthetic geometry/distance; synthetic field values labelled lux for interpolation only',
        leakage=leakage(),equal_moments=equal_moments(),unknown_dynamic=unknown_and_dynamic(),memory=storage(),
        codec=dict(constant_radiance=2.,stored_unencoded=1.,restored_irradiance=2*math.pi,
            linear_mix_0_100=average((0.,100.),(.5,.5)),perceptual_mix_0_100=average((0.,100.),(.5,.5),True)),
        placement=placement(),
        source_ambiguity=dict(distance_metres=[1.,2.],intensity_cd=[1.,4.],
            normal_incidence_lux=[1/1**2,4/2**2],
            note='At one probe these point configurations have identical directional irradiance for every normal'))
    result['source_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--output',type=Path,default=HERE/'results.json')
    args=p.parse_args(); result=run(); args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')
    for case in result['leakage']:
        print(case['name'],'naive',case['naive_linear'],'moment-linear',case['moment_linear'],
              'moment-perceptual',case['moment_perceptual'],'geometry',case['exact_geometry_supported'],case['exact_status'])


if __name__=='__main__': main()
