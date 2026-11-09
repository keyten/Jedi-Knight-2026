#!/usr/bin/env python3
"""Freeze/run/evaluate full IDF experiments. Answers never reach solve().
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import gc
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.idf_full.algorithm import Response,Field,solve,np
from tools.rend2.idf_full.fixtures import author
from tools.rend2.idf.prototype import Oracle
from tools.rend2.idf.run import read,write,pin,receiver_file,sha
from tools.rend2.evaluation.metrics import detection
import scipy

HERE=Path(__file__).resolve().parent


def backend(data,native):
    oracle=Oracle(native,data['geometry'],data['receiver_file']) if data.get('geometry') else None
    return Response(data['positions'],data['normals'],data['bounds'],oracle)


def freeze(a):
    a.work.mkdir(parents=True,exist_ok=False);policy=read(a.policy)
    native=ROOT/'build/asr02-tools/idf_geometry.exe'
    protocol=dict(schema='org.openjk.idf-full-protocol/1',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        policy=pin(a.policy),native=pin(native),cases={},
        sources={p.relative_to(ROOT).as_posix():sha(p) for p in sorted(HERE.glob('*.py'))},
        production={p.relative_to(ROOT).as_posix():sha(p) for p in sorted((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))},
        papers={p.name:pin(p) for p in (ROOT/'build/light-localization-research').glob('zhang2018*.pdf')},
        environment=dict(python=sys.version,numpy=np.__version__,scipy=scipy.__version__,threads=1),
        dependency_sha256={str(p):sha(p) for p in sorted((ROOT/'build/idf-full-deps/scipy/optimize').rglob('*')) if p.is_file() and p.suffix in ('.py','.pyd')})
    old=read(ROOT/policy['compiled']['input_protocol'])
    for name in policy['cases']:
        folder=a.work/name;folder.mkdir();controller=folder/'controller';controller.mkdir()
        if name.startswith('linear_'):
            data,answer=author(name,policy['linear_box']['side_samples'])
            train=data['train'];bounds=policy['linear_box']['bounds'];unit='AuthoredLinearDiffuse'
            for role in ('validation','heldout'):write(controller/(role+'.json'),data[role])
        else:
            fixture=name.removeprefix('compiled_');original=old['cases'][fixture]
            rows=read(original['rows']['path']);saturated=sum(max(r['raw'])==255 for r in rows)
            rows=[r for r in rows if max(r['raw'])<255]
            train=dict(positions=[r['position'] for r in rows],normals=[r['normal'] for r in rows],
                       values=[[(c+.5) for c in r['raw']] for r in rows],ids=[r['id'] for r in rows],
                       source_geometry_sha256=original['geometry']['sha256'],excluded_saturated=saturated)
            train['geometry']=original['geometry']['path'];receiver_file(folder/'receivers.txt',rows)
            train['receiver_file']=str(folder/'receivers.txt');bounds=original['bounds']
            unit='ConditionalGamma1PreencodingMidpoints_NotCalibratedIrradiance'
            # Answer remains sealed; only its hash/path are frozen now.
            answer=dict(external_recipe=original['answer'])
            for role in ('validation','heldout'):
                write(controller/(role+'.json'),dict(external_partition=original['sealed_evaluation_hashes'][role]))
        scale=float(np.max(train['values']));train.update(bounds=bounds,observation_domain=unit,normalization=scale,
              values=(np.asarray(train['values'])/scale).tolist())
        write(folder/'training.json',train);write(controller/'answer.json',answer)
        # Geometry-only field preparation: no observation values or answers used.
        response=backend(train,native)
        try:
            field=Field(response,policy)
            keys=np.array(list(response.cache));coefficients=np.array(list(response.cache.values()))
            np.savez_compressed(folder/'geometry-cache.npz',positions=keys,coefficients=coefficients)
            preparation=dict(source_positions=response.queries,unknown_coefficients=response.unknown,
                invalid_positions=response.invalid,rays=response.oracle.rays if response.oracle else None,
                traversal=response.oracle.work if response.oracle else None,
                cached_coefficients=int(coefficients.size),field_uses_target_RGB=False)
        finally:
            if response.oracle:response.oracle.close()
        protocol['cases'][name]=dict(training=pin(folder/'training.json'),cache=pin(folder/'geometry-cache.npz'),
             evaluator={role:pin(controller/(role+'.json')) for role in ('answer','validation','heldout')},preparation=preparation)
        write(a.work/'protocol.json',protocol)
        print('Frozen',name,'training rows',len(train['values']),'geometry positions',preparation['source_positions'],flush=True)
        del response,field,keys,coefficients;gc.collect()
    write(a.work/'protocol.json',protocol)


def restore_response(data,native,cache):
    response=backend(data,native)
    with np.load(cache,allow_pickle=False) as stored:
        response.cache={tuple(p):v for p,v in zip(stored['positions'],stored['coefficients'])}
    return response


def evaluate(a):
    protocol=read(a.work/'protocol.json');policy=read(protocol['policy']['path'])
    for p in [protocol['policy'],protocol['native'],*protocol['papers'].values()]:
        if sha(p['path'])!=p['sha256']:raise ValueError('Frozen dependency changed')
    for group in ('sources','production'):
        for p,digest in protocol[group].items():
            if sha(ROOT/p)!=digest:raise ValueError('Frozen code changed: '+p)
    for p,digest in protocol['dependency_sha256'].items():
        if sha(p)!=digest:raise ValueError('Numerical dependency changed')
    for c in protocol['cases'].values():
        for p in [c['training'],c['cache'],*c['evaluator'].values()]:
            if sha(p['path'])!=p['sha256']:raise ValueError('Frozen experiment input changed')
    a.out.mkdir(parents=True,exist_ok=False);all_cases={};resources=[]
    # Complete every model before opening any evaluation RGB or source answers.
    for name in policy['cases']:
        c=protocol['cases'][name]
        data=read(c['training']['path']);results={};folder=a.work/name
        for variant in policy['variants']:
            copies=[]
            for repeat in range(2):
                response=restore_response(data,protocol['native']['path'],c['cache']['path']);started=time.perf_counter()
                try:r=solve(response,np.asarray(data['values']),policy,variant)
                finally:
                    if response.oracle:response.oracle.close()
                elapsed=time.perf_counter()-started;resources.append(dict(case=name,variant=variant,repeat=repeat,seconds=elapsed))
                write(folder/(variant+f'-{repeat}.json'),r);copies.append(r)
                del response;gc.collect()
            if copies[0]!=copies[1]:raise ValueError('Non-deterministic full IDF result')
            results[variant]=copies[0];results[variant]['repeat_equal']=True
            print(name,variant,results[variant]['state'],
                  len(results[variant]['model']['positions']) if results[variant]['model'] else None,
                  results[variant]['model']['mse'] if results[variant]['model'] else None,flush=True)
        all_cases[name]=results
        write(a.out/'models.json',all_cases)
    metrics={}
    for name,c in protocol['cases'].items():
        data=read(c['training']['path']);answer=read(c['evaluator']['answer']['path'])
        if 'external_recipe' in answer:
            sealed=answer['external_recipe']
            if sha(sealed['path'])!=sealed['sha256']:raise ValueError('Answer changed')
            recipe=read(sealed['path'])
            truth=[dict(kind=s['type'],position=s['position_world']) for s in recipe['ground_truth']['lights']]
        else:truth=[dict(kind='point',position=s['position']) for s in answer['sources']]
        metrics[name]={}
        for variant,result in all_cases[name].items():
            model=result['model'];candidates=result['initial_proposals']
            if model and model['intensities']:
                intensities=np.sum(model['intensities'],axis=1);maximum=float(np.max(intensities))
                chosen=[i for i,q in enumerate(intensities) if q>max(1e-6,maximum*policy['evaluation_active_intensity_fraction'])]
                candidates=[dict(position=model['positions'][i]) for i in chosen]
            elif model:candidates=[]
            pred=[dict(kind='point',position=c['position']) for c in candidates]
            span=max(data['bounds'][i+3]-data['bounds'][i] for i in range(3))
            matching={}
            tolerances={str(t)+'_width':t*span for t in policy['position_tolerances_fraction']}
            if name.startswith('compiled_'):tolerances['32_WU']=32
            for label,tolerance in tolerances.items():
                d=detection(truth,pred,dict(position_world=tolerance,angle_degrees=10,cone_degrees=10,dimension_relative=.25,roll_degrees=15),1.)
                matching[label]={k:d[k] for k in ('true_positive','false_positive','false_negative','precision','recall')}
            fold_errors={}
            if model:
                for role in ('validation','heldout'):
                    evaluation=read(c['evaluator'][role]['path'])
                    if 'external_partition' in evaluation:
                        record=evaluation['external_partition']
                        if sha(record['path'])!=record['sha256']:raise ValueError('Held-out input changed')
                        rows=[json.loads(line) for line in Path(record['path']).read_text().splitlines()]
                        rows=[r for r in rows if max(r['raw'])<255][:64]
                        receivers=a.work/name/(role+'-receivers.txt');receiver_file(receivers,rows)
                        evaluation=dict(positions=[r['position'] for r in rows],normals=[r['normal'] for r in rows],
                            values=[[c+.5 for c in r['raw']] for r in rows],geometry=data['geometry'],receiver_file=str(receivers))
                    ev=dict(evaluation,bounds=data['bounds'])
                    response=backend(ev,protocol['native']['path'])
                    try:
                        factors=response.matrix(model['positions']);target=np.asarray(evaluation['values'])/data['normalization']
                        if np.all(np.isfinite(factors)):
                            prediction=factors@np.asarray(model['intensities']).reshape(len(model['positions']),target.shape[1])
                            fold_errors[role]=dict(mse=float(np.mean((prediction-target)**2)),relative_rmse=float(np.sqrt(np.sum((prediction-target)**2)/np.sum(target**2))),rows=len(target))
                        else:fold_errors[role]=dict(state='Unavailable',reason='Required prediction unknown',rows=len(target))
                    finally:
                        if response.oracle:response.oracle.close()
            metrics[name][variant]=dict(matching=matching,fold_errors=fold_errors,active_sources=len(candidates),
                  source_positions=[c['position'] for c in candidates],
                  source_truth_used_for_selection=False,accepted_physical_sources=0)
    write(a.out/'metrics.json',metrics);write(a.out/'resources.json',resources)
    write(a.out/'protocol.json',protocol)
    write(a.out/'manifest.json',dict(artifacts={p.name:sha(p) for p in a.out.iterdir() if p.is_file()},
                                   production_modified=False,historical_certification='NotCertified'))
    print('Frozen post-fit evaluation exported',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('phase',choices=('freeze','evaluate'))
    parser.add_argument('--work',type=Path,required=True);parser.add_argument('--out',type=Path)
    parser.add_argument('--policy',type=Path,default=HERE/'policy-v2.json')
    args=parser.parse_args();args.work=args.work.resolve()
    if args.phase=='evaluate' and args.out is None:parser.error('evaluate requires --out')
    (freeze if args.phase=='freeze' else evaluate)(args)
