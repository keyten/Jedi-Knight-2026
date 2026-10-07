"""Optional Windows SP water integration test using privately installed assets.

The engine, renderer and game DLL are copied into build/water-fix-runtime;
the installation is read only. Checks actual BSP classification, water-only
SSR, queued uniform arrays, two-sided visibility and runtime overrides.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess


def read_tga(path):
    data = path.read_bytes()
    assert data[2] == 2 and data[16] == 24, 'Expected uncompressed RGB TGA'
    width, height = struct.unpack_from('<HH', data, 12)
    pixels = data[18 + data[0]:]
    assert len(pixels) == width * height * 3
    return width, height, pixels


def check_debug_images(directory):
    """Exercise queued uniform upload, which standalone GLSL tests bypass."""
    images = [read_tga(directory/name) for name in
              ('water-fix-classified.tga', 'water-fix-normals.tga')]
    assert images[0][:2] == images[1][:2]
    classified, normals = images[0][2], images[1][2]
    changed = 0
    for i in range(0, len(classified), 3):
        b, g, r = classified[i:i+3]
        # Classification is blue; wave normals are pale blue. Restrict the
        # comparison to those pixels so moving NPCs cannot satisfy the test.
        if b > r + 30 and b > g + 12:
            changed += max(abs(classified[i+j] - normals[i+j]) for j in range(3)) > 40
    assert changed >= 20, 'Water debug uniforms did not change classified pixels'
    print('PASS: queued water uniform array reaches GLSL; classified/normal views differ at', changed, 'pixels')
    pixels = read_tga(directory/'water-fix-underwater.tga')[2]
    # Tone mapping compresses the bright red/green difference of (1, .5, .1).
    orange = sum(r > g + 12 and g > b + 20 for b, g, r in zip(pixels[0::3], pixels[1::3], pixels[2::3]))
    assert orange >= 20, 'Camera did not render the water-to-air side of the interface'
    print('PASS: underside of interface visible at', orange, 'pixels')


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--renderer', type=Path,
                        default=repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll')
    parser.add_argument('--engine', type=Path, help='OpenJK executable when assets are in a separate directory')
    parser.add_argument('--sdl', type=Path, help='SDL2.dll when assets are in a separate directory')
    parser.add_argument('--game', type=Path)
    parser.add_argument('--map', choices=('t2_rancor', 't3_hevil', 'yavin1', 'vjun1'), default='t2_rancor')
    parser.add_argument('--half-res', choices=('0', '1'), default='0')
    parser.add_argument('--ambient-waves', action='store_true', help='Enable per-body ambient waves and capture their debug views')
    parser.add_argument('--flow', action='store_true', help='Enable directional flow and capture off/on and debug comparisons')
    parser.add_argument('--geometry', action='store_true', help='Enable render-only subdivision and print its map-load budget')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows SP runtime required')
    installation = args.installation.resolve()
    root = repo/'build/water-fix-runtime'
    home = root/'home'
    base = home/'OpenJK'
    base.mkdir(parents=True, exist_ok=True)
    for name, source in (('openjk_sp.x86_64.exe', args.engine), ('SDL2.dll', args.sdl)):
        src = (source or installation/name).resolve()
        dst = (root/name).resolve()
        if src != dst:
            shutil.copy2(src, dst)
    shutil.copy2(args.renderer, root/'rdsp-rend2_x86_64.dll')
    game_src = (args.game or installation/'jagamex86_64.dll').resolve()
    game_dst = (base/'jagamex86_64.dll').resolve()
    if game_src != game_dst:
        shutil.copy2(game_src, game_dst)

    lake = args.map == 't3_hevil'
    viewpoints = {
        't2_rancor': ('setviewpos -2520 5536 1480 0', 'setviewpos -2464 5536 1430 0',
                      'setviewpos -2464 5536 1438 90', 'textures/common/water_1'),
        't3_hevil': ('setviewpos 512 1152 -60 90', 'setviewpos 512 1152 -200 90',
                     'setviewpos 512 1152 -149 90', 'textures/h_evil/lakewater'),
        'yavin1': ('setviewpos 1024 -3900 210 90', 'setviewpos 1024 -3900 110 90',
                   'setviewpos 1024 -3900 130 90', 'textures/h_evil/lakewater'),
        'vjun1': ('setviewpos 4000 1800 430 90', 'setviewpos 4000 1800 300 90',
                  'setviewpos 4000 1800 346 90', 'textures/common/water2_water1_vjun1'),
    }
    above, below, grazing, shader = viewpoints[args.map]
    # SP setviewpos subtracts 25 but the player's eye height is 36. Leave
    # enough margin to put the actual camera below the deformed interface.
    commands = ['wait 90', 'cam_disable', 'wait 10', 'noclip', 'god',
                'give weapons', 'wait 20', 'weapon 5', 'wait 20',
                'set cg_thirdPerson 0', 'set cg_draw2D 0', 'set cg_drawGun 0',
                'set cg_thirdPersonAlpha 0', 'set cg_thirdPersonRange 0',
                'set cg_thirdPersonVertOffset 0',
                'set r_drawentities 0',
                'set con_notifytime 0',
                above, '+lookdown', 'wait 5' if lake else 'wait 16', '-lookdown',
                'wait 40', 'echo WATER_ABOVE', 'r_waterInfo surfaces',
                'screenshot water-fix-above',
                'r_waterSurfaceDebug 1', 'wait 10', 'screenshot_tga water-fix-classified',
                'r_waterSurfaceDebug 2', 'wait 10', 'screenshot_tga water-fix-normals',
                'r_waterSurfaceDebug 0', below,
                '+lookup', 'wait 16', '-lookup', 'wait 30', 'echo WATER_BELOW',
                'screenshot water-fix-below', 'r_waterInfo',
                'r_waterSnellDebug 1', 'wait 10', 'screenshot_tga water-fix-underwater',
                'r_waterSnellDebug 0',
                'r_waterOverride ' + shader + ' off', 'wait 10',
                'echo WATER_OVERRIDE_OFF', 'r_waterInfo', 'r_waterOverride clear',
                'wait 10', 'echo WATER_OVERRIDE_CLEAR', 'r_waterInfo',
                'r_waterBodies dump', 'echo WATER_FIX_DONE', 'quit']
    if args.ambient_waves:
        captures = ['r_waterWaveTime 4', 'r_waterWaveDebug 1', 'wait 8',
                    'screenshot_tga water-wave-height', 'r_waterWaveDebug 4', 'wait 8',
                    'screenshot_tga water-wave-combined', 'r_waterWaveDebug 5', 'wait 8',
                    'screenshot_tga water-wave-profile', 'r_waterWaveDebug 8', 'wait 8',
                    'screenshot_tga water-wave-split', 'r_waterWaveDebug 0', 'r_waterWaveTime -1']
        commands[commands.index('r_waterSurfaceDebug 0')] += '\n' + '\n'.join(captures)
    if args.flow:
        flow_captures = ['r_waterWaveTime 4', 'r_waterFlow 0', 'wait 8', 'screenshot_tga water-flow-off',
                         'r_waterFlow 1', 'wait 8', 'screenshot_tga water-flow-on',
                         'r_waterFlowDebug 1', 'wait 8', 'screenshot_tga water-flow-direction',
                         'r_waterFlowDebug 3', 'wait 8', 'screenshot_tga water-flow-source',
                         'r_waterFlowDebug 4', 'wait 8', 'screenshot_tga water-flow-detail',
                         'r_waterFlowDebug 0', 'r_waterWaveTime -1']
        flow_reset = next(i for i, command in enumerate(commands) if command.startswith('r_waterSurfaceDebug 0'))
        commands[flow_reset] += '\n' + '\n'.join(flow_captures)
    if args.geometry:
        commands.insert(commands.index('r_waterBodies dump'), 'r_waterGeometryInfo')
        debug_reset = next(i for i, command in enumerate(commands)
                           if command.startswith('r_waterSurfaceDebug 0'))
        commands[debug_reset] += '\n' + '\n'.join([
            grazing, 'centerview', 'wait 40',
            'r_waterWaveAmplitude 4', 'r_waterWaveTime 4', 'wait 12',
            'screenshot_tga water-geometry-grazing',
            'r_waterGeometryDebug 1', 'wait 8', 'screenshot_tga water-geometry-wire',
            'r_waterGeometryDebug 2', 'wait 8', 'screenshot_tga water-geometry-magnitude',
            'r_waterGeometryDebug 3', 'wait 8', 'screenshot_tga water-geometry-original',
            'r_waterGeometryDebug 4', 'wait 8', 'screenshot_tga water-geometry-seams',
            'r_waterGeometryDebug 0', 'r_waterWaveAmplitude 1', 'r_waterWaveTime -1'])
    (base/'waterfix.cfg').write_text('\n'.join(commands)+'\n')
    settings = dict(fs_basepath=str(installation), fs_homepath=str(home), fs_game='OpenJK',
                    cl_renderer='rdsp-rend2', r_fullscreen='0', r_mode='3', s_initsound='0',
                    r_glslCache='1', r_normalMapping='0', r_parallaxMapping='0',
                    r_specularMapping='0', r_pomSilhouette='0', r_diffuseIBL='0',
                    r_waterSurface='1', r_waterSnell='1', r_waterSurfaceDebug='0',
                    r_waterWaves='1' if args.ambient_waves else '0',
                    r_waterFlow='1' if args.flow else '0',
                    r_waterGeometry='1' if args.geometry else '0',
                    r_ssr='0', r_ssrHalfRes=args.half_res, r_cubeMapping='0',
                    r_volumetricFog='2', r_volumetricWater='1',
                    developer='0', logfile='2', com_maxfps='60')
    command = [str(root/'openjk_sp.x86_64.exe')]
    for key, value in settings.items():
        command += ['+set', key, value]
    command += ['+devmap', args.map, '+exec', 'waterfix.cfg']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (root/'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=root, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()
            raise
    log = (base/'qconsole.log').read_text(errors='replace')
    assert code == 0 and 'WATER_FIX_DONE' in log, f'exit code {code}: {log[-3000:]}'
    if args.map in ('t2_rancor', 't3_hevil'):
        expected = r'lakewater\s+water\s+9.*linked\s+7' if lake else r'water_1\s+water\s+6.*linked\s+6'
        assert re.search(expected, log), 'Missing coplanar brush links'
    states = re.findall(r'last frame: (\d+) water draws, view (\w+), SSR (\w+)', log)
    if args.map in ('t2_rancor', 't3_hevil'):
        assert len(states) == 4, states
        assert all(int(states[i][0]) > 0 and states[i][1:] == ('yes', 'yes') for i in (0, 1, 3)), states
        assert states[2] == ('0', 'no', 'no'), states
    assert not re.search(r'GL_INVALID|Could not load watersurface|GLSL shader compile error', log), log[-3000:]
    for name in ('water-fix-above.jpg', 'water-fix-below.jpg'):
        assert (base/'screenshots'/name).stat().st_size > 1000, name
    if args.map in ('t2_rancor', 't3_hevil'):
        check_debug_images(base/'screenshots')
    if args.geometry:
        assert re.search(r'water geometry: \d+ bodies,', log), 'Missing geometry statistics'
        assert any(int(n) > 0 for n in re.findall(r'body \d+: (\d+) vertices', log)), 'No water mesh uploaded'
        for name in ('water-geometry-grazing.tga', 'water-geometry-wire.tga',
                     'water-geometry-magnitude.tga', 'water-geometry-original.tga'):
            assert (base/'screenshots'/name).stat().st_size > 1000, name
    if args.flow:
        for name in ('water-flow-off.tga', 'water-flow-on.tga', 'water-flow-direction.tga',
                     'water-flow-source.tga', 'water-flow-detail.tga'):
            assert (base/'screenshots'/name).stat().st_size > 1000, name
    if args.map in ('t2_rancor', 't3_hevil'):
        print('PASS:', args.map, 'stock interface classification; water-only SSR above/below; overrides off/clear')
    else:
        print('PASS:', args.map, 'water geometry loaded and grazing/debug captures produced')
    print('Log:', base/'qconsole.log')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
