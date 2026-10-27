"""Independent closed-form rectangle solid angle and derivative witnesses.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math


class Dual:
    def __init__(self,value,gradient=(0.,0.,0.)): self.v=float(value); self.g=tuple(gradient)
    def __add__(self,x):
        x=dual(x); return Dual(self.v+x.v,tuple(a+b for a,b in zip(self.g,x.g)))
    __radd__=__add__
    def __neg__(self): return Dual(-self.v,tuple(-x for x in self.g))
    def __sub__(self,x): return self+-dual(x)
    def __rsub__(self,x): return dual(x)+-self
    def __mul__(self,x):
        x=dual(x); return Dual(self.v*x.v,tuple(a*x.v+b*self.v for a,b in zip(self.g,x.g)))
    __rmul__=__mul__
    def __pow__(self,p): return Dual(self.v**p,tuple(p*self.v**(p-1)*a for a in self.g))
    def __truediv__(self,x): return self*dual(x)**-1
    def __rtruediv__(self,x): return dual(x)*self**-1


def dual(x): return x if isinstance(x,Dual) else Dual(x)
def atan2(y,x):
    y,x=dual(y),dual(x)
    return Dual(math.atan2(y.v,x.v),tuple((x.v*a-y.v*b)/(x.v*x.v+y.v*y.v) for a,b in zip(y.g,x.g)))
def sin(x):
    x=dual(x); return Dual(math.sin(x.v),tuple(math.cos(x.v)*a for a in x.g))
def cos(x):
    x=dual(x); return Dual(math.cos(x.v),tuple(-math.sin(x.v)*a for a in x.g))
def variables(x): return [Dual(v,tuple(float(i==j) for j in range(3))) for i,v in enumerate(x)]


def rectangle_reference(bounds,source,intensity=1.,wall=(1.,1.)):
    """I*Omega/A exact area-averaged irradiance, floor z=0, infinite straight wall.

    Footprint entirely beyond vertical wall x=w, source on its left and above top.
    Shadow visibility boundary b=(H*w-h*sx)/(H-h). Boundary clamp is nonsmooth at
    footprint ends. AD differentiates the CLOSED FORM incl. moving integration
    limit; not a frozen sample visibility gradient.
    """
    x0,x1,y0,y1=bounds; sx,sy,H=variables(source); lower=Dual(x0)
    if min(x1-x0,y1-y0,H.v)<=0: raise ValueError('Reference geometry')
    boundary=None
    if wall:
        w,h=wall
        if not (H.v>h>0 and sx.v<w<=x0): raise ValueError('Ordered wall/source/receiver')
        boundary=(H*w-h*sx)/(H-h)
        if boundary.v>=x1: return dict(value=0.,visibility=0.,jacobian=(0.,0.,0.),visibility_jacobian=(0.,0.,0.),boundary=boundary.v)
        if boundary.v>x0: lower=boundary
    def corner(x,y):
        x=x-sx; y=y-sy
        return atan2(x*y,H*(H*H+x*x+y*y)**.5)
    omega=corner(x1,y1)-corner(lower,y1)-corner(x1,y0)+corner(lower,y0)
    value=intensity*omega/((x1-x0)*(y1-y0)); vis=(x1-lower)/(x1-x0)
    return dict(value=value.v,visibility=vis.v,jacobian=value.g,visibility_jacobian=vis.g,
                boundary=None if boundary is None else boundary.v)


def finite_rectangle_visibility(bounds,position,width,wall=(1.,1.)):
    """Exact uniform-emitter area average visibility (NOT irradiance weighting)."""
    lo,hi=bounds[:2]; sx,_,H=position; w,h=wall
    if width<=0 or not H>h>0: raise ValueError('Finite source geometry')
    a=(H*w-h*(sx-width/2))/(H-h); b=(H*w-h*(sx+width/2))/(H-h)
    # Average clamp((hi-edge)/(hi-lo),0,1) over edge interval [b,a].
    def primitive(t):
        if t<=lo: return t
        if t>=hi: return (hi+lo)/2
        return lo+(hi*(t-lo)-(t*t-lo*lo)/2)/(hi-lo)
    return (primitive(a)-primitive(b))/(a-b)


def smooth_point_ad(source,receiver,normal=(0.,0.,1.)):
    s=variables(source); d=[a-b for a,b in zip(s,receiver)]; r2=sum(a*a for a in d); cosine=sum(a*b for a,b in zip(d,normal))
    if cosine.v<=0: return 0.,(0.,0.,0.)
    value=cosine*r2**-1.5
    return value.v,value.g


def frozen_visibility_derivative(bounds,source,wall=(1.,1.),n=32):
    """Differentiate only smooth response at fixed binary point masks; misses edge."""
    x0,x1,y0,y1=bounds; total=[0.,0.,0.]
    b=rectangle_reference(bounds,source,wall=wall)['boundary']
    for i in range(n):
        for j in range(n):
            x=x0+(i+.5)*(x1-x0)/n; y=y0+(j+.5)*(y1-y0)/n
            _,g=smooth_point_ad(source,(x,y,0.))
            if x>=b: total=[a+v/(n*n) for a,v in zip(total,g)]
    return total


def spot_orientation_ad(source,receiver,theta,inner=.3,outer=.6):
    """Smooth angular factor AD. Axis rotated about Y; mask/pose are fixed here."""
    d=[a-b for a,b in zip(source,receiver)]; r=math.sqrt(sum(v*v for v in d)); incoming=[v/r for v in d]
    a=Dual(theta,(1.,0.,0.)); axis=(-sin(a),Dual(0),-cos(a))
    c=-sum(v*w for v,w in zip(axis,incoming))
    t=(c-math.cos(outer))/(math.cos(inner)-math.cos(outer))
    if t.v<=0: return 0.,0.
    if t.v>=1: return smooth_point_ad(source,receiver)[0],0.
    value=t*t*(3-2*t)*smooth_point_ad(source,receiver)[0]
    return value.v,value.g[0]
