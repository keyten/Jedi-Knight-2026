#!/usr/bin/env python3
"""X15 frozen receiver capability census and bounded production stock fits.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write
MAPS=('academy1','mp/duel4','hoth3','hoth2')


def main(a):
    inventory=json.loads((a.private/'inventory.json').read_text())
    captures=json.loads((a.private/'captures.json').read_text())
    a.out.mkdir(parents=True,exist_ok=False)
    source_files=list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))+[ROOT/'tools/lightrecon_test'/f for f in ('receiver_audit.h','lightrecon_map_solver.cpp','scene_audit.h','lightrecon_receiver_support_test.cpp')]+[Path(__file__).resolve()]
    source_hashes={p.relative_to(ROOT).as_posix():sha(p) for p in source_files}
    policy=dict(schema='org.openjk.receiver-support-protocol.v1',input_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                native_sha256=sha(a.tool),test_sha256=sha(a.test),inventory_sha256=sha(a.private/'inventory.json'),
                maps=[],frozen_source_hashes=source_hashes,frozen_before_evaluation=True,uses_hidden_rgb_for_admission=False,
                scope='Full captured observation area census + X10 frozen folds; source-independent common isq/linear support; first two maps run actual bounded Joint',
                profile='UnknownProfile; two explicit conditional NetRadiant alternatives, no historical identity inference',
                process_bytes=1024**3,process_seconds=600,
                environment=dict(os=platform.platform(),cpu=platform.processor(),python=platform.python_version()))
    datasets={}
    for name in MAPS:
        item=next(m for m in inventory['maps'] if m['map']==name)
        data=a.private/'runs'/name.replace('/','_')/'all-pages.jkobs'
        if not data.exists():data=Path(captures[name]['dataset'])
        datasets[name]=data
        policy['maps'].append(dict(map=name,bsp_sha256=item['sha256'],dataset_sha256=sha(data),dataset_bytes=data.stat().st_size,
                                   mode='scene-receiver-preflight' if name in MAPS[:2] else 'receiver-support-only'))
    write(a.out/'protocol.json',policy)
    native=a.private/('runs-x15-'+a.out.name+'-native.exe')
    if native.exists():raise ValueError('Preserve previous executable')
    shutil.copyfile(a.tool,native)
    controlled=[];resources=[]
    for label in ('first','repeat'):
        path=a.out/('native-'+label+'.json');r=execute([a.test,path],a.out,'native-'+label,seconds=180,memory=256*1024**2)
        if r['state']!='Completed' or r['exit_code']:raise ValueError('Controlled preflight regression failed')
        controlled.append(json.loads(path.read_text()));resources.append(r)
    if controlled[0]!=controlled[1]:raise ValueError('Native numerical nondeterminism')
    write(a.out/'native.json',dict(result=controlled[0],resources=resources,repeat_semantics=True))
    results={}
    for item in policy['maps']:
        name=item['map'];folder=a.private/('runs-x15-'+a.out.name)/name.replace('/','_');folder.mkdir(parents=True,exist_ok=False)
        if sha(native)!=policy['native_sha256'] or sha(datasets[name])!=item['dataset_sha256']:raise ValueError('Changed frozen input')
        r=execute([native,datasets[name],folder,item['bsp_sha256'],item['mode']],folder,'native',seconds=policy['process_seconds'],memory=policy['process_bytes'])
        record=dict(input=item,resources=r)
        for source,target in [('receiver-support.json','census'),('census.json','original_census')]:
            if (folder/source).exists():record[target]=json.loads((folder/source).read_text())
        if (folder/'receiver-support-manifest.json').exists():
            target=a.out/(name.replace('/','_')+'-manifest.json');shutil.copyfile(folder/'receiver-support-manifest.json',target);record['manifest_sha256']=sha(target)
        record['profiles']={}
        for profile in ('isq','linear'):
            path=folder/('scheduler-'+profile+'.json')
            if path.exists():
                raw=json.loads(path.read_text());summary={k:v for k,v in raw.items() if k not in ('units','influence')}
                summary['local_units']=len(raw.get('units',[]));summary['unit_statuses']=dict(Counter(x['status'] for x in raw.get('units',[])))
                record['profiles'][profile]=summary
                controller=folder/('scheduler-'+profile+'-controller.json')
                if controller.exists():summary['controller']=json.loads(controller.read_text())
                source=folder/('joint-'+profile+'-receiver-manifest.json')
                if source.exists():shutil.copyfile(source,a.out/(name.replace('/','_')+'-'+profile+'-joint-manifest.json'))
        if r['exit_code'] or r['state']!='Completed':record['reason']=(folder/'native.log').read_text(errors='replace')[-1600:]
        results[name]=record;write(a.out/'stock.json',results)
        print(name,r['state'],record.get('census',{}).get('selected_common'),{p:x.get('joint_calls') for p,x in record['profiles'].items()},flush=True)
    # Repeat the predeclared small MP receiver domain. It cannot see final RGB.
    name='mp/duel4';item=next(m for m in policy['maps'] if m['map']==name);folder=a.private/('runs-x15-'+a.out.name)/'mp_duel4-repeat';folder.mkdir(parents=True,exist_ok=False)
    r=execute([native,datasets[name],folder,item['bsp_sha256'],'receiver-support-only'],folder,'native',seconds=600,memory=1024**3)
    candidate=json.loads((folder/'receiver-support.json').read_text());expected=dict(results[name]['census']);candidate.pop('seconds',None);expected.pop('seconds',None)
    equal=candidate==expected
    # Scene sampling retains extra local TRAINING rows; final folds remain identical.
    expected_manifest=json.loads((a.out/'mp_duel4-manifest.json').read_text());repeat_manifest=json.loads((folder/'receiver-support-manifest.json').read_text())
    repeat_roles={role:[e for e in repeat_manifest['entries'] if e['role']==role] for role in (1,2)}
    expected_roles={role:[e for e in expected_manifest['entries'] if e['role']==role] for role in (1,2)}
    write(a.out/'repeat.json',dict(resources=r,full_census_semantic_match=equal,independent_fold_geometry_match=repeat_roles==expected_roles,
                                  note='Discovery retains local training only in scene mode; no final-target guided sampling'))
    if repeat_roles!=expected_roles:raise ValueError('Independent fold geometry changed')
    if any(sha(ROOT/name)!=digest for name,digest in source_hashes.items()):raise ValueError('Sources changed during frozen experiment; retain as a development attempt, not a final witness')
    write(a.out/'manifest.json',dict(schema='org.openjk.receiver-support-results.v1',sources=source_hashes,
                                   artifacts={p.name:sha(p) for p in a.out.iterdir() if p.is_file() and p.name!='manifest.json'},licensed_payloads_committed=False))


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--private',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--tool',type=Path,default=ROOT/'build/s06-gcc/lightrecon_map_solver.exe');p.add_argument('--test',type=Path,default=ROOT/'build/s06-gcc/lightrecon_receiver_support_test.exe')
    main(p.parse_args())
