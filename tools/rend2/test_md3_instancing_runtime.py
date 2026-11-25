"""Private SP repeated-MD3 A/B with real renderer calls, default fast prepass.

Installed assets and existing engine/game are read only. A private entity override
adds rendered misc_model_static MD3s rather than baked BSP decor. Captures freeze
UI, FX and shader time. Timings, when requested, require foreign games absent.
"""
import argparse,json,re,shutil,subprocess
from pathlib import Path
from test_character_cull_runtime import foreign_games
ROOT=Path(__file__).resolve().parents[2]

def prepare(a):
    run=ROOT/'build/md3-runtime'/a.label
    home=run/'home/OpenJK';(home/'maps/mp').mkdir(parents=True,exist_ok=True)
    (home/'ui').mkdir(exist_ok=True)
    (home/'ui/ingame.txt').write_text('loadmenu { "ui/md3-freeze.menu" }\n')
    (home/'ui/md3-freeze.menu').write_text('menuDef { name "md3_freeze" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\nmenuDef { name "md3_resume" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { delay 1 ; uiScript closeingame ; } }\n')
    entities=['{ "classname" "worldspawn" }','{ "classname" "info_player_start" "origin" "671 -369 -70" "angle" "270" }']
    for i in range(40):
        x=431+(i%8)*64;y=-669-(i//8)*96
        scale=1+(i%3)*.125
        entities.append('{ "classname" "misc_model_static" "model" "models/map_objects/factory/barrel.md3" '
            f'"origin" "{x} {y} -128" "angles" "0 {(i%4)*30} 0" "modelscale" "{scale}" "zoffset" "{i%3*10}"'+' }')
    # Ineligible transforms stay individual, and must still match the reference.
    for i,scale in enumerate(('1 1.5 1','-1 1 1')):
        entities.append('{ "classname" "misc_model_static" "model" "models/map_objects/factory/barrel.md3" '
            f'"origin" "{527+i*128} -569 -128" "modelscale_vec" "{scale}"'+' }')
    (home/'maps/mp/ffa3.ent').write_text('\n'.join(entities)+'\n')
    for name in ('openjk_sp.x86_64.exe','jagamex86_64.dll'):
        shutil.copy2(a.base_binaries/name,run/name)
    shutil.copy2(a.base_binaries/'jagamex86_64.dll',home/'jagamex86_64.dll')
    shutil.copy2(a.renderer,run/'rdsp-rend2_x86_64.dll')
    shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll',run/'SDL2.dll')
    settings=dict(cl_renderer='rdsp-rend2',r_mode=-1,r_customwidth=640,r_customheight=360,r_fullscreen=0,
        r_offscreenPresent=1,r_swapInterval=0,com_maxfps=60,com_maxfpsUnfocused=60,com_maxfpsMinimized=60,
        com_bootlogo=0,in_mouse=0,s_initsound=0,logfile=2,developer=0,r_gl43=a.gl43,
        cg_thirdPerson=0,cg_draw2D=0,cg_drawGun=0,cg_shadows=0,r_picmip=2,r_shaderProgramLazy=1,
        r_shaderCompileProcesses=0,r_captureShaderTime=0,r_autoExposure=0,r_depthPrepass=1,r_depthPrepassFast=1,
        r_md3Instancing=0,r_md3InstancingStats=1,r_ssr=0,r_ssgi=0,r_smaa=0,r_motionBlur=0,
        r_volumetricFog=0,r_volumetricWater=0,r_waterSurface=0,r_atmosphere=0,r_clouds=0,r_autoGrass=0,
        r_ssao=0,r_aoMode=0,r_bloom=0,r_skinSSS=0,r_sunlightMode=0,r_sunShadows=0,r_phongTessellation=0,
        r_shadows=0,r_speeds=0,con_notifytime=-1,r_characterCull=1)
    commands=['wait 60','devmap mp/ffa3','wait 180','god','notarget','noclip','setviewpos 655 -369 -70 270',
        'wait 30','uimenu md3_freeze','fx_freeze 1','wait 20']
    def phase(name,enabled):
        return [f'r_md3Instancing {enabled}',f'echo MD3_BEGIN_{name}','wait 120',
                f'screenshot_tga md3_{name}',f'echo MD3_END_{name}']
    commands+=phase('off',0)+phase('on',1)+phase('repeat',0)
    # Camera changes and velocity/depth fallback must preserve the same frame.
    commands+=['r_motionBlur 1','r_motionBlurShutterAngle 0','wait 30']
    commands+=phase('velocity_off',0)+phase('velocity_on',1)
    commands+=['uimenu md3_resume','wait 10','setviewpos 687 -400 -70 270','wait 20','uimenu md3_freeze','wait 10','r_depthPrepassFast 0','r_motionBlur 0','wait 30']
    commands+=phase('legacy_off',0)+phase('legacy_on',1)
    if a.benchmark:
        commands+=['r_depthPrepassFast 1','r_md3InstancingStats 0','com_maxfps 0','com_maxfpsUnfocused 0','com_maxfpsMinimized 0']
        for i,enabled in enumerate((0,1,1,0)):
            commands+=[f'r_md3Instancing {enabled}','wait 120',f'echo MD3_BENCH_BEGIN_{i}_{enabled}',
                       'r_sunShadowBenchmark 180','wait 220',f'echo MD3_BENCH_END_{i}_{enabled}']
    commands+=['echo MD3_RUNTIME_DONE','wait 10','quit']
    (home/'autoexec_sp.cfg').write_text('\n'.join([f'set {k} "{v}"' for k,v in settings.items()]+commands)+'\n')
    return run,home,[str(run/'openjk_sp.x86_64.exe'),'+set','fs_basepath',str(a.installation),'+set','fs_homepath',str(run/'home'),'+set','fs_game','OpenJK']

def analyze(run,home,a):
    from PIL import Image
    log='\n'.join(p.read_text(errors='replace') for p in home.glob('*.log'))
    assert 'MD3_RUNTIME_DONE' in log,'run incomplete'
    assert not re.search(r'GLSL.*(?:failed|error)|FBO.*incomplete',log,re.I)
    phases={}
    for name,body in re.findall(r'MD3_BEGIN_(\w+)(.*?)MD3_END_\1',log,re.S):
        counts=[tuple(map(int,v)) for v in re.findall(r'md3Instancing: tagged (\d+), fallback (\d+), groups (\d+), instances (\d+), saved (\d+) draws, overflow (\d+), upload (\d+) bytes',body)]
        phases[name]=counts
    if a.gl43:assert any(c[4]>0 for name in ('on','velocity_on','legacy_on') for c in phases[name]),phases
    else:assert all(c[4]==0 for counts in phases.values() for c in counts)
    images={p.stem:Image.open(p).convert('RGB') for p in (home/'screenshots').glob('md3_*.tga')}
    differences={}
    for left,right in [('off','on'),('off','repeat'),('velocity_off','velocity_on'),('legacy_off','legacy_on')]:
        ia=images['md3_'+left];ib=images['md3_'+right]
        assert ia.getbbox(),'empty capture'
        differences[left+'/'+right]=sum(a!=b for a,b in zip(ia.tobytes(),ib.tobytes()))
        assert differences[left+'/'+right]==0,(left,right,differences)
    report=dict(phases=phases,differing_channels=differences,benchmarks=re.findall(r'MD3_BENCH_BEGIN_(\d+)_(\d+)(.*?)MD3_BENCH_END_\1_\2',log,re.S))
    (run/'results.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation',type=Path,default=Path(r'C:/Users/Keyten/Desktop/projects/OpenJK/build-rend2'))
    p.add_argument('--base-binaries',type=Path,default=Path(r'C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/msvc/Release'))
    p.add_argument('--renderer',type=Path,default=ROOT/'build/md3-msvc/Release/rdsp-rend2_x86_64.dll')
    p.add_argument('--label',default='intel');p.add_argument('--gl43',type=int,default=1)
    p.add_argument('--benchmark',action='store_true');p.add_argument('--run',action='store_true');p.add_argument('--analyze-only',action='store_true')
    a=p.parse_args();run,home,launch=prepare(a)
    if a.run:
        foreign=foreign_games();assert not foreign,foreign
        (run/'run-meta.json').write_text(json.dumps(dict(foreign_before=foreign,launch=launch),indent=2))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        with (run/'launch.log').open('w') as output:
            process=subprocess.Popen(launch,cwd=run,startupinfo=startup,stdout=output,stderr=output)
            try:code=process.wait(timeout=600)
            except subprocess.TimeoutExpired:process.terminate();raise
        assert code==0,code
        analyze(run,home,a)
    elif a.analyze_only:analyze(run,home,a)
    else:print('Prepared',run)
if __name__=='__main__':main()
