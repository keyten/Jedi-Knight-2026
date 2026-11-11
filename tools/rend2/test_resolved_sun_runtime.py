"""Actual SP and MP C01 authored-cache A/A/B/A, forced sun and lifecycle validation.

The private authored 100000 lux source is a consumer fixture, NOT stock recovery.
Installed archives are read-only; no renderer test source injection or hot I/O.
"""
import argparse
import json
import shutil
from types import SimpleNamespace
from pathlib import Path
import re
import subprocess
import time
import numpy as np
from PIL import Image
import test_lightrecon_legacy_runtime as L
from validation import mp_control
from gl43_validation.compare_images import compare


def launch(a, runtime, cfg, commands, label):
    if L.game_processes():
        raise RuntimeError('Independent game process is running')
    home = runtime / 'home/OpenJK'
    mp = getattr(a, 'mp', False)
    (home / ('autoexec.cfg' if mp else 'autoexec_sp.cfg')).write_text('\n'.join(commands) + '\n')
    (home / ('openjk.cfg' if mp else 'openjk_sp.cfg')).write_text(''.join(f'set {k} "{v}"\n' for k, v in cfg.items()))
    command = [str(runtime / ('openjk.x86_64.exe' if mp else 'openjk_sp.x86_64.exe'))]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, cfg[key]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    start = time.monotonic()
    with (runtime / (label + '-launch.log')).open('w') as out:
        child = subprocess.Popen(command, cwd=runtime, startupinfo=startup, stdout=out, stderr=subprocess.STDOUT)
        while child.poll() is None and time.monotonic() - start < a.timeout:
            time.sleep(.25)
        if child.poll() is None:
            child.terminate(); child.wait(timeout=15)
            raise TimeoutError(label)
    text = (home / 'qconsole.log').read_text(errors='replace')
    text = re.sub(r'(?m)^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ', '', text)
    text = re.sub(r'\^[0-9]', '', text)
    (runtime / (label + '-console.log')).write_text(text)
    if child.returncode or 'C01_DONE' not in text or re.search(r'GL_INVALID|GLSL shader (?:compile|link) error', text):
        raise RuntimeError('Incomplete/failed game run ' + label + '\n' + text[-2000:])
    return text


def prepare_scene(name):
    if name.startswith('mp/'):
        x, y, z, yaw = mp_control.CAMERAS[name]
        return ['wait 30', f'devmap {name}', 'wait 180', 'team free', 'wait 45',
                'god', 'notarget', 'noclip', 'saberholster', 'wait 30',
                'r_drawentities 0', '-forward', '-back', '-moveleft', '-moveright',
                '-left', '-right', '-lookup', '-lookdown',
                f'setviewpos {x} {y} {z} {yaw}', 'wait 30',
                'ui_openmenu c01_test_pause', 'r_we freeze', 'wait 30']
    x, y, z, yaw = L.CAMERAS[name]
    return ['wait 30', f'devmap {name}', 'wait 180', 'cam_disable', 'exitview',
            'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0',
            'con_notifytime 0', 'give all', 'wait 15', 'weapon 4', 'wait 15',
            f'setviewpos {x} {y} {z} {yaw}', 'wait 90', 'cam_disable', 'lock_disable',
            'cg_thirdPerson 0', f'setviewpos {x} {y} {z} {yaw}', 'wait 15',
            'uimenu recon_freeze', 'fx_freeze 1', 'r_we freeze', 'wait 30']


def capture(label):
    return ['wait 60', f'echo C01_{label}_BEGIN', 'r_lightReconInfo', 'viewpos',
            'cl_paused', 'sv_paused', 'r_speeds 1', 'wait 3',
            f'screenshot_tga c01-{label.lower()}', 'wait 60', 'r_speeds 0',
            f'echo C01_{label}_END']


def prepare_runtime(a, profile='modern', policy=1):
    a.output.mkdir(parents=True, exist_ok=False)
    runtime = a.output / a.map.replace('/', '_')
    setup = a
    if a.mp:
        setup = SimpleNamespace(**vars(a)); setup.binaries = a.sp_binaries
    cfg, _ = L.prepare_runtime(setup, runtime, profile, policy)
    if a.mp:
        home = runtime / 'home/OpenJK'
        for name in ['openjk.x86_64.exe', 'rend2_glslc.exe']:
            shutil.copy2(a.binaries / name, runtime / name)
        for name in ['cgamex86_64.dll', 'jampgamex86_64.dll']:
            shutil.copy2(a.binaries / name, home / name)
        shutil.copy2(a.mp_test_ui, home / 'uix86_64.dll')
        shutil.copy2(a.renderer, runtime / 'rd-rend2_x86_64.dll')
        cfg.update(cl_renderer='rd-rend2', sv_pure=0, g_gametype=0, bot_minplayers=0,
                   sv_maxclients=1,
                   cg_draw2D=0, cg_drawGun=0, cg_thirdPerson=0, con_notifytime=0)
    return runtime, cfg


