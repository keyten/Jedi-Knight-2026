#!/usr/bin/env python3
"""Freeze bounded stock benchmark ROIs, probe geometry and draw coverage reports.

Operates on already scanned/partitioned archives. Stock source XYZ is never scored.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
from make_subsets import publish
from measure import run
from plot_coverage import plot
from prepare_blind import write
from solver_view import TrainingView


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--source-root',type=Path,required=True)
    p.add_argument('--roi-tool',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--resume',action='store_true')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=args.resume)
    result={}
    for name in ('hoth3','hoth2','mp_ffa3'):
        prefix=args.root/name; views=args.root/(name+'-views'); folder=args.output/name
        if args.resume and (folder/'subsets.json').exists() and (args.output/(name+'-rays.json')).exists():
            subsets=json.loads((folder/'subsets.json').read_text(encoding='utf-8'))
            source=json.loads(prefix.with_suffix('.source.json').read_text())
            assert all(s['map_sha256']==source['map_sha256'] for s in subsets)
            result[name]=dict(subsets=subsets,geometry_measurement=dict(wall_seconds=None,
                note='Completed ray output retained; controller interrupted before storing timing.'))
            continue
        subsets=publish(prefix,views,folder)
        plot(views/'coverage.json',args.output/(name+'-coverage.svg'))
        command=[args.roi_tool,args.source_root/(name+'_a.jkobs')]
        for s in subsets:
            lo,hi=s['bounds']; centre=[(a+b)/2 for a,b in zip(lo,hi)]; radius=max(b-a for a,b in zip(lo,hi))/2
            command += [str(x) for x in centre+[radius]]
        measurement=run(command,args.output/(name+'-rays.json'))
        rays=json.loads((args.output/(name+'-rays.json')).read_text())
        assert len(rays['rois'])==len(subsets)
        for s,r in zip(subsets,rays['rois']):
            view=TrainingView(folder/s['name']/'inputs',s['map_sha256'])
            assert sum(1 for _ in view.observations())==s['partitions']['train']
            list(view.grid_observations())
            s['geometry_probe']=r
            write(folder/s['name']/'diagnostics.json',s)
        write(folder/'subsets.json',subsets)
        result[name]=dict(subsets=subsets,geometry_measurement=measurement)
        write(args.output/(name+'-witness.json'),result[name])
        print(name,'subsets=',len(subsets),'geometry peak=',measurement['peak_working_set_bytes'],flush=True)
    write(args.output/'result.json',result)


if __name__=='__main__': main()
