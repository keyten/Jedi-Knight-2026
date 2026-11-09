#!/usr/bin/env python3
"""ASR17 independent geometry diagnosis. Raw licensed witnesses remain private.

This is an explanation tool, never an admission predicate. Exact binary rational
plane residuals distinguish stored geometry disagreement from arithmetic noise.
"""
import argparse
from collections import Counter
from fractions import Fraction
import json
import math
from pathlib import Path

EPS = 2.0**-52


def dot(a, b):
    return math.fsum(x*y for x, y in zip(a, b))


def exact_distance(n, p, d):
    return sum((Fraction(x)*Fraction(y) for x, y in zip(n, p)), -Fraction(d))


def span(s, n, d):
    if s['kind'] == 1:
        axial = dot(n, s['axis'])
        radius = s['half_length']*abs(axial) + s['radius']*math.sqrt(max(0, dot(n, n)-axial*axial))
        centre = dot(n, s['centre'])-d
        return centre-radius, centre+radius
    values = [float(exact_distance(n, p, d)) for p in s['vertices']]
    return min(values), max(values)


def clip(vertices, planes):
    """Exact rational clipping: a witness remains on the actual source polygon."""
    poly = [[Fraction(x) for x in p] for p in vertices]
    for n, d in planes:
        result = []
        for a, b in zip(poly, poly[1:]+poly[:1]):
            da, db = exact_distance(n, a, d), exact_distance(n, b, d)
            if da <= 0:
                result.append(a)
            if (da < 0) != (db < 0):
                t = da/(da-db)
                result.append([x+(y-x)*t for x, y in zip(a, b)])
        poly = result
    return poly


def inspect(world, s, normal):
    """Walk retained BSP independently and retain only nonseparated brushes."""
    stack = [0 if world['nodes'] else -1]
    leaves, unresolved, contacts = [], [], []
    work = 0
    while stack:
        work += 1
        if work > 1048576:
            raise ValueError('Independent BSP traversal budget')
        index = stack.pop()
        if index >= 0:
            plane, front, back = world['nodes'][index]
            lo, hi = span(s, *world['planes'][plane])
            if hi >= 0:
                stack.append(front)
            if lo < 0:
                stack.append(back)
            continue
        leaf_id = -index-1
        cluster, area, first, count = world['leaves'][leaf_id]
        leaf_contact = False
        for brush_id in world['leaf_brushes'][first:first+count]:
            start, size, is_world, solid, sky = world['brushes'][brush_id]
            if not is_world or not solid:
                continue
            ranges, match, separated = [], False, False
            planes = []
            for plane_id in world['sides'][start:start+size]:
                work += 1
                if work > 1048576:
                    raise ValueError('Independent brush work budget')
                n, d = world['planes'][plane_id]
                lo, hi = span(s, n, d)
                rounding = 64*EPS*(1+abs(lo)+abs(hi)+abs(d))
                contact_rounding = 64*EPS*(1+abs(d)+abs(s['contact_distance']))
                aligned = math.dist(normal, n) <= 1e-7
                contact = (s['contact_known'] and lo >= 0 and lo <= contact_rounding
                           and aligned and abs(s['contact_distance']-d) <= contact_rounding)
                ranges.append(dict(plane=plane_id, normal=n, distance=d, lo=lo, hi=hi,
                                   aligned=aligned, plane_delta=s['contact_distance']-d,
                                   contact=bool(contact)))
                planes.append((n, d))
                if lo > rounding or contact:
                    separated, match = True, bool(contact)
                    break
            if match:
                leaf_contact = True
                contacts.append(dict(leaf=leaf_id, brush=brush_id, plane=ranges[-1]['plane']))
            if separated:
                continue
            boundary = [p for p in ranges if p['lo'] >= 0]
            near_aligned = [p for p in ranges if p['aligned']]
            category = 'bsp-unresolved-intersection'
            if boundary:
                category = 'bsp-other-plane-boundary'
                if any(p['lo'] == p['hi'] == 0 and dot(normal, p['normal']) < -1+1e-7 for p in boundary):
                    category = 'bsp-opposite-contact-boundary'
            elif near_aligned:
                category = 'bsp-contact-plane-disagreement'
            deep = None
            if s['kind'] == 0 and len(s['vertices']) >= 3:
                poly = clip(s['vertices'], planes)
                if len(poly) >= 3:
                    centre = [sum(p[k] for p in poly)/len(poly) for k in range(3)]
                    residuals = [exact_distance(n, centre, d) for n, d in planes]
                    if max(residuals) < 0:
                        deep = dict(point=[float(x) for x in centre], point_fraction=[str(x) for x in centre],
                                    max_signed_distance=float(max(residuals)))
            if all(p['hi'] < 0 for p in ranges) or deep:
                category = 'bsp-penetration-witness'
            unresolved.append(dict(leaf=leaf_id, brush=brush_id, category=category,
                                   planes=ranges, deep=deep))
        leaves.append(dict(id=leaf_id, cluster=cluster, area=area, contact=leaf_contact))
    return dict(leaves=leaves, contacts=contacts, unresolved=unresolved, work=work)


