"""Water SSR reflected fog (r_ssrReflectedFog) in game: GPU timings of the water passes with the fog off / on.

Hidden SP runs on t3_hevil (outdoor lake) at minimal settings: 640x360, r_picmip 2, only the water surface SSR
and the froxel height fog (r_volumetricFog 2, quality 0). r_ssrReflectedFog is latched, so every round launches
the game once per setting (off, on, off, on, ...). Reads the installed assets, writes only under
build/water-ssr-fog-runtime/<label>; never touches the installation. Refuses to time while another game runs.
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

ROOT = Path(__file__).resolve().parents[2]
PASSES = ('Main View 0', 'Water surface', 'Water SSR prepass', 'Water shading', 'Froxel fog inject')
# above the lake looking down at it, and at the waterline (test_watersurface_runtime.py t3_hevil)
VIEWS = {'high': ['setviewpos 512 1152 -60 90', '+lookdown', 'wait 6', '-lookdown'],
         'low': ['setviewpos 512 1152 -120 90']}


def game_processes():
    result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True)
    return [line for line in result.stdout.splitlines() if re.match(r'"openjk(?:_sp)?\.x86_64\.exe"', line, re.I)]


def wait_idle(seconds):
    start = time.monotonic()
    while game_processes():
        if time.monotonic() - start > seconds:
            raise RuntimeError('Another game is running; avoid overlapping GPU measurements')
        time.sleep(5)


def run(args, fog, round_index):
    tag = f'{"nv" if args.nvidia else "intel"}_fog{fog}'
    runtime = ROOT / 'build/water-ssr-fog-runtime' / args.label / tag
    home = runtime / 'home/OpenJK'
    home.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.engine / 'openjk_sp.x86_64.exe', runtime / 'openjk_sp.x86_64.exe')
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    shutil.copy2(args.renderer, runtime / 'rdsp-rend2_x86_64.dll')
    shutil.copy2(args.engine / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    # archived cvars of an earlier run would carry over (latched ones too)
    (home / 'openjk_sp.cfg').unlink(missing_ok=True)
    settings = dict(cl_renderer='rdsp-rend2', r_mode='-1', r_customwidth='640', r_customheight='360',
        r_fullscreen='0', r_swapInterval='0', com_maxfps='60', com_bootlogo='0', in_mouse='0', s_initsound='0',
        logfile='2', developer='0', r_offscreenPresent='1', r_picmip='2', r_hdr='1', r_autoExposure='0',
        r_smaa='0', r_motionBlur='0', r_bloom='0', r_ssr='0', r_ssgi='0', r_ssao='0', r_aoMode='0',
        r_contactShadows='0', r_sunlightMode='0', r_ltcAreaLights='0', r_forwardPlus='0', r_atmosphere='0',
        r_clouds='0', r_weatherWetness='0', r_autoGrass='0', r_foliageWind='0', r_skinSSS='0',
        r_pomSilhouette='0', r_waterParticles='0', r_waterfall='0', r_volumetricWater='0',
        r_waterSurface='1', r_waterSurfaceSSR='1', r_ssrTemporal='0', r_ssrQuality='1', r_ssrHiZ='1',
        r_volumetricFog='2', r_volumetricFogQuality='0', r_volumetricFogHeight='1',
        r_volumetricFogHeightOpaqueDistance='800', r_ssrReflectedFog=str(fog), r_shaderProgramCache='1',
        r_gl43='1')
    for item in args.set:
        key, value = item.split('=', 1)
        settings[key] = value
    cfg = [f'set {key} "{value}"' for key, value in settings.items()]
    # the loading screen updates consume wait ticks: wait well past the load (60 fps cap until the bench)
    cfg += ['wait 180', 'devmap t3_hevil', f'wait {args.load_wait}', 'exitview', 'god', 'notarget', 'noclip',
            'cg_draw2D 0', 'cg_drawGun 0', 'r_drawentities 0', 'con_notifytime 0', 'wait 120', 'com_maxfps 0',
            'r_ssrReflectedFog']
    for view, commands in VIEWS.items():
        cfg += commands + ['wait 60', f'screenshot_tga {view}_r{round_index}',
                           f'echo FOG_BENCH_BEGIN_{view}', 'r_speeds 100', f'wait {args.frames}', 'r_speeds 0',
                           f'echo FOG_BENCH_END_{view}']
    cfg += ['echo FOG_RUNTIME_DONE', 'wait 5', 'quit']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(cfg) + '\n', encoding='ascii')
    (home / 'qconsole.log').unlink(missing_ok=True)
    command = [str(runtime / 'openjk_sp.x86_64.exe'), '+set', 'fs_basepath', str(args.installation.resolve()),
               '+set', 'fs_homepath', str(runtime / 'home'), '+set', 'fs_game', 'OpenJK']
    env = os.environ.copy()
    if args.nvidia:
        env['SHIM_MCCOMPAT'] = '0x800000001'
    else:
        env.pop('SHIM_MCCOMPAT', None)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    wait_idle(args.wait_for_idle)
    overlap = False
    with (runtime / 'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=runtime, env=env, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        started = time.monotonic()
        while process.poll() is None:
            if time.monotonic() - started > args.timeout:
                process.terminate()
                process.wait(timeout=20)
                raise TimeoutError(runtime)
            overlap |= len(game_processes()) > 1
            time.sleep(2)
    log = (home / 'qconsole.log').read_text(errors='replace')
    assert process.returncode == 0 and 'FOG_RUNTIME_DONE' in log, log[-4000:]
    assert not re.search(r'GLSL shader compile error|GLSL shader link error|GL_INVALID', log), log[-4000:]
    result = dict(overlap=overlap, renderer=re.findall(r'GL_RENDERER: (.*)', log)[:1],
                  latched=re.findall(r'r_ssrReflectedFog = \^9"\^7(\d)', log)[-1:], views={})
    for view, section in re.findall(r'FOG_BENCH_BEGIN_(\w+)(.*?)FOG_BENCH_END_\1', log, re.S):
        passes = {}
        for name in PASSES:
            values = [float(v) for v in re.findall(re.escape(name) + r': ([0-9.]+)ms', section)][8:-4]
            if values:
                passes[name] = dict(n=len(values), median=statistics.median(values))
        result['views'][view] = passes
    assert all(result['views'].get(view) for view in VIEWS), f'no GPU timings: {runtime}'
    for view in VIEWS:
        assert (home / f'screenshots/{view}_r{round_index}.tga').exists(), f'no screenshot: {runtime}'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True, help='directory with openjk_sp.x86_64.exe + jagame')
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--nvidia', action='store_true')
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--frames', type=int, default=90)
    parser.add_argument('--load-wait', type=int, default=1500, help='wait ticks after devmap')
    parser.add_argument('--timeout', type=int, default=1200)
    parser.add_argument('--wait-for-idle', type=int, default=1800)
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE')
    args = parser.parse_args()
    results = []
    for round_index in range(args.rounds):
        for fog in (0, 1):
            r = run(args, fog, round_index)
            r.update(fog=fog, round=round_index)
            results.append(r)
            print(json.dumps({k: r[k] for k in ('fog', 'round', 'overlap', 'renderer', 'latched')}),
                  {v: {n: round(p['median'], 3) for n, p in ps.items()} for v, ps in r['views'].items()}, flush=True)
    out = ROOT / 'build/water-ssr-fog-runtime' / args.label / f'results_{"nv" if args.nvidia else "intel"}.json'
    out.write_text(json.dumps(results, indent=2))
    print('written', out)


if __name__ == '__main__':
    main()
