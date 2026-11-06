#!/usr/bin/env python3
"""Repeat the native X13/X17 geometry/path witness with process resources."""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write


def main(a):
    a.out.mkdir(parents=True,exist_ok=False)
    values=[]; resources=[]
    for label in ('first','repeat'):
        path=a.out/(label+'.json')
        resource=execute([a.test,path],a.out,label,seconds=180,memory=256<<20)
        if resource['state']!='Completed' or resource['exit_code']:
            raise ValueError('Native path regression failure: '+str(resource))
        values.append(json.loads(path.read_text()))
        resources.append(resource)
    if values[0]!=values[1]: raise ValueError('Native path witness nondeterminism')
    write(a.out/'native.json',dict(result=values[0],resources=resources,deterministic=True,
          native_sha256=sha(a.test),scope='Supplied controlled geometry/type, blind amplitude; not source discovery'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--test',type=Path,default=ROOT/'build/s06-gcc/lightrecon_occupancy_test.exe')
    main(p.parse_args())
