#!/usr/bin/env python3
"""Read-only EFX/SHADER/BSP inventory. Output is research, not runtime rules.

Archives are supplied from lowest to highest priority, matching a single base
directory's PK3 ordering. Loose files, fs_game, homepath and pure-server ordering
must be represented separately; this tool does not claim to emulate all of FS.
No commercial assets are extracted. Pillow is optional (--textures).
"""
import argparse
import collections
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import zipfile


PRIMITIVES = set('particle line tail sound cylinder electricity emitter decal '
                 'orientedparticle fxrunner light camerashake flash'.split())
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"[^"\n]*"|\n|[{}\[\]]|[^\s{}\[\]"]+')


def canonical(value):
    return value.replace('\\', '/').lower()


def effect_path(value):
    value = canonical(value.strip())
    if not value.startswith('effects/'):
        value = 'effects/' + value
    if not value.endswith('.efx'):
        value += '.efx'
    return value


def shader_path(value):
    return re.sub(r'\.(tga|jpg|jpeg|png|dds)$', '', canonical(value))


def tokens(text):
    for match in TOKEN.finditer(text.replace('\r', '')):
        item = match.group()
        if item.startswith('//'):
            continue
        if item.startswith('/*'):
            yield from ['\n'] * item.count('\n')
            continue
        yield item[1:-1] if item.startswith('"') else item


def parse(text, shader_mode=False):
    """Line-valued keys, nested groups, lists and anonymous shader stages.

    Preserve duplicate keys and groups. This is an offline syntax reader, not
    Raven's runtime parser; semantic defaults and flag behavior stay separate.
    """
    ts = list(tokens(text))
    pos = 0

    def skip_lines():
        nonlocal pos
        while pos < len(ts) and ts[pos] == '\n':
            pos += 1

    def group(end=None):
        nonlocal pos
        out = []
        while True:
            skip_lines()
            if pos == len(ts):
                if end:
                    raise ValueError('unclosed ' + end)
                return out
            if ts[pos] == end:
                pos += 1
                return out
            key = ts[pos]
            pos += 1
            if key == '{':
                out.append({'key': '', 'group': group('}')})
                continue
            if key in ('}', ']', '['):
                raise ValueError('unexpected delimiter ' + key)
            lookahead = pos
            while lookahead < len(ts) and ts[lookahead] == '\n':
                lookahead += 1
            # Groups/lists may start on the next line. Empty shader directives
            # must not swallow the following scalar directive.
            if (lookahead < len(ts) and ts[lookahead] in ('{', '[') and
                    not (shader_mode and end is not None and pos < len(ts) and ts[pos] == '\n')):
                pos = lookahead
            if pos < len(ts) and ts[pos] == '{':
                pos += 1
                out.append({'key': key, 'group': group('}')})
            elif pos < len(ts) and ts[pos] == '[':
                pos += 1
                values = []
                while pos < len(ts) and ts[pos] != ']':
                    if ts[pos] != '\n':
                        values.append(ts[pos])
                    pos += 1
                if pos == len(ts):
                    raise ValueError('unclosed list')
                pos += 1
                out.append({'key': key, 'values': values})
            else:
                values = []
                while pos < len(ts) and ts[pos] not in ('\n', '}', ']'):
                    values.append(ts[pos])
                    pos += 1
                out.append({'key': key, 'values': values})
        return out

    return group()


def fields(nodes):
    out = {}
    for n in nodes:
        if 'values' in n:
            out[canonical(n['key'])] = n['values']
    return out


def numbers(values):
    try:
        return [float(v) for v in values]
    except ValueError:
        return []


class Corpus:
    def __init__(self, paths):
        self.archives = [zipfile.ZipFile(p) for p in paths]
        self.paths = paths
        self.index = {}
        self.versions = collections.defaultdict(list)
        for z, path in zip(self.archives, paths):
            for info in z.infolist():
                if not info.is_dir():
                    name = canonical(info.filename)
                    self.index[name] = (z, info)
                    self.versions[name].append({'archive': path.name,
                                               'crc32': '%08x' % info.CRC,
                                               'bytes': info.file_size})

    def read(self, name):
        z, info = self.index[name]
        return z.read(info)

    def source(self, name):
        return self.versions[name][-1]

    def close(self):
        for z in self.archives:
            z.close()


