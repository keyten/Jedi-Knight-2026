#!/usr/bin/env python3
"""Frozen X16 public-input proposal replay. No source-answer reads or bake."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import execute,sha,write


def run(a):
    a.output.mkdir(parents=True,exist_ok=False)
    policy=Path(__file__).with_name('policy-v6.json')
    manifest=dict(policy_sha256=sha(policy),base_revision=subprocess.check_output(
        ['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        tools={str(p):sha(p) for p in (a.baseline,a.tool)},variants={})
    for variant in ('direct','passive_bounce1'):
        original=a.bridge/'bridge'/variant;out=a.output/variant;out.mkdir()
        geometry=original/'views/inputs/geometry.jkobs'
        manifest['variants'][variant]=dict(geometry_sha256=sha(geometry),
            training_sha256=sha(original/'views/inputs/train.jsonl'),
            recipe=json.loads((original/'recipe.json').read_text()))
        jobs=[]
        for ident in range(1,9):
            old=original/f'search-{ident}.request'
            # Same admitted rows, source bounds, nodes, sampling and profiles.
            for label,tool,axes,balanced,slots in [
                ('baseline',a.baseline,False,False,4),
                ('legacy_adapter',a.tool,False,False,4),
                ('budgetmatched',a.tool,False,False,8),
                ('new',a.tool,True,True,8)]:
                folder=out/label;folder.mkdir(exist_ok=True)
                text=old.read_text()
                if label!='baseline':
                    lines=text.splitlines();lines[0]=lines[0].replace('JKHIDDEN1','JKHIDDEN2')
                    lines.insert(3,f'{int(axes)} {int(balanced)} {slots}');text='\n'.join(lines)+'\n'
                path=folder/f'search-{ident}.request';path.write_text(text)
                r=execute([tool,geometry,path],folder,f'search-{ident}',seconds=180)
                if r['exit_code']:raise RuntimeError('Native proposal failed: '+str(r))
                result=json.loads((folder/f'search-{ident}.log').read_text())
                write(folder/f'search-{ident}.json',result)
                jobs.append(dict(job=ident,label=label,count=len(result['candidates']),
                    forward_calls=result['forward_calls'],rays=result['rays'],work=result['work'],
                    nodes=sum(x['nodes'] for x in result['areas']),resources=r))
        manifest['variants'][variant]['requests']={str(i):sha(original/f'search-{i}.request') for i in range(1,9)}
        write(out/'jobs.json',jobs)
    write(a.output/'manifest.json',manifest)
    print('Frozen public-only proposals completed; no private source matching performed')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bridge','baseline','tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('bridge','baseline','tool','output'):setattr(a,name,getattr(a,name).resolve())
    run(a)
