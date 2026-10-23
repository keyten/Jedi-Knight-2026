#!/usr/bin/env python3
"""Actual SP luxel overlay and source-geometry numerical validation. Stock-only private runtime."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import zipfile
from test_lightrecon_legacy_runtime import ROOT, prepare_runtime
from gl43_validation.compare_images import compare

SCENES = [('face', (3464, 700, 85, 90), 4, 2, -1),
          ('patch', (3464, 752, 85, 180), 16, 1, 319)]


def read_bsp(installation):
    for a in sorted((installation/'base').glob('assets[0-3].pk3'), reverse=True):
        with zipfile.ZipFile(a) as z:
            if 'maps/hoth3.bsp' in z.namelist():
                return z.read('maps/hoth3.bsp')
    raise FileNotFoundError('maps/hoth3.bsp')


def source_geometry(bsp):
    def lump(i):
        off, size = struct.unpack_from('<II', bsp, 8+8*i)
        return bsp[off:off+size]
    surfaces, vertices, indices = lump(13), lump(10), lump(11)
    out = []
    for start in range(0, len(surfaces), 148):
        d = surfaces[start:start+148]
        _, _, kind, first, count, fi, ni = struct.unpack_from('<7i', d)
        out.append(dict(kind=kind, lm=struct.unpack_from('<i', d, 36)[0],
                        vertices=[struct.unpack_from('<3f', vertices, (first+i)*80) for i in range(count)],
                        uv=[struct.unpack_from('<2f', vertices, (first+i)*80+20) for i in range(count)],
                        normals=[struct.unpack_from('<3f', vertices, (first+i)*80+52) for i in range(count)],
                        indices=list(struct.unpack_from('<'+'i'*ni, indices, fi*4)),
                        shape=struct.unpack_from('<2i', d, 140)))
    return out


def ground_truth(surface, uv, with_normal=False):
    import numpy as np
    if '_reference' not in surface:
        points, uvs, normals = (np.asarray(surface[key]) for key in ('vertices','uv','normals'))
        triangles=[]
        if surface['kind'] in (1,3):
            for offset in range(0,len(surface['indices']),3):
                idx=surface['indices'][offset:offset+3]
                p,v=points[idx],uvs[idx]
                matrix=np.column_stack((v[1]-v[0],v[2]-v[0]))
                if abs(np.linalg.det(matrix))<1e-14:
                    continue
                inverse=np.linalg.inv(matrix)
                triangles.append((p,v[0],inverse,np.column_stack((p[1]-p[0],p[2]-p[0])) @ inverse,normals[idx]))
        blocks=[]
        if surface['kind']==2:
            width,height=surface['shape']
            for y in range(0,height-2,2):
                for x in range(0,width-2,2):
                    base=y*width+x
                    a,b,c=uvs[base],uvs[base+2],uvs[base+2*width]
                    matrix=np.column_stack((b-a,c-a))
                    if abs(np.linalg.det(matrix))<1e-14:
                        continue
                    indices=[base+j*width+i for j in range(3) for i in range(3)]
                    expected=np.asarray([a+(b-a)*i/2+(c-a)*j/2 for j in range(3) for i in range(3)])
                    if np.max(np.abs(uvs[indices]-expected))>1e-7:
                        continue
                    blocks.append((points[indices].reshape(3,3,3),normals[indices].reshape(3,3,3),a,np.linalg.inv(matrix)))
        surface['_reference']=(triangles,blocks)
    triangles,blocks=surface['_reference']
    def result(position,du,dv,shading):
        if not with_normal:
            return position
        normal=np.cross(du,dv)
        magnitude=np.linalg.norm(normal)
        if magnitude<1e-12 or not math.isfinite(magnitude):
            return position,None,False
        normal/=magnitude
        known=np.all(np.isfinite(shading)) and np.linalg.norm(shading)>.5 and abs(np.dot(normal,shading))>1e-4
        if known and np.dot(normal,shading)<0:
            normal=-normal
        return position,normal,known
    if surface['kind'] in (1, 3):
        for p,origin,inverse,derivatives,normals in triangles:
            a, b = inverse @ (np.asarray(uv)-origin)
            if min(a, b, 1-a-b) >= -1e-5:
                position=(1-a-b)*p[0]+a*p[1]+b*p[2]
                if not with_normal:
                    return position
                shading=(1-a-b)*normals[0]+a*normals[1]+b*normals[2]
                return result(position,derivatives[:,0],derivatives[:,1],shading)
    if surface['kind'] == 2:
        for points,normals,origin,inverse in blocks:
            s, t = inverse @ (np.asarray(uv)-origin)
            if min(s, t) < -1e-5 or max(s, t) > 1+1e-5:
                continue
            bu = ((1-s)**2, 2*s*(1-s), s*s)
            bv = ((1-t)**2, 2*t*(1-t), t*t)
            position=np.einsum('i,j,jik->k',bu,bv,points)
            if not with_normal:
                return position
            # Stable analytic derivatives of quadratic control-point differences;
            # collapsed patch poles must not acquire normals from cancellation.
            ps=2*np.einsum('i,j,jik->k',(1-s,s),bv,np.diff(points,axis=1))
            pt=2*np.einsum('i,j,jik->k',bu,(1-t,t),np.diff(points,axis=0))
            derivatives=np.column_stack((ps,pt)) @ inverse
            shading=np.einsum('i,j,jik->k',bu,bv,normals)
            return result(position,derivatives[:,0],derivatives[:,1],shading)
    return None


def validate_exports(home, surfaces, image_to_logical):
    result = dict(samples=0, face_samples=0, patch_samples=0, soup_samples=0, maximum_error_world=0.,
                  normal_samples=0,maximum_normal_component_error=0.,maximum_normal_angle_degrees=0.,exports={})
    # Compare the same sparse normals across image-vector layouts. An external
    # alternative can shift vector ordinals without changing original page IDs.
    def logical_page(path):
        return image_to_logical[int(re.search(r'-(\d+)\.obj$', path.name)[1])]
    for path in sorted((home/'lightrecon').glob('luxel-image-*.obj'), key=logical_page):
        text = path.read_text()
        rows = re.findall(r'# luxel (\d+) (\d+) surface (\d+) coverage (\d+) issues (\d+) area ([^\n]+)\n(?:v ([^\n]+)\nv ([^\n]+))?', text)
        mapped = 0
        for xx, yy, ordinal, coverage, issues, area, position, endpoint in rows:
            if int(coverage) != 2:
                if position:
                    raise AssertionError('Uncovered/ambiguous texel acquired a fabricated position')
                continue
            if not position or float(area) <= 0:
                raise AssertionError('Mapped sample lacks finite positive footprint')
            point = tuple(map(float, position.split()))
            truth = ground_truth(surfaces[int(ordinal)], ((int(xx)+.5)/128, (int(yy)+.5)/128))
            if truth is None:
                raise AssertionError('No independent source triangle/patch contains mapped centre')
            error = math.dist(point, truth)
            if error > .0021: # production 0.002 tolerance plus nine-digit debug-text rounding
                raise AssertionError(f'Luxel mapping error {error}: {path.name}, surface {ordinal}, pixel {xx},{yy}')
            result['maximum_error_world'] = max(result['maximum_error_world'], error)
            result['samples'] += 1
            # Sparse independent analytic derivative check; every world position
            # above is checked. Keep runtime verification bounded on large maps.
            if result['samples']%32==0:
                import numpy as np
                _,normal,oriented=ground_truth(surfaces[int(ordinal)],((int(xx)+.5)/128,(int(yy)+.5)/128),True)
                if normal is not None:
                    actual=(np.asarray(tuple(map(float,endpoint.split())))-np.asarray(point))/2
                    # Position text is nine digits; endpoint subtraction includes
                    # its bounded rounding. It is not a binary normal exporter.
                    if not oriented and np.dot(actual,normal)<0:
                        normal=-normal
                    component=float(np.max(np.abs(actual-normal)))
                    if component>1e-4:
                        raise AssertionError(f'Incorrect world normal {component}: {path.name}, surface {ordinal}')
                    cosine=float(np.clip(np.dot(actual/np.linalg.norm(actual),normal),-1,1))
                    result['normal_samples']+=1
                    result['maximum_normal_component_error']=max(result['maximum_normal_component_error'],component)
                    result['maximum_normal_angle_degrees']=max(result['maximum_normal_angle_degrees'],math.degrees(math.acos(cosine)))
            kind = surfaces[int(ordinal)]['kind']
            result[{1:'face_samples', 2:'patch_samples', 3:'soup_samples'}[kind]] += 1
            mapped += 1
        ppm = path.with_suffix('.ppm')
        ordinal=int(re.search(r'-(\d+)\.obj$',path.name)[1])
        result['exports'][str(image_to_logical[ordinal])] = dict(mapped=mapped, obj_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                          coverage_sha256=hashlib.sha256(ppm.read_bytes()).hexdigest())
    if result['face_samples'] < 100 or result['patch_samples'] < 10:
        raise AssertionError('Insufficient actual face/patch correspondence coverage')
    return result


def commands(scan):
    c = ['wait 30', 'devmap hoth3', 'wait 180', 'cam_disable', 'exitview', 'god', 'notarget', 'noclip',
         'cg_draw2D 0', 'cg_drawGun 0', 'cg_thirdPerson 0', 'con_notifytime 0', 'wait 20', 'r_lightReconInfo',
         'r_lightReconLuxels list']
    if scan:
        c += [f'r_lightReconLuxels logical {i} 64' for i in range(38)]
    for label, camera, logical, stride, surface in SCENES:
        c += ['r_lightReconLuxels off', 'uimenu recon_unfreeze', 'fx_freeze 0', 'wait 15',
              'cam_disable','lock_disable','cg_thirdPerson 0','r_drawentities 0',
              f'setviewpos {camera[0]} {camera[1]} {camera[2]} {camera[3]}', 'wait 20',
              'cam_disable','lock_disable','cg_thirdPerson 0',
              f'setviewpos {camera[0]} {camera[1]} {camera[2]} {camera[3]}','wait 15',
              'uimenu recon_freeze','fx_freeze 1','wait 12','cl_paused','sv_paused','viewpos',
              f'writecam luxel_{label}_base',f'screenshot_tga luxel-{label}-base', 'wait 4',
              f'r_lightReconLuxels logical {logical} {stride} {surface}', 'wait 12',
              f'screenshot_tga luxel-{label}-overlay', 'wait 4', 'r_lightReconLuxels off', 'wait 12',
              f'writecam luxel_{label}_repeat',f'screenshot_tga luxel-{label}-repeat', 'wait 4']
    # Screenshot readbacks/writes are asynchronous. Let the final request drain
    # before shutdown rather than treating a logged queued write as a saved file.
    return c+['wait 90', 'echo LUXEL_RUNTIME_DONE', 'quit']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--binaries', type=Path, required=True)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--output', type=Path, default=ROOT/'build/lightrecon-visual-20261022')
    p.add_argument('--label', required=True)
    p.add_argument('--timeout', type=int, default=600)
    p.add_argument('--external-layout', action='store_true', help='Private HDR override disables internal atlas; verify original BSP correspondence unchanged')
    p.add_argument('--no-scan', action='store_true')
    args = p.parse_args()
    if os.name != 'nt' or not re.fullmatch(r'[\w-]+', args.label):
        p.error('Windows and a simple unique label required')
    runtime = args.output.resolve()/('luxels-'+args.label)
    settings, archives = prepare_runtime(args, runtime, 'legacy', 3)
    home = runtime/'home/OpenJK'
    with (home/'openjk_sp.cfg').open('a') as f:
        settings.update(r_subdivisions=1, r_lodCurveError=100000, r_deluxeMapping=0,
                        r_floatLightmap=1 if args.external_layout else 0)
        for key in ('r_subdivisions','r_lodCurveError','r_deluxeMapping','r_floatLightmap'):
            f.write(f'set {key} "{settings[key]}"\n')
    if args.external_layout:
        path = home/'maps/hoth3/lm_0000.hdr'
        path.parent.mkdir(parents=True)
        path.write_bytes(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 128 +X 128\n'+bytes((128,128,128,129))*128*128)
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands(not args.no_scan))+'\n', encoding='ascii')
    command = [str(runtime/'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath','fs_homepath','fs_game'):
        command += ['+set', key, settings[key]]
    si = subprocess.STARTUPINFO();si.dwFlags |= subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
    with (runtime/'launch.log').open('w') as log:
        child = subprocess.Popen(command,cwd=runtime,startupinfo=si,stdout=log,stderr=subprocess.STDOUT)
        deadline = time.monotonic()+args.timeout
        while child.poll() is None and time.monotonic()<deadline:
            time.sleep(.5)
        if child.poll() is None:
            child.terminate();child.wait(timeout=20);raise TimeoutError(runtime)
    text = (home/'qconsole.log').read_text(errors='replace')
    if child.returncode or 'LUXEL_RUNTIME_DONE' not in text or re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|luxel-debug failed|luxel-debug unavailable', text):
        raise AssertionError('Failed actual SP validation '+str(runtime)+'\n'+text[-2000:])
    shots = home/'screenshots'
    from PIL import Image
    comparisons = {}
    camera_file=(home/'maps/hoth3_cam.map').read_text()
    cameras=re.findall(r'"origin"\s*"([^"]+)"\s*"angles"\s*"([^"]+)"\s*"fov"\s*"([^"]+)"',camera_file)
    if len(cameras)!=4 or cameras[0]!=cameras[1] or cameras[2]!=cameras[3]:
        raise AssertionError('Camera changed across geometry overlay')
    for label, *_ in SCENES:
        for layer in ('base','overlay','repeat'):
            Image.open(shots/f'luxel-{label}-{layer}.tga').save(runtime/f'{label}-{layer}.png')
        repeat = compare(shots/f'luxel-{label}-base.tga',shots/f'luxel-{label}-repeat.tga')
        overlay = compare(shots/f'luxel-{label}-base.tga',shots/f'luxel-{label}-overlay.tga')
        if not repeat['passed'] or overlay['differing_pixels']<30:
            raise AssertionError(f'Unstable camera or invisible overlay in {label}: {repeat}, {overlay}')
        comparisons[label]=dict(disabled_repeat=repeat, overlay_changed_pixels=overlay['differing_pixels'])
    index_to_logical={int(a):int(b) for a,b in re.findall(r'luxel-debug image=(\d+) logical=(\d+)',text)}
    numerical = validate_exports(home, source_geometry(read_bsp(args.installation)), index_to_logical)
    result=dict(passed=True,settings=settings,command=command,archives=archives,scenes=SCENES,comparisons=comparisons,
                numerical=numerical,geometry=re.findall(r'luxel-geometry=[^\r\n]+',text),
                mapping_reports=re.findall(r'luxel-debug image=[^\r\n]+',text),
                renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),recorded_cameras=cameras,
                driver=re.findall(r'GL_RENDERER:[^\r\n]+',text),external_layout=args.external_layout)
    (runtime/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('settings','command','mapping_reports','numerical')},indent=2))
    print(json.dumps({k:v for k,v in numerical.items() if k!='exports'},indent=2))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
