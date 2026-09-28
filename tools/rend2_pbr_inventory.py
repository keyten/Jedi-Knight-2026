#!/usr/bin/env python3
"""
Read-only material inventory of Jedi Academy PK3s for the rend2 auto PBR
heuristics (r_autoPBR, shared/rd-rend2/tr_autopbr.cpp).

It links  model skin -> surface -> shader -> diffuse texture,  finds which
materials already have authored PBR data (explicit specMap/rmoMap/ormMap/...
keywords or auto discovered _specGloss/_rmo/_orm images, see
CollapseStagesToLightall in tr_shader.cpp), and runs a Python port of
R_ClassifyMaterial on the legacy (diffuse-only) ones.

KEEP THE DICTIONARIES AND RULE ORDER IN SYNC WITH tr_autopbr.cpp.

usage: rend2_pbr_inventory.py <base dir with *.pk3> [--selftest] [--list CLASS] [--pairs] [--skin [--mixed]]
  --pairs  prints shader<TAB>diffuse<TAB>class<TAB>reason for every material,
           to diff against the C++ classifier
  --skin   skin scattering eligibility (r_skinSSS, port of R_SkinSSSClassifyShader
           in tr_skinsss.cpp) of every skin class material; --mixed = r_skinSSSMixedHeads 1
Nothing is written; the report goes to stdout (markdown).
"""

import collections
import os
import re
import sys
import zipfile

# --------------------------------------------------------------------------
# classifier (port of tr_autopbr.cpp)
# --------------------------------------------------------------------------

CLASSES = ['generic', 'metal', 'skin', 'cloth', 'leather', 'plastic', 'hair']

# material words, valid in any path (rule 1: file name, rule 5: directories)
MATERIAL_TOKENS = {
    'hair': 'hair', 'hairs': 'hair', 'beard': 'hair', 'ponytail': 'hair', 'braid': 'hair',
    'braids': 'hair', 'mustache': 'hair', 'fur': 'hair',
    'leather': 'leather', 'belt': 'leather', 'belts': 'leather', 'holster': 'leather',
    'boot': 'leather', 'boots': 'leather', 'glove': 'leather', 'gloves': 'leather',
    'cloth': 'cloth', 'fabric': 'cloth', 'robe': 'cloth', 'robes': 'cloth', 'cape': 'cloth',
    'cloak': 'cloth', 'tunic': 'cloth', 'skirt': 'cloth', 'sleeve': 'cloth', 'sleeves': 'cloth',
    'scarf': 'cloth', 'pants': 'cloth', 'shirt': 'cloth', 'carpet': 'cloth', 'rug': 'cloth',
    'curtain': 'cloth', 'banner': 'cloth', 'flag': 'cloth', 'tapestry': 'cloth',
    'rubber': 'plastic', 'plastic': 'plastic', 'hose': 'plastic', 'tire': 'plastic',
    'armor': 'plastic', 'armour': 'plastic', 'helmet': 'plastic',
    'metal': 'metal', 'metl': 'metal', 'steel': 'metal', 'iron': 'metal', 'chrome': 'metal',
    'grate': 'metal', 'grating': 'metal', 'pipe': 'metal', 'pipes': 'metal', 'rivet': 'metal',
    'aluminum': 'metal', 'brass': 'metal', 'copper': 'metal', 'bronze': 'metal', 'gold': 'metal',
    'silver': 'metal', 'hilt': 'metal',
}

