#!/usr/bin/env python3
"""Seal X13 public count/resource witnesses; never includes licensed geometry.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import json
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import sha,write


def collect(run,out):
    native=json.loads((run/'native.json').read_text())
    if native['result']['failures'] or not native['repeat_semantics']:
        raise ValueError('Missing controlled deterministic evidence')
    provenance=json.loads((run/'manifest.json').read_text())
    for name,digest in provenance['sources'].items():
        if sha(ROOT/name)!=digest:raise ValueError('Source changed since frozen run: '+name)
    ctest=(ROOT/'build/x13-ctest.log').read_text()
    if '100% tests passed out of 38' not in ctest:raise ValueError('Full CPU suite did not pass')
    renderer=(ROOT/'build/x13-renderers-final.log').read_text()
    if not all(s in renderer for s in ('rdsp-rend2_x86_64.dll','rd-rend2_x86_64.dll')):
        raise ValueError('Missing SP/MP Release completion')
    out.mkdir(parents=True,exist_ok=False)
    for name in ('native.json','protocol.json','stock.json','source-semantics.json'):
        shutil.copyfile(run/name,out/name)
    for name in ('compiled.json','resources.json'):
        shutil.copyfile(ROOT/'build/x13-compiled-controls'/name,out/('controlled-'+name))
    stock=json.loads((out/'stock.json').read_text());rows=[]
    for name,record in stock.items():
        if record['resources']['state']!='Completed' or record['resources']['exit_code']:
            raise ValueError('Uncompleted map attempt must be explicitly reviewed')
        for profile in ('isq','linear'):
            r=record[profile];attempts=[a for b in r['branches'] for a in b.get('attempts',[])]
            failures={}
            for a in attempts:
                if not a['sources']:continue
                reason=a['reason']
                category='coverage' if reason.startswith('Joint comparable training coverage missing') else reason.split(':')[-1]
                failures[category]=failures.get(category,0)+1
            row=dict(map=name,profile=profile,joint_calls=r['joint_calls'],source_model_attempts=sum(a['sources']>0 for a in attempts),
                     conditional_placement_queries=r['conditional_placements'],proven_placement_queries=r['proven_placements'],
                     collision_placement_queries=r['collision_placements'],unknown_placement_queries=r['unknown_placements'],
                     fitting_forward_calls=r['fitting_forward_calls'],fitting_rays=r['fitting_rays'],
                     final_heldout_available=r['final_evaluated'],canonical_asset=r['controller']['canonical_asset_available'],
                     map_process_seconds=record['resources']['wall_seconds'],peak_rss_bytes=record['resources']['peak_process_rss_bytes'])
            rows.append(row)
            if r['occupancy']['bake_classes'][0]!=0:
                raise ValueError('Unverified stock records must not prove bake occupancy')
    with (out/'stages.csv').open('w',newline='',encoding='utf-8') as stream:
        w=csv.DictWriter(stream,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    checks=dict(cpu_tests=dict(passed=38,failed=0,log_sha256=sha(ROOT/'build/x13-ctest.log')),
                native=native['result'],sp_release=True,mp_release=True,
                renderer_log_sha256=sha(ROOT/'build/x13-renderers-final.log'),
                game_launched=False,runtime_consumer_enabled=False,
                limitations=['No accepted stock source model or final prediction',
                             'Existing Transfer::QueryPath unknown-motion policy is preserved',
                             'Existing unrelated renderer/worktree edits were not changed by X13'])
    checks['python']={}
    for name,count in (('occupancy',3),('blind',7),('controller',4)):
        path=ROOT/('build/x13-'+name+'-tests.log');log=path.read_text()
        if ('Ran '+str(count)+' tests') not in log or 'OK' not in log or 'skipped=' in log:
            raise ValueError('Missing Python tests: '+name)
        checks['python'][name]=dict(passed=count,log_sha256=sha(path))
    architecture=json.loads((ROOT/'build/x13-architecture.json').read_text())
    if not architecture['dependency_check_pass']:raise ValueError('Architecture boundary failed')
    write(out/'architecture.json',architecture)
    write(out/'checks.json',checks)
    for name in ('tools/rend2/occupancy/finalize.py','tools/rend2/occupancy/test_protocol.py','tools/lightrecon_test/CMakeLists.txt'):
        provenance['sources'][name]=sha(ROOT/name)
    provenance['artifacts']={p.name:sha(p) for p in out.iterdir()}
    provenance['primary_run_identity']=json.loads((run/'protocol.json').read_text())['native_sha256']
    write(out/'manifest.json',provenance)


def verify(folder):
    m=json.loads((folder/'manifest.json').read_text())
    for name,digest in m['sources'].items():
        if sha(ROOT/name)!=digest:raise ValueError('Changed implementation: '+name)
    for name,digest in m['artifacts'].items():
        if sha(folder/name)!=digest:raise ValueError('Changed receipt: '+name)
    print('X13 implementation and result hashes verified')


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--run',type=Path);p.add_argument('--out',type=Path);p.add_argument('--verify',type=Path);a=p.parse_args()
    if a.verify:verify(a.verify)
    elif a.run and a.out:collect(a.run,a.out)
    else:p.error('Use --run/--out or --verify')
