#!/usr/bin/env python3
"""Trusted controller: score frozen external forward predictions, never fit.
Only the solver's inputs/ directory may be mounted into its process.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
from acceptance import load,digest,evaluate
from fixtures import write


def score(manifest_path,prediction_path):
    manifest=load(manifest_path);packet=load(prediction_path)
    if manifest.get('schema')!='org.openjk.x05-benchmark-manifest.v1':raise ValueError('Manifest schema')
    if (set(packet)!={'schema','manifest_sha256','cases'} or packet['schema']!='org.openjk.forward-predictions.v1'
        or packet['manifest_sha256']!=digest(manifest_path)):raise ValueError('Frozen prediction identity/fields')
    fixtures={f['id']:f for f in manifest['fixtures']};required={k for k,v in fixtures.items() if v['required']}
    if set(packet['cases'])-set(fixtures):raise ValueError('Unknown fixture prediction')
    results={}
    for key in sorted(required|set(packet['cases'])):
        f=fixtures[key];path=Path(f['private_case'])
        if digest(path)!=f['private_case_sha256']:raise ValueError('Private targets changed')
        case=load(path)
        if case['map_sha256']!=f['bsp_sha256'] or case['id']!=key:raise ValueError('Cross-map/case mismatch')
        results[key]=evaluate(case['rows'],packet['cases'].get(key,[]),case['contracts'],manifest['policy'],case['output_profile'])
    return dict(schema='org.openjk.forward-score.v1',cases=results,
        status='Passed' if required and all(results[k]['success'] for k in required) else 'Failed',
        scope='Required compiled positive/negative forward contracts only; profile/encoding/derivative/unsupported contracts require their separate diagnostics. This score is never full solver success.',
        no_emitter_recovery=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--predictions',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();r=score(args.manifest,args.predictions);write(args.output,r)
    print(r['status']);return 0 if r['status']=='Passed' else 1


if __name__=='__main__':raise SystemExit(main())
