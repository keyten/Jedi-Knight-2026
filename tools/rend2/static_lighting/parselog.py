"""Parse a qconsole.log with '==== MAP x ====' / r_vfogStaticStats blocks into per map JSON,
and match the structured lights against the emissive fixtures of bspinfo.py.

python parselog.py <qconsole.log> <outprefix> [physThreshold=0.6]
"""
import re, sys, json, math, os, collections

HERE = os.path.dirname(os.path.abspath(__file__))
log = open(sys.argv[1], encoding='latin1').read().splitlines()
prefix = sys.argv[2]
PHYS = float(sys.argv[3]) if len(sys.argv) > 3 else 0.6
LEAK_MAX, EXCESS_MAX = 0.15, 0.2

def strip(l):
    return re.sub(r'^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ', '', l)

maps = collections.OrderedDict()
cur = None
num = r'(-?[\d.]+(?:e[-+]?\d+)?)'
for raw in log:
    l = strip(raw)
    m = re.match(r'==== MAP (\S+) ====', l)
    if m:
        cur = {'map': m.group(1), 'stats': {}, 'lights': {}, 'proxies': {}, 'portals': {}, 'load': {}}
        maps[m.group(1)] = cur
        continue
    if cur is None:
        continue
    m = re.match(r'\[map load\] static lighting detail: (\d+) cells, trace (\d+) ms \((\d+) rays\), area sources (\d+) ms, reconstruct (\d+) ms, other (\d+) ms', l)
    if m:
        cur['load'].update(dict(zip(['cells', 'trace', 'rays', 'area', 'reconstruct', 'other'], map(int, m.groups()))))
    m = re.match(r'\[map load\] (world static lighting|world total|local map total|surfaces total)\s+(\d+) ms', l)
    if m:
        cur['load'][m.group(1)] = int(m.group(2))
    s = cur['stats']
    for key, pat in [
        ('probes', r'\s+probes\s+(\d+) cells, (\d+) valid'),
        ('sun', r'\s+sun visibility\s+(\d+) cells traced \((\d+) refined at edges, (\d+) rays\), (\d+) see the sky'),
        ('seeds', r'\s+seeds\s+(\d+), (\d+) fitted, (\d+) merged'),
        ('pointProxies', r'\s+point proxies\s+(\d+) \((\d+) sources over'),
        ('areaAnchors', r'\s+area anchors\s+(\d+) accepted of (\d+) static sources \((\d+) emissive'),
        ('rayRms', r'\s+ray residual\s+mean ' + num + ', P95 ' + num),
        ('sigma', r'\s+position sigma\s+mean ' + num),
        ('directional', r'\s+directional\s+(\d+) cells, ' + num + '% of the baked light attributed, ' + num + '% directional'),
        ('structured', r'\s+structured\s+(\d+) physical, (\d+) transport, (\d+) relocated, (\d+) traces'),
        ('promoted', r'\s+promoted\s+\S+: (\d+) points, (\d+) spots, ' + num + '% of the baked light, excess ' + num + '%'),
        ('portals', r'\s+portals\s+(\d+) \((\d+) candidates, (\d+) shafts, (\d+) sky\), ' + num + '% of the baked light in shafts, (\d+) traces, ' + num + ' msec'),
        ('msec', r'\s+msec\s+total (\d+): sun trace (\d+), area sources (\d+), reconstruction (\d+) \(split ' + num + ', gradients ' + num + ', seeds ' + num + ', fit ' + num + ', anchors ' + num + ', attribution ' + num + ', lights ' + num + '\), upload (\d+)'),
        ('sky', r'map sun: (\S+)'),
    ]:
        m = re.match(pat, l)
        if m:
            s[key] = [float(x) if re.match(r'^-?[\d.]+(e[-+]?\d+)?$', x) else x for x in m.groups()]
    m = re.match(r'\s+(\d+) (transport|point|spot|rect)\s*((?: promoted| relocated| animated)*) \(' + num + ' ' + num + ' ' + num + r'\) R ' + num + ' color ' + num + ' ' + num + ' ' + num +
                 ' conf ' + num + ' vis ' + num + ' phys ' + num + ' fit ' + num + ' leak ' + num + ' excess ' + num + r' cluster (-?\d+)(.*)', l)
    if m:
        g = m.groups()
        d = {'i': int(g[0]), 'kind': g[1], 'flags': g[2].split(), 'origin': [float(g[3]), float(g[4]), float(g[5])], 'R': float(g[6]),
             'color': [float(g[7]), float(g[8]), float(g[9])], 'conf': float(g[10]), 'vis': float(g[11]), 'phys': float(g[12]),
             'fit': float(g[13]), 'leak': float(g[14]), 'excess': float(g[15]), 'cluster': int(g[16])}
        m2 = re.search(r'axis ' + num + ' ' + num + ' ' + num + ' cone ' + num + ' / ' + num + ' deg spot ' + num, g[17])
        if m2:
            d['axis'] = [float(x) for x in m2.groups()[:3]]
            d['cone'] = [float(m2.group(4)), float(m2.group(5))]
            d['spot'] = float(m2.group(6))
        cur['lights'][d['i']] = d
        continue
    m = re.match(r'\s+(\d+) (point|rect)\s+\(' + num + ' ' + num + ' ' + num + r'\) color ' + num + ' ' + num + ' ' + num + ' confidence ' + num + ' sigma ' + num + ' range ' + num + r' support (\d+)', l)
    if m:
        g = m.groups()
        cur['proxies'][int(g[0])] = {'type': g[1], 'pos': [float(g[2]), float(g[3]), float(g[4])], 'color': [float(g[5]), float(g[6]), float(g[7])],
                                    'conf': float(g[8]), 'sigma': float(g[9]), 'range': float(g[10]), 'support': int(g[11])}
        continue
    m = re.match(r'\s+(\d+)( shaft)?( sky)? \(' + num + ' ' + num + ' ' + num + r'\) n ' + num + ' ' + num + ' ' + num + ' size ' + num + ' x ' + num + ' range ' + num +
                 ' color ' + num + ' ' + num + ' ' + num + ' conf ' + num + ' solid ' + num + ' leak ' + num + ' excess ' + num + r' receivers (\d+) source (\d+) clusters (-?\d+)\|(-?\d+) areas (-?\d+)\|(-?\d+)', l)
    if m:
        g = m.groups()
        cur['portals'][int(g[0])] = {'shaft': bool(g[1]), 'sky': bool(g[2]), 'center': [float(g[3]), float(g[4]), float(g[5])], 'n': [float(g[6]), float(g[7]), float(g[8])],
                                    'size': [float(g[9]), float(g[10])], 'range': float(g[11]), 'color': [float(g[12]), float(g[13]), float(g[14])], 'conf': float(g[15]),
                                    'solid': float(g[16]), 'leak': float(g[17]), 'excess': float(g[18]), 'receivers': int(g[19]), 'source': int(g[20]),
                                    'clusters': [int(g[21]), int(g[22])], 'areas': [int(g[23]), int(g[24])]}
        continue
    m = re.match(r'(maps/\S+\.bsp) \(' + num + ' ' + num + ' ' + num + r'\) : ' + num, l)
    if m:
        cur['viewpos'] = [float(x) for x in m.groups()[1:]]

