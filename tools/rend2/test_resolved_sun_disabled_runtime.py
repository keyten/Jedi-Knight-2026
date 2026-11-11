"""Real SP/MP A/A and original-code vs C01 default-disabled image comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from types import SimpleNamespace
import numpy as np
from PIL import Image
import test_resolved_sun_runtime as R


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['installation', 'binaries', 'renderer', 'baseline-renderer', 'output']:
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--mp', action='store_true')
    p.add_argument('--mp-test-ui', type=Path)
    p.add_argument('--sp-binaries', type=Path)
    p.add_argument('--map', default='hoth2')
    p.add_argument('--world-only', action='store_true', help='Hide dynamic entities during cross-process comparison')
    p.add_argument('--profile', choices=['legacy', 'modern'], default='modern')
    p.add_argument('--timeout', type=int, default=600)
    a = p.parse_args()
    for name in ['installation', 'binaries', 'renderer', 'baseline_renderer', 'output', 'sp_binaries', 'mp_test_ui']:
        value = getattr(a, name)
        if value: setattr(a, name, value.resolve())
    if a.mp and not (a.sp_binaries and a.mp_test_ui): p.error('--mp needs --sp-binaries and --mp-test-ui')
    a.output.mkdir(parents=True, exist_ok=False)
    results = {}
    paths = {}
    for version in ['original', 'C01']:
        case = SimpleNamespace(**vars(a))
        case.output = a.output / version
        case.renderer = a.baseline_renderer if version == 'original' else a.renderer
        runtime, cfg = R.prepare_runtime(case, a.profile, 0)
        cfg.update(r_customwidth=640, r_customheight=360, r_picmip=2,
                   r_shadowMapSize=512, r_sunShadowCache=1, r_sunShadowMode=1,
                   r_atmosphere=0, r_clouds=0, r_volumetricFogTemporal=0,
                   r_cubeMapping=0, r_diffuseIBL=0, r_lightReconUse=0, r_lightReconSun=1)
        if a.profile == 'modern': cfg.update(r_sunlightMode=2, r_pbrPhysical=1)
        cmd = R.prepare_scene(a.map)
        if a.world_only:
            cmd += ['r_drawentities 0']
        for label, master, request in [('A', 0, 1), ('AA', 0, 1), ('OFF', 1, 0), ('MISSING', 1, 1), ('RESTORE', 0, 1)]:
            cmd += [f'r_lightReconUse {master}', f'r_lightReconSun {request}'] + R.capture(label)
        cmd += ['wait 90', 'echo C01_DONE', 'quit']
        text = R.launch(case, runtime, cfg, cmd, 'disabled')
        cameras = []
        for label in ['A', 'AA', 'OFF', 'MISSING', 'RESTORE']:
            block = re.search(f'C01_{label}_BEGIN(.*?)C01_{label}_END', text, re.S)
            if not block or not re.search(r'\d+/\d+/\d+ shaders/batches/surfs \d+ leafs', block[1]):
                raise ValueError('Missing actual world in ' + version + '/' + label)
            for key in ['cl_paused', 'sv_paused']:
                if not re.search(key + r'\s*=\s*"1"', block[1]):
                    raise ValueError('Invalid pause control')
            camera = re.findall(r'maps/[^\n]+ \((-?\d+ -?\d+ -?\d+)\) : (-?\d+)', block[1])
            if len(camera) != 1: raise ValueError('Missing camera proof')
            cameras.append(camera[0])
        if len(set(cameras)) != 1: raise ValueError('Camera moved')
        images = runtime / 'home/OpenJK/screenshots'
        data = np.asarray(Image.open(images / 'c01-a.tga').convert('RGB'))
        if data.std() < 2: raise ValueError('Flat capture')
        def compare(x, y): return R.compare(images / x, images / y, **R.L.IMAGE_TOLERANCE)
        controls = {name: compare('c01-a.tga', 'c01-' + name + '.tga') for name in ['aa', 'off', 'missing', 'restore']}
        if not all(row['passed'] for row in controls.values()): raise ValueError('Failed A/A/disabled control: ' + str(controls))
        results[version] = dict(controls=controls, camera=cameras[0],
                               renderer_sha256=hashlib.sha256(case.renderer.read_bytes()).hexdigest())
        paths[version] = images / 'c01-a.tga'
    if results['original']['camera'] != results['C01']['camera']:
        raise ValueError('Original/current cameras differ')
    result = dict(profile=a.profile, world_only=a.world_only, flavor='MP' if a.mp else 'SP', map=a.map,
                  controls=results, baseline_revision='89a40b0fc',
                  comparison=R.compare(paths['original'], paths['C01'], **R.L.IMAGE_TOLERANCE))
    (a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['comparison']['passed']: raise ValueError('Original/current disabled images differ')


if __name__ == '__main__':
    main()
