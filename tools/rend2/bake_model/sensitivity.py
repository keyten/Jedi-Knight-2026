"""Frozen sensitivity cells. No hyperparameters are selected using held-out RGB.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from hypotheses import catalogue
from identifiability import own_scene, Configuration, compare, retention
from confounding import paired_bootstrap


def run():
    registry=catalogue(); inv=registry['NetRadiant-d9338309-JA/isq_cos']; lin=registry['NetRadiant-d9338309-JA/linear_noangle']
    variants=[('baseline','broad',False,0.,[192.],None,0.,0.,False),
        ('narrow_distances','narrow',False,0.,[192.],None,0.,0.,False),
        ('source_height_free','broad',False,0.,[96.,144.,192.,256.,320.],None,0.,0.,False),
        ('wrong_height_prior','broad',False,0.,[288.,320.],None,0.,0.,False),
        ('training_half_blocks','broad',False,0.,[192.],None,0.,0.,True),
        ('amplitude_prior','broad',False,0.,[192.],None,0.,.01,False),
        ('saturated','narrow',True,0.,[192.],None,0.,0.,False),
        ('bounce_unmodelled','broad',False,24.,[192.],None,0.,0.,False),
        ('bounce_fill_allowed','broad',False,24.,[192.],None,32.,0.,False),
        ('gamma_locked_wrong','gamma',False,0.,[192.],None,0.,0.,False),
        ('gamma_compensation_free','gamma',False,0.,[192.],
         [dict(gamma=g,compensate=c,exposure=0.,srgb=False) for g,c in ((1.,1.),(2.2,1.),(1.,2.),(2.2,2.))],0.,0.,False),
        ('shadow_constraint','door',False,0.,[96.,144.,192.,256.,320.],None,0.,0.,False),
        ('normal_diversity','multi_surface',False,0.,[96.,144.,192.,256.,320.],None,0.,0.,False)]
    output={}
    for name,kind,clipped,bounce,heights,encodings,fill,prior,thin in variants:
        data,conf,_=own_scene(name,inv,kind,2.2 if kind=='gamma' else 1.,clipped,bounce)
        train=[r for r in data if r.partition=='train' and (not thin or int(r.block)%2==0)]
        val=[r for r in data if r.partition=='validation']; held=[r for r in data if r.partition=='heldout']
        configs=[Configuration('z_'+str(z),((0.,0.,z),),conf.axes) for z in heights]
        if kind=='door': configs=[Configuration('z_'+str(z),((-96.,0.,z),),conf.axes) for z in heights]
        result=compare([inv,lin],train,val,held,configs,scene='door' if kind=='door' else 'clear',
                       encodings=encodings,fill_max=fill,amplitude_prior=prior,passes=1)
        a,b=result['results']
        output[name]=dict(comparison=result,retained=retention(result),
            paired_validation_resampling=paired_bootstrap(a['validation']['block_mse'],b['validation']['block_mse']),
            public_policy=dict(training_thinned=thin,heights=heights,fill_max=fill,amplitude_prior=prior))
        print('sensitivity',name,'completed',flush=True)
    data,truth,_=own_scene('complexity',inv,'two_sources')
    parts=[[r for r in data if r.partition==role] for role in ('train','validation','heldout')]
    configs=[Configuration('one_midpoint',((0.,0.,192.),),((0.,0.,-1.),)),truth]
    for weight in (0.,.25,4.):
        result=compare([inv,lin],*parts,configs,complexity_weight=weight,passes=1)
        output['source_count_penalty_'+str(weight)]=dict(comparison=result,retained=retention(result),
            public_policy=dict(complexity_weight=weight,source_counts=[1,2]))
    return output
