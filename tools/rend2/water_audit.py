"""Installed PK3 water asset, connected-body and legacy-stage audit.

Read only: opens the installed pk3 files as ZIP archives, never writes them.

  python tools/rend2/water_audit.py [base directory] [--json out.json]

- indexes every shader definition as rend2 loads them (shaders/*.shader listed, a .mtr of the same name replaces
  its .shader, the files concatenated in reverse sorted order: the first definition found wins)
- scans every BSP: liquid brushes of all models (fog volumes included), the drawn surfaces whose BSP shader has liquid
  contents, refractive shaders, water-like names or MATERIAL_WATER; orientation and area from the triangles; the liquid
  brush side a surface lies on (all vertices inside the brush and on one of its planes)
- applies the decision of R_WaterDecideShaders per surface: CONTENTS_WATER and not lava, an upward interface;
  bottoms and sides retain their stages even with the same shader; linked brush tops are interfaces too.
  Brush association requires the same BSP model and one common plane for every vertex;
  everything else keeps its legacy stages, with the reason
- builds conservative connected bodies, records every winning shader stage,
  and applies the project overlay manifest without modifying installed assets
Patches are measured on their control points here (the renderer tessellates them first): the up facing area of a
patch is approximate, the decision of the stock maps is the same.
"""
import collections
import json
import os
from pathlib import Path
import re
import struct
import sys
import zipfile

DEFAULT_BASE = r'C:\Users\Keyten\Desktop\projects\OpenJK\build-rend2\base'
CONTENTS_LAVA, CONTENTS_WATER, CONTENTS_FOG, CONTENTS_SLIME = 0x2, 0x4, 0x8, 0x20000
SURF_NODRAW, SURF_SKY = 0x200000, 0x2000
MATERIAL_WATER = 13
MST_PLANAR, MST_PATCH, MST_TRISOUP, MST_FLARE = 1, 2, 3, 4
WATER_WORDS = ('water', 'pool', 'lake', 'river', 'pond', 'ocean', 'swamp', 'liquid')


def scroll_to_world(vector, tangent_u, tangent_v):
    """Velocity of a fixed texture feature, not the direction of increasing tcMod offset."""
    return [-vector[0] * tangent_u[axis] - vector[1] * tangent_v[axis] for axis in range(3)]


def name_like(name):
    """R_WaterNameLike: whole words of the path"""
    lower = name.lower()
    for word in WATER_WORDS:
        for m in re.finditer(word, lower):
            start = m.start() == 0 or lower[m.start() - 1] in '/_-'
            nxt = lower[m.end()] if m.end() < len(lower) else ''
            if start and (nxt == '' or nxt in '_-/' or nxt.isdigit()):
                return True
    return False


class Pk3Files:
    def __init__(self, base):
        self.base = base
        self.pk3s = sorted([f for f in os.listdir(base) if f.lower().endswith('.pk3')], key=str.lower)
        self.files = {}
        for p in self.pk3s:
            with zipfile.ZipFile(os.path.join(base, p)) as z:
                for n in z.namelist():
                    self.files[n.lower()] = (p, n)

    def read(self, key):
        p, n = self.files[key]
        with zipfile.ZipFile(os.path.join(self.base, p)) as z:
            return z.read(n)


def tokenize(text):
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.findall(r'"[^"]*"|\{|\}|[^\s{}]+|\n', text)


