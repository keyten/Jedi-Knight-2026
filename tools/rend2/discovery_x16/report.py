#!/usr/bin/env python3
"""Private-answer evaluation AFTER public-only X16 discovery/selection."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import sha,write
from tools.rend2.discovery_x16.fit import admit
from tools.rend2.evaluation.metrics import angle,detection,metric


def load(p,default=None):return json.loads(p.read_text()) if p.is_file() else default


def nearest(truth,candidates,typed=False):
    result=[]
    for source in truth:
        compatible=[c for c in candidates if not typed or c['source_kind']==int(source['kind']=='spot')]
        c=min(compatible,key=lambda c:math.dist(c['position'],source['position']),default=None)
        result.append(dict(kind=source['kind'],distance=math.dist(c['position'],source['position']) if c else None,
            candidate=c['id'] if c else None,axis_error_degrees=angle(source['axis'],c['outward']) if c and source.get('axis') and c['source_kind']==1 else None,
            radius=c.get('historical_radius') if c else None,
            target=c.get('historical_target_distance') if c else None,
            underconstrained=c.get('underconstrained') if c else None,
            confidence=c.get('existence_confidence') if c else None))
    return result


def summarize(a):
    a.output.mkdir(parents=True,exist_ok=True)
    truth=load(a.bridge/'bridge/private/source-answer.json')['sources']
    results=dict(schema='org.openjk.discovery-x16-results/1',
        policy=load(Path(__file__).with_name('policy-v6.json')),variants={},fits=[],
        source_answers_read_only_here=True,no_runtime_consumers=True,
        source_truth_scope='Owned compiler-relative local roots; finite prism assessed separately, stock answers unavailable')
    if a.oracle:
        results['supplied_answer_observability_control']=load(a.oracle/'results.json')
        results['supplied_answer_observability_sha256']=sha(a.oracle/'results.json')
    for variant in ('direct','passive_bounce1'):
        folder=a.proposals/variant;labels={}
        for label in ('baseline','legacy_adapter','budgetmatched','new'):
            candidates=[];jobs=[]
            for i in range(1,9):
                d=load(folder/label/f'search-{i}.json')
                if not d:continue
                candidates+=d['candidates'];jobs.append(d)
            if not jobs:continue
            matches=nearest(truth,candidates);typed=nearest(truth,candidates,True)
            labels[label]=dict(candidates=len(candidates),by_kind=dict(Counter(c['source_kind'] for c in candidates)),
                nearest=matches,typed_nearest=typed,
                proposal_recall=sum(x['distance'] is not None and x['distance']<=32 for x in matches)/3,
                far_from_all_local_roots=sum(min(math.dist(t['position'],c['position']) for t in truth)>32 for c in candidates),
                final_physical_false_positives=None,photometry=None,
                work={k:sum(j[k] for j in jobs) for k in ('forward_calls','rays','work')},
                accounted_job_peak_bytes=max(j['accounted_bytes'] for j in jobs),
                nodes=sum(x['nodes'] for j in jobs for x in j['areas']),
                job_counts=dict(Counter('Proposed' if j['candidates'] else 'NoSupportedEvidence' for j in jobs)),
                admitted={str(p):dict(count=len(cs:=admit(folder,'baseline' if label=='legacy_adapter' else label,p)),
                    types=dict(Counter(c['source_kind'] for c in cs)),nearest=nearest(truth,cs),typed_nearest=nearest(truth,cs,True)) for p in (0,1)})
        original=a.bridge/'bridge'/variant
        rows=[json.loads(s) for s in (original/'views/inputs/train.jsonl').read_text().splitlines()]
        request=original/'search-8.request'
        lines=request.read_text().splitlines();n=int(lines[5]);selected=[]
        for line in lines[6:6+n]:
            v=line.split();selected.append(dict(id=v[0],surface=v[1],position=list(map(float,v[2:5])),
                normal=list(map(float,v[5:8])),rgb=list(map(int,v[8:11]))))
        results['variants'][variant]=dict(input_hashes={name:sha(original/name) for name in [
            'views/inputs/train.jsonl','views/inputs/geometry.jkobs','views/evaluator/validation.jsonl',
            'views/evaluator/heldout.jsonl','recipe.json']},labels=labels,
            last_shard_selected=len(selected),last_shard_positive=sum(max(x['rgb'])>0 for x in selected),
            last_shard_normals=dict(Counter(str(x['normal']) for x in selected)),
            final_count_precision=None,prism='Exact planar-face alternatives only; no complete multipart source model/count calibration')
    for root in a.fits:
        data=load(root/'results.json',{})
        for case in data.get('cases',[]):
            c=case['summary'];native=c.get('result',{});events=[]
            if not native:
                events=load(root/case['name']/'events.json',[])
                if events and events[-1].get('event')=='result':native=events[-1]
            source_metrics=None
            if native.get('preferred_available'):
                predicted=[]
                local_sources=[s for s in native['sources'] if s['kind'] in (1,2)]
                for s in local_sources:
                    kind={1:'point',2:'spot',3:'rectangle',4:'tube'}.get(s['kind'],'point')
                    p=dict(kind=kind,position=s['position'],photometry=dict(
                        unit='relative',domain='frozen q3map2 profile',kind='LegacyRelative',
                        value=max(s['rgb']),rgb=s['rgb'],primaries='CompilerParameterRGB'))
                    if kind=='spot':
                        p.update(direction=s['axis'],direction_convention='photon travel',
                            cone_degrees=2*math.degrees(math.atan((s['radius']+16)/s['target'])))
                    predicted.append(p)
                t=[]
                for source in truth:
                    x=dict(kind=source['kind'],position=source['position'],
                        photometry=dict(unit='relative',domain='frozen q3map2 profile',kind='LegacyRelative',
                            value=source['intensity'],rgb=[source['intensity']*v for v in source['rgb']],primaries='CompilerParameterRGB'))
                    if source['kind']=='spot':x.update(direction=source['axis'],direction_convention='photon travel',
                        cone_degrees=2*math.degrees(math.atan((source['radius']+16)/math.dist(source['target'],source['position']))))
                    t.append(x)
                source_metrics=detection(t,predicted,dict(position_world=32,angle_degrees=10,
                    cone_degrees=10,dimension_relative=.25,roll_degrees=15),1)
                source_metrics['geometric_summary'].pop('position_metres',None)
                for m in source_metrics['matches']:m.pop('position_metres',None);m.pop('xyz_metres',None)
                source_metrics['compiler_Q_parameter_errors']=[]
                for m in source_metrics['matches']:
                    source=truth[m['truth_index']];fitted=local_sources[m['prediction_index']]
                    q=[source['intensity']*v for v in source['rgb']]
                    source_metrics['compiler_Q_parameter_errors'].append(dict(
                        truth_index=m['truth_index'],prediction_index=m['prediction_index'],
                        channel_errors=[v-w for v,w in zip(fitted['rgb'],q)],
                        relative_rgb_l2=math.dist(fitted['rgb'],q)/math.sqrt(sum(v*v for v in q)),
                        scope='Compiler Q parameters, not calibrated radiometric or Rec709 chromaticity accuracy'))
            attempts=native.get('attempts',[])
            held=c.get('heldout') or {}
            final_pass=bool(native.get('preferred_available') and native.get('sources') and
                held.get('available') and held.get('required') and held.get('positive') and
                held['mse']<=1 and held['mse']<=.02*held['black'])
            results['fits'].append(dict(run=root.name,name=case['name'],status=c['status'],
                native_state=native.get('state',c['status']),reason=native.get('reason',c.get('reason')),
                selected_types=dict(Counter(x.get('kind') for x in native.get('sources',[]))),
                source_metrics=source_metrics,heldout=c.get('heldout'),
                native_preferred_nonempty=bool(native.get('preferred_available') and native.get('sources')),
                accepted_nonempty=final_pass,
                source_state='ConditionalCandidate',absolute_scale='Unknown',
                exact_light_suitable=False,existence_probability=None,XYZ_interval_coverage=None,
                catalog={k:native.get(k) for k in ['catalog_input','catalog_models','catalog_admitted','catalog_deferred','finite_unsupported']},
                work={k:native.get(k) for k in ['models','evaluations','rays','cache_hits','preparation_calls']},
                seconds=c.get('seconds'),peak_rss_bytes=c.get('peak_rss_bytes'),
                attempted_proposals=dict(Counter(x.get('proposal','').split(':')[0] for x in attempts)),
                rejected_reasons=dict(Counter(x.get('reason') for x in attempts if x.get('reason')))))
            results['fits'][-1]['alternative_models']=native.get('alternative_models')
            results['fits'][-1]['frozen_alternative_diagnostics']=c.get('frozen_alternative_diagnostics')
            for frozen in results['fits'][-1]['frozen_alternative_diagnostics'] or []:
                pool=[dict(id=s['id'],source_kind=s['kind']-1,position=s['position'],outward=s['axis'],
                    historical_radius=s['radius'],historical_target_distance=s['target'])
                    for s in frozen['sources'] if s['kind'] in (1,2)]
                frozen['typed_nearest_root_errors']=nearest(truth,pool,True)
                frozen['accepted_or_reselected']=False
                frozen['scope']='Independent diagnostic after parameter freeze; final scores never feed selection'
            results['fits'][-1]['validation_evaluations']=sum(r['role']=='validation' for r in c.get('validation',[]))
            if not c.get('validation'):
                results['fits'][-1]['validation_evaluations']=sum(e.get('event')=='validation' for e in events)
            results['fits'][-1]['candidate_deferred']=c.get('deferred_count')
            results['fits'][-1]['run_policy_sha256']=data.get('policy_sha256')
            results['fits'][-1]['native_tool_sha256']=data.get('tool_sha256')
            results['fits'][-1]['proposal_manifest_sha256']=data.get('proposals_manifest_sha256')
            results['fits'][-1]['finite_selected_separate_scope']=sum(s['kind'] not in (1,2) for s in native.get('sources',[]))
            results['fits'][-1]['budget_scope']=('Mandatory native ray work' if 'ray budget' in native.get('reason','') or
                any('ray budget' in x.get('reason','') for x in attempts) else
                'OS wall safety; native work incomplete' if c['status']=='Budget' and not native else c.get('budget_scope'))
            results['fits'][-1]['model_scope']='Conditional direct-only; missing prism-union and indirect transport are not certified zero'
    write(a.output/'results.json',results)
    lines=['# X16 measured discovery and selection','',
        '| Variant / policy | Candidates | Point 1 / point 2 / spot distance WU | Proposal recall | Rays |',
        '|---|---:|---|---:|---:|']
    for variant,v in results['variants'].items():
        for label,d in v['labels'].items():
            ds=' / '.join(f"{x['distance']:.3f}" if x['distance'] is not None else 'Unavailable' for x in d['nearest'])
            lines.append(f"| {variant} / {label} | {d['candidates']} | {ds} | {d['proposal_recall']:.3f} | {d['work']['rays']} |")
    lines+=['','| Run | Native state | Accepted nonempty | Models | Rays | Held-out MSE |',
            '|---|---|---:|---:|---:|---:|']
    for r in results['fits']:
        lines.append(f"| {r['run']} / {r['name']} | {r['native_state']} | {r['accepted_nonempty']} | {r['work']['models']} | {r['work']['rays']} | {(r['heldout'] or {}).get('mse','Unavailable')} |")
    (a.output/'tables.md').write_text('\n'.join(lines)+'\n')
    write(a.output/'manifest.json',dict(policy_sha256=sha(Path(__file__).with_name('policy-v6.json')),
        proposals=sha(a.proposals/'manifest.json'),
        files={p.name:sha(p) for p in a.output.iterdir() if p.is_file() and p.name!='manifest.json'},
        historical_V02_overwritten=False,private_answer_not_solver_input=True))
    print(json.dumps(dict(fits=len(results['fits']),accepted=sum(x['accepted_nonempty'] for x in results['fits']))))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bridge','proposals','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--fits',nargs='*',type=Path,default=[])
    p.add_argument('--oracle',type=Path,help='Post-fit supplied-answer observability output, never discovery input')
    summarize(p.parse_args())
