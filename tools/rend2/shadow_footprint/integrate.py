"""Footprint-aware physical direct-light reference and explicitly sampled estimates.

SI fixtures: geometry in metres, point/spot cd, rectangle cd/m², sun perpendicular
illuminance lux. This does NOT calibrate BSP lightmaps or emulate their sampling.
Planar boundary clipping is exact geometry; smooth/finite-source quadrature has
an estimated numerical error, distinct from optical interval uncertainty.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass, asdict
from functools import lru_cache
import hashlib
import json
import math
import random
from geometry import (add,sub,mul,dot,cross,unit,length,area,intersect,subtract,ray_triangle)


@lru_cache(maxsize=32)
def gauss(n):
    if not 1<=n<=64: raise ValueError('Quadrature order budget')
    result=[]
    for i in range(n):
        z=math.cos(math.pi*(i+.75)/(n+.5))
        for _ in range(30):
            p,q=1.,0.
            for k in range(1,n+1): p,q=((2*k-1)*z*p-(k-1)*q)/k,p
            derivative=n*(z*p-q)/(z*z-1)
            new=z-p/derivative
            if abs(new-z)<1e-15: z=new; break
            z=new
        result.append(((1+z)/2,1/((1-z*z)*derivative*derivative)))
    return tuple(sorted(result))


@dataclass(frozen=True)
class Source:
    id: str = 'source'
    kind: str = 'point'
    position: tuple = (0.,0.,4.)
    rgb: tuple = (1.,1.,1.)
    axis: tuple = (0.,0.,-1.) # propagation, incoming is -axis
    right: tuple = (1.,0.,0.)
    up: tuple = (0.,-1.,0.)
    width: float = .6
    height: float = .4
    inner: float = .3 # radians
    outer: float = .6
    angular_radius: float = .02 # radians; uniform radiance cone
    def __post_init__(self):
        if any(len(p)!=3 for p in (self.position,self.rgb,self.axis,self.right,self.up)) or not all(math.isfinite(v) for p in (self.position,self.rgb,self.axis,self.right,self.up) for v in p): raise ValueError('Nonfinite/invalid source vectors')
        if not all(math.isfinite(v) for v in (self.width,self.height,self.inner,self.outer,self.angular_radius)): raise ValueError('Nonfinite source shape')
        if min(self.rgb)<0 or self.width<=0 or self.height<=0 or not 0<=self.inner<self.outer<math.pi/2 or not 0<=self.angular_radius<math.pi/4: raise ValueError('Invalid source dimensions/angles')
        if any(abs(dot(v,v)-1)>1e-8 for v in (self.axis,self.right,self.up)) or max(abs(dot(a,b)) for a,b in ((self.axis,self.right),(self.axis,self.up),(self.right,self.up)))>1e-8: raise ValueError('Orthonormal source frame required')
        if length(sub(cross(self.right,self.up),self.axis))>1e-8: raise ValueError('Right-handed source frame')
        if self.kind not in ('point','spot','rectangle','sun'): raise ValueError('Unsupported source kind')
    def key(self): return hashlib.sha256(json.dumps(asdict(self),sort_keys=True).encode()).hexdigest()


@dataclass(frozen=True)
class Node:
    position: tuple
    incoming: tuple # nonempty only for sun
    rgb: tuple
    emission_normal: tuple = ()
    spot: tuple = () # axis, cos(inner), cos(outer)
    measure: float = 1. # normalized source-area/solid-angle visibility measure, NOT RGB


def source_nodes(source,n):
    if source.kind in ('point','spot'):
        return (Node(source.position,(),source.rgb,(),(source.axis,math.cos(source.inner),math.cos(source.outer)) if source.kind=='spot' else ()),)
    if source.kind=='rectangle':
        return tuple(Node(add(source.position,add(mul(source.right,(u-.5)*source.width),mul(source.up,(v-.5)*source.height))),(),
                          mul(source.rgb,source.width*source.height*wu*wv),source.axis,(),wu*wv)
                     for u,wu in gauss(n) for v,wv in gauss(n))
    incoming=mul(source.axis,-1)
    if source.angular_radius==0: return (Node((),incoming,source.rgb),)
    # Uniform solid-angle cone. L=E_perp/(pi sin² alpha). No tangent-disk shortcut.
    c=math.cos(source.angular_radius); nodes=[]
    for u,w in gauss(n):
        mu=c+(1-c)*u; r=math.sqrt(max(0.,1-mu*mu))
        for j in range(n*4):
            phi=2*math.pi*(j+.5)/(n*4)
            d=add(mul(incoming,mu),add(mul(source.right,r*math.cos(phi)),mul(source.up,r*math.sin(phi))))
            nodes.append(Node((),d,mul(source.rgb,2*w/((1+c)*n*4)),(),(),w/(n*4)))
    return tuple(nodes)


@dataclass(frozen=True)
class Scene:
    triangles: tuple = ()
    unbounded_unknown: bool = False
    require_sky: bool = False
    # Fixture policy: explicit sky aperture is beyond ALL other caster vertices.
    # General non-enclosing sky/opaque ordering is unsupported by clipping here.
    sky_encloses_casters: bool = False
    identity: str = 'analytic-fixture-v1'

    def query(self,p,node,bias=0.,normal=(0.,0.,1.)):
        origin=add(p,mul(normal,bias))
        direction=node.incoming or unit(sub(node.position,origin))
        maximum=1e9 if node.incoming else length(sub(node.position,origin))-1e-9
        hits=[]
        coplanar=False
        for t in self.triangles:
            n=unit(cross(sub(t.world[1],t.world[0]),sub(t.world[2],t.world[0])))
            if abs(dot(n,direction))<1e-12 and abs(dot(n,sub(origin,t.world[0])))<1e-9:
                # Conservative coplanar segment/AABB overlap; no assumed transparency.
                near,far=0.,maximum
                for k in range(3):
                    lo=min(v[k] for v in t.world)-1e-9; hi=max(v[k] for v in t.world)+1e-9
                    if abs(direction[k])<1e-15:
                        if not lo<=origin[k]<=hi: far=-1
                    else:
                        x,y=sorted(((lo-origin[k])/direction[k],(hi-origin[k])/direction[k])); near=max(near,x); far=min(far,y)
                coplanar|=far>=near
            hit=ray_triangle(origin,direction,t,minimum=1e-9,maximum=maximum)
            if hit: hits.append((hit[0],t.optical))
        hits.sort(); uncertain=self.unbounded_unknown or coplanar; sky=False
        for _,optical in hits:
            if optical=='opaque': return 0.,0.
            if optical=='sky': sky=True; break
            uncertain=True
        if self.require_sky and not sky: uncertain=True
        return (0.,1.) if uncertain else (1.,1.)


class BudgetError(ValueError): pass
@dataclass
class Work:
    limit: int = 200000
    responses: int = 0
    rays: int = 0
    projections: int = 0
    pieces: int = 0
    def step(self,field,n=1):
        setattr(self,field,getattr(self,field)+n)
        if self.responses+self.rays+self.projections+self.pieces>self.limit: raise BudgetError('Explicit work budget exceeded')


def response(p,n,node):
    d=node.incoming or sub(node.position,p); r2=dot(d,d)
    if r2<=1e-20: raise ValueError('Singular source/receiver')
    incoming=node.incoming or mul(d,1/math.sqrt(r2))
    value=max(0.,dot(n,incoming))
    if not node.incoming: value/=r2
    if node.emission_normal: value*=max(0.,-dot(node.emission_normal,incoming))
    if node.spot:
        axis,inner,outer=node.spot; c=-dot(axis,incoming)
        t=max(0.,min(1.,(c-outer)/(inner-outer)))
        value*=t*t*(3-2*t)
    return mul(node.rgb,value)


def project(caster,receiver,node):
    """Project a convex triangle shadow into affine receiver UV; None=uncertain.

    Caster must lie between source and receiver. Crossing either plane is a
    topology event requiring a more general clipping algorithm, never transparent.
    """
    n=receiver.geometric_normal; anchor=receiver.world[0]; polygon=[]
    heights=[dot(sub(p,anchor),n) for p in caster.world]
    if max(abs(h) for h in heights)<=1e-10:
        tangent=abs(dot(node.incoming,n))<=1e-10 if node.incoming else abs(dot(sub(node.position,anchor),n))<=1e-10
        if tangent: return None # coplanar contact cannot certify transparent visibility
    if max(heights)<=1e-10: return ()
    if min(heights)<-1e-10: return None
    if node.incoming:
        denom=dot(node.incoming,n)
        if denom<=1e-10: return None
        polygon=[sub(p,mul(node.incoming,h/denom)) for p,h in zip(caster.world,heights)]
    else:
        source_height=dot(sub(node.position,anchor),n)
        if source_height<=1e-10: return None
        if min(heights)>=source_height-1e-10: return ()
        if max(heights)>=source_height-1e-10: return None
        polygon=[add(node.position,mul(sub(p,node.position),source_height/(source_height-h))) for p,h in zip(caster.world,heights)]
    return tuple(receiver.to_uv(p) for p in polygon)


def visible_pieces(piece,scene,node,work):
    opaque=[]; unknown=[]; skies=[]; unbounded=scene.unbounded_unknown; reasons=[]
    for caster in scene.triangles:
        work.step('projections'); shadow=project(caster,piece.triangle,node)
        if shadow is None:
            if caster.optical!='sky': unbounded=True; reasons.append('CasterCrossesProjectionPlane')
            continue
        if not shadow or area(shadow)<1e-18: continue
        if caster.optical=='opaque': opaque.append(shadow)
        elif caster.optical=='sky': skies.append(shadow)
        else: unknown.append(shadow); reasons.append(caster.optical)
    upper=[piece.polygon]
    for shadow in opaque:
        upper=[p for q in upper for p in subtract(q,shadow)]
        work.step('pieces',len(upper))
    lower=upper[:]
    if unbounded: lower=[]; reasons.append('UnboundedOrProjectionUncertainty')
    for shadow in unknown:
        lower=[p for q in lower for p in subtract(q,shadow)]
        work.step('pieces',len(lower))
    if scene.require_sky:
        if not scene.sky_encloses_casters: lower=[]; reasons.append('UnverifiedSkyOrdering')
        else:
            # Disjoint union of intersections: don't double count sky triangles.
            remainder=lower; admitted=[]
            for sky in skies:
                for q in remainder:
                    p=intersect(q,sky)
                    if area(p)>1e-18: admitted.append(p)
                remainder=[p for q in remainder for p in subtract(q,sky)]
            lower=admitted
            if sum(map(area,upper))-sum(map(area,lower))>1e-12: reasons.append('MissingSkyOrOpticalUncertainty')
    return lower,upper,reasons


def triangle_integral(triangle,poly,node,work,n):
    result=[0.,0.,0.]
    for k in range(1,len(poly)-1):
        a,b,c=poly[0],poly[k],poly[k+1]; determinant=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        for u,wu in gauss(n):
            for v,wv in gauss(n):
                uv=add(a,add(mul(sub(b,a),u),mul(sub(c,a),(1-u)*v)))
                p,normal=triangle.evaluate(uv); work.step('responses')
                value=response(p,normal,node); weight=determinant*(1-u)*wu*wv*triangle.jacobian
                for j in range(3): result[j]+=value[j]*weight
    return result


def smooth_integral(triangle,poly,node,work,tolerance,depth=0,max_depth=4):
    lo=triangle_integral(triangle,poly,node,work,2); hi=triangle_integral(triangle,poly,node,work,4)
    error=max(abs(a-b) for a,b in zip(lo,hi))
    if error<=tolerance or depth>=max_depth: return hi,error,depth>=max_depth and error>tolerance
    values=[0.,0.,0.]; total_error=0.; limited=False
    for k in range(1,len(poly)-1):
        a,b,c=poly[0],poly[k],poly[k+1]; ab=mul(add(a,b),.5); bc=mul(add(b,c),.5); ca=mul(add(c,a),.5)
        for p in ((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)):
            value,err,hit=smooth_integral(triangle,p,node,work,tolerance/(4*(len(poly)-2)),depth+1,max_depth)
            values=add(values,value); total_error+=err; limited|=hit
    return values,total_error,limited


def integrated(fp,source,scene,source_order=1,tolerance=1e-8,limit=200000,max_depth=4):
    """Boundary-aware planar reference. No source-dependent visibility cache.

    Integral includes visibility*response, not visibility(mean)*response(centre).
    Optical lower/upper are separate from quadrature error ESTIMATES, not proofs.
    Receiver discretization envelopes and finite-emitter convergence are caller-
    supplied model error; a single source quadrature order cannot bound them.
    """
    work=Work(limit); lower=[0.,0.,0.]; upper=[0.,0.,0.]; vl=vu=0.; error=0.; reasons=[]; limited=False
    if fp.world_area<=0: raise ValueError('No covered receiver area; no fabricated padding')
    nodes=source_nodes(source,source_order)
    for node in nodes:
        # Visibility is emitter-measure average, independent of irradiance response.
        # Rectangle Gauss weights / area; sun weights have constant common scale.
        measure=node.measure
        for piece in fp.pieces:
            lo,hi,why=visible_pieces(piece,scene,node,work); reasons.extend(why)
            vl+=sum(map(area,lo))*piece.triangle.jacobian*measure
            vu+=sum(map(area,hi))*piece.triangle.jacobian*measure
            identical=lo==hi
            for polygons,target in ((lo,lower),) if identical else ((lo,lower),(hi,upper)):
                for poly in polygons:
                    value,err,hit=smooth_integral(piece.triangle,poly,node,work,tolerance*fp.world_area/max(1,len(nodes)*len(fp.pieces)),max_depth=max_depth)
                    target[:]=add(target,value); error+=err; limited|=hit
                    if identical: upper[:]=add(upper,value)
    A=fp.world_area
    result=dict(status='NumericalLimit' if limited else 'PlanarBoundaryReference',observation=fp.id,source=source.id,
        source_key=source.key(),scene=scene.identity,profile='PhysicalSI-Direct-v1',unit='lux',
        contribution_lower=mul(lower,1/A),contribution_upper=mul(upper,1/A),
        visibility_lower=vl/A,visibility_upper=vu/A,numerical_error_estimate=error/A,
        numerical_error_is_certified_bound=False,geometry_uncertainty=sorted(set(reasons)),
        differentiability='Piecewise; boundary/topology events require branch checks',jacobian=None,
        work=asdict(work),source_order=source_order,finite_source_error='Unknown: compare independent orders',
        geometry_confidence=None) # no heuristic probability
    if fp.geometry_envelope:
        result.update(status='ReceiverGeometryApproximation',receiver_geometry_envelope=fp.geometry_envelope,
            approximate_contribution=result['contribution_upper'],approximate_visibility=result['visibility_upper'],
            contribution_lower=(0.,0.,0.),contribution_upper=None,visibility_lower=0.,visibility_upper=1.)
        result['geometry_uncertainty'].append('CurvedReceiverEnvelope; numerical estimates apply to mesh only')
    return result


def halton(i,base):
    f=1.; value=0.
    while i: f/=base; value+=f*(i%base); i//=base
    return value


def sampled(fp,source,scene,n=4,method='grid',seed=507,source_order=1,bias=0.,limit=200000):
    if not 1<=n<=256: raise ValueError('Sample budget')
    work=Work(limit); a,b=fp.bounds; total=0.; lo=[0.,0.,0.]; hi=[0.,0.,0.]; vl=vu=0.
    rng=random.Random(seed); shifts=(rng.random(),rng.random()); nodes=source_nodes(source,source_order)
    for i in range(n*n):
        if method=='centre': u=v=.5
        elif method=='halton': u,v=(halton(i+1,2)+shifts[0])%1,(halton(i+1,3)+shifts[1])%1
        elif method=='stratified': u,v=((i%n)+rng.random())/n,((i//n)+rng.random())/n
        elif method=='grid': u,v=((i%n)+.5)/n,((i//n)+.5)/n
        else: raise ValueError('Unknown approximation')
        uv=(a[0]+u*(b[0]-a[0]),a[1]+v*(b[1]-a[1]))
        piece=next((p for p in fp.pieces if min(p.triangle.bary(uv))>=-1e-12 and
                    all((q[0]-z[0])*(uv[1]-z[1])-(q[1]-z[1])*(uv[0]-z[0])>=-1e-12 for z,q in zip(p.polygon,p.polygon[1:]+p.polygon[:1]))),None)
        if piece is None: continue
        p,normal=piece.triangle.evaluate(uv); weight=piece.triangle.jacobian; total+=weight
        for node in nodes:
            work.step('rays'); x,y=scene.query(p,node,bias,piece.triangle.geometric_normal)
            work.step('responses'); value=response(p,normal,node)
            lo=add(lo,mul(value,x*weight)); hi=add(hi,mul(value,y*weight))
            measure=node.measure
            vl+=weight*x*measure; vu+=weight*y*measure
    if total==0: return dict(status='SamplingMiss',available=False,work=asdict(work))
    return dict(status='SampledApproximation',available=True,observation=fp.id,source=source.id,source_key=source.key(),
        contribution_lower=mul(lo,1/total),contribution_upper=mul(hi,1/total),visibility_lower=vl/total,visibility_upper=vu/total,
        numerical_error_estimate=None,geometry_confidence=None,work=asdict(work),jacobian=None,
        differentiability='Discrete binary visibility; deterministic does NOT mean correct derivative')
