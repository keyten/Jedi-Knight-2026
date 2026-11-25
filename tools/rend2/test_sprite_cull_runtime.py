"""Private Yavin grass culling/LOD correctness, counters and GPU timing runner.

Stages its own binaries/home; installed game assets are read only. Uses a UI
pause (not timescale 0) and the capture shader clock for repeatable vegetation.
No other OpenJK game may run. --run launches a hidden owned process only.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import test_character_cull_runtime as harness

ROOT = Path(__file__).resolve().parents[2]


def prepare(args):
    runtime = ROOT / 'build/sprite-cull-runtime' / args.label
    home = runtime / 'home/OpenJK'
    (home/'ui').mkdir(parents=True,exist_ok=True)
    (home/'ui/ingame.txt').write_text('loadmenu { "ui/sprite-freeze.menu" }\n')
    (home/'ui/sprite-freeze.menu').write_text('menuDef { name "sprite_freeze" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\n'
        'menuDef { name "sprite_resume" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { delay 1 ; uiScript closeingame ; } }\n')
    for name in ('openjk_sp.x86_64.exe','jagamex86_64.dll'):
        shutil.copy2(args.binaries/name,runtime/name)
    shutil.copy2(args.binaries/'jagamex86_64.dll',home/'jagamex86_64.dll')
    for name in ('rdsp-rend2_x86_64.dll','rend2_glslc.exe'):
        shutil.copy2(args.renderer/name,runtime/name)
    shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll',runtime/'SDL2.dll')
    settings = dict(cl_renderer='rdsp-rend2',r_mode=-1,r_customwidth=640,r_customheight=360,r_fullscreen=0,
        r_swapInterval=0,com_maxfps=60,com_maxfpsUnfocused=60,com_maxfpsMinimized=60,com_bootlogo=0,
        in_mouse=0,s_initsound=0,logfile=2,developer=0,r_gl43=args.gl43,r_offscreenPresent=1,
        cg_thirdPerson=0,cg_draw2D=0,cg_drawGun=0,cg_shadows=0,r_hdr=1,r_picmip=1,
        r_shaderProgramLazy=1,r_shaderCompileProcesses=0,r_captureShaderTime=1300,
        r_autoExposure=0,r_depthPrepass=1,r_depthPrepassFast=1,r_motionBlur=1,r_motionBlurShutterAngle=0,
        r_ssr=0,r_ssgi=0,r_smaa=0,r_volumetricFog=0,r_volumetricWater=0,r_waterSurface=0,
        r_atmosphere=0,r_clouds=0,r_autoGrass=0,r_ssao=0,r_aoMode=0,r_bloom=0,r_skinSSS=0,
        r_sunlightMode=int(args.shadows),r_sunShadows=int(args.shadows),r_forceSun=int(args.shadows),
        r_sunShadowMode=int(args.shadows),r_sunShadowCache=0,r_sunShadowAlphaCasters=1,
        r_dlightMode=1,r_shadows=0,r_speeds=0,r_phongTessellation=0,r_characterCull=0,con_notifytime=-1,
        cg_bobup=0,cg_bobpitch=0,cg_bobroll=0,cg_runpitch=0,cg_runroll=0,r_surfaceSprites=1,
        r_surfaceSpritesCompute=0,r_grassCardMode=args.cards,r_grassBendSegments=4,
        r_grassBendLodDist=args.bend_distance,r_grassCardLodDist=args.card_distance,r_foliageWind=1,r_foliageWindDebug=3,
        r_foliageInteraction=1)
    commands=['wait 60',f'devmap {args.map}','wait 180','exitview','cam_disable','lock_disable',
        'god','notarget','noclip','cg_thirdPerson 0',f'setviewpos {args.pose}','wait 30',
        'uimenu sprite_freeze','fx_freeze 1','wait 30']
    for name,mode in [('off',0),('cull',1),('off_repeat',0),('lod',2)]:
        commands += [f'r_surfaceSpritesCompute {mode}','r_speeds 0','wait 90',f'screenshot_tga sprite_{name}',
                     f'echo SPRITE_PHASE_BEGIN_{name}','r_speeds 100','wait 75','r_speeds 0',
                     f'echo SPRITE_PHASE_END_{name}']
    commands += ['echo SPRITE_CULL_RUNTIME_DONE','wait 5','quit']
    (home/'autoexec_sp.cfg').write_text('\n'.join([f'set {key} "{value}"' for key,value in settings.items()]+commands)+'\n')
    launch=[str(runtime/'openjk_sp.x86_64.exe'),'+set','fs_basepath',str(args.installation.resolve()),
            '+set','fs_homepath',str(runtime/'home'),'+set','fs_game','OpenJK']
    return runtime,home,launch


def analyze(runtime,home,args):
    log='\n'.join(p.read_text(errors='replace') for p in home.glob('*.log'))
    assert 'SPRITE_CULL_RUNTIME_DONE' in log,'runtime did not complete'
    assert not re.search(r'(?:GLSL|sprite compute).*(?:failed|error)|FBO.*incomplete|VM_Create.*failed',log,re.I)
    phases={}
    for name,body in re.findall(r'SPRITE_PHASE_BEGIN_(\w+)(.*?)SPRITE_PHASE_END_\1',log,re.S):
        count_rows=[tuple(map(int,m)) for m in re.findall(r'Surface sprites GPU: candidates (\d+) submitted (\d+) culled (\d+) cards (\d+) verts (\d+) dispatches (\d+) reused (\d+)',body)]
        # One post-processing timer denotes one rendered frame. Discard the
        # phase's first two frames to avoid timer results from the preceding mode.
        frames=re.split(r'Post processing: [\d.]+ms',body)
        samples=[]
        for frame in frames[2:-1]:
            draw=sum(map(float,re.findall(r'Surface sprites: ([\d.]+)ms',frame)))
            cull=sum(map(float,re.findall(r'Surface sprite cull: ([\d.]+)ms',frame)))
            samples.append(dict(draw_ms=draw,cull_ms=cull,total_ms=draw+cull))
        phases[name]=dict(gpu_samples=len(samples),
            draw_median_ms=statistics.median(x['draw_ms'] for x in samples) if samples else None,
            cull_median_ms=statistics.median(x['cull_ms'] for x in samples) if samples else None,
            total_median_ms=statistics.median(x['total_ms'] for x in samples) if samples else None,
            counter_median=[statistics.median(row[i] for row in count_rows) for i in range(7)] if count_rows else None)
    from PIL import Image
    import numpy as np
    images={p.stem:np.asarray(Image.open(p).convert('RGB'),dtype=np.int16) for p in (home/'screenshots').glob('sprite_*.tga')}
    diffs={}
    for a,b in [('off','off_repeat'),('off','cull'),('off','lod')]:
        x,y=images['sprite_'+a],images['sprite_'+b]
        assert np.any(x),'empty capture'
        d=np.abs(x-y)
        diffs[a+'/'+b]=dict(channels=int(np.count_nonzero(d)),pixels=int(np.count_nonzero(np.any(d,axis=2))),max=int(d.max()),mean=float(d.mean()))
    assert diffs['off/off_repeat']['channels']==0,'frozen reference changed'
    assert diffs['off/cull']['channels']==0,'same-LOD culling changed pixels'
    if args.gl43:
        counters=phases['cull']['counter_median']
        assert counters and counters[0]>0 and counters[2]>0,'no real sprite culling reached the GPU'
        assert counters[6]>0,'camera prepass and color did not share a cached result'
    result=dict(map=args.map,pose=args.pose,gl43=args.gl43,shadows=args.shadows,cards=args.cards,
        phases=phases,pixel_differences=diffs,metadata=json.loads((runtime/'run-meta.json').read_text()))
    (runtime/'results.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation',type=Path,required=True)
    parser.add_argument('--binaries',type=Path,default=Path('C:/Users/Keyten/Desktop/projects/OpenJK-rend2/build/msvc/Release'))
    parser.add_argument('--renderer',type=Path,default=ROOT/'build/grass-msvc/Release')
    parser.add_argument('--label',default='yavin1-intel')
    parser.add_argument('--map',default='yavin1')
    parser.add_argument('--pose',default='1024 -4700 210 90')
    parser.add_argument('--gl43',type=int,choices=(0,1),default=1)
    parser.add_argument('--cards',type=int,default=3)
    parser.add_argument('--shadows',action='store_true')
    parser.add_argument('--bend-distance',type=float,default=350)
    parser.add_argument('--card-distance',type=float,default=300)
    parser.add_argument('--run',action='store_true')
    parser.add_argument('--analyze-only',action='store_true')
    args=parser.parse_args()
    if args.analyze_only:
        runtime=ROOT/'build/sprite-cull-runtime'/args.label
        analyze(runtime,runtime/'home/OpenJK',args)
        return
    runtime,home,launch=prepare(args)
    if not args.run:
        print('Prepared',runtime)
        return
    foreign=harness.foreign_games()
    if foreign:raise RuntimeError(f'Foreign games prevent an isolated run: {foreign}')
    metadata=dict(foreign_before=foreign)
    (runtime/'run-meta.json').write_text(json.dumps(metadata,indent=2))
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    with (runtime/'launch.log').open('w') as output:
        process=subprocess.Popen(launch,cwd=runtime,startupinfo=startup,stdout=output,stderr=output)
        try:code=process.wait(timeout=900)
        except subprocess.TimeoutExpired:
            process.terminate();raise
        assert code==0,code
    metadata['foreign_after']=harness.foreign_games()
    (runtime/'run-meta.json').write_text(json.dumps(metadata,indent=2))
    assert not metadata['foreign_after'],'foreign games overlapped the run'
    analyze(runtime,home,args)

if __name__=='__main__':main()
