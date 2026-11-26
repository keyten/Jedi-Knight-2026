"""Private frozen same-build lightall specialization A/B and light switching.

Uses installed assets read-only, private entity/config/cache/binary copies and a
hidden owned SP process. GPU timestamps and full-frame intervals are separate;
nested view/pass timers are never summed. Requires an exclusive GPU lease.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time
from types import SimpleNamespace
from test_sprite_cull_runtime import prepare as prepare_grass
from test_depth_prepass_runtime import tga_pixels
from test_sunshadow_cache_runtime import running_openjk_pids

ROOT = Path(__file__).resolve().parents[2]


def prepare(args):
    assert not (ROOT / 'build/sprite-cull-runtime' / args.label / 'launch.log').exists(), 'Use a fresh label'
    config = SimpleNamespace(**vars(args), map='yavin1', pose='1024 -4700 210 90',
                             gl43=1, cards=3, shadows=True,
                             bend_distance=350, card_distance=300)
    staged = ROOT / 'build/lightall-binaries' / args.label
    staged.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.renderer / 'rdsp-rend2_x86_64.dll', staged / 'rdsp-rend2_x86_64.dll')
    helper = args.renderer / 'rend2_glslc.exe'
    shutil.copy2(helper if helper.exists() else args.binaries / 'rend2_glslc.exe', staged / 'rend2_glslc.exe')
    config.renderer = staged
    runtime, home, launch = prepare_grass(config)
    if args.cache_from:
        for name in ('glslcache', 'cache'):
            source = args.cache_from / name
            if source.exists():
                shutil.copytree(source, home / name, dirs_exist_ok=True)
    entities = ['{ "classname" "worldspawn" }',
                '{ "classname" "info_player_start" "origin" "1024 -4700 210" "angle" "90" }']
    for i in range(40):
        entities.append('{ "classname" "misc_model_static" "model" "models/map_objects/factory/barrel.md3" '
                        f'"origin" "{800+i%8*64} {-4400+i//8*80} 144" '
                        f'"angles" "0 {i%4*30} 0" "modelscale" "{1+i%3*.125}"' + ' }')
    (home / 'maps').mkdir(exist_ok=True)
    (home / 'maps/yavin1.ent').write_text('\n'.join(entities) + '\n')
    cfg = home / 'autoexec_sp.cfg'
    settings = cfg.read_text().split('wait 60', 1)[0]
    settings += ('set r_worldSurfaceList 1\nset r_modelOcclusion 1\n'
                 'set r_md3Instancing 1\nset r_surfaceSpritesCompute 2\n'
                 'set r_sunShadowCache 1\nset r_shadowSinglePass 1\nset r_shadowLayered 1\n'
                 'set r_skyPortalCull 1\nset r_shadowCache 0\nset r_shadowStaticCache 0\n'
                 'set r_shaderProgramCache 1\nset r_lightallSpecialize 0\n'
                 'set r_forwardPlus 0\nset r_ltcAreaLights 0\nset r_autoPBR 0\n'
                 'set r_parallaxMapping 0\nset r_particleProfile 2\nset developer 1\n')
    settings = settings.replace('set r_customwidth "640"', f'set r_customwidth "{args.width}"')
    settings = settings.replace('set r_customheight "360"', f'set r_customheight "{args.height}"')
    commands = ['wait 90', 'devmap yavin1', 'wait 240', 'exitview', 'cam_disable',
                'lock_disable', 'god', 'notarget', 'noclip', 'cg_thirdPerson 0',
                'setviewpos 1024 -4700 210 90', 'wait 30', 'uimenu sprite_freeze',
                'fx_freeze 1', 'wait 30', 'r_forwardPlusSpawnTestLights 0',
                'r_lightallSpecialize 1', 'wait 120', 'r_lightallSpecialize 0', 'wait 60',
                'com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
    phases = []
    for state, lights, modes in [('none', 0, (0, 1, 1, 0)),
                                 ('eight', 8, (0, 1, 0)),
                                 ('returned', 0, (0, 1))]:
        commands += [f'r_forwardPlusSpawnTestLights {lights} 96', 'wait 30']
        for index, mode in enumerate(modes):
            name = f'{state}_{index}_{mode}'
            phases.append(dict(name=name, lights=lights, specialized=mode))
            commands += [f'r_lightallSpecialize {mode}', 'wait 60',
                         f'screenshot_tga lightall_{name}', 'glslusage reset',
                         'r_particleTest profile-reset', f'echo L7_GPU_BEGIN_{name}',
                         'r_speeds 100', 'wait 120', 'r_speeds 0',
                         'r_particleTest profile-dump', f'echo L7_GPU_END_{name}',
                         f'echo L7_USAGE_BEGIN_{name}', 'glslusage all', f'echo L7_USAGE_END_{name}']
            if args.benchmark and state == 'none':
                commands += [f'echo L7_FRAME_BEGIN_{name}', 'r_sunShadowBenchmark 90',
                             'wait 600', f'echo L7_FRAME_END_{name}']
    commands += ['r_speeds 0', 'r_particleProfile 0', 'echo LIGHTALL_RUNTIME_DONE',
                 'com_maxfps 60', 'com_maxfpsUnfocused 60', 'com_maxfpsMinimized 60',
                 'wait 30', 'quit']
    cfg.write_text(settings + '\n'.join(commands) + '\n')
    launch[1:1] = ['+set', 'r_gl43', '1', '+set', 'r_shaderProgramLazy', '1',
                   '+set', 'r_shaderProgramCache', '1', '+set', 'cl_renderer', 'rdsp-rend2']
    return runtime, home, launch, phases


def system_times():
    idle, kernel, user = (wintypes.FILETIME() for _ in range(3))
    assert ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
    value = lambda x: (x.dwHighDateTime << 32) | x.dwLowDateTime
    return value(idle), value(kernel) + value(user)


def summarize(runtime, home, phases):
    log = (runtime / 'launch.log').read_text(errors='replace')
    assert 'LIGHTALL_RUNTIME_DONE' in log, 'Incomplete runtime'
    assert not re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|FBO.*incomplete|GLSL shader (?:compile|link) error', log, re.I)
    assert not re.search(r'Unknown command "(?:god|noclip|notarget)"', log), 'Client not active at setup'
    gpu = {}
    usage = {}
    for name, body in re.findall(r'L7_GPU_BEGIN_(\w+)(.*?)L7_GPU_END_\1', log, re.S):
        values = {}
        for timer, number in re.findall(r'([A-Za-z][A-Za-z0-9+ _()/-]+): ([0-9.]+)ms', body):
            values.setdefault(timer.strip(), []).append(float(number))
        gpu[name] = {timer: dict(n=len(v[8:-4]), median_ms=statistics.median(v[8:-4]))
                     for timer, v in values.items() if len(v) > 12}
        assert any(key.startswith('Main View ') for key in gpu[name]), (name, 'No active Main View samples')
    for name, body in re.findall(r'L7_USAGE_BEGIN_(\w+)(.*?)L7_USAGE_END_\1', log, re.S):
        usage[name] = sorted(set(re.findall(r'\b(lightall[^\s]+)', body)))
    assert 'PARTICLE_PROFILE_BUFFER_OVERFLOW' not in log, 'Buffered timing data overflow'
    assert len(gpu) == len(phases), ('Missing timing phases', gpu.keys())
    assert any('_NO_LOCAL_LIGHTS' in prog for phase in phases if phase['specialized']
               for prog in usage[phase['name']]), 'Specialized program never bound'
    assert all('_NO_LOCAL_LIGHTS' not in prog for phase in phases if not phase['specialized']
               for prog in usage[phase['name']]), 'Disabled path still bound specialized program'
    captures = {phase['name']: tga_pixels(home / f'screenshots/lightall_{phase["name"]}.tga') for phase in phases}
    assert len({len(pixels) for pixels in captures.values()}) == 1, 'Capture dimensions changed'
    for name, pixels in captures.items():
        assert any(pixels), (name, 'Empty capture')
    diffs = {}
    for state in ('none', 'eight', 'returned'):
        names = [p['name'] for p in phases if p['name'].startswith(state+'_')]
        for name in names[1:]:
            delta = sum(a != b for a, b in zip(captures[names[0]], captures[name]))
            diffs[names[0]+'/'+name] = delta
            assert delta == 0, (names[0], name, delta)
    lights_delta = sum(a != b for a, b in zip(captures['none_0_0'], captures['eight_0_0']))
    assert lights_delta > 0, 'Test lights did not influence pixels'
    returned_delta = sum(a != b for a, b in zip(captures['none_0_0'], captures['returned_0_0']))
    assert returned_delta == 0, ('Removing lights did not restore reference', returned_delta)
    frames = {}
    for name, body in re.findall(r'L7_FRAME_BEGIN_(\w+)(.*?)L7_FRAME_END_\1', log, re.S):
        rows = re.findall(r'sunShadowBenchmark: (\d+) frames, mean ([\d.]+) ms, median ([\d.]+) ms, p95 ([\d.]+) ms', body)
        assert len(rows) == 1 and int(rows[0][0]) == 90, (name, 'Incomplete frame window', rows)
        frames[name] = dict(zip(('frames', 'mean_ms', 'median_ms', 'p95_ms'), map(float, rows[0])))
    result = dict(gpu=gpu, frame_intervals=frames, usage=usage, capture_differing_channels=diffs,
                  test_light_differing_channels=lights_delta, returned_reference_differing_channels=returned_delta,
                  renderer=re.findall(r'GL_RENDERER:[^\r\n]+', log))
    (runtime / 'results.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


def process_inventory():
    command = "Get-Process | Sort-Object CPU -Descending | Select-Object -First 20 Id,Name,CPU | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, text=True, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True, help='Engine/game/helper binaries')
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--cache-from', type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=360)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    assert re.fullmatch(r'[A-Za-z0-9_-]+', args.label)
    runtime, home, launch, phases = prepare(args)
    if not args.run:
        print('Prepared', runtime)
        return
    assert not running_openjk_pids(), 'Foreign OpenJK process prevents exclusive run'
    metadata = dict(launch=launch, phases=phases, width=args.width, height=args.height,
                    harness_source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip(),
                    binary_sha256=hashlib.sha256((runtime/'rdsp-rend2_x86_64.dll').read_bytes()).hexdigest(),
                    foreign_before=[], process_inventory_before=process_inventory())
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (runtime / 'launch.log').open('w') as output:
        process = subprocess.Popen(launch, cwd=runtime, startupinfo=startup, stdout=output, stderr=output)
        metadata['owned_pid'] = process.pid
        print('Owned PID', process.pid, flush=True)
        start = time.monotonic()
        previous = system_times()
        samples = []
        while process.poll() is None:
            time.sleep(1)
            current = system_times()
            total = current[1] - previous[1]
            samples.append(dict(unix=time.time(), system_cpu_percent=100*(1-(current[0]-previous[0])/total) if total else 0,
                                foreign_pids=sorted(running_openjk_pids() - {process.pid})))
            previous = current
            if time.monotonic()-start > 600:
                process.terminate()
                process.wait(timeout=10)
                metadata['timed_out'] = True
                break
    metadata['cpu_samples'] = samples
    metadata['process_inventory_after'] = process_inventory()
    metadata['exit_code'] = process.returncode
    metadata['foreign_after'] = sorted(running_openjk_pids())
    (runtime / 'run-meta.json').write_text(json.dumps(metadata, indent=2))
    assert process.returncode == 0, metadata
    summarize(runtime, home, phases)


if __name__ == '__main__':
    main()
