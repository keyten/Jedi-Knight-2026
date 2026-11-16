"""Isolated SP Phong gate/budget/distance/split checks, captures and GPU A/B timing.

python tools/rend2/test_phong_tessellation_runtime.py --installation ../OpenJK/build-rend2
Use --cinematic for the academy1 scripted-camera test. Results stay in build/phong-runtime.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def run(args, gameplay):
    label = 'cinematic' if args.cinematic else f'gameplay{gameplay}'
    if args.velocity:
        label += '-velocity'
    runtime = ROOT / 'build/phong-runtime' / label
    home = runtime / 'home/OpenJK'
    (home / 'ui').mkdir(parents=True, exist_ok=True)
    (home / 'ui/ingame.txt').write_text('loadmenu { "ui/phong-freeze.menu" }\n')
    (home / 'ui/phong-freeze.menu').write_text(
        'menuDef { name "phong_freeze" rect 0 0 0 0 fullScreen 0 visible 1 '
        'onOpen { setcvar "cl_paused" "1"; } }\n')
    for name in ('openjk_sp.x86_64.exe', 'rend2_glslc.exe', 'rdsp-rend2_x86_64.dll', 'jagamex86_64.dll'):
        shutil.copy2(ROOT / 'build/msvc/Release' / name, runtime / name)
    shutil.copy2(runtime / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    settings = dict(cl_renderer='rdsp-rend2', r_mode=-1, r_customwidth=1280, r_customheight=720,
                    r_fullscreen=0, r_swapInterval=0, com_maxfps=60, com_maxfpsUnfocused=60,
                    com_maxfpsMinimized=60, com_bootlogo=0, in_mouse=0, s_initsound=0,
                    logfile=2, developer=0, r_gl43=1, r_offscreenPresent=0,
                    cg_smoothCamera=0, cg_thirdPersonCameraDamp=1, cg_thirdPersonTargetDamp=1,
                    cg_thirdPersonRange=0, cg_thirdPersonVertOffset=0,
                    cg_thirdPersonAlpha=0, r_depthPrepass=1, r_depthPrepassFast=1,
                    r_hdr=1, r_picmip=1, r_shaderProgramLazy=1, r_speeds=0, r_autoExposure=0,
                    r_ssr=0, r_ssgi=0, r_smaa=0, r_motionBlur=int(args.velocity),
                    r_motionBlurShutterAngle=0, r_volumetricFog=0, r_volumetricWater=0,
                    r_waterSurface=0, r_atmosphere=0, r_clouds=0, r_weatherWetness=0,
                    r_autoGrass=0, r_ssao=0, r_aoMode=0, r_bloom=0, r_skinSSS=0,
                    r_sunlightMode=0, r_rainLens=0, r_lensWater=0, r_shaderCompileProcesses=0,
                    r_phongTessellation=0, r_phongTessellationCompare=0, r_compareSplit=.5,
                    r_phongTessGameplay=gameplay, r_phongTessMaxCharacters=1,
                    r_phongTessDistance=160, r_phongTessLevel=4, r_phongTessStrength=.5,
                    r_phongTessMaxDisplacement=1, r_phongTessDebug=1)
    commands = ['wait 90']
    if args.cinematic:
        commands += ['r_phongTessellation 1', 'devmap academy1', 'wait 1200',
                     'screenshot_tga cinematic', 'echo PHONG_RUNTIME_DONE', 'wait 20', 'quit']
    else:
        commands += ['devmap mp/ffa3', 'wait 120', 'exitview', 'cam_disable', 'lock_disable',
                     'god', 'notarget', 'noclip', 'cg_thirdPerson 0', 'cg_draw2D 0', 'cg_drawGun 0',
                     'con_notifytime 0', 'give all', 'wait 60', 'weapon 3', 'wait 60',
                     'setviewpos 671 -769 -70 290', 'wait 30', 'npc spawn kyle', 'wait 60',
                     'setviewpos 701 -769 -70 290', 'npc spawn jedi', 'wait 60',
                     'setviewpos 671 -769 -105 290', 'zoom', 'wait 60',
                     # timescale 0 does not freeze this engine: pause the server instead.
                     'uimenu phong_freeze', 'fx_freeze 1', 'wait 30',
                     'r_phongTessellation 1', 'wait 60', 'r_phongTessellation 0', 'wait 30',
                     'echo PHONG_OFF', 'screenshot_tga off',
                     'r_phongTessellation 1', 'echo PHONG_ON', 'wait 20', 'screenshot_tga on',
                     'r_phongTessMaxCharacters 0', 'echo PHONG_BUDGET_ZERO', 'wait 10', 'screenshot_tga budget0',
                     'r_phongTessMaxCharacters 2', 'echo PHONG_BUDGET_TWO', 'wait 10', 'screenshot_tga budget2',
                     'r_phongTessDistance 1', 'echo PHONG_RANGE_ONE', 'wait 10', 'screenshot_tga range1',
                     'r_phongTessDistance 160', 'r_phongTessMaxCharacters 1',
                     'r_phongTessellationCompare 2', 'wait 10', 'screenshot_tga compare2',
                     'r_phongTessellationCompare 1', 'wait 10', 'screenshot_tga split',
                     'r_phongTessDebug 2', 'wait 10', 'screenshot_tga selected',
                     'r_phongTessDebug 3', 'wait 10', 'screenshot_tga displacement',
                     'r_phongTessDebug 0', 'r_phongTessellationCompare 0', 'wait 20',
                     'r_depthPrepassVerify 1', 'wait 30', 'r_depthPrepassVerify 0']
        if gameplay and args.bench:
            commands += ['com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
            # Warm each configuration before timing. ABBA order limits thermal drift.
            for sample, enabled in enumerate((0, 1, 1, 0, 0, 1)):
                commands += [f'r_phongTessellation {enabled}', 'wait 120',
                             f'echo PHONG_BENCH_BEGIN_{sample}_{enabled}', 'r_speeds 100',
                             'wait 240', 'r_speeds 0',
                             # Existing wall-clock sampler, without synchronous GPU query logging.
                             'r_sunShadowBenchmark 240', 'wait 520',
                             f'echo PHONG_BENCH_END_{sample}_{enabled}']
        commands += ['glslusage all', 'echo PHONG_RUNTIME_DONE', 'wait 20', 'quit']
    config = [f'set {key} "{value}"' for key, value in settings.items()]
    (home / 'autoexec_sp.cfg').write_text('\n'.join(config + commands) + '\n')
    command = [str(runtime / 'openjk_sp.x86_64.exe'), '+set', 'fs_basepath', str(args.installation.resolve()),
               '+set', 'fs_homepath', str(runtime / 'home'), '+set', 'fs_game', 'OpenJK']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 1 if args.cinematic else 0
    exit_code = 0
    if not args.analyze_only:
        with (runtime / 'launch.log').open('w') as output:
            process = subprocess.Popen(command, cwd=runtime, startupinfo=startup, stdout=output, stderr=output)
            print(f'PID {process.pid}: {runtime}', flush=True)
            started = time.monotonic()
            while process.poll() is None:
                if time.monotonic() - started > 600:
                    process.terminate()
                    raise TimeoutError(runtime)
                time.sleep(1)
            exit_code = process.returncode
    log = (home / 'qconsole.log').read_text(errors='replace')
    assert 'PHONG_RUNTIME_DONE' in log and exit_code == 0, log[-4000:]
    assert not re.search(r'GL_INVALID|GLSL shader (?:compile|link) error|Could not load .*shader', log), log[-4000:]
    counts = re.findall(r'Phong tessellation: cinematic (\d), gameplay (\d), selected (\d+)/(\d+), range ([\d.]+)', log)
    assert counts and all(int(n) <= int(limit) for _, _, n, limit, _ in counts), counts
    report = dict(settings=settings, selection=counts, renderer=re.findall(r'GL_RENDERER:.*', log))
    if args.cinematic:
        assert any(c == '1' and int(n) > 0 for c, _, n, _, _ in counts), counts
    else:
        pixels = lambda name: (home / f'screenshots/{name}.tga').read_bytes()[18:]
        # Permit one 8-bit display rounding step between separately linked
        # programs; silhouette or shading errors are much larger than this.
        close = lambda a, b: a == b or all(abs(x - y) <= 1 for x, y in zip(a, b))
        for disabled in ('budget0', 'range1', 'compare2'):
            assert close(pixels('off'), pixels(disabled)), ('disabled path changed the frame', disabled)
        if not gameplay:
            assert all(n == '0' for _, _, n, _, _ in counts), counts
            assert close(pixels('off'), pixels('on')), 'cinematic-only mode changed gameplay'
        else:
            assert any(n == '1' for _, _, n, _, _ in counts), counts
            # Actor AI/spawn placement may leave just one candidate in view.
            assert any(limit == '2' for _, _, _, limit, _ in counts), counts
            assert not close(pixels('off'), pixels('on')), 'enabled tessellation did not change the close-up'
            assert '_PHONG' in log, 'no tessellation program was used'
            # Ignore the comparison's one-pixel divider; both halves must match
            # their full-frame references, including silhouettes and depth.
            split, off, on = pixels('split'), pixels('off'), pixels('on')
            for row in range(720):
                begin = row * 1280 * 3
                assert close(split[begin:begin + 640 * 3], off[begin:begin + 640 * 3]), ('split left', row)
                assert close(split[begin + 641 * 3:begin + 1280 * 3], on[begin + 641 * 3:begin + 1280 * 3]), ('split right', row)
            # A newly exposed one-pixel crack has green selected geometry on
            # both sides, but reveals a different color than the original mesh.
            selected = pixels('selected')
            green = bytearray(selected[i + 1] > 180 and selected[i] < 100 and selected[i + 2] < 100
                              for i in range(0, len(selected), 3))
            checked_gaps = 0
            max_gap_delta = 0
            for index in range(1280, len(green) - 1280):
                column = index % 1280
                if column <= 641 or column == 1279 or green[index]:
                    continue
                if ((green[index - 1] and green[index + 1]) or
                        (green[index - 1280] and green[index + 1280])):
                    checked_gaps += 1
                    pixel = index * 3
                    delta = max(abs(x - y) for x, y in zip(off[pixel:pixel + 3], on[pixel:pixel + 3]))
                    max_gap_delta = max(max_gap_delta, delta)
                    # Small changes at overlapping authored materials are valid;
                    # an exposed wall/clear-color stripe has high contrast.
                    assert delta <= 8, ('new high-contrast mesh crack', column, index // 1280, delta)
            report['seam_check'] = dict(checked_interior_gaps=checked_gaps,
                                       max_display_delta=max_gap_delta, new_high_contrast_cracks=0)
        timings = {0: {}, 1: {}}
        for sample, enabled, body in re.findall(r'PHONG_BENCH_BEGIN_(\d)_(\d)(.*?)PHONG_BENCH_END_\1_\2', log, re.S):
            for timer in ('Depth prepass', 'Main View 0'):
                values = [float(x) for x in re.findall(re.escape(timer) + r': ([\d.]+)ms', body)][15:-5]
                assert values, (sample, timer)
                timings[int(enabled)].setdefault(timer, []).append(statistics.median(values))
            wall = re.search(r'sunShadowBenchmark: (\d+) frames, mean ([\d.]+) ms, median ([\d.]+) ms, p95 ([\d.]+) ms', body)
            if wall:
                timings[int(enabled)].setdefault('Frame wall median', []).append(float(wall[3]))
        report['timings'] = timings
        report['depth_verify'] = re.findall(r'depth prepass verify: .*', log)
    (runtime / 'results.json').write_text(json.dumps(report, indent=2))
    print(f'PASS {label}: {counts}', flush=True)
    if report.get('timings'):
        print(json.dumps(report['timings'], indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--cinematic', action='store_true')
    parser.add_argument('--velocity', action='store_true')
    parser.add_argument('--bench', action='store_true')
    parser.add_argument('--gameplay', type=int, choices=(0, 1))
    parser.add_argument('--analyze-only', action='store_true', help='Recheck existing completed captures/logs')
    arguments = parser.parse_args()
    for mode in ((0,) if arguments.cinematic else ((arguments.gameplay,) if arguments.gameplay is not None else (0, 1))):
        run(arguments, mode)
