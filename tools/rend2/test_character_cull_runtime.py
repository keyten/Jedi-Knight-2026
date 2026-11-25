"""Private SP body-culling correctness/counter and optional full-frame A/B runner.

Requires own renderer/game build and installed assets (read only). Stages a private
home and binaries; --run is explicit, foreign OpenJK processes block a run. Never
terminates foreign games. Capture correctness is separate from uncapped timing.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def foreign_games():
    cmd = "Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(openjk|openjo|jasp|jamp).*\\.exe$' } | Select-Object ProcessId,Name,ExecutablePath | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell', '-NoProfile', '-Command', cmd], capture_output=True, text=True, check=True)
    value = json.loads(result.stdout) if result.stdout.strip() else []
    return value if isinstance(value, list) else [value]

def prepare(args):
    runtime = ROOT / 'build/character-cull-runtime' / args.label
    home = runtime / 'home/OpenJK'
    (home / 'ui').mkdir(parents=True, exist_ok=True)
    (home / 'ui/ingame.txt').write_text('loadmenu { "ui/cull-freeze.menu" }\n')
    (home / 'ui/cull-freeze.menu').write_text('menuDef { name "cull_freeze" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\n'
        'menuDef { name "cull_resume" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { delay 1 ; uiScript closeingame ; } }\n')
    for name in ('openjk_sp.x86_64.exe', 'rdsp-rend2_x86_64.dll', 'jagamex86_64.dll'):
        shutil.copy2(args.binaries / name, runtime / name)
    shutil.copy2(args.binaries / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    helper = args.binaries / 'rend2_glslc.exe'
    if helper.exists(): shutil.copy2(helper, runtime / helper.name)
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    settings = dict(cl_renderer='rdsp-rend2', r_mode=-1, r_customwidth=640, r_customheight=360,
        r_fullscreen=0, r_swapInterval=0, com_maxfps=60, com_maxfpsUnfocused=60,
        com_maxfpsMinimized=60, com_bootlogo=0, in_mouse=0, s_initsound=0,
        logfile=2, developer=0, r_gl43=args.gl43, r_offscreenPresent=1,
        cg_thirdPerson=0, cg_draw2D=0, cg_drawGun=0, cg_shadows=0,
        r_hdr=1, r_picmip=2, r_shaderProgramLazy=1, r_shaderCompileProcesses=0,
        r_captureShaderTime=0, r_autoExposure=0, r_characterCull=0, r_characterCullStats=2,
        r_characterCullDistance=256, r_depthPrepass=1, r_depthPrepassFast=1,
        r_ssr=0, r_ssgi=0, r_smaa=0, r_motionBlur=int(args.velocity),
        r_motionBlurShutterAngle=0, r_volumetricFog=0, r_volumetricWater=0,
        r_waterSurface=0, r_atmosphere=0, r_clouds=0, r_autoGrass=0,
        r_ssao=0, r_aoMode=0, r_bloom=0, r_skinSSS=0,
        r_sunlightMode=1 if args.shadows else 0, r_sunShadows=int(args.shadows),
        r_forceSun=int(args.shadows), r_dlightMode=1, r_shadows=0,
        r_sunShadowMode=1 if args.shadows else 0, r_sunShadowCache=0,
        r_speeds=0, r_phongTessellation=0, con_notifytime=-1,
        cg_bobup=0, cg_bobpitch=0, cg_bobroll=0, cg_runpitch=0, cg_runroll=0)
    commands = ['wait 90', 'devmap mp/ffa3', 'wait 180', 'exitview', 'cam_disable',
        'lock_disable', 'god', 'notarget', 'noclip', 'cg_thirdPerson 0',
        'setviewpos 671 -769 -70 290', 'wait 20']
    for index,npc in enumerate(('stormtrooper', 'stormtrooper', 'reborn', 'boba_fett', 'rockettrooper2', 'atst')):
        commands += [f'setviewpos {431+index*100} -769 -70 290', 'wait 20', f'npc spawn {npc}', 'wait 60']
    commands += ['setviewpos 671 -369 -70 90', 'wait 30', 'uimenu cull_freeze', 'fx_freeze 1', 'wait 30']
    def phase(name, enabled):
        return [f'r_characterCull {enabled}', f'echo CHAR_PHASE_BEGIN_{name}', 'wait 600',
            'r_characterCullStats 0', 'wait 4', f'screenshot_tga char_{name}',
            f'echo CHAR_PHASE_END_{name}', 'r_characterCullStats 2']
    commands += phase('far_off',0) + phase('far_on',1) + phase('far_repeat',0)
    if args.benchmark:
        commands += ['r_characterCullStats 0', 'com_maxfps 0', 'com_maxfpsUnfocused 0', 'com_maxfpsMinimized 0']
        for index, enabled in enumerate((0,1,1,0)):
            commands += [f'r_characterCull {enabled}', 'wait 120', f'echo CHAR_BENCH_BEGIN_{index}_{enabled}',
                'r_sunShadowBenchmark 360', 'wait 400', f'echo CHAR_BENCH_END_{index}_{enabled}']
        commands += ['com_maxfps 60', 'com_maxfpsUnfocused 60', 'com_maxfpsMinimized 60', 'r_characterCullStats 2']
    for name, position in [('visible','671 -369 -70 270'),('near','671 -669 -70 90')]:
        commands += ['uimenu cull_resume', 'wait 10', f'setviewpos {position}', 'wait 30', 'uimenu cull_freeze', 'wait 30']
        commands += phase(name+'_off',0) + phase(name+'_on',1)
    commands += ['echo CHARACTER_CULL_RUNTIME_DONE', 'wait 10', 'quit']
    (home / 'autoexec_sp.cfg').write_text('\n'.join([f'set {key} "{value}"' for key,value in settings.items()] + commands)+'\n')
    launch = [str(runtime / 'openjk_sp.x86_64.exe'), '+set', 'fs_basepath', str(args.installation.resolve()),
        '+set', 'fs_homepath', str(runtime / 'home'), '+set', 'fs_game', 'OpenJK']
    return runtime, home, launch

def analyze(runtime, home, args):
    logs=list(home.glob('*.log'))
    log='\n'.join(p.read_text(errors='replace') for p in logs)
    assert 'CHARACTER_CULL_RUNTIME_DONE' in log, 'runtime did not complete'
    assert not re.search(r'GLSL.*(?:failed|error)|FBO.*incomplete|VM_Create.*failed',log,re.I)
    phases={}
    for name,body in re.findall(r'CHAR_PHASE_BEGIN_(\w+)(.*?)CHAR_PHASE_END_\1',log,re.S):
        counts=[tuple(map(int,m)) for m in re.findall(r'characterCull: (\d+)/(\d+) bodies',body)]
        entries=[(int(flags,16),int(skipped)) for flags,skipped in re.findall(r'characterCull body \d+: flags ([0-9a-fA-F]+), origin [^\n]+, skipped (\d+)',body)]
        assert counts,(name,'no policy counters')
        assert all(not skipped for flags,skipped in entries if flags & 0x30000000),(name,'protected body culled')
        phases[name]=dict(counts=counts,protected=sum(bool(flags&0x30000000) for flags,_ in entries))
    assert any(a>0 for a,b in phases['far_on']['counts']), 'no distant body reached the enabled policy'
    assert all(a==0 for a,b in phases['visible_on']['counts']), 'turn-back view did not restore characters'
    assert phases['far_on']['protected']>0, 'no protected body reached policy'
    for name in ('far_off','far_repeat','visible_off','near_off'):
        assert all(a==0 for a,b in phases[name]['counts']),(name,'disabled still culls')
    from PIL import Image
    images={p.stem:Image.open(p).convert('RGB') for p in (home/'screenshots').glob('char_*.tga')}
    diffs={}
    for a,b in [('far_off','far_on'),('far_off','far_repeat'),('visible_off','visible_on'),('near_off','near_on')]:
        ia,ib=images['char_'+a],images['char_'+b]
        assert ia.getbbox(), 'black/empty capture'
        ba,bb=ia.tobytes(),ib.tobytes()
        diffs[a+'/'+b]=sum(x!=y for x,y in zip(ba,bb))
    assert diffs['visible_off/visible_on']==0, 'visible body changed'
    assert diffs['near_off/near_on']==0, 'nearby body changed'
    if not args.shadows:
        assert diffs['far_off/far_on']==0, 'offscreen body changed color without shadows'
    benchmarks=[]
    for index,mode,body in re.findall(r'CHAR_BENCH_BEGIN_(\d+)_(\d+)(.*?)CHAR_BENCH_END_\1_\2',log,re.S):
        match=re.search(r'sunShadowBenchmark: (\d+) frames, mean ([\d.]+) ms, median ([\d.]+) ms, p95 ([\d.]+) ms',body)
        assert match,('missing frame benchmark',index)
        benchmarks.append(dict(index=int(index),enabled=int(mode),frames=int(match[1]),mean_ms=float(match[2]),median_ms=float(match[3]),p95_ms=float(match[4])))
    metadata=json.loads((runtime/'run-meta.json').read_text()) if (runtime/'run-meta.json').exists() else {}
    result=dict(phases=phases,capture_channel_differences=diffs,benchmarks=benchmarks,run_metadata=metadata)
    (runtime/'results.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation',type=Path,required=True)
    parser.add_argument('--binaries',type=Path,default=ROOT/'build/msvc/Release')
    parser.add_argument('--label',default='correctness')
    parser.add_argument('--gl43',type=int,choices=(0,1),default=0)
    parser.add_argument('--velocity',action='store_true')
    parser.add_argument('--shadows',action='store_true')
    parser.add_argument('--benchmark',action='store_true')
    parser.add_argument('--allow-foreign-correctness',action='store_true',help='Permit non-isolated functional checks only; forbidden with --benchmark')
    parser.add_argument('--run',action='store_true',help='Otherwise only stage the private run')
    parser.add_argument('--analyze-only',action='store_true')
    args=parser.parse_args()
    runtime,home,launch=prepare(args)
    if args.run:
        foreign=foreign_games()
        if foreign and (args.benchmark or not args.allow_foreign_correctness):
            raise RuntimeError(f'Foreign games prevent an isolated run: {foreign}')
        (runtime/'run-meta.json').write_text(json.dumps(dict(foreign_before=foreign,performance_isolated=not foreign),indent=2))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        with (runtime/'launch.log').open('w') as output:
            process=subprocess.Popen(launch,cwd=runtime,startupinfo=startup,stdout=output,stderr=output)
            try: code=process.wait(timeout=900)
            except subprocess.TimeoutExpired:
                process.terminate();raise
            assert code==0,code
        metadata=json.loads((runtime/'run-meta.json').read_text())
        metadata['foreign_after']=foreign_games()
        metadata['performance_isolated']=metadata['performance_isolated'] and not metadata['foreign_after']
        (runtime/'run-meta.json').write_text(json.dumps(metadata,indent=2))
        analyze(runtime,home,args)
    elif args.analyze_only: analyze(runtime,home,args)
    else: print(f'Prepared {runtime}; add --run to launch the private hidden process')
