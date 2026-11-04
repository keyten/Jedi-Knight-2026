#!/usr/bin/env python3
"""V03 opt-in data overlays, private licensed captures, owned sequential PIDs only.

No fitted stock sources are invented. Geometry-only markers independently check
world-coordinate alignment; an optional rejected-candidate export is imported as
an explicit hypothesis, never an accepted light. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2'))
sys.path.insert(0, str(ROOT / 'tools/rend2/datasets'))
from test_lightrecon_legacy_runtime import prepare_runtime, CONSUMERS
from test_ssr_runtime import game_processes
from measure import peak_memory


def run(args):
    if args.output.resolve().is_relative_to(ROOT):
        raise ValueError('Licensed screenshots/geometry must remain outside the repository')
    results = []
    for name, camera in [('hoth3', (3464, 700, 85, 90)),
                         ('hoth2', (1728, -576, 1040, 135)),
                         ('vjun2', None), ('mp/duel4', None)]:
        if args.maps and name not in args.maps:
            continue
        if game_processes():
            raise RuntimeError('Preserving another running game; no overlapping validation')
        runtime = args.output / name.replace('/', '_')
        config, archives = prepare_runtime(args, runtime, 'legacy', 2)
        config.update(r_customwidth=640, r_customheight=360, r_picmip=3,
                      r_texturebits=16, r_hdr=0, r_dynamiclight=0, r_fastsky=1,
                      r_shadows=0, r_weather=0, r_flares=0, r_lodbias=2,
                      r_subdivisions=16, r_shadowMapSize=256,
                      r_autoExposure=0, r_cameraExposure=0,
                      r_lightReconUse=0, com_maxfps=30,
                      com_maxfpsUnfocused=30, com_maxfpsMinimized=30)
        config.update({c: 0 for c in CONSUMERS})
        home = runtime / 'home/OpenJK'
        (home / 'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in config.items()))
        commands = ['wait 30', f'devmap {name}', 'wait 120', 'cam_disable', 'exitview',
                    'god', 'notarget', 'noclip', 'cg_draw2D 0', 'cg_drawGun 0',
                    'cg_thirdPerson 0', 'con_notifytime 0']
        if camera:
            commands += ['setviewpos ' + ' '.join(map(str, camera))]
        commands += ['wait 15', 'uimenu recon_freeze', 'fx_freeze 1', 'wait 15',
                     'r_lightReconDebug off', 'wait 5', 'screenshot_tga v03-before',
                     'r_lightReconDebug geometry', 'wait 10', 'r_lightReconDebug info',
                     'viewpos', 'screenshot_tga v03-geometry', 'wait 5',
                     'r_lightReconDebug layer visibility 1', 'wait 5',
                     'screenshot_tga v03-rays', 'wait 5', 'r_lightReconDebug report geometry',
                     'r_lightReconDebug off', 'wait 5', 'screenshot_tga v03-after']
        if name == 'mp/duel4' and args.candidates:
            target = home / 'lightrecon-debug/rejected.jkdbg'
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(args.candidates, target)
            commands += ['r_lightReconDebug load rejected', 'r_lightReconDebug layer placement 1',
                         'r_lightReconDebug layer sources 1', 'wait 5',
                         'screenshot_tga v03-rejected', 'wait 30', 'r_lightReconDebug info',
                         'r_lightReconDebug report rejected', 'r_lightReconDebug off']
        commands += ['r_lightReconDebug sample indirect 0 0 0 0 0 0 1',
                     'r_lightReconDebug sample radiance 0 0 0 0 0 0 1',
                     'r_lightReconDebug sample conditional 0 0 0 0 0 0 1',
                     'r_lightReconDebug source 18446744073709551615',
                     'r_lightReconDebug load missing-v03-control',
                     'r_lightReconInfo', 'wait 30', 'disconnect', 'wait 5',
                     'r_lightReconDebug info', 'echo V03_RUNTIME_DONE', 'wait 5', 'quit']
        (home / 'autoexec_sp.cfg').write_text('\n'.join(commands) + '\n')
        command = [str((runtime / 'openjk_sp.x86_64.exe').resolve())]
        for key in ('fs_basepath', 'fs_homepath', 'fs_game', 'r_lightReconBuild'):
            command += ['+set', key, str(config[key])]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        start = time.perf_counter()
        peak = 0
        with (runtime / 'launch.log').open('w') as log:
            child = subprocess.Popen(command, cwd=runtime, startupinfo=startup,
                                     stdout=log, stderr=subprocess.STDOUT)
            while child.poll() is None:
                peak = max(peak, peak_memory(child) or 0)
                if time.perf_counter() - start > args.timeout or peak > 1024**3:
                    child.terminate()
                    child.wait(timeout=20)
                    raise TimeoutError('Owned V03 process resource limit: ' + name)
                time.sleep(.2)
            peak = max(peak, peak_memory(child) or 0)
        console = (home / 'qconsole.log').read_text(errors='replace')
        if child.returncode or 'V03_RUNTIME_DONE' not in console:
            raise RuntimeError(name + ': incomplete run; ' + console[-1500:])
        if re.search(r'r_lightRecon\w+ requested=\d+ effective=[1-9]', console):
            raise AssertionError('An actual reconstructed-light consumer became active')
        images = {p.stem: p for p in home.rglob('v03-*.tga')}
        from PIL import Image, ImageChops
        for p in images.values():
            Image.open(p).save(p.with_suffix('.png'))
        before = Image.open(images['v03-before']).convert('RGB')
        after = Image.open(images['v03-after']).convert('RGB')
        geometry = Image.open(images['v03-geometry']).convert('RGB')
        def difference(a, b):
            d = ImageChops.difference(a, b)
            pixels = list(d.get_flattened_data())
            return dict(changed_pixels=sum(any(v) for v in pixels),
                        max_channel=max(max(v) for v in pixels),
                        mean_channel=sum(sum(v) for v in pixels)/(3*len(pixels)))
        receipt = dict(map=name, seconds=time.perf_counter()-start,
                       peak_working_set_bytes=peak, camera_requested=camera,
                       markers=re.findall(r'lighting-debug geometry-marker ([^\r\n]+)', console),
                       counters=re.findall(r'lighting-debug records=[^\r\n]+', console),
                       sample_status=re.findall(r'lighting-debug (?:physical|conditional)[^\r\n]+', console),
                       before_after=difference(before, after),
                       overlay_change=difference(before, geometry),
                       screenshots={k: str(v) for k, v in images.items()},
                       image_sha256={k: hashlib.sha256(v.read_bytes()).hexdigest()
                                     for k, v in images.items()},
                       settings=config, archives=archives,
                       interpretation='Geometry alignment and conditional diagnostic data only; no original-stock lamp ground truth')
        results.append(receipt)
        (args.output / 'results.json').write_text(json.dumps(results, indent=2)+'\n')
        print(json.dumps(receipt), flush=True)
        if not receipt['markers'] or receipt['overlay_change']['changed_pixels'] < 20:
            raise AssertionError(name + ': no independently visible geometry marker; not visual validation')
    return results


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--binaries', type=Path, required=True)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--candidates', type=Path)
    p.add_argument('--timeout', type=int, default=180)
    p.add_argument('--maps', nargs='+', choices=['hoth3', 'hoth2', 'vjun2', 'mp/duel4'])
    run(p.parse_args())
