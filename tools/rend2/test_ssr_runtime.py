"""Isolated Windows SP SSR smoke test and per-pass GPU timing capture.

Reads installed assets, writes only under build/ssr-runtime/<label>. Requires
an explicit installation; never replaces the installed engine or renderer.
"""
import argparse
import hashlib
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
    result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True)
    return [line for line in result.stdout.splitlines()
            if re.match(r'"openjk(?:_sp)?\.x86_64\.exe"', line, re.I)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--nvidia', action='store_true')
    parser.add_argument('--temporal', type=int, choices=(0, 1), default=0)
    parser.add_argument('--half-res', type=int, choices=(0, 1), default=1)
    parser.add_argument('--msaa', type=int, choices=(0, 4), default=0)
    parser.add_argument('--resolve-cache', type=int, choices=(0, 1), default=1)
    parser.add_argument('--rough-occlusion', type=int, choices=(0, 1), default=1)
    parser.add_argument('--bench-frames', type=int, default=90,
                        help='Engine wait ticks per measured section (positive, not wall-clock frames)')
    parser.add_argument('--bench-warmup', type=int, default=25,
                        help='Engine wait ticks before each measured section')
    parser.add_argument('--compare-resolve', action='store_true')
    parser.add_argument('--smoke-only', action='store_true', help='Skip GPU timing sections')
    parser.add_argument('--check-no-prepass-cache', action='store_true',
                        help='Assert that the debug view has no reused hits without velocity')
    parser.add_argument('--allow-overlap', action='store_true',
                        help='Allow functional checks alongside other games; timings are not usable')
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--wait-for-idle', type=int, default=0,
                        help='Wait this many seconds for other game runs to finish')
    args = parser.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9_-]+', args.label)
    assert args.bench_frames >= 40 and args.bench_warmup >= 0
    idle_since = None
    waiting_since = time.monotonic()
    while True:
        now = time.monotonic()
        if args.allow_overlap:
            break
        if game_processes():
            idle_since = None
            if not args.wait_for_idle or now - waiting_since > args.wait_for_idle:
                raise RuntimeError('Another game is running; avoid overlapping GPU measurements')
        else:
            if not args.wait_for_idle:
                break
            idle_since = idle_since or now
            if now - idle_since >= 10:
                break
        if int(now - waiting_since) % 30 < 2:
            print('Waiting for an idle GPU/game slot...', flush=True)
        time.sleep(2)
    build = ROOT / 'build/ssr-msvc/RelWithDebInfo'
    runtime = ROOT / 'build/ssr-runtime' / args.label
    home = runtime / 'home/OpenJK'
    home.mkdir(parents=True, exist_ok=True)
    for name in ('openjk_sp.x86_64.exe', 'rend2_glslc.exe'):
        shutil.copy2(build / name, runtime / name)
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    shutil.copy2(args.renderer, runtime / 'rdsp-rend2_x86_64.dll')
    shutil.copy2(build / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    cfg = ['wait 180', 'devmap mp/ffa3', 'wait 100', 'exitview',
        'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0',
        'r_drawentities 0', 'con_notifytime 0', 'setviewpos 671 -769 -70 290',
        'wait 80', 'screenshot_tga beauty', 'r_ssrDebug 12', 'wait 20',
        'screenshot_tga cache', 'r_ssrDebug 4', 'wait 10', 'screenshot_tga hits',
        'r_ssrDebug 0']
    if args.compare_resolve:
        cfg += ['r_ssrResolveCache 0', 'wait 30', 'screenshot_tga beauty_direct',
                'r_ssrResolveCache 1', 'wait 30', 'screenshot_tga beauty_cached',
                'r_ssrBlendStrength 0', 'wait 30', 'screenshot_tga cubemap',
                'r_ssrBlendStrength 1', f'r_ssrResolveCache {args.resolve_cache}', 'wait 30']
    if not args.smoke_only:
        cfg += ['com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0',
                'echo SSR_BENCH_FPS_CAPS', 'com_maxfps', 'com_maxfpsUnfocused', 'com_maxfpsMinimized']
    modes = [(1, 1, args.resolve_cache), (0, 1, args.resolve_cache), (1, 0, args.resolve_cache)]
    if args.compare_resolve:
        modes.append((1, 1, 1 - args.resolve_cache))
    if args.smoke_only:
        modes = []
    for yaw in (290, 310):
        cfg += [f'setviewpos 671 -769 -70 {yaw}', 'wait 40']
        for cache, cull, resolve in modes:
            label = f'y{yaw}_cache{cache}_cull{cull}_resolve{resolve}'
            cfg += [f'r_ssrHitCache {cache}', f'r_ssrReceiverCull {cull}',
                f'r_ssrResolveCache {resolve}',
                f'wait {args.bench_warmup}', f'echo SSR_BENCH_BEGIN_{label}', 'r_speeds 100',
                f'wait {args.bench_frames}', 'r_speeds 0', f'echo SSR_BENCH_END_{label}']
    if args.compare_resolve:
        # Late A/B/A captures separate the spatial resolve approximation
        # from map-start streaming, hit reuse and GPU warm-up.
        cfg += ['com_maxfps 60', 'com_maxfpsUnfocused 60', 'com_maxfpsMinimized 60',
                'setviewpos 671 -769 -70 290', 'r_ssrHitCache 0',
                'r_ssrReceiverCull 1', 'wait 120',
                'r_ssrResolveCache 0', 'wait 120', 'screenshot_tga direct_late',
                'r_ssrResolveCache 1', 'wait 120', 'screenshot_tga cached_late',
                'r_ssrResolveCache 0', 'wait 120', 'screenshot_tga direct_repeat',
                f'r_ssrResolveCache {args.resolve_cache}']
    # Exercise camera reprojection and a frame history cut as well as static reuse.
    cfg += ['r_ssrHitCache 1', 'r_ssrReceiverCull 1', '+right', 'wait 24',
        '-right', 'wait 20', 'screenshot_tga after_pan',
        'setviewpos 671 -769 -70 110', 'wait 10',
        'setviewpos 671 -769 -70 290', 'wait 20',
        'screenshot_tga after_cut',
        # Saber mode 2 independently forces a velocity/depth prepass.
        'r_ltcSaberShadows 0', 'r_depthPrepass 0', 'r_ssrDebug 12',
        'wait 20', 'screenshot_tga no_prepass',
        'wait 5', 'r_depthPrepass 1', 'echo SSR_RUNTIME_DONE', 'wait 5', 'quit']
    settings = dict(fs_basepath=str(args.installation.resolve()),
        fs_homepath=str(runtime / 'home'), fs_game='OpenJK', cl_renderer='rdsp-rend2',
        r_mode='-1', r_customwidth=str(args.width), r_customheight=str(args.height), r_fullscreen='0',
        # Smoke checks must outlive the 0.75 s real-time level dissolve, even
        # on a GPU rendering thousands of tiny frames per second.
        r_swapInterval='0', com_maxfps='60', com_maxfpsUnfocused='60', com_maxfpsMinimized='60',
        com_bootlogo='0', in_mouse='0',
        s_initsound='0', logfile='2', developer='0', r_hdr='1', r_autoExposure='0',
        r_ssr='1', r_ssrHalfRes=str(args.half_res), r_ssrTemporal=str(args.temporal),
        r_ssrQuality='1', r_ssrHiZ='1', r_ssrEmitters='0', r_ssrHitCache='1',
        r_ssrReceiverCull='1', r_ssgi='0', r_smaa='0', r_motionBlur='0',
        r_depthPrepass='1', r_ext_multisample=str(args.msaa),
        r_ssrResolveCache=str(args.resolve_cache),
        r_ssrRoughOcclusion=str(args.rough_occlusion),
        r_cubeMapping='1', r_normalMapping='1', r_specularMapping='1',
        r_autoPBR='1', r_weatherWetness='0', r_waterSurface='0',
        r_volumetricFog='0', r_volumetricWater='0', r_atmosphere='0', r_clouds='0',
        r_ssao='0', r_aoMode='0', r_contactShadows='0', r_sunlightMode='0',
        r_ltcAreaLights='0', r_forwardPlus='0', r_shaderProgramCache='1',
        r_shaderCompileProcesses='0', r_gl43='1')
    # Com_ParseCommandLine accepts only 32 '+' commands. Set the render
    # configuration before the first wait in autoexec, before renderer init.
    config_settings = [f'set {key} "{value}"' for key, value in settings.items()
                       if key not in ('fs_basepath', 'fs_homepath', 'fs_game')]
    config_settings += ['echo SSR_SETTINGS_BEGIN', 'r_ssr', 'r_ssrTemporal',
        'r_ssrHalfRes', 'r_ssgi', 'r_smaa', 'r_motionBlur', 'r_ext_multisample',
        'r_ssrResolveCache', 'r_ssrRoughOcclusion', 'echo SSR_SETTINGS_END']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(config_settings + cfg) + '\n', encoding='ascii')
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
    overlap = bool(game_processes())
    with (runtime / 'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=runtime, env=env, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        print(f'Launched PID {process.pid}: {runtime}', flush=True)
        started = time.monotonic()
        while process.poll() is None:
            if time.monotonic() - started > args.timeout:
                process.terminate()
                process.wait(timeout=20)
                raise TimeoutError(runtime)
            processes = game_processes()
            overlap |= len(processes) > 1
            time.sleep(2)
    metadata = dict(source_revision=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        renderer_path=str(args.renderer.resolve()),
        renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),
        bench_wait_ticks=args.bench_frames, bench_warmup_ticks=args.bench_warmup,
        benchmark_fps_caps=None if args.smoke_only else dict(
            com_maxfps=0, com_maxfpsUnfocused=0, com_maxfpsMinimized=0))
    (runtime / 'execution.json').write_text(json.dumps(dict(
        returncode=process.returncode, overlap=overlap, settings=settings, **metadata), indent=2))
    log = (home / 'qconsole.log').read_text(errors='replace')
    assert process.returncode == 0 and 'SSR_RUNTIME_DONE' in log, log[-5000:]
    assert not re.search(r'GL_INVALID|GLSL shader compile error|GLSL shader link error|FBO.*incomplete', log), log[-5000:]
    for name in ('beauty', 'cache', 'hits', 'after_pan', 'after_cut', 'no_prepass'):
        assert (home / f'screenshots/{name}.tga').stat().st_size > 1000, name
    # Only the fixed renderer promises no reuse without a valid prepass.
    # The baseline run remains useful for measuring/reproducing old behavior.
    def tga_pixels(name):
        data = (home / f'screenshots/{name}.tga').read_bytes()
        assert data[2] == 2 and data[16] == 24
        width, height = struct.unpack_from('<HH', data, 12)
        pixels = data[18 + data[0]:]
        # The engine's TGA writer can retain GL_PACK_ALIGNMENT row padding
        # when the desktop fits an odd-width requested window.
        if len(pixels) != width * height * 3:
            stride = (width * 3 + 3) & ~3
            assert len(pixels) == stride * height
            pixels = b''.join(pixels[y*stride:y*stride+width*3] for y in range(height))
        return pixels

    def debug_pixels(name):
        pixels = tga_pixels(name)
        green = red = 0
        for b, g, r in zip(pixels[::3], pixels[1::3], pixels[2::3]):
            green += g > r + 20 and g > b + 20
            red += r > g + 20 and r > b + 20
        return dict(reused=green, fresh=red)

    cache_pixels = debug_pixels('cache')
    assert sum(cache_pixels.values()) > 100, 'No SSR hits: inspect screenshots before accepting this run'
    green = debug_pixels('no_prepass')['reused']
    if args.check_no_prepass_cache:
        assert green == 0, f'Hit cache reused {green} pixels without a velocity prepass'
    results = {'overlap': overlap, 'settings': settings,
               'renderer': re.findall(r'GL_RENDERER:.*', log),
               'mode': re.findall(r'MODE: .*', log)[-1:],
               'cache_pixels': cache_pixels,
               'no_prepass_green_pixels': green, 'sections': {}, **metadata}
    if args.compare_resolve:
        direct, cached, repeat = [tga_pixels(name) for name in
                                  ('direct_late', 'cached_late', 'direct_repeat')]
        assert len(direct) == len(cached) == len(repeat)
        results['resolve_quality'] = dict(direct_repeat_identical=direct == repeat,
            cached_vs_direct_mean_absolute_8bit=sum(abs(a-b) for a, b in zip(direct, cached))/len(direct),
            direct_repeat_mean_absolute_8bit=sum(abs(a-b) for a, b in zip(direct, repeat))/len(direct))
    for label, section in re.findall(r'SSR_BENCH_BEGIN_(\w+)(.*?)SSR_BENCH_END_\1', log, re.S):
        passes = {}
        for name in ('Main View 0', 'Screen geometry', 'SSR inputs', 'SSR trace', 'SSR resolve',
                     'SSR temporal', 'SSR composite', 'Post processing'):
            values = [float(v) for v in re.findall(re.escape(name) + r': ([0-9.]+)ms', section)][8:-4]
            if values:
                passes[name] = dict(n=len(values), median=statistics.median(values),
                    mean=statistics.mean(values), p95=sorted(values)[int(.95 * (len(values)-1))])
        results['sections'][label] = passes
    assert len(results['sections']) == 2 * len(modes), results
    (runtime / 'results.json').write_text(json.dumps(results, indent=2))
    for label, passes in results['sections'].items():
        print(label, {name: round(value['median'], 3) for name, value in passes.items()}, flush=True)
    print('PASS: SSR runtime and captures; GPU overlap:', overlap, flush=True)
    if overlap and not args.allow_overlap:
        raise RuntimeError('Another game overlapped this run; do not use its timing results')


if __name__ == '__main__':
    main()
