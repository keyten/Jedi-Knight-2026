"""Isolated stock SP particle A/B captures; hidden window, no sound or mouse.

Usage: python capture_particles.py --binaries <build> --installation <GameData> --output <private-dir>
Only the four original archives are mounted. No files in the installation change.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import statistics
import time

from test_lightrecon_legacy_runtime import settings


def main():
    ap = argparse.ArgumentParser(__doc__)
    ap.add_argument('--binaries', type=Path, required=True)
    ap.add_argument('--installation', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--msaa', type=int, default=0)
    ap.add_argument('--shared-stock', action='store_true', help='Reuse a prepared directory with only assets0-3.pk3')
    ap.add_argument('--debug-context', action='store_true')
    ap.add_argument('--sorting', action='store_true')
    ap.add_argument('--benchmark', action='store_true')
    ap.add_argument('--batching', action='store_true')
    args = ap.parse_args()
    root = args.output.resolve()
    home = root / 'home/OpenJK'
    base = root / 'base'
    home.mkdir(parents=True, exist_ok=True)
    base.mkdir(exist_ok=True)
    if args.shared_stock:
        assert sorted(p.name for p in (args.installation / 'base').iterdir()) == ['assets%d.pk3' % i for i in range(4)]
    else:
        for i in range(4):
            dest = base / ('assets%d.pk3' % i)
            if not dest.exists():
                os.link(args.installation / 'base' / dest.name, dest)
    for name in ('openjk_sp.x86_64.exe', 'rdsp-rend2_x86_64.dll', 'rend2_glslc.exe'):
        shutil.copy2(args.binaries / name, root / name)
    shutil.copy2(args.binaries / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    source_root = Path(__file__).resolve().parents[2]
    shutil.copy2(source_root / 'lib/SDL2/bin/x64/SDL2.dll', root / 'SDL2.dll')
    ui = home / 'ui'
    ui.mkdir(exist_ok=True)
    (ui / 'particle_freeze.menu').write_text('menuDef { name particle_freeze fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\n')
    (ui / 'particle_unfreeze.menu').write_text('menuDef { name particle_unfreeze fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "0"; } }\n')
    (ui / 'ingame.txt').write_text('loadmenu { "ui/particle_freeze.menu" "ui/particle_unfreeze.menu" }\n')
    config = settings('legacy', 0)
    config.update(fs_basepath=str(args.installation.resolve() if args.shared_stock else root), fs_homepath=str(root / 'home'), fs_game='OpenJK',
                  r_hiddenWindow=1, in_nograb=1, r_softParticles=1, r_particleNearFade=1,
                  r_ext_multisample=args.msaa, r_particleLighting=0, r_softParticleDistance=8, r_particleBatching=0,
                  cg_thirdPersonRange=80, cg_thirdPersonVertOffset=16, cg_thirdPersonAlpha=0,
                  r_debugContext=int(args.debug_context), r_ignoreGLErrors=0, r_clear=0, r_weather=0, r_flares=0, r_bloom=0)
    (home / 'openjk_sp.cfg').write_text(''.join('set %s "%s"\n' % item for item in config.items()))
    commands = ['wait 20', 'devmap hoth3', 'wait 120', 'cam_disable', 'exitview',
        'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0',
        'con_notifytime 0', 'uimenu particle_unfreeze', 'fx_freeze 0', 'give all', 'wait 15', 'weapon 4', 'wait 90',
        'cam_disable', 'lock_disable', 'cg_thirdPerson 0', 'setviewpos 3464 700 40 90', 'wait 90', 'developer 1',
        'fx_freeze 1', 'uimenu particle_freeze', 'wait 30', 'cl_paused', 'sv_paused',
        'r_particleTest 0', 'wait 8', 'developer 0', 'screenshot_tga empty', 'wait 3',
        'developer 1', 'r_particleTest surface 144 12', 'wait 12', 'developer 0']
    for label, distance in [('contact-before', 0), ('contact-after', 8), ('contact-repeat', 0)]:
        commands += ['r_softParticleDistance %d' % distance, 'wait 8', 'screenshot_tga ' + label, 'wait 3']
    commands += ['developer 1', 'r_particleTest 12 6 8', 'wait 8', 'developer 0']
    for label, enabled in [('near-before', 0), ('near-after', 1), ('near-repeat', 0)]:
        commands += ['r_particleNearFade %d' % enabled, 'wait 8', 'screenshot_tga ' + label, 'wait 3']
    if args.sorting:
        commands += ['r_softParticleDistance 0', 'r_particleNearFade 0', 'developer 1', 'r_particleTest sort']
        for label, enabled, scene in [('sort-before', 0, 'sort'), ('sort-after', 1, 'sort'),
                                       ('sort-reverse', 1, 'sort-reverse'), ('sort-repeat', 0, 'sort')]:
            commands += ['developer 1', 'r_particleTest ' + scene, 'r_particleSort %d' % enabled, 'wait 12', 'developer 0', 'wait 8',
                         'screenshot_tga ' + label, 'wait 3']
    if args.batching:
        commands += ['r_particleSort 1', 'r_softParticleDistance 8', 'r_particleNearFade 1', 'developer 1']
        for label, enabled, scene in [('before', 0, 'batch'), ('after', 1, 'batch'),
                                       ('reverse', 1, 'batch-reverse'), ('repeat', 0, 'batch')]:
            commands += ['developer 1', 'r_particleTest ' + scene, 'r_particleBatching %d' % enabled, 'wait 12', 'developer 0', 'wait 8',
                         'screenshot_tga batch-' + label, 'wait 3']
    benchmark_cases = [(name, count, batch) for name, count in [('empty', 0), ('p64', 64), ('p512', 512)]
                       for batch in (0, 1, 0)]
    if args.benchmark:
        for case, (name, count, batch) in enumerate(benchmark_cases):
            name = '%s_%d_%d' % (name, batch, case)
            commands += ['r_particleProfile 0', 'r_speeds 0', 'developer 1',
                         'r_particleTest %s' % ('128 2 %d' % count if count else '0'),
                         'r_particleBatching %d' % batch, 'r_softParticleDistance 8', 'r_particleNearFade 1', 'r_particleSort 1', 'wait 120', 'developer 0',
                         'echo PARTICLE_BENCH_%s_BEGIN' % name, 'r_particleProfile 1', 'r_speeds 100',
                         'wait 240', 'r_particleProfile 0', 'r_speeds 0', 'echo PARTICLE_BENCH_%s_END' % name]
    commands += ['echo PARTICLE_CAPTURE_DONE', 'wait 3', 'quit']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands) + '\n')
    command = [str(root / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game', 'r_hiddenWindow', 's_initsound', 'in_mouse', 'in_nograb', 'r_fullscreen'):
        command += ['+set', key, str(config[key])]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    env = os.environ.copy()
    env['SHIM_MCCOMPAT'] = '0x800000001'
    visible = []
    with (root / 'launch.log').open('w') as log:
        child = subprocess.Popen(command, cwd=root, startupinfo=startup, env=env, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 240
        callback_type = C.WINFUNCTYPE(C.c_bool, C.c_void_p, C.c_void_p)
        def check(hwnd, unused):
            pid = C.c_ulong()
            C.windll.user32.GetWindowThreadProcessId(C.c_void_p(hwnd), C.byref(pid))
            if pid.value == child.pid and C.windll.user32.IsWindowVisible(C.c_void_p(hwnd)):
                visible.append(int(hwnd))
            return True
        callback = callback_type(check)
        while child.poll() is None and time.monotonic() < deadline:
            C.windll.user32.EnumWindows(callback, 0)
            if visible:
                child.terminate()
                child.wait()
                raise RuntimeError('Owned process created a visible window')
            time.sleep(0.2)
        if child.poll() is None:
            child.terminate()
            child.wait()
            raise TimeoutError(root)
    text = (home / 'qconsole.log').read_text(errors='replace')
    errors = [line for line in text.splitlines() if re.search('GL_INVALID|GLSL shader.*error|FBO.*incomplete|Could not load.*shader', line)]
    assert child.returncode == 0 and 'PARTICLE_CAPTURE_DONE' in text and not errors, (child.returncode, errors, text[-2500:])
    from PIL import Image, ImageChops
    results = {}
    for kind in ('contact', 'near'):
        images = [Image.open(home / ('screenshots/%s-%s.tga' % (kind, label))).convert('RGB')
                  for label in ('before', 'after', 'repeat')]
        repeat_delta = ImageChops.difference(images[0], images[2])
        repeat = repeat_delta.getbbox()
        max_repeat = max(high for low, high in repeat_delta.getextrema())
        changed_repeat = sum(any(pixel) for pixel in repeat_delta.getdata())
        delta = ImageChops.difference(images[0], images[1])
        # FP16 output/resolve can differ by one display code at sparse rounding ties.
        repeat_limit = max(1, images[0].width * images[0].height // 5000)
        assert repeat is None or (max_repeat <= 1 and changed_repeat <= repeat_limit), (kind, 'unstable A/B/A camera or simulation', repeat, max_repeat, changed_repeat)
        stimulus_pixels = sum(any(pixel) for pixel in delta.getdata())
        stimulus_max = max(high for low, high in delta.getextrema())
        assert stimulus_max >= 12 and stimulus_pixels >= 2000, (kind, 'no clearly visible change', stimulus_max, stimulus_pixels)
        for label, im in zip(('before', 'after'), images):
            im.save(root / ('%s-%s.png' % (kind, label)))
        results[kind] = dict(repeat_identical=repeat is None, repeat_max_delta=max_repeat,
                             repeat_changed_pixels=changed_repeat, changed_bounds=delta.getbbox(),
                             stimulus_pixels=stimulus_pixels, stimulus_max_delta=stimulus_max)
    empty = Image.open(home / 'screenshots/empty.tga').convert('RGB')
    smoke = Image.open(root / 'contact-before.png').convert('RGB')
    presence = ImageChops.difference(empty, smoke)
    presence_pixels = sum(max(pixel) >= 12 for pixel in presence.getdata())
    assert presence_pixels >= 5000 and max(high for low, high in presence.getextrema()) >= 24, ('smoke not clearly visible', presence_pixels)
    empty.save(root / 'empty.png')
    results['smoke_presence'] = dict(pixels_changed_by_12_or_more=presence_pixels)
    if args.sorting:
        shots = {}
        for label in ('before', 'after', 'reverse', 'repeat'):
            shots[label] = Image.open(home / ('screenshots/sort-%s.tga' % label)).convert('RGB')
            shots[label].save(root / ('sort-%s.png' % label))
        for a, b in [('before', 'repeat'), ('after', 'reverse')]:
            diff = ImageChops.difference(shots[a], shots[b])
            maximum = max(high for low, high in diff.getextrema())
            count = sum(any(pixel) for pixel in diff.getdata())
            assert not diff.getbbox() or (maximum <= 1 and count <= repeat_limit), ('sort control', a, b, maximum, count)
        diff = ImageChops.difference(shots['before'], shots['after'])
        count = sum(max(pixel) >= 12 for pixel in diff.getdata())
        maximum = max(high for low, high in diff.getextrema())
        assert count >= 5000 and maximum >= 24, ('sort stimulus too weak', maximum, count)
        results['sorting'] = dict(changed_pixels_by_12_or_more=count, max_delta=maximum,
                                   reversed_submission_identical=not ImageChops.difference(shots['after'], shots['reverse']).getbbox())
    if args.batching:
        shots = {}
        for label in ('before', 'after', 'reverse', 'repeat'):
            shots[label] = Image.open(home / ('screenshots/batch-%s.tga' % label)).convert('RGB')
            shots[label].save(root / ('batch-%s.png' % label))
        results['batching'] = {}
        for label in ('after', 'reverse', 'repeat'):
            diff = ImageChops.difference(shots['before'], shots[label])
            maximum = max(high for low, high in diff.getextrema())
            count = sum(any(pixel) for pixel in diff.getdata())
            assert not diff.getbbox() or (maximum <= 1 and count <= repeat_limit), ('batch control', label, maximum, count)
            results['batching'][label] = dict(identical=not diff.getbbox(), max_delta=maximum, changed_pixels=count)
    result = dict(msaa=args.msaa, hidden=True, visible_windows=visible, settings=config, captures=results,
                  driver=re.findall(r'GL_RENDERER:[^\r\n]*', text))
    if args.benchmark:
        result['benchmark'] = {}
        for case, (name, count, batch) in enumerate(benchmark_cases):
            name = '%s_%d_%d' % (name, batch, case)
            section = re.search('PARTICLE_BENCH_%s_BEGIN(.*?)PARTICLE_BENCH_%s_END' % (name, name), text, re.S)
            assert section, name
            rows = re.findall(r'PARTICLE_PROFILE\|record=([\d.]+)\|submit=([\d.]+)\|sort=([\d.]+)\|items=(\d+)\|particles=(\d+)\|depthCopies=(\d+)', section[1])[20:]
            assert len(rows) >= 50, (name, len(rows))
            stats = {key: statistics.median(float(row[i]) for row in rows)
                     for i, key in enumerate(('record_us', 'submit_us', 'sort_us', 'draw_items', 'particle_draw_items', 'depth_copies'))}
            assert stats['depth_copies'] == (1 if count else 0), (name, 'unexpected depth-copy count', stats)
            if count:
                assert stats['particle_draw_items'] < count if batch else stats['particle_draw_items'] == count, (name, stats)
            gpu = [float(x) for x in re.findall(r'Main View[^:\r\n]*:\s*([\d.]+)ms', section[1])][20:]
            depth = [float(x) for x in re.findall(r'Particle depth:\s*([\d.]+)ms', section[1])][20:]
            stats.update(samples=len(rows), gpu_main_ms=statistics.median(gpu) if gpu else None,
                         gpu_depth_ms=statistics.median(depth) if depth else None,
                         cpu_main_us=statistics.median(float(row[0]) + float(row[1]) for row in rows))
            result['benchmark'][name] = stats
    (root / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)


if __name__ == '__main__':
    main()
