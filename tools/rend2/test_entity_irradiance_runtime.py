"""Run the real SP/MP renderer with a private authored C04 field and image controls."""
import argparse
import json
from pathlib import Path
import re
import shutil
from statistics import median
import subprocess

import numpy as np
from PIL import Image

from gl43_validation.compare_images import compare
from test_resolved_sun_runtime import launch, prepare_runtime, rendered_world
from test_lightrecon_legacy_runtime import IMAGE_TOLERANCE


def scene(args):
    pose = ' '.join(map(str, args.pose))
    commands = [f'devmap {args.map}', 'wait 150']
    if args.mp:
        commands += ['team free', 'wait 40']
    else:
        commands += ['cam_disable', 'exitview']
    commands += ['god', 'notarget', 'noclip']
    if not args.mp:
        commands += ['give all', 'wait 15', 'weapon 4', 'wait 90']
    commands += [f'setviewpos {pose}', 'wait 30']
    if args.mp:
        commands += ['cg_thirdPerson 1', 'cg_thirdPersonRange 112',
                     'wait 90', 'ui_openmenu c01_test_pause']
    else:
        commands += ['cg_thirdPerson 0', 'npc spawn stormtrooper', 'wait 45',
                     'uimenu recon_freeze', 'fx_freeze 1']
    return commands + ['r_we freeze', 'wait 25']