# models/players/<dir>: whole character archetype (rule 3)
CHARACTER_DIRS = {
    # droids: bare / painted metal bodies
    'droids': 'metal', 'assassin_droid': 'metal', 'saber_droid': 'metal', 'gonk': 'metal',
    'mouse': 'metal', 'probe': 'metal', 'protocol': 'metal', 'r2d2': 'metal', 'r5d2': 'metal',
    'remote_sp': 'metal', 'remote': 'metal', 'sentry': 'metal', 'interrogator': 'metal',
    'mark1': 'metal',
    # vehicles: painted hulls are a dielectric coating
    'atst': 'plastic', 'lambdashuttle': 'plastic', 'tie_bomber': 'plastic',
    'tie_fighter': 'plastic', 'x-wing': 'plastic', 'z-95': 'plastic', 'swoop': 'plastic',
    # armoured troopers: plastic-like shells, not metal
    'stormtrooper': 'plastic', 'shadowtrooper': 'plastic', 'snowtrooper': 'plastic',
    'stormpilot': 'plastic', 'swamptrooper': 'plastic', 'hazardtrooper': 'plastic',
    'rockettrooper': 'plastic', 'boba_fett': 'plastic',
    # furry creatures
    'chewbacca': 'hair', 'wampa': 'hair', 'tauntaun': 'hair',
    # naked scaly / leathery creatures (clothed aliens such as trandoshan or
    # noghri go through the body part rule instead)
    'rancor': 'skin', 'mutant_rancor': 'skin', 'howler': 'skin', 'sand_creature': 'skin',
    # fully wrapped
    'tusken': 'cloth',
    # rocks: not a character
    'rocks': 'generic',
}

# body part words for organic characters (rule 4)
PART_TOKENS = {
    'head': 'skin', 'face': 'skin', 'forehead': 'skin', 'eyes': 'skin', 'eye': 'skin',
    'eyesmouth': 'skin', 'mouth': 'skin', 'teeth': 'skin', 'hand': 'skin', 'hands': 'skin',
    'neck': 'skin', 'skin': 'skin', 'flesh': 'skin', 'caps': 'skin', 'cap': 'skin',
    'torso': 'cloth', 'legs': 'cloth', 'leg': 'cloth', 'hips': 'cloth', 'lower': 'cloth',
    'coat': 'cloth', 'jacket': 'cloth', 'vest': 'cloth', 'uniform': 'cloth', 'cuff': 'cloth',
    'cuffs': 'cloth', 'clothes': 'cloth', 'hood': 'cloth', 'flap': 'cloth', 'dress': 'cloth',
    'collar': 'cloth', 'tentacles': 'skin', 'lekku': 'skin',
}

# models/weapons2/<dir> that are not metal
WEAPON_NOT_METAL = {'noweap': 'generic', 'tusken_staff': 'generic', 'noghri_stick': 'generic'}


def tokenize(path):
    """split on / _ - . digits and lower->Upper case changes, lowercase"""
    path = re.sub(r'([a-z])([A-Z])', r'\1 \2', path)
    return [t for t in re.split(r'[^A-Za-z]+', path.lower()) if len(t) > 1]


def strip_ext(p):
    b, e = os.path.splitext(p)
    return b if e.lower() in ('.tga', '.jpg', '.png', '.jpeg', '.dds') else p


def classify(shader_name, diffuse_name):
    """returns (class, reason) -- same order as R_ClassifyMaterial"""
    names = [strip_ext(shader_name.lower()).replace('\\', '/')]
    if diffuse_name:
        d = strip_ext(diffuse_name.lower()).replace('\\', '/')
        if d != names[0]:
            names.append(d)

    # 1. material word in a file name
    for n in names:
        for t in tokenize(n.rsplit('/', 1)[-1]):
            if t in MATERIAL_TOKENS:
                return MATERIAL_TOKENS[t], 'name:' + t

    for n in names:
        parts = n.split('/')
        # 2. weapons
        if len(parts) >= 3 and parts[0] == 'models' and parts[1] == 'weapons2':
            if parts[2] in WEAPON_NOT_METAL:
                return WEAPON_NOT_METAL[parts[2]], 'weapon:' + parts[2]
            return 'metal', 'weapons2'
        if len(parts) >= 4 and parts[0] == 'models' and parts[1] == 'players':
            char = parts[2]
            # 3. character archetype
            if char in CHARACTER_DIRS:
                return CHARACTER_DIRS[char], 'character:' + char
            # 4. body part of an organic character, last token first
            #    (torso_01_hands is a hand texture)
            for t in reversed(tokenize(parts[-1])):
                if t in PART_TOKENS:
                    return PART_TOKENS[t], 'part:' + t

    # 5. material word in a directory
    for n in names:
        for d in n.split('/')[:-1]:
            for t in tokenize(d):
                if t in MATERIAL_TOKENS:
                    return MATERIAL_TOKENS[t], 'dir:' + t

    return 'generic', 'no token'


