"""Hidden stock-only smoke captures, latched settings and CPU/GPU benchmarks.

Each variant starts a fresh process so its latched settings really apply.
No installation, user config, audio device or mouse state is modified.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time

from test_lightrecon_legacy_runtime import settings

CONTROLS = dict(r_softParticles=1, r_softParticleDistance=8, r_particleNearFade=1,
                r_particleCulling=1, r_particleSort=1, r_particleBatching=1,
                r_transparencySort=1, r_particleVolume=1, r_particleSoftScale=.1,
                r_particleDepthOnDemand=1)
VARIANTS = {
    'legacy': {key: 0 for key in CONTROLS},
    'previous': dict(r_transparencySort=0, r_particleVolume=0, r_particleSoftScale=0),
    'flat': dict(r_particleVolume=0, r_particleSoftScale=0),
    'adaptive': dict(r_particleVolume=0),
    'volume': {},
    'unbatched': dict(r_particleBatching=0),
}
CASES = {
    'empty': ('0', '3464 700 40 90'),
    'tiny64': ('128 2 64', '3464 700 40 90'),
    'tiny512': ('128 2 512', '3464 700 40 90'),
    'large64': ('96 20 64', '3464 700 40 90'),
    'fill128': ('64 36 128', '3464 700 40 90'),
    'mixed': ('batch', '3464 700 40 90'),
    'glass': ('sort', '3464 700 40 90'),
    'wall': ('surface 48 4', '3464 700 40 90'),
    'floor': ('floor 28 12', '3464 700 80 270 25'),
    'floor_detail': ('floor 28 3', '3464 700 80 270 25'),
    'floor_mix': ('floor-mix 28 12', '3464 700 80 270 25'),
    'near': ('12 6 8', '3464 700 40 90'),
    'additive': ('additive 12 128', '3464 700 40 90'),
    'stock': ('stock', '3584 700 60 90'),
    'stock_clear': ('stock', '3464 700 40 90'),
    'stock_short': ('stock', '3464 700 40 90'),
}


def run_hidden(command, root, timeout=300):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    env = os.environ.copy()
    env['SHIM_MCCOMPAT'] = '0x800000001'
    visible = []
    with (root / 'launch.log').open('w') as log:
        child = subprocess.Popen(command, cwd=root, startupinfo=startup, env=env, stdout=log, stderr=subprocess.STDOUT)
        callback_type = C.WINFUNCTYPE(C.c_bool, C.c_void_p, C.c_void_p)
        def check(hwnd, unused):
            pid = C.c_ulong()
            C.windll.user32.GetWindowThreadProcessId(C.c_void_p(hwnd), C.byref(pid))
            if pid.value == child.pid and C.windll.user32.IsWindowVisible(C.c_void_p(hwnd)):
                visible.append(int(hwnd))
            return True
        callback = callback_type(check)
        deadline = time.monotonic() + timeout
        while child.poll() is None and time.monotonic() < deadline:
            C.windll.user32.EnumWindows(callback, 0)
            if visible:
                child.terminate(); child.wait()
                raise RuntimeError('Owned game process created a visible window')
            time.sleep(.2)
        if child.poll() is None:
            child.terminate(); child.wait()
            (root / 'capture-status.json').write_text(json.dumps(dict(outcome='timeout', visible_windows=visible))+'\n')
            raise TimeoutError(str(root))
    assert child.returncode == 0, (child.returncode, root)
    (root / 'capture-status.json').write_text(json.dumps(dict(outcome='clean_exit', visible_windows=visible))+'\n')
    return visible


def image_difference(a, b):
    from PIL import ImageChops
    d = ImageChops.difference(a, b)
    red, green, blue = d.split()
    peaks = ImageChops.lighter(ImageChops.lighter(red, green), blue).histogram()
    return dict(max_delta=max(high for low, high in d.getextrema()),
                changed_pixels=a.width * a.height - peaks[0],
                changed_by_12=sum(peaks[12:]))


def run_variant(args, variant):
    root = (args.output / variant).resolve()
    home = root / 'home/OpenJK'
    controls = dict(CONTROLS, **VARIANTS[variant])
    if args.analyze_existing:
        status = json.loads((root / 'capture-status.json').read_text())
        assert status['outcome'] in ('clean_exit', 'timeout') and status['visible_windows'] == [], status
        return analyze_variant(args, variant, root, home, controls, status)
    home.mkdir(parents=True, exist_ok=True)
    stock = args.installation.resolve()
    expected = ['assets%d.pk3' % i for i in range(4)]
    if sorted(p.name for p in (stock / 'base').iterdir()) != expected:
        assert not args.shared_stock, 'Shared stock directory must contain only assets0-3.pk3'
        private = (args.output / 'stock/base').resolve(); private.mkdir(parents=True, exist_ok=True)
        for name in expected:
            if not (private / name).exists(): os.link(stock / 'base' / name, private / name)
        stock = private.parent
    for name in ('openjk_sp.x86_64.exe', 'rdsp-rend2_x86_64.dll', 'rend2_glslc.exe'):
        shutil.copy2(args.binaries / name, root / name)
    shutil.copy2(args.binaries / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    source = Path(__file__).resolve().parents[2]
    shutil.copy2(source / 'lib/SDL2/bin/x64/SDL2.dll', root / 'SDL2.dll')
    ui = home / 'ui'; ui.mkdir(exist_ok=True)
    for name, value in [('smoke_freeze', 1), ('smoke_unfreeze', 0)]:
        (ui / (name + '.menu')).write_text('menuDef { name %s fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "%d"; } }\n' % (name, value))
    (ui / 'ingame.txt').write_text('loadmenu { "ui/smoke_freeze.menu" "ui/smoke_unfreeze.menu" }\n')
    config = settings('legacy', 0)
    config.update(controls)
    config.update(fs_basepath=str(stock), fs_homepath=str(root / 'home'), fs_game='OpenJK',
                  r_hiddenWindow=1, s_initsound=0, in_mouse=0, in_nograb=1, r_fullscreen=0,
                  r_ext_multisample=args.msaa, r_mode=-1, r_customwidth=args.width, r_customheight=args.height,
                  r_particleLighting=0, r_debugContext=int(args.debug_context), r_ignoreGLErrors=0, r_clear=0,
                  r_weather=0, r_flares=0, r_bloom=0, cg_thirdPersonRange=80,
                  cg_thirdPersonVertOffset=16, cg_thirdPersonAlpha=0)
    config['r_captureShaderTime'] = 1000
    if args.lighting == 'media':
        config.update(r_volumetricFog=2, r_volumetricParticles=1, r_particleLighting=1,
                      r_volumetricFogTemporal=0, fx_physicalization=2,
                      fx_physicalizationOptIn='volumetric/black_smoke;volumetric/black_smoke2')
    (home / 'openjk_sp.cfg').write_text(''.join('set %s "%s"\n' % item for item in config.items()))
    commands = ['wait 20', 'devmap hoth3', 'wait 120', 'cam_disable', 'exitview', 'god', 'notarget',
                'noclip', 'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0', 'con_notifytime 0',
                'uimenu smoke_unfreeze', 'fx_freeze 0', 'give all', 'wait 15', 'weapon 4', 'wait 90',
                'cam_disable', 'lock_disable', 'cg_thirdPerson 0', 'setviewpos 3464 700 40 90',
                'wait 90', 'fx_freeze 1', 'uimenu smoke_freeze', 'wait 30', 'developer 1',
                'r_particleTest info', 'developer 0']
    for name in args.cases:
        fixture, camera = CASES[name]
        commands += ['developer 1', 'r_particleTest 0', 'uimenu smoke_unfreeze', 'fx_freeze 0',
                     'cam_disable', 'setviewpos ' + ' '.join(camera.split()[:4]), 'wait 90',
                     'fxcam ' + camera, 'wait 10', 'fx_freeze 1', 'uimenu smoke_freeze', 'wait 15']
        commands += ['viewpos']
        commands += ['developer 0', 'wait 10', 'screenshot_tga ' + name + '-empty', 'wait 10', 'developer 1']
        if name.startswith('stock'):
            commands += ['uimenu smoke_unfreeze', 'fx_freeze 0', 'wait 4']
            for _ in range(6):
                commands += ['fxplay volumetric/black_smoke 96' if name == 'stock_short' else
                             'fxplay volumetric/black_smoke2 140', 'wait 3']
            commands += ['wait 2' if name == 'stock_short' else 'wait 12', 'fx_freeze 1', 'uimenu smoke_freeze']
        else:
            commands += ['r_particleTest ' + fixture]
        commands += ['wait 30', 'developer 0', 'wait 10', 'screenshot_tga ' + name, 'wait 4']
        if args.benchmark:
            # A 60 FPS hidden view leaves the GPU idle and its power clocks can
            # change several-fold inside one sample. Keep this frozen scene
            # continuously busy; never change global driver/power settings.
            buffered = not (args.stream_profile or args.reference)
            if buffered: commands += ['developer 1', 'r_particleTest profile-reset', 'developer 0']
            commands += ['com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0',
                         'wait 600', 'echo SMOKE_BENCH_%s_BEGIN' % name, 'r_particleProfile %d' % (2 if buffered else 1),
                         'r_speeds 100', 'wait 640', 'r_particleProfile 0', 'r_speeds 0']
            if buffered: commands += ['developer 1', 'r_particleTest profile-dump', 'developer 0']
            commands += ['echo SMOKE_BENCH_%s_END' % name, 'com_maxfps 60',
                         'com_maxfpsUnfocused 60', 'com_maxfpsMinimized 60']
        commands += ['screenshot_tga ' + name + '-repeat', 'wait 4']
    if args.latch_check:
        toggled = {key: 0 if float(value) else CONTROLS[key] for key, value in controls.items()}
        commands += ['developer 1', 'echo SMOKE_LATCH_PENDING']
        commands += ['%s %s' % (key,value) for key,value in toggled.items()]
        commands += ['r_particleTest info', 'developer 0', 'wait 8', 'screenshot_tga latch-pending', 'wait 30',
                     'vid_restart', 'wait 180', 'developer 1', 'echo SMOKE_LATCH_APPLIED',
                     'r_particleTest info', 'developer 0']
    commands += ['echo SMOKE_CAPTURE_DONE', 'wait 30', 'quit']
    if args.reference:
        supported = {'empty', 'tiny64', 'tiny512', 'large64', 'fill128', 'mixed',
                     'glass', 'near', 'additive', 'stock', 'stock_clear', 'stock_short'}
        assert variant == 'legacy' and not args.latch_check and set(args.cases) <= supported
        commands = [line for line in commands if line != 'r_particleTest info']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands) + '\n')
    command = [str(root / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game', 'r_hiddenWindow', 's_initsound', 'in_mouse', 'in_nograb', 'r_fullscreen'):
        command += ['+set', key, str(config[key])]
    timeout = max(300, 60 * len(args.cases)) if args.benchmark else 300
    run_hidden(command, root, timeout=timeout)
    status = json.loads((root / 'capture-status.json').read_text())
    return analyze_variant(args, variant, root, home, controls, status)


def analyze_variant(args, variant, root, home, controls, status):
    # Native 4x-MSAA stress controls have sparse one-code output rounding
    # (119/518400 pixels observed). Never permit a larger per-channel error.
    control_pixels = args.width * args.height // (2000 if args.msaa else 5000)
    toggled = {key: 0 if float(value) else CONTROLS[key] for key, value in controls.items()}
    text = (home / 'qconsole.log').read_text(errors='replace')
    # Search each line once: a greedy whole-line regex at every character is
    # quadratic on verbose GL debug logs and can dominate capture analysis.
    error_pattern = re.compile(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|FBO.*incomplete|Could not load.*shader')
    errors = [line for line in text.splitlines() if error_pattern.search(line)]
    assert 'SMOKE_CAPTURE_DONE' in text and not errors and 'PARTICLE_PROFILE_BUFFER_OVERFLOW' not in text, (errors, text[-3000:])
    reported = re.findall(r'PARTICLE_SETTING\|(\w+)=([^|\r\n]+)\|pending=([^|\r\n]+)\|latched=(\d)', text)
    initial = {name: (value, pending, latched) for name, value, pending, latched in reported[:len(CONTROLS)]}
    for name, value in (() if args.reference else controls.items()):
        assert name in initial and float(initial[name][0]) == float(value) and initial[name][2] == '1', (name, value, initial)
    from PIL import Image
    result = dict(variant=variant, controls=controls, lighting=args.lighting, msaa=args.msaa, width=args.width, height=args.height,
                  debug_context=args.debug_context,
                  hidden=True, visible_windows=status['visible_windows'], capture_status=status,
                  driver=re.findall(r'GL_RENDERER:[^\r\n]*', text), cases={})
    if args.benchmark:
        script = (home / 'autoexec_sp.cfg').read_text()
        result['protocol'] = dict(fps_cap=0, warmup_waits=600, sample_waits=640, discard_records=30,
                                  buffered_profile='r_particleProfile 2' in script)
    for name in args.cases:
        shots = []
        for suffix in ('', '-repeat'):
            im = Image.open(home / ('screenshots/%s%s.tga' % (name, suffix))).convert('RGB')
            im.save(root / ('%s%s.png' % (name, suffix))); shots.append(im)
        control = image_difference(*shots)
        assert control['max_delta'] <= 1 and control['changed_pixels'] <= control_pixels, (name, control)
        case = dict(control=control)
        empty = Image.open(home / ('screenshots/%s-empty.tga' % name)).convert('RGB')
        empty.save(root / (name + '-empty.png'))
        case['presence'] = image_difference(empty, shots[0])
        if name != 'empty':
            required = 10 if name.startswith('tiny') else 200 if name.startswith('stock') else 2000
            assert case['presence']['changed_by_12'] >= required and case['presence']['max_delta'] >= 24, ('invisible fixture', name, case['presence'])
        if args.benchmark:
            section = re.search('SMOKE_BENCH_%s_BEGIN(.*?)SMOKE_BENCH_%s_END' % (name, name), text, re.S)
            assert section, name
            rows = re.findall(r'PARTICLE_PROFILE\|record=([\d.]+)\|submit=([\d.]+)\|sort=([\d.]+)\|items=(\d+)\|particles=(\d+)\|depthCopies=(\d+)', section[1])[30:]
            assert args.reference or len(rows) >= 100, (name, len(rows))
            if rows:
                case.update({key: statistics.median(float(row[i]) for row in rows)
                             for i, key in enumerate(('record_us', 'submit_us', 'sort_us', 'draw_items', 'particle_draw_items', 'depth_copies'))})
            case['cpu_main_us'] = statistics.median(float(row[0]) + float(row[1]) for row in rows) if rows else None
            case['samples'] = len(rows)
            gpu = [float(x) for x in re.findall(r'Main View[^:\r\n]*:\s*([\d.]+)ms', section[1])][30:]
            assert gpu, name
            case['gpu_samples'] = len(gpu)
            case['gpu_main_ms'] = statistics.median(gpu)
            case['cpu_p95_us'] = sorted(float(row[0]) + float(row[1]) for row in rows)[int(len(rows)*.95)] if rows else None
            case['gpu_p95_ms'] = sorted(gpu)[int(len(gpu)*.95)]
            # Media can use separate volume proxies; ordinary-packet counts
            # alone do not describe that path. Visible presence is checked above.
            if name.startswith('stock') and not args.reference and args.lighting == 'legacy' and (controls['r_particleSort'] or controls['r_transparencySort']):
                assert case['particle_draw_items'] > 0, ('No actual stock smoke', case)
        result['cases'][name] = case
    if args.latch_check:
        pending = re.search('SMOKE_LATCH_PENDING(.*?)SMOKE_LATCH_APPLIED', text, re.S)[1]
        applied = text.split('SMOKE_LATCH_APPLIED', 1)[1]
        def read_settings(section):
            return {n:(float(v), p, l) for n,v,p,l in re.findall(r'PARTICLE_SETTING\|(\w+)=([^|\r\n]+)\|pending=([^|\r\n]+)\|latched=(\d)', section)}
        before, after = read_settings(pending), read_settings(applied)
        for key, value in controls.items():
            assert before[key][0] == float(value) and float(before[key][1]) == float(toggled[key]) and before[key][2] == '1', (key,before)
            assert after[key][0] == float(toggled[key]) and after[key][1:] == ('none','1'), (key,after)
        last = Image.open(home / ('screenshots/%s-repeat.tga' % args.cases[-1])).convert('RGB')
        held = Image.open(home / 'screenshots/latch-pending.tga').convert('RGB')
        diff = image_difference(last, held)
        assert diff['max_delta'] <= 1 and diff['changed_pixels'] <= control_pixels, diff
        result['latch_check'] = dict(pending_image=diff, applied_after_restart=True, controls_checked=list(controls))
    (root / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    return result


def main():
    ap = argparse.ArgumentParser(__doc__)
    ap.add_argument('--binaries', type=Path, required=True)
    ap.add_argument('--installation', type=Path, required=True, help='Prepared stock-only assets0-3/base directory')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--variants', nargs='+', choices=VARIANTS, default=['flat', 'adaptive', 'volume'])
    ap.add_argument('--cases', nargs='+', choices=CASES)
    ap.add_argument('--width', type=int, default=960)
    ap.add_argument('--height', type=int, default=540)
    ap.add_argument('--msaa', type=int, default=0)
    ap.add_argument('--lighting', choices=['legacy', 'media'], default='legacy')
    ap.add_argument('--debug-context', action='store_true')
    ap.add_argument('--benchmark', action='store_true')
    ap.add_argument('--stream-profile', action='store_true', help='Older binaries only: stream timing records during sampling')
    ap.add_argument('--latch-check', action='store_true')
    ap.add_argument('--reference', action='store_true', help='Pre-feature renderer with capture-only scene inputs; no new feature/profiler API')
    ap.add_argument('--analyze-existing', action='store_true', help='Analyze completed archived captures and preserve their recorded exit/monitor status')
    # Accepted by the previous capture entry point; the new matrix subsumes them.
    ap.add_argument('--shared-stock', action='store_true')
    ap.add_argument('--sorting', action='store_true')
    ap.add_argument('--batching', action='store_true')
    args = ap.parse_args()
    if args.benchmark and args.debug_context:
        ap.error('Use the release GL context for timings; --debug-context is for separate visual/API validation')
    if args.cases is None: args.cases = list(CASES) if args.benchmark else ['glass', 'wall', 'floor', 'mixed']
    results = [run_variant(args, variant) for variant in args.variants]
    (args.output / 'matrix.json').write_text(json.dumps(results, indent=2) + '\n')
    if 'flat' in args.variants and 'volume' in args.variants:
        from PIL import Image
        comparisons = {}
        for case in ('floor', 'wall'):
            if case not in args.cases: continue
            a = Image.open(args.output / 'flat' / (case + '.png')).convert('RGB')
            b = Image.open(args.output / 'volume' / (case + '.png')).convert('RGB')
            diff = image_difference(a,b)
            assert diff['changed_by_12'] >= 2000 and diff['max_delta'] >= 24, ('Weak contact comparison',case,diff)
            comparisons[case] = diff
        (args.output / 'contact-comparison.json').write_text(json.dumps(comparisons,indent=2)+'\n')
    if 'volume' in args.variants and 'unbatched' in args.variants:
        from PIL import Image
        comparisons = {}
        for case in args.cases:
            if case.startswith('stock'): continue # independent live EFX simulation
            a = Image.open(args.output / 'volume' / (case + '.png')).convert('RGB')
            b = Image.open(args.output / 'unbatched' / (case + '.png')).convert('RGB')
            diff = image_difference(a,b)
            limit = args.width*args.height//(2000 if args.msaa else 5000)
            assert diff['max_delta'] <= 1 and diff['changed_pixels'] <= limit, ('Batch mismatch',case,diff)
            comparisons[case] = diff
        (args.output / 'batch-comparison.json').write_text(json.dumps(comparisons,indent=2)+'\n')


if __name__ == '__main__': main()
