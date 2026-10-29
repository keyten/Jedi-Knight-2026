#!/usr/bin/env python3
"""S06 compiler-backed blind evaluation reusing X07 capture/splits/controllers."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'fixed_geometry'))
spec=importlib.util.spec_from_file_location('x07_controller',HERE.parent/'fixed_geometry/run.py')
x=importlib.util.module_from_spec(spec);spec.loader.exec_module(x)

def semantic(value):
    if isinstance(value,dict):return {k:semantic(v)for k,v in value.items()if k not in ('timing','resources','seconds','evaluation_work','frozen_fit_sha256')}
    if isinstance(value,list):return [semantic(v)for v in value]
    return value

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--exe',type=Path,required=True);p.add_argument('--only',choices=x.FIXTURES)
    p.add_argument('--datasets',type=Path,default=ROOT/'build/x02/compiled-fixtures-final');p.add_argument('--bakes',type=Path,default=ROOT/'build/q3map2-bake-verified')
    p.add_argument('--quality',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe');p.add_argument('--dataset-tool',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_tool.exe')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);report=dict(schema='org.openjk.s06-compiler-validation',version=1,cases={})
    for name in ((a.only,)if a.only else x.FIXTURES):
        case=x.create_case(name,out,a.datasets.resolve(),a.bakes.resolve()/'authoring/base/maps',a.bakes.resolve()/'answers',a.quality.resolve(),a.dataset_tool.resolve())
        models=['correct','ambient','wrong_profile','wrong_xyz']+(['dropped_source']if name=='two_colour'else[])
        records={};denied=[case['case']/'views/evaluator/heldout.jsonl',case['case']/'views/evaluator/validation.jsonl',case['case']/'private/answer.json']
        for model in models:
            folder=case['case']/model;inputs,cfg,settings,sources=x.install_model(case,model,folder)
            fit,resources=x.launch_worker(a.exe.resolve(),inputs,folder/'work',denied,worker_script=HERE/'worker.py')
            result=x.evaluate_model(a.exe.resolve(),case,model,folder,fit,cfg,settings,sources)
            records[model]=dict(fit=fit,evaluation=result,resources=resources)
            print(name,model,result['prediction']['heldout']['area_interval_rmse'],flush=True)
        repeat,resources=x.launch_worker(a.exe.resolve(),case['case']/'correct/inputs',case['case']/'repeat/work',denied,worker_script=HERE/'worker.py')
        if semantic(repeat)!=semantic(records['correct']['fit']):raise ValueError('Repeated C++ fit differs')
        if not records['correct']['evaluation']['heldout_positive_prediction_gate']:raise ValueError('Positive-light held-out gate failed '+name)
        report['cases'][name]=dict(bsp_sha256=x.digest(case['bsp']),source_capture_sha256=case['source_capture_sha256'],models=records,repeated_semantics_identical=True)
        x.write(out/'summary.json',report)
    return 0

if __name__=='__main__':raise SystemExit(main())
