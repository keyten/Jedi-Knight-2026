"""Hidden high-quality documentation captures; installed game data stays read-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', required=True)
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--cache-from', type=Path)
    parser.add_argument('--spec', type=Path, help='Settings and full command stream for a reviewed scene batch')
    parser.add_argument('--stock-only', action='store_true', help='Read only original assets0-3; excludes replacement PBR packs')
    parser.add_argument('--installation',type=Path,help='Existing read-only game asset root')
    parser.add_argument('--interactive', action='store_true', help='Keep one hidden game alive; review each map before queuing the next')
    args = parser.parse_args()
    runtime = ROOT / 'build/feature-gallery' / args.label
    runtime.mkdir(parents=True, exist_ok=False)
    home = runtime / 'home/OpenJK'
    (home / 'ui').mkdir(parents=True)
    if args.cache_from:
        for name in ('glslcache', 'cache'):
            if (args.cache_from / name).is_dir():
                shutil.copytree(args.cache_from / name, home / name)
    (home / 'ui/ingame.txt').write_text('loadmenu { "ui/gallery.menu" }\n')
    (home / 'shaders').mkdir()
    (home / 'shaders/gallery.shader').write_text('gallery/transparent\n{\n {\n map $whiteimage\n blendFunc GL_ZERO GL_ONE\n }\n}\n')
    (home / 'ui/gallery.menu').write_text('assetGlobalDef { cursor "gallery/transparent" }\nmenuDef { name "gallery_freeze" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\nmenuDef { name "gallery_resume" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { uiScript closeingame ; } }\n')
    binaries = ROOT / 'build/msvc/Release'
    for name in ('rdsp-rend2_x86_64.dll', 'rend2_glslc.exe'):
        shutil.copy2(binaries / name, runtime / name)
    shutil.copy2(binaries / 'openjk_sp.x86_64.exe', runtime / 'openjk_sp.x86_64.exe')
    shutil.copy2(binaries / 'jagamex86_64.dll', runtime / 'jagamex86_64.dll')
    shutil.copy2(runtime / 'jagamex86_64.dll', home / 'jagamex86_64.dll')
    shutil.copy2(ROOT / 'lib/SDL2/bin/x64/SDL2.dll', runtime / 'SDL2.dll')
    settings = dict(cl_renderer='rdsp-rend2', r_hiddenWindow=1, s_initsound=0,
        in_mouse=0, in_nograb=1, r_fullscreen=0, r_noborder=1, r_mode=-1, r_customwidth=1920,
        r_customheight=1080, r_gl43=1, r_offscreenPresent=1, r_hdr=1,
        r_picmip=0, r_lodbias=0, r_subdivisions=4, r_ext_texture_filter_anisotropic=16,
        r_textureMode='GL_LINEAR_MIPMAP_LINEAR', r_shaderProgramCache=1,
        r_shaderProgramLazy=1, r_shaderCompileProcesses=0, r_swapInterval=0,
        com_maxfps=60, com_maxfpsUnfocused=60, com_maxfpsMinimized=60,
        com_bootlogo=0, logfile=2, developer=0, cg_draw2D=0, cg_drawGun=0,
        con_notifytime=-1, r_autoExposure=0, r_toneMap=1, r_toneMapMode=1,
        cg_thirdPersonCameraDamp=1,cg_thirdPersonTargetDamp=1,cg_thirdPersonRange=0,
        cg_thirdPersonVertOffset=0,cg_thirdPersonAlpha=0,r_autoEmissive=1,
        r_bloom=1, r_bloomSceneIntensity=0.15, r_smaa=1, r_smaaQuality=3,
        r_aoMode=2, r_gtaoQuality=3, r_ssr=1, r_ssrQuality=3,
        r_ssgi=1, r_autoPBR=2, r_autoPBRConvert=1, r_specularAA=1,
        r_diffuseBRDF=1, r_colorGrading=1, r_colorGradingLUT='luts/gallery-warm.cube',
        r_captureShaderTime=1300, r_motionBlur=0, r_volumetricFog=0)
    spec = json.loads(args.spec.read_text(encoding='utf-8')) if args.spec else None
    if spec:
        settings.update(spec.get('settings', {}))
    lut = home / 'luts/gallery-warm.cube'
    lut.parent.mkdir()
    lines=['TITLE "Gallery warm example"', 'LUT_3D_SIZE 8', 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1']
    for b in range(8):
        for g in range(8):
            for r in range(8):
                lines.append(f'{min(1,r/7*1.06):.6f} {g/7:.6f} {b/7*.9:.6f}')
    lut.write_text('\n'.join(lines)+'\n')
    cases=[('tone-mapping','r_toneMapCompare'),('bloom','r_bloomCompare'),
           ('color-grading','r_colorGradingCompare'),('anti-aliasing-smaa','r_smaaCompare'),
           ('ambient-occlusion-gtao','r_aoCompare'),('screen-space-reflections-ssr','r_ssrCompare'),
           ('bounced-light-ssgi','r_ssgiCompare'),('automatic-materials','r_autoPBRCompare'),
           ('stable-highlights','r_specularAACompare'),('matte-surfaces','r_diffuseBRDFCompare')]
    commands=['wait 120','devmap mp/ffa3','wait 240','exitview','cam_disable','lock_disable',
        'god','notarget','noclip','give all','weapon 3','wait 60','cg_thirdPerson 0','setviewpos 671 -769 -70 290',
        'wait 60','uimenu gallery_freeze','fx_freeze 1','wait 30']
    commands += [c+' 2' for _,c in cases]
    commands += ['wait 60','screenshot_tga scene_probe']
    if not args.probe:
        for name, compare in cases:
            commands += [compare+' 2','wait 45',f'screenshot_tga {name}-before',
                compare+' 0','wait 90',f'screenshot_tga {name}-after',
                compare+' 2','wait 45',f'screenshot_tga {name}-repeat']
    commands += ['echo GALLERY_CAPTURE_DONE','wait 10','quit']
    if spec:
        commands = spec['commands'] + ['echo GALLERY_CAPTURE_DONE','wait 10','quit']
    if args.interactive:
        (home/'gallery-loop.cfg').write_text('wait 60\nexecq gallery-queue.cfg\nexecq gallery-loop.cfg\n')
        (home/'gallery-queue.cfg').write_text('')
        commands = ['wait 120','r_norefresh 1','echo GALLERY_SESSION_READY','exec gallery-loop.cfg']
    (home/'autoexec_sp.cfg').write_text('\n'.join(f'set {k} "{v}"' for k,v in settings.items())+'\n'+'\n'.join(commands)+'\n')
    installation=args.installation.resolve() if args.installation else ROOT.parent/'OpenJK/build-rend2'
    if args.stock_only:
        stock=runtime/'stock/base';stock.mkdir(parents=True)
        for i in range(4):
            name=f'assets{i}.pk3'
            os.link(installation/'base'/name,stock/name)
        installation=stock.parent
    launch=[str(runtime/'openjk_sp.x86_64.exe'),'+set','fs_basepath',str(installation),
            '+set','fs_homepath',str(runtime/'home'),'+set','fs_game','OpenJK']
    early=('r_hiddenWindow','s_initsound','in_mouse','in_nograb','r_fullscreen','r_noborder','cl_renderer',
           'r_mode','r_customwidth','r_customheight','r_gl43','r_offscreenPresent','r_hdr',
           'r_shaderProgramCache','r_shaderProgramLazy','r_shaderCompileProcesses')
    for key in early: launch += ['+set',key,str(settings[key])]
    metadata={'settings':settings,'cases':cases,'commands':commands,'spec':spec,'launch':launch,'binaries':{
        p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in runtime.glob('*.dll')}}
    (runtime/'manifest.json').write_text(json.dumps(metadata,indent=2))
    from capture_smoke_volume import run_hidden
    print(f'Launching monitored hidden capture: {runtime}',flush=True)
    stop=threading.Event()
    def acknowledge_queue():
        last=''
        while not stop.wait(.2):
            path=home/'qconsole.log'
            if not path.exists():continue
            log=path.read_text(errors='replace')
            import re
            matches=re.findall(r'GALLERY_ACK_([a-zA-Z0-9_-]+)',log)
            if matches and matches[-1]!=last:
                last=matches[-1]
                (home/'gallery-queue.cfg').write_text('')
                (runtime/'queue-status.json').write_text(json.dumps(dict(acknowledged=last)))
    if args.interactive:
        threading.Thread(target=acknowledge_queue,daemon=True).start()
    try:
        run_hidden(launch, runtime, timeout=28800 if args.interactive else 900)
    finally:
        stop.set()
    log=(home/'qconsole.log').read_text(errors='replace')
    assert 'GALLERY_CAPTURE_DONE' in log,log[-6000:]
    assert 'ERROR: SG:' not in log, 'Engine/game save ABI mismatch; captures rejected'
    print('Capture complete',flush=True)

if __name__=='__main__': main()
