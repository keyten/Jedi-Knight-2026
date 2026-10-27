#!/usr/bin/env python3
"""Controlled sampling/trace variants of the SAME multi-sun source fixture.
No original tolerance or golden BSP is changed. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from compiler_boundary import load
from compare_bakes import author,make_assets,CASES,floor_samples,predict
from bake_model import encode_color,COMPILER_SHA256


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--q3map2',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    compiler=args.q3map2.resolve(); digest=hashlib.sha256(compiler.read_bytes()).hexdigest()
    if digest!=COMPILER_SHA256: raise ValueError('Pinned compiler required')
    result={}; case=next(c for c in CASES if c['name']=='multi_sun')
    for name,extra,offset in [('control',[],None),('no_trace_subdivision',['-lomem'],None),
        ('supersample2',['-super','2'],None),('adaptive_samples2',['-samples','2'],None),
        ('filter',['-filter'],None),('offset0',[],0),('offset2',[],2)]:
        base=(args.output/name/'base').resolve(); make_assets(base)
        if offset is not None:
            shader=base/'shaders/bake_forward.shader'; text=shader.read_text()
            text=text.replace(' qer_editorimage textures/bake_forward/white.tga',
                ' q3map_lightmapSampleOffset '+str(offset)+'\n qer_editorimage textures/bake_forward/white.tga')
            shader.write_text(text)
        path=author(base,case); commands=[]
        common=[str(compiler),'-v','-game','ja','-fs_basepath',str(base.parent),'-fs_game','base','-threads','1']
        stages=[['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],
            ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0','-bounce','0','-super','1','-nocollapse']+extra]
        for i,stage in enumerate(stages):
            argv=common+stage+[str(path)]; commands.append(argv)
            p=subprocess.run(argv,capture_output=True,text=True,errors='replace')
            (base.parent/f'stage{i}.log').write_text(p.stdout+p.stderr,encoding='utf-8')
            if p.returncode or re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked|Unknown (?:argument|option)',p.stdout+p.stderr,re.I):
                raise RuntimeError('Failed compiler variant '+name)
        raw=load(path.with_suffix('.bsp'))[0]; rows=[]; errors=[]
        for row in floor_samples(raw):
            old=encode_color(predict(case,(*row['world'][:2],1.)))
            errors.extend(abs(a-b) for a,b in zip(old,row['rgb']))
            if abs(row['world'][0]+15)<32:
                rows.append(dict(world=row['world'],rgb=row['rgb'],old_analytic=old))
        result[name]=dict(compiler_sha256=digest,commands=commands,shader_sha256=hashlib.sha256((base/'shaders/bake_forward.shader').read_bytes()).hexdigest(),
            bsp_sha256=hashlib.sha256(path.with_suffix('.bsp').read_bytes()).hexdigest(),
            full_interior_max_error_to_OLD_control_formula=max(errors),seam_rows=rows,
            source_override=dict(extra=extra,sample_offset=offset),
            note='Changed sampling recipe; mismatch to OLD point formula is diagnostic, NOT a failed original tolerance')
        print(name,'compiled',flush=True)
    (args.output/'result.json').write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')


if __name__=='__main__': main()
