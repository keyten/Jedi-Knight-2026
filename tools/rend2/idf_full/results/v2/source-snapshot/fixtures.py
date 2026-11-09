"""Controller-only authoring. No import from the numerical discovery module.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import itertools
import numpy as np


def author(name,side=10):
    positions=[];normals=[];roles=[];ids=[]
    ticks=np.linspace(-.94,.94,side)
    for axis,sign in itertools.product(range(3),(-1,1)):
        other=[i for i in range(3) if i!=axis]
        for i,j in itertools.product(range(side),repeat=2):
            p=np.zeros(3);p[axis]=sign;p[other]=[ticks[i],ticks[j]]
            n=np.zeros(3);n[axis]=-sign
            # Spatial tiles, fixed before source intensities/positions enter.
            bucket=(axis*7+(sign+1)*3+(i//2)*3+j//2)%5
            roles.append('train' if bucket<3 else 'validation' if bucket==3 else 'heldout')
            positions.append(p);normals.append(n);ids.append(f'box/{axis}/{sign}/{i}/{j}')
    if name=='linear_two':
        sources=[dict(position=[-.4,-.25,.45],intensity=1.4),dict(position=[.45,.35,.1],intensity=.6)]
    elif name=='linear_four':
        sources=[dict(position=[-.55,-.4,.45],intensity=2.),dict(position=[.5,-.35,0],intensity=.9),
                 dict(position=[-.1,.5,-.35],intensity=.65),dict(position=[.45,.48,.4],intensity=.16)]
    elif name=='linear_fill_control':sources=[]
    else:raise ValueError(name)
    x=np.array(positions);n=np.array(normals);values=np.zeros((len(x),1))
    for s in sources:
        delta=np.array(s['position'])-x;r2=np.sum(delta**2,axis=1)
        values[:,0]+=s['intensity']*np.maximum(0,np.sum(n*delta,axis=1))/r2**1.5
    if name=='linear_fill_control':values[:]=.4 # supplied uniform indirect fill, out-of-model
    result={}
    for role in ('train','validation','heldout'):
        select=np.array([r==role for r in roles])
        result[role]=dict(positions=x[select].tolist(),normals=n[select].tolist(),values=values[select].tolist(),
                          ids=[i for i,r in zip(ids,roles) if r==role])
    return result,dict(sources=sources,domain='AuthoredLinearDiffuse_NoPhotometricCalibrationClaimForJA',
                       control='Uniform prescribed indirect fill, not a sky/compiler replay' if not sources else None)
