#!/usr/bin/env python3
"""Bounded offline profile/source ambiguity experiments, NOT a production solver.

Fitting accepts training records only. Validation ranks hypotheses; held-out
targets are read by the controlling runner only after fitting/ranking freeze.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass, asdict
from collections import defaultdict
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
from bake_model import point_scalar, spot_factor, encode_color, dot, sub, unit
from hypotheses import catalogue, continuous_encode, require, UnspecifiedModel, VERSION


@dataclass(frozen=True)
class Observation:
    id: str
    position: tuple
    normal: tuple
    rgb: tuple
    block: str
    partition: str
    footprint: float = 2.
    domain: str = 'EncodedRGB8'
    primaries: str = 'Unknown'
    source_transfer: str = 'Unknown'

    def __post_init__(self):
        if self.domain!='EncodedRGB8' or len(self.rgb)!=3 or any(type(v) is not int or not 0<=v<=255 for v in self.rgb):
            raise ValueError('Comparison requires original encoded RGB8, not linear irradiance')
        if (len(self.position)!=3 or len(self.normal)!=3 or
            not all(math.isfinite(v) for v in self.position+self.normal) or
            abs(sum(v*v for v in self.normal)-1)>1e-5 or
            not math.isfinite(self.footprint) or self.footprint<0):
            raise ValueError('Finite world position, unit normal and non-negative footprint required')


@dataclass(frozen=True)
class Configuration:
    id: str
    origins: tuple
    axes: tuple
    nuisance_dof: int = 0
    # A configuration is a FIXED source-count/position hypothesis. It has no RGB
    # observations/answers and cannot modify geometry in response to validation.

    def __post_init__(self):
        if not self.origins or len(self.origins)!=len(self.axes) or self.nuisance_dof<0:
            raise ValueError('Source count/axis/parameter budget mismatch')
        for origin,axis in zip(self.origins,self.axes):
            if (len(origin)!=3 or len(axis)!=3 or not all(math.isfinite(v) for v in origin+axis)
                or abs(sum(v*v for v in axis)-1)>1e-5):
                raise ValueError('Finite source position and unit propagation axis required')


def interval_residual(predicted, raw, round_nearest=False, compression=0.):
    if not math.isfinite(predicted) or not 0<=raw<=255:
        raise ValueError('Invalid observation comparison')
    lo=max(0.,raw-(.5 if round_nearest else 0.)-compression)
    hi=math.inf if raw==255 else raw+(.5 if round_nearest else 1.)+compression
    return max(lo-predicted,0.,predicted-hi)


def metrics(predicted, data):
    if len(predicted)!=len(data) or not data:
        return dict(status='Unavailable',interval_rmse=None,byte_rmse=None,blocks=0,n=0)
    loss=0.; byte_loss=0.; by_block=defaultdict(list); censored=0
    for rgb,row in zip(predicted,data):
        errors=[interval_residual(v,b)**2 for v,b in zip(rgb,row.rgb)]
        loss+=sum(errors); byte_loss+=sum((int(min(255,max(0,v)))-b)**2 for v,b in zip(rgb,row.rgb))
        by_block[row.block].append(sum(errors)/3)
        censored+=sum(v==255 for v in row.rgb)
    blocks={k:sum(v)/len(v) for k,v in sorted(by_block.items())}
    return dict(status='MeasuredEncodedPrediction',interval_rmse=math.sqrt(loss/(3*len(data))),
                byte_rmse=math.sqrt(byte_loss/(3*len(data))),n=len(data),blocks=len(blocks),
                block_mse=blocks,censored_channels=censored,
                physical_error=None,posterior_probability=None)


def visibility(position, origin, scene):
    """Analytic own-content scene geometry. No PVS or unknown-as-transparent rule."""
    if scene not in ('wall','door'): return True
    x,y,z=position; X,Y,Z=origin
    if x*X>=0 or X==x: return True
    t=-x/(X-x); crossing=(y+t*(Y-y),z+t*(Z-z))
    return crossing[1]>112 or (scene=='door' and abs(crossing[0])<28)


class Prepared:
    def __init__(self,hypothesis,data,configuration,scene='clear',masks=None):
        require(hypothesis,'attenuation','angle','spot','point_scale','output')
        self.h=hypothesis; self.data=tuple(data); self.configuration=configuration
        self.a=hypothesis.rule('attenuation').values(); self.angle=hypothesis.rule('angle')
        self.scale=hypothesis.rule('point_scale').values()['point_scale']
        self.geometry=[]; self.cache={}
        for j,(origin,axis) in enumerate(zip(configuration.origins,configuration.axes)):
            values=[]
            for i,row in enumerate(data):
                v=sub(origin,row.position); r=math.sqrt(dot(v,v))
                if r<=1e-6: raise ValueError('Source coincides with receiver')
                cosine=dot(row.normal,v)/r
                visible=visibility(row.position,origin,scene) if masks is None else masks[i][j]
                if visible is None: raise UnspecifiedModel('uncertified ray in fit batch')
                cone=1.
                if hypothesis.rule('spot').selector=='q3map_radius16_edge32':
                    p=hypothesis.rule('spot').values()
                    cone=spot_factor(row.position,origin,axis,p['target_distance'],p['radius'])
                values.append((r,cosine,visible,cone))
            self.geometry.append(values)

    def scalar(self,source,intensity):
        key=(source,intensity)
        if key in self.cache: return self.cache[key]
        values=[]
        for r,c,visible,cone in self.geometry[source]:
            if not visible or c<=0 or intensity==0: values.append(0.); continue
            if self.angle.selector=='none': c=1.
            value=point_scalar(intensity,r,c,linear=self.h.rule('attenuation').selector=='linear',
                scale=self.scale,extra_distance=self.a['extra_distance_world'],fade=self.a['fade'],
                half_lambert=self.angle.selector=='half_lambert',angle_scale=self.angle.values()['angle_scale'],
                fast=self.a['fast'],tolerance=self.a['tolerance'])*cone
            if self.a['fast'] and value<=self.a['tolerance']: value=0.
            values.append(value)
        if len(self.cache)>=192: self.cache.clear()  # per-fit, bounded scratch
        self.cache[key]=values
        return values

    def predict(self,parameters,encoding):
        count=len(self.configuration.origins)
        rgb=[list(parameters[count*3:count*3+3]) if len(parameters)>count*3 else [0.,0.,0.] for _ in self.data]
        for j in range(count):
            q=parameters[3*j:3*j+3]; intensity=max(q)
            if not intensity: continue
            scalar=self.scalar(j,intensity)
            for i,value in enumerate(scalar):
                for c in range(3): rgb[i][c]+=value*q[c]/intensity
        return [continuous_encode(value,**encoding) for value in rgb]


def line_minimum(function,maximum,current,steps=16):
    grid=sorted(set([0.,maximum,current]+[maximum*10**(-4+4*i/steps) for i in range(steps+1)]))
    # Linear cutoff creates a flat dark plateau. Refining just the first tied
    # coarse minimum can miss a narrow lit minimum between logarithmic probes.
    # Add fixed subdivisions BEFORE selecting a bracket (same work for all laws).
    grid=sorted(set(grid+[a+(b-a)*j/4 for a,b in zip(grid,grid[1:]) for j in (1,2,3)]))
    values=[function(v) for v in grid]
    best=min(range(len(grid)),key=lambda i:(values[i],grid[i]))
    lo,hi=grid[max(0,best-1)],grid[min(len(grid)-1,best+1)]
    # Bounded deterministic local refinement, not a convergence guarantee.
    for _ in range(16):
        x=lo+(hi-lo)/3; y=hi-(hi-lo)/3
        if function(x)<=function(y): hi=y
        else: lo=x
    candidates=[(values[best],grid[best]),(function((lo+hi)/2),(lo+hi)/2)]
    return min(candidates)[1]


def fit_one(prepared,encoding,amplitude_max=3000.,fill_max=0.,passes=2,amplitude_prior=0.):
    data=prepared.data
    if not math.isfinite(amplitude_max+fill_max+amplitude_prior) or amplitude_max<=0 or fill_max<0 or amplitude_prior<0 or not 1<=passes<=8:
        raise ValueError('Invalid bounded fit policy')
    if not data or len({r.block for r in data})<2: raise ValueError('Insufficient independent training blocks')
    n=len(prepared.configuration.origins)
    mean=[sum(r.rgb[c] for r in data)/len(data) for c in range(3)]
    colour=[x/max(1.,max(mean)) for x in mean]
    parameters=[min(300.,amplitude_max)*x for _ in range(n) for x in colour]
    if fill_max: parameters += [0.,0.,0.]
    evaluations=0
    def objective(p):
        nonlocal evaluations
        evaluations+=1
        if evaluations>6000: raise ValueError('Fit work budget exhausted')
        predicted=prepared.predict(p,encoding)
        loss=sum(interval_residual(v,b)**2 for rgb,row in zip(predicted,data) for v,b in zip(rgb,row.rgb))/(3*len(data))
        return loss+amplitude_prior*sum((v/300.)**2 for v in p[:n*3])
    for _ in range(passes):
        for j in range(n):
            old=parameters[3*j:3*j+3]; peak=max(old)
            chroma=[v/peak for v in old] if peak else [1.,1.,1.]
            def common(value):
                p=parameters[:]; p[3*j:3*j+3]=[value*x for x in chroma]; return objective(p)
            intensity=line_minimum(common,amplitude_max,peak)
            parameters[3*j:3*j+3]=[intensity*x for x in chroma]
        for k in range(len(parameters)):
            maximum=amplitude_max if k<3*n else fill_max
            def channel(value):
                p=parameters[:]; p[k]=value; return objective(p)
            parameters[k]=line_minimum(channel,maximum,parameters[k],steps=10)
    return parameters,objective(parameters),evaluations


def compare(hypotheses,training,validation,heldout,configurations,scene='clear',
            encodings=None,fill_max=0.,amplitude_prior=0.,passes=2,amplitude_max=3000.,
            fixed_amplitudes=None,masks=None,complexity_weight=.25):
    """Only training is passed to fit_one; holdout is evaluated after rank freeze."""
    if not encodings: encodings=(dict(gamma=1.,compensate=1.,exposure=0.,srgb=False),)
    if any(r.partition!='train' for r in training): raise ValueError('Non-training targets passed to fitter')
    results=[]
    for h in hypotheses:
        best=None; alternatives=[]
        for configuration in configurations:
            for encoding in encodings:
                try:
                    prepared=Prepared(h,training,configuration,scene,None if masks is None else masks['train'])
                    if fixed_amplitudes is None:
                        params,loss,evaluations=fit_one(prepared,encoding,amplitude_max,fill_max,passes,amplitude_prior)
                        k=3*len(configuration.origins)+(3 if fill_max else 0)
                    else:
                        if len(fixed_amplitudes)!=3*len(configuration.origins): raise ValueError('Known source amplitude/count mismatch')
                        params=list(fixed_amplitudes); evaluations=0
                        loss=metrics(prepared.predict(params,encoding),training)['interval_rmse']**2; k=0
                    # Same nominal parameter/selection budget for every family.
                    same_size=[c for c in configurations if len(c.origins)==len(configuration.origins)]
                    geometry_dof=max(c.nuisance_dof for c in same_size) or sum(len({c.origins[s][axis] for c in same_size})>1
                                     for s in range(len(configuration.origins)) for axis in range(3))
                    encoding_dof=sum(len({e[key] for e in encodings})>1 for key in ('gamma','compensate','exposure'))
                    k+=geometry_dof+encoding_dof
                    effective=len({r.block for r in training})
                    penalty=complexity_weight*(k*math.log1p(effective)+math.log(len(configurations)*len(encodings)))/effective
                    item=dict(configuration=configuration.id,parameters=params,train_mse=loss,
                              parameter_count=k,training_selection_score=loss+penalty,
                              complexity_penalty=penalty,encoding=encoding,evaluations=evaluations,
                              geometry_dof=geometry_dof,encoding_dof=encoding_dof)
                    alternatives.append(item)
                    if best is None or (item['training_selection_score'],configuration.id)<(best['training_selection_score'],best['configuration'].id):
                        best=dict(item,configuration=configuration)
                except UnspecifiedModel as e:
                    best=None; alternatives=[]; break
        if best is None:
            results.append(dict(id=h.stable_id,status='UnspecifiedModel',validation=None,heldout=None)); continue
        conf=best.pop('configuration')
        val=Prepared(h,validation,conf,scene,None if masks is None else masks['validation']).predict(best['parameters'],best['encoding'])
        results.append(dict(best,id=h.stable_id,family=h.family,hypothesis_sha256=h.digest(),
            evaluation_sha256=hashlib.sha256(json.dumps(dict(profile=h.digest(),configuration=asdict(conf),
                parameters=best['parameters'],encoding=best['encoding'],formula_version=VERSION,
                domain='CompilerEncodedRGB8'),sort_keys=True,allow_nan=False).encode()).hexdigest(),
            configuration=conf.id,status='ConditionalEvidence',validation=metrics(val,validation),
            validation_score=metrics(val,validation)['interval_rmse']**2+best['complexity_penalty'] if validation else None,
            alternatives=alternatives,compiler_identity_verified=False,absolute_scale='Unknown'))
    ranked=[r for r in results if r.get('validation') and r['validation']['interval_rmse'] is not None]
    ranked.sort(key=lambda r:(r['validation_score'],r['id']))
    ranking=[r['id'] for r in ranked]  # frozen before held-out prediction
    for r in ranked:
        conf=next(c for c in configurations if c.id==r['configuration'])
        predicted=Prepared(next(h for h in hypotheses if h.stable_id==r['id']),heldout,conf,scene,
                           None if masks is None else masks['heldout']).predict(r['parameters'],r['encoding'])
        r['heldout']=metrics(predicted,heldout)
    return dict(ranking=ranking,results=results,selection='validation only; held-out excluded from fit/ranking',
                source_count_policy='fixed within configuration; matched across families',
                policy=dict(amplitude_max=amplitude_max,fill_max=fill_max,passes=passes,
                    amplitude_prior=amplitude_prior,complexity_weight=complexity_weight,
                    configurations=len(configurations),encoding_recipes=len(encodings)),
                exact_compiler_identity='Unknown',posterior_probabilities=None)


def retention(comparison,bands=(.25,1.,2.,4.)):
    measured=[r for r in comparison['results'] if r.get('validation') and r['validation']['interval_rmse'] is not None]
    if not measured: return dict(status='InsufficientEvidence',plausible=[])
    best=min(r['validation']['interval_rmse'] for r in measured)
    return dict(status='ConditionalOnly',regret_band_sweep={str(b):[r['id'] for r in measured
        if r['validation']['interval_rmse']<=best+b] for b in bands},
        interpretation='Operational regret bands in encoded bytes, NOT posterior credible sets',
        historical_profile='UnknownProfile')


def feature_assessment(comparison,hypotheses,bands=(.25,1.,2.,4.)):
    """Report feature equivalence classes, never promote historical identity.

    Regret is an explicit operational allowance, not statistical confidence.
    A singleton is conditional on this source/encoding/visibility experiment;
    the controlling harness must still check sensitivity and model discrepancy.
    """
    kept=retention(comparison,bands)
    classes={}
    for band,ids in kept.get('regret_band_sweep',{}).items():
        classes[band]={feature:sorted({hypotheses[i].rule(feature).selector or 'Unknown' for i in ids})
                       for feature in ('attenuation','angle','spot','output')}
    return dict(historical_profile='UnknownProfile',conditional_feature_sets=classes,
                profile_posterior=None,requires_sensitivity_review=True,
                unsupported_features=['full visibility/filter replay','GI','absolute calibration'],
                interpretation='Singleton feature sets are conditional, not compiler identification')


def write(path,value):
    Path(path).write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def own_scene(name,truth,kind='broad',gamma=1.,clipped=False,bounce=0.,seed=20261027,
              exposure=0.,compensate=1.):
    """Quantized realistic counterexamples plus declared analytic visibility.

    Each three-sample receiver patch is a SINGLE split/bootstrap unit. Patches
    are >=12 world units apart (footprint radius 2 + filtering margin 2 each).
    Folds are assigned by patch before generating any target RGB.
    """
    rnd=random.Random(seed); rows=[]
    if kind=='narrow':
        centres=[(144*math.cos(i*math.pi/8),144*math.sin(i*math.pi/8),1.) for i in range(16)]
    else:
        centres=[(x,y,1.) for y in (-240,-144,-48,48,144,240) for x in (-240,-144,-48,48,144,240)]
    normals=[(0.,0.,1.)]*len(centres)
    if kind=='multi_surface':
        for x in (-320.,320.):
            for z in (48.,144.,240.,336.):
                for y in (-192.,0.,192.):
                    centres.append((x,y,z)); normals.append((-math.copysign(1.,x),0.,0.))
    conf=Configuration('truth',((-96.,0.,192.),) if kind in ('wall','door') else ((0.,0.,192.),),((0.,0.,-1.),))
    if kind=='two_sources': conf=Configuration('truth',((-96.,0.,192.),(96.,0.,192.)),((0.,0.,-1.),)*2)
    recipe=dict(gamma=gamma,compensate=compensate,exposure=exposure,srgb=False)
    q=[(3000. if clipped else 150. if kind=='two_sources' else 300.)]*3*len(conf.origins)
    for group,(p,n) in enumerate(zip(centres,normals)):
        role=('train','train','train','validation','heldout')[group%5]
        for j in range(3):
            # Tangential, correlated samples stay on their actual receiver plane.
            delta=(0.,float(j-1),0.) if abs(n[0])>.5 else (float(j-1),0.,0.)
            position=tuple(x+d for x,d in zip(p,delta))
            dummy=Observation(f'{name}/{group}/{j}',position,n,(0,0,0),str(group),role)
            prepared=Prepared(truth,(dummy,),conf,'wall' if kind=='wall' else 'door' if kind=='door' else 'clear')
            params=q[:]
            if bounce: params += [bounce*(1 if kind=='ambient' else 1+.35*position[0]/320),bounce*.7,bounce*.4]
            encoded=prepared.predict(params,recipe)[0]
            rgb=tuple(min(255,int(v)) for v in encoded)
            rows.append(Observation(dummy.id,position,n,rgb,dummy.block,role))
    return rows,conf,q


def run_synthetic(output):
    registry=catalogue(); inv=registry['NetRadiant-d9338309-JA/isq_cos']; lin=registry['NetRadiant-d9338309-JA/linear_noangle']
    suites={}
    cases=[('narrow',inv,'narrow',1.,False,0.),('broad',inv,'broad',1.,False,0.),
           ('linear_broad',lin,'broad',1.,False,0.),('multiple_normals',inv,'multi_surface',1.,False,0.),
           ('clipped',inv,'narrow',1.,True,0.),('gamma',inv,'broad',2.2,False,0.),
           ('bounce_contaminated',inv,'broad',1.,False,24.),('door_shadows',inv,'door',1.,False,0.),
           ('wall_shadows',inv,'wall',1.,False,0.),('unknown_count',inv,'two_sources',1.,False,0.),
           ('angular',registry['NetRadiant-d9338309-JA/isq_half_lambert'],'multi_surface',1.,False,0.),
           ('spot',registry['NetRadiant-d9338309-JA/spot'],'broad',1.,False,0.),
           ('exposure',inv,'broad',1.,False,0.),('ambient_fill',inv,'ambient',1.,False,16.)]
    for name,truth,kind,gamma,clipped,bounce in cases:
        data,conf,q=own_scene(name,truth,kind,gamma,clipped,bounce,exposure=128. if name=='exposure' else 0.)
        train=[r for r in data if r.partition=='train']; val=[r for r in data if r.partition=='validation']; held=[r for r in data if r.partition=='heldout']
        candidates=[inv,lin,registry['NetRadiant-d9338309-JA/linear_cos'],registry['NetRadiant-d9338309-JA/isq_noangle']]
        if name=='angular': candidates += [registry['NetRadiant-d9338309-JA/isq_half_lambert']]
        if name=='spot': candidates += [registry['NetRadiant-d9338309-JA/spot']]
        protocols={}
        protocols['known_geometry_intensity']=compare(candidates,train,val,held,[conf],
            scene=kind if kind in ('wall','door') else 'clear',fixed_amplitudes=q)
        protocols['known_geometry_fitted_rgb']=compare(candidates,train,val,held,[conf],scene=kind if kind in ('wall','door') else 'clear')
        if name in ('narrow','broad','multiple_normals','door_shadows','wall_shadows'):
            configurations=[Configuration('height_'+str(z),tuple((x,y,z) for x,y,_ in conf.origins),conf.axes) for z in (96.,144.,192.,256.,320.)]
            protocols['variable_geometry_rgb']=compare(candidates,train,val,held,configurations,scene=kind if kind in ('wall','door') else 'clear',passes=1)
        if name in ('gamma','exposure','ambient_fill','bounce_contaminated','clipped','unknown_count'):
            encodings=[dict(gamma=g,compensate=c,exposure=e,srgb=False) for g,c,e in ((1.,1.,0.),(2.2,1.,0.),(1.,2.,0.),(2.2,2.,0.),(1.,1.,128.))] if name in ('gamma','exposure') else None
            configurations=[conf]
            if name=='unknown_count': configurations=[Configuration('one_midpoint',((0.,0.,192.),),((0.,0.,-1.),)),conf]
            protocols['nuisance_sensitivity']=compare(candidates,train,val,held,configurations,encodings=encodings,
                fill_max=32. if name in ('bounce_contaminated','ambient_fill') else 0.,passes=1)
        if name=='spot':
            configurations=[Configuration('axis_'+str(deg),conf.origins,
                ((math.sin(math.radians(deg)),0.,-math.cos(math.radians(deg))),),1) for deg in (-15.,0.,15.)]
            protocols['axis_prior']=compare(candidates,train,val,held,configurations,passes=1)
        suites[name]=dict(truth_profile=truth.stable_id,source_answers='evaluation only',
            partitions={p:sum(r.partition==p for r in data) for p in ('train','validation','heldout')},
            training_patch_blocks=len({r.block for r in train}),data_sha256=hashlib.sha256(
                json.dumps([asdict(r) for r in data],sort_keys=True).encode()).hexdigest(),protocols=protocols,
            retained={p:retention(result) for p,result in protocols.items()})
        write(output/(name+'.json'),suites[name]); print(name,'completed',flush=True)
    return suites


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    registry=catalogue(); write(args.output/'hypotheses.json',{k:asdict(v) for k,v in registry.items()})
    result=run_synthetic(args.output)
    write(args.output/'result.json',dict(schema='org.openjk.bake-identifiability.v1',synthetic=result,
          formula_version=VERSION,solver_implemented=False,stock_default='UnknownProfile'))


if __name__=='__main__': main()