def run(a):
    runtime, cfg = prepare_runtime(a)
    cfg.update(r_customwidth=640, r_customheight=360, r_picmip=2,
               r_shadowMapSize=512, r_sunShadowMode=1, r_sunShadowCache=a.cache_mode,
               r_sunlightMode=2, r_sunShadows=1, r_depthPrepass=1,
               r_atmosphere=1, r_atmosphereSky=2, r_clouds=1, r_cloudScale=4,
               r_cloudTemporal=0, r_cloudWindSpeed=0, r_cloudShadows=1,
               r_volumetricFog=2, r_volumetricFogQuality=0, r_volumetricFogTemporal=0,
               r_autoExposure=0, r_cameraExposure=0, r_drawSun=0, r_pbrSunIlluminance=0,
               r_cubeMapping=0, r_diffuseIBL=0, r_lightReconUse=0, r_lightReconSun=1)
    if a.mp:
        # The sky is visible in duel4. Cloud ray jitter advances while game time is paused.
        cfg.update(r_clouds=0, r_cloudShadows=0)
    text = launch(a, runtime, cfg, prepare_scene(a.map) +
                  ['r_lightReconInfo dependencies', 'echo C01_DONE', 'quit'], 'manifest')
    cache = runtime / 'home/rend2/lightrecon/v1'
    subprocess.run([str(a.fixture_tool), str(cache), str(runtime / 'manifest-console.log')], check=True)
    cmds = prepare_scene(a.map)
    for label, use in [('A', 0), ('AA', 0), ('B', 1), ('BB', 1), ('RESTORE', 0)]:
        cmds += [f'r_lightReconUse {use}'] + capture(label)
        if label in ['AA', 'BB']:
            timing_label = 'A' if label == 'AA' else 'B'
            cmds += [f'echo C01_TIMING_{timing_label}_BEGIN', 'r_shadowStats 1', 'r_speeds 100',
                     'wait 60', 'r_speeds 0', 'r_shadowStats 0', f'echo C01_TIMING_{timing_label}_END']
        if label == 'BB': cmds += ['r_sunShadowVerify 2', 'wait 30', 'r_sunShadowVerify 0']
    cmds += ['r_sunShadowVerify 2', 'wait 12', 'r_sunShadowVerify 0']
    cmds += ['r_lightReconUse 1', 'r_forceSun 1'] + capture('FORCED')
    unfreeze = 'ui_openmenu c01_test_resume' if a.mp else 'uimenu recon_unfreeze'
    freeze = 'ui_openmenu c01_test_pause' if a.mp else 'uimenu recon_freeze'
    cmds += ['r_forceSun 2', unfreeze, 'wait 60', freeze] + capture('ANIMATED')
    cmds += ['r_forceSun 0', 'vid_restart', 'wait 180'] + capture('RESTART')
    cmds += ['disconnect', 'wait 15', 'echo C01_UNLOAD_BEGIN', 'r_lightReconInfo',
             'echo C01_UNLOAD_END', 'wait 30', 'echo C01_DONE', 'quit']
    text = launch(a, runtime, cfg, cmds, 'capture')
    rows = {}
    cameras = {}
    for label in ['A', 'AA', 'B', 'BB', 'RESTORE', 'FORCED', 'ANIMATED', 'RESTART']:
        blocks = re.findall(f'C01_{label}_BEGIN(.*?)C01_{label}_END', text, re.S)
        if len(blocks) != 1:
            raise ValueError('Missing/duplicate capture block ' + label)
        block = blocks[0]
        selected = re.search(r'resolved-sun canonical=(\d+) policy=(\d+) kind=(\w+) reason=(\w+)', block)
        if not selected or selected[1] != ('1' if label in ['B', 'BB', 'RESTART'] else '0'):
            raise ValueError('Incorrect selection ' + label + '\n' + block[:1500])
        if not re.search(r'\d+/\d+/\d+ shaders/batches/surfs \d+ leafs', block):
            raise ValueError('Capture does not show a rendered world: ' + label)
        if label != 'RESTART':
            for cvar in ('cl_paused', 'sv_paused'):
                if not re.search(cvar + r'\s*=\s*"1"', block):
                    raise ValueError('Unfrozen scene ' + label)
        if label in ['A', 'AA', 'B', 'BB', 'RESTORE']:
            camera = re.search(r'maps/' + re.escape(a.map) + r'\.bsp \((-?\d+ -?\d+ -?\d+)\) : (-?\d+)', block)
            if not camera:
                raise ValueError('Missing capture camera ' + label)
            cameras[label] = camera.groups()
        rows[label] = list(selected.groups())
    if len(set(cameras.values())) != 1:
        raise ValueError('Capture camera moved: ' + str(cameras))
    if not re.search(r'C01_UNLOAD_BEGIN.*?lifecycle=Unloaded', text, re.S):
        raise ValueError('World publication survived unload')
    images = runtime / 'home/OpenJK/screenshots'
    def path(label): return images / ('c01-' + label.lower() + '.tga')
    for label in ('A', 'AA', 'B', 'BB', 'RESTORE'):
        data = np.asarray(Image.open(path(label)))
        if data.std() < 2:
            raise ValueError('Flat/invalid world capture ' + label)
    comparisons = {}
    for x, y in [('A', 'AA'), ('B', 'BB'), ('A', 'RESTORE'), ('A', 'B')]:
        comparisons[x + '/' + y] = compare(path(x), path(y), **L.IMAGE_TOLERANCE)
    verification = re.findall(r'sunShadowVerify cascade (\d): (PASS|MISMATCH), ([^\n]+)', text)
    if not verification or any(row[1] != 'PASS' for row in verification):
        raise ValueError('No valid static-cache depth verification: ' + str(verification[:5]))
    timing = {}
    for label in ['A', 'B']:
        block = re.search(f'C01_TIMING_{label}_BEGIN(.*?)C01_TIMING_{label}_END', text, re.S)
        if block:
            records = re.findall(r'([A-Za-z][A-Za-z0-9 ()+/_-]*): ([\d.]+)ms(?: |$)', block[1])
            from statistics import median
            timing[label] = {name.strip(): dict(samples=sum(n == name for n, _ in records),
                median_ms=median(float(v) for n, v in records if n == name)) for name, _ in records}
    if not all(comparisons[x]['passed'] for x in ['A/AA', 'B/BB', 'A/RESTORE']):
        raise ValueError('Invalid A/A or restore control: ' + str(comparisons))
    if comparisons['A/B']['passed']:
        raise ValueError('Authored sun has no measured visual effect')
    comparisons['A/B'].pop('passed')
    comparisons['A/B']['expected_difference'] = True
    result = dict(passed=True, scope='Manually authored controlled sun; no recovered stock lighting',
                  map=a.map, selection=rows, cameras=cameras, comparisons=comparisons,
                  settings=cfg, screenshots=str(images), gpu_timing=timing,
                  static_depth_checks=len(verification), original_stock_recovery=False)
    (a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['installation', 'binaries', 'renderer', 'fixture-tool', 'output']:
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--map', choices=tuple(L.CAMERAS) + tuple(mp_control.CAMERAS), default='hoth2')
    p.add_argument('--mp', action='store_true', help='Launch a private local MP validation world')
    p.add_argument('--mp-test-ui', type=Path, help='Private pause UI built with c01-mp-pause-ui.patch')
    p.add_argument('--sp-binaries', type=Path, help='SP helper binaries required by common stock-only setup')
    p.add_argument('--timeout', type=int, default=600)
    p.add_argument('--cache-mode', type=int, choices=[1, 2, 3], default=2)
    a = p.parse_args()
    for name in ['installation', 'binaries', 'renderer', 'fixture_tool', 'output']:
        setattr(a, name, getattr(a, name).resolve())
    if a.mp and not (a.sp_binaries and a.mp_test_ui): p.error('--mp needs --sp-binaries and --mp-test-ui')
    if a.mp_test_ui: a.mp_test_ui = a.mp_test_ui.resolve()
    if a.sp_binaries: a.sp_binaries = a.sp_binaries.resolve()
    run(a)
