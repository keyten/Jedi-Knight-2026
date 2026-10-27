#!/usr/bin/env python3
"""Compiled-fixture/controller integration, deterministic views and leak checks.

Uses existing q3map2/Rend2 captures; it does NOT claim to invoke the compiler.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import subprocess
from prepare_blind import prepare
from solver_view import TrainingView, digest
from evaluation_view import EvaluationView


def run(argv, log):
    result=subprocess.run(list(map(str,argv)),capture_output=True,text=True)
    log.write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{argv}: {result.stderr}')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--quality',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--fixtures',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    result={}
    for source in sorted(args.fixtures.glob('*.jkobs')):
        name=source.stem; prefix=args.output/name
        run([args.quality,'derive',source,prefix.with_suffix('.jkobs')],prefix.with_suffix('.derive.log'))
        run([args.tool,'validate',prefix.with_suffix('.jkobs')],prefix.with_suffix('.validate.log'))
        run([args.quality,'scan',prefix.with_suffix('.jkobs'),prefix],prefix.with_suffix('.scan.log'))
        meta=json.loads(prefix.with_suffix('.source.json').read_text())
        assert meta['compiler_known'] and meta['synthetic'], 'Known compiler fixture required'
        # Frozen using receiver-cell geometry only, before fitting or target RGB
        # inspection. The previous stock seed gave no validation block here.
        one=prepare(prefix,args.output/(name+'-views'),block=256,gap=1,seed=20261032)
        two=prepare(prefix,args.output/(name+'-repeat'),block=256,gap=1,seed=20261032)
        assert one==two and one['all_pages_complete']
        view=TrainingView(args.output/(name+'-views')/'inputs',meta['map_sha256'])
        assert list(view.observations())==list(view.observations())
        list(view.grid_observations())
        for role,files in [('inputs',('manifest.json','train.jsonl','grid.jsonl','geometry.jkobs')),
                           ('evaluator',('validation.jsonl','heldout.jsonl','validation-grid.jsonl','heldout-grid.jsonl'))]:
            for member in files:
                assert digest(args.output/(name+'-views')/role/member)==digest(args.output/(name+'-repeat')/role/member)
        run([args.quality,'public-check',view.geometry_path,'unused'],prefix.with_suffix('.public-check.log'))
        evaluator=EvaluationView(args.output/(name+'-views')/'evaluator',meta['map_sha256'])
        list(evaluator.observations('validation'))
        try:
            list(evaluator.observations('heldout'))
            raise AssertionError('Held-out stage permission bypass')
        except PermissionError: pass
        assert not set(r['id'] for r in view.observations()) & set(r['id'] for r in evaluator.observations('validation'))
        result[name]=dict(source_sha256=digest(source),compiler_version=meta['compiler_version'],
                          partitions=one['partitions'],grid_partitions=one['grid_partitions'],
                          texels=one['derived_texel_denominator'],all_pages=True,repeat_identical=True,
                          public_geometry_checked=True,ground_truth_loaded=False)
        if name=='point_room':
            destination=args.output/'cancelled.jkobs'
            destination.write_bytes(b'existing destination retained')
            stopped=subprocess.run([str(args.quality),'derive',str(source),str(destination),'0','0','16','2'],capture_output=True,text=True)
            assert stopped.returncode and destination.read_bytes()==b'existing destination retained'
            assert not list(args.output.glob('cancelled.jkobs.tmp-*'))
            result[name]['atomic_cancel_preserves_destination']=True
        print(name,result[name],flush=True)
    if not result: raise RuntimeError('No compiled fixtures found')
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__': main()
