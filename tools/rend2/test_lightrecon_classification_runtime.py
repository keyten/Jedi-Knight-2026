#!/usr/bin/env python3
"""Stock-map CPU classification + read-only image controls in an isolated SP runtime.

Reuses the existing runtime setup/image comparator. Optional external-dm fixture
is a private BSP copy with compact colour IDs and loose lm_/dm_ TGAs; installed
archives are read-only and no game data enter Git.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import zipfile
from test_lightrecon_legacy_runtime import ROOT, CAMERAS, prepare_runtime
from gl43_validation.compare_images import compare


def external_dm_fixture(installation, home, interleaved):
    source = None
    for archive in sorted((installation/'base').glob('assets[0-3].pk3'), reverse=True):
        with zipfile.ZipFile(archive) as z:
            if 'maps/hoth3.bsp' in z.namelist():
                source = bytearray(z.read('maps/hoth3.bsp')); break
    if source is None:
        raise FileNotFoundError('maps/hoth3.bsp')
    off, size = struct.unpack_from('<II', source, 8+8*14)
    so, sb = struct.unpack_from('<II', source, 8+8*13)
    stride = 128*128*3
    if not size or size % stride or sb % 148:
        raise AssertionError('Fixture requires complete original JA pages')
    references=[]
    for surface in range(so, so+sb, 148):
        references += [i for i in struct.unpack_from('<4i', source, surface+36) if i>=0]
    source_interleaved=size//stride>1 and not any(i & 1 for i in references)
    all_colour_pages=list(range(0,size//stride,2 if source_interleaved else 1))
    # External lookup is bounded by the largest referenced original page. Stock
    # hoth3 has one completely unreferenced trailing colour page in its BSP lump.
    colour_pages=[page for page in all_colour_pages if page<=max(references)]
    logical={page:i for i,page in enumerate(colour_pages)}
    # Retain original normalized source UVs. Only page IDs and image storage change.
    for surface in range(so, so+sb, 148):
        for slot in range(4):
            index = struct.unpack_from('<i', source, surface+36+slot*4)[0]
            if index >= 0:
                struct.pack_into('<i', source, surface+36+slot*4, logical[index]*(2 if interleaved else 1))
    folder = home/'maps/hoth3'; folder.mkdir(parents=True, exist_ok=True)
    def tga(rgb):
        bgr=bytearray(stride)
        bgr[0::3],bgr[1::3],bgr[2::3]=rgb[2::3],rgb[1::3],rgb[0::3]
        return struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,128,128,24,32)+bgr
    direction=bytearray(bytes((128,128,255))*128*128)
    direction[:6]=bytes((0,0,0,127,127,127)) # distinct unknown-direction cases
    expected_hashes={}
    def rgba_hash(rgb):
        rgba=bytearray(128*128*4)
        rgba[0::4],rgba[1::4],rgba[2::4],rgba[3::4]=rgb[0::3],rgb[1::3],rgb[2::3],bytes((255,))*128*128
        return hashlib.sha256(rgba).hexdigest()
    for i,page in enumerate(colour_pages):
        rgb = source[off+page*stride:off+(page+1)*stride]
        (folder/f'lm_{i*(2 if interleaved else 1):04d}.tga').write_bytes(tga(rgb))
        name=f'lm_{i*2+1:04d}.tga' if interleaved else f'dm_{i:04d}.tga'
        (folder/name).write_bytes(tga(direction))
        base_index=i*(2 if interleaved else 1)
        expected_hashes[f'{base_index}:lightmap']=rgba_hash(rgb)
        expected_hashes[f'{base_index+1 if interleaved else i}:deluxe']=rgba_hash(direction)
    struct.pack_into('<I', source, 8+8*14+4, 0)
    (home/'maps/hoth3.bsp').write_bytes(source)
    return dict(path='maps/hoth3.bsp', sha256=hashlib.sha256(source).hexdigest(), pages=len(colour_pages),
                mode='interleaved' if interleaved else 'dm',source_unreferenced_pages=len(all_colour_pages)-len(colour_pages),
                directions='synthetic +Z with zero/neutral sentinel pixels',expected_pixel_sha256=expected_hashes)


def commands(maps):
    # autoexec is read before all SP game commands are registered.
    out=['wait 30']
    for name in maps:
        x,y,z,yaw=CAMERAS[name]
        out += [f'devmap {name}','wait 180','cam_disable','exitview','god','notarget','noclip',
                'cg_draw2D 0','cg_drawGun 0','cg_thirdPerson 0','r_drawentities 0','con_notifytime 0',
                f'setviewpos {x} {y} {z} {yaw}','wait 20','cam_disable','lock_disable',
                # GPU weather shading has independent history as well as motion.
                # Remove weather only in this private static-frame control.
                'uimenu recon_freeze','fx_freeze 1','r_we clear','wait 30',f'writecam classify_{name}_base',
                f'screenshot_tga classify-{name}-base','wait 12',f'echo BAKED_{name}_BEGIN',
                'r_lightReconInfo images','r_lightReconObservations','r_lightReconObservations scan',
                'r_lightReconObservations surface 0','r_lightReconObservations direction 0 0 0',
                'r_lightReconObservations direction 0 1 0','r_lightReconObservations direction 0 2 0',
                f'echo BAKED_{name}_END','wait 12',
                f'writecam classify_{name}_repeat',f'screenshot_tga classify-{name}-repeat','wait 30',
                'uimenu recon_unfreeze','fx_freeze 0']
    return out+['wait 90','echo BAKED_RUNTIME_DONE','quit']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation',type=Path,required=True)
    p.add_argument('--binaries',type=Path,required=True)
    p.add_argument('--renderer',type=Path,required=True)
    p.add_argument('--output',type=Path,default=ROOT/'build/lightrecon-classification-runtime')
    p.add_argument('--label',required=True)
    p.add_argument('--external-dm-fixture',action='store_true')
    p.add_argument('--interleaved',action='store_true',help='Use lm_even/lm_odd names instead of lm_i/dm_i in the private fixture')
    p.add_argument('--timeout',type=int,default=600)
    args=p.parse_args()
    if os.name!='nt' or not re.fullmatch(r'[\w-]+',args.label) or (args.interleaved and not args.external_dm_fixture):
        p.error('Windows and a unique simple label required')
    runtime=args.output.resolve()/args.label
    settings,archives=prepare_runtime(args,runtime,'legacy',3)
    home=runtime/'home/OpenJK'
    settings['r_deluxeMapping']=0
    with (home/'openjk_sp.cfg').open('a') as f: f.write('set r_deluxeMapping "0"\n')
    fixture=external_dm_fixture(args.installation,home,args.interleaved) if args.external_dm_fixture else None
    maps=['hoth3'] if fixture else ['hoth3','hoth2']
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands(maps))+'\n',encoding='ascii')
    command=[str(runtime/'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath','fs_homepath','fs_game'): command+=['+set',key,settings[key]]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    with (runtime/'launch.log').open('w') as log:
        child=subprocess.Popen(command,cwd=runtime,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+args.timeout
        while child.poll() is None and time.monotonic()<deadline: time.sleep(.5)
        if child.poll() is None:
            child.terminate();child.wait(timeout=20);raise TimeoutError(runtime)
    text=(home/'qconsole.log').read_text(errors='replace')
    if child.returncode or 'BAKED_RUNTIME_DONE' not in text or re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|baked-summary (?:failed|unavailable)',text):
        raise AssertionError('Failed actual SP classification: '+str(runtime)+'\n'+text[-2500:])
    results={}
    from PIL import Image, ImageStat
    recorded={}
    # SP writecam can retain the previous map filename; identify camera records
    # by their explicit tags, never by the destination filename alone.
    for path in (home/'maps').glob('*_cam.map'):
        for tag,origin,angles,fov in re.findall(r'"targetname"\s*"([^"]+)"\s*"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"',path.read_text()):
            recorded[tag]=[origin,angles,fov]
    for name in maps:
        section=re.search(f'BAKED_{name}_BEGIN(.*?)BAKED_{name}_END',text,re.S)
        if not section: raise AssertionError('Missing map diagnostics')
        cameras=[recorded.get(f'classify_{name}_{layer}') for layer in ('base','repeat')]
        if cameras[0] is None or cameras[0]!=cameras[1]: raise AssertionError('Unstable camera '+name)
        shots=home/'screenshots'
        if max(ImageStat.Stat(Image.open(shots/f'classify-{name}-base.tga').convert('RGB')).stddev)<2:
            raise AssertionError('Flat/empty runtime frame '+name)
        same=compare(shots/f'classify-{name}-base.tga',shots/f'classify-{name}-repeat.tga')
        if not same['passed']: raise AssertionError('Classification changed the controlled frame '+str(same))
        for layer in ('base','repeat'): Image.open(shots/f'classify-{name}-{layer}.tga').save(runtime/f'{name}-{layer}.png')
        lines=re.findall(r'baked-(?:summary|categories|luxels|style|exclusion)[^\r\n]+',section[1])
        fields={}
        for line in lines:
            if line.startswith('baked-summary surfaces=') or line.startswith('baked-luxels texels='):
                fields.update((k,int(v)) for k,v in re.findall(r'([\w-]+)=(\d+)\b',line))
        if fields.get('valid',0)<100 or fields.get('charts',0)<10 or (fixture and fields.get('paired-deluxe',0)<1):
            raise AssertionError('Missing or implausible scan '+str(lines))
        if fields['deluxe-valid']+fields['deluxe-unavailable']!=fields['valid'] or fields['fit-candidates']+fields['excluded']!=fields['texels']:
            raise AssertionError('Inconsistent mutually exclusive scan totals '+str(fields))
        directions=re.findall(r'baked-direction[^\r\n]+',section[1])
        if len(directions)!=3: raise AssertionError('Missing explicit direction diagnostics')
        if fixture and not all(expected in line for expected,line in zip(
                ('state=ZeroSentinel raw=0,0,0','state=Neutral raw=127,127,127','state=Available raw=128,128,255'),directions)):
            raise AssertionError('Incorrect external direction decoding '+str(directions))
        if fixture:
            captured={f'{i}:{role}':digest for i,role,digest in re.findall(r'lm-observation index=(\d+) role=(\w+)[^\r\n]*?hash=([0-9a-f]{64})',section[1])}
            for identity,digest in fixture['expected_pixel_sha256'].items():
                if captured.get(identity)!=digest: raise AssertionError('External pixels changed at '+identity)
        results[name]=dict(diagnostics=lines,cameras=cameras,unchanged=same,
                          directions=directions,
                          map_identity=re.findall(r'observation-map=[^\r\n]+',section[1]),
                          original_capture=re.findall(r'(?:luxel-geometry|observations|grid-observations)=[^\r\n]+',section[1]))
    result=dict(passed=True,maps=results,settings=settings,archives=archives,fixture=fixture,command=command,controls=commands(maps),
                renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest())
    (runtime/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return 0


if __name__=='__main__': raise SystemExit(main())