def classify(row, world):
    normal = row['normal']
    analyses = [inspect(world, s, normal) for s in row['supports']]
    facets = set()
    for a in analyses:
        facets.update(x['category'] for x in a['unresolved'])
        if any(l['cluster'] < 0 and not l['contact'] for l in a['leaves']):
            facets.add('bsp-unclustered-without-contact')
    reason = row['reason']
    signed_ranges = [span(s, normal, s['contact_distance']) for s in row['supports']]
    signed = [v for pair in signed_ranges for v in pair]
    orientation = [dot(normal, n)/math.sqrt(dot(n,n)) for named in row.get('named_orientation', [])
                   for n in named['vertex_normals'] if dot(n,n) > 0]
    if orientation and max(orientation) < -.999:
        facets.add('named-winding-opposes-all-vertex-normals')
    category = 'evaluator-other'
    if reason.startswith('Occupied source support'):
        failed = next((a for s, a in zip(row['supports'], analyses) if s['native_space'] not in (0, 5)), None)
        if failed:
            categories = {b['category'] for b in failed['unresolved']}
            category = next((k for k in ('bsp-penetration-witness', 'bsp-contact-plane-disagreement',
                                         'bsp-opposite-contact-boundary', 'bsp-other-plane-boundary',
                                         'bsp-unresolved-intersection') if k in categories),
                            'bsp-unclustered-without-contact')
    elif reason == 'Incomplete local visual support':
        issues = row['local']['issues']
        category = ('visual-patch-envelope' if issues & 64 else 'visual-bounds-only' if issues & 128
                    else 'visual-global-coverage-gap')
    elif reason.startswith('Cylinder occupied'):
        category = 'hypothesis-cylinder-through-mount'
    elif reason.startswith('Emitting support is behind'):
        category = 'hypothesis-stand-off-or-side'
    elif 'optics' in reason:
        category = 'visual-unknown-optics'
    elif reason.startswith('Operational:'):
        category = 'stale-or-operational'
    facets.add(category)
    local = row['local']['triangles']
    approximate = [t for t in local if t['error'] > 0]
    separated = 0
    for t in approximate:
        residuals = [float(exact_distance(normal, v, dot(normal,row['anchor']))) for v in t['vertices']]
        pad = t['error']*math.sqrt(dot(normal,normal))
        separated += min(residuals)-pad > max(signed) or max(residuals)+pad < min(signed)
    if approximate and separated == len(approximate):
        facets.add('all-local-patch-envelopes-plane-separated')
    if any(t['optical'] in (3, 4, 5) for t in local):
        facets.add('local-unknown-optics-present')
    if row.get('point_probes'):
        if row['point_probes'][0]['space'] == 1:
            facets.add('front-eighth-unit-in-solid')
        if row['point_probes'][2]['space'] == 1:
            facets.add('back-eighth-unit-in-solid')
    metadata = {s['id']: s for s in world['surface_metadata']}
    if any(metadata[s]['model'] or metadata[s]['motion'] for s in row['surfaces']):
        facets.add('named-model-or-moving-geometry')
    details = dict(source=row['source'], fixture=row['fixture'], state=row['state'], reason=reason,
                   category=category, facets=sorted(facets), mount_signed_range=[min(signed), max(signed)] if signed else None,
                   winding_vertex_normal_dot_range=[min(orientation),max(orientation)] if orientation else None,
                   approximate_triangles=len(approximate), plane_separated_approximate_triangles=separated,
                   supports=analyses)
    return details


