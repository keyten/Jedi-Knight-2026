#!/usr/bin/env python3
"""Actual SP -> original CPU export -> blind offline admission of q3map2 fixtures.
The evaluator keeps authoring maps/answers private. No fitting or GPU readback.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_lightrecon_legacy_runtime import prepare_runtime
from gl43_validation.compare_images import compare
from run_pipeline import sha, run, write_json, bsp_counts


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('installation', 'binaries', 'renderer', 'tool', 'suite', 'output', 'private-archives'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--timeout', type=int, default=600)
    args = p.parse_args()
    args.output = args.output.resolve()
    if args.output.exists():
        raise RuntimeError('Use a fresh output directory')
    base = args.output/'stock-assets/base'
    base.mkdir(parents=True)
    for source in args.private_archives.glob('assets?.pk3'):
        os.link(source, base/source.name)
    suite = args.suite.resolve()
    manifest = json.loads((suite/'manifest.json').read_text())
    assert manifest['q3map2_executed']
    names = list(manifest['scenes']) + ['bounce_control']
    runtime = args.output/'run'
    config, archives = prepare_runtime(args, runtime, 'legacy', 3)
    config.update(r_pbrWorldUnitsPerMeter=32, r_deluxeMapping=0, r_mapOverBrightBits=0,
                  r_linearLighting=0, cg_draw2D=0, cg_drawGun=0, r_drawentities=0, r_dynamiclight=0,
                  cg_smoothCamera=0, cg_thirdPersonCameraDamp=1, cg_thirdPersonTargetDamp=1,
                  cg_thirdPersonRange=0, cg_thirdPersonVertOffset=0)
    home = runtime/'home/OpenJK'
    for folder in ('maps', 'shaders', 'textures'):
        (home/folder).mkdir(exist_ok=True)
    shutil.copytree(suite/'authoring/base/shaders', home/'shaders', dirs_exist_ok=True)
    shutil.copytree(suite/'authoring/base/textures', home/'textures', dirs_exist_ok=True)
    for name in names:
        source = suite/f'authoring/base/maps/{name}.bsp'
        recipe = json.loads((suite/f'answers/{name}.q3map-recipe.json').read_text())
        assert recipe['executed'] and recipe['repeated_surface_lighting_geometry_identical']
        assert recipe['compiled_bsp_sha256'] == sha(source)
        bsp_counts(source)
        shutil.copyfile(source, home/f'maps/lrq3_{name}.bsp')
    (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()))
    commands = ['wait 30']
    for name in names:
        commands += [f'devmap lrq3_{name}', 'wait 60', 'cam_disable', 'exitview', 'god', 'notarget',
                     'give all', 'wait 10', 'weapon 3', 'wait 45', 'noclip', 'cg_thirdPerson 0',
                     'setviewpos -150 -96 64 30', 'cg_draw2D 0', 'cg_drawGun 0',
                     'r_drawentities 0', 'con_notifytime 0', 'r_we clear', 'wait 20',
                     'uimenu recon_freeze', 'fx_freeze 1', 'wait 90',
                     f'echo Q3DATA_{name}_BEGIN',
                     f'r_lightReconExport {name}_a 0 0 0 128 224',
                     f'writecam q3_{name}_a', f'screenshot_tga q3-{name}-a', 'wait 6',
                     f'r_lightReconExport {name}_b 0 0 0 128 224',
                     f'writecam q3_{name}_b', f'screenshot_tga q3-{name}-b', 'wait 15',
                     f'echo Q3DATA_{name}_END', 'uimenu recon_unfreeze', 'fx_freeze 0']
    commands += ['echo Q3DATA_RUNTIME_DONE', 'wait 5', 'quit']
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands)+'\n')
    cmd = [str(runtime/'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        cmd += ['+set', key, config[key]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (runtime/'launch.log').open('w') as log:
        process = subprocess.Popen(cmd, cwd=runtime, startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=20)
            raise
    text = (home/'qconsole.log').read_text(errors='replace')
    if process.returncode or 'Q3DATA_RUNTIME_DONE' not in text or re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error',text):
        raise RuntimeError('Actual SP map-load failure: '+text[-3000:])
    cameras = {}
    for path in (home/'maps').glob('*_cam.map'):
        for tag, origin, angles, fov in re.findall(r'"targetname"\s*"([^"]+)"\s*"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"',path.read_text()):
            cameras[tag] = [origin, angles, fov]
    for folder in ('inputs', 'repeat', 'reports'):
        (args.output/folder).mkdir()
    result = dict(schema='org.openjk.q3map2-runtime-validation', version=1,
                  renderer_sha256=sha(args.renderer), tool_sha256=sha(args.tool), settings=config,
                  archives=archives, maps={})
    for name in names:
        first = next((runtime/'home').rglob(f'{name}_a.jkobs'),None)
        if first is None:
            section = re.search(f'Q3DATA_{name}_BEGIN(.*?)Q3DATA_{name}_END',text,re.S)
            raise RuntimeError('Export missing: '+(section[1] if section else text[-2000:]))
        second = first.with_name(f'{name}_b.jkobs')
        assert sha(first) == sha(second), f'Non-deterministic CPU export: {name}'
        recipe = json.loads((suite/f'answers/{name}.q3map-recipe.json').read_text())
        version = recipe['version_output'].splitlines()[0]
        public = dict(schema='org.openjk.q3map2-public-bake-profile', version=1,
                      compiler_version=version, compiler_binary_sha256=recipe['binary_sha256'],
                      compiled_bsp_sha256=recipe['compiled_bsp_sha256'], material_inputs=recipe['material_inputs'],
                      transfer='linear; gamma=1, compensate=1, exposure=0',
                      absolute_scale='unknown legacy bake units; not measured lux/cd/nits',
                      bsp=['-game','ja','-meta','-leaktest','-noflares','-samplesize','16','-threads','1'],
                      light=['-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0',
                             '-bounce','1' if name=='bounce_colour' else '0'])
        profile = args.output/f'reports/{name}.public-profile.json'
        write_json(profile, public)
        for source,destination in [(first,args.output/f'inputs/{name}.jkobs'),
                                   (second,args.output/f'repeat/{name}.jkobs')]:
            run([args.tool.resolve(),'benchmark',source,destination,version,profile],
                args.output/f'reports/{name}-{destination.parent.name}.admit.txt')
        admitted = args.output/f'inputs/{name}.jkobs'
        assert sha(admitted) == sha(args.output/f'repeat/{name}.jkobs')
        audit = json.loads(run([args.tool.resolve(),'audit-bsp',admitted,suite/f'authoring/base/maps/{name}.bsp'],
                              args.output/f'reports/{name}.bsp-audit.json'))
        inspection = run([args.tool.resolve(),'inspect',admitted],args.output/f'reports/{name}.inspect.txt')
        for partition in ('train','validation','heldout'):
            run([args.tool.resolve(),'samples',admitted,partition],args.output/f'reports/{name}.{partition}.jsonl')
        camera = cameras.get(f'q3_{name}_a')
        assert camera and camera == cameras.get(f'q3_{name}_b'), 'Unstable camera: '+name
        image = compare(home/f'screenshots/q3-{name}-a.tga',home/f'screenshots/q3-{name}-b.tga')
        if not image['passed']:
            raise AssertionError('Unstable fixed-view control: '+name+' '+str(image))
        result['maps'][name] = dict(raw_export_sha256=sha(first), dataset_sha256=sha(admitted),
                                  bytes=admitted.stat().st_size, audit=audit, inspect=inspection,
                                  camera=camera, image_repeat=image, two_exports_identical=True,
                                  blind=True, source_bsp_repeated_identical=recipe['repeated_bsp_identical'],
                                  compiler_grid_repeat_identical=recipe['repeated_lightgrid_identical'],
                                  compiler_varying_grid_cells=recipe['varying_logical_grid_cells'])
        write_json(args.output/'result.partial.json',result)
        print(name, 'bake/raw/UV/colour/geometry/benchmark/repeat valid',flush=True)
    write_json(args.output/'result.json',result)


if __name__ == '__main__':
    main()
