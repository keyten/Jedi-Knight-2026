#!/usr/bin/env python3
"""Opt-in S13 data-marker runtime validation. Never adds lights; own PID only.
Unfitted stock fixture priors are MAGENTA and explicitly labelled as such.
GPL-2.0-or-later.
"""
import argparse,json,math,os,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/rend2'))
sys.path.insert(0,str(ROOT/'tools/rend2/datasets'))
from test_lightrecon_legacy_runtime import settings
from measure import peak_memory

def run(out,renderer,rays):
    if out.resolve().is_relative_to(ROOT):raise ValueError('Stock screenshots stay external')
    out.mkdir(parents=True,exist_ok=False);results={}
    for name,camera in [('hoth3',(3464,700,85,90)),('hoth2',(1728,-576,1040,135))]:
        folder=out/name;home=folder/'home/OpenJK';home.mkdir(parents=True)
        binaries=ROOT/'build/x02/runtime/run'
        for f in ('openjk_sp.x86_64.exe','rend2_glslc.exe'):shutil.copy2(binaries/f,folder/f)
        shutil.copy2(binaries/'home/OpenJK/jagamex86_64.dll',home/'jagamex86_64.dll')
        shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll',folder/'SDL2.dll');shutil.copy2(renderer,folder/'rdsp-rend2_x86_64.dll')
        (home/'ui').mkdir();(home/'ui/ingame.txt').write_text('loadmenu { "ui/recon-freeze.menu" }\n')
        (home/'ui/recon-freeze.menu').write_text('menuDef { name "recon_freeze" rect 0 0 0 0 fullScreen 0 visible 1 onOpen { setcvar "cl_paused" "1"; } }\n')
        config=settings('legacy',3);config.update(r_customwidth=640,r_customheight=360,com_maxfps=30,com_maxfpsUnfocused=30,com_maxfpsMinimized=30,r_picmip=3,r_textureMode='GL_LINEAR_MIPMAP_NEAREST',r_simpleMipMaps=1,r_dynamiclight=0,r_fastsky=1,r_shadows=0,r_lodbias=2,r_subdivisions=16,r_weather=0,r_flares=0)
        config.update(fs_basepath=str(ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets'),fs_homepath=str(folder/'home'),fs_game='OpenJK')
        (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()),encoding='ascii')
        prefix=ROOT/'build/x02/final'/name;source=json.loads(prefix.with_suffix('.source.json').read_text());hints=folder/'hints.tsv'
        subprocess.run([str(rays.resolve()),str(prefix.with_suffix('.public.jkobs')),'--hints',str(hints.resolve())],check=True)
        allhints=[]
        for line in hints.read_text().splitlines():
            f=line.split();p=list(map(float,f[1:4]));n=list(map(float,f[4:7]));d=math.dist(p,camera[:3]);allhints.append((d,int(f[0]),p,n))
        wall=[v for v in sorted(allhints) if math.hypot(*v[3][:2])>.9]
        if wall:
            _,_,target,normal=wall[0]
            position=[v+96*n for v,n in zip(target,normal)]
            yaw=math.degrees(math.atan2(target[1]-position[1],target[0]-position[0]))
            camera=(*position,yaw)
        selected=sorted(allhints,key=lambda v:(math.dist(v[2],camera[:3]),v[1]))[:24]
        preview=home/'lightrecon-preview';preview.mkdir()
        text=[f"JKRECONPREVIEW1 {source['map_sha256']} {len(selected)}"]
        for d,ident,p,n in selected:text.append(' '.join(map(str,[ident,0,1,*p,1,0,0,0,1,0,0,0,1,0,0,0,3,0,0])))
        (preview/'stock-priors.txt').write_text('\n'.join(text)+'\n',encoding='ascii')
        x,y,z,yaw=camera
        commands=['wait 60',f'devmap {name}','wait 120','cam_disable','exitview','god','notarget','noclip','cg_draw2D 0','cg_drawGun 0','cg_thirdPerson 0','con_notifytime 0',f'setviewpos {x} {y} {z} {yaw}','wait 10','uimenu recon_freeze','fx_freeze 1','wait 10','r_lightReconPreview off','wait 5','screenshot_tga s13-before','wait 5','r_lightReconPreview stock-priors','wait 5','screenshot_tga s13-markers','wait 5','r_lightReconPreview off','wait 5','screenshot_tga s13-after','r_lightReconInfo','echo S13_PREVIEW_DONE','wait 5','quit']
        (home/'autoexec_sp.cfg').write_text('\n'.join(commands)+'\n',encoding='ascii')
        command=[str(folder/'openjk_sp.x86_64.exe')]
        for k in ('fs_basepath','fs_homepath','fs_game'):command+=['+set',k,config[k]]
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        start=time.monotonic();rss=0;expired=False
        with (folder/'launch.log').open('w') as log:
            child=subprocess.Popen(command,cwd=folder,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
            while child.poll() is None:
                rss=max(rss,peak_memory(child) or 0)
                if time.monotonic()-start>240 or rss>1536*1024*1024:
                    expired=True;child.terminate();child.wait(timeout=10);break
                time.sleep(.5)
            rss=max(rss,peak_memory(child) or 0)
        console=(home/'qconsole.log').read_text(errors='replace')
        results[name]=dict(exit_code=child.returncode,budget_exhausted=expired,seconds=time.monotonic()-start,peak_process_rss_bytes=rss,
            camera=camera,done='S13_PREVIEW_DONE' in console,marker_import='DATA ONLY markers' in console,markers=len(selected),settings=config,
            claim='Unfitted legitimate glow priors only; no stock physical reconstruction demonstrated',screenshots=[str(p) for p in home.rglob('s13-*.tga')])
        (out/'result.json').write_text(json.dumps(results,indent=2)+'\n');print(name,results[name],flush=True)
    return results
if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--renderer',type=Path,required=True);p.add_argument('--rays',type=Path,required=True);a=p.parse_args();run(a.out,a.renderer,a.rays)
