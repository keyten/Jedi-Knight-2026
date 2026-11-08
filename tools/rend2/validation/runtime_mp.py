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
from tools.rend2.validation import mp_control


def run_case(a,name,profile,index):
    if not getattr(a,'allow_mp_launch',False):
        return dict(status='Unperformed',reason='MP launch requires explicit permission',map=name,profile=profile)
    if name not in mp_control.CAMERAS:
        return dict(status='Unperformed',reason='No frozen world-camera protocol for this map',map=name,profile=profile)
    if legacy.game_processes():return dict(status='Unperformed',reason='Independent game process preserved',map=name,profile=profile)
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
    (home/'ui/jampingame.txt').write_text(mp_control.menu_list(menu),encoding='latin1')
    for n in reversed(range(4)):
        with zipfile.ZipFile(a.installation/'base'/f'assets{n}.pk3') as archive:
            if 'ui/jampmenus.txt' in archive.namelist():
                menu=archive.read('ui/jampmenus.txt').decode('latin1');break
    (home/'ui/v02-menus.txt').write_text(mp_control.menu_list(menu),encoding='latin1')
    # One executable path for this entire batch, rather than a new firewall
    # identity for every map/profile home. Never change system firewall rules.
    process_folder=a.output/'process';process_folder.mkdir(exist_ok=True)
    def install(source,target):
        if target.exists():
            if hashlib.sha256(target.read_bytes()).digest()!=hashlib.sha256(source.read_bytes()).digest():
                raise ValueError('Stale MP process binary; use a fresh output directory: '+target.name)
        else:shutil.copy2(source,target)
    for binary in ('openjk.x86_64.exe','rend2_glslc.exe'):
        install(a.mp_binaries/binary,process_folder/binary)
    for binary in ('cgamex86_64.dll','jampgamex86_64.dll','uix86_64.dll'):
        shutil.copy2(a.mp_binaries/binary,home/binary)
    install(a.mp_renderer,process_folder/'rd-rend2_x86_64.dll')
    install(ROOT/'lib/SDL2/bin/x64/SDL2.dll',process_folder/'SDL2.dll')
    config.update(cl_renderer='rd-rend2',r_customwidth=640,r_customheight=360,r_picmip=3,
        com_maxfps=30,com_maxfpsUnfocused=30,com_maxfpsMinimized=30,
        r_subdivisions=16,r_lodbias=2,r_shadowMapSize=256,
        r_textureMode='GL_LINEAR_MIPMAP_NEAREST',r_lightReconUse=0,
        sv_pure=0,g_gametype=0,bot_minplayers=0,sv_maxclients=1,
        ui_menuFilesMP='ui/v02-menus.txt',
        cg_draw2D=0,cg_drawGun=0,cg_thirdPerson=0,con_notifytime=0)
    config.update({c:0 for c in legacy.CONSUMERS})
    (home/'openjk.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()),encoding='ascii')
    commands=mp_control.commands(name,legacy.CONSUMERS)
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
    if child.returncode or 'X19_MP_DONE' not in text:raise ValueError('Incomplete MP run; inspect qconsole.log')
    scene_control=mp_control.validate_sections(text,name,legacy.validate_consumers)
    images=mp_control.validate_images(home/'screenshots',compare,legacy.IMAGE_TOLERANCE)
    errors=re.findall(r'[^\r\n]*(?:GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|FBO.*incomplete)[^\r\n]*',text)
    value=dict(status='Passed' if not errors and all(x['passed'] for x in images.values()) else 'Failed',
        map=name,profile=profile,images=images,errors=errors,
        viewpos=re.findall(r'[^\r\n]*\(-?\d+ -?\d+ -?\d+\)[^\r\n]*',text),
        renderer_sha256=hashlib.sha256(a.mp_renderer.read_bytes()).hexdigest(),archives=archives,
        config=config,command=command,seconds=time.perf_counter()-start,peak_rss_bytes=peak,
        consumer_blocks_observed=5,scene_control=scene_control,debug_capture_supported=False,
        debug_marker_alignment='Unavailable at build policy 0; no marker alignment claim',
        no_recovered_lights=True,network_settings_modified=False,firewall_settings_modified=False)
    write(runtime/'result.json',value)
    return value


def run(a):
    a.output.mkdir(parents=True,exist_ok=False);results=[]
    stopped=None; launches=0
    for index,(name,profile) in enumerate((n,p) for n in a.maps for p in a.profiles):
        if not getattr(a,'allow_mp_launch',False):
            results.append(dict(status='Unperformed',map=name,profile=profile,
                reason='MP launch requires explicit permission; no process or network configuration attempted'))
            write(a.output/'results.json',results)
            continue
        if launches>=getattr(a,'max_launches',1):
            results.append(dict(status='Unperformed',map=name,profile=profile,
                reason='Explicit MP launch budget exhausted'))
            write(a.output/'results.json',results)
            continue
        if stopped:
            results.append(dict(status='Unperformed',map=name,profile=profile,
                reason='Stopped after an invalid earlier runtime control: '+stopped))
            write(a.output/'results.json',results)
            continue
        try:
            launches+=1
            r=run_case(a,name,profile,index)
        except FileNotFoundError as e:
            stopped=str(e)
            r=dict(status='Unperformed',reason='Required runtime input unavailable: '+stopped,map=name,profile=profile)
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
    p.add_argument('--max-launches',type=int,default=1,help='Bound total owned MP processes; default one, never retry automatically')
    p.add_argument('--maps',nargs='+',choices=('mp/duel4','mp/ffa3'),default=['mp/duel4','mp/ffa3'])
    p.add_argument('--profiles',nargs='+',choices=('legacy','modern'),default=['legacy','modern'])
    a=p.parse_args()
    if not a.allow_mp_launch:p.error('MP launch is opt-in; pass --allow-mp-launch after reviewing Windows/network implications')
    for name in ('installation','binaries','renderer','mp_binaries','mp_renderer','output'):
        setattr(a,name,getattr(a,name).resolve())
    raise SystemExit(1 if any(r['status']=='Failed' for r in run(a)) else 0)
