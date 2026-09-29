#!/usr/bin/env python3
"""SP source-context integration in a clean home, using freshly built binaries."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def run(installation):
    repo = Path(__file__).resolve().parents[2]
    root = repo/'build/fx-stage4-runtime'
    game = root/'home/OpenJK'
    effects = game/'effects/test'
    effects.mkdir(parents=True, exist_ok=True)
    for name in ['physicalization_cluster.efx', 'physicalization_sources_smoke.efx']:
        shutil.copy2(repo/'tools/fx'/name, effects/name)
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/jagamex86_64.dll', game)
    binary = root/'bin'
    binary.mkdir(exist_ok=True)
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/openjk_sp.x86_64.exe', binary)
    shutil.copy2(repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll', binary)
    for p in installation.glob('SDL2*.dll'): shutil.copy2(p, binary)
    cfg = ['wait 90', 'set developer 1', 'cam_disable', 'wait 8',
           'echo SOURCES_OFF_BEGIN', 'fxplay test/physicalization_sources_smoke 64',
           'fxplay test/physicalization_sources_smoke 64', 'wait 20',
           'echo SOURCES_OFF', 'fxsources',
           'set fx_physicalizationSources 1', 'wait 24',
           'echo SOURCES_COLD_ENABLE', 'fxsources',
           'echo SOURCES_FRESH_BEGIN', 'fxplay test/physicalization_sources_smoke 64',
           'fxplay test/physicalization_sources_smoke 64', 'wait 90',
           'set fx_freeze 1', 'wait 4', 'echo SOURCES_FRESH', 'fxsources',
           'set fx_physicalizationSources 0', 'wait 4', 'echo SOURCES_DISABLED', 'fxsources',
           'set fx_physicalizationSources 1', 'wait 4', 'echo SOURCES_REENABLE', 'fxsources',
           'set fx_freeze 0', 'wait 4', 'echo SOURCES_NEW_GENERATION_BEGIN',
           'fxplay test/physicalization_cluster 64', 'fxplay test/physicalization_cluster 64',
           'wait 4', 'set fx_freeze 1', 'wait 4', 'echo SOURCES_NEW_GENERATION', 'fxsources',
           'echo SOURCES_DONE', 'quit']
    (game/'sources.cfg').write_text('\n'.join(cfg)+'\n', encoding='utf-8')
    settings = {'fs_basepath':str(installation), 'fs_homepath':str(root/'home'), 'fs_game':'OpenJK',
                'cl_renderer':'rdsp-rend2', 'r_mode':'3', 'r_fullscreen':'0', 's_initsound':'0',
                'r_glslCache':'1', 'r_normalMapping':'0', 'r_specularMapping':'0',
                'r_volumetricFog':'2', 'r_volumetricParticles':'1', 'r_volumetricEmission':'1',
                'r_volumetricFogDebug':'0', 'r_volumetricFogFreeze':'0',
                'fx_physicalization':'2', 'fx_physicalizationOptIn':'test/physicalization_cluster test/physicalization_sources_smoke',
                'fx_physicalizationComposite':'0', 'fx_physicalizationAdaptive':'0',
                'fx_physicalizationEmission':'0', 'fx_physicalizationAggregate':'0',
                'fx_physicalizationSources':'0', 'fx_physicalizationDebug':'1', 'fx_freeze':'0',
                'developer':'0', 'logfile':'2', 'com_maxfps':'60'}
    args = [str(binary/'openjk_sp.x86_64.exe')]
    for k,v in settings.items(): args += ['+set', k, v]
    args += ['+devmap', 't1_fatal', '+exec', 'sources.cfg']
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 0
    process = subprocess.Popen(args, cwd=binary, startupinfo=info)
    try:
        code = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(); raise
    log = (game/'qconsole.log').read_text(errors='replace')
    log = re.sub(r'(?m)^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ', '', log)
    assert code == 0 and 'SOURCES_DONE' in log, log[-2000:]

    def section(begin, end):
        return log.split('\n'+begin+'\n', 1)[1].split('\n'+end+'\n', 1)[0]

    def scene(text):
        m = re.search(r'FXSOURCE_SCENE\|world\|([\d|-]+)', text)
        assert m, text
        # generation, FX time, rows, density, glow, untracked, overflow
        return tuple(int(x) for x in m.group(1).split('|'))

    off = section('SOURCES_OFF_BEGIN', 'SOURCES_OFF')
    assert 'FXSOURCE|' not in off, off
    assert 'FX sources: tracking 0' in section('SOURCES_OFF', 'SOURCES_COLD_ENABLE')
    cold = scene(section('SOURCES_COLD_ENABLE', 'SOURCES_FRESH_BEGIN'))
    assert cold[2:5] == (0,0,0) and cold[5] > 0, cold
    fresh_text = section('SOURCES_FRESH', 'SOURCES_DISABLED')
    fresh = scene(fresh_text)
    rows = [tuple(int(x) for x in m) for m in re.findall(
        r'FXSOURCE_ROW\|world\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', fresh_text)]
    owners = {row[1] for row in rows}
    assert len(owners) == 2 and all(row[0] == fresh[0] and row[4] == 0 for row in rows), rows
    assert all({row[2] for row in rows if row[1] == owner} == {0,8} for owner in owners), rows
    traces_text = section('SOURCES_FRESH_BEGIN', 'SOURCES_FRESH')
    traces = [(path, *(int(x) for x in values)) for path, *values in re.findall(
        r'FXSOURCE\|([^|\r\n]+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', traces_text)]
    tracked = [r for r in traces if r[2]]
    assert {r[2] for r in tracked} == owners, tracked
    for owner in owners:
        parent = [r for r in tracked if r[2] == owner and 'sources_smoke' in r[0]]
        children = [r for r in tracked if r[2] == owner and 'cluster' in r[0]]
        assert len(parent) == 3 and len(children) >= 16, (parent, children)
    assert 'FX sources: tracking 0' in section('SOURCES_DISABLED','SOURCES_REENABLE')
    renewed = scene(section('SOURCES_REENABLE','SOURCES_NEW_GENERATION_BEGIN'))
    assert renewed[0] != fresh[0] and renewed[2:5] == (0,0,0) and renewed[5] > 0, renewed
    final = scene(section('SOURCES_NEW_GENERATION','SOURCES_DONE'))
    assert final[0] == renewed[0] and final[2:5] == (2,8,0) and final[6] == 0, final
    print('PASS source propagation: two independent bursts; delayed/runner/emitter/death children; physics scope; cold enable and generation reset.', flush=True)
    print('Last scene (generation, FX time, rows, density, glow, untracked, overflow):', final)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--installation', type=Path, required=True)
    run(ap.parse_args().installation.resolve())
