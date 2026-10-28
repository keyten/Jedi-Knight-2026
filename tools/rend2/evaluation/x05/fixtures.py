"""Versioned own-content BSP controls, original CPU samples and private truth.
Reuses existing compiler recipes, geometry mapping and trace mathematics.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
for name in ('bake_model','shadow_footprint','datasets'):
    sys.path.insert(0,str(HERE.parents[1]/name))
from compare_bakes import brush, entity, make_assets, floor_samples, encode_color, predict, CASES, bsp, sha
from compiler_boundary import load as _load_geometry
from compiler_trace import Replay
from geometry import ray_triangle, sub, unit
from bake_model import point_scalar, spot_factor, COMPILER_SHA256
from hypotheses import continuous_encode
from prepare_blind import assignment, FIELDS


def load_geometry(path):
    return _load_geometry(path,strict_receiver_uv=False)


def write(path, value):
    Path(path).write_text(json.dumps(value,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')


def definition(name):
    box=[brush((-400,-400,-16),(400,400,0)),brush((-400,-400,0),(-384,400,432)),
         brush((384,-400,0),(400,400,432)),brush((-384,-400,0),(384,-384,432)),
         brush((-384,384,0),(384,400,432))]
    sky=name in ('x05_sky_aperture_v1','x05_sky_sealed_v1','x05_sun_positive_v1')
    if name=='x05_sky_aperture_v1':
        box += [brush((-384,-384,400),(-192,384,416)),brush((192,-384,400),(384,384,416)),
                brush((-192,-384,400),(192,-192,416)),brush((-192,192,400),(192,384,416)),
                brush((-384,-384,416),(384,384,432),'x05sky')]
    else:
        roof='x05panel' if name=='x05_panel_positive_v1' else 'x05sun' if name=='x05_sun_positive_v1' else 'white'
        box += [brush((-384,-384,400),(384,384,416),roof)]
        if name=='x05_sky_sealed_v1':box += [brush((-384,-384,416),(384,384,432),'x05sky')]
    source=[]
    if name in ('x05_wall_pair_v1','x05_doorway_pair_v1','x05_doorway_pair_v2','x05_doorway_pair_v3'):
        if name=='x05_wall_pair_v1':box += [brush((-2,-384,0),(2,384,400))]
        elif name in ('x05_doorway_pair_v1','x05_doorway_pair_v3'):box += [brush((-2,-384,0),(2,-96,400)),brush((-2,96,0),(2,384,400)),brush((-2,-96,256),(2,96,400))]
        else:
            # Two separated apertures provide independent spatial support.
            # v1 is retained as an explicit failed-admission diagnostic.
            box += [brush((-2,-384,0),(2,-192,400)),brush((-2,-64,0),(2,64,400)),
                    brush((-2,192,0),(2,384,400)),brush((-2,-192,256),(2,-64,400)),brush((-2,64,256),(2,192,400))]
        source=[dict(origin=[-160.,0.,192.],rgb=[1.,1.,1.],intensity=1200. if name.endswith('_v3') else 300.,kind='point')]
    text=entity({'classname':'worldspawn','gridsize':'64 64 64'})
    text=text[:-2]+''.join(box)+'}\n'
    text+=entity({'classname':'info_player_start','origin':'-128 -64 64'})
    if source:
        text+=entity({'classname':'info_player_deathmatch','origin':'128 -64 64'})
        text+=entity({'classname':'light','origin':'-160 0 192','light':str(source[0]['intensity']),'_color':'1 1 1'})
    return text,dict(id=name,components=['sky'] if name.startswith('x05_sky_') else ['sun'] if name=='x05_sun_positive_v1'
                     else ['surface_emission'] if name=='x05_panel_positive_v1' else ['direct'],sources=source,
        sky=sky,profile='sky17' if name.startswith('x05_sky_') else 'sun' if name=='x05_sun_positive_v1'
            else 'area' if name=='x05_panel_positive_v1' else 'isq',
        compiler_photometry='Legacy relative; no SI calibration')


def compile_suite(compiler, output, names):
    compiler=Path(compiler).resolve(); output=Path(output).resolve()
    if sha(compiler)!=COMPILER_SHA256:raise ValueError('Pinned compiler executable hash required')
    output.mkdir(parents=True,exist_ok=False);base=output/'authoring/base';make_assets(base)
    path=base/'shaders/bake_forward.shader'
    path.write_text(path.read_text()+'''
textures/bake_forward/x05sky
{ surfaceparm sky
 surfaceparm nolightmap
 q3map_lightRGB 1 1 1
 q3map_skyLight 120 3
 skyparms - 512 -
}
textures/bake_forward/x05sun
{ surfaceparm sky
 surfaceparm nolightmap
 q3map_sun 1 1 1 120 0 90
 skyparms - 512 -
}
textures/bake_forward/x05panel
{ q3map_surfacelight 200
 q3map_lightRGB 1 1 1
 q3map_lightSubdivide 4096
 q3map_backsplash 0 0
 { map textures/bake_forward/white.tga
 }
}
''',encoding='ascii',newline='\n')
    results={}
    for name in names:
        source,truth=definition(name);mp=base/'maps'/(name+'.map');mp.write_text(source,encoding='ascii',newline='\n')
        commands=[]
        for i,stage in enumerate((['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],
            ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1','-exposure','0','-bounce','0','-super','1','-nocollapse'])):
            cmd=[str(compiler),'-game','ja','-fs_basepath',str(base.parent),'-fs_game','base','-threads','1',*stage,str(mp)]
            result=subprocess.run(cmd,capture_output=True,text=True,errors='replace',timeout=180)
            (output/(name+'-'+str(i)+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            if result.returncode or re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked|Unknown (?:argument|option)',result.stdout,re.I):
                raise ValueError('Compiler failed '+name+': '+result.stdout[-1500:])
            commands.append(cmd)
        truth.update(map_sha256=sha(mp),bsp_sha256=sha(mp.with_suffix('.bsp')),compiler_sha256=sha(compiler),
                     shader_sha256=sha(path),argv=commands)
        results[name]=truth;write(output/(name+'.recipe.json'),truth)
        print('compiled',name,flush=True)
    write(output/'recipes.json',results);return results


def floor_rows(path, policy, maximum_coordinate=272.):
    lumps,surfaces,triangles=load_geometry(Path(path));digest=sha(Path(path))
    floor=next(s for s in surfaces if s['page']>=0 and s['triangles'] and
               all(abs(p[2])<1e-6 for t in s['triangles'] for p in t.world) and s['triangles'][0].normals[0][2]>.99)
    out=[]
    for r in floor_samples(lumps,maximum_coordinate):
        p=r['world'];t=next(t for t in floor['triangles'] if min(t.bary(t.to_uv(p)))>=-1e-7)
        uv=t.to_uv(p);x=round(uv[0]*128-.5);y=round(uv[1]*128-.5)
        offset=(floor['page']*128*128+y*128+x)*3
        raw=list(lumps[14][offset:offset+3])
        if raw!=r['rgb'] or abs(uv[0]*128-.5-x)>1e-4 or abs(uv[1]*128-.5-y)>1e-4:raise ValueError('Original luxel mapping mismatch')
        identity=hashlib.sha256(f'{digest}/{floor["page"]}/{x}/{y}/{floor["ordinal"]}/0/x05-map-v1'.encode()).hexdigest()
        n=list(t.normals[0]);area=t.jacobian/(128*128)
        row=dict(id=identity,image=floor['page'],page=floor['page']//2,x=x,y=y,surface=floor['ordinal'],slot=0,style=0,
            position=p,normal=n,shading_normal=n,area=area,area_error=0.,
            # Circumscribed radius about the centre, from the actual affine UV
            # basis; sqrt(2*area) is a full diagonal, not a radius.
            footprint_radius=max(math.dist(p,t.evaluate((uv[0]+du/256,uv[1]+dv/256))[0]) for du in (-1,1) for dv in (-1,1)),
            coverage=2,issues=0,classification=25,fit_candidate=1,material_signals=1,material_uncertainty=23,
            kind=0,material='textures/bake_forward/white',raw=raw,encoding=0,
            channel_flags=[1|(8 if v==255 else 0)|(2 if v==0 else 0) for v in raw],transfer=1,primaries=1,
            source=[v/255. for v in raw],scene=None,deluxe_state=1,deluxe=None,
            positive_region=0,negative_region=0,region_state=4,region_reasons=0,
            local_geometry_complete=True,local_geometry_intersects=True,local_geometry_issues=0)
        add_partition(row,policy);out.append(row)
    return out,(lumps,surfaces,triangles)


def add_partition(row,policy):
    s=policy['split'];row['partition']=assignment(row,s['block_world'],s['gap_world'],s['seed'],s['origin_world'])
    row['block']=','.join(str(math.floor((p-o)/s['block_world'])) for p,o in zip(row['position'],s['origin_world']))
    return row


def visible(triangles,origin,endpoint=None,direction=None):
    if direction is None:
        delta=sub(endpoint,origin);maximum=math.sqrt(sum(v*v for v in delta))-.05;direction=unit(delta)
    else:maximum=65536.
    hits=[]
    for t in triangles:
        h=ray_triangle(origin,direction,t,.0,minimum=.5,maximum=maximum)
        if h:hits.append((h[0],t.optical))
    if direction is not None and endpoint is None:
        return bool(hits) and min(hits)[1]=='sky'
    return not any(optical!='sky' for _,optical in hits)


def sky_directions():
    directions=[];angle=0.
    for elevation in (math.pi/12,math.pi/4):
        for _ in range(8):
            directions.append([math.cos(angle)*math.cos(elevation),math.sin(angle)*math.cos(elevation),math.sin(elevation)])
            angle+=math.pi/4
        angle+=math.pi/8
    return directions+[[0,0,1]]


def reference(rows,triangles,truth,model='correct',amplitude=1.,known_visibility=None):
    values=[];states=[]
    for row in rows:
        p=[x+n for x,n in zip(row['position'],row['normal'])];normal=row['normal'];rgb=[0.,0.,0.];pairs=[]
        profile=truth['profile']
        if profile in ('isq','linear','spot','noangle'):
            for si,source in enumerate(truth['sources']):
                origin=source['origin']
                if model=='wrong_geometry':origin=[origin[0]+128,origin[1],origin[2]]
                clear=(known_visibility[row['id']][si]=='Visible' if known_visibility is not None and model!='wrong_geometry'
                       else visible(triangles,p,origin));pairs.append('Visible' if clear else 'Blocked')
                if model=='all_visible':clear=True
                if not clear:continue
                d=sub(origin,p);r=math.sqrt(sum(v*v for v in d));cosine=sum(a*b for a,b in zip(normal,d))/r
                if cosine<=0:continue
                linear=profile=='linear'
                if model=='wrong_profile':linear=not linear
                value=point_scalar(source['intensity']*amplitude,r,1. if linear or profile=='noangle' else cosine,linear=linear)
                if profile=='spot' and model!='wrong_spot':
                    value*=spot_factor(p,origin,source.get('axis',[0,0,-1]),128.,source.get('radius',64.))
                    if value<=1:value=0.
                for c in range(3):rgb[c]+=value*source['rgb'][c]
        elif profile in ('sky17','sun'):
            dirs=sky_directions() if profile=='sky17' else [[0.,0.,1.]]
            for d in dirs:
                clear=visible(triangles,p,direction=d);pairs.append('VisibleSky' if clear else 'BlockedSky')
                if model=='all_visible':clear=True
                if clear:
                    value=120./len(dirs)*max(0.,sum(a*b for a,b in zip(normal,d)))
                    rgb=[x+value for x in rgb]
        elif profile=='area':
            # Exactly the existing verified unoccluded ceiling polygon subset.
            rgb=predict(dict(area=True),p);pairs=['VisibleArea']
        elif profile=='ambient':rgb=[truth.get('ambient',20.)]*3
        else:raise ValueError('Unspecified reference profile '+profile)
        # ColorToBytes shared-peak arithmetic can produce 255+3e-14 in double.
        # continuous_encode already rejects values outside its roundoff envelope.
        values.append([min(255.,v) for v in continuous_encode(rgb)]);states.append(pairs)
    return values,states


def publish_rows(prefix,rows,map_sha,geometry,compiler_version='2.5.17n-git-d9338309e'):
    prefix=Path(prefix);prefix.parent.mkdir(parents=True,exist_ok=True)
    import shutil
    shutil.copyfile(geometry,prefix.with_suffix('.public.jkobs'))
    with prefix.with_suffix('.rows.jsonl').open('w',encoding='utf-8',newline='\n') as stream:
        for r in sorted(rows,key=lambda r:(r['image'],r['y'],r['x'])):
            stream.write(json.dumps({k:v for k,v in r.items() if k not in ('partition','block','groups')},separators=(',',':'))+'\n')
    pages=sorted(set(r['image'] for r in rows))
    write(prefix.with_suffix('.source.json'),dict(schema='org.openjk.observation-quality.v1',map_sha256=map_sha,
        compiler_known=True,synthetic=True,compiler_version=compiler_version,derived_complete=False,
        rows=len(rows),raw_pixels=len(pages)*128*128,grid_cells=0,grid_present=0,
        grid_directions_quantized_uncalibrated=0,grid_assigned=0,
        pages=[dict(image=i,page=i//2,width=128,height=128,missing=0) for i in pages],
        scope='Explicit controlled planar floor query subset; not all-page observation derivation'))
