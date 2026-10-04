"""Water asset audit and classification coverage of the modern water surface (r_waterSurface, tr_watersurface.cpp).

Read only: opens the installed pk3 files as ZIP archives, never writes them.

  python tools/rend2/water_audit.py [base directory] [--json out.json]

- indexes every shader definition as rend2 loads them (shaders/*.shader listed, a .mtr of the same name replaces
  its .shader, the files concatenated in reverse sorted order: the first definition found wins)
- scans every BSP: liquid brushes of all models (fog volumes included), the drawn surfaces whose BSP shader has liquid
  contents, refractive shaders, water-like names or MATERIAL_WATER; orientation and area from the triangles; the liquid
  brush side a surface lies on (all vertices inside the brush and on one of its planes)
- applies the decision of R_WaterDecideShaders: CONTENTS_WATER and not lava, at least half of the shader's area on the
  map facing up, its downward faces (brush bottoms) left out -> drawn as water (optics: slime with CONTENTS_SLIME); a refractive shader on a liquid brush -> water;
  everything else keeps its legacy stages, with the reason
Patches are measured on their control points here (the renderer tessellates them first): the up facing area of a
patch is approximate, the decision of the stock maps is the same.
"""
import collections
import json
import os
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
    info = dict(surfaceparms=[], sort=None, refractive=False, deforms=[], tcmods=[], maps=[], blends=[], cull=None)
    line, line_depth = [], 1
    lines = []
    for d, t in body:
        if t == '\n' or t in '{}':
            if line:
                lines.append((line_depth, line))
            line = []
            continue
        if not line:
            line_depth = d
        line.append(t.strip('"').lower())
    if line:
        lines.append((line_depth, line))
    for d, l in lines:
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
        else:
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


def scan_bsp(data, defs):
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
                            model=brush_model.get(bi, -1), planes=bp))

    def vert(v):
        return struct.unpack_from('<3f', dv, v * 80), struct.unpack_from('<3f', dv, v * 80 + 52)

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
        if not (liquid or refractive or name_like(name) or material):
            continue
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
        sx = sy = sz = area = 0.0
        for a, b, c in tris:
            if max(a, b, c) >= len(vs):
                continue
            pa, pb, pc = vs[a][0], vs[b][0], vs[c][0]
            e1 = [pb[i] - pa[i] for i in range(3)]
            e2 = [pc[i] - pa[i] for i in range(3)]
            n = (e2[1] * e1[2] - e2[2] * e1[1], e2[2] * e1[0] - e2[0] * e1[2], e2[0] * e1[1] - e2[1] * e1[0])
            ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
            area += 0.5 * ln
            sx, sy, sz = sx + n[0], sy + n[1], sz + n[2]
        vn = [sum(v[1][i] for v in vs) for i in range(3)]
        if sx * vn[0] + sy * vn[1] + sz * vn[2] < 0:
            sx, sy, sz = -sx, -sy, -sz
        ln = (sx * sx + sy * sy + sz * sz) ** 0.5
        if ln < 1e-6:
            sx, sy, sz = vn
            ln = (sx * sx + sy * sy + sz * sz) ** 0.5 or 1.0
        normal = (sx / ln, sy / ln, sz / ln)

        link = None
        for b in liquids:
            def inside(p):
                return all(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3] <= 2.0 for pl in b['planes'])

            def on_side(p):
                return any(abs(pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3]) < 1.5 for pl in b['planes'])
            if all(inside(v[0]) and on_side(v[0]) for v in vs):
                link = b
                break
        surfaces.append(dict(index=si, shader=name, contents=cf, flags=sf, type=stype, normal=normal, area=area,
                             orient=orientation(normal[2]), model=surf_model.get(si, -1), refractive=refractive,
                             link=link['brush'] if link else None, link_cls=link['cls'] if link else None,
                             link_fog=link['fog'] if link else False))
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
        if c & SURF_NODRAW:
            pass
        cls = liquid_class(c)
        e['optics'] = 'slime' if (cls == 'slime' or e['slime']) else 'water'
        e['fog'] |= bool(c & CONTENTS_FOG)
        if cls == 'lava':
            reason = 'lava'
        elif c & CONTENTS_WATER:
            reason = 'CONTENTS_WATER' if e['up_area'] > 0 and e['up_area'] >= 0.5 * (e['area'] - e['down_area']) else \
                'mostly not facing up (waterfall / stream)'
        elif cls == 'slime':
            reason = 'slime without water contents'
        elif e['refractive'] and e['linked']:
            reason = 'refractive on a water brush'
        elif e['refractive']:
            reason = 'refractive, no water semantics (generic refraction)'
        else:
            reason = 'water-like name / material only, no water semantics'
        e['reason'] = reason
        e['modern'] = reason in ('CONTENTS_WATER', 'refractive on a water brush')
    return per


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
    print(f'pk3: {", ".join(files.pk3s)}')
    print(f'shader definitions: {len(defs)}')

    report = dict(base=base, pk3s=files.pk3s, maps={})
    candidates = collections.OrderedDict()
    for key in sorted(k for k in files.files if k.endswith('.bsp')):
        liquids, surfaces = scan_bsp(files.read(key), defs)
        per = decide(surfaces, defs)
        if not liquids and not per:
            continue
        name = key.replace('maps/', '')
        report['maps'][name] = dict(
            liquid_brushes=[dict(brush=b['brush'], cls=b['cls'], shader=b['shader'], model=b['model'], fog=b['fog'])
                            for b in liquids],
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

    print('\n## shaders without water semantics (water-like name / material / refractive) used by the maps')
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
        json.dump(report, open(out_json, 'w'), indent=1, default=list)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
