#!/usr/bin/env python3
"""Actual MP disabled-consumer A/B/A, reusing the SP asset and image helpers.

No source answers or recovered illumination. Private local listen server only.
GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'tools/rend2'))
import test_lightrecon_legacy_runtime as legacy
from tools.rend2.all_stock.run import write
from tools.rend2.gl43_validation.compare_images import compare


def run_case(a,name,profile,index):
    if legacy.game_processes():return dict(status='Unperformed',reason='Independent game process preserved')
    runtime=a.output/(name.replace('/','_')+'-'+profile)
    config,archives=legacy.prepare_runtime(a,runtime,profile,0)
    home=runtime/'home/OpenJK'
    # Retain stock menus, adding the same invisible freeze menu used by SP.
    # MP cl_paused is read-only to the console; the UI owns this state.
    menu=None
    for n in reversed(range(4)):
        with zipfile.ZipFile(a.installation/'base'/f'assets{n}.pk3') as archive:
            if 'ui/jampingame.txt' in archive.namelist():
                menu=archive.read('ui/jampingame.txt').decode('latin1');break
    if menu is None:raise ValueError('Stock MP ingame menu list missing')
    (home/'ui/jampingame.txt').write_text(menu+'\nloadmenu { "ui/recon-freeze.menu" }\n',encoding='latin1')
    for n in reversed(range(4)):
        with zipfile.ZipFile(a.installation/'base'/f'assets{n}.pk3') as archive:
            if 'ui/jampmenus.txt' in archive.namelist():
                menu=archive.read('ui/jampmenus.txt').decode('latin1');break
    (home/'ui/v02-menus.txt').write_text(menu+'\nloadmenu { "ui/recon-freeze.menu" }\n',encoding='latin1')
    # One executable path for this entire batch, rather than a new firewall
    # identity for every map/profile home. Never change system firewall rules.
    process_folder=a.output/'process';process_folder.mkdir(exist_ok=True)
    for binary in ('openjk.x86_64.exe','rend2_glslc.exe'):
        if not (process_folder/binary).exists():shutil.copy2(a.mp_binaries/binary,process_folder/binary)
    for binary in ('cgamex86_64.dll','jampgamex86_64.dll','uix86_64.dll'):
        shutil.copy2(a.mp_binaries/binary,home/binary)
    if not (process_folder/'rd-rend2_x86_64.dll').exists():
        shutil.copy2(a.mp_renderer,process_folder/'rd-rend2_x86_64.dll')
        shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll',process_folder/'SDL2.dll')
    config.update(cl_renderer='rd-rend2',r_customwidth=640,r_customheight=360,r_picmip=3,
        com_maxfps=30,com_maxfpsUnfocused=30,com_maxfpsMinimized=30,
        r_subdivisions=16,r_lodbias=2,r_shadowMapSize=256,
        r_textureMode='GL_LINEAR_MIPMAP_NEAREST',r_lightReconUse=0,
        net_ip='127.0.0.1',net_port=28930+index,sv_lanForceRate=1,sv_pure=0,
        cl_allowDownload=0,g_gametype=0,bot_minplayers=0,sv_maxclients=1,
        ui_menuFilesMP='ui/v02-menus.txt',
        cg_draw2D=0,cg_drawGun=0,cg_thirdPerson=0,con_notifytime=0)
    config.update({c:0 for c in legacy.CONSUMERS})
    (home/'openjk.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()),encoding='ascii')
    camera=['setviewpos -2880 2036 50 90'] if name=='mp/duel4' else []
    commands=['wait 30',f'devmap {name}','wait 180','team free','wait 30','god','notarget','noclip',
              *camera,'wait 30','ui_openmenu recon_freeze','wait 20']
    for label,master,request in [('a',0,1),('b',1,0),('repeat',0,1)]:
        commands += [f'r_lightReconUse {master}']+[f'{c} {request}' for c in legacy.CONSUMERS]
        commands += ['wait 12',f'echo V02_{label.upper()}_BEGIN','r_lightReconInfo','viewpos',
                     'cl_paused','sv_paused','glslusage all',f'echo V02_{label.upper()}_END',
                     f'screenshot_tga v02-{label}','wait 5']
    # Independently bounded geometry diagnostic: show then explicitly disable.
    commands += ['r_lightReconDebug geometry','wait 10',
                 'screenshot_tga v02-debug','r_lightReconDebug off','wait 12',
                 'screenshot_tga v02-restored','echo V02_MP_DONE','wait 5','quit']
    (home/'autoexec.cfg').write_text('\n'.join(commands)+'\n',encoding='ascii')
    command=[str(process_folder/'openjk.x86_64.exe')]
    for k in ('fs_basepath','fs_homepath','fs_game'):command+=['+set',k,config[k]]
    start=time.perf_counter();peak=0
    from tools.rend2.datasets.measure import peak_memory
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    with (runtime/'launch.log').open('w') as stream:
        child=subprocess.Popen(command,cwd=runtime,stdout=stream,stderr=subprocess.STDOUT,startupinfo=startup)
        while child.poll() is None:
            peak=max(peak,peak_memory(child) or 0)
            if time.perf_counter()-start>a.timeout:
                child.terminate();child.wait(timeout=15);raise TimeoutError('Owned MP timeout')
            time.sleep(.25)
    text=(home/'qconsole.log').read_text(errors='replace')
    text=re.sub(r'\^[0-9]','',text)
    if child.returncode or 'V02_MP_DONE' not in text:raise ValueError('Incomplete MP run; inspect qconsole.log')
    from PIL import Image
    import numpy as np
    shots=home/'screenshots';pixels=np.asarray(Image.open(shots/'v02-a.tga').convert('RGB'))
    if pixels.std()<2 or len(np.unique(pixels.reshape(-1,3),axis=0))<64:
        raise ValueError('Empty/flat MP frame, never a successful regression')
    consumers=[]
    for label in ('A','B','REPEAT'):
        match=re.search('V02_'+label+'_BEGIN(.*?)V02_'+label+'_END',text,re.S)
        if not match:raise ValueError('Missing MP diagnostic block')
        legacy.validate_consumers(match[1],label,0)
        for c in ('cl_paused','sv_paused'):
            if not re.search(r'\b'+c+r'\s*=\s*"1"',match[1]):
                raise ValueError('MP camera/animation is not frozen: '+c)
        consumers.append(match[1])
    images=dict(ab=compare(shots/'v02-a.tga',shots/'v02-b.tga',**legacy.IMAGE_TOLERANCE),
        repeat=compare(shots/'v02-a.tga',shots/'v02-repeat.tga',**legacy.IMAGE_TOLERANCE),
        restored=compare(shots/'v02-repeat.tga',shots/'v02-restored.tga',**legacy.IMAGE_TOLERANCE))
    errors=re.findall(r'[^\r\n]*(?:GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|FBO.*incomplete)[^\r\n]*',text)
    value=dict(status='Passed' if not errors and all(x['passed'] for x in images.values()) else 'Failed',
        map=name,profile=profile,images=images,errors=errors,
        viewpos=re.findall(r'[^\r\n]*\(-?\d+ -?\d+ -?\d+\)[^\r\n]*',text),
        renderer_sha256=hashlib.sha256(a.mp_renderer.read_bytes()).hexdigest(),archives=archives,
        config=config,command=command,seconds=time.perf_counter()-start,peak_rss_bytes=peak,
        consumer_blocks_observed=3,debug_capture_supported=False,
        debug_marker_alignment='Unavailable at build policy 0; no marker alignment claim',
        no_recovered_lights=True)
    write(runtime/'result.json',value)
    return value


def run(a):
    a.output.mkdir(parents=True,exist_ok=False);results=[]
    stopped=None
    for index,(name,profile) in enumerate((n,p) for n in a.maps for p in a.profiles):
        if stopped:
            results.append(dict(status='Unperformed',map=name,profile=profile,
                reason='Stopped after an invalid earlier runtime control: '+stopped))
            write(a.output/'results.json',results)
            continue
        try:r=run_case(a,name,profile,index)
        except Exception as e:
            stopped=str(e)
            r=dict(status='Failed',reason=stopped,map=name,profile=profile)
        results.append(r);write(a.output/'results.json',results);print(name,profile,r['status'],flush=True)
        if r['status']!='Passed':
            # Remaining cases are recorded Unperformed, without more processes.
            stopped=r.get('reason','Image regression/control failed')
    return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('installation','binaries','renderer','mp-binaries','mp-renderer','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--timeout',type=int,default=180)
    p.add_argument('--allow-mp-launch',action='store_true',
        help='Explicit local MP launch. Windows may request firewall approval; no rules are changed')
    p.add_argument('--maps',nargs='+',choices=('mp/duel4','mp/ffa3'),default=['mp/duel4','mp/ffa3'])
    p.add_argument('--profiles',nargs='+',choices=('legacy','modern'),default=['legacy','modern'])
    a=p.parse_args()
    if not a.allow_mp_launch:p.error('MP launch is opt-in; pass --allow-mp-launch after reviewing Windows/network implications')
    for name in ('installation','binaries','renderer','mp_binaries','mp_renderer','output'):
        setattr(a,name,getattr(a,name).resolve())
    raise SystemExit(1 if any(r['status']=='Failed' for r in run(a)) else 0)
