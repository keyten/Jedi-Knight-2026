"""Stock audit of the liquid media (r_volumetricWater, tr_liquid.cpp): every BSP, read only.

  python tools/rend2/liquid_audit.py [base directory] [--json out.json] [--map name]

Opens the installed pk3 files as ZIP archives (never writes them) and mirrors R_LoadLiquidBrushes exactly:
- the liquid brushes of every model (class of the contents: lava over slime over water), in the order the loader
  tests them: brush model (moving, unsupported) -> CONTENTS_FOG (already a BSP fog medium) -> shape (6..32 sides,
  the first six axial bounds) -> capacity (MAX_LIQUID_BRUSHES brushes, MAX_LIQUID_PLANES planes)
- the medium (optics slot) of each kept brush: an env.json "Liquids" rule (brush shader, then its upward side
  shader), else slime for a water brush whose upward side shader has CONTENTS_SLIME, else its class
- what gameplay sees as liquid and the medium does not (the skipped brushes, with the entity of a brush model)
- overlaps (same medium: union on the GPU; different media: added), touching pairs, the drawn liquid surfaces
  against the top of the brushes under them (a medium above a visible surface, or a gap under it)
- GPU budget: brushes within 4096 units (MAX_GPU_LIQUIDS nearest reach the block), intervals per sun ray from
  points inside the liquids (LIQUID_MAX_HITS = 8 keeps the earliest) and the share of froxel segments where a
  medium has three or more intervals (the slow sorted path of LiquidCoverage)
- memory of the map (plane buffer, hunk)
"""
import collections
import json
import math
import os
import re
import struct
import sys

import water_audit as W

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONSTANTS = open(os.path.join(ROOT, 'shared/rd-rend2/tr_local.h'), encoding='latin-1').read()


def constant(name):
    return int(re.search(r'#define\s+%s\s+(\d+)' % name, CONSTANTS).group(1))


MAX_LIQUID_BRUSHES = constant('MAX_LIQUID_BRUSHES')
MAX_LIQUID_PLANES = constant('MAX_LIQUID_PLANES')
MAX_LIQUID_SIDES = constant('MAX_LIQUID_SIDES')
MAX_GPU_LIQUIDS = constant('MAX_GPU_LIQUIDS')
MAX_HITS = 8
SIZEOF_LIQUID_BRUSH = 4 * (6 + 7)   # liquidBrush_t: bounds + 7 ints
CLASSES = ['water', 'slime', 'lava']
CONTENTS_LAVA, CONTENTS_SLIME, CONTENTS_WATER, CONTENTS_FOG = 0x2, 0x20000, 0x4, 0x8
SURF_NODRAW = 0x200000


def liquid_class(contents):
    if contents & CONTENTS_LAVA:
        return 2
    if contents & CONTENTS_SLIME:
        return 1
    if contents & CONTENTS_WATER:
        return 0
    return -1


