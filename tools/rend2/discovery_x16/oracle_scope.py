#!/usr/bin/env python3
"""Supplied-answer forward observability control, NOT discovery input."""
import argparse
import json
import math
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.validation.bridge import geometry,oracle_source,oracle_request,oracle_tool
from tools.rend2.discovery_x16.fit import frozen_geometry
from tools.rend2.all_stock.run import write,sha


def run(a):
    a.output.mkdir(parents=True,exist_ok=False)
    answer=a.bridge/'bridge/private/source-answer.json';truth=json.loads(answer.read_text())['sources']
    records={}
    for variant in ('direct','passive_bounce1'):
        folder=a.output/variant;folder.mkdir()
        bsp=a.bridge/'bridge/authoring/base/maps'/('v02_connected_rooms_v1_'+variant+'.bsp')
        lumps,_,casters=geometry(bsp,strict_receiver_uv=False)
        folds=frozen_geometry(a.bridge/'bridge'/variant/'discovered/request.txt')
        sources=[]
        for s in truth:
            v=oracle_source(kind=int(s['kind']=='spot'),position=s['position'],axis=s.get('axis',[0,0,-1]),
                rgb=[s['intensity']*c for c in s['rgb']])
            if s['kind']=='spot':v.update(target=math.dist(s['target'],s['position']),radius=s['radius'])
            sources.append(v)
        metrics={}
        for root,s in enumerate(sources):
            roles={}
            for role,rows in folds.items():
                q=[dict(world=r['position'],normal=r['normal'],surface=r['surface']) for r in rows]
                v=oracle_tool(a.tool,folder,f'root{root}-{role}',oracle_request(bsp,lumps,casters,q,{},[s]))
                values=v['rows'];available=[r for r in values if r['available']]
                roles[role]=dict(requested=len(rows),available=len(available),
                    positive=sum(max(r['packed'])>=8 for r in available),
                    nonzero=sum(max(r['packed'])>0 for r in available),
                    peak=max((max(r['packed']) for r in available),default=None),
                    squared_energy=sum(sum(c*c for c in r['packed']) for r in available),
                    all_zero=bool(len(available)==len(rows) and all(max(r['packed'])==0 for r in available)),
                    rays=v['rays'])
            metrics[str(root)]=dict(type=truth[root]['kind'],roles=roles)
        records[variant]=dict(bsp_sha256=sha(bsp),roots=metrics,discovery=False,
            recipe_scope='Pinned local point/spot direct kernel, generic static traces; prism and compiler bounce not replayed')
    write(a.output/'results.json',dict(answer_sha256=sha(answer),variants=records,
        scope='Post-fit oracle observability, never supplied to proposal/catalog/amplitude code'))
    print(json.dumps(records))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bridge','tool','output'):p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())