SELFTEST = [
    ('models/players/stormtrooper/torso', 'plastic'),
    ('models/players/stormtrooper/caps', 'plastic'),
    ('models/players/kyle/kyle_head', 'skin'),
    ('models/players/jedi_hf/head_face', 'skin'),
    ('models/players/alora/alora_torso', 'cloth'),
    ('models/players/alora/alora_legs_blue', 'cloth'),
    ('models/players/protocol/c3po_leg', 'metal'),
    ('models/players/r2d2/r2d2', 'metal'),
    ('models/players/chewbacca/torso', 'hair'),
    ('models/players/kyle/kyle_hair', 'hair'),
    ('models/players/reborn/boots', 'leather'),
    ('models/players/jedi_tf/torso_01_hands', 'skin'),
    ('models/players/alora2/alora2_torso_cuffs', 'cloth'),
    ('models/players/trandoshan/trandoshan_torso', 'cloth'),
    ('models/players/trandoshan/trandoshan_head', 'skin'),
    ('models/players/tusken/head', 'cloth'),
    ('models/players/rancor/rancor_body', 'skin'),
    ('models/players/kyle/kyle_arms', 'generic'),
    ('models/players/reborn/boss_hood', 'cloth'),
    ('models/players/jedi_tf/tentacles_01', 'skin'),
    ('models/players/mark1/arms', 'metal'),
    ('textures/imperial/vent_hood', 'generic'),
    ('models/weapons2/blaster_r/blaster', 'metal'),
    ('models/weapons2/tusken_staff/staff', 'generic'),
    ('textures/imperial/basic_metal', 'metal'),
    ('textures/imperial/wall_panel', 'generic'),
    ('textures/yavin/stone_wall1', 'generic'),
    ('textures/factory/metalgrate', 'generic'),  # no camel case, one token: stays generic
    ('textures/factory/metal_grate', 'metal'),
    ('models/map_objects/imperial/crate_armor', 'plastic'),
    ('textures/metalwork/floor', 'generic'),
    ('textures/metal/floor', 'metal'),
]


def selftest():
    bad = 0
    for path, want in SELFTEST:
        got, reason = classify(path, path)
        ok = got == want
        bad += not ok
        print('%-4s %-50s %-8s %-8s %s' % ('ok' if ok else 'FAIL', path, want, got, reason))
    print('%d failures' % bad)
    return bad


# --------------------------------------------------------------------------
# PK3 index
# --------------------------------------------------------------------------

IMAGE_EXT = ('.tga', '.jpg', '.png', '.jpeg', '.dds')
AUTHORED_SUFFIXES = ('_specgloss', '_rmo', '_orm')
PBR_KEYWORDS = ('specmap', 'specularmap', 'rmomap', 'rmosmap', 'moxrmap', 'mosrmap', 'ormmap', 'ormsmap')
SCALAR_KEYWORDS = ('specularreflectance', 'specularexponent', 'gloss', 'roughness', 'specularscale')


class Index:
    def __init__(self, base):
        self.files = {}          # lowercase path -> pk3 (last one wins, like the fs search order)
        self.images = set()      # lowercase path without extension
        self.shaders = {}        # name -> info dict
        self.skins = []          # (skin path, surface, shader)
        pk3s = sorted(p for p in os.listdir(base) if p.lower().endswith('.pk3'))
        texts = []
        for p in pk3s:
            z = zipfile.ZipFile(os.path.join(base, p))
            for n in z.namelist():
                ln = n.lower()
                self.files[ln] = p
                if ln.endswith(IMAGE_EXT):
                    self.images.add(os.path.splitext(ln)[0])
                elif ln.endswith(('.shader', '.mtr')):
                    texts.append((ln, z.read(n).decode('latin1')))
                elif ln.endswith('.skin'):
                    for line in z.read(n).decode('latin1').splitlines():
                        if ',' in line:
                            s, sh = line.split(',', 1)
                            sh = sh.strip().lower()
                            if sh and not sh.endswith('nodraw'):
                                self.skins.append((ln, s.strip().lower(), strip_ext(sh)))
            self.pk3s = pk3s
        # .shader first, .mtr overrides (tr_shader.cpp looks for .mtr first)
        texts.sort(key=lambda t: t[0].endswith('.mtr'))
        for fn, text in texts:
            for name, body in parse_shader_file(text):
                self.shaders[name] = dict(parse_shader_body(body), file=fn)

    def has_image(self, name):
        return strip_ext(name.lower()) in self.images


