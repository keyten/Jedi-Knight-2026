"""Trusted ASR23 author/export controller. Run before any optimizer process.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

from .solver import load, write, sha
from tools.rend2.validation.bridge import mapped_rows, geometry, entity
from tools.rend2.datasets.prepare_blind import assignment
from tools.rend2.adaptive_hidden.run import spatial
from tools.rend2.grid_triangulation.triangulate import Bridge, role
from tools.rend2.bake_model.bake_model import COMPILER_SHA256

ROOT = Path(__file__).resolve().parents[3]
NAMES = ('point_room', 'spot', 'panel', 'tube', 'two_colour', 'overlap_whites',
         'sun_sky', 'skylight', 'bounce_colour', 'bounce_control', 'door_occlusion', 'wall_leak')


def execute(command, log):
    r = subprocess.run(list(map(str, command)), capture_output=True, text=True, errors='replace', timeout=180)
    log.write_text(r.stdout+r.stderr, encoding='utf-8')
    if r.returncode or re.search(r'Entity leaked|\*+\s*ERROR\b', r.stdout+r.stderr, re.I):
        raise ValueError('Author/capture failed: '+str(log))


def extras(work, compiler, native, bakes):
    if sha(compiler) != COMPILER_SHA256:
        raise ValueError('Pinned compiler required')
    folder = work/'authoring'; base = folder/'base'
    base.mkdir(parents=True)
    for name in ('shaders', 'textures'):
        shutil.copytree(bakes/'authoring/base'/name, base/name)
    (base/'maps').mkdir()
    specs = []
    for name in ('empty', 'mixed_sun_local'):
        original = 'point_room' if name == 'empty' else 'sun_sky'
        text = (bakes/'authoring/base/maps'/(original+'.map')).read_text()
        if name == 'empty':
            text = re.sub(r'\{\s*"classname"\s*"light"[^{}]*\}', '', text)
            sources = []
        else:
            sources = [dict(kind='point', position=[-80, 32, 176])]
            text += entity(dict(classname='light', origin='-80 32 176', light='280', _color='1 .2 .1'))
        mp = base/'maps'/(name+'.map'); mp.write_text(text, encoding='ascii')
        for i, stage in enumerate((['-meta', '-leaktest', '-noflares', '-samplesize', '16'], ['-vis'],
                ['-light', '-deluxe', '-deluxemode', '0', '-gamma', '1', '-compensate', '1', '-exposure', '0', '-bounce', '0'])):
            execute([compiler, '-game', 'ja', '-fs_basepath', folder, '-fs_game', 'base', '-threads', '1', *stage, mp],
                    folder/(name+'-'+str(i)+'.log'))
        bsp = mp.with_suffix('.bsp')
        raw, prefix = folder/(name+'.jkobs'), folder/name
        execute([native/'lightrecon_regions_inspect.exe', bsp, prefix, 0,0,128,512,128,32,1,raw], folder/(name+'-capture.log'))
        execute([native/'lightrecon_dataset_quality.exe', 'scan', raw, prefix], folder/(name+'-scan.log'))
        answer = work/'private'/(name+'-authored-answer.json')
        write(answer, dict(sources=sources, compiled_bsp_sha256=sha(bsp),
                           solar=name == 'mixed_sun_local', authored_before_inference=True,
                           compiler_sha256=sha(compiler), source_map_sha256=sha(mp)))
        specs.append(dict(name=name, bsp=bsp, raw=raw, sanitized=prefix.with_suffix('.public.jkobs'), answer=answer))
    return specs


def surface_fixtures(surfaces):
    fixtures = []
    for s in surfaces:
        # This is an owned shader semantic, not an entity/source-answer lookup.
        if not s['shader']['name'].endswith(('/panel', '/tube')) or not s['triangles']:
            continue
        n = s['triangles'][0].geometric_normal
        if n[2] < -.99:
            fixtures.append(dict(kind='surface', surface=s['ordinal'], cue='fixture',
                                 provenance='Original owned full-face emitting shader and downward face'))
    return fixtures


def sampled_folds(rows, policy):
    folds = {key: [] for key in ('train', 'validation', 'heldout')}
    for r in rows:
        key = assignment(r, policy['block_world'], policy['gap_world'], policy['seed'], policy['origin_world'])
        if key:
            r = dict(r)
            r['block'] = [math.floor((v-o)/policy['block_world']) for v, o in zip(r['position'], policy['origin_world'])]
            folds[key].append(r)
    return {key: spatial(sorted(values, key=lambda r: r['id']), policy['rows_per_fold']) for key, values in folds.items()}


def original_grid(lumps, folder, sanitized, native):
    """Trusted original JA records + indirection; native BSP domain classifies them.

    The geometry-only captures used by the old bridge omit grid storage. Read
    the original compiled bytes rather than synthesizing observations/probes.
    """
    bounds = struct.unpack_from('<6f', lumps[7])
    entities = lumps[0].split(b'}',1)[0].decode('ascii')
    match = re.search(r'"gridsize"\s*"([^\"]+)"',entities)
    spacing = list(map(float,match[1].split())) if match else [64.,64.,128.]
    if len(spacing) != 3 or any(not math.isfinite(v) or v <= 0 for v in spacing):
        raise ValueError('Invalid original grid spacing')
    origin = [s*math.ceil(v/s) for s,v in zip(spacing,bounds[:3])]
    shape = [1+int((s*math.floor(v/s)-o)/s) for s,v,o in zip(spacing,bounds[3:],origin)]
    count = math.prod(shape)
    if count > 4096 or len(lumps[17]) != 2*count or len(lumps[15])%30:
        raise ValueError('Original JA grid layout/record size mismatch')
    indices = struct.unpack('<'+'H'*count,lumps[17])
    result = []
    for i,index in enumerate(indices):
        if index*30+30 > len(lumps[15]):
            continue
        xyz = [i%shape[0],(i//shape[0])%shape[1],i//(shape[0]*shape[1])]
        p = [o+k*s for o,k,s in zip(origin,xyz,spacing)]
        record = lumps[15][index*30:(index+1)*30]
        polar,azimuth = [v*2*math.pi/256 for v in record[28:30]]
        styles = [dict(id=style,directed=list(record[12+3*slot:15+3*slot]),
                       weak=max(record[12+3*slot:15+3*slot]) <= 2)
                  for slot,style in enumerate(record[24:28]) if style != 255]
        result.append(dict(ordinal=i,xyz=xyz,position=p,present=True,
            correlation_key=hashlib.sha256(record).hexdigest(),styles=styles,
            consumer256=[math.cos(azimuth)*math.sin(polar),math.sin(azimuth)*math.sin(polar),math.cos(polar)],
            dynamic_possible=False))
    path=folder/'grid-geometry.request'
    path.write_text('JKPOINTS23 '+str(len(result))+'\n'+'\n'.join(' '.join(map(str,r['position'])) for r in result)+'\n',encoding='ascii')
    child=subprocess.run([str(native/'lightrecon_blind_basis_tool.exe'),str(sanitized),str(path)],capture_output=True,text=True,timeout=60)
    if child.returncode:
        raise ValueError('Original grid geometry classification: '+child.stderr)
    for r,location in zip(result,json.loads(child.stdout)['locations']):
        r.update(space={0:'free',1:'solid',2:'outside'}.get(location['space'],'unknown'),boundary=location['boundary'])
    return result


def original_bridge_folds(folder, policy):
    from tools.rend2.datasets.solver_view import TrainingView
    from tools.rend2.datasets.evaluation_view import EvaluationView
    manifest = load(folder/'views/inputs/manifest.json')
    t = TrainingView(folder/'views/inputs', manifest['map_sha256'])
    e = EvaluationView(folder/'views/evaluator', manifest['map_sha256'], permitted=('validation','heldout'))
    folds = {key: spatial(sorted(list(t.observations()) if key == 'train' else list(e.observations(key)), key=lambda r: r['id']),
                          policy['rows_per_fold']) for key in ('train','validation','heldout')}
    # Original V02 blocks use its already frozen assignment; no RGB re-partition.
    m = load(folder/'views/inputs/manifest.json')
    origin = m.get('partition_origin_world', [0,0,0]); block = m['block_world']
    for values in folds.values():
        for r in values:
            r['block'] = [math.floor((v-o)/block) for v,o in zip(r['position'], origin)]
    return folds


def freeze(a):
    a.work.mkdir(parents=True, exist_ok=False)
    (a.work/'private').mkdir(); (a.work/'public').mkdir()
    policy = load(a.policy)
    shutil.copyfile(a.policy, a.work/'policy.json')
    lock = load(ROOT/'tools/rend2/datasets/q3map2-validation.json')
    specs = []
    for name in NAMES:
        bsp = a.bakes/'authoring/base/maps'/(name+'.bsp')
        answer = a.bakes/'answers'/(name+'.q3map-recipe.json')
        if sha(bsp) != lock['maps'][name]['bsp_sha256']:
            raise ValueError('Original BSP lock mismatch '+name)
        specs.append(dict(name=name, bsp=bsp, raw=a.captures/(name+'.jkobs'),
                          sanitized=a.captures/(name+'.public.jkobs'), answer=answer))
    specs += extras(a.work, a.compiler, a.native, a.bakes)
    for variant in ('direct', 'passive_bounce1'):
        folder = a.bridge/'bridge'/variant
        specs.append(dict(name='bridge_'+variant, bsp=a.bridge/'bridge/authoring/base/maps'/('v02_connected_rooms_v1_'+variant+'.bsp'),
                          raw=folder/'raw-geometry.jkobs', sanitized=folder/'views/inputs/geometry.jkobs',
                          answer=a.bridge/'bridge/private/source-answer.json', original_bridge=folder))
    protocol = dict(schema='org.openjk.asr23-frozen.v1', policy=policy, scenes=[],
        compiler_sha256=sha(a.compiler), branch=subprocess.check_output(['git','branch','--show-current'], text=True).strip(),
        base_revision=subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip(),
        remote=subprocess.check_output(['git','remote','get-url','origin'], text=True).strip(),
        target_domain='Conditional same-material forward RGB8 appearance, fixed gamma=1; no calibrated irradiance',
        solver_boundary='Public directory only by argument/API; no OS filesystem sandbox claimed',
        grid_holdout='Original grid records hashed independently; only train records public. Lightmap illumination scores condition on this allowed grid evidence.',
        stock_source_positions='Unavailable', source_answers_pinned_before_inference=True)
    sources = sorted((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    sources += [ROOT/'shared/rd-rend2'/n for n in ('tr_lightrecon_types.h','tr_lightrecon_types.cpp')]
    sources += list(Path(__file__).parent.glob('*.py'))+[a.policy, ROOT/'tools/lightrecon_test/lightrecon_blind_basis_tool.cpp',
            ROOT/'tools/lightrecon_test/CMakeLists.txt', ROOT/'tools/lightrecon_test/adaptive_hidden.cpp', ROOT/'tools/lightrecon_test/adaptive_hidden.h',
            ROOT/'tools/lightrecon_test/lightrecon_hidden_tool.cpp']
    protocol['sources'] = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources}
    prior = load(ROOT/'tools/rend2/iterative_discovery/results/asr22-v1/protocol.json')['sources']
    for name, digest in protocol['sources'].items():
        if name.startswith('shared/') and prior.get(name) != digest:
            raise ValueError('Historical core/canonical contract changed since ASR22: '+name)
    protocol['binaries'] = {name: sha(a.native/(name+'.exe')) for name in ('lightrecon_adaptive_tool','lightrecon_blind_basis_tool','lightrecon_grid_geometry_tool')}
    for spec in specs:
        name, bsp = spec['name'], spec['bsp']
        folder = a.work/'public'/name; folder.mkdir()
        # Pin the actual answer bytes without opening source parameters.
        answer = a.work/'private'/(name+'-answer.json')
        shutil.copyfile(spec['answer'], answer)
        digest = sha(bsp)
        rows, (lumps, surfaces, casters) = mapped_rows(bsp)
        # One original receiver response; colored reflectors stay in geometry.
        rows = [r for r in rows if next(s for s in surfaces if s['ordinal'] == r['surface'])['shader']['name'].endswith('/white')]
        solid_empty = name == 'empty' and not rows
        if solid_empty:
            # q3map2 collapses a black surface to a solid-page texel and removes
            # its UV Jacobian. Repeated geometry queries are correlated training
            # evidence only: they cannot manufacture an independent held-out fold.
            if max(lumps[14][:128*128*3]) != 0:
                raise ValueError('Empty solid original color page is not black')
            for t in casters:
                p = [sum(v[k] for v in t.world)/3 for k in range(3)]
                rows.append(dict(id=hashlib.sha256((digest+'/solid-training/'+t.id).encode()).hexdigest(),
                     surface=t.surface,position=p,normal=list(t.geometric_normal),area=1,raw=[0,0,0],
                     transfer=1,primaries=1,block=[int(t.surface),0,0]))
            folds = dict(train=rows,validation=[],heldout=[])
        else:
            folds = original_bridge_folds(spec['original_bridge'], policy) if 'original_bridge' in spec else sampled_folds(rows, policy)
        if not solid_empty and any(len(v) < 8 for v in folds.values()):
            raise ValueError('Insufficient frozen fold support '+name+': '+str({k:len(v) for k,v in folds.items()}))
        if any({tuple(r['block']) for r in folds[k]} & {tuple(r['block']) for r in folds[j]}
               for k,j in itertools_pairs(folds)):
            raise ValueError('Cross-fold spatial block leakage '+name)
        bound = [min(r['position'][k] for r in rows)-16 for k in range(3)]+[max(r['position'][k] for r in rows)+16 for k in range(3)]
        grid = original_grid(lumps, folder, spec['sanitized'], a.native)
        public_grid = [r for r in grid if r['correlation_key'] and role(r['correlation_key'], digest) == 'train']
        receivers = []
        for values in folds.values():
            receivers += [{k:r[k] for k in ('id','surface','position','normal','area','block')} for r in values]
        sky = any(s['shader']['flags'] & 0x2000 for s in surfaces)
        solar = []
        if sky:
            # Source-independent fixed directional alternatives, never authored sun RGB/XYZ.
            for axis in ([0,0,-1],[.7071067812,0,-.7071067812],[-.7071067812,0,-.7071067812],
                         [0,.7071067812,-.7071067812],[0,-.7071067812,-.7071067812]):
                solar.append(dict(kind='sun',position=[0,0,0],axis=axis,cue='sun'))
        public = dict(map_sha256=digest, training=folds['train'], receivers=receivers,
                      grid=public_grid, fixtures=surface_fixtures(surfaces), solar=solar, bounds=bound)
        # Strip every non-training target and raw-derived scalar from public geometry.
        write(folder/'public.json', public)
        shutil.copyfile(spec['sanitized'], folder/'geometry.jkobs')
        write(a.work/'private'/(name+'-folds.json'), dict(validation=folds['validation'], heldout=folds['heldout']))
        rec = dict(name=name, map_sha256=digest, answer_sha256=sha(answer), public_sha256=sha(folder/'public.json'),
                   geometry_sha256=sha(folder/'geometry.jkobs'), evaluator_sha256=sha(a.work/'private'/(name+'-folds.json')),
                   counts={key:len(values) for key,values in folds.items()}, grid_train=len(public_grid), grid_total=len(grid),
                   source_answer_path=str(spec['answer']), original_bsp_path=str(bsp),
                   fold_scope='Solid-black original page; correlated training-only negative control, predictive holdout Unavailable' if solid_empty else
                              'Original V02 folds; geometry-only farthest subset' if 'original_bridge' in spec else 'New frozen footprint-eroded spatial blocks; geometry-only farthest subset',
                   full_raw_capture_sha256=sha(spec['raw']))
        protocol['scenes'].append(rec)
        print('Frozen', name, rec['counts'], 'grid', len(public_grid), flush=True)
    write(a.work/'protocol.json', protocol)
    return protocol


def itertools_pairs(keys):
    keys = list(keys)
    return [(a,b) for i,a in enumerate(keys) for b in keys[i+1:]]


def parser():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--policy', type=Path, default=Path(__file__).with_name('policy-v1.json'))
    p.add_argument('--native', type=Path, default=ROOT/'build/s06-gcc')
    p.add_argument('--bakes', type=Path, default=ROOT/'build/q3map2-bake-verified')
    p.add_argument('--captures', type=Path, default=ROOT/'build/x02/compiled-fixtures-frozen')
    p.add_argument('--bridge', type=Path, default=Path(os.environ.get('TEMP','/tmp'))/'jk-lightrecon-v02-bridge-v2')
    p.add_argument('--compiler', type=Path, default=ROOT/'build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe')
    return p


if __name__ == '__main__':
    a = parser().parse_args()
    for key,value in vars(a).items():
        if isinstance(value, Path):
            setattr(a,key,value.resolve())
    freeze(a)