def capture(label, master):
    return [f'r_lightReconUse {master}', 'wait 25', f'echo C04_{label}_BEGIN',
            'r_lightReconInfo', 'viewpos', 'cl_paused', 'sv_paused', 'r_speeds 1', 'wait 2',
            f'screenshot_tga c04-{label.lower()}', 'wait 8', 'r_speeds 0',
            f'echo C04_{label}_END']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('installation', 'binaries', 'renderer', 'fixture-tool', 'output'):
        p.add_argument('--' + name, required=True, type=Path)
    for name in ('mp-test-ui', 'sp-binaries', 'authored-base', 'geometry', 'private-archives'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--map', required=True)
    p.add_argument('--mp', action='store_true')
    p.add_argument('--legacy', action='store_true')
    p.add_argument('--sun', action='store_true')
    p.add_argument('--lifecycle', action='store_true', help='Also test vid_restart and disconnect')
    p.add_argument('--missing-field', action='store_true', help='Use an empty cache and require legacy fallback')
    p.add_argument('--pose', nargs=4, required=True, type=int)
    p.add_argument('--bounds', nargs=6, required=True, type=float)
    p.add_argument('--timeout', type=int, default=120)
    args = p.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    runtime, config = prepare_runtime(args, 'legacy' if args.legacy else 'modern', 1)
    home = runtime / 'home/OpenJK'
    if args.authored_base:
        for name in ('shaders', 'textures'):
            shutil.copytree(args.authored_base / name, home / name, dirs_exist_ok=True)
        source = args.authored_base / 'maps' / (args.map.removeprefix('lrq3_') + '.bsp')
        (home / 'maps').mkdir(exist_ok=True)
        shutil.copy2(source, home / 'maps' / (args.map + '.bsp'))
    config.update(r_customwidth=640, r_customheight=360, r_picmip=1,
                  r_lightReconUse=0, r_lightReconEntities=1, r_lightReconSun=0,
                  r_volumetricFog=0, r_cubeMapping=0, r_diffuseIBL=0,
                  r_sunlightMode=2 if args.sun else 0, r_sunShadows=int(args.sun),
                  r_sunShadowMode=int(args.sun), r_entitySunLight=int(args.sun),
                  r_shadowMapSize=512, r_normalMapping=int(not args.legacy),
                  r_specularMapping=int(not args.legacy), r_drawSun=0,
                  cg_draw2D=0, cg_drawGun=0, cg_thirdPerson=0,
                  cg_thirdPersonCameraDamp=1, cg_thirdPersonTargetDamp=1,
                  r_smaa=0, r_motionBlur=0, r_ssao=0, cg_shadows=0,
                  con_notifytime=0, g_spawnInvulnerability=0, cg_saberTrail=0)
    if args.mp:
        config.update(forcepowers='1-1-000000000000000000', g_maxForceRank=1)
    manifest = ['wait 20', f'devmap {args.map}', 'wait 150', 'r_lightReconInfo dependencies']
    manifest_config = dict(config)
    cache = runtime / 'home/rend2/lightrecon/v1'
    geometry = args.geometry
    if not geometry and not args.missing_field:
        manifest_config['r_lightReconBuild'] = 3
        center = [(a + b) / 2 for a, b in zip(args.bounds[:3], args.bounds[3:])]
        radius = max(b - a for a, b in zip(args.bounds[:3], args.bounds[3:])) / 2
        manifest += ['r_lightReconExport c04 1 ' + ' '.join(map(str, (*center, radius))), 'wait 30']
        geometry = cache / 'observations/c04.jkobs'
    launch(args, runtime, manifest_config, manifest + ['echo C01_DONE', 'quit'], 'manifest')
    if not args.missing_field:
        subprocess.run([str(args.fixture_tool), str(cache),
                        str(runtime / 'manifest-console.log'), str(geometry),
                        *map(str, args.bounds)], check=True)

    commands = ['wait 20'] + scene(args)
    for label, master in [('A', 0), ('AA', 0), ('B', 1), ('BB', 1), ('RESTORE', 0)]:
        commands += capture(label, master)
        if label in ('AA', 'BB'):
            commands += [f'echo C04_GPU_{label}_BEGIN', 'r_speeds 100', 'wait 35',
                         'r_speeds 0', f'echo C04_GPU_{label}_END']
    if not args.legacy:
        commands += ['r_forwardPlusSpawnTestLights 4 160', 'wait 20']
        commands += capture('DYNAMIC_A', 0) + capture('DYNAMIC_B', 1)
        commands += ['r_forwardPlusSpawnTestLights 0 160']
    if args.lifecycle:
        commands += ['r_lightReconUse 1', 'vid_restart', 'wait 150', 'echo C04_RESTART_BEGIN',
                     'r_lightReconInfo', 'echo C04_RESTART_END', 'disconnect', 'wait 20',
                     'echo C04_UNLOAD_BEGIN', 'r_lightReconInfo', 'echo C04_UNLOAD_END']
    commands += ['echo C01_DONE', 'quit']
    log = launch(args, runtime, config, commands, 'captures')
    rows = {}
    cameras = set()
    for label in ['A', 'AA', 'B', 'BB', 'RESTORE'] + ([] if args.legacy else ['DYNAMIC_A', 'DYNAMIC_B']):
        block = re.search(f'C04_{label}_BEGIN(.*?)C04_{label}_END', log, re.S)
        if not block or not rendered_world(block[1]):
            raise RuntimeError('Missing world in ' + label)
        if not all(re.search(name + r'\s*=\s*"1"', block[1]) for name in ('cl_paused', 'sv_paused')):
            raise RuntimeError('Unfrozen capture ' + label)
        camera = re.search(r'maps/' + re.escape(args.map) + r'\.bsp\s+\(([-\d ]+)\)\s*:\s*(-?\d+)', block[1])
        if not camera:
            raise RuntimeError('No world camera in ' + label)
        cameras.add(camera.groups())
        stats = re.search(r'entity-irradiance generation=(\d+) queries=(\d+) hits=(\d+) root=(\d+) '
                          r'body=(\d+) fallback=(\d+) cpu=([\d.]+)us', block[1])
        if not stats:
            raise RuntimeError('Missing C04 diagnostics')
        generation, queries, hits, root, body, fallback = map(int, stats.groups()[:6])
        expect_field = label in ('B', 'BB', 'DYNAMIC_B') and not args.missing_field
        if bool(root + body) != expect_field:
            raise RuntimeError('Unexpected field selection in ' + label + '\n' + block[1][:1500])
        if args.missing_field and not re.search(r'r_lightReconEntities requested=1 effective=0 capability=Missing', block[1]):
            raise RuntimeError('Expected missing field in ' + label)
        rows[label] = dict(generation=generation, queries=queries, hits=hits,
                           root=root, body=body, fallback=fallback, cpu_us=float(stats[7]))
        draw = re.search(r'(\d+)/(\d+)/(\d+) shaders/batches/surfs', block[1])
        rows[label]['shaders_batches_surfaces'] = list(map(int, draw.groups()))
    if len(cameras) != 1:
        raise RuntimeError('Camera moved across captures')
    if args.lifecycle:
        if not re.search(r'C04_RESTART_BEGIN.*?r_lightReconEntities requested=1 effective=1', log, re.S):
            raise RuntimeError('Field unavailable after vid_restart')
        if not re.search(r'C04_UNLOAD_BEGIN.*?lifecycle=Unloaded', log, re.S):
            raise RuntimeError('Field survived world unload')

    def path(label):
        return home / 'screenshots' / ('c04-' + label.lower() + '.tga')

    comparisons = {}
    for x, y in [('A', 'AA'), ('B', 'BB'), ('A', 'RESTORE'), ('A', 'B')]:
        comparisons[x + '/' + y] = compare(path(x), path(y), **IMAGE_TOLERANCE)
    if not all(comparisons[k]['passed'] for k in ('A/AA', 'B/BB', 'A/RESTORE')):
        raise RuntimeError('Unstable A/A, B/B or restore image: ' + str(comparisons))
    if comparisons['A/B']['passed'] != args.missing_field:
        raise RuntimeError('Unexpected A/B image comparison: ' + str(comparisons['A/B']))
    if not args.legacy:
        for a, b in [('A', 'DYNAMIC_A'), ('B', 'DYNAMIC_B'), ('DYNAMIC_A', 'DYNAMIC_B')]:
            comparisons[a + '/' + b] = compare(path(a), path(b), **IMAGE_TOLERANCE)
    for label in ('A', 'B'):
        image = Image.open(path(label))
        if np.asarray(image).std() < 2:
            raise RuntimeError('Flat capture ' + label)
        image.save(args.output / (label.lower() + '.png'))
    gpu = {}
    for label in ('AA', 'BB'):
        timing = re.search(f'C04_GPU_{label}_BEGIN(.*?)C04_GPU_{label}_END', log, re.S)
        samples = re.findall(r'([A-Za-z][\w +/_()-]*): ([\d.]+)ms', timing[1])
        gpu[label] = {name: dict(samples=sum(n == name for n, _ in samples),
                                 median_ms=median(float(v) for n, v in samples if n == name))
                      for name, _ in samples}
    result = dict(map=args.map, mp=args.mp, legacy=args.legacy, sun=args.sun,
                  authored=not args.missing_field, captures=rows, comparisons=comparisons, gpu=gpu)
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
