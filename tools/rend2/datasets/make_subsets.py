#!/usr/bin/env python3
"""Controller-only ROI selection/publication. Retains the map's global split.

The diagnostic report stays OUTSIDE inputs/. Geometry is the complete sanitized
scene so cropping observations cannot silently delete occluders.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import shutil
from prepare_blind import Group, rows, write
from solver_view import digest


def contains(row, bounds):
    return row['position'] is not None and all(lo<=v<=hi for v,lo,hi in zip(row['position'],bounds[0],bounds[1]))


def select(report, rawpath=None, facts=None):
    """Deterministic broad stress selections, not certified semantic rooms."""
    cells=[(k,g) for k,g in report['groups'].items() if k.startswith('spatial256/') and g['counts'].get('mapped',0)>=64]
    if not cells: return []
    scored=[]
    for k,g in cells:
        centre=[(int(x)+.5)*256 for x in k.split('/')[1].split(',')]
        scored.append((k,g,centre))
    choices=[]; used=[]
    brightness={}
    if rawpath:
        for row in rows(rawpath):
            if row['coverage']!=2 or row['position'] is None or any(v is None for v in row['raw']): continue
            key='spatial256/'+','.join(str(math.floor(x/256)) for x in row['position'])
            total,count=brightness.get(key,(0.,0))
            brightness[key]=(total+sum(row['raw'])/3,count+1)
    mean=lambda x:brightness.get(x[0],(0.,1))[0]/max(1,brightness.get(x[0],(0.,1))[1])
    hints=(facts or {}).get('fixture_geometry_priors',[])
    near_hint=lambda x:any(math.dist(x[2],h['position'])<=512 for h in hints)
    specifications=[('multi_normal',lambda x:x[1]['orientation_span_proxy']),
        ('dense_receiver',lambda x:x[1]['distinct_receiver_surfaces']),
        ('material_uncertain',lambda x:x[1]['counts'].get('material_style_inconsistent',0)),
        ('bright_fixture_candidate',lambda x:mean(x) if near_hint(x) else -1),
        ('weak_encoded_rgb',lambda x:-mean(x)),
        ('upward_receiver_exterior_candidate',lambda x:x[1]['normal_orientation_counts'].get('z+',0))]
    for name,key in specifications:
        ordered=sorted(scored,key=lambda x:(-key(x),x[0]))
        chosen=next((x for x in ordered if all(math.dist(x[2],p)>384 for p in used)),ordered[0])
        used.append(chosen[2]); c=chosen[2]
        choices.append(dict(name=name,bounds=[[v-128 for v in c],[v+128 for v in c]],
                            selection='Frozen 256-world-unit cell; room/outdoor topology requires ray evidence; encoded brightness is not photometry'))
    return choices


def publish(prefix, views, output, manifest=None):
    prefix,views,output=Path(prefix),Path(views),Path(output)
    report=json.loads((views/'coverage.json').read_text(encoding='utf-8'))
    factpath=prefix.with_suffix('.facts.json')
    facts=json.loads(factpath.read_text()) if factpath.exists() else None
    selected=manifest if manifest is not None else select(report,prefix.with_suffix('.rows.jsonl'),facts)
    output.mkdir(parents=True,exist_ok=False)
    original=json.loads((views/'inputs/manifest.json').read_text())
    groups={s['name']:Group() for s in selected}; hist={s['name']:Counter() for s in selected}
    for r in rows(prefix.with_suffix('.rows.jsonl')):
        for s in selected:
            if contains(r,s['bounds']):
                groups[s['name']].add(r)
                if all(v is not None for v in r['raw']): hist[s['name']][int(sum(r['raw'])/3)] += 1
    results=[]
    for s in selected:
        root=output/s['name']; inputs=root/'inputs'; evaluation=root/'evaluator'
        inputs.mkdir(parents=True); evaluation.mkdir()
        os.link(views/'inputs/geometry.jkobs',inputs/'geometry.jkobs')
        counts={}; grid_counts={}
        for role,directory,outdir in [('train','inputs',inputs),('validation','evaluator',evaluation),('heldout','evaluator',evaluation)]:
            for grid in (False,True):
                name=('grid' if role=='train' else role+'-grid')+'.jsonl' if grid else role+'.jsonl'
                count=0
                with (views/directory/name).open(encoding='utf-8') as src,(outdir/name).open('w',encoding='utf-8',newline='\n') as dst:
                    for line in src:
                        r=json.loads(line)
                        if contains(r,s['bounds']): dst.write(line); count+=1
                (grid_counts if grid else counts)[role]=count
        public=dict(original,counts=counts,grid_counts=grid_counts,subset=s,
                    sha256={n:digest(inputs/n) for n in ('train.jsonl','grid.jsonl','geometry.jkobs')})
        write(inputs/'manifest.json',public)
        private=dict(public,role='evaluator',sha256={n:digest(evaluation/n) for n in ('validation.jsonl','heldout.jsonl','validation-grid.jsonl','heldout-grid.jsonl')})
        write(evaluation/'manifest.json',private)
        info=dict(s,map_sha256=original['map_sha256'],partitions=counts,grid_partitions=grid_counts,
                  quality=groups[s['name']].report(),encoded_rgb_histogram=dict(hist[s['name']]),
                  bake_profile='controlled' if report['source']['compiler_known'] else 'UnknownProfile',
                  admission=('conditional three-fold input; photometric/model assumptions still required'
                             if min(counts.values())>=32 else 'partition-only/insufficient local folds; use the global map split'),
                  visibility='Complete geometry retained; use per-ray results. Region IDs are optional.')
        write(root/'diagnostics.json',info); results.append(info)
    write(output/'subsets.json',results)
    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('prefix',type=Path); p.add_argument('views',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--manifest',type=Path)
    args=p.parse_args()
    print(json.dumps(publish(args.prefix,args.views,args.output,
                            json.loads(args.manifest.read_text()) if args.manifest else None),indent=2))


if __name__=='__main__': main()
