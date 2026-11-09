"""Complete research IDF discovery/refinement cycle, never a production solver.
Only observation-domain additive linear values enter residual subtraction.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import itertools
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'build/idf-full-deps'))
import numpy as np
from scipy.optimize import least_squares, nnls


def sphere(subdivisions=1):
    phi = (1+np.sqrt(5))/2
    vertices = np.array([(0,a,b*phi) for a,b in itertools.product((-1,1),repeat=2)]+
                        [(a,b*phi,0) for a,b in itertools.product((-1,1),repeat=2)]+
                        [(b*phi,0,a) for a,b in itertools.product((-1,1),repeat=2)],float)
    vertices /= np.linalg.norm(vertices,axis=1)[:,None]
    faces=[]
    for f in itertools.combinations(range(12),3):
        a,b,c=vertices[list(f)]; n=np.cross(b-a,c-a); side=(vertices-a)@n
        if np.all(side<=1e-9) or np.all(side>=-1e-9): faces.append(f)
    vertices=vertices.tolist()
    for _ in range(subdivisions):
        edges={}; new=[]
        def midpoint(a,b):
            key=tuple(sorted((a,b)))
            if key not in edges:
                v=np.array(vertices[a])+vertices[b];v/=np.linalg.norm(v)
                edges[key]=len(vertices);vertices.append(v.tolist())
            return edges[key]
        for a,b,c in faces:
            ab,bc,ca=midpoint(a,b),midpoint(b,c),midpoint(c,a)
            new.extend(((a,ab,ca),(b,bc,ab),(c,ca,bc),(ab,bc,ca)))
        faces=new
    return np.array(vertices),np.array(faces,int)


class Response:
    """Explicit geometry and known/unknown coefficients, optional native visibility."""
    def __init__(self, positions,normals,bounds,oracle=None):
        self.x=np.asarray(positions,float);self.n=np.asarray(normals,float)
        self.bounds=np.asarray(bounds,float).reshape(2,3);self.oracle=oracle;self.cache={}
        self.reference_distance=np.max(self.bounds[1]-self.bounds[0])/2
        self.queries=0;self.unknown=0;self.invalid=0

    def factors(self,points):
        points=np.atleast_2d(points); output=[]
        for p in points:
            key=tuple(np.round(p,12))
            if key not in self.cache:
                self.queries+=1
                delta=p-self.x; r2=np.sum(delta*delta,axis=1)
                v=np.maximum(0,np.sum(delta*self.n,axis=1))/np.maximum(r2,1e-18)**1.5*self.reference_distance**2
                free=np.all(p>self.bounds[0]+1e-8) and np.all(p<self.bounds[1]-1e-8)
                if self.oracle is not None:
                    g=self.oracle.query(p.tolist());free=free and g['free'];states=np.array(g['states'])
                    v[states==1]=0
                    missing=(states==2)&(v>0);self.unknown+=int(np.sum(missing));v[missing]=np.nan
                if not free:
                    v[:]=np.nan;self.invalid+=1
                self.cache[key]=v
            output.append(self.cache[key])
        return np.array(output)

    def matrix(self,positions):
        if not len(positions):return np.empty((len(self.x),0))
        return self.factors(positions).T


class Field:
    def __init__(self,response,policy):
        self.response=response;self.policy=policy
        b=response.bounds;self.centre=np.mean(b,axis=0);self.radius=np.linalg.norm(b[1]-b[0])/2
        axes=[np.linspace(b[0,i]+policy['wall_margin_fraction']*(b[1,i]-b[0,i]),
                          b[1,i]-policy['wall_margin_fraction']*(b[1,i]-b[0,i]),policy['grid_divisions']) for i in range(3)]
        self.positions=np.array(list(itertools.product(*axes)));self.directions,self.faces=sphere(policy['sphere_subdivisions'])
        offsets=self.directions*policy['sphere_radius_fraction']*self.radius
        probes=self.positions[:,None,:]+offsets[None,:,:]
        self.coefficients=response.factors(probes.reshape(-1,3))
        self.centres=response.factors(self.positions)
        self.free=np.all(np.isfinite(self.centres),axis=1)
        self.surface_directions=response.x-self.centre
        self.surface_directions/=np.linalg.norm(self.surface_directions,axis=1)[:,None]
        self.transforms=0;self.pair_visits=0

    def propose(self,values):
        self.transforms+=1
        brightness=np.mean(values,axis=1)
        if np.min(brightness)<-self.policy['negative_residual_tolerance']:
            raise ValueError('Negative unexplained linear observation')
        brightness=np.maximum(brightness,0) # tolerance-sized roundoff only
        a=self.coefficients;mask=np.isfinite(a)&(a>1e-12)
        ratios=np.full_like(a,np.inf)
        np.divide(brightness[None,:],a,out=ratios,where=mask)
        labels=np.argmin(ratios,axis=1);valid=np.any(mask,axis=1)
        self.pair_visits+=a.size
        u=self.surface_directions[labels].reshape(len(self.positions),len(self.directions),3)
        triangles=u[:,self.faces,:];aa,bb,cc=triangles[:,:,0],triangles[:,:,1],triangles[:,:,2]
        numerator=np.abs(np.sum(aa*np.cross(bb,cc),axis=2))
        denominator=1+np.sum(aa*bb+bb*cc+cc*aa,axis=2)
        areas=2*np.arctan2(numerator,denominator)
        areas[~np.all(valid.reshape(len(self.positions),-1)[:,self.faces],axis=2)]=0
        scores=np.sum(areas,axis=1);scores[~self.free]=0
        if np.max(brightness)<=1e-12:scores[:]=0
        # Local maxima on the room lattice, not eight arbitrary high-vote cells.
        local=[];step=np.max(self.response.bounds[1]-self.response.bounds[0])/(self.policy['grid_divisions']-1)
        for i,p in enumerate(self.positions):
            if scores[i]<=1e-10:continue
            neighbours=np.flatnonzero(np.linalg.norm(self.positions-p,axis=1)<=step*1.75)
            if any(scores[j]>scores[i]+1e-10 or (abs(scores[j]-scores[i])<=1e-10 and j<i) for j in neighbours):continue
            local.append(i)
        # Paper starts high then lowers until at least one proposal appears.
        threshold=len(self.response.x)/10
        selected=[]
        while local and not selected:
            for i in sorted(local,key=lambda i:(-scores[i],i)):
                if scores[i]<threshold:continue
                if all(np.linalg.norm(self.positions[i]-self.positions[j])>=self.policy['peak_merge_radius_fraction']*self.radius for j in selected):selected.append(i)
                if len(selected)==self.policy['max_peaks']:break
            threshold*=.5
        return [dict(position=self.positions[i].tolist(),vote=float(scores[i])) for i in selected]


def render(response,positions,intensities,channels):
    a=response.matrix(positions)
    if not np.all(np.isfinite(a)):raise ValueError('Unavailable required source/receiver coefficient')
    return a@np.asarray(intensities).reshape(len(positions),channels)


def fit(response,values,positions,policy):
    """Fixed-position nonnegative solve, then joint LM, using no source truth."""
    positions=np.asarray(positions,float);channels=values.shape[1]
    if not len(positions):return dict(positions=[],intensities=[],mse=float(np.mean(values**2)),nnls=True,lm_evaluations=0)
    a=response.matrix(positions)
    if not np.all(np.isfinite(a)):return None
    intensities=np.column_stack([nnls(a,values[:,c],maxiter=200)[0] for c in range(channels)])
    lo,hi=response.bounds;span=hi-lo
    unit=np.clip((positions-lo)/span,1e-5,1-1e-5)
    x0=np.r_[np.log(unit/(1-unit)).ravel(),np.log(np.maximum(intensities,1e-8)).ravel()]
    def unpack(x):
        xyz=lo+span/(1+np.exp(-np.clip(x[:len(positions)*3].reshape(-1,3),-30,30)))
        q=np.exp(np.clip(x[len(positions)*3:].reshape(-1,channels),-30,20))
        return xyz,q
    def residual(x):
        xyz,q=unpack(x)
        try:return (render(response,xyz,q,channels)-values).ravel()
        except ValueError:return np.full(values.size,1e3)
    optimized=least_squares(residual,x0,method='lm',max_nfev=policy['max_nonlinear_evaluations'],
                           ftol=1e-10,xtol=1e-10,gtol=1e-10,diff_step=policy['finite_difference_step'])
    xyz,q=unpack(optimized.x)
    # A failed/invalid optimizer is never interpreted as a black prediction.
    prediction=render(response,xyz,q,channels)
    return dict(positions=xyz.tolist(),intensities=q.tolist(),mse=float(np.mean((prediction-values)**2)),
                nnls=True,lm_evaluations=int(optimized.nfev),lm_success=bool(optimized.success))


def solve(response,values,policy,variant='full'):
    values=np.asarray(values,float);channels=values.shape[1]
    if np.min(values)<0 or not np.all(np.isfinite(values)):raise ValueError('Nonnegative finite linear observations required')
    field=Field(response,policy); initial=field.propose(values)
    history=[dict(stage='initial_votes',proposals=initial)]
    empty=dict(positions=[],intensities=[],mse=float(np.mean(values**2)),nnls=True,lm_evaluations=0)
    if variant=='proposal_only':
        return dict(state='ProposalsOnly',model=None,initial_proposals=initial,history=history,cost=cost(response,field))
    if variant=='one_pass_fit':
        model=fit(response,values,[c['position'] for c in initial[:policy['max_lights']]],policy) if initial else empty
        return dict(state='Converged' if model and model['mse']<=policy['mse_threshold'] else 'Partial',
                    model=model,initial_proposals=initial,history=history,cost=cost(response,field))
    working=[];q=np.empty((0,channels));best=empty;state='Budget'
    for iteration in range(policy['max_rounds']):
        prediction=render(response,working,q,channels)
        residual=values-prediction
        if np.min(residual)<-policy['negative_residual_tolerance']:
            state='NegativeResidualFailure';break
        proposals=initial if iteration==0 else field.propose(residual)
        active=[];new=0
        for c in proposals:
            distances=[np.linalg.norm(np.asarray(c['position'])-p) for p in working]
            if distances and min(distances)<policy['peak_merge_radius_fraction']*field.radius:
                active.append(int(np.argmin(distances)))
            elif len(working)<policy['max_lights']:
                working.append(c['position']);q=np.vstack((q,np.zeros((1,channels))));active.append(len(working)-1);new+=1
        history.append(dict(stage='discovery',iteration=iteration,proposals=len(proposals),added=new,working_sources=len(working)))
        if new:
            model=fit(response,values,working,policy)
            if model and model['mse']<best['mse']:best=model
            history.append(dict(stage='nnls_lm',iteration=iteration,new_sources=new,
                                mse=model['mse'] if model else None,nfev=model['lm_evaluations'] if model else None))
            if model and model['mse']<=policy['mse_threshold']:
                best=model;state='Converged';break
        if not active:
            state='NoRemainingVotePeaks';break
        q[sorted(set(active))]+=policy['intensity_increment']
        after_increment=values-render(response,working,q,channels)
        history.append(dict(stage='intensity_increment',iteration=iteration,active=sorted(set(active)),
                            minimum_remaining=float(np.min(after_increment))))
        if np.min(after_increment)<-policy['negative_residual_tolerance']:
            state='NegativeResidualFailure';break
        # Section 5.3: recompute votes with all OTHER lights removed.
        for j in range(len(working)):
            other=[p for i,p in enumerate(working) if i!=j]; other_q=np.delete(q,j,axis=0)
            one=values-render(response,other,other_q,channels)
            peaks=field.propose(one)
            if peaks:
                chosen=min(peaks,key=lambda c:np.linalg.norm(np.asarray(c['position'])-working[j]))
                old=working[j];working[j]=chosen['position']
                minimum=float(np.min(values-render(response,working,q,channels)))
                accepted=minimum>=-policy['negative_residual_tolerance']
                if not accepted:working[j]=old
                history.append(dict(stage='leave_one_out_relocation',iteration=iteration,source=j,old=old,
                                    proposed=chosen['position'],new=working[j],accepted=accepted,
                                    minimum_remaining=minimum))
        model=fit(response,values,working,policy)
        if model and model['mse']<best['mse']:best=model
        history.append(dict(stage='nnls_lm',iteration=iteration,new_sources=0,
                            mse=model['mse'] if model else None,nfev=model['lm_evaluations'] if model else None))
        if model and model['mse']<=policy['mse_threshold']:
            best=model;state='Converged';break
    return dict(state=state,model=best,initial_proposals=initial,history=history,cost=cost(response,field))


def cost(response,field):
    return dict(source_position_queries=response.queries,invalid_positions=response.invalid,
                unknown_coefficients=response.unknown,idf_transforms=field.transforms,
                idf_pair_visits=field.pair_visits,sphere_vertices=len(field.directions),sphere_triangles=len(field.faces),
                room_lattice_positions=len(field.positions),global_map_grid=False)
