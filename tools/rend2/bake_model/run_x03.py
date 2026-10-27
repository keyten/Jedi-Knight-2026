#!/usr/bin/env python3
"""One reproducible X03 research suite; every optional stage is explicitly reported.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
from hypotheses import catalogue, file_hash, VERSION
from identifiability import run_synthetic, write, feature_assessment
from confounding import analytic_experiments, paired_bootstrap
import sensitivity
import compiled_profiles
import stock_profiles


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compiled-bakes',type=Path)
    p.add_argument('--stock-root',type=Path)
    p.add_argument('--ray-tool',type=Path)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    starting_hashes={str(p.name):file_hash(p) for p in Path(__file__).parent.glob('*.py')}
    for name in ('synthetic','compiled','stock'): (args.output/name).mkdir()
    registry=catalogue(); write(args.output/'hypotheses.json',{k:asdict(v) for k,v in registry.items()})
    lock=json.loads((Path(__file__).parent/'source-lock.json').read_text())
    inspected=[]
    for version in lock['versions']:
        for entry in version['files']:
            path=Path(entry['inspection_path'])
            if not path.exists():
                inspected.append(dict(path=str(path),status='LocalSourceUnavailable')); continue
            digest=hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
            if digest!=entry['lf_sha256']: raise ValueError('Source-lock mismatch: '+str(path))
            inspected.append(dict(path=str(path),status='HashMatchesLock',sha256=digest))
    result=dict(schema='org.openjk.bake-identifiability.v1',formula_version=VERSION,
        analytic=analytic_experiments(),synthetic=run_synthetic(args.output/'synthetic'),
        sensitivity=sensitivity.run(),compiled=None,stock=None,
        claims=dict(A='known-compiler formula checks only',B='conditional validation preference',
                    C='NOT recovered; stock remains UnknownProfile'),
        source_lock_validation=inspected,
        model_discrepancy='Unknown; no arbitrary normalized confidence/posterior',solver_implemented=False)
    if args.compiled_bakes: result['compiled']=compiled_profiles.run(args.compiled_bakes,args.output/'compiled')
    if args.stock_root:
        if not args.ray_tool: raise ValueError('--stock-root requires CPU --ray-tool')
        result['stock']=stock_profiles.run(args.stock_root,args.ray_tool,args.output/'stock')
    for case in result['synthetic'].values():
        for protocol in case['protocols'].values():
            r=protocol['results']
            if len(r)>=2 and r[0].get('validation') and r[1].get('validation'):
                protocol['paired_validation_resampling']=paired_bootstrap(r[0]['validation']['block_mse'],r[1]['validation']['block_mse'])
            protocol['feature_assessment']=feature_assessment(protocol,registry)
    result['source_sha256']={str(p.name):file_hash(p) for p in Path(__file__).parent.glob('*.py')}
    if result['source_sha256']!=starting_hashes:
        raise RuntimeError('Research source files changed during the run; result not published')
    result['input_catalogue_sha256']=file_hash(Path(__file__).parent/'profiles.json')
    result['source_lock_sha256']=file_hash(Path(__file__).parent/'source-lock.json')
    write(args.output/'result.json',result)
    print('X03 suite complete; no historical stock compiler identified',flush=True)


if __name__=='__main__': main()