def shader_inventory(corpus, errors):
    definitions = collections.defaultdict(list)
    for name in sorted(corpus.index):
        if not name.startswith('shaders/') or not name.endswith('.shader'):
            continue
        try:
            roots = parse(corpus.read(name).decode('cp1252'), shader_mode=True)
        except ValueError as exc:
            errors.append({'file': name, 'error': str(exc)})
            continue
        for node in roots:
            if 'group' not in node:
                continue
            stages = []
            for stage in node['group']:
                if stage['key'] or 'group' not in stage:
                    continue
                f = fields(stage['group'])
                maps = []
                for key in ('map', 'clampmap', 'animmap', 'oneshotanimmap'):
                    maps += f.get(key, [])[1:] if 'animmap' in key else f.get(key, [])
                blend = [canonical(v) for v in f.get('blendfunc', [])]
                if blend == ['add']:
                    blend = ['gl_one', 'gl_one']
                elif blend == ['blend']:
                    blend = ['gl_src_alpha', 'gl_one_minus_src_alpha']
                elif blend == ['filter']:
                    blend = ['gl_dst_color', 'gl_zero']
                stages.append({'blend': blend, 'maps': [canonical(v) for v in maps],
                               'glow': 'glow' in f, 'rgbgen': f.get('rgbgen', []),
                               'alphagen': f.get('alphagen', []), 'alphafunc': f.get('alphafunc', [])})
            definitions[shader_path(node['key'])].append({
                'file': name, 'source': corpus.source(name),
                'particle_lighting': fields(node['group']).get('particlelighting', []),
                'stages': stages})
    return definitions


def classify(effect, p):
    """Conservative research candidates; confidence is deliberately not a probability."""
    if p['type'] not in ('particle', 'orientedparticle'):
        return {'material': 'light' if p['type'] == 'light' else 'non_medium',
                'tier': 'structural', 'reason': ['primitive type'], 'representation': 'none'}
    shaders = p['shaders']
    per_shader = []
    reason = []
    # Exact shader vocabulary. Never use a Dust name as a positive medium rule.
    for s in shaders:
        if s in ('gfx/misc/black_smoke', 'gfx/misc/black_smoke2'):
            role = 'dark_smoke'
        elif s in ('gfx/effects/alpha_smoke', 'gfx/effects/alpha_smoke2'):
            role = 'smoke_or_dust'
        elif s == 'gfx/misc/steam':
            role = 'smoke_steam_or_dust'
        elif s == 'gfx/effects/wcloud' and effect == 'effects/noghri_stick/gas_cloud.efx':
            role = 'gas'
        elif s in ('gfx/effects/fire', 'gfx/effects/fire2', 'gfx/effects/fire3', 'gfx/effects/fire4'):
            role = 'flame'
        elif s.startswith('gfx/exp/'):
            role = 'fireball_candidate'
        elif any(t in s for t in ('flare', 'spark', 'flash')) or s in ('gfx/misc/dotfill_a', 'gfx/misc/dust'):
            role = 'detail_no_medium'
        else:
            role = 'unclassified'
        per_shader.append(role)
    roles = set(per_shader)
    if len(roles) == 1 and 'unclassified' not in roles:
        role = per_shader[0]
        reason.append('all shader alternatives share audited vocabulary: ' + role)
        tier = 'family_candidate'
        if role == 'smoke_steam_or_dust':
            context = effect + ' ' + p['name'].lower()
            if 'orangeglow' in p['name'].lower():
                role = 'fire_glow_candidate'
                reason.append('steam texture is reused as an orange explosion glow')
            elif 'smoke' in context or effect == 'effects/env/fire.efx':
                role = 'light_smoke'
            elif 'dust' in context:
                role = 'dust'
            elif 'steam' in context or 'mist' in context:
                role = 'steam_or_mist'
            else:
                tier = 'review'
        elif role == 'smoke_or_dust':
            role = 'dust' if 'dust' in effect + p['name'].lower() else 'smoke'
        if role == 'detail_no_medium':
            tier = 'negative_candidate'
            if 'gfx/misc/dust' in shaders:
                reason.append('texture inspection: sparse streaks, not a continuous dust cloud')
    else:
        role = 'mixed_shader_roles' if len(roles) > 1 else 'unclassified'
        tier = 'review'
        reason.append('shader alternatives: ' + ', '.join(sorted(roles)))
    flags = ' '.join(p['fields'].get('flags', []) + p['fields'].get('flag', [])).lower()
    if 'depthhack' in flags or 'playerview' in flags:
        reason.append('view-model/2D exclusion')
        representation = 'none'
    elif role in ('dark_smoke', 'light_smoke', 'smoke', 'dust', 'steam_or_mist', 'gas') and tier != 'review':
        representation = 'particle_candidate'
    else:
        representation = 'none'
    if 'volumetricmedia' in p['groups']:
        reason.append('explicit volumetricMedia takes precedence')
        representation = 'explicit_particle'
    return {'material': role, 'tier': tier, 'reason': reason, 'representation': representation}