def parse_shaders(text):
    toks = tokenize(text)
    out, i, n = [], 0, len(toks)
    while i < n:
        if toks[i] == '\n':
            i += 1
            continue
        name = toks[i].strip('"').lower().replace('\\', '/')
        j = i + 1
        while j < n and toks[j] == '\n':
            j += 1
        if j >= n or toks[j] != '{':
            i += 1
            continue
        depth, k, body = 0, j, []
        while k < n:
            if toks[k] == '{':
                depth += 1
            elif toks[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            body.append((depth, toks[k]))
            k += 1
        out.append((name, body))
        i = k + 1
    return out


def summarize(body):
    info = dict(surfaceparms=[], sort=None, refractive=False, deforms=[], tcmods=[], maps=[], blends=[], cull=None,
                stages=[], fogparms=None)
    line, line_depth, stage_id = [], 1, -1
    lines = []
    for d, t in body:
        if t == '{' and d == 2:
            stage_id += 1
        if t == '\n' or t in '{}':
            if line:
                lines.append((line_depth, line, stage_id))
            line = []
            continue
        if not line:
            line_depth = d
        line.append(t.strip('"').lower())
    if line:
        lines.append((line_depth, line, stage_id))
    for d, l, stage_id in lines:
        k = l[0]
        if k == 'refractive':
            info['refractive'] = True
        if d == 1:
            if k == 'surfaceparm' and len(l) > 1:
                info['surfaceparms'].append(l[1])
            elif k == 'sort' and len(l) > 1:
                info['sort'] = l[1]
            elif k == 'deformvertexes':
                info['deforms'].append(' '.join(l[1:]))
            elif k == 'cull' and len(l) > 1:
                info['cull'] = l[1]
            elif k == 'fogparms':
                info['fogparms'] = ' '.join(l[1:])
        else:
            while len(info['stages']) <= stage_id:
                info['stages'].append(dict(directives=[]))
            info['stages'][stage_id]['directives'].append(' '.join(l))
            if k in ('map', 'clampmap', 'animmap', 'videomap'):
                info['maps'].append(' '.join(l[1:]))
            elif k == 'tcmod':
                info['tcmods'].append(' '.join(l[1:]))
            elif k == 'blendfunc':
                info['blends'].append(' '.join(l[1:]))
    return info


def load_shader_definitions(files):
    shader_files = sorted(k for k in files.files if k.startswith('shaders/') and k.endswith('.shader'))
    defs, all_defs = {}, collections.defaultdict(list)
    for key in reversed(shader_files):
        mtr = key[:-len('.shader')] + '.mtr'
        use = mtr if mtr in files.files else key
        for name, body in parse_shaders(files.read(use).decode('latin-1')):
            all_defs[name].append((use, files.files[use][0]))
            if name not in defs:
                defs[name] = (summarize(body), use, files.files[use][0])
    return defs, all_defs


def lump(data, i):
    ofs, ln = struct.unpack_from('<ii', data, 8 + i * 8)
    return data[ofs:ofs + ln]


def liquid_class(contents):
    if contents & CONTENTS_LAVA:
        return 'lava'
    if contents & CONTENTS_SLIME:
        return 'slime'
    if contents & CONTENTS_WATER:
        return 'water'
    return None


def orientation(nz):
    if nz > 0.7:
        return 'up'
    if nz < -0.7:
        return 'down'
    if abs(nz) < 0.3:
        return 'vertical'
    return 'sloped'


def scan_bsp(data, defs, all_surfaces=False):
    sh = lump(data, 1)
    shaders = []
    for o in range(0, len(sh), 72):
        name = sh[o:o + 64].split(b'\0')[0].decode('latin-1').lower()
        sf, cf = struct.unpack_from('<II', sh, o + 64)
        shaders.append((name, sf, cf))
    pl = lump(data, 2)
    planes = [struct.unpack_from('<4f', pl, o) for o in range(0, len(pl), 16)]
    md = lump(data, 7)
    models = [struct.unpack_from('<6f4i', md, o) for o in range(0, len(md), 40)]
    br = lump(data, 8)
    brushes = [struct.unpack_from('<3i', br, o) for o in range(0, len(br), 12)]
    bs = lump(data, 9)
    sides = [struct.unpack_from('<3i', bs, o) for o in range(0, len(bs), 12)]
    fg = lump(data, 12)
    fogs = []
    for o in range(0, len(fg), 72):
        fog_shader = fg[o:o + 64].split(b'\0')[0].decode('latin-1').lower()
        fogs.append(dict(shader=fog_shader, brush=struct.unpack_from('<i', fg, o + 64)[0],
                         definition=defs[fog_shader][0]['fogparms'] if fog_shader in defs else None))
    dv = lump(data, 10)
    nverts = len(dv) // 80
    di = lump(data, 11)
    indexes = struct.unpack_from('<%di' % (len(di) // 4), di, 0) if di else ()
    su = lump(data, 13)

    surf_model, brush_model = {}, {}
    for mi, m in enumerate(models):
        fs, ns, fb, nb = m[6:10]
        for s in range(fs, fs + ns):
            surf_model[s] = mi
        for b in range(fb, fb + nb):
            brush_model[b] = mi

    liquids = []
    for bi, (fside, nside, shn) in enumerate(brushes):
        if not (0 <= shn < len(shaders)):
            continue
        cf = shaders[shn][2]
        cls = liquid_class(cf)
        if not cls:
            continue
        bp = [planes[sides[s][0]] for s in range(fside, fside + nside) if 0 <= sides[s][0] < len(planes)]
        liquids.append(dict(brush=bi, cls=cls, shader=shaders[shn][0], fog=bool(cf & CONTENTS_FOG),
                            model=brush_model.get(bi, -1), planes=bp,
                            bounds=[[next((-p[3] for p in bp if p[i] < -.999), 0) for i in range(3)],
                                    [next((p[3] for p in bp if p[i] > .999), 0) for i in range(3)]]))

    def vert(v):
        return (struct.unpack_from('<3f', dv, v * 80),
                struct.unpack_from('<2f', dv, v * 80 + 12),
                struct.unpack_from('<3f', dv, v * 80 + 52))

    surfaces = []
    for si, o in enumerate(range(0, len(su), 148)):
        shader_num, fog_num, stype, first_vert, num_verts, first_index, num_indexes = struct.unpack_from('<7i', su, o)
        if not (0 <= shader_num < len(shaders)) or stype == MST_FLARE:
            continue
        name, sf, cf = shaders[shader_num]
        d = defs.get(name)
        refractive = bool(d and d[0]['refractive'])
        liquid = liquid_class(cf) is not None
        material = (sf & 0x1f) == MATERIAL_WATER
        vs = [vert(v) for v in range(first_vert, first_vert + num_verts) if 0 <= v < nverts]
        if not vs:
            continue
        # area weighted normal of the triangles (patches: their control point grid)
        tris = []
        if stype == MST_PATCH:
            pw, ph = struct.unpack_from('<2i', su, o + 140)
            for y in range(ph - 1):
                for x in range(pw - 1):
                    a, b, c, e = y * pw + x, y * pw + x + 1, (y + 1) * pw + x, (y + 1) * pw + x + 1
                    tris += [(a, c, b), (b, c, e)]
        else:
            idx = indexes[first_index:first_index + num_indexes]
            tris = [tuple(idx[t:t + 3]) for t in range(0, len(idx) - 2, 3)]
        sx = sy = sz = area = uv_area = mirror_area = 0.0
        tangent_u_sum, tangent_v_sum = [0.0] * 3, [0.0] * 3
        for a, b, c in tris:
            if max(a, b, c) >= len(vs):
                continue
            pa, pb, pc = vs[a][0], vs[b][0], vs[c][0]
            e1 = [pb[i] - pa[i] for i in range(3)]
            e2 = [pc[i] - pa[i] for i in range(3)]
            n = (e2[1] * e1[2] - e2[2] * e1[1], e2[2] * e1[0] - e2[0] * e1[2], e2[0] * e1[1] - e2[1] * e1[0])
            ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
            triangle_area = 0.5 * ln
            area += triangle_area
            sx, sy, sz = sx + n[0], sy + n[1], sz + n[2]
            du1, dv1 = vs[b][1][0] - vs[a][1][0], vs[b][1][1] - vs[a][1][1]
            du2, dv2 = vs[c][1][0] - vs[a][1][0], vs[c][1][1] - vs[a][1][1]
            determinant = du1 * dv2 - du2 * dv1
            if triangle_area > 1e-6 and abs(determinant) > 1e-8:
                tu = [(e1[i] * dv2 - e2[i] * dv1) / determinant for i in range(3)]
                tv = [(e2[i] * du1 - e1[i] * du2) / determinant for i in range(3)]
                tangent_u_sum = [tangent_u_sum[i] + tu[i] * triangle_area for i in range(3)]
                tangent_v_sum = [tangent_v_sum[i] + tv[i] * triangle_area for i in range(3)]
                uv_area += triangle_area
                mirror_area += (-1 if determinant < 0 else 1) * triangle_area
        vn = [sum(v[2][i] for v in vs) for i in range(3)]
        if sx * vn[0] + sy * vn[1] + sz * vn[2] < 0:
            sx, sy, sz = -sx, -sy, -sz
        ln = (sx * sx + sy * sy + sz * sz) ** 0.5
        if ln < 1e-6:
            sx, sy, sz = vn
            ln = (sx * sx + sy * sy + sz * sz) ** 0.5 or 1.0
        normal = (sx / ln, sy / ln, sz / ln)
        tangent_u = [x / uv_area for x in tangent_u_sum] if uv_area else [0.0] * 3
        tangent_v = [x / uv_area for x in tangent_v_sum] if uv_area else [0.0] * 3
        mirror_sign = (-1 if mirror_area < 0 else 1) if uv_area and abs(mirror_area) >= .8 * uv_area else 0
        bounds = [[min(v[0][i] for v in vs) for i in range(3)],
                  [max(v[0][i] for v in vs) for i in range(3)]]

        link = None
        for b in liquids:
            if b['model'] != surf_model.get(si, -1):
                continue
            if any(bounds[1][k] < b['bounds'][0][k] - 2 or
                   bounds[0][k] > b['bounds'][1][k] + 2 for k in range(3)):
                continue
            def inside(p):
                return all(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3] <= 2.0 for pl in b['planes'])

            side = next((pl for pl in b['planes']
                         if all(abs(sum(pl[i]*v[0][i] for i in range(3))-pl[3]) < 1.5 for v in vs)), None)
            if side and all(inside(v[0]) for v in vs):
                link = b
                link_side = side[:3]
                break
        linked_top = link and link_side[2] > .7 and orientation(normal[2]) == 'up'
        if not all_surfaces and not (liquid or refractive or name_like(name) or material or linked_top):
            continue
        surfaces.append(dict(index=si, shader=name, contents=cf, flags=sf, type=stype, normal=normal, area=area,
                             vertex_count=num_verts, triangle_count=len(tris),
                             patch_dimensions=list(struct.unpack_from('<2i', su, o + 140)) if stype == MST_PATCH else None,
                             fog_num=fog_num, fog=fogs[fog_num] if 0 <= fog_num < len(fogs) else None,
                             bounds=bounds, aspect=max(bounds[1][i] - bounds[0][i] for i in range(3)) /
                             max(sorted((bounds[1][i] - bounds[0][i] for i in range(3)), reverse=True)[1], 1e-6),
                             orient=orientation(normal[2]), model=surf_model.get(si, -1), refractive=refractive,
                             uv_tangent_u=tangent_u, uv_tangent_v=tangent_v,
                             uv_coverage=min(1.0, uv_area / area) if area else 0.0, uv_mirror_sign=mirror_sign,
                             link=link['brush'] if link else None, link_cls=link['cls'] if link else None,
                             link_fog=link['fog'] if link else False, link_side=link_side if link else None))
    return liquids, surfaces


def decide(surfaces, defs):
    """R_WaterDecideShaders (without r_waterOverride / r_waterSurfaceExperimental)"""
    per = collections.OrderedDict()
    for s in surfaces:
        e = per.setdefault(s['shader'], dict(shader=s['shader'], contents=0, surfaces=0, area=0.0, up_area=0.0, down_area=0.0,
                                             linked=0, orient=collections.Counter(), fog=False, slime=False,
                                             refractive=s['refractive'], models=set()))
        e['contents'] |= s['contents']
        e['surfaces'] += 1
        e['area'] += s['area']
        e['orient'][s['orient']] += 1
        e['models'].add(s['model'])
        if s['orient'] == 'up':
            e['up_area'] += s['area']
        elif s['orient'] == 'down':
            e['down_area'] += s['area']
        if s['link'] is not None:
            e['linked'] += 1
            e['fog'] |= s['link_fog']
            e['slime'] |= s['link_cls'] == 'slime'
    for e in per.values():
        c = e['contents']
        cls = liquid_class(c)
        e['optics'] = 'slime' if (cls == 'slime' or e['slime']) else 'water'
        e['fog'] |= bool(c & CONTENTS_FOG)
        if cls == 'lava':
            reason = 'lava'
        elif c & CONTENTS_WATER:
            reason = 'CONTENTS_WATER' if e['up_area'] > 0 else \
                'mostly not facing up (waterfall / stream)'
        elif cls == 'slime':
            reason = 'slime without water contents'
        elif e['linked'] and e['up_area'] > 0:
            reason = 'top of a water brush'
        elif e['refractive']:
            reason = 'refractive, no water semantics (generic refraction)'
        else:
            reason = 'water-like name / material only, no water semantics'
        e['reason'] = reason
        e['modern'] = reason in ('CONTENTS_WATER', 'top of a water brush')
    for e in per.values():
        e['modern_surfaces'] = 0
    for surface in surfaces:
        e = per[surface['shader']]
        side = surface['link_side']
        linked_top = side and surface['link_cls'] != 'lava' and side[2] > .7 and sum(
            side[i]*surface['normal'][i] for i in range(3)) > .7
        surface['modern'] = bool(e['modern'] and surface['orient'] == 'up' and
                                 (surface['contents'] & CONTENTS_WATER or linked_top) and
                                 not surface['contents'] & CONTENTS_LAVA)
        e['modern_surfaces'] += int(surface['modern'])
    return per


def build_bodies(liquids, surfaces, defs):
    """Match renderer's conservative coplanar connected-component grouping."""
    candidates = [s for s in surfaces if liquid_class(s['contents']) or
                  (s['link'] is not None and s['link_side'][2] > .7 and s['orient'] == 'up')]
    parents = list(range(len(candidates)))

    def root(i):
        while parents[i] != i:
            i = parents[i]
        return i

    def touch(a, b, eps=2):
        return all(a[0][k] <= b[1][k] + eps and b[0][k] <= a[1][k] + eps for k in range(3))

    brush_lookup = {b['brush']: b for b in liquids}
    for i, a in enumerate(candidates):
        ac = brush_lookup[a['link']]['cls'] if a['link'] is not None else liquid_class(a['contents'])
        for j in range(i + 1, len(candidates)):
            b = candidates[j]
            bc = brush_lookup[b['link']]['cls'] if b['link'] is not None else liquid_class(b['contents'])
            if a['model'] != b['model'] or ac != bc or a['orient'] != b['orient'] or not touch(a['bounds'], b['bounds']):
                if a['model'] != b['model'] or ac != bc or a['link'] is None or b['link'] is None or not touch(brush_lookup[a['link']]['bounds'], brush_lookup[b['link']]['bounds'], .5):
                    continue
            if a['link'] is not None and b['link'] is not None and (a['link'] == b['link'] or touch(brush_lookup[a['link']]['bounds'], brush_lookup[b['link']]['bounds'], .5)):
                parents[root(j)] = root(i)
                continue
            if sum(a['normal'][k] * b['normal'][k] for k in range(3)) < .94:
                continue
            if abs(sum(a['normal'][k] * (a['bounds'][0][k] - b['bounds'][0][k]) for k in range(3))) > 4:
                continue
            parents[root(j)] = root(i)
    grouped = collections.OrderedDict()
    for i, s in enumerate(candidates):
        grouped.setdefault(root(i), []).append(s)
    bodies = []
    for group in grouped.values():
        ids = sorted({s['link'] for s in group if s['link'] is not None})
        brush_depths = [max(0, brush_lookup[b]['bounds'][1][2] - brush_lookup[b]['bounds'][0][2]) for b in ids]
        names = sorted({s['shader'] for s in group})
        motion_names = {s['shader'] for s in group if liquid_class(s['contents']) or
                        (s['link'] is not None and s['link_side'] and s['link_side'][2] > .7)}
        cls = brush_lookup[ids[0]]['cls'] if ids else liquid_class(group[0]['contents'])
        profile, source = 'generic_water', 'fallback'
        if cls == 'lava':
            profile, source = 'no_water_dynamics', 'auto: lava contents'
        elif cls == 'slime' or 'textures/common/water2_water1_vjun1' in names:
            profile, source = 'slime', 'auto: slime contents/optics'
        elif 'textures/common/water2_still' in names:
            profile, source = 'still_pool', 'stock: still shader'
        elif 'textures/h_evil/wfall' in names and group[0]['normal'][2] < .7:
            profile, source = 'waterfall', 'stock: waterfall shader and orientation'
        stages = {n: defs[n][0]['stages'] if n in defs else [] for n in names}
        scrolls = []
        turbs, stretches = [], []
        for name, shader_stages in stages.items():
            if name not in motion_names:
                continue
            for stage_index, stage in enumerate(shader_stages):
                stage_tcmods = [d.split() for d in stage['directives'] if d.startswith('tcmod ')]
                stable_stage_basis = not any(len(parts) > 1 and parts[1] in
                                             ('scale', 'transform', 'rotate', 'entitytranslate')
                                             for parts in stage_tcmods)
                for directive in stage['directives']:
                    parts = directive.split()
                    if len(parts) >= 4 and parts[:2] == ['tcmod', 'scroll']:
                        try:
                            vector = [float(parts[2]), float(parts[3])]
                            matching = [s for s in group if s['shader'] == name]
                            candidate_area = sum(s['area'] for s in matching)
                            reliable = [s for s in matching if s['uv_coverage'] >= .8 and s['uv_mirror_sign']]
                            reliable_area = sum(s['area'] for s in reliable)
                            surface_velocities = [(s, scroll_to_world(vector, s['uv_tangent_u'], s['uv_tangent_v'])) for s in reliable]
                            world_sum = [sum(v[axis] * s['area'] for s, v in surface_velocities) for axis in range(3)]
                            world = [world_sum[axis] /
                                     reliable_area if reliable_area else 0.0 for axis in range(3)]
                            world_speed = sum(x*x for x in world) ** .5
                            magnitude_area = sum(sum(x*x for x in v) ** .5 * s['area'] for s, v in surface_velocities)
                            basis_agreement = min(1.0, sum(x*x for x in world_sum) ** .5 / magnitude_area) if magnitude_area else 0.0
                            scrolls.append(dict(shader=name, stage=stage_index, vector=vector,
                                                stage_importance='unknown; equal weight in automatic analysis',
                                                stage_mapping='base UV basis' if stable_stage_basis else
                                                              'unsupported stage scale/transform/rotation; override required',
                                                world_velocity=world, world_speed=world_speed,
                                                basis_agreement=basis_agreement,
                                                uv_basis_reliable=bool(stable_stage_basis and candidate_area and reliable_area >= .8 * candidate_area and
                                                                       basis_agreement >= .8 and world_speed > 1e-4),
                                                reliable_surface_area=reliable_area,
                                                candidate_surface_area=candidate_area))
                        except ValueError:
                            pass
                    elif len(parts) >= 3 and parts[:2] == ['tcmod', 'turb']:
                        turbs.append(dict(shader=name, stage=stage_index, parameters=parts[2:]))
                    elif len(parts) >= 3 and parts[:2] == ['tcmod', 'stretch']:
                        stretches.append(dict(shader=name, stage=stage_index, parameters=parts[2:]))
        reliable_scrolls = [s for s in scrolls if s['uv_basis_reliable']]
        world_sum = [sum(s['world_velocity'][axis] for s in reliable_scrolls) for axis in range(3)]
        magnitude_sum = sum(s['world_speed'] for s in reliable_scrolls)
        agreement = min(1.0, sum(x*x for x in world_sum) ** .5 / magnitude_sum) if magnitude_sum else 0.0
        world_velocity = [x / len(reliable_scrolls) for x in world_sum] if reliable_scrolls else [0.0] * 3
        world_speed = sum(x*x for x in world_velocity) ** .5
        uv_coverage = min(1.0, sum(s['area'] * s['uv_coverage'] for s in group) /
                          max(sum(s['area'] for s in group), 1e-6))
        confidence = ('high' if reliable_scrolls and len(reliable_scrolls) == len(scrolls) and
                      agreement >= .72 and uv_coverage >= .8 else
                      ('ambiguous' if scrolls else 'none'))
        legacy_analysis = dict(dominant_texture_space_motion=[
                                   sum(s['vector'][axis] for s in scrolls) / len(scrolls) if scrolls else 0.0
                                   for axis in range(2)],
                               stage_importance='not determinable from blend state; stages weighted equally',
                               world_velocity=world_velocity, world_speed=world_speed,
                               stage_agreement=agreement, uv_coverage=uv_coverage,
                               confidence=confidence,
                               automatic_eligible=confidence == 'high')
        bounds = [[min(s['bounds'][0][k] for s in group) for k in range(3)],
                  [max(s['bounds'][1][k] for s in group) for k in range(3)]]
        bodies.append(dict(id=len(bodies) + 1, model=group[0]['model'], liquid_class=cls,
                           optics_profile=cls,
                           dynamics_profile=profile, decision_source=source,
                           surface_ids=[s['index'] for s in group], brush_ids=ids,
                           area=sum(s['area'] for s in group), bounds=bounds,
                           shape_aspect=max(bounds[1][k] - bounds[0][k] for k in range(3)) /
                           max(sorted((bounds[1][k] - bounds[0][k] for k in range(3)), reverse=True)[1], 1e-6),
                           orientation=collections.Counter(s['orient'] for s in group),
                           mean_brush_depth=sum(brush_depths) / len(brush_depths) if brush_depths else None,
                           deepest_brush_depth=max(brush_depths) if brush_depths else None,
                           shaders=names, stages=stages, legacy_scrolls=scrolls,
                           legacy_turb=turbs, legacy_stretch=stretches,
                           legacy_motion_analysis=legacy_analysis,
                           surface_flags=sorted({s['flags'] for s in group}),
                           content_flags=sorted({s['contents'] for s in group}),
                           fog=any(s['link_fog'] for s in group),
                           fog_parameters=[f for f in {json.dumps(s['fog'], sort_keys=True) for s in group if s['fog']}
                                           for f in [json.loads(f)]],
                           outdoor_indoor='unknown: no reliable BSP exposure evidence'))
    return bodies


def resolve_flow(body, explicit=None):
    analysis = body['legacy_motion_analysis']
    result = dict(direction=[0.0, 0.0, 0.0], speed=0.0, velocity=[0.0, 0.0, 0.0],
                  source='none', confidence=analysis['confidence'], decision='no usable authored direction')
    suppressed = body['dynamics_profile'] in ('still_pool', 'calm_water', 'lake', 'slime', 'no_water_dynamics')
    if analysis['confidence'] == 'high' and not suppressed and analysis['world_speed'] > 0:
        speed = min(analysis['world_speed'], 256.0)
        direction = [x / analysis['world_speed'] for x in analysis['world_velocity']]
        result = dict(direction=direction, speed=speed,
                      velocity=[x * speed for x in direction], source='legacy-derived',
                      confidence='high', decision='reliable UV basis and agreeing authored scroll stages')
    elif suppressed and analysis['confidence'] == 'high':
        result['decision'] = f'suppressed by {body["dynamics_profile"]} dynamics profile'
    elif analysis['confidence'] == 'ambiguous':
        result['decision'] = 'ambiguous stage directions or unreliable/mixed UV mapping; explicit override required'
    if explicit is not None:
        if isinstance(explicit, dict):
            vector = list(explicit.get('Direction', [0, 0, 0]))
            speed = float(explicit.get('Speed', sum(x*x for x in vector) ** .5))
        else:
            vector = list(explicit) + [0] * (3 - len(explicit))
            speed = sum(x*x for x in vector) ** .5
        length = sum(x*x for x in vector) ** .5
        direction = [x / length for x in vector] if length else [0.0, 0.0, 0.0]
        result = dict(direction=direction, speed=max(0.0, speed),
                      velocity=[x * max(0.0, speed) for x in direction], source='explicit',
                      confidence='high', decision='explicit project overlay')
    return result


def main(argv):
    base = DEFAULT_BASE
    out_json = None
    args = list(argv)
    if '--json' in args:
        i = args.index('--json')
        out_json = args[i + 1]
        del args[i:i + 2]
    if args:
        base = args[0]
    files = Pk3Files(base)
    defs, all_defs = load_shader_definitions(files)
    override_file = Path(__file__).with_name('water_body_overrides.json')
    overrides = json.loads(override_file.read_text(encoding='utf-8')) if override_file.exists() else {}
    print(f'pk3: {", ".join(files.pk3s)}')
    print(f'shader definitions: {len(defs)}')

    report = dict(base=base, pk3s=files.pk3s, maps={}, shader_usage={})
    candidates = collections.OrderedDict()
    for key in sorted(k for k in files.files if k.endswith('.bsp')):
        liquids, surfaces = scan_bsp(files.read(key), defs)
        per = decide(surfaces, defs)
        if not liquids and not per:
            continue
        name = key.replace('maps/', '')
        bodies = build_bodies(liquids, surfaces, defs)
        for rule in overrides.get(name[:-4] if name.endswith('.bsp') else name, {}).get('WaterBodies', []):
            selector = rule.get('Selector', {})
            pattern = selector.get('Shader')
            prefix = selector.get('ShaderPrefix')
            for body in bodies:
                if pattern and pattern not in body['shaders']:
                    continue
                if prefix and not any(shader.startswith(prefix) for shader in body['shaders']):
                    continue
                if 'BodyId' in selector and selector['BodyId'] != body['id']:
                    continue
                if 'Point' in selector and not all(body['bounds'][0][k] <= selector['Point'][k] <= body['bounds'][1][k] for k in range(3)):
                    continue
                if 'DynamicsProfile' in rule:
                    body['dynamics_profile'] = rule['DynamicsProfile']
                    body['decision_source'] = 'explicit project overlay'
                if 'Flow' in rule:
                    body['explicit_flow'] = rule['Flow']
        for body in bodies:
            body['resolved_flow'] = resolve_flow(body, body.pop('explicit_flow', None))
        report['maps'][name] = dict(
            liquid_brushes=[dict(brush=b['brush'], cls=b['cls'], shader=b['shader'], model=b['model'], fog=b['fog'], bounds=b['bounds'])
                            for b in liquids],
            surfaces=surfaces,
            bodies=bodies,
            shaders=[dict((k, (sorted(v) if isinstance(v, set) else dict(v) if isinstance(v, collections.Counter) else v))
                          for k, v in e.items()) for e in per.values()])
        if not any(b['cls'] for b in liquids) and not any(e['modern'] for e in per.values()) and \
                not any(e['contents'] & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA) for e in per.values()):
            continue
        world = sum(1 for b in liquids if b['model'] == 0)
        classes = collections.Counter((b['cls'], b['shader'], 'fog' if b['fog'] else '') for b in liquids)
        print(f'\n## {name}: liquid brushes {len(liquids)} (world {world}, brush models {len(liquids) - world}) '
              + ', '.join(f'{n} {c}/{s}{" " + f if f else ""}' for (c, s, f), n in classes.items()))
        for e in per.values():
            if not (e['contents'] & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA) or e['refractive']):
                continue
            o = e['orient']
            print(f'   {"WATER " if e["modern"] else "legacy"} {e["shader"]:<44} {e["optics"]:<5} '
                  f'n={e["surfaces"]:3d} up/down/vert/sloped {o["up"]}/{o["down"]}/{o["vertical"]}/{o["sloped"]} '
                  f'up share {100 * e["up_area"] / max(e["area"] - e["down_area"], 1e-6):5.1f}% linked {e["linked"]:3d}'
                  f'{" fog-medium" if e["fog"] else ""} models {sorted(e["models"])} | {e["reason"]}')
            candidates.setdefault(e['shader'], set()).add(name)
        for body in bodies:
            flow = body['resolved_flow']
            motion = body['legacy_motion_analysis']
            print(f'   body {body["id"]:2d} {body["dynamics_profile"]:<13} shaders {", ".join(body["shaders"])}; '
                  f'legacy {motion["confidence"]} agreement {motion["stage_agreement"]:.2f} UV {motion["uv_coverage"]:.2f}; '
                  f'flow {flow["source"]}/{flow["confidence"]} velocity {tuple(round(x, 3) for x in flow["velocity"])}; '
                  f'{flow["decision"]}')
            for scroll in body['legacy_scrolls']:
                print(f'      stage {scroll["stage"]} {scroll["shader"]} scroll {tuple(scroll["vector"])} -> '
                      f'world {tuple(round(x, 3) for x in scroll["world_velocity"])} '
                      f'basis agreement {scroll["basis_agreement"]:.2f} '
                      f'{"reliable" if scroll["uv_basis_reliable"] else "unreliable"}')

    print('\n## shaders without water semantics (water-like name / material / refractive) used by the maps')
    for name, map_data in report['maps'].items():
        for body in map_data['bodies']:
            for shader in body['shaders']:
                usage = report['shader_usage'].setdefault(shader, dict(maps=[], body_count=0, shared_by_distinct_bodies=False,
                    definition=None, definition_file=None, pk3=None))
                if name not in usage['maps']:
                    usage['maps'].append(name)
                usage['body_count'] += 1
    for shader, usage in report['shader_usage'].items():
        usage['shared_by_distinct_bodies'] = usage['body_count'] > 1
        if shader in defs:
            usage['definition'], usage['definition_file'], usage['pk3'] = defs[shader]
    for name, m in report['maps'].items():
        for e in m['shaders']:
            if e['contents'] & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA):
                continue
            print(f'   {name:<22} {e["shader"]:<44} {e["surfaces"]:3d} surfaces | {e["reason"]}')

    print('\n## water-like or water contents shader definitions not used by any map')
    used = {e['shader'] for m in report['maps'].values() for e in m['shaders']}
    for name, (info, use, pk3) in sorted(defs.items()):
        if name in used:
            continue
        water = 'water' in info['surfaceparms'] or 'slime' in info['surfaceparms']
        if water or info['refractive'] or name_like(name):
            print(f'   {name:<52} {use} ({pk3}) parms {" ".join(info["surfaceparms"])}'
                  f'{" REFRACTIVE" if info["refractive"] else ""}')

    print('\n## definitions of the stock water shaders')
    for name in candidates:
        d = defs.get(name)
        if not d:
            print(f'   {name}: no definition (image fallback)')
            continue
        info, use, pk3 = d
        print(f'   {name}: {use} ({pk3}), defined {len(all_defs[name])}x; parms {" ".join(info["surfaceparms"])}; '
              f'sort {info["sort"]}; cull {info["cull"]}; deforms {info["deforms"]}; tcMods {info["tcmods"]}; '
              f'blends {info["blends"]}; refractive {info["refractive"]}')
    if out_json:
        for map_data in report['maps'].values():
            for surface in map_data['surfaces']:
                surface.pop('link_side', None)
        with open(out_json, 'w', encoding='utf-8') as output:
            json.dump(report, output, indent=1, default=list)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
