"""Bounded offline request contract; no renderer or production evaluator installed.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import dataclass,replace
import math
from integrate import integrated,sampled,BudgetError


@dataclass(frozen=True)
class Policy:
    mode: str = 'reference' # coarse, reference; smoothing requires separate surrogate
    source_order: int = 1
    receiver_samples: int = 4
    tolerance: float = 1e-8
    work_limit: int = 200000


def evaluate(fp,source,scene,profile='PhysicalSI-Direct-v1',policy=Policy()):
    """Owned value result; None means unknown, never a zero-valued fallback light.

    A selected historical bake profile requires its actual sampling/trace rules;
    physical area averaging is NOT silently substituted for a compiler point
    sample. The pinned compiler fixture has a separate restricted replay tool.
    """
    base=dict(observation=fp.id,source=source.id,profile=profile,available=False,jacobian=None)
    if profile!='PhysicalSI-Direct-v1': return dict(base,status='UnspecifiedModel',contribution=None,
        missing=['profile-specific footprint/sampling/trace/output encoding rules'])
    try:
        if policy.mode not in ('coarse','reference') or not math.isfinite(policy.tolerance) or policy.tolerance<=0:
            raise ValueError('Invalid integration policy')
        result=(integrated(fp,source,scene,source_order=policy.source_order,tolerance=policy.tolerance,limit=policy.work_limit)
                if policy.mode=='reference' else sampled(fp,source,scene,n=policy.receiver_samples,
                                                       source_order=policy.source_order,limit=policy.work_limit))
        return dict(base,**{k:v for k,v in result.items() if k not in base},
                    available=result.get('available',True),mode=policy.mode)
    except BudgetError as error: return dict(base,status='WorkLimit',message=str(error),contribution=None)
    except ValueError as error: return dict(base,status='InvalidInput',message=str(error),contribution=None)


def position_jacobian(fp,source,scene,step,policy=Policy()):
    """Retraced central difference, labelled numerical; not a physical derivative claim.

    Caller must inspect step/order/geometry stability. Unknown optical intervals
    have no unique derivative; one-sided bounds are not a probability model.
    Work limit applies per evaluation, bounded total seven evaluations.
    """
    centre=evaluate(fp,source,scene,policy=policy)
    if not math.isfinite(step) or step<=0: return dict(centre,status='InvalidDerivativeStep')
    def exact(r): return r.get('available') and r.get('contribution_upper') is not None and r.get('contribution_lower')==r.get('contribution_upper')
    if not exact(centre): return dict(centre,derivative_status='UnavailableForGeometryInterval')
    columns=[]; work=centre['work']['responses']+centre['work']['rays']+centre['work']['projections']+centre['work']['pieces']
    for k in range(3):
        p=list(source.position); p[k]+=step
        plus=evaluate(fp,replace(source,position=tuple(p)),scene,policy=policy)
        p[k]-=2*step
        minus=evaluate(fp,replace(source,position=tuple(p)),scene,policy=policy)
        if not exact(plus) or not exact(minus): return dict(centre,derivative_status='UnavailableNeighbour')
        columns.append(tuple((a-b)/(2*step) for a,b in zip(plus['contribution_upper'],minus['contribution_upper'])))
        for r in (plus,minus): work+=sum(r['work'][name] for name in ('responses','rays','projections','pieces'))
    return dict(centre,jacobian=columns,derivative_status='RetracedFiniteDifferenceEstimate',
                derivative_step=step,total_derivative_work=work,
                derivative_warning='Check multiple steps/source orders and topology; determinism alone is insufficient')
