#!/usr/bin/env python3
"""X13 frozen occupancy audit; native source search/fitting, no stock truth invented.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write
MAPS=('t1_danger','mp/siege_desert','hoth3','hoth2')


def entities(raw):
    tokens=re.findall(r'"((?:\\.|[^"\\])*)"|([{}])',raw.decode('latin1').rstrip('\0'))
    out=[];record=None;key=None
    for text,brace in tokens:
        if brace=='{':
            if record is not None: raise ValueError('Nested entity')
            record={}
        elif brace=='}':
            if record is None or key is not None: raise ValueError('Malformed entity')
            out.append(record);record=None
        elif record is not None:
            if key is None:key=text.lower()
            else:record[key]=text;key=None
    if record is not None:raise ValueError('Truncated entities')
    return out


def source_semantics(item):
    # Licensed data stay in memory; only aggregate ownership/key-presence statistics leave here.
    with zipfile.ZipFile(item['archive']) as z: raw=z.read(item['member'])
    if hashlib.sha256(raw).hexdigest()!=item['sha256']:raise ValueError('Original BSP changed')
    if raw[:8]!=b'RBSP\x01\0\0\0':raise ValueError('Unsupported original BSP')
    def lump(n,stride=1):
        start,length=struct.unpack_from('<II',raw,8+8*n)
        if start+length>len(raw) or length%stride:raise ValueError('Corrupt BSP lump')
        return raw[start:start+length]
    e=entities(lump(0));models=lump(7,40);surfaces=lump(13,148);count=len(models)//40
    bymodel={}
    for entry in e:
        m=re.fullmatch(r'\*(\d+)',entry.get('model',''))
        if m:bymodel.setdefault(int(m[1]),[]).append(entry)
    classes=Counter();keys=Counter();mapped=0;lm=0;surfs=0;ambiguous=0
    for index in range(1,count):
        entries=bymodel.get(index,[]);mapped+=bool(entries);ambiguous+=len(entries)>1
        for entry in entries:
            classes[entry.get('classname','Unknown')]+=1
            for key in ('origin','angles','angle','lip','height','speed','target','spawnflags','pos1','pos2'):
                keys[key]+=key in entry
        first,n=struct.unpack_from('<ii',models,index*40+24)
        if first<0 or n<0 or first+n>len(surfaces)//148:raise ValueError('Invalid surface ownership')
        surfs+=n
        for s in range(first,first+n):
            lm+=any(v>=0 for v in struct.unpack_from('<4i',surfaces,s*148+36))
    return dict(model_count=count,non_world_models=count-1,models_with_entity_reference=mapped,
                multiply_referenced_models=ambiguous,entity_classes=dict(sorted(classes.items())),
                declared_key_presence=dict(sorted(keys.items())),non_world_surfaces=surfs,
                non_world_lightmapped_surfaces=lm,
                bake_participation='Unknown; a lightmap record does not prove any selected world bake pose',
                captured_bounds='Original model-lump bounds, not a measured gameplay/current transform',
                runtime_sweeps_inferred=False,original_bake_state_inferred=False)


def freeze(private,tool,test,out,revision):
    if out.exists():raise ValueError('Preserve frozen X13 policy')
    inventory=json.loads((private/'inventory.json').read_text());captures=json.loads((private/'captures.json').read_text())
    maps=[]
    for name in MAPS:
        item=next(m for m in inventory['maps'] if m['map']==name)
        path=private/'runs'/name.replace('/','_')/'all-pages.jkobs'
        if not path.exists():path=Path(captures[name]['dataset'])
        maps.append(dict(map=name,bsp_sha256=item['sha256'],dataset_sha256=sha(path),dataset_bytes=path.stat().st_size))
    write(out,dict(schema='org.openjk.bake-occupancy-protocol.v1',revision=revision,
        input_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        inventory_sha256=sha(private/'inventory.json'),maps=maps,native_sha256=sha(tool),test_sha256=sha(test),
        mode='scene-bake-conditional',frozen_before_evaluation=True,
        policy='X12 source/fold/profile/work budgets retained; explicit X13 conditional placement; independent visibility/GI uncertainty retained',
        original_x12_sha256=sha(ROOT/'tools/rend2/stock_joint_fits/results/v1/stock.json'),
        environment=dict(os=platform.platform(),cpu=platform.processor(),python=platform.python_version()),
        profile='Independent assumed NetRadiant inverse-square/linear branches, stock UnknownProfile',
        absolute_photometry='Unknown',source_xyz_truth='Unavailable',uses_heldout_for_search=False,
        process_seconds=600,process_bytes=1024**3))


def run(private,tool,test,protocol,out):
    policy=json.loads(protocol.read_text());revision=policy['revision']
    if sha(tool)!=policy['native_sha256'] or sha(test)!=policy['test_sha256']:raise ValueError('Executable changed after freeze')
    if sha(private/'inventory.json')!=policy['inventory_sha256']:raise ValueError('Inventory changed')
    out.mkdir(parents=True,exist_ok=False)
    write(out/'protocol.json',policy)
    frozen=private/('runs-'+revision+'-native.exe')
    if frozen.exists():raise ValueError('Preserve old private executable')
    shutil.copyfile(tool,frozen)
    native=[];resources=[]
    for suffix in ('first','repeat'):
        path=out/(suffix+'.json');r=execute([test,path],out,'native-'+suffix,seconds=120,memory=256*1024**2)
        if r['exit_code'] or r['state']!='Completed':raise ValueError('Controlled X13 regression failed')
        native.append(json.loads(path.read_text()));resources.append(r)
    if native[0]!=native[1]:raise ValueError('Changed deterministic native evidence')
    write(out/'native.json',dict(result=native[0],repeat_semantics=True,resources=resources))
    inventory=json.loads((private/'inventory.json').read_text());captures=json.loads((private/'captures.json').read_text())
    reports={};semantics={}
    for entry in policy['maps']:
        name=entry['map'];item=next(m for m in inventory['maps'] if m['map']==name)
        semantics[name]=source_semantics(item);write(out/'source-semantics.json',semantics)
        data=private/'runs'/name.replace('/','_')/'all-pages.jkobs'
        if not data.exists():data=Path(captures[name]['dataset'])
        if sha(data)!=entry['dataset_sha256']:raise ValueError('Changed dataset')
        folder=private/('runs-'+revision)/name.replace('/','_');folder.mkdir(parents=True,exist_ok=False)
        r=execute([frozen,data,folder,item['sha256'],policy['mode']],folder,'native',seconds=policy['process_seconds'],memory=policy['process_bytes'])
        record=dict(bsp_sha256=item['sha256'],dataset_sha256=entry['dataset_sha256'],resources=r,
                    historical_profile='UnknownProfile',source_truth='Unavailable')
        for profile in ('isq','linear'):
            path=folder/('scheduler-'+profile+'.json')
            if path.exists():
                raw=json.loads(path.read_text());summary={k:v for k,v in raw.items() if k not in ('units','influence')}
                summary['local_units']=len(raw.get('units',[]));summary['unit_statuses']=dict(Counter(x['status'] for x in raw.get('units',[])))
                for label in ('controller','occupancy'):
                    p=folder/('scheduler-'+profile+'-'+label+'.json')
                    if p.exists():summary[label]=json.loads(p.read_text())
                record[profile]=summary
            else:record[profile]=dict(status='Unavailable',reason='No completed scoped scheduler report')
        record['private_debug_hashes']={p.name:sha(p) for p in folder.glob('scheduler-*-placement.tsv')}
        reports[name]=record;write(out/'stock.json',reports)
        print(name,r['state'],[(p,record[p].get('status'),record[p].get('fitting_forward_calls')) for p in ('isq','linear')],flush=True)
    files=list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))+[ROOT/'tools/lightrecon_test/lightrecon_map_solver.cpp',ROOT/'tools/lightrecon_test/scene_audit.h',ROOT/'tools/lightrecon_test/lightrecon_occupancy_test.cpp',Path(__file__).resolve()]
    write(out/'manifest.json',dict(schema='org.openjk.bake-occupancy-results.v1',
        sources={p.relative_to(ROOT).as_posix():sha(p) for p in files if p.is_file()},
        artifacts={p.name:sha(p) for p in out.iterdir() if p.name!='manifest.json'},
        licensed_payloads_published=False,runtime_consumers_changed=False))


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--private',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--test',type=Path,required=True);p.add_argument('--protocol',type=Path,required=True);p.add_argument('--revision',default='x13-a');p.add_argument('--out',type=Path);p.add_argument('--freeze',action='store_true');a=p.parse_args()
    if a.freeze:freeze(a.private,a.tool.resolve(),a.test.resolve(),a.protocol,a.revision)
    elif a.out:run(a.private,a.tool.resolve(),a.test.resolve(),a.protocol,a.out)
    else:p.error('Use --freeze or --out')
