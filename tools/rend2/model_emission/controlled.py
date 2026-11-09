"""Compile an owned static MD3 scene and inspect only its masked glowing bulb."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from emission import extract, write
from test_emission import author, instance
from model_instances import instances as models
from bsp_fixtures.catalogue import digest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"bsp_fixtures"))
from bsp_fixtures.compiled import brush


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    output=args.output.resolve()
    base=output/'authoring/base'
    author(base)
    shader='''textures/asr09/caulk
{
 surfaceparm nodraw
 surfaceparm nolightmap
}
models/asr11/housing
{
 { map models/asr11/white
 }
}
models/asr11/bulb
{
 { map models/asr11/white
 }
 { map models/asr11/glow
   blendFunc add
   glow
 }
}
'''
    (base/'shaders/asr11.shader').write_text(shader,encoding='ascii')
    (base/'shaders/shaderlist.txt').write_text('asr11\n',encoding='ascii')
    walls=[((-128,-128,-16),(128,128,0)),((-128,-128,128),(128,128,144)),
           ((-128,-128,0),(-112,128,128)),((112,-128,0),(128,128,128)),
           ((-112,-128,0),(112,-112,128)),((-112,112,0),(112,128,128))]
    text='{\n"classname" "worldspawn"\n'+''.join(brush(lo,hi) for lo,hi in walls)+'}\n'
    text+='{ "classname" "info_player_start" "origin" "64 64 32" }\n'
    for fields in [instance(origin='0 0 32'),instance(origin='32 0 32',angle='90',modelscale_vec='2 3 4'),
                   instance(origin='-32 0 32',modelscale_vec='-1 1 1')]:
        text+='{\n'+''.join('"%s" "%s"\n'%(k,v) for k,v in fields.items())+'}\n'
    path=base/'maps/asr11.map'
    path.write_text(text,encoding='ascii')
    compiler=args.compiler.resolve()
    command=[str(compiler),'-game','ja','-fs_basepath',str(base.parent),'-fs_game','base','-threads','1','-meta',str(path)]
    run=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180,encoding='utf-8',errors='replace')
    (output/'compiler.log').write_text(run.stdout,encoding='utf-8')
    if run.returncode:
        raise ValueError('q3map2 failed; inspect compiler.log')
    data=path.with_suffix('.bsp').read_bytes()
    assets=models.Assets(base)
    try:
        catalogue=models.build(data,assets)
        result=extract(catalogue,assets)
        checks=0
        def need(ok,message):
            nonlocal checks
            checks+=1
            if not ok:raise AssertionError(message)
        need(len(result['regions'])==3,'One masked bulb region per actual static placement')
        need(len(result['fixtures'])==3,'Three separate fixtures')
        expected=[((-38,3,33),4,(0,0,1)),((6,3,33),4,(0,0,-1)),((23,12,36),24,(0,0,-1))]
        for region,(center,area,normal) in zip(sorted(result['regions'],key=lambda r:r['centroid'][0]),expected):
            need(region['md3_surfaces']==[1],'Housing has no emitting geometry')
            for actual,wanted in zip(region['centroid'],center):need(abs(actual-wanted)<1e-6,'World centroid')
            for actual,wanted in zip(region['outward'],normal):need(abs(actual-wanted)<1e-6,'Visible side after winding/reflection')
            need(abs(region['emitting_area']-area)<1e-6,'Clipped world emitting area')
            need(abs(region['source_triangle_RGB_coverage']-.5)<1e-6,'Half of the bulb, no whole-mesh fallback')
            need(region['colour_prior'][0]==1,'Relative source-color prior')
            need(region['historical_bake_participation']=='unknown','Historical participation remains unknown')
        need(len({r['source_id'] for r in result['regions']})==1,'Repeated models retain shared source identity')
        write(result,output/'emission')
        models.write(catalogue,output/'models',True)
        before={p.name:digest(p.read_bytes()) for p in (output/'emission').iterdir()}
        write(extract(catalogue,assets),output/'emission')
        after={p.name:digest(p.read_bytes()) for p in (output/'emission').iterdir()}
        need(before==after,'Repeated exports are byte-identical')
        report=dict(checks=checks,counts=result['counts'],compiler_sha256=digest(compiler.read_bytes()),command=command,
                    bsp_sha256=digest(data),source_map_sha256=digest(path.read_bytes()),output_sha256=before,
                    repeated_export_byte_equal=True,scope='Owned fresh compiled static MD3 geometry and conditional emissive masks; no light phase or runtime consumer')
        (output/'validation.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,sort_keys=True))
    finally:assets.close()


if __name__=='__main__':main()
