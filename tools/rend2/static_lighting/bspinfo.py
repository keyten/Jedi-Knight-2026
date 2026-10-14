"""Offline ground truth for the static lighting validation.

python bspinfo.py <map> [...]  -> writes <map>.emit.json: emissive surface clusters
(q3map_surfacelight / glow stage shaders), light grid statistics, info_null spot targets.
"""
import zipfile, struct, sys, re, json, math, glob, os, collections

BASE = os.environ.get('JKA_BASE', r'C:/Users/Keyten/Desktop/projects/OpenJK/build-rend2/base')
OUT = os.path.dirname(os.path.abspath(__file__))

def pk3s():
    # later (alphabetically higher) pk3 overrides earlier; game order
    return sorted(glob.glob(BASE + '/*.pk3'), key=lambda p: os.path.basename(p).lower())

def read_file(name):
    data = None
    for p in pk3s():
        z = zipfile.ZipFile(p)
        try:
            data = z.read(name)
        except KeyError:
            pass
    return data

def shader_table():
    """name -> dict(surfacelight=float or 0, glow=bool, color=(r,g,b) or None)"""
    texts = {}
    for p in pk3s():
        z = zipfile.ZipFile(p)
        for i in z.infolist():
            if i.filename.lower().startswith('shaders/') and i.filename.lower().endswith('.shader'):
                texts[i.filename.lower()] = z.read(i).decode('latin1')
    table = {}
    for fn, t in texts.items():
        t = re.sub(r'//[^\n]*', '', t)
        pos = 0
        tok = re.compile(r'\{|\}|[^\s{}]+')
        toks = tok.findall(t)
        i = 0
        while i < len(toks):
            name = toks[i]
            if i + 1 < len(toks) and toks[i + 1] == '{':
                depth = 0
                j = i + 1
                body = []
                while j < len(toks):
                    if toks[j] == '{':
                        depth += 1
                    elif toks[j] == '}':
                        depth -= 1
                        if depth == 0:
                            break
                    body.append(toks[j].lower())
                    j += 1
                info = {'surfacelight': 0.0, 'glow': False, 'sky': False}
                for k, b in enumerate(body):
                    if b == 'q3map_surfacelight' and k + 1 < len(body):
                        try:
                            info['surfacelight'] = float(body[k + 1])
                        except ValueError:
                            pass
                    elif b == 'glow':
                        info['glow'] = True
                    elif b == 'surfaceparm' and k + 1 < len(body) and body[k + 1] == 'sky':
                        info['sky'] = True
                table[name.lower()] = info
                i = j + 1
            else:
                i += 1
    return table

def lumps(d):
    return [struct.unpack('<ii', d[8 + 8 * k:16 + 8 * k]) for k in range(18)]

