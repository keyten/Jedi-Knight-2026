"""ASR04 small numerical study on original compiled planar receivers.
The estimator receives no source answers. Outputs freeze before answer scoring.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.isophotes.prototype import analyze, solve

HERE = Path(__file__).resolve().parent


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    with Path(path).open('w', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, indent=2, sort_keys=True, allow_nan=False)
        f.write('\n')


def pin(path):
    return dict(path=str(path), sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest())


def compiled_patch(path, surface, policy):
    rows = []
    with path.open(encoding='utf-8') as f:
        for count, line in enumerate(f):
            if count >= 32768: raise ValueError('Input row budget')
            r = json.loads(line)
            if (r['surface'] == surface and r['coverage'] == 2 and r['position'] and r['normal']
                    and r['area'] > 0 and r['style'] == 0 and r['fit_candidate']
                    and not (r['issues'] & ((1 << 2) | (1 << 8) | (1 << 11)))):
                rows.append(r)
    if not rows or len(rows) > policy['max_samples']: raise ValueError('Compiled patch unavailable')
    n = rows[0]['normal']; axis = max(range(3), key=lambda k: abs(n[k]))
    if abs(n[axis]) < 1-1e-8: raise ValueError('This bounded adapter tests axis-aligned planes only')
    axes = [k for k in range(3) if k != axis]
    coordinate = rows[0]['position'][axis]
    if any(abs(r['position'][axis]-coordinate) > policy['plane_tolerance_world'] for r in rows):
        raise ValueError('Nonplanar source receiver')
    result = []
    for r in rows:
        normals = [r['normal'], r.get('shading_normal', r['normal'])]
        result.append(dict(uv=[round(r['position'][k], 6) for k in axes], value=sum(r['raw'])/3,
            material=r['material'], saturated=any(x >= 255 or flag & 8 for x, flag in zip(r['raw'], r['channel_flags'])),
            normal_mismatch=any(sum(a*b for a, b in zip(n, other)) < policy['normal_dot_minimum'] for other in normals),
            quantized=True))
    return result, dict(axis=axis, axes=axes, coordinate=coordinate, normal=n, surface=surface)


def local(world, frame):
    return [*[world[k] for k in frame['axes']],
            (world[frame['axis']]-frame['coordinate'])*frame['normal'][frame['axis']]]


def world(projection, height, frame):
    p = [0., 0., 0.]
    for k, value in zip(frame['axes'], projection): p[k] = value
    p[frame['axis']] = frame['coordinate']+frame['normal'][frame['axis']]*height
    return p


def authored(rows, frame, source=(-64., 16., 160.)):
    a, b, h = local(source, frame)
    return [dict(r, value=200*160**2*h/((r['uv'][0]-a)**2+(r['uv'][1]-b)**2+h*h)**1.5,
                 saturated=False, quantized=False) for r in rows]


def score(result, frame, truth):
    for m in result['methods'].values():
        if 'projection' in m:
            m['projection_error_world'] = min(math.dist(m['projection'], local(p, frame)[:2]) for p in truth)
            if m.get('height') is not None:
                p = world(m['projection'], m['height'], frame)
                m['position_world'] = p
                m['position_error_world'] = min(math.dist(p, q) for q in truth)


def intersect_normals(items):
    # Isophote center is the foot of the source on the known plane. Fit their
    # normal lines, rather than use their intensity ratios to infer distance.
    a, b = [[0.]*3 for _ in range(3)], [0.]*3
    for projection, frame in items:
        foot = world(projection, 0, frame); n = frame['normal']
        for i in range(3):
            for j in range(3):
                q = (1 if i == j else 0)-n[i]*n[j]
                a[i][j] += q; b[i] += q*foot[j]
    return solve(a, b)


def run(args):
    policy = read(HERE/'policy.json')
    args.out.mkdir(parents=True, exist_ok=False)
    protocol = dict(experiment=policy['experiment'], policy=pin(HERE/'policy.json'),
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        code={p.name: pin(p) for p in sorted(HERE.glob('*.py'))}, inputs={},
        note='Full owned compiler captures; new deterministic within-patch holdout, not reuse of ASR01/02 scores. No claimed OS sandbox or blind human experiment.')
    patches, frames, cases = {}, {}, {}
    for name, surfaces in policy['compiled_surfaces'].items():
        source = args.fixtures/(name+'.source.json')
        original = args.bsp/(name+'.bsp')
        metadata = read(source)
        if pin(original)['sha256'] != metadata['map_sha256'] or not metadata['compiler_known']:
            raise ValueError('Original compiled BSP identity unavailable')
        protocol['inputs'][name] = dict(rows=pin(args.fixtures/(name+'.rows.jsonl')),
            source=pin(source), bsp=pin(original), answer=pin(args.answers/(name+'.q3map-recipe.json')))
        for surface in surfaces:
            key = f'compiled/{name}/{surface}'
            patches[key], frames[key] = compiled_patch(args.fixtures/(name+'.rows.jsonl'), surface, policy)
            cases[key] = dict(kind='OriginalCompilerRGB8', scene=name)
    floor, frame = patches['compiled/point_room/5'], frames['compiled/point_room/5']
    ideal = authored(floor, frame)
    variants = dict(ideal=ideal,
        gain17=[dict(r, value=17*r['value']) for r in ideal],
        gamma_half=[dict(r, value=255*(r['value']/255)**.5) for r in ideal],
        quantized=[dict(r, value=round(r['value']), quantized=True) for r in ideal],
        material_seam=[dict(r, material='other' if r['uv'][0] >= 0 else r['material'],
                            value=r['value']*(.4 if r['uv'][0] >= 0 else 1)) for r in ideal],
        saturated=[dict(r, value=min(255, round(6*r['value'])), saturated=6*r['value'] >= 254.5, quantized=True) for r in ideal],
        hard_shadow=[dict(r, value=0 if r['uv'][0] >= 0 else r['value']) for r in ideal],
        normal_change=[dict(r, normal_mismatch=r['uv'][0] >= 0) for r in ideal],
        bounce_virtual=[dict(r) for r in ideal],
        distant=authored(floor, frame, (-64, 16, 2000)))
    second = authored(floor, frame, (96, -32, 100))
    variants['two_points'] = [dict(r, value=r['value']+.6*s['value']) for r, s in zip(ideal, second)]
    for name, rows in variants.items():
        key = 'control/'+name; patches[key] = rows; frames[key] = frame
        cases[key] = dict(kind='AuthoredFieldOnCompiledGeometry', scene='point_room',
                         # Reflection of (-64,16,160) in .2x+.1y+z=88.8.
                         # All floor receivers and the real source are on the
                         # same side of that mirror. This is a reflected-component
                         # ambiguity, not a diffuse q3map2 bounce simulation.
                         truth=[[-64-24/1.05, 16-12/1.05, 160-120/1.05]] if name == 'bounce_virtual' else
                               [[-64., 16., 2000.]] if name == 'distant' else
                               [[-64., 16., 160.], [96., -32., 100.]] if name == 'two_points' else [[-64., 16., 160.]])
    # Same gamma control on a second plane for a geometry-only triangulation check.
    key = 'control/gamma_half_wall'
    wall, frames[key] = patches['compiled/point_room/3'], frames['compiled/point_room/3']
    patches[key] = [dict(r, value=255*(r['value']/255)**.5) for r in authored(wall, frames[key])]
    cases[key] = dict(kind='AuthoredFieldOnCompiledGeometry', scene='point_room', truth=[[-64., 16., 160.]])
    protocol['controlled_fields'] = 'Explicit relative unoccluded point field, gain/transfer/quantization/seam/shadow edits. bounce_virtual is the isolated specular mirror-return component (.2x+.1y+z=88.8); direct component suppressed, NOT compiler bounce.'
    write(args.out/'protocol.json', protocol)
    results = {key: dict(case=cases[key], frame=frames[key], result=analyze(rows, policy)) for key, rows in patches.items()}
    # All proposals/diagnostics freeze before private source recipe values open.
    write(args.out/'frozen-estimates.json', results)
    for key, record in results.items():
        case = record['case']
        if case['kind'] == 'OriginalCompilerRGB8':
            answer = read(args.answers/(case['scene']+'.q3map-recipe.json'))
            if answer['compiled_bsp_sha256'] != read(args.fixtures/(case['scene']+'.source.json'))['map_sha256']:
                raise ValueError('Source answer identity mismatch')
            truth = [s['position_world'] for s in answer['ground_truth']['lights']]
        else:
            truth = case['truth']
        score(record['result'], record['frame'], truth)
    triangulated = {}
    for name, keys, truth in [('compiled_point', [f'compiled/point_room/{s}' for s in (3, 4, 5)], None),
            ('gamma_control', ['control/gamma_half', 'control/gamma_half_wall'], [-64., 16., 160.])]:
        selected = [(results[k]['result']['methods']['isophote'], frames[k]) for k in keys]
        used = [(m['projection'], f) for m, f in selected if m.get('admission') == 'Conditional']
        estimate = intersect_normals(used) if used else None
        if truth is None:
            truth = read(args.answers/'point_room.q3map-recipe.json')['ground_truth']['lights'][0]['position_world']
        triangulated[name] = dict(position=estimate, admitted_planes=len(used),
            error_world=math.dist(estimate, truth) if estimate else None, physical_source_certified=False)
    write(args.out/'results.json', dict(cases=results, triangulated=triangulated))
    table = ['| Case | Gradient projection / 3D error | Isophote projection / 3D error | Forward projection / 3D error | Admission G/I/F |',
             '|---|---:|---:|---:|---|']
    for key, record in results.items():
        methods = record['result']['methods']
        cells = []
        for name in ('gradient', 'isophote', 'forward'):
            m = methods[name]
            cells.append(' / '.join(f'{m[k]:.2f}' if k in m else '—' for k in ('projection_error_world', 'position_error_world')))
        states = '/'.join('C' if methods[n]['admission'] == 'Conditional' else 'U' for n in ('gradient', 'isophote', 'forward'))
        table.append('| '+key+' | '+' | '.join(cells)+' | '+states+' |')
    with (args.out/'tables.md').open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('\n'.join(table)+'\n')
    print('\n'.join(table))
    print('Triangulation:', json.dumps(triangulated))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', type=Path, default=ROOT/'build/x02/compiled-fixtures-frozen')
    parser.add_argument('--answers', type=Path, default=ROOT/'build/q3map2-bake-verified/answers')
    parser.add_argument('--bsp', type=Path, default=ROOT/'build/q3map2-bake-verified/authoring/base/maps')
    parser.add_argument('--out', type=Path, required=True)
    run(parser.parse_args())
