"""Exact gauges and declared analytic countermodels, not historical bake replay.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
import random
from bake_model import grid_reduce, dot


def rmse(a,b): return math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))


def analytic_experiments():
    k=2250000.; centre=240.
    slope=2*k/centre**3; intercept=3*k/centre**2
    narrow=[centre+x/4 for x in range(-8,9)]; broad=list(range(96,481,16))
    falloff={name:dict(inverse_square=[k/r**2 for r in values],
        tangent_linear=[max(0.,intercept-slope*r) for r in values],
        rmse=rmse([k/r**2 for r in values],[max(0.,intercept-slope*r) for r in values]))
        for name,values in [('narrow',narrow),('broad',broad)]}
    height={}
    for extent in (8,240):
        rho=list(range(-extent,extent+1,max(1,extent//8)))
        one=[300*7500*192/(192**2+r*r)**1.5 for r in rho]
        farther=[1200*7500*384/(384**2+r*r)**1.5 for r in rho]
        height[str(extent)]=dict(rmse=rmse(one,farther),source_heights=[192,384],source_intensities=[300,1200])
    x=list(range(-12,13)); sigma=4.
    blur=[]
    for source,filtering in ((0.,4.),(math.sqrt(12),2.),(4.,0.)):
        effective=math.hypot(source,filtering)
        curve=[.5*(1+math.erf(v/(math.sqrt(2)*effective))) for v in x]
        blur.append(dict(source_sigma=source,filter_sigma=filtering,curve=curve))
    albedo=[.1,.3,.7,1.]; intensity=300.
    gauge=max(abs(rho*intensity-(rho/2)*(intensity*2)) for rho in albedo)
    # Ordered compression is not replayed; the specified single-pass reducer is.
    grid=[]
    for value in (20.,50.,100.,200.):
        c=[value]*3
        a,d,u=grid_reduce([(c,[0,0,1])],directed_leak=.125)
        other,_,_=grid_reduce([(c,[0,0,1])],directed_leak=.25)
        grid.append(dict(directed=d[0],ambient_a=a[0],ambient_b_with_half_ambient_gain=other[0]*.5))
    gamma=2.2; scale=2.; I=40.; compensation=1.
    y=255*(I/255)**(1/gamma)/compensation
    alternate=255*((I*scale**gamma)/255)**(1/gamma)/(compensation*scale)
    return dict(schema='org.openjk.bake-confounding.v1',
        linear_inverse_square_tangent=falloff,tangent_nuisance=dict(global_gain=slope,linear_authored_scalar=(intercept/slope)/.9375),
        distance_intensity=height,albedo_intensity_exact_error=gauge,
        albedo_scope='Only if the observation contains rho*E; NOT asserted for every JA lightmap',
        gamma_compensation_gauge_error=abs(y-alternate),
        sun_rgb_normalization_amplitude_gauge_error=abs(120-(120*math.sqrt(3))/math.sqrt(3)),
        gaussian_shadow_filter_extent=blur,
        shadow_scope='Analytic Gaussian convolution counterexample, NOT a q3map filter claim',
        grid_calibration_gauge=grid,
        unknown_count_coincident_inverse_sources='P1/r^2 + P2/r^2 == (P1+P2)/r^2 away from per-source cutoffs',
        confidence='Exact gauges are algebra; no posterior probabilities inferred')


def paired_bootstrap(a,b,seed=20261027,repetitions=128):
    keys=sorted(set(a)&set(b))
    if len(keys)<3: return dict(status='InsufficientIndependentBlocks',blocks=len(keys))
    values=[a[k]-b[k] for k in keys]; randomizer=random.Random(seed)
    means=sorted(sum(values[randomizer.randrange(len(values))] for _ in values)/len(values) for _ in range(repetitions))
    return dict(status='PairedSpatialBlockResampling',blocks=len(values),mean_difference=sum(values)/len(values),
        percentile_5=means[int(.05*repetitions)],percentile_95=means[int(.95*repetitions)],
        fraction_below_zero=sum(v<0 for v in means)/repetitions,
        interpretation='Sampling stability under exchangeable blocks; NOT a posterior or a calibrated stock confidence')
