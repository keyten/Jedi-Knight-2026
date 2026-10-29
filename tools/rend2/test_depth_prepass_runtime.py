"""Depth prepass without a velocity buffer: timing, depth check and captures.

Hidden low resolution SP run on mp/ffa3 with everything that wants a velocity
buffer off (SSR, SSGI, SMAA T2x, motion blur, LTC saber pass), so the prepass
draws depth only. Times the "Depth prepass" GPU timer with r_depthPrepassFast
1 (depth-only velocity programs) and 0 (lightall / generic) in A/B/A/B order,
runs r_depthPrepassVerify (samples nearer in one program family than in the
other, both orders) and captures both modes plus a repeat of the first.

Writes only under build/prepass-runtime/<label>; never touches the install.
    python tools/rend2/test_depth_prepass_runtime.py --installation <OpenJK/build-rend2> --label intel
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def game_processes():
    result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True)
    return [line for line in result.stdout.splitlines()
            if re.match(r'"openjk(?:_sp)?\.x86_64\.exe"', line, re.I)]


def tga_pixels(path):
    data = path.read_bytes()
    assert data[2] == 2 and data[16] == 24, path
    width, height = struct.unpack_from('<HH', data, 12)
    pixels = data[18 + data[0]:]
    if len(pixels) != width * height * 3:
        stride = (width * 3 + 3) & ~3
        pixels = b''.join(pixels[y * stride:y * stride + width * 3] for y in range(height))
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/ssr-msvc/RelWithDebInfo')
    parser.add_argument('--label', required=True)
    parser.add_argument('--nvidia', action='store_true')
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=360)
    parser.add_argument('--bench-frames', type=int, default=120)
    parser.add_argument('--rounds', type=int, default=3, help='A/B rounds of r_depthPrepassFast 1 / 0')
    parser.add_argument('--no-npcs', action='store_true', help='static view (timing only, like the SSR runs)')
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE')
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9_-]+', args.label)

    runtime = ROOT / 'build/prepass-runtime' / args.label
    home = runtime / 'home/OpenJK'
    if (home / 'screenshots').exists():
        shutil.rmtree(home / 'screenshots')
    home.mkdir(parents=True, exist_ok=True)
    for name in ('openjk_sp.x86_64.exe', 'rend2_glslc.exe', 'rdsp-rend2_x86_64.dll'):
        shutil.copy2(args.build / name, runtime / name)
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    shutil.copy2(args.build / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    # archived cvars of an earlier run must not leak into this one
    for stale in ('openjk_sp.cfg', 'qconsole.log'):
        if (home / stale).exists():
            (home / stale).unlink()

    view = 'setviewpos 671 -769 -70 290'
    cfg = ['wait 180', 'devmap mp/ffa3', 'wait 100', 'exitview', 'god', 'notarget', 'noclip',
           'cg_draw2D 0', 'cg_drawGun 0', 'con_notifytime 0', view, 'wait 40',
           # skeletal (G2) models in view for the timing and the depth check
           *([] if args.no_npcs else ['npc spawn reborn', 'npc spawn stormtrooper']), 'wait 60',
           'com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
    labels = []
    for r in range(args.rounds):
        for fast in (1, 0):
            label = f'fast{fast}_r{r}'
            labels.append(label)
            cfg += [f'r_depthPrepassFast {fast}', 'wait 30', f'echo PREPASS_BEGIN_{label}', 'r_speeds 100',
                    f'wait {args.bench_frames}', 'r_speeds 0', f'echo PREPASS_END_{label}']
    cfg += ['r_depthPrepassFast 1', 'echo PREPASS_VERIFY_BEGIN', 'r_depthPrepassVerify 1', 'wait 120',
            'r_depthPrepassVerify 0', 'echo PREPASS_VERIFY_END',
            # captures: freeze shader time so the A/B/A shots see the same frame
            'com_maxfps 60', 'timescale 0', 'wait 20',
            'r_depthPrepassFast 1', 'wait 10', 'screenshot_tga fast',
            'r_depthPrepassFast 0', 'wait 10', 'screenshot_tga legacy',
            'r_depthPrepassFast 1', 'wait 10', 'screenshot_tga fast_repeat',
            'timescale 1', 'echo PREPASS_DONE', 'wait 60', 'quit']

    settings = dict(fs_basepath=str(args.installation.resolve()), fs_homepath=str(runtime / 'home'),
        fs_game='OpenJK', cl_renderer='rdsp-rend2',
        r_mode='-1', r_customwidth=str(args.width), r_customheight=str(args.height), r_fullscreen='0',
        r_swapInterval='0', com_maxfps='60', com_maxfpsUnfocused='60', com_maxfpsMinimized='60',
        com_bootlogo='0', in_mouse='0', s_initsound='0', logfile='2', developer='0',
        r_offscreenPresent='1', r_picmip='2', r_hdr='1', r_autoExposure='0',
        r_depthPrepass='1', r_depthPrepassFast='1', r_depthPrepassVerify='0',
        # nothing that wants a velocity buffer (tr_image.cpp needVelocityBuffer)
        r_ssr='0', r_ssgi='0', r_smaa='0', r_motionBlur='0', r_ltcAreaLights='0',
        r_ext_multisample='0', r_cubeMapping='1', r_normalMapping='1', r_specularMapping='1',
        # unrelated features off (several game instances share the machine)
        r_weatherWetness='0', r_waterSurface='0', r_volumetricFog='0', r_volumetricWater='0',
        r_atmosphere='0', r_clouds='0', r_ssao='0', r_aoMode='0', r_contactShadows='0',
        r_sunlightMode='0', r_forwardPlus='0', r_bloom='0', r_skinSSS='0', r_autoGrass='0',
        r_rainStreaks='0', r_rainSplashes='0', r_rainLens='0', r_waterParticles='0',
        r_shaderProgramCache='1', r_shaderCompileProcesses='0', r_gl43='1')
    for item in args.set:
        key, value = item.split('=', 1)
        settings[key] = value
    config = [f'set {key} "{value}"' for key, value in settings.items()
              if key not in ('fs_basepath', 'fs_homepath', 'fs_game')]
    (home / 'autoexec_sp.cfg').write_text('\n'.join(config + cfg) + '\n', encoding='ascii')

    command = [str(runtime / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, settings[key]]
    env = os.environ.copy()
    if args.nvidia:
        env['SHIM_MCCOMPAT'] = '0x800000001'
    else:
        env.pop('SHIM_MCCOMPAT', None)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    others = len(game_processes())
    with (runtime / 'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=runtime, env=env, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        print(f'Launched PID {process.pid}: {runtime} ({others} other games running)', flush=True)
        started = time.monotonic()
        while process.poll() is None:
            if time.monotonic() - started > args.timeout:
                process.terminate()
                process.wait(timeout=20)
                raise TimeoutError(runtime)
            others = max(others, len(game_processes()) - 1)
            time.sleep(2)

    log = (home / 'qconsole.log').read_text(errors='replace')
    assert process.returncode == 0 and 'PREPASS_DONE' in log, log[-5000:]
    assert not re.search(r'GL_INVALID|GLSL shader compile error|GLSL shader link error|FBO.*incomplete', log), \
        log[-5000:]
    results = dict(renderer=re.findall(r'GL_RENDERER:.*', log)[-1:], other_games=others,
                   settings=settings, sections={})
    for label in labels:
        section = re.search(rf'PREPASS_BEGIN_{label}(.*?)PREPASS_END_{label}', log, re.S).group(1)
        for name in ('Depth prepass', 'Main View 0'):
            values = [float(v) for v in re.findall(re.escape(name) + r': ([0-9.]+)ms', section)][8:-4]
            assert values, (label, name)
            results['sections'].setdefault(label, {})[name] = dict(
                n=len(values), median=statistics.median(values), mean=statistics.mean(values))
    summary = {}
    for fast in (1, 0):
        medians = [results['sections'][f'fast{fast}_r{r}']['Depth prepass']['median'] for r in range(args.rounds)]
        summary[f'fast{fast}'] = dict(prepass_medians=medians, prepass_median=statistics.median(medians))
    results['summary'] = summary

    verify = re.search(r'PREPASS_VERIFY_BEGIN(.*?)PREPASS_VERIFY_END', log, re.S).group(1)
    checks = re.findall(r'depth prepass verify: (\w+) nearer than (\w+): (\d+) samples \((\d+) surfs\)', verify)
    assert len(checks) >= 10 and {c[0] for c in checks} == {'fast', 'legacy'}, checks
    results['verify'] = dict(frames=len(checks), max_nearer_samples=max(int(c[2]) for c in checks),
                             surfs=max(int(c[3]) for c in checks),
                             nonzero=[c for c in checks if int(c[2])][:10])

    shots = {name: tga_pixels(home / f'screenshots/{name}.tga') for name in ('fast', 'legacy', 'fast_repeat')}
    def diff(a, b):
        pa, pb = shots[a], shots[b]
        assert len(pa) == len(pb)
        changed = sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])
        return dict(identical=pa == pb, changed_pixels=changed)
    results['captures'] = dict(fast_vs_repeat=diff('fast', 'fast_repeat'), fast_vs_legacy=diff('fast', 'legacy'))
    (runtime / 'results.json').write_text(json.dumps(results, indent=2))

    print('renderer:', results['renderer'], 'other games:', others)
    for label, passes in results['sections'].items():
        print(label, {name: round(value['median'], 3) for name, value in passes.items()})
    print('summary:', json.dumps(summary))
    print('verify:', json.dumps(results['verify']))
    print('captures:', json.dumps(results['captures']))
    assert results['verify']['max_nearer_samples'] == 0, 'prepass depth differs between the program families'
    print('PASS')


if __name__ == '__main__':
    main()
