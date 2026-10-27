#!/usr/bin/env python3
"""Deterministic attribution counterexamples, not light fitting for JA.

Reuses X04's tiny independent triangle query. No third-party packages, GL,
neural model, stock assets or production geometry changes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'shadow_footprint'))
from integrate import Scene, Node, gauss
from fixtures import quad, wall
from transport import Transfer, Edge, dot, sub, length, direct, nnls, fit_smooth, rmse


def write_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')


def network(profile='inverse_square',rho=(.7,.18,.08)):
    """One small diffuse reflector and black receivers; a passive first-bounce
    network. Small-patch midpoint form factors, NOT exact scene radiosity.
    Metres, cd, lux; fixed neutral source I=3 cd per RGB channel.
    """
    source=(-1.,-.6,.8); reflector=(0.,0.,1.6)
    positions=[reflector]; normals=[(0.,0.,-1.)]; areas=[.25]
    split=['unused']; surfaces=['reflector']
    for y in (-.9,-.6,-.3,0.,.3,.6,.9):
        for x in (-1.2,-.8,-.4,0.,.4,.8,1.2):
            positions.append((x,y,0.)); normals.append((0.,0.,1.)); areas.append(.04)
            split.append('train' if y<=-.3 else 'heldout' if y>=.3 else 'gap'); surfaces.append('floor')
        for z in (.3,.6,.9,1.2):
            positions.append((1.8,y,z)); normals.append((-1.,0.,0.)); areas.append(.04)
            split.append('train' if y<=-.3 else 'heldout' if y>=.3 else 'gap'); surfaces.append('wall')
    edges=[]
    for i,p in enumerate(positions[1:],1):
        delta=sub(p,reflector); r=length(delta); u=tuple(v/r for v in delta)
        kernel=max(0.,dot(normals[0],u))*max(0.,-dot(normals[i],u))/(math.pi*r*r)
        # Reciprocal, nonnegative; unmatched outgoing power escapes this model.
        edges += [Edge(0,i,areas[i]*kernel,areas[i]*kernel),Edge(i,0,areas[0]*kernel,areas[0]*kernel)]
    transfer=Transfer(tuple(areas),tuple(edges),'small-patch-v1')
    D=[[direct(p,n,source,profile)]*3 for p,n in zip(positions,normals)]
    reflectance=[list(rho)]+[[0.,0.,0.] for _ in positions[1:]]
    G=transfer.apply(D,reflectance)[0]
    return dict(source=source,positions=positions,normals=normals,areas=areas,
                split=split,surfaces=surfaces,D=D,G=G,transfer=transfer,reflectance=reflectance)


def indices(data,partition): return [i for i,s in enumerate(data['split']) if s==partition]
def flat(rows,selection): return [v for i in selection for v in rows[i]]
def combine(a,b,gain=1.): return [[gain*(x+y) for x,y in zip(ar,br)] for ar,br in zip(a,b)]


def constrained(data,target,selection,profile='inverse_square',rho=(.7,.18,.08)):
    candidate=network(profile,rho); column=combine(candidate['D'],candidate['G'])
    intensity=nnls([flat(column,selection)],flat(target,selection))[0]
    prediction=[[intensity*x for x in row] for row in column]
    return intensity,prediction,candidate


def stability():
    true=network(); train=indices(true,'train'); held=indices(true,'heldout')
    results=[]
    for bounce in (False,True):
        zero=[[0.,0.,0.] for _ in true['G']]
        target=combine(true['D'],true['G'] if bounce else zero,3.)
        for profile in ('inverse_square','linear_alternative'):
            for label,rho in (('known',(.7,.18,.08)),('wrong_half',(.35,.09,.04)),('unknown_grey',(.4,.4,.4))):
                intensity,pred,candidate=constrained(true,target,train,profile,rho if bounce else (0.,0.,0.))
                # Deliberately unsafe attribution competitor: independent nonnegative
                # GI gain. Same two amplitudes for each profile, NOT physical proof.
                beta=nnls([flat(candidate['D'],train),flat(candidate['G'],train)],flat(target,train))
                loose=combine([[beta[0]*v for v in r] for r in candidate['D']],
                              [[beta[1]*v for v in r] for r in candidate['G']])
                results.append(dict(bounce=bounce,profile=profile,material=label,
                    fitted_cd_per_channel=intensity,
                    predicted_gi_mean_lux=sum(flat(candidate['G'],held))*intensity/(3*len(held)),
                    train_rmse_lux=rmse(flat(pred,train),flat(target,train)),
                    heldout_rmse_lux=rmse(flat(pred,held),flat(target,held)),
                    unsafe_independent_direct_gain=beta[0],unsafe_independent_gi_gain=beta[1],
                    unsafe_heldout_rmse_lux=rmse(flat(loose,held),flat(target,held))))
    # A zero-bounce scene: allocating an independent bounce basis can explain
    # attenuation-model discrepancy. The truth here contains NO reflected energy.
    target=combine(true['D'],[[0.,0.,0.] for _ in true['G']],3.)
    candidate=network('linear_alternative')
    beta=nnls([flat(candidate['D'],train),flat(candidate['G'],train)],flat(target,train))
    freeI,freeResidual=fit_smooth(flat(candidate['D'],train),flat(target,train),
        [float(i) for i in range(3*len(train))],source_penalty=.02,smoothness=0.)
    false_gi=dict(truth_bounce_lux=0.,wrong_profile_direct_gain=beta[0],
        wrong_profile_independent_gi_gain=beta[1],
        free_residual_direct_gain=freeI,free_residual_mean_lux=sum(freeResidual)/len(freeResidual),
        note='Here even the loose transport basis rejects positive GI; a free residual can still swallow all zero-bounce light')
    return dict(rows=results,zero_bounce_false_attribution=false_gi,
                train_samples=len(train),heldout_samples=len(held),gap_samples=len(indices(true,'gap')),
                separation_metres=.6,shared_rgb_gain=True,
                profile_caveat='linear_alternative is an idealized confounder, NOT a q3map2 formula; see compiler_controls for pinned kernels')


def emitter_suppression():
    data=network(); train=indices(data,'train'); held=indices(data,'heldout')
    target=combine(data['D'],data['G'],3.)
    # Red channel: fixed physically authored white source and red reflecting patch.
    D=[r[0] for r in data['D']]; y=[r[0] for r in target]
    tD=[D[i] for i in train]; ty=[y[i] for i in train]
    # Smoothness neighbours live on the receiver, never across floor/wall corners.
    # An X-only field deliberately ignores the spatial held-out Y direction.
    x=[data['positions'][i][0]+(5 if data['surfaces'][i]=='wall' else 0) for i in train]
    rows=[]
    for label,penalty,smooth,magnitude in (
        ('free_per_sample',.02,0.,0.),('smooth_plus_sparse_sources',.02,.01,0.),
        ('smooth_no_source_penalty',0.,.01,0.),('residual_energy_penalty',.02,.01,.3)):
        intensity,z=fit_smooth(tD,ty,x,penalty,smooth,magnitude)
        def predicted(i):
            # Declared nearest training site interpolation; no held-out RGB read.
            j=min(range(len(train)),key=lambda j:sum((a-b)**2 for a,b in zip(data['positions'][train[j]],data['positions'][i])))
            return intensity*D[i]+z[j]
        rows.append(dict(model=label,cd=intensity,source_recalled=intensity>=1.5,
            train_rmse_lux=rmse([intensity*d+r for d,r in zip(tD,z)],ty),
            heldout_rmse_lux=rmse([predicted(i) for i in held],[y[i] for i in held]),
            parameters=1+len(train),physical_gi=False))
    for label,column in (('constant_unresolved',[1.]*len(D)),('transport_energy_coupled',None)):
        if column:
            values=nnls([tD,[1.]*len(train)],ty,l1=(.02,0.))
            pred=[values[0]*d+values[1] for d in D]; intensity=values[0]
        else:
            col=[d+g[0] for d,g in zip(D,data['G'])]
            intensity=nnls([[col[i] for i in train]],ty)[0]; pred=[intensity*v for v in col]
        rows.append(dict(model=label,cd=intensity,source_recalled=intensity>=1.5,
            train_rmse_lux=rmse([pred[i] for i in train],ty),
            heldout_rmse_lux=rmse([pred[i] for i in held],[y[i] for i in held]),
            parameters=2 if column else 1,physical_gi=not bool(column)))
    # Even held-out cannot separate a perfectly correlated residual and emitter.
    duplicate=nnls([tD,tD],ty,l1=(.02,0.))
    priors=[]
    other=[direct(p,n,(-.85,-.6,.8)) for p,n in zip(data['positions'],data['normals'])]
    for penalty in (0.,.05,.5):
        c1=[D[i]+data['G'][i][0] for i in train]
        fit=nnls([c1,[other[i] for i in train]],ty,l1=(penalty,penalty),passes=2000)
        prediction=[fit[0]*(D[i]+data['G'][i][0])+fit[1]*other[i] for i in held]
        priors.append(dict(l1=penalty,true_source_cd=fit[0],extra_source_cd=fit[1],
            false_emitters_at_1_5cd=int(fit[1]>=1.5),heldout_rmse_lux=rmse(prediction,[y[i] for i in held])))
    return dict(truth_cd=3.,recall_threshold_cd=1.5,rows=rows,source_count_priors=priors,
        identical_basis_allocation=duplicate,
        warning='Recall is a toy threshold, NOT calibrated source-existence probability; held-out fit alone cannot prove attribution')


def geometry_cases():
    source=(-1.,-.6,1.5); reflector=(-.5,.8,.8); receiver=(1.,0.,.2)
    # Opening y in [.4,.7], z in [.3,1.2]; original X04 triangle predicates.
    pieces=wall(x=0,height=2,y0=-2,y1=.4)+wall(x=0,height=2,y0=.7,y1=2)
    pieces+=quad(((0,.4,0),(0,.7,0),(0,.7,.3),(0,.4,.3)))
    pieces+=quad(((0,.4,1.2),(0,.7,1.2),(0,.7,2),(0,.4,2)))
    def vis(scene,a,b): return scene.query(a,Node(b,(),(1.,1.,1.)))
    rows={}
    for label,scene in (
        ('doorway',Scene(pieces)),('sealed_wall',Scene(wall(x=0,height=2,y0=-2,y1=2))),
        ('unknown_mask',Scene(tuple(replace(t,optical='alpha_unknown') for t in pieces))),
        ('unbounded_missing',Scene(pieces,unbounded_unknown=True))):
        primary=vis(scene,receiver,source); incoming=vis(scene,reflector,source); reflected=vis(scene,receiver,reflector)
        rows[label]=dict(direct_visibility=primary,source_to_reflector=incoming,
            reflector_to_receiver=reflected,
            possible_one_bounce_path=[incoming[0]*reflected[0],incoming[1]*reflected[1]],
            local_physical_emitters=0)
    # Hemispheric sky irradiance is a boundary condition, no local point emitter.
    sky=Scene(quad(((-2,-2,2),(2,-2,2),(2,2,2),(-2,2,2)),optical='sky'),require_sky=True)
    rows['sky_only']=dict(zenith_visibility=sky.query((0.,0.,0.),Node((),(0.,0.,1.),(1.,1.,1.))),
        radiance_cd_m2=2.,horizontal_irradiance_lux=2*math.pi,local_physical_emitters=0,
        note='Uniform unoccluded hemisphere, analytic pi L; one zenith query alone does not integrate sky')
    return rows


def reflectance_ambiguity():
    # Outgoing Lambertian radiance L=rho*E/pi, NOT the incident-multiplier LM.
    L=.4*3/math.pi
    alternatives=[dict(reflectance=rho,incident_lux=L*math.pi/rho,
                       outgoing_cd_m2=rho*(L*math.pi/rho)/math.pi) for rho in (.2,.4,.8)]
    # Two-bounce-only receiver sees the product I*rho1*rho2: still not identifiable.
    return dict(observed_outgoing_cd_m2=L,alternatives=alternatives,
        incident_ratio_max_min=4.,two_bounce_product_examples=[(3.,.4,.5),(6.,.2,.5),(3.,.8,.25)],
        lm_warning='Do not multiply receiver rho into JA lightmap target or divide baked RGB by current PBR albedo')


def finite_reflector_reference():
    """Independent finite-area quadrature vs midpoint surfel; constant reflected
    exitance M=1 lm/m2. No claim that incident light is constant in a real map.
    """
    normal=(0.,0.,1.); centre=(0.,0.,1.)
    def evaluate(receiver,size,order):
        total=0.
        for u,wu in gauss(order):
            for v,wv in gauss(order):
                p=((u-.5)*size,(v-.5)*size,centre[2]); d=sub(p,receiver); r=length(d)
                total+=size*size*wu*wv*(d[2]/r)**2/(math.pi*r*r)
        return total
    rows=[]
    for size in (.1,.5,1.):
        for receiver in ((.1,0.,0.),(.1,0.,.8)):
            ref=evaluate(receiver,size,48); practical=evaluate(receiver,size,4)
            d=sub(centre,receiver); r=length(d); midpoint=size*size*d[2]**2/(math.pi*r**4)
            rows.append(dict(size_metres=size,receiver=receiver,reference_lux=ref,
                order24_difference=abs(ref-evaluate(receiver,size,24)),
                midpoint_relative_error=abs(midpoint-ref)/ref,
                gauss4_relative_error=abs(practical-ref)/ref,
                reference_evaluations=48**2,fast_evaluations=4**2))
    return rows


def censoring():
    # An admitted synthetic linear RGB8 observation, no renderer transform.
    def interval(raw): return (raw/255,(raw+1)/255) if raw<255 else (1.,None)
    values=[0.,.02,.5,1.,2.]
    encoded=[min(255,int(255*v)) for v in values]
    lower=[interval(v)[0] for v in encoded]
    # A deliberately incomplete blocker gives [0,unoccluded]. It is not zero GI.
    model=Transfer((1.,1.),(Edge(0,1,0.,.2),),'unknown-path')
    bounds=model.apply([[3.,3.,3.],[0.,0.,0.]],[[.5,.5,.5],[0.,0.,0.]])
    return dict(true_relative=values,raw_rgb8=encoded,intervals=[interval(v) for v in encoded],
        point_value_rmse=rmse(lower,values),unknown_transport_lower=bounds[0][1],
        unknown_transport_upper=bounds[1][1],
        note='Censoring is lower-bound information; no exact upper target or interval probability is inferred')


def bounce_orders():
    t=Transfer((1.,1.),(Edge(0,1,.2,.2),Edge(1,0,.2,.2)),'two-patch-closed-subnetwork')
    D=[[10.]*3,[0.]*3]; rho=[[.5]*3,[.5]*3]
    layers=t.bounces(D,rho,4)
    # This analytic network has per-step power contraction .1.
    return dict(layers_lux=[layer[0] for layer in layers],power_contraction=.1,
        tail_power_after_four_bound_lumens=10*.1**5/(1-.1),
        note='Nonzero later bounces demonstrated on a separate passive network; not q3map empirical radiosity')


def uncertainty_and_leakage():
    true=network(); train=indices(true,'train'); held=indices(true,'heldout')
    low=network(rho=(.5,.1,.02)); high=network(rho=(.85,.3,.2))
    truth=flat(true['G'],held); a=flat(low['G'],held); b=flat(high['G'],held)
    covered=sum(x-1e-14<=v<=y+1e-14 for x,v,y in zip(a,truth,b))
    # Unknown bake material C: an interval is not an estimated mean reflectance.
    zero=network(rho=(0.,0.,0.)); maximum=network(rho=(1.,1.,1.))
    # A two-node Euclidean smoothness residual crosses a sealed thin wall:
    # min (z0-1)^2 + z1^2 + lambda*(z0-z1)^2, lambda=1.
    naive_dark_side=1/3
    return dict(bounded_material=dict(lower_reflectance=(.5,.1,.02),upper_reflectance=(.85,.3,.2),
        heldout_channels=len(truth),channels_enclosed=covered,
        interpretation='Deterministic conditional envelope at FIXED source/profile/geometry, not a probability'),
        unknown_material_gi_mean_lux_range=[3*sum(flat(zero['G'],held))/(3*len(held)),
                                          3*sum(flat(maximum['G'],held))/(3*len(held))],
        thin_wall=dict(naive_smooth_dark_side=naive_dark_side,visibility_constrained_dark_side=0.,
            reason='Euclidean proximity cannot authorize cross-wall transfer'),
        sparse_training_sites=[train[i] for i in range(0,len(train),8)],
        unknown_profile_posterior=None)


def sparse_clipped_fit():
    data=network(); train=indices(data,'train')[::8]; held=indices(data,'heldout')
    column=[d[0]+g[0] for d,g in zip(data['D'],data['G'])]
    gain=128.; raw=[min(255,int(3*gain*v)) for v in column]
    lo=max(raw[i]/(gain*column[i]) for i in train)
    hi=min((raw[i]+1)/(gain*column[i]) for i in train if raw[i]<255)
    fitted=(lo+hi)/2
    def error(i):
        value=fitted*gain*column[i]; upper=math.inf if raw[i]==255 else raw[i]+1
        return max(raw[i]-value,0.,value-upper)
    exact_byte_fit=nnls([[gain*column[i] for i in train]],[raw[i] for i in train])[0]
    return dict(train_samples=len(train),heldout_samples=len(held),gain_bytes_per_lux=gain,
        saturated_training_samples=sum(raw[i]==255 for i in train),
        truth_cd=3.,feasible_cd_interval=[lo,hi],treating_clipped_as_exact_cd=exact_byte_fit,
        heldout_interval_rmse_bytes=math.sqrt(sum(error(i)**2 for i in held)/len(held)),
        heldout_intervals_covered=sum(lo*gain*column[i]<=((raw[i]+1) if raw[i]<255 else math.inf) and
            hi*gain*column[i]>=raw[i] for i in held),
        source_recalled=lo>=1.5,extra_physical_sources=0,
        all_clipped_intensity_upper=None,
        note='Known single candidate, linear scalar quantizer; NOT NR shared-peak ColorToBytes. No source-discovery benchmark.')


def run():
    result=dict(schema='org.openjk.inverse-gi-research',version=1,
        units='SI analytical scenes only; no stock-map absolute calibration',
        stability=stability(),emitter_suppression=emitter_suppression(),geometry=geometry_cases(),
        reflectance=reflectance_ambiguity(),finite_reflector=finite_reflector_reference(),censoring=censoring(),
        bounce_orders=bounce_orders(),uncertainty=uncertainty_and_leakage(),sparse_clipped=sparse_clipped_fit())
    data=network(); sums=[0.]*len(data['areas'])
    for e in data['transfer'].edges: sums[e.sender]+=e.upper
    result['network']=dict(patches=len(sums),edges=len(data['transfer'].edges),
        maximum_outgoing_fraction=max(sums),basis='reciprocal midpoint, black receivers; no higher bounce in this fixture')
    # Deterministic witness excludes timings and machine-specific paths.
    result['source_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'transport.py',HERE/'experiment.py')}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,default=HERE/'results/analytic.json')
    args=parser.parse_args(); result=run(); write_json(args.output,result)
    print(json.dumps(dict(output=str(args.output),network=result['network'],
        zero_bounce_false_attribution=result['stability']['zero_bounce_false_attribution'],
        suppression=result['emitter_suppression']['rows']),indent=2))


if __name__=='__main__': main()