def analyse(m, shaders):
    d = read_file('maps/%s.bsp' % m)
    L = lumps(d)
    ents = d[L[0][0]:L[0][0] + L[0][1]].decode('latin1')
    blocks = [dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', b)) for b in re.findall(r'\{([^{}]*)\}', ents)]
    world = blocks[0]
    gs = [float(x) for x in world.get('gridsize', '64 64 128').split()]
    # shaders
    off, ln = L[1]
    names = [d[off + 72 * k: off + 72 * k + 64].split(b'\0')[0].decode('latin1').lower() for k in range(ln // 72)]
    # model 0
    off, ln = L[7]
    mins = struct.unpack('<3f', d[off:off + 12]); maxs = struct.unpack('<3f', d[off + 12:off + 24])
    origin = [gs[k] * math.ceil(mins[k] / gs[k]) for k in range(3)]
    top = [gs[k] * math.floor(maxs[k] / gs[k]) for k in range(3)]
    bounds = [int((top[k] - origin[k]) / gs[k]) + 1 for k in range(3)]
    ncell = bounds[0] * bounds[1] * bounds[2]
    goff, gln = L[15]; aoff, aln = L[17]
    ngrid = gln // 30
    arr = struct.unpack('<%dH' % (aln // 2), d[aoff:aoff + aln])
    grid = [d[goff + 30 * k: goff + 30 * k + 30] for k in range(ngrid)]
    nvalid = nzero = nanim = 0
    lum = []
    for i in range(min(ncell, len(arr))):
        g = grid[arr[i]] if arr[i] < ngrid else None
        if g is None:
            continue
        styles = g[24:28]
        if styles[0] == 255:
            continue
        nvalid += 1
        a = g[0:3]; dd = g[12:15]
        if max(a) == 0 and max(dd) == 0:
            nzero += 1
        if any(s != 255 for s in styles[1:]):
            nanim += 1
    # surfaces
    voff, vln = L[10]
    soff, sln = L[13]
    nsurf = sln // 148
    emit = []
    for k in range(nsurf):
        s = d[soff + 148 * k: soff + 148 * k + 148]
        shaderNum, fogNum, stype, firstVert, numVerts = struct.unpack('<5i', s[:20])
        name = names[shaderNum] if 0 <= shaderNum < len(names) else ''
        info = shaders.get(name)
        if not info or info['sky'] or not (info['surfacelight'] > 0 or info['glow']):
            continue
        pts = [struct.unpack('<3f', d[voff + 80 * v: voff + 80 * v + 12]) for v in range(firstVert, firstVert + numVerts)]
        if not pts:
            continue
        c = [sum(p[j] for p in pts) / len(pts) for j in range(3)]
        ext = max(max(p[j] for p in pts) - min(p[j] for p in pts) for j in range(3))
        emit.append({'shader': name, 'c': c, 'ext': ext, 'surfacelight': info['surfacelight'], 'glow': info['glow']})
    # cluster emissive surfaces within 48 units (one fixture)
    clusters = []
    for e in emit:
        for cl in clusters:
            if math.dist(cl['c'], e['c']) < 48:
                n = cl['n']
                cl['c'] = [(cl['c'][j] * n + e['c'][j]) / (n + 1) for j in range(3)]
                cl['n'] += 1
                cl['shaders'].add(e['shader'])
                cl['surfacelight'] = max(cl['surfacelight'], e['surfacelight'])
                break
        else:
            clusters.append({'c': e['c'], 'n': 1, 'shaders': {e['shader']}, 'surfacelight': e['surfacelight']})
    for cl in clusters:
        cl['shaders'] = sorted(cl['shaders'])
    infonull = [[float(x) for x in b['origin'].split()] for b in blocks if b.get('classname') in ('info_null', 'info_notnull') and 'targetname' in b and 'origin' in b]
    res = {'map': m, 'gridsize': gs, 'bounds': bounds, 'cells': ncell, 'uniqueGrid': ngrid, 'valid': nvalid,
           'blackValid': nzero, 'animated': nanim, 'emissiveSurfaces': len(emit), 'fixtures': clusters,
           'surfacelightFixtures': sum(1 for c in clusters if c['surfacelight'] > 0), 'infoNullTargets': len(infonull)}
    json.dump(res, open(os.path.join(OUT, m.replace('/', '_') + '.emit.json'), 'w'), indent=1)
    print('%-10s grid %s=%d cells (%d unique), valid %d (%.1f%%), black-valid %d, animated %d, emissive surfs %d -> fixtures %d (%d surfacelight), info_null targets %d' % (
        m, bounds, ncell, ngrid, nvalid, 100.0 * nvalid / max(ncell, 1), nzero, nanim, len(emit), len(clusters), res['surfacelightFixtures'], len(infonull)))

if __name__ == '__main__':
    sh = shader_table()
    print('shaders', len(sh), 'surfacelight', sum(1 for v in sh.values() if v['surfacelight'] > 0), 'glow', sum(1 for v in sh.values() if v['glow']))
    for m in sys.argv[1:]:
        analyse(m, sh)
