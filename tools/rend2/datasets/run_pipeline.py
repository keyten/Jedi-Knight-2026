#!/usr/bin/env python3
"""Generate, inspect and validate self-authored offline lighting datasets, twice.

No GL, JA installation or Python dependencies. q3map2 is OPTIONAL and never
reported as executed unless supplied and all stages succeed. Answers/authoring
files must stay outside the directory mounted for a future solver.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import re
import math


def bsp_counts(path):
    data = path.read_bytes()
    if data[:8] != b'RBSP\x01\0\0\0' or len(data) < 152:
        raise RuntimeError(f'Expected JA RBSP v1: {path}')
    lumps = [struct.unpack_from('<II', data, 8 + i * 8) for i in range(18)]
    for offset, size in lumps:
        if offset > len(data) or size > len(data) - offset:
            raise RuntimeError(f'Invalid BSP lump: {path}')
    counts = dict(surfaces=lumps[13][1] // 148, vertices=lumps[10][1] // 80,
                  lightmap_pages=lumps[14][1] // (128 * 128 * 3),
                  grid_records=lumps[15][1] // 30, grid_indices=lumps[17][1] // 2)
    if not all(counts.values()) or lumps[14][1] % (128 * 128 * 3):
        raise RuntimeError(f'Empty/incomplete bake: {path}: {counts}')
    return counts


def compare_bakes(first, second):
    """Strict surface/geometry comparison; report raw grid variability, never hide it.

    Near-solid grid nudging can vary in this external compiler. Its output is
    retained verbatim; grid variability is a failed compiler repeat check, not
    an exemption from our same-input dataset determinism requirement.
    """
    ancillary = []
    grid_identical = True
    grids = []
    for data in (first, second):
        o, n = struct.unpack_from('<II', data, 8 + 15 * 8)
        j, m = struct.unpack_from('<II', data, 8 + 17 * 8)
        if n % 30 or m % 2:
            raise RuntimeError('Invalid compiled lightgrid layout')
        records = [data[t:t+30] for t in range(o, o+n, 30)]
        indices = struct.unpack_from('<' + 'H' * (m // 2), data, j)
        if any(i >= len(records) for i in indices):
            raise RuntimeError('Invalid compiled lightgrid index')
        grids.append([records[i] for i in indices])
    if len(grids[0]) != len(grids[1]):
        raise RuntimeError('Non-deterministic logical grid dimensions')
    for lump in range(18):
        o, n = struct.unpack_from('<II', first, 8 + lump * 8)
        o2, n2 = struct.unpack_from('<II', second, 8 + lump * 8)
        a, b = first[o:o+n], second[o2:o2+n2]
        if lump in (15, 17):
            grid_identical &= a == b
            continue
        if len(a) != len(b):
            raise RuntimeError(f'Non-deterministic BSP lump size: {lump}')
        differences = [i for i in range(len(a)) if a[i] != b[i]]
        if differences and (lump != 9 or any(i % 12 < 8 for i in differences)):
            raise RuntimeError(f'Non-deterministic surface lighting/geometry: lump {lump}')
        ancillary.extend(i // 12 for i in differences)
    return dict(repeated_bsp_identical=first == second,
                repeated_surface_lighting_geometry_identical=True,
                repeated_lighting_geometry_identical=grid_identical,
                repeated_lightgrid_identical=grid_identical,
                varying_logical_grid_cells=[i for i, (a, b) in enumerate(zip(*grids)) if a != b],
                varying_ancillary_brushside_surface_refs=sorted(set(ancillary)),
                bsp_bytes_modified_after_compiler=False)


def compile_recipe(root, base, mapfile, name, commands, record, q3map):
    logs = root / 'compiler-logs'
    logs.mkdir(exist_ok=True)
    record['binary_sha256'] = sha(q3map)
    record['version_output'] = run([q3map, '-help'], logs / 'version.txt')
    record['argv'] = [[str(q3map)] + command[1:] for command in commands]
    repeated = []
    payloads = []
    for iteration in range(2):
        for i, command in enumerate(record['argv']):
            text = run(command, logs / f'{name}-pass{iteration+1}-stage{i}.txt')
            if re.search(r'\*+\s*(?:ERROR|leaked)\b|Entity leaked', text, re.I):
                raise RuntimeError(f'q3map2 reported a failed or leaking bake: {name}, stage {i}')
        record['bsp_counts'] = bsp_counts(mapfile.with_suffix('.bsp'))
        repeated.append(sha(mapfile.with_suffix('.bsp')))
        payloads.append(mapfile.with_suffix('.bsp').read_bytes())
        (logs / f'{name}-pass{iteration+1}.bsp').write_bytes(payloads[-1])
    # This published compiler may vary the ancillary RBSP brush-side surfaceNum.
    # Do NOT alter/normalize its output or its source hash. Check all real lighting
    # and geometry bytes, report this ancillary-field difference explicitly.
    record.update(compare_bakes(*payloads))
    record.update(executed=True, compiled_bsp_sha256=repeated[-1], repeated_bsp_sha256=repeated)
    write_json(root / 'answers' / f'{name}.q3map-recipe.json', record)
    return record


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')


def run(argv, log=None):
    result = subprocess.run([str(x) for x in argv], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')
    if log:
        log.write_text(result.stdout, encoding='utf-8', newline='\n')
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}): {argv}\n{result.stdout[-3000:]}')
    return result.stdout


def entity(fields):
    return '{\n' + ''.join(f'"{key}" "{value}"\n' for key, value in fields.items()) + '}\n'


def brush(lo, hi, texture='white'):
    x, y, z = lo
    X, Y, Z = hi
    planes = [((x,y,Z),(X,y,Z),(X,Y,Z)), ((x,Y,z),(X,Y,z),(X,y,z)),
              ((x,y,z),(X,y,z),(X,y,Z)), ((X,Y,z),(x,Y,z),(x,Y,Z)),
              ((x,Y,z),(x,y,z),(x,y,Z)), ((X,y,z),(X,Y,z),(X,Y,Z))]
    # Classic .map PlaneFromPoints uses the opposite winding from our CPU triangles.
    return '{\n' + ''.join(' '.join('( %g %g %g )' % p for p in (plane[0], plane[2], plane[1])) +
                          f' recon_offline/{texture} 0 0 0 1 1 0 0 0\n' for plane in planes) + '}\n'


def author_map(name, answer):
    """A separate q3map2 recipe, NOT identical to our CPU numerical baker.

    q3map light values are legacy compiler units. Do not compare them with the
    CPU fixture's cd/nits numerically. Generated source entities are answer-only.
    """
    geometry = [brush((-208,-144,-16),(208,144,0)),
                brush((-208,-144,0),(-192,144,272)),
                brush((192,-144,0),(208,144,272), 'red' if name == 'bounce_colour' else 'white'),
                brush((-192,-144,0),(192,-128,272)),
                brush((-192,128,0),(192,144,272))]
    if name == 'skylight':
        geometry += [brush((-192,-128,256),(-32,128,272)),
                     brush((32,-128,256),(192,128,272)),
                     brush((-32,-128,256),(32,-32,272)),
                     brush((-32,32,256),(32,128,272)),
                     brush((-192,-128,272),(192,128,288), 'sky')]
    else:
        geometry += [brush((-192,-128,256),(192,128,272),
                           'sky' if name == 'sun_sky' else 'white')]
    if name == 'wall_leak':
        geometry += [brush((-2,-128,0),(2,128,256))]
    if name == 'door_occlusion':
        geometry += [brush((-2,-128,0),(2,-40,256)), brush((-2,40,0),(2,128,256)),
                     brush((-2,-40,160),(2,40,256))]
    if name in ('panel', 'tube'):
        w, h = (72,32) if name == 'panel' else (80,4)
        geometry += [brush((-w,-h,240),(w,h,244), name)]
    result = '{\n"classname" "worldspawn"\n"gridsize" "64 64 128"\n' + ''.join(geometry) + '}\n'
    result += entity({'classname': 'info_player_start', 'origin': '-128 -64 32', 'angle': '0'})
    # Keep the otherwise dark sealed room during BSP outside-fill; this is NOT a light.
    if name == 'wall_leak':
        result += entity({'classname': 'info_player_deathmatch', 'origin': '128 -64 32'})
    retained = []
    for i, light in enumerate(answer['lights']):
        if light['type'] not in ('point', 'spot'):
            continue  # surfaced light is expressed by material, not a point surrogate
        fields = {'classname': 'light', 'origin': ' '.join(map(str, light['position'])),
                  '_color': ' '.join(map(str, light['colour'])), 'light': '300'}
        if light['type'] == 'spot':
            fields.update(target=f'spot_target_{i}', radius='64')
            target = [p + d * 128 for p, d in zip(light['position'], light['axis_propagation'])]
            result += entity({'classname': 'info_null', 'targetname': f'spot_target_{i}',
                              'origin': ' '.join(map(str, target))})
        result += entity(fields)
        retained.append(fields)
    return result, retained


def q3_ground_truth(name, answer, entities):
    lights = []
    for source, fields in zip((s for s in answer['lights'] if s['type'] in ('point','spot')), entities):
        light = dict(type=source['type'], position_world=[float(x) for x in fields['origin'].split()],
                     rgb_parameter=[float(x) for x in fields['_color'].split()],
                     authored_intensity=float(fields['light']), intensity_unit='legacy_q3map2_light_units', style=0)
        if source['type'] == 'spot':
            light.update(axis_propagation=source['axis_propagation'], target_distance_world=128,
                         target_world=[p+d*128 for p,d in zip(source['position'],source['axis_propagation'])],
                         authored_radius_world=64, effective_radius_addition_world=16,
                         effective_half_angle_degrees=math.degrees(math.atan(80/128)))
        lights.append(light)
    surface = None
    if name in ('panel','tube'):
        w,h = (72,32) if name=='panel' else (80,4)
        surface = dict(type='emitting_rectangular_prism', bounds=[[-w,-h,240],[w,h,244]],
                       emitting_faces='all six; q3map_surfacelight shader', surfacelight=2000 if name=='tube' else 100,
                       unit='legacy_q3map2_surface_light_units', texture_rgb8=[220,255,200],
                       native_analytic_tube=False)
    return dict(schema='org.openjk.q3map-ground-truth', version=1, backend='q3map2; not CPU reference baker',
                scene=name, lights=lights, surface_emitter=surface,
                sun=dict(azimuth_degrees=14,elevation_degrees=68,intensity=120,
                         rgb_parameter=[1,.98,.9],unit='legacy_q3map2_sun_units') if name in ('sun_sky','skylight') else None,
                sky=dict(value=25,iterations=3,texture_rgb8=[80,130,200]) if name in ('sun_sky','skylight') else None,
                bounce_iterations=1 if name=='bounce_colour' else 0, gamma=1,compensate=1,exposure=0,
                transfer='linear; default JA colour policy',grid_spacing_world=[64,64,128],styles=[0],
                physical_absolute_scale='unknown; do not relabel compiler units as cd/nits/lux')


def authoring(root, name, answer, q3map):
    base = root / 'authoring' / 'base'
    (base / 'maps').mkdir(parents=True, exist_ok=True)
    (base / 'shaders').mkdir(exist_ok=True)
    (base / 'textures' / 'recon_offline').mkdir(parents=True, exist_ok=True)
    # Our own constant TGA textures. No JA/third-party map or texture content.
    for material, rgb in [('white',(180,180,180)), ('red',(190,35,18)), ('panel',(220,255,200)), ('tube',(220,255,200)), ('sky',(80,130,200))]:
        header = struct.pack('<BBBHHBHHHHBB', 0,0,2,0,0,0,0,0,16,16,24,0x20)
        (base / 'textures' / 'recon_offline' / f'{material}.tga').write_bytes(header + bytes(rgb[::-1]) * 256)
    (base / 'shaders' / 'shaderlist.txt').write_text('recon_offline\n', encoding='ascii')
    shader = '''textures/recon_offline/white
{
 { map $lightmap
 }
 { map textures/recon_offline/white.tga
   blendFunc GL_DST_COLOR GL_ZERO
 }
}
textures/recon_offline/red
{
 { map $lightmap
 }
 { map textures/recon_offline/red.tga
   blendFunc GL_DST_COLOR GL_ZERO
 }
}
textures/recon_offline/panel
{ q3map_surfacelight 100
  { map $lightmap
  }
  { map textures/recon_offline/panel.tga
    blendFunc GL_DST_COLOR GL_ZERO
  }
  { map textures/recon_offline/panel.tga
    blendFunc GL_ONE GL_ONE
    glow
  }
}
textures/recon_offline/sky
{ qer_editorimage textures/recon_offline/sky.tga
  surfaceparm sky
  surfaceparm nolightmap
  q3map_sun 1 0.98 0.9 120 14 68
  q3map_skylight 25 3
  skyparms - 512 -
}
textures/recon_offline/tube
{ q3map_surfacelight 2000
  { map $lightmap
  }
  { map textures/recon_offline/tube.tga
    blendFunc GL_DST_COLOR GL_ZERO
  }
  { map textures/recon_offline/tube.tga
    blendFunc GL_ONE GL_ONE
    glow
  }
}
'''
    (base / 'shaders' / 'recon_offline.shader').write_text(shader, encoding='ascii', newline='\n')
    source, entities = author_map(name, answer)
    mapfile = base / 'maps' / f'{name}.map'
    mapfile.write_text('// Self-authored OpenJK regression fixture; GPL-2.0-or-later\n' + source,
                       encoding='ascii', newline='\n')
    common = ['-game','ja','-fs_basepath',str(base.parent.resolve()),'-fs_game','base','-threads','1']
    commands = [['-meta','-leaktest','-noflares','-samplesize','16'], ['-vis'],
                ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1',
                 '-exposure','0','-bounce', '1' if name == 'bounce_colour' else '0']]
    record = dict(schema='org.openjk.q3map-recipe', version=1, executed=False,
                  map_sha256=sha(mapfile), intensity_unit='legacy_q3map2_light_units_NOT_candela',
                  material_inputs={str(p.relative_to(base)).replace('\\', '/'): sha(p)
                                   for p in sorted(base.rglob('*'))
                                   if p.suffix in ('.tga', '.shader', '.txt')},
                  entities=entities, source_map=f'base/maps/{name}.map',
                  cpu_reference_is_same_bake=False,
                  ground_truth=q3_ground_truth(name, answer, entities),
                  tube='thin emitting rectangular prism; no claim of native analytic tube support',
                  argv=[['q3map2'] + common + stage + [str(mapfile.resolve())] for stage in commands])
    if q3map:
        record = compile_recipe(root, base, mapfile, name, record['argv'], record, q3map)
        if name == 'bounce_colour':
            # Identical authored geometry/lights, separate zero-bounce control bake.
            control = base / 'maps/bounce_control.map'
            control.write_bytes(mapfile.read_bytes())
            ctrl = dict(record, executed=False, source_map='base/maps/bounce_control.map')
            ctrl['ground_truth'] = dict(record['ground_truth'],scene='bounce_control',bounce_iterations=0)
            ctrl_commands = [[str(q3map)] + common + stage + [str(control.resolve())] for stage in commands]
            ctrl_commands[-1][-2] = '0'
            compile_recipe(root, base, control, 'bounce_control', ctrl_commands, ctrl, q3map)
    write_json(root / 'answers' / f'{name}.q3map-recipe.json', record)
    return record['executed']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='fresh output directory')
    parser.add_argument('--q3map2', type=Path, help='optional real compiler, records its binary hash and logs')
    args = parser.parse_args()
    tool, root = args.tool.resolve(), args.output.resolve()
    if root.exists():
        raise SystemExit('Use a fresh output directory; previous witnesses are never overwritten')
    root.mkdir(parents=True)
    for folder in ('inputs', 'answers', 'repeat/inputs', 'repeat/answers', 'reports'):
        (root / folder).mkdir(parents=True)
    version = run([tool, 'version']).strip()
    scenes = run([tool, 'list']).splitlines()
    manifest = dict(schema='org.openjk.offline-dataset-suite', version=1, tool_version=version,
                    tool_sha256=sha(tool), q3map2_executed=False, scenes={})
    for name in scenes:
        for prefix in (root, root / 'repeat'):
            run([tool, 'fixture', name, prefix / 'inputs' / f'{name}.jkobs',
                 prefix / 'answers' / f'{name}.json'])
        original = root / 'inputs' / f'{name}.jkobs'
        answer = root / 'answers' / f'{name}.json'
        assert sha(original) == sha(root / 'repeat' / 'inputs' / original.name), f'Non-deterministic dataset: {name}'
        assert sha(answer) == sha(root / 'repeat' / 'answers' / answer.name), f'Non-deterministic answer: {name}'
        inspect = run([tool, 'inspect', original], root / 'reports' / f'{name}.inspect.txt')
        run([tool, 'validate', original], root / 'reports' / f'{name}.validate.txt')
        # Full answer files, raw source maps and source entity declarations are never mounted here.
        assert b'ANSWER_ONLY_DO_NOT_EXPORT' not in original.read_bytes()
        manifest['q3map2_executed'] |= authoring(root, name, json.loads(answer.read_text()),
                                               args.q3map2.resolve() if args.q3map2 else None)
        manifest['scenes'][name] = dict(bytes=original.stat().st_size, sha256=sha(original),
                                       answer_sha256=sha(answer), repeated_bytes_identical=True,
                                       source_sha256=re.search(r'source=.*? sha256=([0-9a-f]{64})', inspect)[1],
                                       inspected=True, validated=True)
        print(name, original.stat().st_size, 'bytes; repeated SHA/geometry valid', flush=True)
    for name, expected in [('wall_leak',1), ('door_occlusion',0)]:
        result = run([tool,'trace',root/'inputs'/f'{name}.jkobs','-80','0','80','80','0','80'])
        # VisibilityState::Visible=0, Blocked=1, Unknown=2.
        assert f'visibility={expected} ' in result, result
        (root/'reports'/f'{name}.trace.txt').write_text(result)
    blocks = {}
    for partition in ('train', 'validation', 'heldout'):
        text = run([tool, 'samples', root/'inputs/point_room.jkobs', partition],
                   root/'reports'/f'point_room.{partition}.jsonl')
        rows = [json.loads(line) for line in text.splitlines()]
        assert rows, f'Empty {partition} fixture partition'
        for row in rows:
            block = tuple(math.floor(x / 128) for x in row['world'])
            assert blocks.setdefault(block, partition) == partition, 'Spatial split leaks between partitions'
    manifest['spatial_partition_control'] = 'point_room JSONL blocks disjoint, all three nonempty'
    write_json(root/'manifest.json', manifest)
    (root/'LICENSE.txt').write_text('These self-authored scenes, textures and generated data are GPL-2.0-or-later.\nNo stock JA content is included. See repository LICENSE.txt.\n')
    print('PASS: 11 datasets generated twice, validated offline; q3map2_executed=',manifest['q3map2_executed'])


if __name__ == '__main__':
    main()
