#!/usr/bin/env python3
"""Blind fixed-XYZ RGB worker; no compiler answers, validation or Python light law."""
import argparse
import json
import math
from pathlib import Path
import sys
import time
import os
HERE=Path(__file__).resolve().parent
DATASETS=HERE.parent/'datasets'
sys.path.insert(0,str(DATASETS))
from solver_view import TrainingView
from common import PROTOCOL, SCHEMA, POLICY, load, write, digest, canonical, admission, validate_rows, weights, loss, eigenvalues, solve
from client import Client, ForwardUnavailable, BudgetExceeded, FitCancelled
from guard import install


def fit(client,rows,policy,model='emitters'):
    validate_rows(rows);admitted=admission(rows)
    if admitted['status']!='Admitted':return dict(status=admitted['status'],admission=admitted,fitted_rgb=None)
    if any(v['unknown'] for v in client.hello['visibility']):raise ForwardUnavailable('Unknown source/receiver visibility; not numerical black')
    w,_=weights(rows,policy['spatial_block']);n=3 if model=='ambient' else 3*client.hello['sources'];command='A' if model=='ambient' else 'E'
    preparation_work=dict(client.work)
    def delta(a,b):return {k:a[k]-b[k] for k in a}
    x=[policy['initial']]*n;upper=policy['upper'];calls=0;history=[];memo={}
    reserve=4*n+2*n*18+8
    def evaluate(values):
        result=client.predict(values,command);a,m=loss(rows,result['rgb'],w);return a,m,result
    def objective(values):
        nonlocal calls
        key=tuple(values)
        if key not in memo:
            if client.work['forward_calls']>=client.budget-reserve:raise BudgetExceeded('Optimization budget including reserved diagnostics')
            a,m,_=evaluate(values);memo[key]=(a,m)
        calls+=1;return memo[key][0]
    value=objective(x);initial=value;stop='PassLimit';iterations=0;gn_iterations=0
    # Local finite-difference amplitude proposals. Every derivative sample and
    # line-search value uses C++. No global fixed matrix across FAST/encoding.
    for iteration in range(8):
        if value<=policy['objective_stop']:break
        current=evaluate(x)[2]['rgb'];columns=[]
        for k in range(n):
            a=x[:];b=x[:];a[k]=min(upper,a[k]+1.);b[k]=max(0.,b[k]-1.)
            pa=evaluate(a)[2]['rgb'];pb=evaluate(b)[2]['rgb'];columns.append([(u-v)/(a[k]-b[k])*math.sqrt(wi/3) for p,q,wi in zip(pa,pb,w) for u,v in zip(p,q)])
        residuals=[(v-(b+.5 if b<255 else 255))*math.sqrt(wi/3) for row,p,wi in zip(rows,current,w) for v,b in zip(p,row['raw'])]
        H=[[math.fsum(a*b for a,b in zip(u,v)) for v in columns] for u in columns];g=[math.fsum(a*b for a,b in zip(column,residuals)) for column in columns]
        damping=max(max(H[k][k] for k in range(n))*1e-8,1e-10)
        for k in range(n):H[k][k]+=damping
        step=solve(H,[-v for v in g]);accepted=False
        for alpha in (1.,.5,.25,.125,.0625):
            proposal=[max(0.,min(upper,a+alpha*max(-500.,min(500.,b)))) for a,b in zip(x,step)]
            trial=objective(proposal)
            if trial<value:x,value=proposal,trial;accepted=True;break
        gn_iterations=iteration+1
        if not accepted:break
    for sweep in range(policy['max_passes']):
        if value<=policy['objective_stop']:stop='IntervalStationary';break
        previous=value;before=x[:]
        for k in range(n):
            grid=sorted(set([0.,64.,192.,384.,768.,upper,x[k]]))
            scores=[]
            for z in grid:
                proposal=x[:];proposal[k]=z;scores.append(objective(proposal))
            best=min(range(len(grid)),key=lambda i:(scores[i],abs(grid[i]-x[k]),grid[i]));chosen=grid[best];best_value=scores[best]
            lo=grid[max(0,best-1)];hi=grid[min(len(grid)-1,best+1)];ratio=(math.sqrt(5)-1)/2
            a=hi-ratio*(hi-lo);b=lo+ratio*(hi-lo)
            def at(z):
                v=x[:];v[k]=z;return objective(v)
            fa,fb=at(a),at(b)
            for _ in range(policy['golden_steps']):
                if fa<=fb:hi,b,fb=b,a,fa;a=hi-ratio*(hi-lo);fa=at(a)
                else:lo,a,fa=a,b,fb;b=lo+ratio*(hi-lo);fb=at(b)
            for z,v in ((a,fa),(b,fb)):
                if v<best_value:chosen,best_value=z,v
            if best_value<=value:x[k]=chosen;value=best_value
        iterations=sweep+1;motion=max(abs(a-b) for a,b in zip(x,before));history.append(dict(pass_index=iterations,objective=value,maximum_parameter_motion=motion))
        if value<=policy['objective_stop']:stop='IntervalStationary';break
        if previous-value<=policy['improvement_stop'] and motion<=policy['parameter_step_stop']:stop='StagnatedAtQuantizedObjective';break
    # Freeze BEFORE strict replay, validation, held-out or uncertainty probes.
    optimization_work=dict(client.work)
    frozen=tuple(x);frozen_hash=canonical(list(frozen));cached=evaluate(frozen)[2]
    strict=client.predict(frozen,'R' if command=='E' else 'A');strict_loss,maximum=loss(rows,strict['rgb'],w)
    disagreement=max(abs(a-b) for p,q in zip(cached['rgb'],strict['rgb']) for a,b in zip(p,q))
    if disagreement>1e-5:raise ForwardUnavailable('Cached and strict production predictions disagree')
    strict_work=dict(client.work)
    slices=[]
    for k in range(n):
        if maximum>policy['feasible_excess']:
            slices.append(dict(status='Unavailable',reason='No admitted max-one-code fit neighbourhood'));continue
        def feasible(z):
            v=list(frozen);v[k]=z;return evaluate(v)[1]<=policy['feasible_excess']
        ends=[]
        for edge in (0.,upper):
            if feasible(edge):ends.append(edge);continue
            good,bad=frozen[k],edge
            for _ in range(14):
                mid=(good+bad)/2
                if feasible(mid):good=mid
                else:bad=mid
            ends.append(good)
        slices.append(dict(status='ConditionalFeasibleSlice',lower=ends[0],upper=ends[1],other_parameters='frozen',maximum_encoded_interval_excess=policy['feasible_excess'],probability=None))
    # Response Jacobian, NOT derivative of censored loss and not a Fisher matrix.
    columns=[]
    for k in range(n):
        a=list(frozen);b=list(frozen);a[k]=min(upper,a[k]+1.);b[k]=max(0.,b[k]-1.)
        pa=evaluate(a)[2]['rgb'];pb=evaluate(b)[2]['rgb'];columns.append([(u-v)/(a[k]-b[k])*math.sqrt(wi/3) for p,q,wi in zip(pa,pb,w) for u,v in zip(p,q)])
    gram=[[math.fsum(a*b for a,b in zip(u,v)) for v in columns] for u in columns]
    singular=[math.sqrt(x) for x in eigenvalues(gram)];cutoff=max(singular,default=0)*1e-4;rank=sum(x>cutoff for x in singular)
    return dict(status='FrozenDiagnosticFit',optimizer_stop=stop,protocol=PROTOCOL,fitted_rgb=[list(frozen[k:k+3]) for k in range(0,n,3)],
                amplitude_unit='CompilerRelativeRGB_NOT_candela',source_positions='ProvidedFixedPrior_NotEstimated',
                initial_rgb=[policy['initial']]*n,initial_objective=initial,training_objective=strict_loss,
                maximum_training_interval_error=maximum,frozen_parameter_sha256=frozen_hash,strict_replay_max_difference=disagreement,
                numerical_support=dict(mode='CompilerFaithful',output_domain='EncodedRgb8BeforeQuantization',
                    strict_prediction_status_codes=sorted(set(strict['status'])),
                    prediction_status_names=[{3:'AvailableRelative',4:'AvailablePhysical',7:'AvailableConditional'}.get(v,'UnrecognizedStatus') for v in sorted(set(strict['status']))],
                    trace_scope='Conditional general compiler-tree provider; not the scoped multi_sun verified replay',
                    absolute_photometry='NotComparable',derivative_scope='Local amplitude finite differences only; no XYZ/shadow derivative claim'),
                profile_key=strict['profile_key'],passes=iterations,objective_requests=calls,history=history,
                gn_iterations=gn_iterations,optimizer_schedule='bounded local production-FD GN proposals, interval-accepted line search, coordinate fallback',
                admission=admitted,conditional_feasible_slices=slices,
                conditioning=dict(singular_values=singular,effective_rank=rank,parameters=n,relative_rank_threshold=1e-4,
                    condition_estimate=singular[0]/singular[-1] if singular and singular[-1]>0 else None,
                    meaning='Local encoded-response Jacobian at fixed geometry/profile; not source localization information or a probability'),
                parameter_identification='LocallyRankDeficient' if rank<n else 'ConditionalOnFixedGeometryAndProfile',
                stage_work=dict(geometry_preparation=preparation_work,optimization=delta(optimization_work,preparation_work),strict_final=delta(strict_work,optimization_work),uncertainty=delta(client.work,strict_work)),
                physical_emitter_confidence=None,runtime_publication=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--probe',choices=('V','C','B','U'),help='Adversarial production-status test only; never an optimization model')
    p.add_argument('--deny-probe',type=Path,action='append',default=[],help='Controller test of an EXISTING private file; its contents must remain inaccessible')
    args=p.parse_args();inputs=args.inputs.resolve();work=args.work.resolve();exe=args.exe.resolve();work.mkdir(parents=True,exist_ok=True)
    install(inputs,work,exe,[HERE/n for n in ('worker.py','client.py','common.py','guard.py')]+[DATASETS/'solver_view.py'])
    start=time.perf_counter();cpu_start=time.process_time();result=dict(schema=SCHEMA,protocol=PROTOCOL,runtime_publication=False);client=None
    try:
        cfg=load(inputs/'fit-config.json')
        allowed={'schema','map_sha256','geometry_request','geometry_sha256','candidate_geometry','model','solver','profile','provenance'}
        if set(cfg)!=allowed or cfg['schema']!=SCHEMA:raise ValueError('Unexpected/private config field')
        if cfg['model'] not in ('emitters','ambient'):raise ValueError('Unsupported fit model')
        if cfg['profile'].get('family')!='NetRadiant-d9338309e-subsets-v2' or cfg['profile'].get('selector') not in ('isq-cosine','linear-cosine'):raise ValueError('Unsupported/UnknownProfile is not a numeric law')
        policy=cfg['solver'];required=set(POLICY)
        if set(policy)!=required or not 1<=policy['max_calls']<=4000 or not 0<=policy['initial']<=policy['upper']<=2000:raise ValueError('Solver policy/bounds')
        view=TrainingView(inputs,cfg['map_sha256'],max_records=512);rows=list(view.observations());validate_rows(rows)
        request=inputs/cfg['geometry_request']
        if request.parent!=inputs or digest(request)!=cfg['geometry_sha256']:raise ValueError('Geometry request checksum/path')
        # Exercise the actual worker read boundary against all withheld classes.
        denied=[]
        for name in ('evaluator/heldout.jsonl','answers/point_room.json','evaluator/validation.jsonl'):
            try:(inputs.parent/name).read_text();raise ValueError('Private read unexpectedly allowed')
            except PermissionError:denied.append(name)
        for i,path in enumerate(args.deny_probe):
            try:path.read_text();raise ValueError('Existing private file unexpectedly readable')
            except PermissionError:denied.append('existing-private-file-'+str(i))
        result.update(map_sha256=cfg['map_sha256'],candidate_geometry=cfg['candidate_geometry'],profile=cfg['profile'],
            input_manifest_sha256=digest(inputs/'manifest.json'),geometry_request_sha256=cfg['geometry_sha256'],
            training_ids_sha256=canonical([r['id'] for r in rows]),partition_hashes=load(inputs/'manifest.json')['sha256'],
            denied_private_reads=denied,initialization_policy=policy,model=cfg['model'])
        loading=time.perf_counter()-start;client=Client(exe,request,policy['max_calls'])
        print(json.dumps(dict(event='forward_started',pid=client.process.pid)),flush=True)
        if client.hello['map_sha256']!=cfg['map_sha256'] or client.hello['rows']!=len(rows):raise ValueError('Forward dataset identity/receiver count')
        expected_recipe=[int(cfg['profile']['selector']=='linear-cosine'),0,7500,16,1,0,0,1,1,1,.75,1,1,0,0,0,0,0]
        if [float(x) for x in request.read_text().splitlines()[1].split()]!=expected_recipe:raise ValueError('Unspecified public forward recipe')
        for i,(geometry,row) in enumerate(zip(client.hello['receiver_geometry'],rows)):
            expected=[i+1,row['surface']+1,*row['position'],*row['normal']]
            if len(geometry)!=8 or any(abs(a-b)>1e-4 for a,b in zip(geometry,expected)):raise ValueError('Source texel/forward receiver correspondence')
        expected=[[c['id'],*c['xyz']] for c in cfg['candidate_geometry']]
        if len(client.hello['candidate_geometry'])!=len(expected) or any(len(g)!=4 or any(abs(a-b)>1e-4 for a,b in zip(g,e)) for g,e in zip(client.hello['candidate_geometry'],expected)):raise ValueError('Fixed candidate geometry mismatch')
        if args.probe:client.predict([64.]*(3*client.hello['sources']),args.probe)
        result['forward_preparation']=client.hello;result.update(fit(client,rows,policy,cfg['model']))
        result['timing']=dict(dataset_loading_seconds=loading,worker_wall_seconds=time.perf_counter()-start,
                              worker_cpu_seconds=time.process_time()-cpu_start,
                              cpp_evaluation_seconds=client.seconds,ipc_evaluation_seconds=client.wall)
    except (ValueError,KeyError,TypeError,PermissionError,ForwardUnavailable,BudgetExceeded,FitCancelled,OSError) as e:
        result.update(status=type(e).__name__,reason=str(e),fitted_rgb=None)
    finally:
        if client is not None:
            result['work']=client.work;client.close()
    write(work/'fit.json',result)
    print(json.dumps(dict(event='finished',status=result['status'])),flush=True)
    return 0 if result['status'] in ('FrozenDiagnosticFit','InsufficientPositiveEvidence','CensoredUnidentifiable') else 2

if __name__=='__main__':raise SystemExit(main())