def entities(data):
    text = W.lump(data, 0).split(b'\0')[0].decode('latin-1')
    return [dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', m.group(1))) for m in re.finditer(r'\{([^{}]*)\}', text)]


def load_rules(files, base_name):
    """env.json "Liquids" of the map, as R_LoadLiquidRules (pk3 or loose file)"""
    key = 'cubemaps/%s/env.json' % base_name
    text = None
    loose = os.path.join(files.base, key)
    if os.path.isfile(loose):
        text = open(loose, encoding='latin-1').read()
    elif key in files.files:
        text = files.read(key).decode('latin-1')
    if not text:
        return []
    try:
        doc = json.loads(text)
    except ValueError:
        return []
    rules = []
    for entry in doc.get('Liquids', []) if isinstance(doc, dict) else []:
        shader, profile = str(entry.get('Shader', '')).lower(), str(entry.get('Profile', '')).lower()
        if shader and profile in CLASSES:
            rules.append((shader, CLASSES.index(profile)))
    return rules


def rule_profile(rules, name):
    lower = name.lower()
    for pattern, profile in rules:
        if (lower.startswith(pattern[:-1]) if pattern.endswith('*') else lower == pattern):
            return profile
    return -1


class Brush:
    def __init__(self, index, cls, medium, source, shader, top, planes, bounds):
        self.index, self.cls, self.medium, self.source = index, cls, medium, source
        self.shader, self.top, self.planes, self.bounds = shader, top, planes, bounds

    def clip(self, o, d, t0, t1):
        enter, exit_ = t0, t1
        for n0, n1, n2, dist in self.planes:
            denom = n0 * d[0] + n1 * d[1] + n2 * d[2]
            s = n0 * o[0] + n1 * o[1] + n2 * o[2] - dist
            if abs(denom) < 1e-8:
                if s > 0:
                    return None
                continue
            t = -s / denom
            if denom < 0:
                enter = max(enter, t)
            else:
                exit_ = min(exit_, t)
            if enter >= exit_:
                return None
        return (enter, exit_)

    def inside(self, p, eps=0.0):
        return all(n0 * p[0] + n1 * p[1] + n2 * p[2] - dist <= eps for n0, n1, n2, dist in self.planes)


def load_liquids(data, rules):
    """R_LoadLiquidBrushes: kept brushes, skipped records, plane count"""
    sh = W.lump(data, 1)
    shaders = [(sh[o:o + 64].split(b'\0')[0].decode('latin-1'),) + struct.unpack_from('<ii', sh, o + 64)
               for o in range(0, len(sh), 72)]
    pl = W.lump(data, 2)
    planes = [struct.unpack_from('<4f', pl, o) for o in range(0, len(pl), 16)]
    md = W.lump(data, 7)
    models = [struct.unpack_from('<6f4i', md, o) for o in range(0, len(md), 40)]
    br = W.lump(data, 8)
    brushes = [struct.unpack_from('<3i', br, o) for o in range(0, len(br), 12)]
    bs = W.lump(data, 9)
    sides = [struct.unpack_from('<3i', bs, o) for o in range(0, len(bs), 12)]

    first0, count0 = models[0][8], models[0][9]
    kept, skipped, num_planes = [], [], 0

    def model_of(b):
        for m, model in enumerate(models):
            if model[8] <= b < model[8] + model[9]:
                return m
        return -1

    for i, (first_side, num_sides, shader_num) in enumerate(brushes):
        if not 0 <= shader_num < len(shaders):
            continue
        contents = shaders[shader_num][2]
        cls = liquid_class(contents)
        if cls < 0:
            continue
        name = shaders[shader_num][0]
        if not first0 <= i < first0 + count0:
            skipped.append(dict(brush=i, reason='brush model (moving)', shader=name, cls=CLASSES[cls], model=model_of(i)))
            continue
        if contents & CONTENTS_FOG:
            skipped.append(dict(brush=i, reason='fog contents (BSP fog medium)', shader=name, cls=CLASSES[cls], model=0))
            continue
        if num_sides < 6 or num_sides > MAX_LIQUID_SIDES or first_side < 0 or first_side + num_sides > len(sides):
            skipped.append(dict(brush=i, reason='shape', shader=name, cls=CLASSES[cls], model=0))
            continue
        bounds = [[0.0] * 3, [0.0] * 3]
        axial = True
        for k in range(6):
            pn = sides[first_side + k][0]
            if not 0 <= pn < len(planes):
                axial = False
                break
            axis, sign = k >> 1, (1.0 if k & 1 else -1.0)
            if planes[pn][axis] * sign < 0.999:
                axial = False
                break
            bounds[k & 1][axis] = sign * planes[pn][3]
        if not axial or any(bounds[0][a] >= bounds[1][a] for a in range(3)):
            skipped.append(dict(brush=i, reason='shape', shader=name, cls=CLASSES[cls], model=0))
            continue
        if len(kept) >= MAX_LIQUID_BRUSHES or num_planes + num_sides > MAX_LIQUID_PLANES:
            skipped.append(dict(brush=i, reason='capacity', shader=name, cls=CLASSES[cls], model=0))
            continue

        brush_planes, top = [], -1
        for k in range(num_sides):
            pn, side_shader = sides[first_side + k][0], sides[first_side + k][1]
            if not 0 <= pn < len(planes):
                continue
            p = planes[pn]
            brush_planes.append(p)
            if p[2] > 0.7 and 0 <= side_shader < len(shaders) and (
                    top < 0 or (liquid_class(shaders[side_shader][2]) >= 0 and not shaders[side_shader][1] & SURF_NODRAW
                                and shaders[top][1] & SURF_NODRAW)):
                top = side_shader
        top_name = shaders[top][0] if top >= 0 else ''
        medium, source = cls, 'contents'
        profile = rule_profile(rules, name)
        if profile < 0 and top >= 0:
            profile = rule_profile(rules, top_name)
        if profile >= 0:
            medium, source = profile, 'env.json'
        elif cls == 0 and top >= 0 and shaders[top][2] & CONTENTS_SLIME:
            medium, source = 1, 'top side slime'
        num_planes += len(brush_planes)
        kept.append(Brush(i, cls, medium, source, name, top_name, brush_planes, bounds))
    return kept, skipped, num_planes


class SolidWorld:
    """the solid brushes of the world model, bucketed on a 512 unit xy grid"""

    def __init__(self, data):
        sh = W.lump(data, 1)
        contents = [struct.unpack_from('<i', sh, o + 68)[0] for o in range(0, len(sh), 72)]
        pl = W.lump(data, 2)
        planes = [struct.unpack_from('<4f', pl, o) for o in range(0, len(pl), 16)]
        model = struct.unpack_from('<6f4i', W.lump(data, 7), 0)
        br, bs = W.lump(data, 8), W.lump(data, 9)
        self.cells = collections.defaultdict(list)
        for i in range(model[8], model[8] + model[9]):
            first_side, num_sides, shader_num = struct.unpack_from('<3i', br, i * 12)
            if not (0 <= shader_num < len(contents) and contents[shader_num] & 1) or num_sides < 6:
                continue
            ps = [planes[struct.unpack_from('<i', bs, (first_side + k) * 12)[0]] for k in range(num_sides)]
            lo = (-ps[0][3], -ps[2][3], -ps[4][3])
            hi = (ps[1][3], ps[3][3], ps[5][3])
            for cx in range(int(math.floor(lo[0] / 512)), int(math.floor(hi[0] / 512)) + 1):
                for cy in range(int(math.floor(lo[1] / 512)), int(math.floor(hi[1] / 512)) + 1):
                    self.cells[(cx, cy)].append((lo, hi, ps))

    def inside(self, p):
        for lo, hi, ps in self.cells.get((int(math.floor(p[0] / 512)), int(math.floor(p[1] / 512))), ()):
            if all(lo[a] <= p[a] <= hi[a] for a in range(3)) and \
                    all(n0 * p[0] + n1 * p[1] + n2 * p[2] - d <= 0.0 for n0, n1, n2, d in ps):
                return True
        return False


def box_overlap(a, b):
    return [min(a.bounds[1][x], b.bounds[1][x]) - max(a.bounds[0][x], b.bounds[0][x]) for x in range(3)]


def intervals(brushes, o, d, t0, t1):
    out = []
    for b in brushes:
        hit = b.clip(o, d, t0, t1)
        if hit:
            out.append((hit[0], hit[1], b.medium))
    return out


def sample_points(b):
    lo, hi = b.bounds
    center = [(lo[x] + hi[x]) * 0.5 for x in range(3)]
    points = [center, [center[0], center[1], lo[2] + 2.0], [lo[0] + 4.0, lo[1] + 4.0, lo[2] + 4.0],
              [hi[0] - 4.0, hi[1] - 4.0, (lo[2] + hi[2]) * 0.5]]
    return [p for p in points if b.inside(p)]


def gpu_stats(kept):
    """sun rays from inside the liquids, froxel segments of camera rays near them"""
    max_sun_hits, sun_over = 0, 0
    sun_rays = 0
    for b in kept:
        for p in sample_points(b):
            for elevation in (15, 30, 45, 60, 90):
                for azimuth in range(0, 360, 45) if elevation < 90 else (0,):
                    e, a = math.radians(elevation), math.radians(azimuth)
                    d = (math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))
                    hits = len(intervals(kept, p, d, -1.0, 65536.0))
                    sun_rays += 1
                    max_sun_hits = max(max_sun_hits, hits)
                    sun_over += hits > MAX_HITS
    # camera rays: from above each brush top, down into it at several angles, 32 unit segments
    segments, slow = 0, 0
    for b in kept:
        lo, hi = b.bounds
        center = [(lo[x] + hi[x]) * 0.5 for x in range(3)]
        for elevation in (-10, -30, -60, -89):
            for azimuth in range(0, 360, 90):
                e, a = math.radians(elevation), math.radians(azimuth)
                d = (math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e))
                o = (center[0] - d[0] * 600.0, center[1] - d[1] * 600.0, hi[2] + 64.0)
                for k in range(48):
                    hits = intervals(kept, o, d, k * 32.0, k * 32.0 + 32.0)
                    if not hits:
                        continue
                    segments += 1
                    per = collections.Counter(h[2] for h in hits)
                    slow += max(per.values()) >= 3
    return dict(sunRays=sun_rays, maxSunHits=max_sun_hits, sunRaysOverMaxHits=sun_over,
                coveredSegments=segments, slowPathSegments=slow)


