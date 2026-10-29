"""Isolated Windows SP integration check for static sun-shadow caching.

Uses read-only installed assets and locally built engine/game/renderer binaries.
Each mode gets a private home; repeating mode 3 exercises its on-disk pages.
Verification readbacks are disabled during the GPU/CPU timing windows.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def running_openjk_pids():
    """Read-only process inventory; never suspend/terminate another test."""
    class ProcessEntry(ctypes.Structure):
        _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                    ('th32ProcessID', wintypes.DWORD), ('th32DefaultHeapID', ctypes.c_size_t),
                    ('th32ModuleID', wintypes.DWORD), ('cntThreads', wintypes.DWORD),
                    ('th32ParentProcessID', wintypes.DWORD), ('pcPriClassBase', wintypes.LONG),
                    ('dwFlags', wintypes.DWORD), ('szExeFile', wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    found = set()
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        present = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while present:
            if entry.szExeFile.lower().startswith('openjk') and entry.szExeFile.lower().endswith('.exe'):
                found.add(entry.th32ProcessID)
            present = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    return found


def commands(map_name, mode, verify, benchmark_frames):
    origin = (1024, -3900, 256) if map_name == 'yavin1' else (1752, 800, 96)
    x, y, z = origin
    result = ['wait 90', 'cam_disable', 'exitview', 'wait 10', 'noclip', 'god',
              'give weapons', 'wait 90', 'weapon 5', 'wait 90',
              'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0', 'con_notifytime 0',
              'cg_bobup 0', 'cg_bobpitch 0', 'cg_bobroll 0', 'cg_runpitch 0', 'cg_runroll 0',
              'r_forceSun 1', 'setviewpos %d %d %d 90' % origin, 'wait 180',
              'echo SUN_STATIONARY_BEGIN', 'r_shadowStats 1', 'r_speeds 100',
              'wait 120', 'r_speeds 0', 'r_shadowStats 0',
              'echo SUN_STATIONARY_END', 'screenshot_tga sun-stationary', 'wait 12']
    if verify and mode:
        result += ['r_sunShadowVerify 2', 'wait 30', 'r_sunShadowVerify 0']
    # Subtexel/small integer steps exercise hits and all scroll directions.
    for index, (dx, dy) in enumerate(((1, 0), (2, 0), (2, 2), (-2, 2),
                                     (-2, -2), (0, 0), (300, 300), (0, 0))):
        result += ['echo SUN_MOVE_%d' % index,
                   'setviewpos %d %d %d 90' % (x + dx, y + dy, z)]
        if verify and mode:
            result += ['r_sunShadowVerify 2', 'wait 16', 'r_sunShadowVerify 0']
        else:
            result += ['wait 4']
    result += ['screenshot_tga sun-after-scroll', 'wait 12',
               'echo SUN_MOVING_BEGIN', 'r_shadowStats 1', 'r_speeds 100',
               '+moveright', 'wait 120', '-moveright',
               'r_speeds 0', 'r_shadowStats 0', 'echo SUN_MOVING_END',
               'screenshot_tga sun-moving', 'wait 12']
    if verify and mode:
        result += ['r_sunShadowVerify 1', 'wait 16',
                   'setviewpos %d %d %d 0' % (x + 10000, y + 10000, z),
                   'r_sunShadowVerify 2', 'wait 16', 'r_sunShadowVerify 0',
                   'setviewpos %d %d %d 90' % origin,
                   'r_sunShadowVerify 2', 'wait 16', 'r_sunShadowVerify 0']
        for setting in ('r_drawentities 0', 'r_drawentities 1',
                        'r_drawworld 0', 'r_drawworld 1', 'r_nocurves 1',
                        'r_nocurves 0', 'r_facePlaneCull 0', 'r_facePlaneCull 1'):
            result += [setting, 'r_sunShadowVerify 2', 'wait 16', 'r_sunShadowVerify 0']
    if benchmark_frames:
        result += ['r_sunShadowVerify 0', 'r_speeds 0', 'r_shadowStats 0',
                   'setviewpos %d %d %d 90' % origin, 'wait 180',
                   'echo SUN_FPS_STATIONARY_BEGIN',
                   'r_sunShadowBenchmark %d' % benchmark_frames,
                   'wait %d' % (benchmark_frames * 2 + 60),
                   'echo SUN_FPS_STATIONARY_END',
                   'setviewpos %d %d %d 90' % origin, 'wait 180',
                   'echo SUN_FPS_MOVING_BEGIN',
                   'r_sunShadowBenchmark %d' % benchmark_frames]
        # Oscillate through the same small area instead of eventually flying
        # out of the map; these timings include the game and buffer swapping.
        for _ in range((benchmark_frames * 2 + 120) // 120 + 1):
            result += ['+moveright', 'wait 60', '-moveright',
                       '+moveleft', 'wait 60', '-moveleft']
        result += ['echo SUN_FPS_MOVING_END']
    result += ['echo SUN_RUNTIME_DONE', 'wait 8', 'quit']
    return result


def summarize(log):
    verification = re.findall(r'sunShadowVerify cascade (\d): (PASS|MISMATCH), (\d+)/(\d+) differ, (\d+) above 2 D24 steps, max ([^\s,]+), (\d+) outside one-texel footprint', log)
    summary = dict(verification_count=len(verification),
                   verification_failures=[list(row) for row in verification if row[1] != 'PASS'],
                   exact_comparisons=sum(int(row[2]) == 0 for row in verification),
                   max_depth_error=max((float(row[5]) for row in verification), default=0.0),
                   disk_windows_loaded=len(re.findall(r'Sun shadow disk: loaded \d+ pages', log)),
                   gpu={}, cpu={}, frame={}, cache=re.findall(r'sun static cache: ([^\r\n]+)', log),
                   restore=re.findall(r'sun working restore: ([^\r\n]+)', log),
                   disk=re.findall(r'sun disk pages: ([^\r\n]+)', log))
    for label in ('STATIONARY', 'MOVING'):
        frame_region = re.search(r'SUN_FPS_%s_BEGIN(.*?)SUN_FPS_%s_END' % (label, label), log, re.S)
        if frame_region:
            sample = re.search(r'sunShadowBenchmark: (\d+) frames, mean ([\d.]+) ms, median ([\d.]+) ms, p95 ([\d.]+) ms, ([\d.]+) FPS', frame_region.group(1))
            if sample:
                summary['frame'][label.lower()] = dict(samples=int(sample[1]),
                    mean_ms=float(sample[2]), median_ms=float(sample[3]),
                    p95_ms=float(sample[4]), fps=float(sample[5]))
        region = re.search(r'SUN_%s_BEGIN(.*?)SUN_%s_END' % (label, label), log, re.S)
        if not region:
            continue
        block = region.group(1)
        frames, sun = [], 0.0
        pattern = r'(Sunshadow Cascade|Sun static shadow build|Sun dynamic shadow overlay|Main View) \d+: ([\d.]+)ms'
        for name, value in re.findall(pattern, block):
            if name == 'Main View':
                if sun:
                    frames.append(sun)
                sun = 0.0
            else:
                sun += float(value)
        if frames:
            summary['gpu'][label.lower()] = dict(samples=len(frames),
                sun_median_ms=statistics.median(frames), sun_min_ms=min(frames), sun_max_ms=max(frames))
        cpu = re.findall(r'\s+(sun cascade|sun static|sun dynamic)\s+[\d.]+\s+([\d.]+)\s+([\d.]+)', block)
        summary['cpu'][label.lower()] = [dict(kind=k, front_ms=float(f), submit_ms=float(s)) for k, f, s in cpu]
    return summary


def run_case(args, mode, run_index):
    repo = Path(__file__).resolve().parents[2]
    root = (args.output or repo/'build/sun-shadow-runtime').resolve()
    runtime = root/('mode%d' % mode)
    home = runtime/'home'
    base = home/'OpenJK'
    base.mkdir(parents=True, exist_ok=True)
    for name in ('openjk_sp.x86_64.exe', 'rdsp-rend2_x86_64.dll'):
        shutil.copy2(args.binaries/name, runtime/name)
    shutil.copy2(args.binaries/'jagamex86_64.dll', base/'jagamex86_64.dll')
    shutil.copy2(repo/'lib/SDL2/bin/x64/SDL2.dll', runtime/'SDL2.dll')
    (base/'sun-test.cfg').write_text('\n'.join(commands(args.map, mode, not args.no_verify, args.benchmark_frames))+'\n', encoding='ascii')
    (base/'autoexec_sp.cfg').write_text('wait 120\ndevmap %s\nwait 60\nexec sun-test.cfg\n' % args.map, encoding='ascii')
    log_path = base/'qconsole.log'
    if log_path.exists():
        shutil.move(str(log_path), str(runtime/('previous-%d.log' % time.time_ns())))
    settings = dict(fs_basepath=str(args.installation), fs_homepath=str(home), fs_game='OpenJK',
        cl_renderer='rdsp-rend2', r_fullscreen='0', r_mode='-1', r_customwidth=str(args.window[0]), r_customheight=str(args.window[1]),
        r_swapInterval='0', s_initsound='0', r_sunlightMode='1', r_sunShadows='1', r_sunShadowMode='1',
        r_shadowMapSize=str(args.shadow_size), r_sunShadowCache=str(mode), r_shadowPCSS='1',
        r_depthPrepass='1', r_forceSun='1', r_cubeMapping='0', r_diffuseIBL='0', r_ssr='0', r_ssgi='0',
        r_volumetricFog='0', r_clouds='0', r_atmosphere='0', r_postProcess='1',
        r_normalMapping='1', r_specularMapping='1', r_hdr='1', r_contactShadows='0',
        r_dynamiclight='0', r_shaderProgramCache='1', r_sunShadowVerify='0', r_shadowStats='1',
        developer='0', logfile='2', com_maxfps=str(args.maxfps),
        com_maxfpsMinimized=str(args.maxfps), com_maxfpsUnfocused=str(args.maxfps))
    # The SP engine accepts at most 32 startup command lines. Passing every
    # setting as +set silently concatenates the remainder into one cvar.
    # A private config is read before renderer initialization and has no such
    # limit; only filesystem selection must remain on the command line.
    (base/'openjk_sp.cfg').write_text(''.join('seta %s "%s"\n' % (key, value.replace('\\', '/'))
        for key, value in settings.items()), encoding='ascii')
    command = [str(runtime/'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        value = settings[key]
        command += ['+set', key, value]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    print('Launching mode %d, map %s, private home %s' % (mode, args.map, home), flush=True)
    foreign_games = running_openjk_pids()
    with (runtime/'launch.log').open('w') as output:
        process = subprocess.Popen(command, cwd=runtime, startupinfo=startup,
                                   stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            foreign_games.update(running_openjk_pids() - {process.pid})
            time.sleep(0.5)
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
            raise RuntimeError('Timed out; private log: %s' % log_path)
    log = log_path.read_text(errors='replace') if log_path.exists() else (runtime/'launch.log').read_text(errors='replace')
    archived = runtime/('run-%d.log' % run_index)
    archived.write_text(log, encoding='utf-8')
    summary = summarize(log)
    summary.update(mode=mode, exit_code=process.returncode, map=args.map,
                   foreign_game_pids=sorted(foreign_games), performance_isolated=not foreign_games,
                   completed='SUN_RUNTIME_DONE' in log, log=str(archived),
                   errors=re.findall(r'[^\r\n]*(?:GL_INVALID|GL_OUT_OF_MEMORY|FBO incomplete|GLSL compile.*failed|GLSL link.*failed)[^\r\n]*', log))
    (runtime/('run-%d.json' % run_index)).write_text(json.dumps(summary, indent=2), encoding='utf-8')
    printed = dict(summary)
    printed['verification_failures'] = summary['verification_failures'][:3]
    printed['failure_count'] = len(summary['verification_failures'])
    printed['cache'] = summary['cache'][-3:]
    print(json.dumps(printed, indent=2), flush=True)
    if process.returncode or not summary['completed'] or summary['errors']:
        raise RuntimeError('Runtime failure: %s\n%s' % (archived, log[-3000:]))
    if mode and not args.no_verify and not summary['verification_count']:
        raise RuntimeError('No cached sun cascades verified; check sun setup: %s' % archived)
    if args.benchmark_frames and len(summary['frame']) != 2:
        raise RuntimeError('Full-frame benchmarks did not complete: %s' % archived)
    return summary


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, default=repo/'build/sun-shadow-cache/Release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--modes', type=int, nargs='+', choices=range(4), default=[0, 1, 2, 3, 3])
    parser.add_argument('--map', default='mp/ffa3')
    parser.add_argument('--shadow-size', type=int, default=1024)
    parser.add_argument('--timeout', type=int, default=420)
    parser.add_argument('--no-verify', action='store_true')
    parser.add_argument('--maxfps', type=int, default=0)
    # Correctness-only runs next to other game instances: e.g. --window 480 270 --maxfps 60
    parser.add_argument('--window', type=int, nargs=2, default=[960, 540], metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--benchmark-frames', type=int, default=0)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows runtime required')
    summaries = [run_case(args, mode, index) for index, mode in enumerate(args.modes)]
    failures = sum(len(s['verification_failures']) for s in summaries)
    if failures:
        raise RuntimeError('%d cached cascade comparisons failed' % failures)


if __name__ == '__main__':
    main()
