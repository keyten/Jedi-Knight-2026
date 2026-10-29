#!/usr/bin/env python3
"""Trusted compact compiler-backed S06 report; no raw images or held-out pixels committed."""
import argparse
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]

def semantic(v):
    if isinstance(v,dict):return {k:semantic(x)for k,x in v.items()if k not in ('timing','resources','seconds','evaluation_work','frozen_fit_sha256')}
    if isinstance(v,list):return [semantic(x)for x in v]
    return v

def main():
    p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--repeat',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    first=json.loads((a.run/'summary.json').read_text());second=json.loads((a.repeat/'summary.json').read_text())
    if semantic(first)!=semantic(second):raise ValueError('Two complete compiled-fixture runs differ')
    report=dict(schema='org.openjk.s06-compiled-witness',version=1,repeated_semantics_identical=True,cases={},source_sha256={})
    resources={}
    for name,case in first['cases'].items():
        rows={};resources[name]={}
        for model,entry in case['models'].items():
            fit=entry['fit'];evaluation=entry['evaluation'];production=fit['production']
            rows[model]=dict(fitted_q=fit['fitted_rgb'],frozen_parameter_sha256=fit['frozen_parameter_sha256'],profile_key=fit['profile_key'],
                input_partition_hashes=fit['partition_hashes'],source_parameters=evaluation['source_parameters'],
                prediction=evaluation['prediction'],production=production,hidden_read_denials=fit['denied_private_reads'],
                protocol=fit['protocol'],supplied_geometry=fit['candidate_geometry'],converged=fit['converged'],
                parameter_slices=fit['conditional_feasible_slices'],heldout_positive_prediction_gate=evaluation['heldout_positive_prediction_gate'])
            if model=='correct':rows[model]['oracle_heldout']=evaluation['true_amplitudes_strict_cpp_heldout']
            resources[name][model]=dict(process=entry['resources'],fit=fit['timing'])
        report['cases'][name]=dict(bsp_sha256=case['bsp_sha256'],source_capture_sha256=case['source_capture_sha256'],models=rows)
    files=['shared/rd-rend2/lightrecon/'+n for n in ('amplitude_fit.h','amplitude_fit.cpp','bake_forward.h','bake_forward.cpp')]+['tools/lightrecon_test/bake_fit_server.h','tools/rend2/amplitude_fit/worker.py','tools/rend2/amplitude_fit/run.py']
    report['source_sha256']={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest()for s in files}
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'compiled.json').write_text(json.dumps(report,sort_keys=True,indent=2,allow_nan=False)+'\n')
    (a.out/'resources.json').write_text(json.dumps(resources,sort_keys=True,indent=2,allow_nan=False)+'\n')
    lines=['fixture,model,train_interval_rmse,validation_interval_rmse,heldout_interval_rmse,black_heldout_rmse,converged,forward_calls,rays']
    for name,case in report['cases'].items():
        for model,row in case['models'].items():
            e=row['prediction'];w=row['production']['work'];lines.append(','.join(map(str,[name,model,*[e[p]['area_interval_rmse']for p in ('train','validation','heldout')],e['heldout']['black_interval_rmse'],row['converged'],w['forward_calls'],w['rays']])))
    (a.out/'compiled.csv').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))

if __name__=='__main__':main()