def effects_inventory(corpus, errors):
    effects = []
    for name in sorted(corpus.index):
        if not name.startswith('effects/') or not name.endswith('.efx'):
            continue
        try:
            roots = parse(corpus.read(name).decode('cp1252'))
        except ValueError as exc:
            errors.append({'file': name, 'error': str(exc)})
            continue
        effect = {'file': name, 'source': corpus.source(name),
                  'sha256': hashlib.sha256(corpus.read(name)).hexdigest(),
                  'fields': fields(roots), 'primitives': []}
        for ordinal, node in enumerate(n for n in roots if 'group' in n):
            kind = canonical(node['key'])
            f = fields(node['group'])
            groups = {canonical(n['key']): fields(n['group']) for n in node['group'] if 'group' in n}
            refs = {k: [effect_path(v) for v in f[k]] for k in ('playfx', 'emitfx', 'deathfx', 'impactfx') if k in f}
            p = {'ordinal': ordinal, 'type': kind, 'name': ' '.join(f.get('name', [])),
                 'fields': f, 'groups': groups,
                 'shaders': [shader_path(v) for v in f.get('shaders', f.get('shader', []))],
                 'raw_shader_names': f.get('shaders', f.get('shader', [])),
                 'engine_supported_type': kind in PRIMITIVES,
                 'child_effects': refs}
            # Defaults are from CPrimitiveTemplate, not inferred from missing fields.
            p['numeric'] = {'life_ms': numbers(f.get('life', ['50'])),
                            'count_per_call': numbers(f.get('count', ['1'])),
                            'delay_ms': numbers(f.get('delay', ['0'])),
                            'size_start': numbers(groups.get('size', groups.get('width', {})).get('start', ['1'])),
                            'size_end': numbers(groups.get('size', groups.get('width', {})).get('end', ['1']))}
            p['candidate'] = classify(name, p)
            effect['primitives'].append(p)
        effects.append(effect)
    return effects


def map_inventory(corpus, errors):
    usage = []
    maps = []
    for name in sorted(corpus.index):
        if not name.startswith('maps/') or not name.endswith('.bsp'):
            continue
        try:
            z, info = corpus.index[name]
            with z.open(info) as stream:
                header = stream.read(16)
                magic, version, offset, length = struct.unpack('<4siii', header)
                if magic not in (b'RBSP', b'IBSP') or offset < 16 or length < 0 or offset + length > info.file_size:
                    raise ValueError('invalid BSP entity lump')
                stream.seek(offset)
                entity_text = stream.read(length).decode('cp1252').rstrip('\x00')
            maps.append({'file': name, 'source': corpus.source(name), 'magic': magic.decode(), 'version': version})
            for index, body in enumerate(re.findall(r'\{([^{}]*)\}', entity_text)):
                entity = {canonical(k): v for k, v in re.findall(r'"([^"\n]*)"\s*"([^"\n]*)"', body)}
                if canonical(entity.get('classname', '')) != 'fx_runner':
                    continue
                fx = entity.get('fxfile', '')
                if fx:
                    usage.append({'map': name, 'entity_index': index, 'effect': effect_path(fx),
                                  'entity': entity})
        except (ValueError, struct.error, UnicodeError) as exc:
            errors.append({'file': name, 'error': str(exc)})
    return maps, usage


