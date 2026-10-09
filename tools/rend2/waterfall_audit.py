#!/usr/bin/env python3
"""Audit installed PK3 BSP waterfall sheets and waterfall-named EFX runners.

Discovery may use names, but the report records evidence and never turns that
search into a runtime classifier. Original archives are opened read-only.
"""
import json
import math
import re
import struct
import sys
from pathlib import Path

from water_audit import Pk3Files, load_shader_definitions, scan_bsp, lump, MST_PATCH, MST_PLANAR, MST_TRISOUP


TYPE_NAMES = {MST_PLANAR: 'planar', MST_PATCH: 'patch', MST_TRISOUP: 'triangle soup'}


def entities(data):
    text = lump(data, 0).rstrip(b'\0').decode('cp1252', errors='replace')
    result = []
    for block in re.findall(r'\{([^}]*)\}', text, re.S):
        pairs = re.findall(r'"([^"]*)"\s+"([^"]*)"', block)
        result.append({k.lower(): v for k, v in pairs})
    return result


def vec(text):
    try:
        values = [float(v) for v in text.split()]
        return values if len(values) == 3 else None
    except ValueError:
        return None


def point_bounds_distance(p, bounds):
    return math.sqrt(sum((p[i] - min(max(p[i], bounds[0][i]), bounds[1][i])) ** 2 for i in range(3)))


def fall_geometry(surface):
    n = surface['normal']
    gravity = [0.0, 0.0, -1.0]
    dot = sum(gravity[i] * n[i] for i in range(3))
    fall = [gravity[i] - dot * n[i] for i in range(3)]
    length = math.sqrt(sum(v*v for v in fall)) or 1.0
    fall = [v / length for v in fall]
    corners = [[surface['bounds'][(c >> i) & 1][i] for i in range(3)] for c in range(8)]
    along = [sum(p[i] * fall[i] for i in range(3)) for p in corners]
    top = min(range(8), key=lambda i: along[i])
    bottom = max(range(8), key=lambda i: along[i])
    return fall, corners[top], corners[bottom]


def main(argv):
    base = argv[0] if argv else r'C:\Users\Keyten\Desktop\projects\OpenJK\build-rend2\base'
    output = Path(argv[1] if len(argv) > 1 else 'docs/rend2-waterfall-stock-audit.json')
    files = Pk3Files(base)
    defs, _ = load_shader_definitions(files)
    maps = {}
    decorative = []
    rejected = []
    for key in sorted(k for k in files.files if k.startswith('maps/') and k.endswith('.bsp')):
        data = files.read(key)
        liquids, surfaces = scan_bsp(data, defs, all_surfaces=True)
        map_name = key[5:-4]
        fx = []
        for index, entity in enumerate(entities(data)):
            effect = entity.get('fxfile', '').lower().replace('\\', '/')
            if 'waterfall' not in effect and 'water_fall' not in effect and 'wfall' not in effect:
                continue
            origin = vec(entity.get('origin', ''))
            fx.append({'entity_id': index, 'effect': effect, 'origin': origin,
                       'target': entity.get('target'), 'angle': entity.get('angle')})
        found = []
        for surface in surfaces:
            info = defs.get(surface['shader'], ({'surfaceparms': [], 'stages': [], 'deforms': [],
                                                 'maps': [], 'blends': [], 'tcmods': []}, None, None))[0]
            steep = surface['orient'] in ('vertical', 'sloped')
            exact = surface['shader'] == 'textures/h_evil/wfall'
            named = any(word in surface['shader'] for word in ('waterfall', 'wfall', '/falls'))
            semantic_sheet = steep and 'water' in info['surfaceparms'] and surface['link'] is None
            if not (exact or named or semantic_sheet):
                continue
            if surface['link'] is not None:
                rejected.append({'map': map_name, 'surface_id': surface['index'], 'shader': surface['shader'],
                                 'reason': 'liquid brush side; not a waterfall without explicit metadata'})
                continue
            if not steep:
                rejected.append({'map': map_name, 'surface_id': surface['index'], 'shader': surface['shader'],
                                 'reason': 'not steep; horizontal water remains in the lake/pool path'})
                continue
            fall, top, bottom = fall_geometry(surface)
            related_fx = []
            for emitter in fx:
                if emitter['origin'] is None:
                    continue
                distance = point_bounds_distance(emitter['origin'], surface['bounds'])
                if distance <= 768.0:
                    related_fx.append(dict(emitter, distance_to_bounds=distance))
            width_height = sorted((surface['bounds'][1][i] - surface['bounds'][0][i] for i in range(3)), reverse=True)
            found.append({
                'surface_id': surface['index'], 'shader': surface['shader'],
                'geometry_type': TYPE_NAMES.get(surface['type'], str(surface['type'])),
                'patch_dimensions': surface['patch_dimensions'], 'orientation': surface['orient'],
                'normal': surface['normal'], 'bounds': surface['bounds'],
                'approximate_width': width_height[1], 'approximate_height': width_height[0],
                'area': surface['area'], 'vertices': surface['vertex_count'],
                'triangles_or_patch_quads': surface['triangle_count'],
                'vertices_per_1000_area': 1000.0 * surface['vertex_count'] / max(surface['area'], 1e-6),
                'liquid_brush_relation': None if surface['link'] is None else {
                    'brush_id': surface['link'], 'class': surface['link_cls'], 'side': surface.get('link_side')},
                'contents_flags': surface['contents'], 'surface_flags': surface['flags'],
                'shader_source': defs[surface['shader']][1] if surface['shader'] in defs else None,
                'shader_pk3': defs[surface['shader']][2] if surface['shader'] in defs else None,
                'surfaceparms': info['surfaceparms'], 'stages': info['stages'],
                'textures': info['maps'], 'blend_functions': info['blends'],
                'tcmods': info['tcmods'], 'deform_vertexes': info['deforms'],
                'fall_direction_world': fall, 'approximate_top_edge_point': top,
                'approximate_bottom_impact_point': bottom, 'associated_efx': related_fx,
                'classification': 'water sheet',
                'classification_evidence': ('exact audited stock mapping' if exact else
                                            'audit candidate only; requires explicit metadata')
            })
        if found or fx:
            maps[map_name] = {'surfaces': found, 'waterfall_named_efx': fx}
        for emitter in fx:
            if not found:
                decorative.append({'map': map_name, **emitter,
                                   'classification': 'decorative/particle effect; no audited waterfall sheet'})
    report = {
        'base': base, 'pk3s': files.pk3s,
        'method': 'all installed shader packs and BSP draw surfaces; names used for discovery only',
        'runtime_exact_mapping': ['textures/h_evil/wfall'],
        'maps': maps, 'decorative_effect_only': decorative,
        'rejected_vertical_liquid_sides': rejected,
    }
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    surfaces = sum(len(m['surfaces']) for m in maps.values())
    print(f'{output}: {surfaces} waterfall surfaces in {sum(bool(m["surfaces"]) for m in maps.values())} maps; '
          f'{len(decorative)} effect-only runners')


if __name__ == '__main__':
    main(sys.argv[1:])
