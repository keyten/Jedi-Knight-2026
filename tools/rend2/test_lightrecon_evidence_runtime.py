#!/usr/bin/env python3
"""Actual stock SP emitter diagnostics and before/after LTC contract comparison.
Reuses the existing isolated runtime, fixed cameras and image comparator. No assets enter Git.
"""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
from test_lightrecon_legacy_runtime import ROOT, CAMERAS, prepare_runtime
from gl43_validation.compare_images import compare
from PIL import Image

def commands():
    out=['wait 30']
    for name in ('hoth3','hoth2'):
        x,y,z,yaw=CAMERAS[name]
        out += [f'devmap {name}','wait 180','cam_disable','exitview','god','notarget','noclip',
                'cg_draw2D 0','cg_drawGun 0','cg_thirdPerson 0','r_drawentities 0','con_notifytime 0',
                f'setviewpos {x} {y} {z} {yaw}','wait 20','cam_disable','lock_disable',
                'uimenu recon_freeze','fx_freeze 1','r_we clear','r_ltcExtractLights','wait 30',
                f'writecam evidence_{name}_base',f'screenshot_tga evidence-{name}-base',
                f'echo EVIDENCE_{name}_BEGIN','r_lightReconEvidence list',
                f'echo EVIDENCE_{name}_END','wait 20',f'writecam evidence_{name}_repeat',
                f'screenshot_tga evidence-{name}-repeat','wait 15','uimenu recon_unfreeze','fx_freeze 0']
    return out+['echo EVIDENCE_RUNTIME_DONE','wait 20','quit']

def run(args,renderer,label,policy):
    args.renderer=renderer
    runtime=args.output.resolve()/label
    settings,archives=prepare_runtime(args,runtime,'legacy',policy)
    # Exercise automatic LTC with its existing Forward+ path, independently of the evidence provider.
    settings.update(r_forwardPlus=args.ltc,r_ltcAreaLights=args.ltc,r_ltcAutoAreaLights=2,r_deluxeMapping=0)
    home=runtime/'home/OpenJK'
    (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in settings.items()))
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands())+'\n')
    cmd=[str(runtime/'openjk_sp.x86_64.exe')]
    for k in ('fs_basepath','fs_homepath','fs_game'):cmd+=['+set',k,settings[k]]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    print('Launching',label,flush=True)
    with (runtime/'launch.log').open('w') as f:
        p=subprocess.Popen(cmd,cwd=runtime,startupinfo=startup,stdout=f,stderr=subprocess.STDOUT)
        try:p.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=20);raise
    text=(home/'qconsole.log').read_text(errors='replace')
    if p.returncode or 'EVIDENCE_RUNTIME_DONE' not in text or re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error',text):
        raise AssertionError('Actual SP runtime failed: '+str(runtime)+'\n'+text[-2500:])
    cameras={}
    for path in (home/'maps').glob('*_cam.map'):
        for tag,origin,angles,fov in re.findall(r'"targetname"\s*"([^"]+)"\s*"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"',path.read_text()):
            cameras[tag]=[origin,angles,fov]
    result=dict(settings=settings,archives=archives,renderer_sha256=hashlib.sha256(renderer.read_bytes()).hexdigest(),maps={})
    for name in ('hoth3','hoth2'):
        camera=cameras.get(f'evidence_{name}_base')
        if camera is None or camera!=cameras.get(f'evidence_{name}_repeat'):raise AssertionError('Camera moved')
        a=home/f'screenshots/evidence-{name}-base.tga';b=home/f'screenshots/evidence-{name}-repeat.tga'
        tolerance=dict(max_delta=1,max_pixels=512,max_mean=0.0005) if args.ltc else {}
        compared=compare(a,b,**tolerance)
        # An unstable original frame is explicitly unverified, never a visual pass.
        if not compared['passed']:print('Image control unstable:',label,name,compared,flush=True)
        section=re.search(f'EVIDENCE_{name}_BEGIN(.*?)EVIDENCE_{name}_END',text,re.S)[1]
        generated=home/f'maps/{name}.arealights.generated.json'
        if not generated.is_file():raise AssertionError('LTC candidate export missing')
        sources=re.findall(r'id=(\d+) origin=(\S+) material=(.*?) shapes=(\d+) issues=(\d+) static=(\d+)',section)
        if policy==3 and len({row[0] for row in sources})!=len(sources):raise AssertionError('Duplicate source IDs')
        metadata=dict(camera=camera,diagnostics='\n'.join(line for line in section.strip().splitlines() if 'id=' not in line),source_records=sources,ltc_sha256=hashlib.sha256(generated.read_bytes()).hexdigest(),within_run=compared)
        if policy==3 and 'hypotheses only' not in section:raise AssertionError('No published emitter evidence '+section)
        result['maps'][name]=metadata
        Image.open(a).save(runtime/f'{name}.png')
    (runtime/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation',type=Path,required=True);p.add_argument('--binaries',type=Path,required=True)
    p.add_argument('--renderer',type=Path,required=True);p.add_argument('--baseline-renderer',type=Path)
    p.add_argument('--ltc',type=int,choices=(0,1),default=0,help='LTC enabled: allow at most one code step on 512/518400 pixels (old binary has FP accumulation jitter)')
    p.add_argument('--output',type=Path,default=ROOT/'build/lightrecon-evidence-runtime');p.add_argument('--timeout',type=int,default=600)
    args=p.parse_args();renderer=args.renderer
    result={}
    if args.baseline_renderer:result['before']=run(args,args.baseline_renderer,'before',0)
    result['after']=run(args,renderer,'after',3)
    if 'before' in result:
        for name in ('hoth3','hoth2'):
            a=result['before']['maps'][name];b=result['after']['maps'][name]
            b['same_camera']=a['camera']==b['camera']
            if not b['same_camera']:print('Cross-run camera differs; visual comparison unverified:',name,flush=True)
            if a['ltc_sha256']!=b['ltc_sha256']:raise AssertionError('Legacy LTC candidate output changed: '+name)
            comparison=compare(args.output/'before'/f'{name}.png',args.output/'after'/f'{name}.png',**(dict(max_delta=1,max_pixels=512,max_mean=0.0005) if args.ltc else {}))
            b['before_after']=comparison
            b['visual_verified']=b['same_camera'] and a['within_run']['passed'] and b['within_run']['passed'] and comparison['passed']
            if b['same_camera'] and a['within_run']['passed'] and b['within_run']['passed'] and not comparison['passed']:
                raise AssertionError('LTC frame changed '+name+': '+str(comparison))
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['after']['maps'],indent=2))
if __name__=='__main__':main()
