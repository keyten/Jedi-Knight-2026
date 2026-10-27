"""Point-query adaptive receiver sampling: a measured approximation, not certified.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import asdict
from geometry import add,mul,sub,area
from integrate import Work,source_nodes,response


def adaptive(fp,source,scene,tolerance=1e-5,min_depth=1,max_depth=6,limit=200000):
    work=Work(limit); nodes=source_nodes(source,1); total_area=fp.world_area; cache={}; limited=False
    def sample(triangle,uv):
        key=(triangle.id,uv)
        if key in cache: return cache[key]
        p,n=triangle.evaluate(uv); result=[0.]*8
        for node in nodes:
            work.step('rays'); low,high=scene.query(p,node)
            value=response(p,n,node); work.step('responses')
            result[0]+=low*node.measure; result[1]+=high*node.measure
            for j in range(3): result[2+j]+=low*value[j]; result[5+j]+=high*value[j]
        if len(cache)<65536: cache[key]=result
        return result
    def recurse(t,p,depth):
        nonlocal limited
        a,b,c=p; centre=mul(add(add(a,b),c),1/3); ab=mul(add(a,b),.5); bc=mul(add(b,c),.5); ca=mul(add(c,a),.5)
        cells=((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca))
        coarse=sample(t,centre); fine=[0.]*8
        for q in cells:
            values=sample(t,mul(add(add(q[0],q[1]),q[2]),1/3))
            fine=[x+y/4 for x,y in zip(fine,values)]
        delta=max(abs(x-y) for x,y in zip(coarse,fine)); ar=area(p)*t.jacobian
        if depth>=max_depth or (depth>=min_depth and delta<=tolerance):
            limited|=depth>=max_depth and delta>tolerance
            return [v*ar for v in fine],delta*ar
        out=[0.]*8; error=0.
        for q in cells:
            v,e=recurse(t,q,depth+1); out=[x+y for x,y in zip(out,v)]; error+=e
        return out,error
    result=[0.]*8; error=0.
    if not total_area: raise ValueError('No receiver area')
    for piece in fp.pieces:
        for i in range(1,len(piece.polygon)-1):
            value,e=recurse(piece.triangle,(piece.polygon[0],piece.polygon[i],piece.polygon[i+1]),0)
            result=[x+y for x,y in zip(result,value)]; error+=e
    return dict(status='AdaptiveSampledLimit' if limited else 'AdaptiveSampledApproximation',
        contribution_lower=mul(result[2:5],1/total_area),contribution_upper=mul(result[5:8],1/total_area),
        visibility_lower=result[0]/total_area,visibility_upper=result[1]/total_area,
        numerical_error_estimate=error/total_area,numerical_error_is_certified_bound=False,
        warning='Equal samples can miss a narrow shadow/opening; refinement is NOT a completeness proof',
        work=asdict(work),jacobian=None)
