"""Original-UV footprint pieces and tiny independent reference geometry.

Offline reference only. The production BVH remains lightrecon/visibility.cpp.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass
import math


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def length(a): return math.sqrt(dot(a, a))
def unit(a):
    size=length(a)
    if not math.isfinite(size) or size<1e-14: raise ValueError('Degenerate/nonfinite vector')
    return mul(a, 1/size)
def wedge(a, b): return a[0]*b[1]-a[1]*b[0]
def signed_area(p): return sum(wedge(a,b) for a,b in zip(p,p[1:]+p[:1]))/2 if p else 0.
def area(p): return abs(signed_area(p))


def clip(polygon, a, b, inside=True):
    """Convex polygon clipping, inside = left of oriented edge (no edge padding)."""
    if not polygon: return ()
    result=[]
    sign=1 if inside else -1
    for p,q in zip(polygon,polygon[1:]+polygon[:1]):
        x=sign*wedge(sub(b,a),sub(p,a)); y=sign*wedge(sub(b,a),sub(q,a))
        if x>=0: result.append(p)
        if (x<0)!=(y<0): result.append(add(p,mul(sub(q,p),x/(x-y))))
    return tuple(result)


def intersect(p, q):
    if signed_area(q)<0: q=tuple(reversed(q))
    for a,b in zip(q,q[1:]+q[:1]): p=clip(p,a,b)
    return p


def subtract(p, q):
    """Disjoint convex pieces of p minus convex q; shared edges have zero area."""
    if signed_area(q)<0: q=tuple(reversed(q))
    result=[]
    for a,b in zip(q,q[1:]+q[:1]):
        outside=clip(p,a,b,False)
        if area(outside)>1e-18: result.append(outside)
        p=clip(p,a,b)
        if not p: break
    return result


@dataclass(frozen=True)
class Triangle:
    id: str
    surface: int
    world: tuple
    uv: tuple
    normals: tuple
    optical: str = 'opaque'  # opaque, sky, alpha_unknown, translucent, moving, unsupported

    def __post_init__(self):
        if len(self.world)!=3 or len(self.uv)!=3 or len(self.normals)!=3: raise ValueError('Triangle arity')
        if not all(math.isfinite(v) for group in (self.world,self.uv,self.normals) for p in group for v in p): raise ValueError('Nonfinite triangle')
        if abs(self.det)<1e-14 or length(cross(sub(self.world[1],self.world[0]),sub(self.world[2],self.world[0])))<1e-14:
            raise ValueError('Degenerate UV/world triangle')

    @property
    def det(self): return wedge(sub(self.uv[1],self.uv[0]),sub(self.uv[2],self.uv[0]))
    def bary(self, uv):
        e,f,q=sub(self.uv[1],self.uv[0]),sub(self.uv[2],self.uv[0]),sub(uv,self.uv[0])
        s,t=wedge(q,f)/self.det,wedge(e,q)/self.det
        return (1-s-t,s,t)
    def evaluate(self, uv):
        w=self.bary(uv)
        p=tuple(sum(w[j]*self.world[j][i] for j in range(3)) for i in range(3))
        n=unit(tuple(sum(w[j]*self.normals[j][i] for j in range(3)) for i in range(3)))
        return p,n
    @property
    def geometric_normal(self):
        n=unit(cross(sub(self.world[1],self.world[0]),sub(self.world[2],self.world[0])))
        return n if dot(n,self.normals[0])>=0 else mul(n,-1)
    @property
    def jacobian(self): return length(cross(sub(self.world[1],self.world[0]),sub(self.world[2],self.world[0])))/abs(self.det)
    def to_uv(self,p):
        e,f,q=sub(self.world[1],self.world[0]),sub(self.world[2],self.world[0]),sub(p,self.world[0])
        d=dot(e,e)*dot(f,f)-dot(e,f)**2
        s=(dot(q,e)*dot(f,f)-dot(q,f)*dot(e,f))/d
        t=(dot(q,f)*dot(e,e)-dot(q,e)*dot(e,f))/d
        return add(self.uv[0],add(mul(sub(self.uv[1],self.uv[0]),s),mul(sub(self.uv[2],self.uv[0]),t)))


@dataclass(frozen=True)
class Piece:
    triangle: Triangle
    polygon: tuple
    @property
    def world_area(self): return area(self.polygon)*self.triangle.jacobian


@dataclass(frozen=True)
class Footprint:
    id: str
    pieces: tuple
    coverage: float
    issues: tuple = ()
    bounds: tuple = ((0.,0.),(1.,1.))
    geometry_envelope: float = 0. # nonzero => actual curved receiver is not certified by this mesh
    @property
    def world_area(self): return sum(p.world_area for p in self.pieces)


def footprint(identifier, triangles, box, max_pieces=256):
    """Same original normalized-UV clipping convention as surfaces.cpp MapLuxels.

    Actual UV geometry determines area/shape. Unsupported/overlapping charts do
    not get invented positions. Duplicated geometry is one energy observation.
    Different triangle normals remain per-piece, not flattened to one normal.
    """
    lo,hi=box
    if not all(math.isfinite(v) for p in box for v in p) or min(hi[i]-lo[i] for i in range(2))<=0: raise ValueError('Degenerate footprint')
    bounds=(lo,(hi[0],lo[1]),hi,(lo[0],hi[1])); pieces=[]; seen={}; issues=[]
    for t in triangles:
        key=tuple(sorted((*p,*uv) for p,uv in zip(t.world,t.uv)))
        if key in seen:
            other=seen[key]
            old=sorted((*p,*uv,*n) for p,uv,n in zip(other.world,other.uv,other.normals))
            new=sorted((*p,*uv,*n) for p,uv,n in zip(t.world,t.uv,t.normals))
            if old!=new or t.optical!=other.optical: raise ValueError('Conflicting duplicate normal/material reference')
            issues.append('DuplicateReference'); continue
        seen[key]=t
        poly=intersect(t.uv,bounds)
        if area(poly)<1e-18: continue
        if signed_area(poly)<0: poly=tuple(reversed(poly))
        if any(area(intersect(poly,p.polygon))>area(bounds)*1e-9 for p in pieces):
            raise ValueError('Ambiguous overlapping UV charts')
        pieces.append(Piece(t,poly))
        if len(pieces)>max_pieces: raise ValueError('Footprint piece budget')
    coverage=sum(area(p.polygon) for p in pieces)/area(bounds)
    if not pieces: issues.append('Padding')
    elif coverage<1-1e-8: issues.append('PartialCoverage')
    return Footprint(identifier,tuple(pieces),coverage,tuple(sorted(set(issues))),box)


def rectangle(x0,x1,y0,y1,z=0.,identifier='receiver'):
    p=((x0,y0,z),(x1,y0,z),(x1,y1,z),(x0,y1,z)); uv=((0.,0.),(1.,0.),(1.,1.),(0.,1.))
    triangles=tuple(Triangle(identifier+str(j),1,tuple(p[i] for i in ids),tuple(uv[i] for i in ids),((0.,0.,1.),)*3)
                    for j,ids in enumerate(((0,1,2),(0,2,3))))
    return footprint(identifier,triangles,((0.,0.),(1.,1.)))


def patch_mesh(controls, n=8):
    """Quadratic receiver approximation; affine UV only, explicit hull deviation.

    A flat mesh is NOT exact curved visibility. Returns a conservative geometric
    envelope using the Bezier control hull versus each cell's bilinear corners.
    The caller carries that envelope into uncertainty, not a confidence product.
    """
    if len(controls)!=9 or n<1 or n>128: raise ValueError('Patch bounds')
    def evaluate(u,v):
        bu=((1-u)**2,2*u*(1-u),u*u); bv=((1-v)**2,2*v*(1-v),v*v)
        du=(-2*(1-u),2-4*u,2*u); dv=(-2*(1-v),2-4*v,2*v)
        p=tuple(sum(controls[3*y+x][i]*bu[x]*bv[y] for y in range(3) for x in range(3)) for i in range(3))
        a=tuple(sum(controls[3*y+x][i]*du[x]*bv[y] for y in range(3) for x in range(3)) for i in range(3))
        b=tuple(sum(controls[3*y+x][i]*bu[x]*dv[y] for y in range(3) for x in range(3)) for i in range(3))
        return p,unit(cross(a,b))
    # Bound second derivatives from the control hull, including the diagonal
    # bilinear-to-triangle term. Deliberately loose, finite, scale-aware.
    second=max(length(mul(add(sub(controls[j],mul(controls[j+1],2)),controls[j+2]),2)) for j in (0,3,6))
    second+=max(length(mul(add(sub(controls[j],mul(controls[j+3],2)),controls[j+6]),2)) for j in (0,1,2))
    mixed=max(length(mul(add(sub(controls[3*y+x],controls[3*y+x+1]),sub(controls[3*(y+1)+x+1],controls[3*(y+1)+x])),4)) for y in (0,1) for x in (0,1))
    error=(second+2*mixed)/(n*n)
    triangles=[]
    for y in range(n):
        for x in range(n):
            uv=((x/n,y/n),((x+1)/n,y/n),((x+1)/n,(y+1)/n),(x/n,(y+1)/n))
            for k,ids in enumerate(((0,1,2),(0,2,3))):
                samples=[evaluate(*uv[i]) for i in ids]
                triangles.append(Triangle(f'patch/{x}/{y}/{k}',1,tuple(s[0] for s in samples),tuple(uv[i] for i in ids),tuple(s[1] for s in samples)))
    return tuple(triangles),error


def ray_triangle(origin,direction,t,bary_epsilon=0.,minimum=1e-8,maximum=1e9,det_epsilon=1e-12):
    """Independent Moller/Trumbore reference; optional compiler epsilon replay.

    This tiny fixture routine is not a replacement for production BVH queries.
    Direction is unit receiver->source. No culling, no hard-coded lightmap push.
    """
    e,f=sub(t.world[1],t.world[0]),sub(t.world[2],t.world[0]); p=cross(direction,f); d=dot(e,p)
    if abs(d)<det_epsilon: return None
    q=sub(origin,t.world[0]); u=dot(q,p)/d; c=cross(q,e); v=dot(direction,c)/d
    depth=dot(f,c)/d
    if u < -bary_epsilon or u>1+bary_epsilon or v < -bary_epsilon or u+v>1+bary_epsilon or not minimum<depth<maximum: return None
    return depth,u,v
