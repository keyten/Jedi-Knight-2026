#!/usr/bin/env python3
"""Consolidate actual V02 receipts, preserving unsupported/missing denominators.

Only aggregate stock metrics are exported. Original geometry/RGB and private
answers remain outside the repository. GPL-2.0-or-later.
"""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import re
import shutil
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import sha,write
from tools.rend2.evaluation.metrics import metric


def load(path,default=None):
    return json.loads(path.read_text()) if path.is_file() else default


def gate(value,limit,available=True):
    if not available or value is None:return dict(status='Unavailable',value=None,limit=limit)
    return dict(status='Passed' if value<=limit else 'Failed',value=value,limit=limit)


def stock_map(folder,receipt):
    census=load(folder/'census.json',{});support=load(folder/'receiver-support.json',{})
    folds=load(folder/'folds.json',{}).get('folds',[])
    profiles={}
    for name in ('isq','linear'):
        schedule=load(folder/('scheduler-'+name+'.json'),{})
        fit=load(folder/(name+'.json'),{})
        controller=load(folder/('scheduler-'+name+'-controller.json'),{})
        units=schedule.get('units',[])
        branches=schedule.get('branches',[])
        attempts=[x for b in branches for x in b.get('attempts',[])]
        profiles[name]={k:v for k,v in schedule.items() if k not in ('units','branches','influence')}
        profiles[name].update(
            units=len(units),unit_states=dict(Counter(x['status'] for x in units)),
            units_with_candidates=sum(x.get('candidates',0)>0 for x in units),
            complete_units=sum(x.get('complete',False) for x in units),
            catalog_models=sum(x.get('catalog_models',0) for x in branches),
            deferred_models=sum(x.get('deferred',0) for x in branches),
            attempted_model_states=dict(Counter(x['status'] for x in attempts)),
            rejected_reasons=dict(Counter(x.get('reason','') for x in attempts if x['status']!='Converged')),
            maximum_admitted_rows=max((x.get('coverage_admitted',0) for x in attempts),default=0),
            maximum_used_rows=max((x.get('coverage_used',0) for x in attempts),default=0),
            missing_predictions=sum(x.get('missing_predictions',0) for x in attempts),
            unknown_visibility=sum(x.get('unknown_visibility',0) for x in attempts),
            controller=controller,last_wave_work=fit.get('work',{}),
            shadow_observed_gradients=fit.get('shadow_observed_gradients'),
            shadow_candidates=fit.get('shadow_candidates'),
            selected_sources=len(fit.get('sources',[])),
            final_evaluation_performed=fit.get('final_evaluation_performed',False),
            heldout=metric(fit.get('producer',{}).get('heldout_interval_mse'),
                reason='No accepted frozen source model' if not fit.get('final_evaluation_performed') else None,
                unit='RGB8 interval squared'))
    requested=support.get('mapped_area');supported=support.get('categories',{}).get('Supported',{}).get('area')
    out=dict(bsp_sha256=receipt['bsp_sha256'],dataset_sha256=receipt['dataset_sha256'],
        process_status=receipt['status'],resources=receipt['resources'],
        source_surfaces=census.get('source_surfaces'),source_hints=census.get('source_candidates'),
        raw_pages=census.get('raw_colour_pages'),raw_texels=census.get('derived_texels'),
        mapped_area=requested,supported_area=supported,
        supported_area_fraction=supported/requested if supported is not None and requested else None,
        receiver_support_categories=support.get('categories',{}),
        selected_requested=support.get('selected_requested'),selected_common=support.get('selected_common'),
        selected_roles=support.get('selected_roles'),folds=folds,profiles=profiles,
        transport=load(folder/'transport.json',{}),
        unknown_SI=True,original_source_answers=None,stock_profile='UnknownProfile',
        material_interpretation='SP capture context; not independently certified MP optics',
        physical_suitability='Unavailable: no accepted historical physical configuration',
        source_confidence_distribution=None,sun='UnknownSun',sky='UnresolvedEnvironment',
        photometric_fields=0,canonical_nonempty_asset=False)
    return out


def angular_summary(data):
    rows=data['rows'];result={}
    for name in sorted({x['representation'] for x in rows}):
        group=[x for x in rows if x['representation']==name]
        radiance=[x['radiance']['relative_rms'] for x in group if x['radiance']]
        diffuse=[x['irradiance']['relative_rms'] for x in group]
        result[name]=dict(cases=len(group),radiance_comparable=len(radiance),
            diffuse_mean=sum(diffuse)/len(diffuse),diffuse_worst=max(diffuse),
            radiance_mean=sum(radiance)/len(radiance) if radiance else None,
            radiance_worst=max(radiance) if radiance else None,
            # Keep individual scene rows as well; averages cannot hide failure.
            scenes={r['scene']:dict(irradiance=r['irradiance'],radiance=r['radiance'],
                bytes=[q.get('bytes') for q in r['quantization']],
                fit=r['fit'],quantization=r['quantization']) for r in group})
    return result