def source_refs(source_root, names):
    hits = collections.defaultdict(list)
    effect_names = set(names)
    for root in ('code', 'codemp'):
        for path in sorted((source_root / root).rglob('*')):
            if path.suffix.lower() not in ('.cpp', '.c', '.h'):
                continue
            text = path.read_text(encoding='utf-8', errors='replace')
            for number, line in enumerate(text.splitlines(), 1):
                for literal in re.findall(r'"([^"\n]+)"', line):
                    candidate = effect_path(literal)
                    if candidate in effect_names:
                        hits[candidate].append({'file': str(path.relative_to(source_root)), 'line': number})
    return hits


def texture_metrics(corpus, shaders, referenced):
    from PIL import Image, ImageFilter, ImageStat
    out = {}
    for shader in sorted(referenced):
        for definition in shaders.get(shader, []):
            for stage in definition['stages']:
                for image in stage['maps']:
                    if image.startswith('$') or image in out:
                        continue
                    # Raven shader scripts often request .tga while the PK3
                    # stores .jpg; R_FindImageFile retries alternate extensions.
                    base = shader_path(image)
                    candidates = [image] + [base + ext for ext in ('.tga', '.jpg', '.png', '.dds')]
                    resolved = next((n for n in candidates if n in corpus.index), None)
                    if not resolved:
                        out[image] = {'status': 'missing'}
                        continue
                    try:
                        with Image.open(io.BytesIO(corpus.read(resolved))) as im:
                            size = im.size
                            has_alpha = 'A' in im.getbands() or 'transparency' in im.info
                            rgba = im.convert('RGBA')
                            rgba.thumbnail((96, 96))
                            rgb = rgba.convert('RGB')
                            alpha = rgba.getchannel('A')
                            lum = rgb.convert('L')
                            pixels = list(rgba.getdata())
                            mean_alpha = sum(v[3] for v in pixels) / (255 * len(pixels))
                            coverage = sum(v[3] > 25 for v in pixels) / len(pixels)
                            # Effective mask: alpha for alpha blends, luminance for additive.
                            use_alpha = 'gl_one_minus_src_alpha' in stage['blend'] and has_alpha
                            mask = alpha if use_alpha else lum
                            w, h = mask.size
                            weighted = center = edge = edge_count = 0.0
                            for y in range(h):
                                for x in range(w):
                                    value = mask.getpixel((x, y)) / 255.0
                                    r = math.hypot((x + .5 - w / 2) / (w / 2), (y + .5 - h / 2) / (h / 2))
                                    weighted += value
                                    if r < .25:
                                        center += value
                                    if r > .85:
                                        edge += value
                                        edge_count += 1
                            blur = mask.filter(ImageFilter.GaussianBlur(1.5))
                            high_frequency = sum(abs(a-b) for a, b in zip(mask.getdata(), blur.getdata())) / (255 * len(pixels))
                            out[image] = {'status': 'ok', 'resolved': resolved, 'source': corpus.source(resolved),
                                          'dimensions': size, 'has_alpha': has_alpha, 'mean_alpha': mean_alpha,
                                          'alpha_coverage_gt_0_1': coverage,
                                          'mean_rgb': [v / 255 for v in ImageStat.Stat(rgb).mean],
                                          'mean_luminance': ImageStat.Stat(lum).mean[0] / 255,
                                          'effective_mask': 'alpha' if use_alpha else 'luminance',
                                          'radial_core_fraction': center / max(weighted, 1e-9),
                                          'outer_mask_mean': edge / max(edge_count, 1),
                                          'high_frequency_residual': high_frequency}
                    except (OSError, ValueError) as exc:
                        out[image] = {'status': 'decode_error', 'resolved': resolved, 'error': str(exc)}
    return out


