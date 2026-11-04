#!/usr/bin/env python3
"""Compare repeat semantic receipts and adversarial controlled material inputs."""
import argparse
import json
from pathlib import Path
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import write


def read(p):return json.loads(p.read_text())


def run(a):
    a.output.mkdir(parents=True,exist_ok=False);checks={};rebake={}
    for variant in ('direct','passive_bounce1'):
        first=a.bridge/'bridge'/variant;second=a.bridge_repeat/'bridge'/variant
        same=read(first/'recipe.json')['bsp_sha256']==read(second/'recipe.json')['bsp_sha256']
        filename='v02_connected_rooms_v1_'+variant+'.bsp'
        x=(a.bridge/'bridge/authoring/base/maps'/filename).read_bytes()
        y=(a.bridge_repeat/'bridge/authoring/base/maps'/filename).read_bytes()
        def lump(data,index):
            at,size=struct.unpack_from('<ii',data,8+8*index);return data[at:at+size]
        rebake[variant]=dict(byte_identical=same,
            different_lumps=[i for i in range(18) if lump(x,i)!=lump(y,i)],
            meaning='A changed immutable BSP hash legitimately changes IDs and fold identities; not identical solver inputs')
        if same:
            checks[variant+'_candidate_identity_and_evidence']=read(first/'frozen-candidates.json')==read(second/'frozen-candidates.json')
        if a.exact_replay:
            target=a.exact_replay/variant
            for request in sorted(first.glob('search-*.request')):
                old=read(first/(request.stem+'.json'));new=read(target/(request.stem+'.log'))
                checks[variant+'/'+request.stem]={k:v for k,v in old.items() if k!='resources'}=={k:v for k,v in new.items() if k!='resources'}
            for kind in ('discovered','oracle-xyz'):
                checks[variant+'/'+kind]=read(first/kind/'events.json')==read(target/kind/'events.json')
    for name in ('hoth3','mp_duel4'):
        for profile in ('isq','linear'):
            first=a.input/'stock'/name;second=a.stock_repeat/'stock'/name
            checks[name+'_'+profile+'_scheduler']=read(first/('scheduler-'+profile+'.json'))==read(second/('scheduler-'+profile+'.json'))
            checks[name+'_'+profile+'_Joint']=read(first/(profile+'.json'))==read(second/(profile+'.json'))
    folder=a.bridge/'bridge/direct'
    recipe=read(folder/'recipe.json')
    # Public controlled geometry declaration is not a generic stock-material
    # override. Wrong-map, unknown optical names and duplicate ordinals fail.
    material=(folder/'controlled-materials.tsv').read_text()
    lines=material.splitlines()
    variants={'wrong-map':material.replace(recipe['bsp_sha256'],'0'*64,1),
              'unsupported-material':material.replace('textures/bake_forward/panel','textures/unknown',1),
              'duplicate-surface':'\n'.join([lines[0],lines[1],lines[1],*lines[3:]])+'\n'}
    bsp=next((a.bridge/'bridge/authoring/base/maps').glob('*_direct.bsp'))
    tests={}
    for name,value in variants.items():
        path=a.output/(name+'.tsv');path.write_text(value)
        cmd=[a.native/'lightrecon_regions_inspect.exe',bsp,a.output/name,0,0,160,800,128,32,1,a.output/(name+'.jkobs'),path]
        result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=30)
        tests[name]=dict(exit_code=result.returncode,reason=result.stdout+result.stderr)
        checks['material_'+name]=result.returncode!=0 and 'Controlled material' in tests[name]['reason']
    record=dict(schema='org.openjk.validation-repeat/1',checks=checks,materials=tests,compiler_rebake=rebake,
                all_passed=all(checks.values()),policy_changed=False)
    write(a.output/'results.json',record);print(json.dumps(record))
    if not record['all_passed']:raise AssertionError('V02 deterministic/identity regression')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('input','bridge','bridge-repeat','stock-repeat','output','native'):
        p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--exact-replay',type=Path)
    run(p.parse_args())
