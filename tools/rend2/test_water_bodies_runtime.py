"""Validate water-body load and resolution in the SP renderer with installed PK3s.

Copies executables into a private build directory; the installation is read only.
Usage: python tools/rend2/test_water_bodies_runtime.py --installation <game dir>
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


MAPS = ('t2_rancor', 't2_trip', 't3_bounty', 't3_hevil', 'yavin1',
        'yavin1b', 'yavin2', 'vjun1', 't2_port', 'vjun2')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--maps', nargs='+', choices=MAPS, default=MAPS)
    parser.add_argument('--debug-draw', action='store_true')
    parser.add_argument('--ambient-waves', action='store_true', help='Initialize modern water with ambient waves on each map')
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--sdl', type=Path)
    parser.add_argument('--game', type=Path)
    args = parser.parse_args()
    maps = tuple(args.maps)
    if os.name != 'nt':
        parser.error('Windows renderer required')
    repo = Path(__file__).resolve().parents[2]
    root = repo/'build/water-bodies-runtime'
    base = root/'home/OpenJK'
    base.mkdir(parents=True, exist_ok=True)
    # A fresh log makes each assertion refer to this launch only.
    if (base/'qconsole.log').exists():
        (base/'qconsole.log').unlink()
    installation = args.installation.resolve()
    for name, source in (('openjk_sp.x86_64.exe', args.engine), ('SDL2.dll', args.sdl)):
        src = (source or installation/name).resolve()
        dst = (root/name).resolve()
        if src != dst:
            shutil.copy2(src, dst)
    shutil.copy2(repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll', root/'rdsp-rend2_x86_64.dll')
    game_src = (args.game or installation/'jagamex86_64.dll').resolve()
    game_dst = (base/'jagamex86_64.dll').resolve()
    if game_src != game_dst:
        shutil.copy2(game_src, game_dst)
    shutil.copy2(repo/'build/rend2/water-body-overlay.pk3', base/'zzzz_rend2_water_bodies.pk3')
    commands = ['wait 90']
    for i, map_name in enumerate(maps):
        if i:
            commands += ['map '+map_name, 'wait 90']
        commands += ['echo WATER_BODY_BEGIN_'+map_name, 'r_waterBodies',
                     'echo WATER_BODY_END_'+map_name,
                     'echo WATER_BODY_DUMP_'+map_name, 'r_waterBodies dump',
                     'echo WATER_BODY_DUMP_END_'+map_name]
        if args.debug_draw:
            commands += ['r_waterBodies draw', 'wait 5', 'r_waterBodies draw']
    commands += ['echo WATER_BODY_TEST_DONE', 'quit']
    (base/'waterbodies.cfg').write_text('\n'.join(commands)+'\n')
    settings = dict(fs_basepath=str(installation), fs_homepath=str(root/'home'),
                    fs_game='OpenJK', cl_renderer='rdsp-rend2', r_fullscreen='0',
                    r_mode='3', s_initsound='0', r_glslCache='1',
                    r_waterSurface='1' if args.ambient_waves else '0',
                    r_waterWaves='1' if args.ambient_waves else '0',
                    r_volumetricWater='0',
                    r_cubeMapping='0', r_diffuseIBL='0', developer='0',
                    logfile='2', com_maxfps='120')
    command = [str(root/'openjk_sp.x86_64.exe')]
    for key, value in settings.items():
        command += ['+set', key, value]
    command += ['+devmap', maps[0], '+exec', 'waterbodies.cfg']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (root/'launch.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=root, startupinfo=startup,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()
            raise
    log = (base/'qconsole.log').read_text(errors='replace')
    assert code == 0 and 'WATER_BODY_TEST_DONE' in log, log[-4000:]
    audit = json.loads((repo/'docs/rend2-water-body-stock-audit.json').read_text())
    for map_name in maps:
        match = re.search(r'WATER_BODY_BEGIN_'+map_name+r'(.*?)WATER_BODY_END_'+map_name,
                          log, re.S)
        assert match, f'missing {map_name} markers'
        count = len(audit['maps'][map_name+'.bsp']['bodies'])
        assert re.search(r'\b'+str(count)+r' water bodies\b', match.group(1)), \
            f'{map_name}: expected {count} bodies: {match.group(1)[-1000:]}'
        dump = re.search(r'WATER_BODY_DUMP_'+map_name+r'(.*?)WATER_BODY_DUMP_END_'+map_name,
                         log, re.S)
        assert dump, f'{map_name}: no dump'
        records = [json.loads(line[line.index('{'):]) for line in dump.group(1).splitlines()
                   if '{' in line and line.rstrip().endswith('}')]
        body_records = [record for record in records if record.get('type') == 'body']
        assert len(body_records) == count, \
            f'{map_name}: invalid JSON Lines dump'
        expected_profiles = {body['id']: body['dynamics_profile'] for body in audit['maps'][map_name+'.bsp']['bodies']}
        for record in body_records:
            assert record['dynamicsProfile'] == expected_profiles[record['id']], \
                f'{map_name} body {record["id"]}: unexpected dynamics profile'
        print(f'PASS {map_name}: {count} bodies')
    if any(any(body['decision_source'].startswith('explicit') for body in
               audit['maps'][map_name+'.bsp']['bodies']) for map_name in maps):
        assert 'explicit env.json' in log, 'project overlay was not applied'
    if args.debug_draw:
        assert 'r_waterBodies draw on' in log and 'r_waterBodies draw off' in log
    assert not re.search(r'GL_INVALID|GLSL shader compile error', log), log[-2000:]
    print('Log:', base/'qconsole.log')


if __name__ == '__main__':
    main()
