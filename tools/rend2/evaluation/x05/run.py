#!/usr/bin/env python3
"""X05 controller: frozen admission/contracts, real bakes, blind files, no solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys

from acceptance import load, digest, target, predictions, evaluate, profile_gate, PROFILE
from fixtures import (HERE, ROOT, write, floor_rows, reference, publish_rows, add_partition, load_geometry,
                      compile_suite, visible, sha, COMPILER_SHA256)
from audit import audit_old, stats, source_truth
from prepare_blind import prepare, eligible, rows as source_rows
sys.path.insert(0,str(HERE.parent))
from adapters import audit_training
from solver_view import TrainingView
from evaluation_view import EvaluationView
from identifiability import line_minimum
from diagnostics import run_diagnostics


def tagged(rows,truth,triangles):
    _,states=reference(rows,triangles,truth)
    for row,pairs in zip(rows,states):
        groups=['all'];x=row['position'][0]
        if truth['profile'] in ('sky17','sun'):
            groups+=['exposed'] if any('Visible' in v for v in pairs) else ['sealed']
        elif truth['profile']=='area':groups+=['area']
        else:
            groups+=['lit'] if any(v=='Visible' for v in pairs) else ['blocked']
            if x>32:groups+=['opening'] if any(v=='Visible' for v in pairs) else ['behind_wall']
            if truth['profile']=='spot':
                from bake_model import spot_factor
                p=[x+n for x,n in zip(row['position'],row['normal'])];s=truth['sources'][0]
                factor=spot_factor(p,s['origin'],s.get('axis',[0,0,-1]),128.,64.)
                if 0<factor<1:groups+=['angular_edge']
        row['groups']=groups;row['visibility_pairs']=pairs
    return rows


def run_controls(rows,train,triangles,truth,contracts,policy):
    values,_=reference(rows,triangles,truth)
    controls={'black':predictions(rows),'all_blocked':predictions(rows),
        'oracle_forward':predictions(rows,values),
        'all_visible':predictions(rows,reference(rows,triangles,truth,'all_visible')[0]),
        'wrong_profile':predictions(rows,reference(rows,triangles,truth,'wrong_profile')[0]),
        'wrong_geometry':predictions(rows,reference(rows,triangles,truth,'wrong_geometry')[0]),
        'missing':[], 'unavailable':predictions(rows,status='Unavailable',reason='Deliberate abstention control'),
        'partial':predictions(rows[:1],values[:1])}
    if truth['profile']=='spot':controls['wrong_spot']=predictions(rows,reference(rows,triangles,truth,'wrong_spot')[0])
    if train:
        weight=sum(r['area'] for r in train)
        ambient=[sum(r['raw'][c]*r['area'] for r in train)/weight for c in range(3)]
        controls['training_ambient']=predictions(rows,[ambient]*len(rows))
    else:controls['training_ambient']=predictions(rows,status='Unavailable',reason='Empty training stream; no heldout fit')
    results={name:evaluate(rows,p,contracts,policy) for name,p in controls.items()}
    if truth['profile'] not in ('isq','linear','spot','noangle'):
        for name in ('wrong_profile','wrong_geometry'):
            results[name]=dict(status='NotApplicable',success=False,reason='This control changes a finite point/spot parameter only; no fabricated alternative for this component')
    return results


def make_new_cases(compiled,policy,output,inspector,quality_tool):
    cases={};manifest=[]
    specifications={
        'x05_sky_aperture_v1':[dict(id='positive_sky',group='exposed',kind='PositiveLight')],
        'x05_sky_sealed_v1':[dict(id='sealed_sky_negative',group='sealed',kind='DarkBlockedNegative')],
        'x05_wall_pair_v1':[dict(id='wall_illuminated',group='lit',kind='PositiveLight'),dict(id='wall_blocked',group='blocked',kind='DarkBlockedNegative')],
        'x05_doorway_pair_v1':[dict(id='doorway_through',group='opening',kind='PositiveLight'),dict(id='doorway_blocked',group='behind_wall',kind='DarkBlockedNegative')],
        'x05_doorway_pair_v2':[dict(id='doorway_through',group='opening',kind='PositiveLight'),dict(id='doorway_blocked',group='behind_wall',kind='DarkBlockedNegative')],
        'x05_doorway_pair_v3':[dict(id='doorway_through',group='opening',kind='PositiveLight'),dict(id='doorway_blocked',group='behind_wall',kind='DarkBlockedNegative')],
        'x05_panel_positive_v1':[dict(id='strong_area',group='area',kind='PositiveLight')],
        'x05_sun_positive_v1':[dict(id='strong_sun',group='exposed',kind='PositiveLight')]}
    recipes=load(Path(compiled)/'recipes.json')
    for name,truth in recipes.items():
        bsp=Path(compiled)/'authoring/base/maps'/(name+'.bsp')
        if digest(bsp)!=truth['bsp_sha256']:raise ValueError('New fixture hash mismatch')
        rows,(_,surfaces,triangles)=floor_rows(bsp,policy,policy['new_receiver_domain']['floor_abs_xy_max_world']);tagged(rows,truth,triangles)
        folder=output/name;folder.mkdir();geometry=folder/'geometry.jkobs'
        cmd=[str(inspector.resolve()),str(bsp.resolve()),str(folder/'geometry'), '0','0','192','384','256','128','1',str(geometry.resolve())]
        completed=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
        (folder/'geometry.log').write_text(completed.stdout+completed.stderr,encoding='utf-8')
        if completed.returncode:raise ValueError('Existing CPU geometry export failed '+completed.stderr)
        # Use the existing sanitizer, including BlindBenchmark metadata and
        # removal of source photometry/vertex RGB. Geometry-only != blind.
        sanitized=folder/'sanitized'
        scan=subprocess.run([str(quality_tool.resolve()),'scan',str(geometry.resolve()),str(sanitized.resolve())],capture_output=True,text=True)
        if scan.returncode:raise ValueError('Canonical geometry sanitization failed '+scan.stderr)
        prefix=folder/'observations';publish_rows(prefix,rows,truth['bsp_sha256'],sanitized.with_suffix('.public.jkobs'))
        # Private group labels/source parameters are never passed to prepare().
        split=policy['split'];report=prepare(prefix,folder/'views',split['block_world'],split['gap_world'],split['seed'],split['origin_world'])
        check=subprocess.run([str(quality_tool.resolve()),'public-check',str(folder/'views/inputs/geometry.jkobs'),'unused'],capture_output=True,text=True)
        if check.returncode:raise ValueError('Canonical blind geometry sanitization failed '+check.stderr)
        audit=audit_training(folder/'views/inputs',truth['bsp_sha256'])
        private=EvaluationView(folder/'views/evaluator',truth['bsp_sha256'],permitted=('heldout',))
        held_ids={r['id'] for r in private.observations('heldout')}
        held=[r for r in rows if r['id'] in held_ids];train=[r for r in rows if r['partition']=='train']
        write(folder/'controller-case.json',dict(schema='org.openjk.forward-private-case.v1',id=name,
            map_sha256=truth['bsp_sha256'],rows=held,contracts=specifications[name],output_profile=PROFILE))
        controls=run_controls(held,train,triangles,truth,specifications[name],policy)
        cases[name]=dict(audit=audit,partitions={p:stats([r for r in rows if r['partition']==p],truth['sources']) for p in ('train','validation','heldout')},
            controls=controls,raw_scope='Independent compiler-backed floor luxels; original bytes and UV mapping, no screenshots',
            geometry_public_check=check.stdout.strip(),unsupported_receiver_uv_triangles=sum(s.get('unsupported_receiver_uv_triangles',0) for s in surfaces))
        v=load(folder/'views/inputs/manifest.json')
        manifest.append(dict(id=name,bsp_sha256=truth['bsp_sha256'],map_sha256=truth['map_sha256'],
            compiler_sha256=truth['compiler_sha256'],shader_sha256=truth['shader_sha256'],recipe_sha256=digest(Path(compiled)/(name+'.recipe.json')),
            input_member_hashes=v['sha256'],split_identity={k:v.get(k) for k in ('seed','block_world','gap_world','partition_origin_world')},
            evaluator_member_hashes=load(folder/'views/evaluator/manifest.json')['sha256'],
            private_case=str((folder/'controller-case.json').resolve()),private_case_sha256=digest(folder/'controller-case.json'),
            contracts=specifications[name],output_profile=PROFILE,protocol='ForwardKnownSources private reference; inverse input contains no source answers',
            required=name not in ('x05_doorway_pair_v1','x05_doorway_pair_v2'),
            categories=['SunSkyDirectional'] if truth['sky'] else ['AreaTube'] if truth['profile']=='area' else ['PositiveLight','DarkBlockedNegative']))
        print('evaluated',name,{k:v['status'] for k,v in controls.items()},flush=True)
    return cases,manifest


def profile_trials(rows,triangles,truth,policy,narrow=False):
    # A narrow annulus gives genuinely identical distances at independent positions.
    if narrow:
        rows=[dict(r) for r in rows if sorted(round(abs(v)) for v in r['position'][:2])==[96,128]]
        for r in rows:
            x,y=r['position'][:2];r['partition']='train' if x<0 and y<0 else 'validation' if x>0 and y<0 else 'heldout'
        # These disjoint quadrants have >=192WU separation, well beyond footprints.
    by_role={p:[r for r in rows if r['partition']==p] for p in ('train','validation','heldout')}
    if min(map(len,by_role.values()))<2:return dict(status='Failed',reason='Empty/weak profile fold',counts={k:len(v) for k,v in by_role.items()})
    alternatives=[]
    from acceptance import target
    import metrics
    _,states=reference(rows,triangles,truth)
    visibility={r['id']:v for r,v in zip(rows,states)}
    for model in ('isq','linear','noangle','spot') if not narrow else ('isq','linear'):
        candidate=dict(truth,profile=model)
        def objective(I):
            trial=dict(candidate,sources=[dict(truth['sources'][0],intensity=I)])
            val,_=reference(by_role['train'],triangles,trial,known_visibility=visibility)
            return metrics.prediction_metrics([target(r) for r in by_role['train']],predictions(by_role['train'],val))['rgb_interval_rmse']['value']**2
        I=line_minimum(objective,policy['profile']['amplitude_bounds'][1],300.,steps=policy['profile']['amplitude_search_steps'])
        trial=dict(candidate,sources=[dict(truth['sources'][0],intensity=I)])
        item=dict(id=model,parameter_budget=1,split_hash=hashlib.sha256(json.dumps({p:[r['id'] for r in rs] for p,rs in by_role.items()},sort_keys=True).encode()).hexdigest(),
            comparison_id=PROFILE,amplitude=I)
        for role in ('validation','heldout'):
            val,_=reference(by_role[role],triangles,trial,known_visibility=visibility)
            item[role+'_rmse']=metrics.prediction_metrics([target(r) for r in by_role[role]],predictions(by_role[role],val))['rgb_interval_rmse']['value']
        alternatives.append(item)
    gate=profile_gate(alternatives,'isq',policy,'Ambiguous' if narrow else 'Discriminating')
    gate['partitions']={k:stats(v,truth['sources']) for k,v in by_role.items()};return gate


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--compiled',type=Path,required=True)
    p.add_argument('--inspector',type=Path,required=True);p.add_argument('--quality-tool',type=Path,required=True)
    p.add_argument('--old12',type=Path,default=ROOT/'build/x02/compiled-fixtures-frozen')
    p.add_argument('--recipes12',type=Path,default=ROOT/'build/q3map2-bake-verified/answers')
    p.add_argument('--old22',type=Path,default=ROOT/'build/bake-model-comparison-final')
    p.add_argument('--audit',type=Path)
    p.add_argument('--compiler',type=Path,help='Compile new controls into a fresh --compiled directory before evaluating')
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False);policy=load(HERE/'policy.json')
    if args.compiler:compile_suite(args.compiler,args.compiled,policy['new_fixtures'])
    old=load(args.audit) if args.audit else audit_old(args.old12,args.recipes12,args.old22,policy,args.output/'audit.json')
    cases,manifest=make_new_cases(args.compiled,policy,args.output,args.inspector,args.quality_tool)
    raw,_=floor_rows(args.old22/'authoring/base/maps/inverse_square.bsp',policy)
    _,_,triangles=load_geometry(args.old22/'authoring/base/maps/inverse_square.bsp')
    truth=dict(profile='isq',sources=[dict(origin=[0,0,192],rgb=[1,1,1],intensity=300)])
    # A second broad case uses existing canonical six-surface observations,
    # selected by geometry/normal diversity, never by their brightness.
    recipe=load(args.recipes12/'point_room.q3map-recipe.json');sources=source_truth(recipe)
    broad_truth=dict(profile='isq',sources=sources)
    broad=[];seen=set()
    for row in source_rows(args.old12/'point_room.rows.jsonl'):
        if not eligible(row):continue
        add_partition(row,policy)
        if row['partition'] is None:continue
        key=(row['surface'],*(math.floor(v/64) for v in row['position']))
        if key in seen:continue
        seen.add(key);broad.append(row)
    _,_,broad_triangles=load_geometry(args.recipes12.parent/'authoring/base/maps/point_room.bsp')
    profiles=dict(broad=profile_trials(broad,broad_triangles,broad_truth,policy),
        broad_floor=profile_trials(raw,triangles,truth,policy),narrow=profile_trials(raw,triangles,truth,policy,True))
    diagnostics,additional=run_diagnostics(args.output,args.old22,policy)
    manifest.extend(additional)
    observed={d['id']:d['status'] for d in diagnostics}
    for m in manifest:
        if not m['required'] or m.get('role')=='diagnostic':continue
        for c in cases[m['id']]['controls']['oracle_forward']['contracts']:observed[c['id']]=c['status']
    observed['broad_profile_separation']=profiles['broad']['status']
    observed['narrow_profile_ambiguity']='Passed' if profiles['narrow']['status']=='Ambiguous' else 'Failed'
    control_checks={}
    for m in manifest:
        if not m['required'] or m.get('role')=='diagnostic':continue
        controls=cases[m['id']]['controls'];positive=any(c['kind']=='PositiveLight' for c in m['contracts'])
        required_fail=['missing','unavailable','partial']+(['black','all_blocked'] if positive else [])
        if any(c['kind']=='DarkBlockedNegative' for c in m['contracts']) or m['id']=='x05_sky_aperture_v1':required_fail+=['all_visible']
        if m['id'] in ('x05_wall_pair_v1','x05_doorway_pair_v3'):required_fail+=['wrong_profile','wrong_geometry']
        control_checks[m['id']]=dict(status='Passed' if all(controls[k]['status']=='Failed' for k in required_fail) else 'Failed',
            expected_rejected=required_fail,actually_rejected=[k for k in required_fail if controls[k]['status']=='Failed'])
    missing=set(policy['required_contracts'])-set(observed)
    acceptance=dict(status='Passed' if not missing and all(observed[k]=='Passed' for k in policy['required_contracts']) and all(c['status']=='Passed' for c in control_checks.values()) and all(d['status']=='Passed' for d in diagnostics) else 'Failed',
        contracts=observed,control_checks=control_checks,missing=sorted(missing),meaning='Benchmark reference/admission self-check only; no production forward evaluator or inverse solver was tested')
    result=dict(schema='org.openjk.x05-result.v1',policy_sha256=digest(HERE/'policy.json'),cases=cases,profiles=profiles,
        diagnostics=diagnostics,acceptance=acceptance,original_audit=old,no_emitter_recovery=True,renderer_modified=False)
    write(args.output/'result.json',result)
    write(args.output/'manifest.json',dict(schema='org.openjk.x05-benchmark-manifest.v1',policy=policy,policy_sha256=digest(HERE/'policy.json'),
        source_lock_sha256=digest(HERE.parents[1]/'bake_model/source-lock.json'),fixtures=manifest,
        controller_sources={p.name:digest(p) for p in sorted(HERE.glob('*.py'))},
        reference_sources={name:digest(HERE.parents[1]/name) for name in (
            'evaluation/metrics.py','evaluation/adapters.py','datasets/prepare_blind.py','datasets/solver_view.py','datasets/evaluation_view.py',
            'bake_model/bake_model.py','bake_model/compare_bakes.py','bake_model/hypotheses.py','bake_model/identifiability.py',
            'shadow_footprint/compiler_boundary.py','shadow_footprint/compiler_trace.py','shadow_footprint/geometry.py',
            'shadow_footprint/analytic.py','shadow_footprint/integrate.py')},
        cpu_tools=dict(inspector_sha256=digest(args.inspector),quality_sha256=digest(args.quality_tool)),
        diagnostics=[dict(id=d['id'],categories=d['categories'],required=True) for d in diagnostics],
        profile_contracts=dict(broad='Train-only scalar nuisance fit, validation separation and independent heldout confirmation on multiple receiver normals',narrow='Same distance in separated quadrants; preserve ambiguity'),
        controls=control_checks,resource_limits=dict(max_case_records=100000,json_bytes=64*1024*1024,compiler_timeout_seconds=180),
        reference_models=['Existing source-locked direct kernels','17-direction sky subset','Ceiling polygon form-factor subset'],
        frozen_before_future_solver_fitting=True))
    print('profile outcomes',profiles['broad']['status'],profiles['narrow']['status'],flush=True)
    print('benchmark self-check',acceptance['status'],flush=True)
    return 0 if acceptance['status']=='Passed' else 1


if __name__=='__main__':raise SystemExit(main())