def runtime_summary(path):
    rows=load(path/'results.json',[]);result=[]
    for r in rows:
        d=r.get('result',r)
        result.append(dict(map=r.get('map'),profile=r.get('profile'),status=r['status'],
            reason=r.get('reason'),images=d.get('images',d.get('layers')),
            consumer_blocks=d.get('consumer_blocks_observed'),
            permutation_usage_identical=d.get('permutation_usage_identical'),
            renderer_sha256=d.get('renderer_sha256'),errors=d.get('errors'),
            camera=d.get('recorded_camera',d.get('viewpos')),
            seconds=d.get('seconds'),peak_rss_bytes=d.get('peak_rss_bytes'),
            debug_alignment_verified=False if 'debug_marker_alignment' in d else None))
    return result


def build(a):
    a.output.mkdir(parents=True,exist_ok=True)
    protocol=load(a.input/'protocol.json')
    phases=load(a.input/'phases.json',{})
    stock={name:stock_map(a.input/'stock'/name.replace('/','_'),receipt)
           for name,receipt in load(a.input/'stock/maps.json',{}).items()}
    baseline=load(ROOT/protocol['policy']['baseline'])
    counts={name:sum(any(p.get(name,0)>0 for p in m['profiles'].values()) for m in stock.values())
            for name in ('local_calls','joint_calls','fitting_forward_calls')}
    stages=[
        ('Original observation capture',57,57,'Full immutable inventory; reused input capture'),
        ('Geometry/readiness',57,len(stock),'New native full-map capability preflight on selected maps'),
        ('Local candidate generation attempted',None,counts['local_calls'],'Bounded incomplete searches'),
        ('Global coordination attempted',0,len(stock),'Separate supported conditional profiles'),
        ('Joint::Solve entered',0,counts['joint_calls'],'Entry alone is not an accepted fit'),
        ('Numerical direct-source forward during joint',0,counts['fitting_forward_calls'],'Separate from local proposal forward scoring'),
        ('Independent validation callback',0,sum(any(p.get('controller',{}).get('validation_calls',0)>0 for p in m['profiles'].values()) for m in stock.values()),'Includes rejected/empty models'),
        ('Uncertainty assessment attempted',0,sum(any(p.get('controller',{}).get('uncertainty_attempts',0)>0 for p in m['profiles'].values()) for m in stock.values()),'No optimizer convergence promotion'),
        ('Frozen final held-out evaluation',0,sum(any(p.get('final_evaluation_performed') for p in m['profiles'].values()) for m in stock.values()),'Unavailable is not successful darkness'),
        ('Nonempty canonical publication',0,sum(m['canonical_nonempty_asset'] for m in stock.values()),'No original-source ground truth'),
        ('Prepared stock physical lighting field',0,0,'P02 geometry-only support is a separate result')]
    joint=load(a.input/'joint/run/numerical.json',{})
    field=load(a.input/'fields/run/summary.json',{})
    shadow=load(a.input/'shadow/run/summary.json',{})
    contributions=load(a.input/'contributions/run/results.json',{})
    queries=load(a.input/'queries/metrics.json',{})
    angular=load(a.input/'angular/run/quality.json',{})
    probes=load(a.input/'probes/run/stock.json',{})
    compiled=load(a.input/'compiled/run/compiled.json',{})
    indirect=load(a.input/'indirect/native-results.json',{})
    limits=protocol['policy'];gates={}
    s=field.get('semantic',{})
    for key in ('point_relative_error','spot_relative_error','rectangle_relative_error',
                'tube_relative_error','bounce_heldout_relative_rmse','fine_bounce_heldout_relative_rmse',
                'heldout_relative_rmse'):
        gates['field_'+key]=gate(s.get(key),limits['field_quality']['diffuse_relative_rmse_max'])
    gates['field_missing_support']=gate(s.get('heldout_unavailable'),0)
    for name,row in queries.get('queries',{}).items():
        gates['query_'+name+'_allocations']=gate(row['allocations'],0,row['ok']>0)
        gates['query_'+name+'_p95']=gate(row['p95_ns'],limits['queries']['p95_nanoseconds_soft_budget'],row['ok']>0)
    gates['wall_false_clear']=gate(shadow.get('semantic',{}).get('false_clear'),0)
    gates['area_fraction']=gate(shadow.get('semantic',{}).get('area_error'),.035)
    gates['tube_fraction']=gate(shadow.get('semantic',{}).get('tube_error'),.04)
    for name,m in stock.items():
        gates['stock_'+name+'_positive_accepted_fit']=dict(status='Unavailable',value=None,
            reason='No accepted nonempty source configuration; final held-out not reached')
    bridge=load(a.bridge/'bridge/results.json',{}) if a.bridge else {}
    for name,b in bridge.items():
        gates['blind_'+name+'_candidate_recall']=dict(status='Passed' if b['recall_gate_passed'] else 'Failed',
            value=b['candidate_set_recall'],minimum=limits['blind_bridge']['recall_gate'])
    for kind,path in [('sp',a.runtime),('mp',a.mp)]:
        for r in runtime_summary(path) if path else []:
            gates['runtime_'+kind+'_'+r['map']+'_'+r['profile']]=dict(status=r['status'],value=None,
                reason=r.get('reason') or 'Original frozen image tolerance; repeat controls retained')
    gpu=load(a.gpu/'report.json',{}) if a.gpu else {}
    if gpu:
        gates['historical_shader_baseline']=dict(status='Passed' if gpu.get('passed') else 'Failed',
            value=gpu.get('differences'),reason='No historical baseline rewrite')
    repeat=load(a.repeat/'results.json',{}) if a.repeat else {}
    if repeat:
        gates['same_input_semantic_repeat']=dict(status='Passed' if repeat.get('all_passed') else 'Failed',
            value=sum(repeat.get('checks',{}).values()),total=len(repeat.get('checks',{})))
        gates['compiler_full_BSP_byte_repeat']=dict(status='Passed' if all(x['byte_identical'] for x in repeat.get('compiler_rebake',{}).values()) else 'Failed',
            value=None,reason='Rebakes differ in lightgrid lumps 15/17; fixed immutable BSP solver replay is separate')
    result=dict(schema='org.openjk.reusable-lighting-validation/1',protocol=protocol,
        environment=load(a.input/'environment.json',{}),
        physical_deployment_readiness='NotReady',
        scientific_gate_counts=dict(Counter(x['status'] for x in gates.values())),
        phase_execution=phases,scientific_gates=gates,
        baseline=dict(schema=baseline['schema'],maps=len(baseline['maps']),
            sha256=sha(ROOT/protocol['policy']['baseline']),frozen_date='2026-10-31',overwritten=False),
        pipeline=dict(selected=len(stock),not_rerun=57-len(stock),
            stages=[dict(stage=n,old_57=o,new_selected=v,scope=scope) for n,o,v,scope in stages]),
        stock=stock,joint_controlled=joint,compiled_fixed_XYZ=compiled,
        field_builder=field,angular=angular_summary(angular) if angular else {},
        shadow=shadow,contributions=contributions,queries=queries,
        probes=probes,probe_controlled=load(a.input/'probes/run/controlled.json',{}),
        indirect=indirect,blind_bridge=bridge,determinism=repeat,
        solar=load(a.input/'solar/native.json',{}),
        shadow_association=load(a.input/'shadow_association/native-results.json',{}),
        uncertainty=load(a.input/'uncertainty/run/numerical.json',{}),
        runtime_sp=runtime_summary(a.runtime) if a.runtime else [],
        runtime_mp=runtime_summary(a.mp) if a.mp else [],
        runtime_cache=load(a.runtime_cache/'result.json',{}) if a.runtime_cache else {},
        runtime_lifecycle=load(a.runtime_cache/'lifecycle.json',{}) if a.runtime_cache else {},
        shader=load(a.gpu/'report.json',{}) if a.gpu else {},
        disabled_revision_delta=load(a.delta/'report.json',{}) if a.delta else {},
        constraints=['No stock XYZ/type/count or SI truth',
          'Controlled supplied geometry, blind discovery and supplied-answer oracle are distinct',
          'Conditional roots and material/profile alternatives are not physical exact lights',
          'No renderer consumers or runtime resources enabled',
          'OS timings share hardware with other independent agents, not isolated microbenchmarks'])
    if a.bridge:
        result['blind_compiler_recipes']={name:load(a.bridge/'bridge'/name/'recipe.json',{})
            for name in bridge}
    if a.cache_log and a.cache_log.exists():result['corrected_selective_cache_log']=a.cache_log.read_text()
    cache_log=a.input/'cache/correctness.log'
    result['cache_correctness_log']=cache_log.read_text() if cache_log.exists() else None
    def key_values(line):
        return {k:float(v) if any(c in v for c in '.eE') else int(v)
                for k,v in re.findall(r'(\w+)=([-+]?\d+(?:\.\d*)?(?:e[-+]?\d+)?)',line)}
    if result['cache_correctness_log']:
        line=next((s for s in result['cache_correctness_log'].splitlines() if s.startswith('METRICS ')),None)
        result['cache_measured']=key_values(line) if line else None
    if result.get('corrected_selective_cache_log'):
        result['cache_selective_measured']=[key_values(s) for s in result['corrected_selective_cache_log'].splitlines() if s.startswith(('SELECTIVE ','QUERY '))]
    write(a.output/'results.json',result)
    for name,source in [('angular.csv',a.input/'angular/run/quality.csv'),
                        ('field.csv',a.input/'fields/run/quality.csv'),
                        ('shadow.csv',a.input/'shadow/run/quality.csv')]:
        if source.is_file():shutil.copy2(source,a.output/name)
    with (a.output/'stock.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(['map','bsp_sha256','profile','units','proposal_units','joint_calls',
            'fitting_forward_calls','validation_calls','final_calls','mapped_area','supported_area_fraction',
            'selected_common','selected_requested','seconds','peak_rss_bytes','status'])
        for name,m in stock.items():
            for profile,p in m['profiles'].items():
                w.writerow([name,m['bsp_sha256'],profile,p['units'],p['units_with_candidates'],p.get('joint_calls'),
                    p.get('fitting_forward_calls'),p['controller'].get('validation_calls'),p['controller'].get('final_calls'),
                    m['mapped_area'],m['supported_area_fraction'],m['selected_common'],m['selected_requested'],
                    m['resources']['wall_seconds'],m['resources']['peak_process_rss_bytes'],p.get('status')])
    lines=['# V02 generated measured tables','',
        'Unavailable metrics remain null. Stock input coverage is not fitted coverage.','',
        '| Stage | Frozen 57-map baseline | New selected maps |',
        '|---|---:|---:|']
    for n,o,v,_ in stages:lines.append(f'| {n} | {o if o is not None else "not uniformly recorded"} | {v} |')
    lines+=['','| Map | Source proposals (isq/linear) | Joint calls | Supported mapped area | Time s | RSS MiB |',
            '|---|---:|---:|---:|---:|---:|']
    for name,m in stock.items():
        p=m['profiles'];lines.append('| '+name+' | '+
            '/'.join(str(x['units_with_candidates']) for x in p.values())+' | '+
            str(sum(x.get('joint_calls',0) for x in p.values()))+' | '+
            f"{100*m['supported_area_fraction']:.2f}% | {m['resources']['wall_seconds']:.3f} | "+
            f"{m['resources']['peak_process_rss_bytes']/1024**2:.2f} |")
    lines+=['','| Query | Successful calls | p50 us | p95 us | p99 us | Allocations |',
            '|---|---:|---:|---:|---:|---:|']
    for n,q in queries.get('queries',{}).items():
        lines.append(f"| {n} | {q['ok']}/{q['calls']} | {q['p50_ns']/1000:g} | {q['p95_ns']/1000:g} | {q['p99_ns']/1000:g} | {q['allocations']} |")
    (a.output/'tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    # Hash all evaluated inputs, not just the git parent of uncommitted work.
    sources=[*Path(__file__).parent.glob('*.py'),Path(__file__).with_name('policy-v1.json'),
        ROOT/'tools/rend2/lightrecon_regression.py',
        ROOT/'tools/lightrecon_test/lightrecon_regions_inspect.cpp',
        ROOT/'tools/lightrecon_test/lightrecon_validation_tool.cpp',
        ROOT/'tools/lightrecon_test/CMakeLists.txt']
    write(a.output/'manifest.json',dict(schema='org.openjk.validation-artifact-manifest/1',
        sources={p.relative_to(ROOT).as_posix():sha(p) for p in sources},
        artifacts={p.name:sha(p) for p in a.output.iterdir() if p.is_file() and p.name!='manifest.json'},
        copyrighted_map_payloads=False,source_answers='Owned controlled bridge only; stock answers unavailable'))
    print(json.dumps(dict(maps=len(stock),gates=dict(Counter(x['status'] for x in gates.values())))))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('input','output','bridge','runtime','mp','runtime-cache','gpu','delta','cache-log','repeat'):
        p.add_argument('--'+n,type=Path,required=n in ('input','output'))
    p.add_argument('--enforce',action='store_true',help='Exit nonzero on failed scientific gates after writing the complete report')
    a=p.parse_args();result=build(a)
    if a.enforce and any(x['status']=='Failed' for x in result['scientific_gates'].values()):
        raise SystemExit(1)
