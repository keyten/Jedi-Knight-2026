#!/usr/bin/env python3
"""P02 bounded geometry audit. Stock has no photometric ground truth.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import platform
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write
MAPS=('hoth3','vjun2','mp/duel4')

def main(a):
    a.out.mkdir(parents=True,exist_ok=False)
    inventory=json.loads((a.private/'inventory.json').read_text())
    captures=json.loads((a.private/'captures.json').read_text())
    sources=list((ROOT/'shared/rd-rend2/lightrecon').glob('probe*'))+[ROOT/'shared/rd-rend2/lightrecon/field_basis.cpp',ROOT/'shared/rd-rend2/lightrecon/field_basis.h',ROOT/'tools/lightrecon_test/probe_fixtures.h',ROOT/'tools/lightrecon_test/lightrecon_probes_test.cpp',ROOT/'tools/lightrecon_test/lightrecon_probes_tool.cpp',Path(__file__)]
    source_hashes={p.relative_to(ROOT).as_posix():sha(p) for p in sources}
    policy=dict(schema='org.openjk.probes-protocol.v1',commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),source_hashes=source_hashes,tool_sha256=sha(a.tool),test_sha256=sha(a.test),inventory_sha256=sha(a.private/'inventory.json'),maps=MAPS,roi_rule='Three 512-WU boxes around original static surface vertex anchors at ordinal quantiles 1/4, 1/2, 3/4; geometry only',coarse=128,minimum=32,clearance=1,max_nodes_per_roi=10000,max_probes_per_roi=10000,max_links_per_roi=80000,max_work_per_roi=32000000,process_seconds=180,process_bytes=768*1024**2,lighting_coefficients=0,historical_observations_moved=False,hidden_rgb_used=False,environment=dict(os=platform.platform(),cpu=platform.processor(),python=platform.python_version()))
    write(a.out/'protocol.json',policy)
    results={}
    for name in MAPS:
        data=a.private/'runs'/name.replace('/','_')/'all-pages.jkobs'
        if not data.exists():data=Path(captures[name]['dataset'])
        bsp=next(x for x in inventory['maps'] if x['map']==name)
        report=a.out/(name.replace('/','_')+'.json')
        r=execute([a.tool,data,report],a.out,name.replace('/','_'),seconds=180,memory=768*1024**2)
        result=dict(resources=r,dataset_sha256=sha(data),original_bsp_sha256=bsp['sha256'])
        if r['state']=='Completed' and r['exit_code']==0:
            result['geometry']=json.loads(report.read_text());assert result['geometry']['bsp_sha256']==bsp['sha256']
        results[name]=result;write(a.out/'stock.json',results)
        print(name,r['state'],r['exit_code'],flush=True)
    native=[];resources=[]
    for label in ('first','repeat'):
        path=a.out/('controlled-'+label+'.json')
        r=execute([a.test,path],a.out,'controlled-'+label,seconds=180,memory=512*1024**2)
        resources.append(r)
        if r['exit_code'] or r['state']!='Completed':raise RuntimeError('Native tests failed')
        d=json.loads(path.read_text());d.pop('query_nanoseconds',None);native.append(d)
    assert native[0]==native[1]
    write(a.out/'controlled.json',dict(semantic=native[0],resources=resources,repeat_identical=True))
    report=a.out/'mp_duel4-repeat.json'
    r=execute([a.tool,Path(captures['mp/duel4']['dataset']),report],a.out,'mp_duel4-repeat',seconds=180,memory=768*1024**2)
    repeat=dict(resources=r,matching_topology_keys=False)
    if not r['exit_code']:
        second=json.loads(report.read_text());first=results['mp/duel4']['geometry'];repeat['matching_topology_keys']=[x.get('topology_key') for x in first['rois']]==[x.get('topology_key') for x in second['rois']]
    write(a.out/'repeat.json',repeat)
    assert repeat['matching_topology_keys']
    for p,h in source_hashes.items():assert sha(ROOT/p)==h,p
    write(a.out/'manifest.json',dict(schema='org.openjk.probes-results.v1',sources=source_hashes,artifacts={p.name:sha(p) for p in a.out.iterdir() if p.is_file()},licensed_payloads=False,stock_light_truth=None))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--private',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--tool',type=Path,default=ROOT/'build/s06-gcc/lightrecon_probes_tool.exe');p.add_argument('--test',type=Path,default=ROOT/'build/s06-gcc/lightrecon_probes_test.exe');main(p.parse_args())
