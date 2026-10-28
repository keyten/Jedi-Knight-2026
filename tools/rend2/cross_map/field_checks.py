#!/usr/bin/env python3
"""Selective NEW angular/doorway/transport stress inputs, existing math only.
GPL-2.0-or-later. Relative synthetic quantities, not inferred JA radiance.
"""
from dataclasses import replace
import argparse
import json
import math
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
for folder in ('shadow_footprint','ddgi_research','rbdoom_research','inverse_gi'):
    sys.path.insert(0,str(HERE.parent/folder))
from integrate import Scene,Source,Node,integrated,sampled
from geometry import Triangle,rectangle
from analytic import rectangle_reference
from probe_weights import exact_weights,average,display_weight
from field_basis import unit,sg,sg_irradiance,project,sphere_grid,directions,evaluate,errors
from transport import Transfer,Edge


def wall(y0=-10.,y1=10.,optical='opaque'):
    vertices=((1.,y0,0.),(1.,y1,0.),(1.,y1,2.),(1.,y0,2.))
    return tuple(Triangle('wall'+str(i),i+1,tuple(vertices[j] for j in ix),((0.,0.),(1.,0.),(0.,1.)),((1.,0.,0.),)*3,optical) for i,ix in enumerate(((0,1,2),(0,2,3))))


def run():
    probes=((0.,0.,1.),(2.,0.,1.));query=(1.5,0.,1.)
    scenes={'sealed':Scene(wall()),'doorway':Scene(wall(y1=-.1)+wall(y0=.1)),
        'unknown_grate':Scene(wall(optical='alpha_unknown')),'no_geometry':Scene(unbounded_unknown=True)}
    spatial={}
    for key,scene in scenes.items():
        weights,unknown=exact_weights(scene,query,probes)
        spatial[key]=dict(weights=weights,unknown=unknown,conditional_supported_value=average([100.,0.],weights),
            field_status='Unknown' if unknown else 'Available' if sum(weights)>0 else 'Unavailable',
            semantics='Support visibility, not occupancy or source localization; unknowns cannot certify complete field interpolation')
    # No admissible support must not become a black field or renormalized leak.
    w,u=exact_weights(Scene(wall()),query,((0.,-.05,1.),(0.,.05,1.)))
    spatial['all_supports_blocked']=dict(weights=w,value=average([100.,100.],w),status='Unavailable' if not sum(w) else 'Available',
        moment_display_value=average([100.,100.],[display_weight(.0001)]*2),unknown=u)
    axes=[(unit((.6,.2,1.)),(1.,.1,.03)),(unit((-.7,.8,.3)),(.02,.1,1.)),(unit((-.3,-.8,.5)),(.1,.9,.05)),(unit((.5,-.1,-1.)),(.8,.1,.4))]
    nodes=sphere_grid(32);held=directions(129);angular=[]
    for sharpness in (8.,48.):
        radiance=lambda d:tuple(.01+sum(rgb[c]*sg(d,a,sharpness) for a,rgb in axes) for c in range(3))
        coeff=project([(d,w,radiance(d)) for d,w in nodes],4)
        truthE=[tuple(math.pi*.01+sum(rgb[c]*sg_irradiance(n,a,sharpness) for a,rgb in axes) for c in range(3)) for n in held]
        truthL=[radiance(d) for d in held]
        for L in (2,4):
            cs=coeff[:(L+1)**2]
            angular.append(dict(sharpness=sharpness,degree=L,bytes_fp32=3*4*len(cs),
                incoming_radiance=errors(truthL,[evaluate(cs,d,L) for d in held]),
                diffuse_irradiance=errors(truthE,[evaluate(cs,d,L,True) for d in held]),
                domain='Synthetic relative-linear; convolution E, not E/pi',source_inference=False))
    transport={}
    for key,opening in [('sealed',0.),('doorway',.2),('unknown_opening',None)]:
        edges=[Edge(0,1,.25,.25),Edge(1,0,.25,.25)]
        if opening is not None and opening:edges+=[Edge(1,2,opening,opening),Edge(2,1,opening,opening)]
        if opening is None:edges+=[Edge(1,2,0.,.2),Edge(2,1,0.,.2)]
        transfer=Transfer((1.,1.,1.),tuple(edges),'x06-'+key);E=[[3.,1.,.2],[0.,0.,0.],[0.,0.,0.]]
        rho=[[.7,.3,.1]]*3;one=transfer.apply(E,rho)
        # Separate intervals propagated conservatively from each previous order.
        low=transfer.apply(one[0],rho);high=transfer.apply(one[1],rho)
        transport[key]=dict(first_bounce=one,second_bounce_lower=low[0],second_bounce_upper=high[1],
            no_local_emitter_in_bright_room=True,source_linked=True)
    shadows=[]
    for x in (-.15,-.05,.05):
        reference=rectangle_reference((1.3,1.4,-.05,.05),(x,0.,4.));h=1e-5
        fd=(rectangle_reference((1.3,1.4,-.05,.05),(x+h,0.,4.))['value']-rectangle_reference((1.3,1.4,-.05,.05),(x-h,0.,4.))['value'])/(2*h)
        shadows.append(dict(source_x=x,value=reference['value'],derivative=reference['jacobian'][0],central_difference=fd,
            relative_error=abs(fd-reference['jacobian'][0])/max(abs(fd),1e-15)))
    return dict(schema='org.openjk.x06-field-checks.v1',probe_cases=spatial,angular=angular,passive_transport=transport,shadow_signs=shadows,
        limits='Small new synthetic inputs to existing reference routines; no stock inferred field or general q3map bounce replay')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=run();a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print('field/probe/shadow checks generated')
