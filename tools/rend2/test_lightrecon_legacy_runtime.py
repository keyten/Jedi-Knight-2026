#!/usr/bin/env python3
"""Isolated actual-SP A/B/A validation; installed assets are read-only inputs.

Defaults exercise hoth3 indoor and hoth2 outdoor with legacy and modern settings.
Missing binaries/assets, unstable controls or incomplete captures are failures,
never a passing visual result. The hidden-context GPU suite does not run this.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from gl43_validation.compare_images import compare
from test_ssr_runtime import game_processes

ROOT = Path(__file__).resolve().parents[2]
CAMERAS = {'hoth3': (3464, 700, 85, 90), 'hoth2': (1728, -576, 1040, 135)}
CONSUMERS = re.findall(r'LIGHTRECON_CONSUMER\((\w+),', (ROOT / 'shared/rd-rend2/tr_lightrecon_cvars.inc').read_text())
# A single display-code step in <=0.001% of this fixed 960x540 frame permits
# sparse FP16/8-bit rounding, not visible lighting or stochastic saber flicker.
IMAGE_TOLERANCE = dict(max_delta=1, max_pixels=5, max_mean=1e-5)


def settings(profile, policy):
    result = dict(cl_renderer='rdsp-rend2', r_fullscreen=0, r_mode=-1, r_customwidth=960, r_customheight=540,
        r_swapInterval=0, com_maxfps=60, com_maxfpsUnfocused=60, com_maxfpsMinimized=60,
        s_initsound=0, in_mouse=0, com_bootlogo=0, developer=0, logfile=2,
        r_shaderCompileProcesses=0, r_shaderProgramCache=1, r_shaderProgramLazy=1,
        r_gl43=1, r_hdr=1, r_autoExposure=0, r_cameraExposure=0, r_cameraMode=0, r_exposureCompensation=0,
        r_toneMap=1, r_toneMapMode=0, r_gamma=1, r_intensity=1, r_overBrightBits=0,
        r_ext_multisample=0, r_smaa=0, r_motionBlur=0, r_ssr=0, r_ssgi=0,
        r_autoPBR=0, r_cubeMapping=0, r_diffuseIBL=0, r_specularAA=0, r_skinSSS=0,
        r_weatherWetness=0, r_waterSurface=0, r_volumetricWater=0,
        r_atmosphere=0, r_clouds=0, r_ssao=0, r_aoMode=0, r_contactShadows=0,
        r_staticLightReconstruction=0, r_volumetricFogStaticDirectional=0,
        r_recoveredVolumetricLights=0, r_lightPortals=0, r_entityLightProbes=0,
        r_entityLightGrid=0, r_entitySunSplit=0, r_ltcAreaCalibration=0,
        r_volumetricFogTemporal=0, r_volumetricFogRGBExtinction=0,
        r_sunShadowCache=0, r_shadowMapSize=1024, r_sunShadowMode=0,
        r_dynamiclight=1, r_ltcSaberAreaLights=0, r_lightReconBuild=policy, r_lightReconUse=0,
        fx_physicalization=0, fx_physicalizationSources=0, fx_physicalizationEmission=0,
        cg_fov=80, cg_bobup=0, cg_bobpitch=0, cg_bobroll=0, cg_runpitch=0, cg_runroll=0)
    result.update({c: 1 for c in CONSUMERS}) # master off, all consumer requests deliberately on
    if profile == 'legacy':
        result.update(r_normalMapping=0, r_specularMapping=0, r_pbrPhysical=0,
                      r_forwardPlus=0, r_ltcAreaLights=0, r_volumetricFog=0, r_sunlightMode=0)
    else:
        result.update(r_normalMapping=1, r_specularMapping=1, r_pbrPhysical=1,
                      r_forwardPlus=1, r_ltcAreaLights=1, r_volumetricFog=2,
                      r_sunlightMode=1, r_sunShadowMode=1, r_sunShadows=1)
    return result


def commands(map_name):
    x, y, z, yaw = CAMERAS[map_name]
    result = ['wait 120', f'devmap {map_name}', 'wait 180', 'cam_disable', 'exitview',
        'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0', 'con_notifytime 0',
        'give all', 'wait 15', 'weapon 4', 'wait 15', f'setviewpos {x} {y} {z} {yaw}', 'npc spawn stormtrooper', 'wait 90',
        'cam_disable', 'lock_disable', 'cg_thirdPerson 0', f'setviewpos {x} {y} {z} {yaw}', 'wait 15',
        'uimenu recon_freeze', 'fx_freeze 1', 'wait 30']
    for layer, count in [('base', 0), ('dynamic', 4)]:
        if layer == 'dynamic':
            # Scheduled FX need a running game tick to instantiate. Freeze again
            # before comparisons; each layer has its own fixed A/B/A camera.
            result += ['uimenu recon_unfreeze', 'fx_freeze 0', 'wait 30', 'developer 1',
                       'fxplay test/lightrecon_legacy 8', 'wait 30', 'fx_freeze 1', 'developer 0',
                       'uimenu recon_freeze', 'wait 20', 'screenshot_tga recon-fx-proof']
        result += [f'r_forwardPlusSpawnTestLights {count} 384', 'wait 20']
        for label, master, request in [('a', 0, 1), ('b', 1, 0), ('repeat', 0, 1)]:
            tag = f'{layer}_{label}'.upper()
            result += [f'r_lightReconUse {master}'] + [f'{c} {request}' for c in CONSUMERS]
            result += ['wait 12', f'echo LIGHTRECON_{tag}_BEGIN', 'r_lightReconInfo',
                       'r_pbrPhysical', 'r_forwardPlus', 'r_ltcAreaLights', 'r_volumetricFog', 'r_sunlightMode', 'cl_paused', 'sv_paused', 'viewpos',
                       f'writecam recon_{layer}_{label}', 'glslusage all', f'echo LIGHTRECON_{tag}_END', f'screenshot_tga recon-{layer}-{label}', 'wait 6']
    result += ['echo LIGHTRECON_RUNTIME_DONE', 'wait 5', 'quit']
    return result


def validate_consumers(section, label, policy):
    """An absent/truncated diagnostic must never be mistaken for all-off."""
    rows = re.findall(r'\b(r_lightRecon\w+) requested=(\d+) effective=(\d+) capability=(\w+) feature=(\d+)', section)
    if len(rows) != len(CONSUMERS) or {r[0] for r in rows} != set(CONSUMERS):
        raise AssertionError('Missing or duplicate consumer diagnostics')
    requested, master = ('0', '1') if label == 'B' else ('1', '0')
    capability = 'Disabled' if policy == 0 else 'Missing'
    if any(r[1:] != (requested, '0', capability, '0') for r in rows):
        raise AssertionError(f'Unexpected disabled consumer state: {rows}')
    state = re.search(r'lightRecon: [^\n]*use=(\d+)', section)
    if not state or state[1] != master:
        raise AssertionError('Master switch state not observed')


def run_case(args, map_name, profile):
    if game_processes():
        raise RuntimeError('Another OpenJK process is running; refusing overlapping validation')
    runtime = args.output.resolve() / f'{map_name}-{profile}-build{args.build_policy}-{args.label}'
    if runtime.exists():
        raise RuntimeError(f'Use a fresh output directory, preserving prior evidence: {runtime}')
    base = args.output.resolve() / 'stock-assets/base'
    home = runtime / 'home/OpenJK'
    base.mkdir(parents=True, exist_ok=True)
    home.mkdir(parents=True)
    # Standard SP pause freezes serverTime and animation. timescale=0 does NOT:
    # Com_ModifyMsec scales only nonzero values. A blank non-fullscreen test menu
    # keeps rendering the world without adding a pause overlay or changing engine.
    (home / 'ui').mkdir()
    (home / 'ui/ingame.txt').write_text('loadmenu { "ui/recon-freeze.menu" }\n', encoding='ascii')
    (home / 'ui/recon-freeze.menu').write_text(
        'menuDef { name "recon_freeze" rect 0 0 0 0 fullScreen 0 visible 1 '
        'onOpen { setcvar "cl_paused" "1"; } }\n'
        'menuDef { name "recon_unfreeze" rect 0 0 0 0 fullScreen 0 visible 1 '
        'onOpen { setcvar "cl_paused" "0"; } }\n', encoding='ascii')
    (home / 'effects/test').mkdir(parents=True)
    shutil.copy2(ROOT / 'tools/rend2/lightrecon_baseline/legacy_fx.efx', home / 'effects/test/lightrecon_legacy.efx')
    # Only stock archives, excluding installed loose maps, configs and extra PK3s.
    # Reuse one private stock-only directory. Prefer NTFS hard links; Program
    # Files ACLs may require copying instead. Neither tool nor game writes PK3s.
    archives = []
    for name in ('assets0.pk3', 'assets1.pk3', 'assets2.pk3', 'assets3.pk3'):
        source = args.installation / 'base' / name
        if not source.is_file():
            raise FileNotFoundError(source)
        target = base / name
        if not target.exists():
            try:
                os.link(source, target)
            except OSError:
                shutil.copy2(source, target)
        if target.stat().st_size != source.stat().st_size:
            raise RuntimeError('Incomplete private stock archive ' + str(target))
        archives.append(dict(name=name, bytes=source.stat().st_size))
    for name in ('openjk_sp.x86_64.exe', 'rend2_glslc.exe'):
        shutil.copy2(args.binaries / name, runtime / name)
    shutil.copy2(args.binaries / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    shutil.copy2(args.renderer, runtime / 'rdsp-rend2_x86_64.dll')
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    config = settings(profile, args.build_policy)
    config.update(fs_basepath=str(base.parent), fs_homepath=str(runtime / 'home'), fs_game='OpenJK')
    (home / 'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in config.items()), encoding='ascii')
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands(map_name)) + '\n', encoding='ascii')
    command = [str(runtime / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, config[key]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    env = os.environ.copy()
    if args.nvidia:
        env['SHIM_MCCOMPAT'] = '0x800000001'
    print(f'Launching {map_name}/{profile}, private runtime {runtime}', flush=True)
    with (runtime / 'launch.log').open('w') as log:
        process = subprocess.Popen(command, cwd=runtime, env=env, startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(.5)
        if process.poll() is None:
            process.terminate() # only this invocation's child process
            process.wait(timeout=20)
            raise TimeoutError(runtime)
    text = (home / 'qconsole.log').read_text(errors='replace')
    text = re.sub(r'(?m)^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ', '', text)
    text = re.sub(r'\^[0-9]', '', text)
    errors = re.findall(r'[^\r\n]*(?:GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|FBO.*incomplete|Could not load.*shader)[^\r\n]*', text)
    if process.returncode or 'LIGHTRECON_RUNTIME_DONE' not in text or errors:
        raise RuntimeError(f'Incomplete or failed runtime: {runtime}\n{errors}\n{text[-2500:]}')
    if re.search(r'r_lightRecon\w+ requested=\d+ effective=[1-9]', text):
        raise AssertionError('Disabled consumer became effective')
    if 'fxplay: test/lightrecon_legacy at distance 8' not in text:
        raise AssertionError('Controlled legacy FX was not played')
    applied = re.findall(r'build applied=(\d) result=(\w+) cache-called=(\d) build-called=(\d)', text)
    expected = (str(args.build_policy), 'Disabled' if args.build_policy == 0 else 'Unavailable', '0', '0')
    if len(applied) != 6 or any(row != expected for row in applied):
        raise AssertionError(f'Map-load policy or missing-backend contract not observed: {applied}')
    shots = home / 'screenshots'
    camera_file = home / f'maps/{map_name}_cam.map'
    cameras = re.findall(r'"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"', camera_file.read_text())
    if len(cameras) != 6 or len(set(cameras[:3])) != 1 or len(set(cameras[3:])) != 1:
        raise AssertionError(f'Camera is not stable across A/B/A: {cameras}')
    from PIL import Image
    import numpy as np
    pixels = np.asarray(Image.open(shots / 'recon-base-a.tga').convert('RGB'))
    if pixels.std() < 2 or len(np.unique(pixels.reshape(-1, 3), axis=0)) < 64:
        raise AssertionError('Flat/empty capture: camera or game is invalid; never accept an equal blank frame')
    layers, usage = {}, {}
    for layer in ('base', 'dynamic'):
        layers[layer] = dict(comparison=compare(shots / f'recon-{layer}-a.tga', shots / f'recon-{layer}-b.tga', runtime / f'diff-{layer}-ab.png', **IMAGE_TOLERANCE),
                            repeat_control=compare(shots / f'recon-{layer}-a.tga', shots / f'recon-{layer}-repeat.tga', runtime / f'diff-{layer}-repeat.png', **IMAGE_TOLERANCE))
        usage[layer] = {}
        for label in ('A', 'B', 'REPEAT'):
            tag = f'{layer}_{label}'.upper()
            section = re.search(f'LIGHTRECON_{tag}_BEGIN(.*?)LIGHTRECON_{tag}_END', text, re.S)
            if not section:
                raise AssertionError('Missing diagnostic capture ' + tag)
            validate_consumers(section[1], label, args.build_policy)
            usage[layer][label] = sorted(re.findall(r'^\s*(\S+)\s+(\d+) used of\s+(\d+)', section[1], re.M))
            for name in ('r_pbrPhysical', 'r_forwardPlus', 'r_ltcAreaLights', 'r_volumetricFog', 'r_sunlightMode', 'cl_paused', 'sv_paused'):
                matches = re.findall(r'\b' + name + r'\s*=\s*"([^"\s]+)', section[1])
                expected = 1 if name in ('cl_paused', 'sv_paused') else config[name]
                if not matches or int(float(matches[-1])) != expected:
                    raise AssertionError(f'{name} was changed or not queried: {matches}')
    stimulus = compare(shots / 'recon-base-a.tga', shots / 'recon-dynamic-a.tga')
    stimulus.pop('passed') # expected difference: this is a positive stimulus control
    stimulus['expected_difference'] = True
    if stimulus['differing_pixels'] < 100:
        raise AssertionError('Dynamic lights/FX stimulus did not visibly affect the frame')
    fx_stimulus = compare(shots / 'recon-base-a.tga', shots / 'recon-fx-proof.tga')
    fx_stimulus.pop('passed'); fx_stimulus['expected_difference'] = True
    if fx_stimulus['differing_pixels'] < 100:
        raise AssertionError(f'The legacy FX sprite/light did not visibly affect the frame: {fx_stimulus}')
    result = dict(map=map_name, profile=profile, camera=CAMERAS[map_name], recorded_camera=dict(base=cameras[0], dynamic=cameras[3]), settings=config,
        command=command, archives=archives, build_policy_records=applied, renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),
        source_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        driver=re.findall(r'GL_RENDERER:[^\r\n]*', text), layers=layers, dynamic_stimulus=stimulus, fx_stimulus=fx_stimulus,
        permutation_usage=usage, permutation_usage_identical=all(u['A'] == u['B'] == u['REPEAT'] for u in usage.values()),
        tolerance=IMAGE_TOLERANCE, errors=errors)
    result['passed'] = all(l['comparison']['passed'] and l['repeat_control']['passed'] for l in layers.values()) and result['permutation_usage_identical']
    (runtime / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('settings', 'command', 'permutation_usage')}, indent=2), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True, help='GameData containing stock base/assets[0-3].pk3')
    parser.add_argument('--binaries', type=Path, required=True, help='SP engine + game + rend2_glslc directory')
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/lightrecon-visual')
    parser.add_argument('--maps', nargs='+', choices=tuple(CAMERAS), default=list(CAMERAS))
    parser.add_argument('--profiles', nargs='+', choices=('legacy', 'modern'), default=['legacy', 'modern'])
    parser.add_argument('--build-policy', type=int, choices=range(4), default=3)
    parser.add_argument('--label', default='capture', help='Unique evidence label within an output directory')
    parser.add_argument('--nvidia', action='store_true')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
        parser.error('label must contain only letters, digits, dash and underscore')
    if os.name != 'nt':
        parser.error('This actual-SP harness currently requires Windows; GPU contracts remain separate')
    results = [run_case(args, m, p) for m in args.maps for p in args.profiles]
    (args.output / f'summary-{args.label}.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