def parse_shader_file(text):
    text = re.sub(r'//[^\n]*', '', text)
    i, n = 0, len(text)
    toks = re.finditer(r'\{|\}|[^\s{}]+', text)
    depth, name, start = 0, None, 0
    for m in toks:
        t = m.group(0)
        if t == '{':
            if depth == 0:
                start = m.end()
            depth += 1
        elif t == '}':
            depth -= 1
            if depth == 0 and name:
                yield name.lower(), text[start:m.start()]
                name = None
        elif depth == 0:
            name = t


def parse_shader_body(body):
    stages, cur, depth = [], None, 0
    for line in body.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith('{'):
            cur = []
            stages.append(cur)
            continue
        if line.startswith('}'):
            cur = None
            continue
        if cur is not None:
            cur.append(line.lower().split())
    info = {'diffuse': None, 'explicit': None, 'scalar': False, 'shiny': False, 'stages': len(stages)}
    info['convert'] = lightall_status(stages)
    info['skinlayer'] = skin_layering(stages)
    for st in stages:
        kw = {l[0]: l[1:] for l in st if l}
        m = kw.get('map') or kw.get('clampmap') or kw.get('animmap')
        for k in PBR_KEYWORDS:
            if k in kw:
                info['explicit'] = k
        if any(k in kw for k in SCALAR_KEYWORDS):
            info['scalar'] = True
        if kw.get('tcgen', [''])[0] in ('environment',) or kw.get('alphagen', [''])[0] == 'lightingspecular':
            info['shiny'] = True
        if info['diffuse'] is None and m and m[0] not in ('$lightmap', '$whiteimage') \
                and 'glow' not in kw and not m[0].startswith('*'):
            info['diffuse'] = strip_ext(m[-1] if kw.get('animmap') else m[0])
    return info


# --------------------------------------------------------------------------
# skin scattering eligibility (port of R_SkinSSSClassifyShader, tr_skinsss.cpp)
# --------------------------------------------------------------------------

SKIN_EXCLUDED_TOKENS = {'eyes', 'eye', 'eyesmouth', 'moutheyes', 'teeth', 'mouth', 'cap', 'caps'}


def skin_layering(stages):
    """'blended' when the lit base stage blends, 'layered' when a later lit stage
    is alpha blended on top of it (jedi_tf), else ''"""
    lit = []
    for st in stages:
        kw = {}
        for l in st:
            if l:
                kw.setdefault(l[0], l[1:])
        if (kw.get('rgbgen') or [''])[0] in ('lightingdiffuse', 'lightingdiffuseentity') and 'glow' not in kw:
            lit.append(kw)
    if not lit:
        return ''
    blend = lambda kw: tuple(kw.get('blendfunc') or [])
    b = blend(lit[0])
    if b and b not in (('gl_one', 'gl_zero'),):
        return 'blended'
    for kw in lit[1:]:
        if blend(kw) in (('blend',), ('gl_src_alpha', 'gl_one_minus_src_alpha')):
            return 'layered'
    return ''


def skin_eligibility(cls, reason, info, mixed):
    """(scatter, reason) as R_SkinSSSClassifyShader decides it"""
    if info and info.get('skinlayer') == 'blended':
        return 0.0, 'blended'
    if cls != 'skin':
        return 0.0, 'not skin'
    token = reason.split(':', 1)[1] if ':' in reason else None
    if token in SKIN_EXCLUDED_TOKENS:
        return 0.0, 'excluded part'
    if token == 'head' and not mixed:
        return 0.0, 'mixed head'
    if info and info.get('skinlayer') == 'layered':
        return 0.0, 'layered'
    return 1.0, 'skin'