def report(data):
    s = data['summary']
    lines = ['# EFX corpus audit', '', 'Research candidates, not validated runtime classifications.', '',
             'Archive order: ' + ', '.join(Path(p).name for p in data['archives_low_to_high']), '',
             '```json', json.dumps(s, indent=2), '```', '', '## Map fx_runner usage', '',
             '| Effect | Instances | Maps |', '|---|---:|---:|']
    usage = collections.defaultdict(list)
    for u in data['map_usage']:
        usage[u['effect']].append(u)
    for fx, us in sorted(usage.items(), key=lambda item: (-len(item[1]), item[0])):
        lines.append('| `%s` | %d | %d |' % (fx, len(us), len(set(u['map'] for u in us))))
    lines += ['', '## Primitive inventory', '']
    for e in data['effects']:
        lines += ['### ' + e['file'], '', 'Source: ' + e['source']['archive'], '',
                  '| # / Name | Type | Shaders | Material candidate | Proxy candidate |', '|---|---|---|---|---|']
        for p in e['primitives']:
            c = p['candidate']
            lines.append('| %d / %s | %s | %s | %s | %s |' %
                         (p['ordinal'], p['name'].replace('|', '/'), p['type'], ', '.join(p['shaders']), c['material'], c['representation']))
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archives', nargs='+', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--source-root', type=Path)
    ap.add_argument('--textures', action='store_true')
    args = ap.parse_args()
    corpus = Corpus(args.archives)
    try:
        errors = []
        shaders = shader_inventory(corpus, errors)
        effects = effects_inventory(corpus, errors)
        maps, usage = map_inventory(corpus, errors)
        names = {e['file'] for e in effects}
        referenced = {s for e in effects for p in e['primitives'] for s in p['shaders']}
        refs = source_refs(args.source_root, names) if args.source_root else {}
        images = texture_metrics(corpus, shaders, referenced) if args.textures else {}
        ps = [p for e in effects for p in e['primitives']]
        child_refs = {r for p in ps for rs in p['child_effects'].values() for r in rs}
        roots = {u['effect'] for u in usage} | set(refs)
        graph = {e['file']: {r for p in e['primitives'] for rs in p['child_effects'].values() for r in rs} for e in effects}
        reachable, todo = set(), list(roots)
        while todo:
            name = todo.pop()
            if name in reachable or name not in names:
                continue
            reachable.add(name)
            todo += sorted(graph[name] - reachable)
        summary = {'effects': len(effects), 'primitives': len(ps),
                   'engine_supported_primitives': sum(p['engine_supported_type'] for p in ps),
                   'unsupported_primitive_groups': dict(collections.Counter(p['type'] for p in ps if not p['engine_supported_type'])),
                   'primitive_types': dict(sorted(collections.Counter(p['type'] for p in ps).items())),
                   'material_candidates': dict(sorted(collections.Counter(p['candidate']['material'] for p in ps).items())),
                   'representation_candidates': dict(collections.Counter(p['candidate']['representation'] for p in ps)),
                   'referenced_shader_names': len(referenced), 'missing_shader_definitions': sorted(referenced - set(shaders)),
                   'ambiguous_shader_definitions': sorted(s for s in referenced if len(shaders.get(s, [])) > 1),
                   'maps': len(maps), 'map_fx_runner_instances': len(usage),
                   'map_fx_runner_effects': len({u['effect'] for u in usage}),
                   'source_literal_effects': len(refs), 'reachable_from_detected_roots': len(reachable),
                   'unresolved_child_effects': sorted(child_refs - names),
                   'unresolved_map_effects': sorted({u['effect'] for u in usage} - names),
                   'texture_status': dict(collections.Counter(i['status'] for i in images.values())),
                   'parse_errors': len(errors)}
        data = {'schema_version': 1, 'archives_low_to_high': [str(p.resolve()) for p in args.archives],
                'limitations': ['PK3-only overlay; no loose files/homepath/fs_game/pure ordering',
                                'shader duplicates are alternatives, not a claimed engine winner',
                                'literal source refs are potential roots, not runtime call counts',
                                'map instances are placement counts; not an observed usage percentage',
                                'candidate tiers are evidence labels, not calibrated confidence',
                                'texture metrics are downsampled heuristics, not physical material parameters'],
                'summary': summary, 'errors': errors, 'effects': effects,
                'shaders': {s: shaders.get(s, []) for s in sorted(referenced)},
                'textures': images, 'maps': maps, 'map_usage': usage,
                'source_literal_refs': refs, 'reachable_effects': sorted(reachable),
                'overrides': {n: v for n, v in sorted(corpus.versions.items()) if len(v) > 1 and
                              (n.endswith('.efx') or n.endswith('.shader') or n.endswith('.bsp'))}}
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'inventory.json').write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        (args.out / 'inventory.md').write_text(report(data), encoding='utf-8')
        print(json.dumps(summary, indent=2))
    finally:
        corpus.close()


if __name__ == '__main__':
    main()