def audit_map(files, key, defs):
    data = files.read(key)
    base_name = key[len('maps/'):-len('.bsp')].split('/')[-1]
    rules = load_rules(files, base_name)
    kept, skipped, num_planes = load_liquids(data, rules)
    all_liquids = len(kept) + len(skipped)
    if not all_liquids:
        return None

    ents = entities(data)
    by_model = {int(e['model'][1:]): e for e in ents if e.get('model', '').startswith('*')}
    for s in skipped:
        if s['model'] > 0:
            e = by_model.get(s['model'], {})
            s['entity'] = {k: v for k, v in e.items() if k in ('classname', 'targetname', 'script_targetname', 'spawnflags',
                                                                'origin', 'parm1', 'spawnscript', 'usescript')}

    same = cross = touching = 0
    for i in range(len(kept)):
        for j in range(i + 1, len(kept)):
            ov = box_overlap(kept[i], kept[j])
            if min(ov) > 0.5:
                if kept[i].medium == kept[j].medium:
                    same += 1
                else:
                    cross += 1
            elif min(ov) > -0.5 and sorted(ov)[1] > 0.5:
                touching += 1

    def near(radius):
        best = 0
        for b in kept:
            c = [(b.bounds[0][x] + b.bounds[1][x]) * 0.5 for x in range(3)]
            n = 0
            for o in kept:
                d2 = sum(max(o.bounds[0][x] - c[x], 0.0, c[x] - o.bounds[1][x]) ** 2 for x in range(3))
                n += d2 <= radius * radius
            best = max(best, n)
        return best

    # the drawn liquid surfaces facing up against the top of the kept brushes under them
    surface_checks = []
    _, surfaces = W.scan_bsp(data, defs)
    dv, su = W.lump(data, 10), W.lump(data, 13)
    di = W.lump(data, 11)
    indexes = struct.unpack_from('<%di' % (len(di) // 4), di, 0) if di else ()
    solid = SolidWorld(data)
    for s in surfaces:
        if s['orient'] != 'up' or s['model'] != 0 or liquid_class(s['contents']) < 0:
            continue
        o = s['index'] * 148
        first_vert, num_verts = struct.unpack_from('<2i', su, o + 12)
        verts = [struct.unpack_from('<3f', dv, v * 80) for v in range(first_vert, first_vert + num_verts)]
        if not verts:
            continue
        # exact at the triangle centroids (patches: the centers of their control point quads): 3 units above
        # must be outside every kept brush (no medium in the air over the surface), 3 units below inside one
        # (no gap under it); points inside solid world brushes are hidden and not counted
        z = sum(v[2] for v in verts) / len(verts)
        samples = []
        if s['type'] == W.MST_PATCH:
            pw, ph = struct.unpack_from('<2i', su, o + 140)
            for y in range(ph - 1):
                for x in range(pw - 1):
                    quad = [verts[q] for q in (y * pw + x, y * pw + x + 1, (y + 1) * pw + x, (y + 1) * pw + x + 1)
                            if q < len(verts)]
                    samples.append([sum(v[a] for v in quad) / len(quad) for a in range(3)])
        else:
            first_index, num_indexes = struct.unpack_from('<2i', su, o + 20)
            for t in range(first_index, first_index + num_indexes - 2, 3):
                tri = [verts[indexes[t + k]] for k in range(3) if indexes[t + k] < len(verts)]
                if len(tri) == 3:
                    samples.append([sum(v[a] for v in tri) / 3.0 for a in range(3)])
        above = gap = counted = 0
        for p in samples:
            up, down = (p[0], p[1], p[2] + 3.0), (p[0], p[1], p[2] - 3.0)
            if solid.inside(up) and solid.inside(down):
                continue
            counted += 1
            above += any(b.inside(up) for b in kept) and not solid.inside(up)
            gap += not any(b.inside(down) for b in kept) and not solid.inside(down)
        if counted and gap == counted:
            status = 'no brush'
        elif above > 0.1 * counted:
            status = 'medium above surface at %d of %d samples' % (above, counted)
        elif gap > 0.1 * counted:
            status = 'gap under surface at %d of %d samples' % (gap, counted)
        else:
            status = 'ok'
        surface_checks.append(dict(shader=s['shader'], z=round(z, 1), samples=counted, above=above, gap=gap,
                                   status=status))

    count = collections.Counter(CLASSES[b.cls] for b in kept)
    media = collections.Counter(CLASSES[b.medium] for b in kept)
    sources = collections.Counter(b.source for b in kept)
    reasons = collections.Counter(s['reason'] for s in skipped)
    result = dict(
        map=key[len('maps/'):-len('.bsp')], pk3=files.files[key][0], rules=len(rules),
        kept=len(kept), planes=num_planes, maxSides=max((len(b.planes) for b in kept), default=0),
        classes={c: count.get(c, 0) for c in CLASSES}, media={c: media.get(c, 0) for c in CLASSES},
        slotSources=dict(sources),
        skipped={r: reasons.get(r, 0) for r in ('fog contents (BSP fog medium)', 'shape', 'capacity', 'brush model (moving)')},
        gameplayOnly=skipped, overlapSameMedium=same, overlapCrossMedium=cross, touchingPairs=touching,
        within4k=near(4096.0), gpuMax=MAX_GPU_LIQUIDS,
        shaders=sorted(set('%s %s -> %s medium (%s), top %s' % (CLASSES[b.cls], b.shader, CLASSES[b.medium], b.source,
                                                                  b.top or '-') for b in kept)),
        surfaces=surface_checks,
        memory=dict(planeBuffer=max(num_planes, 1) * 16, hunk=len(kept) * SIZEOF_LIQUID_BRUSH + num_planes * 16),
    )
    result.update(gpu_stats(kept))
    return result


def main(argv):
    base = W.DEFAULT_BASE
    out_json = only = None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--json':
            out_json = args.pop(0)
        elif a == '--map':
            only = args.pop(0).lower()
        else:
            base = a
    files = W.Pk3Files(base)
    defs, _ = W.load_shader_definitions(files)
    bsps = sorted(k for k in files.files if k.startswith('maps/') and k.endswith('.bsp'))
    results = []
    print('%d BSPs in %s' % (len(bsps), base))
    for key in bsps:
        if only and key[len('maps/'):-len('.bsp')] != only:
            continue
        r = audit_map(files, key, defs)
        if not r:
            continue
        results.append(r)
        sk = r['skipped']
        print('%-22s %-24s kept %3d (W%d S%d L%d) media W%d S%d L%d | skipped fog %d shape %d cap %d bmodel %d | '
              'overlap %d/%d touching %d | 4k %d/%d | sun hits max %d (>8: %d of %d) | slow seg %d/%d | %d B' % (
                  r['map'], r['pk3'], r['kept'], r['classes']['water'], r['classes']['slime'], r['classes']['lava'],
                  r['media']['water'], r['media']['slime'], r['media']['lava'],
                  sk['fog contents (BSP fog medium)'], sk['shape'], sk['capacity'], sk['brush model (moving)'],
                  r['overlapSameMedium'], r['overlapCrossMedium'], r['touchingPairs'], r['within4k'], r['gpuMax'],
                  r['maxSunHits'], r['sunRaysOverMaxHits'], r['sunRays'], r['slowPathSegments'], r['coveredSegments'],
                  r['memory']['planeBuffer']))
        for line in r['shaders']:
            print('      ', line)
        for s in r['gameplayOnly']:
            print('       gameplay only: brush %d model %d %s %s %s %s' % (s['brush'], s['model'], s['cls'], s['reason'],
                                                                         s['shader'], s.get('entity', '')))
        bad = [s for s in r['surfaces'] if s['status'] != 'ok']
        print('       drawn liquid surfaces up: %d, on a brush top: %d%s' % (
            len(r['surfaces']), len(r['surfaces']) - len(bad),
            ''.join('\n         %s z %.0f: %s' % (s['shader'], s['z'], s['status']) for s in bad)))
    print('%d maps with liquids' % len(results))
    if out_json:
        with open(out_json, 'w') as f:
            json.dump(results, f, indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