def lightall_status(stages):
    """
    Port of CollapseStagesToGLSL's skip test plus ConvertLegacyShinyStages
    (tr_shader.cpp), assuming r_cubeMapping 1 and r_detailTextures 1.
    Returns (status, detail): 'lightall', 'converted' (+ 'env dropped'),
    'skip' (+ reason) or 'unlit'.
    """
    parsed = []
    for st in stages:
        kw = {}
        for l in st:
            if l:
                kw.setdefault(l[0], l[1:])
        parsed.append(kw)

    def blend(kw):
        b = kw.get('blendfunc')
        if not b:
            return None
        if b[0] == 'add':
            return ('gl_one', 'gl_one')
        if b[0] == 'filter':
            return ('gl_dst_color', 'gl_zero')
        if b[0] == 'blend':
            return ('gl_src_alpha', 'gl_one_minus_src_alpha')
        return tuple(b[:2])

    tcgen = lambda kw: (kw.get('tcgen') or ['texture'])[0]
    has_lightmap = any((kw.get('map') or [''])[0] == '$lightmap' for kw in parsed)
    lit = has_lightmap or any((kw.get('rgbgen') or [''])[0] in ('lightingdiffuse', 'lightingdiffuseentity') for kw in parsed)
    if not lit:
        return ('unlit', '')

    active = list(parsed)
    diffuse = None
    converted = False
    env = False
    for kw in parsed:
        spec = (kw.get('alphagen') or [''])[0] == 'lightingspecular'
        if diffuse is None:
            model_lit = (kw.get('rgbgen') or [''])[0] in ('lightingdiffuse', 'lightingdiffuseentity')
            m = (kw.get('map') or kw.get('clampmap') or kw.get('animmap') or [''])[0]
            if 'glow' not in kw and not spec and tcgen(kw) in ('texture', 'base') and m != '$lightmap' \
                    and (model_lit or has_lightmap):
                diffuse = kw
            continue
        if spec:
            if blend(kw) == ('gl_src_alpha', 'gl_one') and tcgen(kw) in ('texture', 'base') and not converted:
                converted = True
                active.remove(kw)
            continue
        diffuse_model_lit = (diffuse.get('rgbgen') or [''])[0] in ('lightingdiffuse', 'lightingdiffuseentity')
        if tcgen(kw) == 'environment' and diffuse_model_lit and 'glow' not in kw and \
                blend(kw) in (('gl_dst_color', 'gl_src_color'), ('gl_one', 'gl_one')):
            env = True
            active.remove(kw)

    for kw in active:
        a = (kw.get('alphagen') or [''])[0]
        if a == 'lightingspecular':
            return ('skip', 'alphaGen lightingSpecular')
        if a == 'portal':
            return ('skip', 'alphaGen portal')
        if tcgen(kw) not in ('texture', 'base', 'lightmap', 'environment', 'vector'):
            return ('skip', 'tcGen ' + tcgen(kw))
    if converted:
        return ('converted', 'env dropped' if env else '')
    return ('lightall', 'env dropped' if env else '')


def material_source(idx, shader, diffuse):
    info = idx.shaders.get(shader)
    if info and info['explicit']:
        return 'authored:' + info['explicit']
    if diffuse:
        for suf in AUTHORED_SUFFIXES:
            if strip_ext(diffuse) + suf in idx.images:
                return 'authored:' + suf[1:]
    if info and info['scalar']:
        return 'authored:scalar'
    return 'legacy'


