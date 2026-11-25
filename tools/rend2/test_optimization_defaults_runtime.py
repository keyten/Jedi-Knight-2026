"""Isolated opt-in optimization regression matrix (installed assets are read-only).

The shadow case verifies cube traversal/layered depth and static-cache depth,
then compares full/static caches to uncached rendering across draw/cull/LOD
changes with a same-scene depth oracle. The sky case compares skyportal culling,
including the conservative modern-alpha-caster bypass. Timings are tagged
non-isolated when another OpenJK process exists and never imply FPS speedup.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time
from test_depth_prepass_runtime import tga_pixels
from test_sunshadow_cache_runtime import running_openjk_pids

ROOT = Path(__file__).resolve().parents[2]
ERRORS = r'[^\r\n]*(?:GL_INVALID|GL_OUT_OF_MEMORY|FBO incomplete|GLSL shader compile error|GLSL shader link error)[^\r\n]*'


def commands(args):
    out = ['wait 120', 'devmap ' + args.map, 'wait 120', 'exitview', 'cam_disable',
           'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0',
           'con_notifytime -1', 'cg_bobup 0', 'cg_bobpitch 0', 'cg_bobroll 0',
           'cg_runpitch 0', 'cg_runroll 0', 'cg_thirdPerson 0', 'wait 20']
    if args.case == 'shadow':
        out += ['setviewpos 671 -769 -70 290']
        if not args.no_npcs:
            out += ['npc spawn reborn', 'npc spawn stormtrooper']
        out += ['wait 60', f'r_forwardPlusSpawnTestLights {args.lights} 96', 'wait 20', 'timescale 0',
                'r_shadowCache 0', 'r_shadowStaticCache 0',
                'r_shadowLayered 0', 'r_shadowVerify 1', 'wait 4',
                'r_shadowLayered 1', 'r_shadowVerify 1', 'wait 4']
        # A newly changed setting must invalidate a warmed slot/world bank.
        # timescale 0 is not a reliable SP freeze: same-scene depth is the
        # correctness oracle; image A/B only counts with a stable repeat.
        variants = [('baseline', []), ('worldoff', ['r_drawworld 0']),
                    ('worldon', ['r_drawworld 1']), ('curvesoff', ['r_nocurves 1']),
                    ('curveson', ['r_nocurves 0']), ('planeculloff', ['r_facePlaneCull 0']),
                    ('planecullon', ['r_facePlaneCull 1']), ('nocull', ['r_nocull 1']),
                    ('cull', ['r_nocull 0']), ('entitiesoff', ['r_drawentities 0']),
                    ('entitieson', ['r_drawentities 1']), ('lod', ['r_lodbias 2']),
                    ('lodrestore', ['r_lodbias 0']),
                    ('cameralod', ['r_shadowCasterLod 1', 'setviewpos 705 -750 -70 260']),
                    ('cameramove', ['setviewpos 650 -800 -70 310'])]
        previous = ['r_drawworld 1', 'r_nocurves 0', 'r_facePlaneCull 1',
                    'r_nocull 0', 'r_drawentities 1', 'r_lodbias 0',
                    'r_shadowCasterLod 0', 'setviewpos 671 -769 -70 290']
        for name, changes in variants:
            # Warm the OLD state, then change controls while caching stays on.
            # Otherwise a test can pass even with a missing invalidation key.
            out += previous + [f'r_shadowCache {args.full_cache}', 'r_shadowStaticCache 0', 'wait 8']
            out += changes + ['wait 30']
            if args.full_cache:
                out += ['r_shadowCache 2', 'wait 8']
            else:
                out += ['r_shadowLayered 0', 'r_shadowVerify 1', 'wait 4',
                        'r_shadowLayered 1', 'r_shadowVerify 1', 'wait 4']
            out += ['screenshot_tga full_' + name, 'wait 2']
            if args.static_cache:
                out += previous + ['r_shadowCache 0', 'r_shadowStaticCache 1', 'wait 8']
                out += changes + ['wait 30', 'r_shadowStaticCache 2', 'wait 8',
                                  'screenshot_tga static_' + name, 'wait 2']
            out += ['r_shadowCache 0', 'r_shadowStaticCache 0', 'wait 8',
                    'screenshot_tga reference_' + name, 'wait 2']
            previous += changes
        out += ['wait 4', 'screenshot_tga reference_repeat', 'wait 4']
        modes = [('legacy', ['r_shadowCache 0', 'r_shadowStaticCache 0',
                             'r_shadowSinglePass 0', 'r_shadowLayered 0']),
                 ('optimized', ['r_shadowSinglePass 1', 'r_shadowLayered 1',
                                f'r_shadowCache {args.full_cache}', f'r_shadowStaticCache {args.static_cache}'])]
    else:
        out += ['wait 30', 'timescale 0', 'r_skyPortalCull 0', 'wait 8',
                'screenshot_tga sky_legacy', 'r_skyPortalCull 1', 'wait 8',
                'screenshot_tga sky_optimized', 'r_skyPortalCull 0', 'wait 8',
                'screenshot_tga sky_repeat']
        modes = [('legacy', ['r_skyPortalCull 0']), ('optimized', ['r_skyPortalCull 1'])]
    # Timestamp timing is a diagnostic separate from the exact-depth checks.
    # Frame intervals run without r_speeds/shadowStats/readbacks.
    out += ['r_shadowVerify 0', 'r_sunShadowVerify 0', 'r_shadowStats 0',
            'com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
    for round_index in range(args.rounds):
        for name, settings in modes:
            label = name + '_' + str(round_index)
            out += settings + ['wait 30', 'echo AUDIT_GPU_BEGIN_' + label,
                               'r_speeds 100', 'wait ' + str(args.frames), 'r_speeds 0',
                               'echo AUDIT_GPU_END_' + label,
                               'wait 20', 'echo AUDIT_FRAME_BEGIN_' + label,
                               'r_sunShadowBenchmark ' + str(args.frames),
                               'wait ' + str(args.frames + 30),
                               'echo AUDIT_FRAME_END_' + label]
    return out + ['echo OPTIMIZATION_AUDIT_DONE', 'com_maxfps 60',
                  'com_maxfpsUnfocused 60', 'com_maxfpsMinimized 60', 'wait 30', 'quit']


def summarize(log, args, home):
    result = dict(case=args.case, map=args.map, gl43=args.gl43, sun_mode=args.sun_mode,
                  alpha_casters=args.alpha_casters, renderer=re.findall(r'GL_RENDERER:.*', log),
                  errors=re.findall(ERRORS, log),
                  cube_verification=re.findall(r'shadowVerify cube[^\r\n]+', log),
                  cache_verification=re.findall(r'(?:r_shadowStaticCache|point shadow cache) verify:[^\r\n]+', log),
                  gpu={}, frame={}, captures={})
    assert 'OPTIMIZATION_AUDIT_DONE' in log and not result['errors'], log[-4000:]
    assert not re.search(r'shadowVerify cube[^\r\n]*MISMATCH', log)
    result['verification_failures'] = [row for row in result['cache_verification']
                                       if not re.search(r', 0 of \d+ depth texels differ', row)]
    if args.case == 'shadow':
        assert result['cube_verification'], 'No cube depth check was exercised'
        if args.full_cache or args.static_cache:
            assert result['cache_verification'], 'No shadow cache depth check was exercised'
        names = ['baseline', 'worldoff', 'worldon', 'curvesoff', 'curveson',
                 'planeculloff', 'planecullon', 'nocull', 'cull', 'entitiesoff',
                 'entitieson', 'lod', 'lodrestore', 'cameralod', 'cameramove']
        expected = {'reference_' + name for name in names} | {'reference_repeat'}
        actual = {p.stem for p in (home/'screenshots').glob('reference_*.tga')}
        assert actual == expected, (actual, expected)
        for name in names:
            ref = tga_pixels(home/'screenshots'/('reference_' + name + '.tga'))
            for kind in (('full', 'static') if args.static_cache else ('full',)):
                pixels = tga_pixels(home/'screenshots'/(kind + '_' + name + '.tga'))
                result['captures'][kind + '_' + name] = dict(
                    equal=pixels == ref,
                    changed_pixels=sum(pixels[i:i+3] != ref[i:i+3] for i in range(0, len(ref), 3)))
        repeat = tga_pixels(home/'screenshots/reference_repeat.tga')
        last = tga_pixels(home/'screenshots/reference_cameramove.tga')
        result['repeat_equal'] = repeat == last
    else:
        a, b, repeat = [tga_pixels(home/('screenshots/' + name + '.tga'))
                        for name in ('sky_legacy', 'sky_optimized', 'sky_repeat')]
        result['repeat_equal'] = a == repeat
        result['captures']['sky'] = dict(equal=a == b,
            changed_pixels=sum(a[i:i+3] != b[i:i+3] for i in range(0, len(a), 3)))
    for match in re.finditer(r'AUDIT_GPU_BEGIN_(\w+)(.*?)AUDIT_GPU_END_\1', log, re.S):
        values = {}
        for name, number in re.findall(r'([A-Za-z][A-Za-z0-9+ _()/-]+): ([0-9.]+)ms', match[2]):
            values.setdefault(name.strip(), []).append(float(number))
        result['gpu'][match[1]] = {name: dict(n=len(v[8:-4]), median=statistics.median(v[8:-4]))
                                  for name, v in values.items() if len(v) > 12}
    for match in re.finditer(r'AUDIT_FRAME_BEGIN_(\w+)(.*?)AUDIT_FRAME_END_\1', log, re.S):
        result['frame'][match[1]] = re.findall(r'sunShadowBenchmark:[^\r\n]+', match[2], re.I)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--binaries', type=Path, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--cache-from', type=Path, help='Seed glslcache from another private home/OpenJK on the same GPU')
    p.add_argument('--case', choices=['shadow', 'sky'], default='shadow')
    p.add_argument('--map', default='mp/ffa3')
    p.add_argument('--gl43', type=int, choices=[0, 1], default=1)
    p.add_argument('--sun-mode', type=int, choices=[0, 1], default=0)
    p.add_argument('--alpha-casters', type=int, choices=[0, 1], default=1)
    p.add_argument('--nvidia', action='store_true')
    p.add_argument('--lazy-shaders', action='store_true', help='Compile scene-used lightall variants on demand')
    p.add_argument('--full-cache', type=int, choices=[0, 1], default=0,
                   help='Explicitly exercise opt-in full-slot reuse and its depth verifier')
    p.add_argument('--static-cache', type=int, choices=[0, 1], default=0,
                   help='Explicitly exercise the opt-in static split (known depth mismatch)')
    p.add_argument('--lights', type=int, choices=range(1, 5), default=4)
    p.add_argument('--no-npcs', action='store_true')
    p.add_argument('--frames', type=int, default=90)
    p.add_argument('--rounds', type=int, default=2)
    p.add_argument('--timeout', type=int, default=900)
    args = p.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9_-]+', args.label)
    runtime = ROOT/'build/optimization-audit'/args.label
    home = runtime/'home/OpenJK'
    assert not runtime.exists(), 'Use a fresh label to retain earlier evidence'
    home.mkdir(parents=True)
    if args.cache_from and (args.cache_from/'glslcache').is_dir():
        shutil.copytree(args.cache_from/'glslcache', home/'glslcache')
    for name in ('openjk_sp.x86_64.exe', 'rdsp-rend2_x86_64.dll', 'rend2_glslc.exe'):
        shutil.copy2(args.binaries/name, runtime/name)
    shutil.copy2(args.binaries/'jagamex86_64.dll', home/'jagamex86_64.dll')
    shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll', runtime/'SDL2.dll')
    settings = dict(cl_renderer='rdsp-rend2', r_mode=-1, r_customwidth=640, r_customheight=360,
        r_fullscreen=0, r_swapInterval=0, r_offscreenPresent=1, in_mouse=0, s_initsound=0,
        com_bootlogo=0, com_maxfps=60, com_maxfpsUnfocused=60, com_maxfpsMinimized=60,
        developer=0, logfile=2, r_picmip=2, r_hdr=1, r_autoExposure=0,
        r_shaderProgramCache=1, r_shaderCompileProcesses=0,
        r_shaderProgramLazy=int(args.lazy_shaders),
        r_gl43=args.gl43, r_sunShadowMode=args.sun_mode,
        r_sunShadowAlphaCasters=args.alpha_casters, r_sunShadowCache=0,
        r_dlightMode=2 if args.case == 'shadow' else 1,
        r_forwardPlus=1 if args.case == 'shadow' else 0, r_forwardPlusMaxShadowLights=4,
        r_sunlightMode=0 if args.case == 'shadow' else 1,
        r_shadowSinglePass=1, r_shadowLayered=1, r_shadowCache=0, r_shadowStaticCache=0,
        r_depthPrepass=1, r_depthPrepassFast=1, r_ext_multisample=0,
        r_ssr=0, r_ssgi=0, r_smaa=0, r_motionBlur=0, r_ltcAreaLights=0,
        r_cubeMapping=0, r_normalMapping=1, r_specularMapping=1,
        r_weatherWetness=0, r_waterSurface=0, r_volumetricFog=0, r_volumetricWater=0,
        r_atmosphere=0, r_clouds=0, r_ssao=0, r_aoMode=0, r_contactShadows=0,
        r_bloom=0, r_skinSSS=0, r_autoGrass=0, r_rainStreaks=0,
        r_rainSplashes=0, r_rainLens=0, r_waterParticles=0)
    (home/'openjk_sp.cfg').write_text(''.join('seta %s "%s"\n' % item for item in settings.items()))
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands(args))+'\n')
    command = [str(runtime/'openjk_sp.x86_64.exe'), '+set', 'fs_basepath', str(args.installation.resolve()),
               '+set', 'fs_homepath', str(runtime/'home'), '+set', 'fs_game', 'OpenJK']
    environment = os.environ.copy()
    if args.nvidia:
        environment['SHIM_MCCOMPAT'] = '0x800000001'
    else:
        environment.pop('SHIM_MCCOMPAT', None)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    foreign = running_openjk_pids()
    with (runtime/'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=runtime, env=environment, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        print('Launched %d in %s; foreign games %s' % (process.pid, runtime, sorted(foreign)), flush=True)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            foreign.update(running_openjk_pids() - {process.pid})
            time.sleep(1)
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
            raise RuntimeError('Timed out: ' + str(home/'qconsole.log'))
    log = (home/'qconsole.log').read_text(errors='replace')
    assert process.returncode == 0, log[-4000:]
    result = summarize(log, args, home)
    result.update(settings=settings, foreign_game_pids=sorted(foreign), performance_isolated=not foreign)
    (runtime/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2), flush=True)
    assert not result['verification_failures'], result['verification_failures'][:3]
    # A single stable repeat at the END cannot certify all earlier camera/LOD
    # states of a running SP simulation. Shadow correctness is the exact
    # same-scene depth oracle; its beauty captures remain diagnostic.
    if args.case == 'sky' and result['repeat_equal']:
        assert result['captures']['sky']['equal'], 'Sky image mismatch with stable reference'
    print('PASS depth/runtime checks; shadow images diagnostic; timing isolation:', not foreign)


if __name__ == '__main__':
    main()
