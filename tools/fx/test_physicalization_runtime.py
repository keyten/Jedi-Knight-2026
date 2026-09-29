#!/usr/bin/env python3
"""Optional Windows SP integration test with private installed assets.

Isolated home/output stays under build/. Does not update the installation.
Tests cold flags-off, live gates, authoring precedence and asset-hash fallback.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

FIXTURE = Path(__file__).with_name('physicalization_explosion.efx').read_text()


def run_case(root, installation, name, enabled, mismatch=False):
    home = root / name / 'home'
    base = home / 'OpenJK'
    effects = base / 'effects/test'
    effects.mkdir(parents=True, exist_ok=True)
    (effects/'stage2_explosion.efx').write_text(FIXTURE)
    shutil.copy2(root.parent/'msvc-all/RelWithDebInfo/jagamex86_64.dll', base/'jagamex86_64.dll')
    if mismatch:
        # Semantically identical private shader file with a different checksum.
        with zipfile.ZipFile(installation/'base/assets1.pk3') as archive:
            text = archive.read('shaders/effects.shader')
        (base/'shaders').mkdir(exist_ok=True)
        (base/'shaders/effects.shader').write_bytes(text + b'\n// runtime guard fixture\n')
    commands = ['wait 90', 'set developer 1', 'cam_disable', 'wait 8',
                'fxplay test/stage2_explosion 32', 'wait 30', 'set fx_freeze 1']
    for composite, adaptive in [(0, 0), (1, 0), (1, 1), (0, 1)]:
        commands += ['set fx_physicalizationComposite %d' % composite,
                     'set fx_physicalizationAdaptive %d' % adaptive, 'wait 8',
                     'echo STAGE2_GATE_%d_%d' % (composite, adaptive), 'r_volparticles']
    commands += ['set fx_physicalization 0', 'wait 8', 'echo STAGE2_MASTER_OFF',
                 'r_volparticles', 'echo STAGE2_DONE', 'quit']
    (base/'stage2.cfg').write_text('\n'.join(commands)+'\n')
    args = [str(installation/'openjk_sp.x86_64.exe')]
    settings = {'fs_basepath': str(installation), 'fs_homepath': str(home), 'fs_game': 'OpenJK',
                'cl_renderer': 'rdsp-rend2', 'r_fullscreen': '0', 'r_mode': '3', 's_initsound': '0',
                'r_glslCache': '1', 'r_normalMapping': '0', 'r_specularMapping': '0',
                'r_volumetricFog': '2', 'r_volumetricParticles': '1', 'r_volumetricFogFreeze': '0',
                'r_volumetricFogDebug': '0', 'r_volumetricParticlesDebug': '0', 'developer': '0',
                'fx_physicalization': '2', 'fx_physicalizationOptIn': 'test/stage2_explosion',
                'fx_physicalizationStrength': '8', 'fx_physicalizationComposite': str(int(enabled)),
                'fx_physicalizationAdaptive': str(int(enabled)), 'fx_physicalizationDebug': '1',
                'fx_physicalizationEmission': '0', 'fx_physicalizationAggregate': '0',
                'fx_freeze': '0', 'logfile': '2', 'com_maxfps': '60'}
    for key, value in settings.items():
        args += ['+set', key, value]
    args += ['+devmap', 't1_fatal', '+exec', 'stage2.cfg']
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 0
    process = subprocess.Popen(args, cwd=installation, startupinfo=info)
    try:
        code = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait()
        raise
    log = (base/'qconsole.log').read_text(errors='replace')
    assert code == 0 and 'STAGE2_DONE' in log, log[-2000:]
    assert 'fxplay: test/stage2_explosion' in log, 'SP game command unavailable'
    counts = [int(n) for n in re.findall(r'volumetric FX particles \(frame \d+\): (\d+) submitted', log)]
    assert len(counts) == 5, counts
    if enabled:
        assert counts[1] == counts[0]+1 and counts[2] == counts[1] and counts[3] == counts[0], counts
        pattern = 'composite 1 adaptive %d' % (0 if mismatch else 1)
        assert pattern in log, pattern
    else:
        assert counts[:4] == [counts[0]]*4, counts
        assert 'FX composition: effects/test/stage2_explosion.efx' not in log, 'cold off must not analyze'
        assert 'FX advanced: effects/test/stage2_explosion.efx' not in log, 'cold off must not analyze'
    # The fixture's authored medium survives all gates, including master off.
    assert counts[4] == counts[0] and counts[4] > 0, counts
    print('PASS %s: submitted %s; adaptive hash guard %s' % (name, counts, mismatch), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--installation', type=Path, required=True)
    args = ap.parse_args()
    if os.name != 'nt':
        ap.error('Windows SP runtime required')
    root = Path(__file__).resolve().parents[2]/'build/fx-stage2-runtime'
    for name, enabled, mismatch in [('cold-off', False, False), ('enabled', True, False), ('hash-mismatch', True, True)]:
        run_case(root, args.installation.resolve(), name, enabled, mismatch)


if __name__ == '__main__':
    main()
