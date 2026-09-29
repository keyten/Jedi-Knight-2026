#!/usr/bin/env python3
"""SP integration of opt-in emission, exact coalescing and asset-hash fallback.

Uses a private unchanged stock rocket EFX in isolated ignored homepaths to avoid
the installation's authored rocket override. No installation changes.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def run(root, installation, mismatch=False, authored_budget=False):
    repo = Path(__file__).resolve().parents[2]
    game = root/'home/OpenJK'
    (game/'effects/test').mkdir(parents=True, exist_ok=True)
    (game/'effects/rocket').mkdir(exist_ok=True)
    shutil.copy2(repo/'tools/fx/physicalization_cluster.efx', game/'effects/test/physicalization_cluster.efx')
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/jagamex86_64.dll', game)
    with zipfile.ZipFile(installation/'base/assets1.pk3') as archive:
        (game/'effects/rocket/explosion.efx').write_bytes(archive.read('effects/rocket/explosion.efx'))
        if mismatch:
            (game/'shaders').mkdir(exist_ok=True)
            (game/'shaders/explosions.shader').write_bytes(archive.read('shaders/explosions.shader') + b'\n// deliberate guard mismatch\n')
    if authored_budget:
        (game/'effects/test/authored_glows.efx').write_text('particle\n{\n count 23\n life 5000\n shader gfx/misc/steam\n size\n {\n  start 24\n  end 24\n }\n volumetricMedia\n {\n  extinction 0\n  emissive 1 0.4 0.1\n  emissiveDensity 0.01\n }\n}\n')
    binary = root/'bin'
    binary.mkdir(exist_ok=True)
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/openjk_sp.x86_64.exe', binary)
    shutil.copy2(repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll', binary)
    for p in installation.glob('SDL2*.dll'): shutil.copy2(p, binary)
    cfg = ['wait 90', 'set developer 1', 'cam_disable',
           'wait 8', 'fxplay test/physicalization_cluster 32', 'fxplay rocket/explosion 160', 'fxplay rocket/explosion 160',
           'set fx_physicalizationEmission 0', 'wait 8', 'set fx_freeze 1']
    for step, (emission, aggregate) in enumerate([(0,0),(1,0),(1,1),(0,1),(0,0)]):
        cfg += ['set fx_physicalizationEmission %d' % emission,
                'set fx_physicalizationAggregate %d' % aggregate, 'wait 8',
                'echo STAGE3_%d_%d' % (emission,aggregate), 'r_volparticles']
        if not mismatch and not authored_budget and step in (0,1):
            cfg += ['set con_notifytime 0', 'set r_volumetricFogDebug 33', 'wait 8',
                    'screenshot_png stage3-emission-'+('on' if emission else 'off'),
                    'wait 8', 'set r_volumetricFogDebug 0', 'wait 8']
    cfg += ['set fx_physicalization 0', 'wait 8', 'echo STAGE3_MASTER_OFF',
            'r_volparticles']
    if authored_budget:
        cfg += ['set fx_freeze 0', 'wait 4', 'fxplay test/authored_glows 32', 'wait 2', 'set fx_freeze 1', 'wait 8', 'echo STAGE3_AUTHORED', 'r_volparticles',
                'set fx_physicalization 2', 'set fx_physicalizationEmission 1', 'wait 8',
                'echo STAGE3_AUTHORED_AND_AUTO', 'r_volparticles']
    cfg += ['echo STAGE3_DONE', 'quit']
    (game/'stage3.cfg').write_text('\n'.join(cfg)+'\n')
    settings = {'fs_basepath':str(installation),'fs_homepath':str(root/'home'),'fs_game':'OpenJK',
                'cl_renderer':'rdsp-rend2','r_mode':'3','r_fullscreen':'0','s_initsound':'0',
                'r_glslCache':'1','r_normalMapping':'0','r_specularMapping':'0',
                'r_volumetricFog':'2','r_volumetricParticles':'1', 'r_volumetricEmission':'1',
                'r_volumetricFogDebug':'0','r_volumetricParticlesDebug':'0','r_volumetricFogFreeze':'0',
                'fx_physicalization':'2','fx_physicalizationOptIn':'test/physicalization_cluster rocket/explosion',
                'fx_physicalizationEmission':'1','fx_physicalizationAggregate':'0',
                'fx_physicalizationComposite':'0','fx_physicalizationAdaptive':'0',
                'fx_physicalizationSources':'0', 'fx_physicalizationDebug':'1','fx_freeze':'0','developer':'0','logfile':'2','com_maxfps':'60'}
    args = [str(binary/'openjk_sp.x86_64.exe')]
    for k,v in settings.items(): args += ['+set',k,v]
    args += ['+devmap','t1_fatal','+exec','stage3.cfg']
    info = subprocess.STARTUPINFO(); info.dwFlags |= subprocess.STARTF_USESHOWWINDOW; info.wShowWindow=0
    process = subprocess.Popen(args,cwd=binary,startupinfo=info)
    try:
        code = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(); raise
    log = (game/'qconsole.log').read_text(errors='replace')
    assert code == 0 and 'STAGE3_DONE' in log, log[-2000:]
    stats = [tuple(int(x) for x in m) for m in re.findall(
        r'volumetric FX particles \(frame \d+\): (\d+) submitted, \d+ rejected, \d+ culled, \d+ capped, (\d+) uploaded \(\d+ changed, \d+ vanished, (\d+)/\d+ emissive, (\d+) glows dropped',log)]
    folded = [int(x) for x in re.findall(r'automatic exact coalescing: (\d+) inputs folded',log)]
    assert len(stats)==(8 if authored_budget else 6) and len(folded)==len(stats), (stats,folded)
    assert folded[:6] == [0,0,3,3,0,0], folded
    assert stats[0][0] == stats[4][0] and stats[1][0] == stats[2][0],stats
    assert stats[2][1] == stats[1][1]-3 and stats[3][1] == stats[0][1]-3,stats
    if mismatch:
        assert stats[1][0] == stats[0][0] and stats[1][2] == 0,stats
        assert 'emission 1' not in '\n'.join(s for s in log.splitlines() if 'FX advanced: effects/rocket/explosion.efx' in s)
    else:
        assert stats[1][0] > stats[0][0]+4 and stats[1][2] == 4 and stats[1][3] > 0,stats
        assert 'FX advanced: effects/rocket/explosion.efx' in log and 'emission 1' in log
    assert stats[5][0] == stats[0][0]-4 and stats[5][2] == 0,stats
    if authored_budget:
        assert stats[6][2] == 23 and stats[7][2] == 24 and stats[7][3] > 0,stats
    print('PASS stage3 hash-mismatch=%s: (submitted, uploaded, emissive, dropped)=%s; folded=%s' % (mismatch,stats,folded),flush=True)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--installation',type=Path,required=True)
    ap.add_argument('--case',choices=['all','enabled','hash-mismatch','authored-budget'],default='all')
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[2]/'build/fx-stage3-runtime'
    for name, mismatch, budget in [('enabled',False,False),('hash-mismatch',True,False),('authored-budget',False,True)]:
        if args.case in ('all',name): run(root/name,args.installation.resolve(),mismatch,budget)


if __name__=='__main__': main()
