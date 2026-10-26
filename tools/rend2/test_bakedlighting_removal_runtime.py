#!/usr/bin/env python3
"""Actual SP before/after static controls plus independent lighting smoke captures.

Uses the established isolated runtime preparation and image comparator. No assets
are copied to Git. One process per latched configuration, no renderer mocks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from PIL import Image, ImageStat
from test_lightrecon_legacy_runtime import prepare_runtime, CAMERAS, CONSUMERS
from gl43_validation.compare_images import compare

CASES = [
    ('legacy', 'hoth3', 0, dict(r_entityLightGrid=0)),
    ('physical-fog', 'hoth3', 3, dict(r_pbrPhysical=1, r_volumetricFog=2,
        r_forwardPlus=1, r_ltcAreaLights=1, r_normalMapping=1, r_specularMapping=1)),
    ('fog1-multipoint', 'hoth3', 0, dict(r_volumetricFog=1, r_entityLightGrid=1,
        r_normalMapping=1, r_specularMapping=1)),
    ('cacheonly-gpu-grid', 'hoth3', 1, dict(r_pbrPhysical=1, r_volumetricFog=2,
        r_entityLightGrid=2, r_entityLightProbes=0, r_normalMapping=1, r_specularMapping=1)),
    ('independent-probes', 'hoth3', 0, dict(r_pbrPhysical=1, r_volumetricFog=2,
        r_entityLightGrid=2, r_entityLightProbes=1, r_forwardPlus=1,
        r_normalMapping=1, r_specularMapping=1)),
    ('sun-raster', 'hoth2', 0, dict(r_volumetricFog=2, r_gl43=0, r_sunlightMode=1,
        r_sunShadows=1, r_sunShadowMode=0, r_entitySunSplit=1,
        r_normalMapping=1, r_specularMapping=1)),
    ('sun-physical-ltc', 'hoth2', 2, dict(r_pbrPhysical=1, r_volumetricFog=2,
        r_sunlightMode=1, r_sunShadows=1, r_sunShadowMode=1, r_ltcAreaLights=1,
        r_forwardPlus=1, r_entityLightGrid=2, r_entityLightProbes=1,
        r_normalMapping=1, r_specularMapping=1)),
    ('ltc-request-only', 'hoth3', 0, dict(r_ltcAreaLights=1, r_forwardPlus=0,
        r_normalMapping=1, r_specularMapping=1)),
    ('forward-only', 'hoth2', 0, dict(r_forwardPlus=1, r_ltcAreaLights=0,
        r_normalMapping=1, r_specularMapping=1)),
]


def run_case(args, name, map_name, policy, overrides, stage):
    runtime = args.output/f'{name}-{stage}'
    args.renderer = args.before_renderer if stage == 'before' else args.after_renderer
    config, archives = prepare_runtime(args, runtime, 'legacy', policy)
    config.update(overrides)
    config.update(cg_smoothCamera=0, cg_thirdPersonCameraDamp=1,
        cg_thirdPersonTargetDamp=1, cg_thirdPersonRange=0, cg_thirdPersonVertOffset=0,
        r_volumetricFogNoise=0, r_weathersystem=0, r_deluxeMapping=0,
        r_volumetricFogTemporal=0, r_loadProfile=1)
    if stage == 'before':
        # Historical baseline only; these names have no current registration/alias.
        config.update(r_staticLightReconstruction=0, r_volumetricFogStaticDirectional=0,
            r_recoveredVolumetricLights=0, r_lightPortals=0, r_ltcAreaCalibration=0)
    home = runtime/'home/OpenJK'
    if args.actor_fixture_suite:
        source=args.actor_fixture_suite/'authoring/base'
        (home/'maps').mkdir(exist_ok=True)
        shutil.copyfile(source/'maps/point_room.bsp',home/'maps/lr_saber_room.bsp')
        for folder in ('shaders','textures'):
            shutil.copytree(source/folder,home/folder,dirs_exist_ok=True)
    # Release the input catcher as well as server pause for weapon/NPC stimuli.
    menu = home/'ui/recon-freeze.menu'
    # UI_InGameMenu installs its catcher AFTER onOpen; close on the next paint.
    menu.write_text(menu.read_text().replace('setcvar "cl_paused" "0";',
                    'delay 1 ; uiScript closeingame ;'))
    (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()))
    x,y,z,yaw = CAMERAS[map_name]
    commands = ['wait 30',f'devmap {map_name}','wait 180','cam_disable','exitview',
        'god','notarget','give all','wait 10','weapon 3','wait 45','noclip',
        'cg_thirdPerson 0','cg_draw2D 0','cg_drawGun 0','con_notifytime 0','r_we clear',
        f'setviewpos {x} {y} {z} {yaw}','wait 30','uimenu recon_freeze','fx_freeze 1',
        'r_drawentities 0','wait 90','r_pbrInfo','r_lightReconInfo',
        'echo BAKED_STATIC_BEGIN','writecam baked_static_a','screenshot_tga baked-static-a',
        'wait 8','writecam baked_static_b','screenshot_tga baked-static-b','wait 10',
        'echo BAKED_STATIC_END',
        # NPC/weapon/saber and dynamic stimuli are inspected separately from the
        # exact static comparison: animation/FX randomness is not a lighting oracle.
        'uimenu recon_unfreeze','wait 6','echo BAKED_ANIMATION_BEGIN','cl_paused','sv_paused',
        'echo BAKED_ANIMATION_END','fx_freeze 0','r_drawentities 1',
        *(['devmap lr_saber_room','wait 120','cam_disable','exitview','lock_disable',
           'god','notarget','give all','wait 20','noclip','setviewpos 0 0 64 90','wait 30']
          if args.actor_fixture_suite else
          ['setviewpos 3320 700 123 90','wait 30'] if map_name=='hoth3' else []),
        'npc spawn stormtrooper','wait 90','cam_disable','lock_disable','exitview',
        'saber single_1','setSaberAll 3','weapon 1','wait 90','saberBlade 1 1 1',
        '+attack','wait 12','-attack','wait 20','cg_thirdPerson 1',
        'cg_thirdPersonRange 120','cg_thirdPersonAngle 30','cg_thirdPersonPitchOffset 12',
        'wait 60','uimenu recon_freeze','fx_freeze 1',
        'r_forwardPlusSpawnTestLights 4 384','wait 20',
        'screenshot_tga baked-entities-saber','wait 10','r_pbrInfo','r_lightReconInfo',
        'glslusage all','echo BAKED_RUNTIME_DONE','wait 5','quit']
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands)+'\n')
    command = [str(runtime/'openjk_sp.x86_64.exe')]
    for k in ('fs_basepath','fs_homepath','fs_game'):command+=['+set',k,config[k]]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    with (runtime/'launch.log').open('w') as log:
        child=subprocess.Popen(command,cwd=runtime,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
        try:child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            child.terminate();child.wait(timeout=20);raise
    text=(home/'qconsole.log').read_text(errors='replace')
    text=re.sub(r'\^[0-9]','',text)
    assert child.returncode==0 and 'BAKED_RUNTIME_DONE' in text
    assert not re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|\bERROR:',text)
    assert not re.search(r'\br_lightRecon\w+ requested=\d+ effective=[1-9]',text)
    assert not re.search(r'\b(?:NaN|Inf|Infinity)\b',text,re.I)
    animation=re.search(r'BAKED_ANIMATION_BEGIN(.*?)BAKED_ANIMATION_END',text,re.S)
    # SP SV_CheckPaused sets sv_paused=1 but never clears it. Only cl_paused=0
    # controls resumption; the stale server diagnostic is not an activity oracle.
    assert animation and re.search(r'\bcl_paused\s*=\s*"0"',animation[1]), 'Actor stimulus never unpaused'
    cams=[]
    for f in (home/'maps').glob('*_cam.map'):
        cams+=re.findall(r'"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"',f.read_text())
    assert len(cams)==2 and cams[0]==cams[1]
    shots=home/'screenshots'
    image=Image.open(shots/'baked-static-a.tga').convert('RGB')
    assert max(ImageStat.Stat(image).stddev)>2 and len(image.getcolors(1000000) or [])>64
    repeat=compare(shots/'baked-static-a.tga',shots/'baked-static-b.tga')
    assert repeat['passed'], repeat
    return dict(settings=config,camera=cams[0],repeat=repeat,archives=archives,
        renderer=str(args.renderer),runtime=str(runtime),passed=True,
        renderer_sha256=hashlib.sha256((runtime/'rdsp-rend2_x86_64.dll').read_bytes()).hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('installation','binaries','before-renderer','after-renderer','output','private-archives'):
        p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--timeout',type=int,default=600)
    p.add_argument('--only',nargs='+',choices=[x[0] for x in CASES],help='Run a focused subset')
    p.add_argument('--actor-fixture-suite',type=Path,
                   help='Optional self-authored q3map2 point-room for unobstructed character/saber views')
    args=p.parse_args();args.output=args.output.resolve()
    assert not args.output.exists(),'Use a fresh evidence directory'
    base=args.output/'stock-assets/base';base.mkdir(parents=True)
    for f in args.private_archives.glob('assets?.pk3'):os.link(f,base/f.name)
    report={}
    for name,m,policy,overrides in CASES:
        if args.only and name not in args.only:continue
        a=run_case(args,name,m,policy,overrides,'before')
        b=run_case(args,name,m,policy,overrides,'after')
        assert a['camera']==b['camera'],(name,a['camera'],b['camera'])
        comparison=compare(Path(a['runtime'])/'home/OpenJK/screenshots/baked-static-a.tga',
            Path(b['runtime'])/'home/OpenJK/screenshots/baked-static-a.tga',
            args.output/f'{name}-diff.png',max_delta=1,max_pixels=5,max_mean=1e-5)
        report[name]=dict(map=m,before=a,after=b,comparison=comparison)
        (args.output/'result.partial.json').write_text(json.dumps(report,indent=2)+'\n')
        assert comparison['passed'],(name,comparison)
        print(name,'PASS fixed-view before/after',comparison,flush=True)
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