def analyze(folder):
    world = json.loads((folder/'diagnosis-world.json').read_text())
    rows = [json.loads(line) for line in (folder/'diagnosis.jsonl').read_text().splitlines()]
    details = [classify(row, world) for row in rows]
    independent_work = sum(a['work'] for d in details for a in d['supports'])
    if independent_work > 16000000:
        raise ValueError('Independent map work budget')
    (folder/'classification.json').write_text(json.dumps(details, indent=2, allow_nan=False)+'\n')
    primary = Counter(d['category'] for d in details)
    facets = Counter(f for d in details for f in d['facets'])
    return dict(schema='org.openjk.mount-diagnosis-classification.v1', attempts=len(rows),
                states=dict(Counter(r['state'] for r in rows)), primary=dict(sorted(primary.items())),
                facets=dict(sorted(facets.items())),
                samples={k:[d['source'] for d in details if d['category'] == k][:3] for k in sorted(primary)},
                native_diagnostic_work=max((r['diagnostic_cumulative_work'] for r in rows), default=0),
                independent_work=independent_work,
                visibility=world['visibility'],
                ray_states=dict(Counter(str(r['state']) for row in rows for r in row['rays'])),
                ray_state_order=['Visible', 'Blocked', 'Unknown'],
                point_probe_states={str(offset):dict(Counter(str(p['space']) for row in rows
                                    for p in row.get('point_probes',[]) if p['offset']==offset))
                                    for offset in (.125,4,-.125,-4)},
                point_space_order=['Free','Solid','Outside','Sky','Unknown','Exterior'],
                claims_admission=False, reads_target_rgb=False)


def numerical_witnesses(folder):
    """Publish IDs/residuals only; exact points and plane equations stay private."""
    details = json.loads((folder/'classification.json').read_text())
    rows = {r['source']:r for r in map(json.loads,(folder/'diagnosis.jsonl').read_text().splitlines())}
    selected = []
    wanted = {'8734946737344204972', '140031554626216494'}
    for category in sorted({d['category'] for d in details}):
        selected.append(next(d for d in details if d['category']==category))
    selected += [d for d in details if d['source'] in wanted and d not in selected]
    result = []
    for d in selected:
        row=rows[d['source']]
        failed=next((i for i,s in enumerate(row['supports']) if s['native_space'] not in (0,5)),0)
        a=d['supports'][failed]
        brush=next((b for b in a['unresolved'] if b['category']==d['category']),
                   a['unresolved'][0] if a['unresolved'] else None)
        witness=None
        if brush:
            planes=[p for p in brush['planes'] if p['lo']>=0 or p['aligned']]
            witness=dict(leaf=brush['leaf'],brush=brush['brush'],category=brush['category'],
                         planes=[dict(id=p['plane'],signed_range=[p['lo'],p['hi']],
                                 normal_dot=dot(row['normal'],p['normal'])) for p in planes],
                         penetration_max_signed_distance=brush['deep']['max_signed_distance'] if brush['deep'] else None)
        result.append(dict(source=d['source'],fixture=d['fixture'],category=d['category'],
                    state=d['state'],mount_signed_range=d['mount_signed_range'],
                    winding_vertex_normal_dot_range=d['winding_vertex_normal_dot_range'],
                    support_index=failed,brush=witness,
                    patch_error_range=[min(t['error'] for t in row['local']['triangles'] if t['error']>0),
                                       max(t['error'] for t in row['local']['triangles'] if t['error']>0)]
                                      if d['approximate_triangles'] else None,
                    probes=[{k:p[k] for k in ('offset','space','leaf','boundary')} for p in row['point_probes']],
                    rays=[{k:r[k] for k in ('name','state','issues','surface','triangle','optical')} for r in row['rays']]))
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('folder', type=Path)
    a = p.parse_args()
    print(json.dumps(analyze(a.folder), indent=2))
