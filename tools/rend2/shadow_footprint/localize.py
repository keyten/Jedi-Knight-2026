"""Bounded one-height/intensity fitting experiment; not a general inverse solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math
from analytic import rectangle_reference


def centre_value(bounds,height,wall):
    x0,x1,y0,y1=bounds; x=(x0+x1)/2; y=(y0+y1)/2
    visible=not wall or x>height*wall[0]/(height-wall[1])
    return height/(height*height+x*x+y*y)**1.5 if visible else 0.


def exact(bounds,height,wall):
    return rectangle_reference(bounds,(0.,0.,height),wall=wall)['value']


def experiment():
    true_height=4.; intensity=8.
    # RGB intensity-only probes occupy one tiny patch. Shadow evidence uses a
    # different receiver and known caster. Validation uses a SECOND caster height.
    unshadowed=[((x-.01,x+.01,-.01,.01),None) for x in (-.04,0.,.04)]
    shadow=[((x-.04,x+.04,-.04,.04),(1.,1.)) for x in (1.27,1.33,1.39)]
    validation=[((x-.03,x+.03,-.03,.03),(.8,1.5)) for x in (1.2,1.28,1.36,1.45)]
    # Declared realistic quantization in an independent analytic fixture domain.
    def target(records): return [math.floor(intensity*exact(b,true_height,w)*2048+.5)/2048 for b,w in records]
    truth_val=target(validation); results=[]
    edge_truth=4/3; edge_sigma=.005 # supplied measurement tolerance, NOT inferred confidence
    for method in ('intensity_only','centre_visibility','integrated_visibility','explicit_boundary'):
        train=unshadowed if method in ('intensity_only','explicit_boundary') else unshadowed+shadow
        observations=target(train)
        evaluator=centre_value if method=='centre_visibility' else exact
        def objective(height):
            columns=[evaluator(b,height,w) for b,w in train]
            amplitude=max(0.,min(64.,sum(a*b for a,b in zip(columns,observations))/max(1e-20,sum(a*a for a in columns))))
            loss=sum((amplitude*a-b)**2 for a,b in zip(columns,observations))/len(train)
            if method=='explicit_boundary': loss+=((height/(height-1)-edge_truth)/edge_sigma)**2*.01
            return loss,amplitude
        for initial in (2.2,3.,5.5,7.5):
            height=initial; iterations=0
            for iteration in range(80):
                h=1e-4; grad=(objective(height+h)[0]-objective(height-h)[0])/(2*h)
                direction=-grad; step=1.
                if abs(direction)<1e-10: break
                # Fixed bounded trust step with strict-objective acceptance.
                direction=max(-.35,min(.35,direction*20))
                accepted=False
                for _ in range(18):
                    candidate=max(2.,min(8.,height+step*direction))
                    if objective(candidate)[0]<objective(height)[0]-1e-16:
                        height=candidate; accepted=True; break
                    step*=.5
                iterations+=1
                if not accepted: break
            loss,amplitude=objective(height)
            # Correct physical/reference prediction for all held-out records,
            # even if training used a centre-only surrogate.
            held=math.sqrt(sum((amplitude*exact(b,height,w)-target)**2 for (b,w),target in zip(validation,truth_val))/len(validation))
            results.append(dict(method=method,initial_height=initial,height=height,position_error=abs(height-true_height),
                fitted_cd=amplitude,train_mse=loss,heldout_lux_rmse=held,iterations=iterations,
                boundary_receiver_and_occluder='separate from brightness targets; validation caster height1.5m'))
    return dict(truth=dict(height_metres=true_height,intensity_cd=intensity),results=results,
        protocol='one height on known axis, bounded 2..8m; fixed source count; analytic own-content forward geometry',
        geometry_generalization='Not demonstrated for unknown XYZ/caster correspondences/bake profile',
        quantizer='nearest 1/2048lux; no JA physical calibration',posterior=None)