def main():
    args = sys.argv[1:]
    if '--selftest' in args:
        sys.exit(1 if selftest() else 0)
    if not args:
        print(__doc__)
        sys.exit(1)
    base = args[0]
    list_class = args[args.index('--list') + 1] if '--list' in args else None
    idx = Index(base)

    # materials: every shader referenced by a skin plus every shader / implicit texture under models/ and textures/
    mats = {}
    for skin, surf, sh in idx.skins:
        mats.setdefault(sh, set()).add(surf)
    for name in idx.shaders:
        if name.startswith(('models/', 'textures/')):
            mats.setdefault(name, set())
    for img in idx.images:
        if img.startswith(('models/players/', 'models/weapons2/', 'models/map_objects/')) \
                and not re.search(r'_(n|nh|rmo|orm|orms|rmos|specgloss|glow|g|s|spec|e|emissive)$', img):
            mats.setdefault(img, set())

    rows = []
    for name, surfs in mats.items():
        info = idx.shaders.get(name)
        diffuse = info['diffuse'] if info else (name if idx.has_image(name) else None)
        if not diffuse:
            continue
        src = material_source(idx, name, diffuse)
        cls, reason = classify(name, diffuse)
        normal = 'nh' if diffuse + '_nh' in idx.images else ('n' if diffuse + '_n' in idx.images else '-')
        area = '/'.join(name.split('/')[:2])
        rows.append(dict(name=name, diffuse=diffuse, src=src, cls=cls, reason=reason, normal=normal,
                         area=area, surfs=sorted(surfs), shiny=bool(info and info['shiny'])))

    if '--skin' in args:
        mixed = '--mixed' in args
        by_reason = collections.defaultdict(list)
        for r in rows:
            if r['cls'] != 'skin' or not r['name'].startswith('models/'):
                continue
            scatter, why = skin_eligibility(r['cls'], r['reason'], idx.shaders.get(r['name']), mixed)
            by_reason[why].append(r)
        print('# skin scattering eligibility (r_skinSSSMixedHeads %d)\n' % mixed)
        for why in ('skin', 'excluded part', 'mixed head', 'layered', 'blended'):
            rs = by_reason.get(why, [])
            used = [r for r in rs if r['surfs']]
            print('## %s: %d materials (%d used by a model skin)\n' % (why, len(rs), len(used)))
            for r in sorted(used, key=lambda r: r['name']):
                print('- %s (%s; %s)' % (r['name'], r['reason'], ', '.join(r['surfs'][:4])))
            print()
        # characters (skin files) without any scattering surface
        chars = collections.defaultdict(set)
        for skin, surf, sh in idx.skins:
            chars[skin.split('/')[2]].add(sh)
        scatter_mats = {r['name'] for r in by_reason.get('skin', [])}
        none = sorted(c for c, shs in chars.items() if not (shs & scatter_mats))
        print('## characters without a scattering surface: %d of %d\n' % (len(none), len(chars)))
        print(', '.join(none))
        return

    if '--pairs' in args:
        for r in sorted(rows, key=lambda r: r['name']):
            print('%s	%s	%s	%s' % (r['name'], r['diffuse'], r['cls'], r['reason']))
        return

    if list_class:
        for r in sorted(rows, key=lambda r: r['name']):
            if r['src'] == 'legacy' and r['cls'] == list_class:
                print('%-60s %-14s %s' % (r['name'], r['reason'], ','.join(r['surfs'][:4])))
        return

    print('# rend2 auto PBR material inventory\n')
    print('PK3s (search order): ' + ', '.join(idx.pk3s) + '\n')
    print('%d shaders parsed, %d images, %d skin surface entries, %d materials with a diffuse\n'
          % (len(idx.shaders), len(idx.images), len(idx.skins), len(rows)))

    print('## PBR coverage by area\n')
    print('| area | materials | authored | legacy | with _n/_nh |')
    print('|---|---|---|---|---|')
    by_area = collections.defaultdict(list)
    for r in rows:
        by_area[r['area']].append(r)
    for area, rs in sorted(by_area.items(), key=lambda kv: -len(kv[1]))[:14]:
        a = sum(r['src'] != 'legacy' for r in rs)
        print('| %s | %d | %d | %d | %d |' % (area, len(rs), a, len(rs) - a, sum(r['normal'] != '-' for r in rs)))

    print('\n## Authored sources\n')
    for k, v in collections.Counter(r['src'] for r in rows).most_common():
        print('- %s: %d' % (k, v))

    legacy = [r for r in rows if r['src'] == 'legacy']
    print('\n## Heuristic class distribution (legacy materials only, r_autoPBR 2)\n')
    print('| class | all | models/players | models/weapons2 | models/map_objects | textures |')
    print('|---|---|---|---|---|---|')
    for c in CLASSES:
        rs = [r for r in legacy if r['cls'] == c]
        cnt = lambda pre: sum(r['name'].startswith(pre) for r in rs)
        print('| %s | %d | %d | %d | %d | %d |' % (c, len(rs), cnt('models/players'), cnt('models/weapons2'),
                                                  cnt('models/map_objects'), cnt('textures/')))

    print('\n## Deciding rule (legacy materials)\n')
    rule = collections.Counter(r['reason'].split(':')[0] for r in legacy)
    print(', '.join('%s %d' % kv for kv in rule.most_common()))
    print('\nTop deciding tokens: ' + ', '.join('%s %d' % kv for kv in
          collections.Counter(r['reason'] for r in legacy if r['reason'] != 'no token').most_common(40)))

    print('\n## Most common tokens in legacy model material names\n')
    tok = collections.Counter()
    for r in legacy:
        if r['name'].startswith('models/'):
            tok.update(set(tokenize(r['name'].rsplit('/', 1)[-1])))
    print(', '.join('%s %d' % kv for kv in tok.most_common(80)))

    print('\n## Skin surfaces -> class (legacy, models/players)\n')
    sc = collections.defaultdict(collections.Counter)
    for r in legacy:
        for s in r['surfs']:
            s = re.sub(r'^(l_|r_)', '', s)
            if '_cap_' in s:
                continue
            sc[s][r['cls']] += 1
    for s, c in sorted(sc.items(), key=lambda kv: -sum(kv[1].values()))[:24]:
        print('- %s: %s' % (s, ', '.join('%s %d' % kv for kv in c.most_common())))

    print('\n## Lightall conversion of lit shaders (r_autoPBRConvert 1)\n')
    print('| area | lightall today | converted (spec mask) | +env dropped | still vertex lit |')
    print('|---|---|---|---|---|')
    conv = collections.defaultdict(collections.Counter)
    still = collections.Counter()
    for name, info in idx.shaders.items():
        st, detail = info['convert']
        if st == 'unlit':
            continue
        area = '/'.join(name.split('/')[:2])
        c = conv[area]
        c['converted' if st == 'converted' else ('skip' if st == 'skip' else 'lightall')] += 1
        if st == 'converted' and detail:
            c['env'] += 1
        if st == 'skip':
            still[detail] += 1
    tot = collections.Counter()
    for area, c in sorted(conv.items(), key=lambda kv: -sum(kv[1].values())):
        tot.update(c)
        if area.startswith('models/') or sum(c.values()) > 60:
            print('| %s | %d | %d | %d | %d |' % (area, c['lightall'], c['converted'], c['env'], c['skip']))
    print('| **all** | %d | %d | %d | %d |' % (tot['lightall'], tot['converted'], tot['env'], tot['skip']))
    print('\nStill vertex lit, by reason: ' + ', '.join('%s %d' % kv for kv in still.most_common()))
    left = sorted(n for n, i in idx.shaders.items() if i['convert'] == ('skip', 'alphaGen lightingSpecular'))
    print('\nlightingSpecular shaders the pattern does not match: ' + ', '.join(left[:40]))

    print('\n## Legacy shaders with env-map / lightingSpecular stages (legacy "shiny" hint)\n')
    sh = collections.Counter(r['cls'] for r in legacy if r['shiny'])
    print(', '.join('%s %d' % kv for kv in sh.most_common()) or 'none')

    print('\n## Misclassification candidates\n')
    print('Organic player parts whose skin surface disagrees with the class:')
    for r in sorted(legacy, key=lambda r: r['name']):
        surfs = ' '.join(r['surfs'])
        if r['cls'] == 'skin' and re.search(r'\btorso\b|\bhips\b|leg', surfs) and not re.search(r'head|hand|face', surfs):
            print('- %s (%s, surfaces: %s)' % (r['name'], r['reason'], ', '.join(r['surfs'][:3])))
        if r['cls'] == 'cloth' and re.search(r'head|face|hand', surfs) and not re.search(r'torso|hips|leg', surfs):
            print('- %s (%s, surfaces: %s)' % (r['name'], r['reason'], ', '.join(r['surfs'][:3])))


if __name__ == '__main__':
    main()
