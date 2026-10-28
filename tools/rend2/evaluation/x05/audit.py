"""Private compiled-fixture census. Counts are evidence, never source recovery.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from collections import Counter
import json
import math
from pathlib import Path
import sys
from fixtures import (HERE, ROOT, write, sha, floor_rows, load_geometry, add_partition,
                      visible, sky_directions, CASES)
from prepare_blind import rows as stream_rows, eligible


def quantiles(values):
    values=sorted(values)
    return {str(q):values[round(q*(len(values)-1))] if values else None for q in (0.,.1,.5,.9,1.)}


def stats(rows,sources=()):
    area=sum(r['area'] for r in rows);normals=Counter();surfaces=set();blocks=set();brightness=[];distances=[]
    histogram=Counter();saturated=positive=0;by_normal={}
    bounds=[[math.inf]*3,[-math.inf]*3]
    for r in rows:
        surfaces.add(r['surface']);blocks.add(r['block']);n=r['normal']
        key=','.join(str(round(v,3)) for v in n);normals[key]+=1
        entry=by_normal.setdefault(key,dict(count=0,sum_mean_rgb=0.,peak=0,area=0.))
        entry['count']+=1;entry['sum_mean_rgb']+=sum(r['raw'])/3.;entry['peak']=max(entry['peak'],max(r['raw']));entry['area']+=r['area']
        brightness.append(sum(r['raw'])/3.);histogram[str(max(r['raw'])//16*16)]+=1
        saturated+=sum(v==255 for v in r['raw']);positive+=sum(v>=8 for v in r['raw'])
        for k,x in enumerate(r['position']):bounds[0][k]=min(bounds[0][k],x);bounds[1][k]=max(bounds[1][k],x)
        for s in sources:distances.append(math.dist(r['position'],s['origin']))
    return dict(count=len(rows),known_area_world2=area,independent_receiver_surfaces=len(surfaces),spatial_blocks=len(blocks),
        normal_direction_counts=dict(normals),position_bounds=bounds if rows else None,
        normal_signal={k:dict(count=v['count'],mean_encoded_rgb=v['sum_mean_rgb']/v['count'],maximum_code=v['peak'],known_area_world2=v['area']) for k,v in by_normal.items()},
        source_distance_world_quantiles=quantiles(distances),encoded_mean_rgb_quantiles=quantiles(brightness),
        encoded_peak_histogram=dict(sorted(histogram.items(),key=lambda x:int(x[0]))),
        channels_above_policy_floor=positive,channel_denominator=3*len(rows),saturated_channels=saturated,
        domain='Original RGB8; no linear luminance/physical intensity inferred')


def source_truth(recipe):
    g=recipe['ground_truth'];sources=[]
    for r in g['lights']:
        sources.append(dict(origin=r['position_world'],rgb=r['rgb_parameter'],intensity=r['authored_intensity'],kind=r['type'],
                            axis=r.get('axis_propagation',[0,0,-1])))
    return sources


def visibility_stats(rows,triangles,truth):
    selected={}
    for r in sorted(rows,key=lambda r:r['id']):
        key=(r['surface'],*(math.floor(v/64) for v in r['position']))
        selected.setdefault(key,r)
    selected=list(selected.values())[:256];counts=Counter();boundaries=0
    for r in selected:
        p=[v+n for v,n in zip(r['position'],r['normal'])]
        queries=[('source',s['origin']) for s in truth.get('sources',[])]
        if truth.get('sky') or truth.get('profile') in ('sky17','sun'):
            queries += [('sky',d) for d in sky_directions()]
        for kind,value in queries:
            clear=visible(triangles,p,value) if kind=='source' else visible(triangles,p,direction=value)
            counts['Visible' if clear else 'Blocked']+=1
            if kind=='source':
                tangent=next(k for k in range(3) if abs(r['normal'][k])<.1)
                near=[]
                for sign in (-1,1):
                    q=list(p);q[tangent]+=sign*min(8.,r.get('footprint_radius',8.))
                    near.append(visible(triangles,q,value))
                boundaries+=any(v!=clear for v in near)
    return dict(receiver_queries=len(selected),known_visible_pairs=counts['Visible'],known_blocked_pairs=counts['Blocked'],
        unknown_pairs=0,boundary_sensitive_receivers=boundaries,
        coverage='Geometry-only representative surface/64WU cells, capped at 256 receivers; not extrapolated to all texels',
        semantics='Explicit opaque/sky self-authored planar scene only; these are private reference labels, not PVS')


def audit_old(compiled12,recipes12,compiled22,policy,output):
    result={};compiled12,recipes12,compiled22=map(Path,(compiled12,recipes12,compiled22))
    for path in sorted(compiled12.glob('*.source.json')):
        name=path.name.removesuffix('.source.json');meta=json.loads(path.read_text())
        recipe=json.loads((recipes12/(name+'.q3map-recipe.json')).read_text());sources=source_truth(recipe)
        if meta['map_sha256']!=recipe['compiled_bsp_sha256']:raise ValueError('Original fixture identity mismatch')
        rows=[add_partition(r,policy) for r in stream_rows(compiled12/(name+'.rows.jsonl')) if eligible(r)]
        by_id={r['id']:r for r in rows}
        old={}
        for role,directory in [('train','inputs'),('validation','evaluator'),('heldout','evaluator')]:
            rs=[json.loads(line) for line in (compiled12/(name+'-views')/directory/(role+'.jsonl')).read_text().splitlines()]
            old_manifest=json.loads((compiled12/(name+'-views')/'inputs/manifest.json').read_text())
            for r in rs:
                r['footprint_radius']=by_id[r['id']]['footprint_radius']
                r['block']=','.join(str(math.floor(v/old_manifest['block_world'])) for v in r['position'])
            s=stats(rs,sources);s['zero_interval_rmse_nearest_historical']=math.sqrt(sum(sum(max(v-.5,0.)**2 for v in r['raw'])*r['area'] for r in rs)/(3*sum(r['area'] for r in rs))) if rs else None
            old[role]=s
        bsp_path=recipes12.parent/'authoring/base/maps'/(name+'.bsp')
        _,_,triangles=load_geometry(bsp_path)
        g=recipe['ground_truth'];components=['direct'] if sources else []
        if g.get('sun'):components+=['sun','sky']
        if g.get('surface_emitter'):components+=['surface_emission: six-face prism, not native tube']
        if g['bounce_iterations']:components+=['bounce mixed with direct; no component replay oracle']
        result['compiled12/'+name]=dict(source_sha256=meta['map_sha256'],recipe_sha256=sha(recipes12/(name+'.q3map-recipe.json')),
            eligible=stats(rows,sources),old_partitions=old,
            new_geometry_partitions={p:stats([r for r in rows if r['partition']==p],sources) for p in ('train','validation','heldout')},
            visibility=visibility_stats(rows,triangles,dict(sources=sources,sky=bool(g.get('sky')))),components=components,
            raw_scope='Every eligible original captured colour observation; padding/unsupported excluded with original taxonomy',
            original_colour_pixel_denominator=sum(p['width']*p['height'] for p in meta['pages'] if not p['missing']))
        print('audited',name,flush=True)
    frozen=json.loads((HERE.parents[1]/'bake_model/validation.json').read_text())
    for c in CASES:
        name=c['name'];path=compiled22/'authoring/base/maps'/(name+'.bsp')
        if sha(path)!=frozen['cases'][name]['bsp_sha256']:raise ValueError('Frozen original compiler BSP changed '+name)
        rows,(_,_,triangles)=floor_rows(path,policy)
        sources=[] if c.get('sun') or c.get('area') or c.get('no_emit') else [dict(origin=[0,0,192],rgb=c.get('color',[1,1,1]),intensity=c.get('intensity',300))]
        result['compiled22/'+name]=dict(source_sha256=sha(path),eligible=stats(rows,sources),
            partitions={p:stats([r for r in rows if r['partition']==p],sources) for p in ('train','validation','heldout')},
            visibility=visibility_stats(rows,triangles,dict(sources=sources,sky=bool(c.get('sun')))),
            components=['bounce plus direct'] if c.get('bounces') else ['sun/sky'] if c.get('sun') else ['area'] if c.get('area') else ['ambient'] if c.get('no_emit') else ['direct'],
            raw_scope='Existing independent planar-floor compiler-verification receiver domain, not full-map luxel coverage')
    write(output,result);return result
