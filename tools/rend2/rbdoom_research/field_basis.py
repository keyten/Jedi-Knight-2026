#!/usr/bin/env python3
"""Independent SH/SG field experiments using generic Legendre recurrences.

Not copied RBDOOM code, an engine bake, or inverse emitter localization. All
inputs are declared synthetic relative-linear RGB distributions. No SDK/assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'shadow_footprint'))
from integrate import gauss


def unit(p):
    if len(p)!=3 or not all(math.isfinite(x) for x in p): raise ValueError('Invalid direction')
    n=math.sqrt(sum(x*x for x in p))
    if n<1e-12: raise ValueError('Zero direction')
    return tuple(x/n for x in p)


def dot(a,b): return sum(x*y for x,y in zip(a,b))


def legendre(l,x):
    if l==0: return 1.
    a,b=1.,x
    for k in range(2,l+1): a,b=b,((2*k-1)*x*b-(k-1)*a)/k
    return b


@lru_cache(maxsize=16384)
def basis(direction,L):
    if not isinstance(L,int) or not 0<=L<=8: raise ValueError('SH degree budget')
    if len(direction)!=3 or not all(math.isfinite(x) for x in direction) or abs(dot(direction,direction)-1)>1e-9:
        raise ValueError('Unit finite direction required')
    x,y,z=direction; phi=math.atan2(y,x); out=[0.]*(L+1)**2
    # Real orthonormal Condon-Shortley basis; index l*l+l+m. Generic
    # recurrence, not the reference project's hand-expanded polynomials.
    for m in range(L+1):
        pmm=1.
        for k in range(1,m+1): pmm*=-(2*k-1)*math.sqrt(max(0.,1-z*z))
        prev=0.; current=pmm
        for l in range(m,L+1):
            if l>m:
                prev,current=current,((2*l-1)*z*current-(l+m-1)*prev)/(l-m)
            norm=math.sqrt((2*l+1)/(4*math.pi)*math.factorial(l-m)/math.factorial(l+m))
            if m==0: out[l*l+l]=norm*current
            else:
                out[l*l+l-m]=math.sqrt(2)*norm*current*math.sin(m*phi)
                out[l*l+l+m]=math.sqrt(2)*norm*current*math.cos(m*phi)
    return tuple(out)


def sphere_grid(n=32):
    if not 4<=n<=64: raise ValueError('Quadrature budget')
    return [( (math.sqrt(max(0.,1-z*z))*math.cos(phi),math.sqrt(max(0.,1-z*z))*math.sin(phi),z),
              w*2*math.pi/n)
            for u,w in gauss(n) for j in range(2*n)
            for z,phi in [(2*u-1,2*math.pi*(j+.5)/(2*n))]]


def directions(n=257):
    if not 1<=n<=1024: raise ValueError('Direction budget')
    return [unit((math.sqrt(1-z*z)*math.cos(phi),math.sqrt(1-z*z)*math.sin(phi),z))
            for i in range(n) for z,phi in [(1-2*(i+.5)/n,i*math.pi*(3-math.sqrt(5)))]]


def factors(L):
    # 2*pi*integral_0^1 u P_l(u) du; Gauss integrates these polynomials exactly
    # up to floating error. A_l are factors for true E, not E/pi.
    return [2*math.pi*sum(w*u*legendre(l,u) for u,w in gauss(16)) for l in range(L+1)]


def evaluate(coeff,direction,L,diffuse=False):
    if len(coeff)!=(L+1)**2: raise ValueError('Coefficient span')
    Y=basis(direction,L); A=factors(L) if diffuse else [1.]*(L+1)
    return tuple(sum(coeff[l*l+l+m][c]*Y[l*l+l+m]*A[l]
                     for l in range(L+1) for m in range(-l,l+1)) for c in range(3))


def sg(direction,axis,sharpness):
    if not math.isfinite(sharpness) or not 0<sharpness<=256: raise ValueError('SG sharpness')
    # Unit integral over the sphere. The amplitude below is integrated radiance,
    # not candela of a physical light and not SG peak radiance.
    norm=sharpness/(-2*math.pi*math.expm1(-2*sharpness))
    return norm*math.exp(sharpness*(dot(direction,axis)-1))


LOBES=((unit((.2,.1,1.)),(1.,.03,.01)),(unit((-.9,.4,.1)),(.01,.08,.8)),
       (unit((.2,-.9,.2)),(.05,.6,.02)))


def distribution(direction,sharpness=32.):
    return tuple(.015+sum(rgb[c]*sg(direction,axis,sharpness) for axis,rgb in LOBES) for c in range(3))


def bessel_i0(x):
    if not math.isfinite(x) or abs(x)>64: raise ValueError('Reference Bessel domain')
    total=term=1.; factor=x*x/4
    for k in range(1,256):
        term*=factor/(k*k); total+=term
        if term<1e-16*total: return total
    raise ValueError('Reference series budget')


def sg_irradiance(normal,axis,sharpness=32.,order=48):
    # Independent hemisphere integral, with azimuth integrated analytically:
    # E=2*pi*N*exp(-lambda) integral_0^1 u exp(lambda*u*cosBeta)
    #                       I0(lambda*sqrt(1-u²)*sinBeta) du.
    # Unlike a full-sphere max(0,n.w) quadrature, this has no clipping crease.
    if not 0<sharpness<=64: raise ValueError('Reference sharpness budget')
    cosine=max(-1.,min(1.,dot(normal,axis))); sine=math.sqrt(max(0.,1-cosine*cosine))
    norm=sharpness/(-math.expm1(-2*sharpness))
    return norm*sum(w*u*math.exp(sharpness*(u*cosine-1))*
        bessel_i0(sharpness*math.sqrt(1-u*u)*sine) for u,w in gauss(order))


def irradiance_reference(normal,order=48):
    terms=[(sg_irradiance(normal,axis,32.,order),rgb) for axis,rgb in LOBES]
    return tuple(math.pi*.015+sum(v*rgb[c] for v,rgb in terms) for c in range(3))


def project(samples,L=8):
    coeff=[[0.]*3 for _ in range((L+1)**2)]
    for direction,weight,rgb in samples:
        if weight<0 or not math.isfinite(weight) or len(rgb)!=3 or not all(math.isfinite(x) for x in rgb):
            raise ValueError('Invalid projection sample')
        for i,Y in enumerate(basis(direction,L)):
            for c in range(3): coeff[i][c]+=weight*rgb[c]*Y
    return coeff


def errors(reference,predicted):
    if len(reference)!=len(predicted) or not reference: raise ValueError('Metric span')
    truth=[v for row in reference for v in row]; pred=[v for row in predicted for v in row]
    mse=sum((a-b)**2 for a,b in zip(truth,pred))/len(truth)
    energy=sum(v*v for v in truth)/len(truth)
    return dict(rmse=math.sqrt(mse),relative_rms=math.sqrt(mse/energy),
        minimum=min(pred),negative_channels=sum(v<0 for v in pred),channels=len(pred))


def compare():
    nodes=sphere_grid(40); held=directions(257)
    samples=[(d,w,distribution(d)) for d,w in nodes]
    coeff=project(samples); truthL=[distribution(d) for d in held]
    truthE=[irradiance_reference(n) for n in held]
    # Independent resolution check on a subset, not a certified error bound.
    fineE=[irradiance_reference(n,64) for n in held[::16]]
    coarseE=truthE[::16]
    rows=[]
    for L in (1,2,3,4,8):
        count=(L+1)**2; raw=coeff[:count]
        predL=[evaluate(raw,d,L) for d in held]; predE=[evaluate(raw,d,L,True) for d in held]
        clamped=[tuple(max(0.,v) for v in row) for row in predE]
        half=[[struct.unpack('<e',struct.pack('<e',v))[0] for v in row] for row in raw]
        halfE=[evaluate(half,d,L,True) for d in held]
        rows.append(dict(degree=L,coefficients_rgb=count,fp32_bytes=count*3*4,
            radiance=errors(truthL,predL),irradiance=errors(truthE,predE),
            clamped_irradiance=errors(truthE,clamped),
            fp16_coefficient_extra_rmse=errors(predE,halfE)['rmse']))
    # By-construction capacity control, NOT an estimated lighting model or a fair
    # unknown-shape optimizer comparison: the generating SG axes/widths are known.
    oracle=dict(method='Three known SG lobes plus known constant; oracle capacity control',
        radiance=errors(truthL,[distribution(d) for d in held]),
        stored_float_parameters=3*(2+1+3)+3,
        fitted_unknown_geometry=False)
    return dict(input='Three distinct coloured SG incoming-radiance lobes, lambda=32, plus constant .015',
        nodes=len(nodes),heldout_directions=len(held),rows=rows,sg_oracle=oracle,
        reference_48_vs_64_max_irradiance_difference=max(abs(a-b) for row,other in zip(coarseE,fineE) for a,b in zip(row,other)))


def delta_control():
    # The exact incident angular delta has finite integrated energy; its pointwise
    # radiance RMS is undefined. Only the convolved E is compared here.
    axis=(0.,0.,1.); Y=basis(axis,8); coeff=[(v,v,v) for v in Y]
    held=directions(513); truth=[(max(0.,d[2]),)*3 for d in held]
    rows=[]
    for L in (1,2,3,4,8):
        pred=[evaluate(coeff[:(L+1)**2],d,L,True) for d in held]
        rows.append(dict(degree=L,metrics=errors(truth,pred)))
    return rows


def validity_control():
    # Linear scalar reduction of the bright/dark boundary: brightness is NOT a
    # validity bit. This does not simulate the whole reference renderer shader.
    values=(0.,100.); weights=(.5,.5); explicit_valid=(True,True)
    correct=sum(v*w for v,w,valid in zip(values,weights,explicit_valid) if valid)/sum(weights)
    chosen=[(v,w) for v,w in zip(values,weights) if 3*v>=.0001]
    brightness_filter=sum(v*w for v,w in chosen)/sum(w for _,w in chosen)
    return dict(values=values,explicit_valid=explicit_valid,proper_linear_interpolation=correct,
        reject_black_and_renormalize=brightness_filter,
        no_valid_support=None,reason='Black is a valid physical value; absent/invalid must have separate support status')


def run():
    return dict(schema='org.openjk.rbdoom-field-research',version=1,
        reference_commit='ea29c006e84fedcc0e7c173c383a087ce0a8c0d5',
        units='Synthetic relative-linear RGB incoming radiance / derived irradiance; NOT stock-map SI calibration',
        convention='Real orthonormal Condon-Shortley, z polar, index=l*l+l+m; incoming receiver-to-source direction',
        comparison=compare(),delta_irradiance=delta_control(),validity=validity_control(),
        diffuse_factors=factors(8),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=HERE/'results.json')
    args=p.parse_args(); result=run();args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')
    for row in result['comparison']['rows']:
        print('L',row['degree'],'RGB coeffs',row['coefficients_rgb'],'radiance rel RMS',row['radiance']['relative_rms'],
              'irradiance rel RMS',row['irradiance']['relative_rms'])
