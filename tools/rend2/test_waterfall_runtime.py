"""Capture legacy/modern stock-waterfall comparisons in a private SP runtime.

The installed game and PK3 files are read-only. Generated configs, logs and
screenshots live under build/waterfall-runtime.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


VIEWS = {
    # The large paired falls: the camera is north of the sheets and faces -Y.
    't3_hevil': 'setviewpos -448 -1600 -1050 90',
    # Audited sloped fall; face it from the positive-normal side.
    'yavin1': 'setviewpos -80 -4240 620 205',
    'yavin1b': 'setviewpos -80 -4240 620 205',
}
EXTRA_VIEWS = {
    # yavin1b surface 1, on the opposite side of the map from the reused
    # yavin1 sheet (surface 2).
    'yavin1b': 'setviewpos -1050 2650 850 108',
}


def launch(repo, installation, map_name, enabled, timeout):
    root = repo/'build/waterfall-runtime'/('modern' if enabled else 'legacy')
    home = root/'home'
    base = home/'OpenJK'
    mode = 'modern' if enabled else 'legacy'
    label = f'{map_name}-{mode}'
    base.mkdir(parents=True, exist_ok=True)
    (base/'screenshots').mkdir(exist_ok=True)
    for name in ('openjk_sp.x86_64.exe', 'SDL2.dll'):
        shutil.copy2(installation/name, root/name)
    shutil.copy2(repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll', root/'rdsp-rend2_x86_64.dll')
    shutil.copy2(installation/'jagamex86_64.dll', base/'jagamex86_64.dll')
    shutil.copy2(repo/'build/rend2/water-body-overlay.pk3', base/'zzzz_rend2_water_bodies.pk3')
    old = base/'screenshots'/f'waterfall-{label}.tga'
    if old.exists():
        old.unlink()
    if (base/'qconsole.log').exists():
        (base/'qconsole.log').unlink()

    commands = ['wait 120', 'cam_disable', 'wait 10', 'noclip', 'god',
                'set cg_draw2D 0', 'set cg_drawGun 0', 'set r_drawentities 0',
                'set con_notifytime 0', VIEWS[map_name], 'centerview', 'wait 90',
                'r_waterfalls', 'r_waterGeometryInfo', f'screenshot_tga waterfall-{label}', 'wait 10']
    # Measure the real dam view rather than treating the synthetic shader
    # harness as scene performance.  r_speeds 100 uses renderer GPU timestamp
    # queries; the legacy/modern medians include the same complete main view.
    commands += ['r_speeds 100', 'wait 16', 'r_speeds 0', 'wait 4']
    if map_name in EXTRA_VIEWS:
        commands += [EXTRA_VIEWS[map_name], 'centerview', '+lookdown', 'wait 8', '-lookdown',
                     'wait 40', f'screenshot_tga waterfall-{map_name}-upper-{mode}', 'wait 10']
    if enabled:
        commands += ['r_waterSurfaceDebug 9', 'r_waterSurfaceSplit 0.5', 'wait 5',
                     f'screenshot_tga waterfall-{map_name}-split', 'r_waterSurfaceDebug 0']
        for debug_mode, name in ((1, 'classified'), (2, 'flow'), (3, 'along'),
                           (5, 'thickness'), (7, 'turbulence'), (8, 'aeration'),
                           (9, 'foam-source'), (10, 'spray-source'),
                           (11, 'impact'), (12, 'wireframe')):
            commands += [f'r_waterfallDebug {debug_mode}', 'wait 5', f'screenshot_tga waterfall-{name}']
        commands += ['r_waterfallDebug 0']
    commands += ['echo WATERFALL_RUNTIME_DONE', 'quit']
    (base/'waterfall.cfg').write_text('\n'.join(commands)+'\n', encoding='utf-8')
    (base/'autoexec_sp.cfg').write_text(
        f'wait 300\ndevmap {map_name}\nwait 60\nexec waterfall.cfg\n', encoding='utf-8')
    settings = dict(fs_basepath=str(installation), fs_homepath=str(home), fs_game='OpenJK',
                    cl_renderer='rdsp-rend2', r_fullscreen='0', r_mode='6', s_initsound='0',
                    r_glslCache='1', r_waterSurface='1', r_waterfall='1' if enabled else '0',
                    r_waterfallQuality='1', r_waterfallGeometry='1', r_ssr='0',
                    r_cubeMapping='0', r_diffuseIBL='0', r_volumetricFog='2',
                    r_volumetricWater='1', developer='1', logfile='2', com_maxfps='60')
    command = [str(root/'openjk_sp.x86_64.exe')]
    for key, value in settings.items():
        command += ['+set', key, value]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (root/'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=root, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + timeout
        marker = False
        while process.poll() is None and time.monotonic() < deadline:
            log_path = base/'qconsole.log'
            if log_path.exists() and 'WATERFALL_RUNTIME_DONE' in log_path.read_text(errors='replace'):
                marker = True
                break
            time.sleep(0.5)
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if not marker and time.monotonic() >= deadline:
            raise subprocess.TimeoutExpired(command, timeout)
    log = (base/'qconsole.log').read_text(errors='replace')
    assert 'WATERFALL_RUNTIME_DONE' in log, log[-5000:]
    assert not re.search(r'GL_INVALID|GLSL shader compile error|Could not load watersurface', log), log[-5000:]
    shot = base/'screenshots'/f'waterfall-{label}.tga'
    assert shot.exists(), f'missing {shot}'
    return root, log, shot


def gpu_main_view_samples(log):
    return [float(value) for value in re.findall(r'Main View \d+: ([0-9.]+)ms', log)]


def gpu_water_samples(log):
    return [float(value) for value in re.findall(r'Water surface: ([0-9.]+)ms', log)]


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--map', choices=tuple(VIEWS), default='t3_hevil')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows SP runtime required')
    installation = args.installation.resolve()
    legacy_root, legacy_log, legacy_shot = launch(repo, installation, args.map, False, args.timeout)
    modern_root, modern_log, modern_shot = launch(repo, installation, args.map, True, args.timeout)
    expected = 8 if args.map == 't3_hevil' else (2 if args.map == 'yavin1b' else 1)
    assert re.search(rf'{expected} waterfall sheets?', modern_log), modern_log[-5000:]
    assert re.search(rf'{expected} waterfall sheets?.*renderer legacy', legacy_log), legacy_log[-5000:]
    legacy_gpu = gpu_main_view_samples(legacy_log)
    modern_gpu = gpu_main_view_samples(modern_log)
    legacy_water_gpu = gpu_water_samples(legacy_log)
    modern_water_gpu = gpu_water_samples(modern_log)
    assert legacy_gpu and modern_gpu and legacy_water_gpu and modern_water_gpu, \
        'missing r_speeds 100 GPU timestamp samples'
    print('PASS: legacy-off and dedicated waterfall-on runs completed')
    print(f'Actual {args.map} main-view GPU median: legacy {statistics.median(legacy_gpu):.3f} ms, '
          f'modern {statistics.median(modern_gpu):.3f} ms '
          f'({len(legacy_gpu)}/{len(modern_gpu)} samples)')
    print(f'Actual {args.map} water-slot GPU median: legacy {statistics.median(legacy_water_gpu):.3f} ms, '
          f'modern {statistics.median(modern_water_gpu):.3f} ms '
          f'({len(legacy_water_gpu)}/{len(modern_water_gpu)} samples)')
    print('Legacy:', legacy_shot)
    print('Modern:', modern_shot)
    print('Modern log:', modern_root/'home/OpenJK/qconsole.log')


if __name__ == '__main__':
    main()
