#!/usr/bin/env python3
"""S13 trusted controller; native producer fits and publishes, RGB answers remain here.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.joint.compiled import case,sha


def run(tool,views,bakes,out):
    out.mkdir(parents=True,exist_ok=False)
    cases=[];resources=[]
    for name in ('point_room','two_colour','door_occlusion'):
        for wrong in (False,True):
            directory=out/(name+('-wrong' if wrong else '-correct'))
            directory.mkdir()
            r,w=case(tool,views,bakes,name,directory,wrong,publish=True)
            cases.append(r);resources.append(w)
            print(name,'wrong' if wrong else 'correct',r['result'].get('producer'),flush=True)
    # Repeated run must match the entire canonical archive, excluding time/RSS.
    repeat=out/'repeat';repeat.mkdir()
    r,w=case(tool,views,bakes,'two_colour',repeat,publish=True)
    original=out/'two_colour-correct'/'two_colour-cache'
    repeated=repeat/'two_colour-cache'
    a={p.name:sha(p) for p in original.glob('*.jklr')}
    b={p.name:sha(p) for p in repeated.glob('*.jklr')}
    if not a or a!=b:raise ValueError('Canonical producer serialization is not deterministic')
    files=['shared/rd-rend2/lightrecon/'+n for n in ('producer.cpp','producer.h','joint.cpp','joint.h','disk.cpp','bake_forward.cpp','uncertainty.cpp')]
    files+=['shared/rd-rend2/tr_lightrecon_types.h','shared/rd-rend2/tr_lightrecon_types.cpp','tools/lightrecon_test/lightrecon_joint_tool.cpp','tools/rend2/joint/compiled.py','tools/rend2/producer/benchmark.py']
    result=dict(schema='org.openjk.producer-compiled.v1',cases=cases,repeat_archive_sha256=a,
        executable_sha256=sha(tool),source_sha256={p:sha(ROOT/p) for p in files},
        limitations=['Fixed XYZ is supplied, not discovered','No absolute photometry','No stock reconstruction claim','Independent existence calibration remains unavailable'])
    (out/'compiled.json').write_text(json.dumps(result,sort_keys=True,indent=2,allow_nan=False)+'\n')
    (out/'resources.json').write_text(json.dumps(resources+[w],sort_keys=True,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--views',type=Path,default=ROOT/'build/x07-verified')
    p.add_argument('--bakes',type=Path,default=ROOT/'build/q3map2-bake-verified')
    p.add_argument('--out',type=Path,required=True);a=p.parse_args();run(a.tool,a.views,a.bakes,a.out)