def reason(L, promotedCount, cap):
    """why a light is not promoted (the order of the gates in ClassifyPoint / BuildLights)"""
    if 'promoted' in L['flags']:
        return 'PROMOTED'
    if L['kind'] == 'rect':
        return 'rect (never promoted)'
    if L['kind'] == 'transport':
        if L['vis'] == 0 and L['phys'] == 0 and L['fit'] == 0:
            return 'transport: not fitted'
        return 'transport: phys<0.35'
    if L['phys'] < PHYS:
        # which factor of V * sqrt(quality * cPos) * conf^1/4 is the weakest
        return 'low physical confidence'
    if 'animated' in L['flags']:
        return 'animated style'
    if L['leak'] > LEAK_MAX:
        return 'leak'
    if L['excess'] > EXCESS_MAX:
        return 'excess'
    if L['leak'] == 0 and L['excess'] == 0:
        return 'source cap / not evaluated'
    return 'other (seen<=0 / no start cell)'

def weak_factor(L):
    # phys = V * sqrt(q * cPos) * conf^0.25; report the smallest of V, sqrt(q), conf^0.25
    f = {'visibility': L['vis'], 'fit': math.sqrt(max(L['fit'], 0)), 'transport conf': L['conf'] ** 0.25}
    if L['vis'] > 0 and L['fit'] > 0 and L['conf'] > 0:
        rest = L['phys'] / (L['vis'] * math.sqrt(L['fit']) * L['conf'] ** 0.25)
        f['position (sigma/R)'] = math.sqrt(max(rest, 0) ** 2)
    return min(f, key=f.get)

summary = []
for name, M in maps.items():
    fn = os.path.join(HERE, name.replace('/', '_') + '.emit.json')
    fixtures = json.load(open(fn))['fixtures'] if os.path.exists(fn) else None
    lights = list(M['lights'].values())
    reasons = collections.Counter()
    weak = collections.Counter()
    for L in lights:
        L['reason'] = reason(L, 0, 32)
        reasons[L['reason']] += 1
        if L['reason'] in ('low physical confidence', 'transport: phys<0.35') and L['kind'] != 'rect':
            L['weak'] = weak_factor(L)
            weak[L['weak']] += 1
        if fixtures:
            best = min(fixtures, key=lambda f: math.dist(f['c'], L['origin']))
            L['fixtureDist'] = math.dist(best['c'], L['origin'])
            L['fixture'] = best['shaders'][0]
    M['reasons'] = dict(reasons)
    M['weak'] = dict(weak)
    kinds = collections.Counter(L['kind'] for L in lights)
    M['kinds'] = dict(kinds)
    M['physical'] = sum(1 for L in lights if L['kind'] in ('point', 'spot') and L['phys'] >= PHYS)
    if fixtures is not None:
        M['fixtures'] = len(fixtures)
        # fixtures with a non transport light within 96 units
        M['fixturesMatched'] = sum(1 for f in fixtures if any(L['kind'] in ('point', 'spot') and math.dist(f['c'], L['origin']) < 96 for L in lights))
        M['fixturesAnyProxy'] = sum(1 for f in fixtures if any(math.dist(f['c'], L['origin']) < 128 for L in lights))
    json.dump(M, open(prefix + '_' + name.replace('/', '_') + '.json', 'w'), indent=1)
    s = M['stats']
    print('%-9s cells %s seeds %s proxies %s kinds %s phys %d reasons %s weak %s portals %s fixtures %s/%s/%s' % (
        name, s.get('probes', ['?'])[0], s.get('seeds'), s.get('pointProxies'), dict(kinds), M['physical'], dict(reasons), dict(weak), s.get('portals'),
        M.get('fixtures'), M.get('fixturesMatched'), M.get('fixturesAnyProxy')))
