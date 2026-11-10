"""Freeze and evaluate bounded original JA fixture regions using existing providers.

Licensed geometry, images, coordinates and RGB stay in the ignored work folder.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import io
import itertools
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.bsp_fixtures.catalogue import Assets, digest
from tools.rend2.grid_triangulation.triangulate import Bridge
from tools.rend2.lightmap_patches.run import read_rows, sha, write
from tools.rend2.lightmap_patches.extract import extract
from tools.rend2.fixture_association import adapters
from tools.rend2.fixture_association.associate import associate

# These are an explicitly selected cohort, not a map-wide recovery claim. No fit
# scores were used to choose them. IDs refer to the existing ASR09 mask-axis-32 census.
REGIONS = [
    ('hoth2-wall-bank', 'hoth2', '02626b3107', 5863),
    ('hoth2-small-ceiling', 'hoth2', '013d1d8bd2', 5879),
    ('hoth3-ceiling', 'hoth3', '1239808a0b', 10508),
    ('hoth3-blue', 'hoth3', 'af95eb783d', 9394),
    ('hoth3-large-ceiling', 'hoth3', '087b20e917', 10490),
    ('duel4-yellow-panel', 'mp_duel4', '00434bf31f', 1035),
    ('duel4-white-panel', 'mp_duel4', 'b809a4559c', 1052),
    ('duel4-square-light', 'mp_duel4', '009cf3188a', 1022),
]
POLICY = dict(roi_half_extent=384, block_size=256, block_offset=[17, 31, 47],
              geometry_only_offset_alternatives=[17, 81, 145, 209],
              split_seed='asr24-stock-v1', gap='3 * planar per-axis footprint radius bound + 1 WU',
              max_rows_per_fold=48, source_standoff=4, power_upper=100000,
              fit_max_sweeps=200,
              fit_relative_loss_tolerance=1e-4,
              background_upper=255, gain=1, validation_slack=1e-4,
              heldout_mse_ratio_max=.90, heldout_robust_ratio_max=.95,
              minimum_grid_supports=3, grid_max_angle_degrees=30,
              source_colour='free nonnegative relative RGB; independent texture prior is diagnostic',
              transfer='conditional default NetRadiant forward encoding; stock profile unknown',
              hypothesis='one fixed mounted-point approximation per named luminous component',
              receiver_coverage='Shape::Prepare geometry-only known coverage; known blocked rows retained; unknown rows excluded and counted',
              grid_ray='BSP ClassifySupport of actual segment hull + StaticVisibility optical query; mover state remains conditional',
              substantive_response='At least one held-out receiver changes by one RGB8 code; removing sources with frozen background worsens interval MSE',
              dynamic_state='conditional static bake, unknown mover state retained',
              historical_source_certified=False, runtime_publication=False)


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def length(a):
    return math.sqrt(dot(a, a))


def split(row):
    """Membership uses geometry only; footprints cannot touch another fold's block."""
    cell = []
    normal = row.get('normal', [0, 0, 0])
    for axis, (p, offset) in enumerate(zip(row['position'], row.get('partition_offset', POLICY['block_offset']))):
        x = p-offset
        k = math.floor(x/POLICY['block_size'])
        # Planar footprint displacement is tangent to the geometric normal.
        # A spherical bound incorrectly erased whole floors near a Z boundary.
        # This remains an outer bound, with the same 3x filtering margin.
        extent = row['footprint_radius']*math.sqrt(max(0, 1-normal[axis]**2))
        if min(x-k*POLICY['block_size'], (k+1)*POLICY['block_size']-x) <= 3*extent+1:
            return None
        cell.append(k)
    key = hashlib.sha256((POLICY['split_seed']+'/'+str(cell)).encode()).hexdigest()
    bucket = int(key[:8], 16) % 10
    return ('train' if bucket < 6 else 'validation' if bucket < 8 else 'heldout',
            int(key[:15], 16)+1)


def eligible(row):
    bad = sum(1 << i for i in (1, 2, 3, 8, 9, 10, 11, 12, 13))
    return (row.get('position') and row.get('normal') and row['fit_candidate'] and
            row['coverage'] == 2 and row['kind'] == 0 and row['style'] == 0 and
            not row['issues'] & bad and row['area'] > 0 and
            not row['material_signals'] & (4 | 8) and row['encoding'] == 0)


def normal_bin(row):
    axis = max(range(3), key=lambda k: abs(row['normal'][k]))
    return axis, row['normal'][axis] > 0


def fixture_lines(spec, sparse=False):
    if spec['kind'] == 'surface-mesh':
        return [' '.join(map(str, ['surface-mesh', 1, spec['fixture']['surfaces'][0], 100000, 100000, 100000]))]
    return [' '.join(map(str, ['mounted-point', i+1,
                              spec.get('source_surfaces', [spec['fixture']['surfaces'][0]]*len(spec.get('source_positions', [spec['source_position']])))[i], *position,
                              *([POLICY['power_upper']]*3)]+(['visible', spec['source_component_ids'][i]] if sparse else [])))
            for i, position in enumerate(spec.get('source_positions', [spec['source_position']]))]


def invoke(command, folder, name, timeout=300):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=timeout)
    (folder/(name+'.stdout')).write_text(result.stdout, encoding='utf-8')
    (folder/(name+'.stderr')).write_text(result.stderr, encoding='utf-8')
    if result.returncode:
        raise ValueError(result.stderr.strip() or 'Native tool failed')
    return result.stdout


def rows_text(rows):
    return [str(len(rows))]+[' '.join(map(str, [int(r['id'][:15], 16)+1, split(r)[1],
                 r['surface'], *r['position'], *r['normal'], r['area'], *r['raw']])) for r in rows]


def sources():
    paths = list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    paths += [ROOT/'shared/rd-rend2'/s for s in ('tr_lightrecon_types.cpp', 'tr_lightrecon_types.h')]
    paths += list((ROOT/'tools/rend2/stock_aesthetic').glob('*.py'))
    paths += [ROOT/'tools/lightrecon_test'/s for s in
              ('aesthetic_fit.cpp', 'aesthetic_fit.h', 'aesthetic_sparse.inc', 'lightrecon_aesthetic_tool.cpp',
               'lightrecon_grid_geometry_tool.cpp')]
    for module in ('fixture_association', 'lightmap_patches', 'bsp_fixtures', 'grid_triangulation'):
        paths += list((ROOT/'tools/rend2'/module).glob('*.py'))
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(paths)}


def prepare(a):
    if (a.area_groups or a.point_groups) and not a.discovery:
        raise ValueError('Extended surface groups require the independent-grid discovery report')
    a.work.mkdir(parents=True, exist_ok=False)
    regions = REGIONS
    if a.discovery:
        discovery = load(a.discovery)
        discovery_map = discovery.get('map', 'maps/mp/duel4.bsp')[5:-4].replace('/', '_')
        selected = discovery['candidates'][:3]
        if a.all_stock_inputs:
            selected = [c for c in selected if c['agreeing_distinct_records'] >= 3]
        if not selected or (not a.all_stock_inputs and len(selected) != 3) or any(c['agreeing_distinct_records'] < 3 for c in selected):
            raise ValueError('Three independently grid-supported original fixtures required')
        if a.area_groups or a.point_groups:
            selected = list({c['surfaces'][0]: c for c in reversed(selected)}.values())
        region_map = discovery_map[3:] if discovery_map.startswith('mp_') else discovery_map
        regions = [(region_map+'-'+('area-' if a.area_groups else 'grid-')+str(i+1),
                    discovery_map, c['fixture_id'], c['surfaces'][0]) for i, c in enumerate(selected)]
        shutil.copyfile(a.discovery, a.work/'discovery.json')
    assets = Assets(a.base)
    try:
        maps = {}
        for m in sorted({r[1] for r in regions}):
            bsp, origin = assets.read('maps/'+m.replace('mp_', 'mp/')+'.bsp')
            if bsp is None:
                raise FileNotFoundError('Blocked: licensed stock map absent: '+m)
            catalogue = a.catalogues/m/'catalogue.json'
            prefix = a.catalogues/m if m.startswith('mp_') else a.rows/m
            source = prefix.with_suffix('.source.json')
            if a.all_stock_inputs:
                source = Path(load(a.all_stock_inputs)[m]['source'])
            identity = digest(bsp)
            if identity != load(catalogue)['map_sha256'] or identity != load(source)['map_sha256']:
                raise ValueError('Original BSP/catalogue/receiver identity mismatch')
            capture = (a.stock_private/'capture'/m/'home/rend2/lightrecon/v1/observations'/(m+'_all.jkobs')
                       if m.startswith('mp_') else a.captures/(m+'_a.jkobs'))
            rows_path = prefix.with_suffix('.rows.jsonl') if m.startswith('mp_') else prefix.with_suffix('.rows.jsonl.gz')
            if a.all_stock_inputs:
                inputs = load(a.all_stock_inputs)[m]
                source = Path(inputs['source'])
                capture = Path(inputs['capture'])
                rows_path = Path(inputs['rows'])
                if identity != load(source)['map_sha256']:
                    raise ValueError('Unified original receiver identity mismatch')
            maps[m] = dict(bsp_sha256=identity, bsp_source=origin, catalogue=str(catalogue.resolve()),
                           catalogue_sha256=sha(catalogue), rows=str(rows_path.resolve()),
                           rows_sha256=sha(rows_path), source=str(source.resolve()),
                           source_sha256=sha(source), capture=str(capture.resolve()), capture_sha256=sha(capture))
        specs = []
        if a.discovery and (discovery['map_sha256'] != maps[discovery_map]['bsp_sha256'] or
                            discovery['catalogue_sha256'] != maps[discovery_map]['catalogue_sha256'] or
                            discovery['capture_sha256'] != maps[discovery_map]['capture_sha256']):
            raise ValueError('Discovery/original source identity mismatch')
        for name, m, prefix, ordinal in regions:
            cat = load(maps[m]['catalogue'])
            matches = [f for f in cat['fixtures'] if f['id'].startswith(prefix)]
            if len(matches) != 1:
                raise ValueError('Frozen original fixture identity missing/ambiguous')
            f = matches[0]
            if f['surfaces'] != [ordinal] or not f['static_eligible'] or not f['emitting_footprint_known']:
                raise ValueError('Frozen fixture has no original static luminous footprint')
            positions = None
            if a.area_groups or a.point_groups:
                members = [c for c in cat['fixtures'] if c['surfaces'] == [ordinal] and
                           c['emitting_footprint_known'] and c['static_eligible']]
                f = dict(f)
                total = sum(c['emitting_area'] for c in members)
                f['original_component_ids'] = [c['id'] for c in members]
                f['id'] = digest(('asr24-surface-group/'+str(ordinal)+'/'+','.join(f['original_component_ids'])).encode())
                f['centroid'] = [sum(c['centroid'][k]*c['emitting_area'] for c in members)/total for k in range(3)]
                f['footprint'] = [poly for c in members for poly in c['footprint']]
                f['emitting_area'] = total
                f['issues'] = sorted(set(f['issues']+[
                    'Grouped separately visible original luminous components' if a.point_groups else
                    'Whole original face is a conditional extended-emission approximation']))
                if a.point_groups:
                    if len(members) > 4:
                        raise ValueError('Point-group enumeration requires at most four original visible components')
                    positions = [[x+4*n for x, n in zip(c['centroid'], c['outward'])] for c in members]
            p = [x+POLICY['source_standoff']*n for x, n in zip(f['centroid'], f['outward'])]
            h = POLICY['roi_half_extent']
            specs.append(dict(name=name, map=m, fixture=f, source_position=p,
                              kind='surface-mesh' if a.area_groups else 'mounted-point',
                              source_position_role=('Grid-only reference 4 WU outside grouped visible support; fit integrates exact original face'
                                                    if a.area_groups else 'Group reference; fitted locations use each original component +4 WU'
                                                    if a.point_groups else 'Fixed fixture-local point approximation'),
                              roi=[x-h for x in f['centroid']]+[x+h for x in f['centroid']]))
            if a.receiver_path:
                bridge = Bridge(a.geometry_tool, maps[m]['capture'])
                try:
                    end = [x+768*n for x, n in zip(p, f['outward'])]
                    hit = bridge.query('segment', *p, *end)
                    specs[-1]['receiver_path'] = dict(start=p, end=end, result=hit)
                    if hit['opaque_hit_known'] and not hit['dynamic_possible']:
                        axis = max(range(3), key=lambda k: abs(f['outward'][k]))
                        margin = [192 if k != axis else 32 for k in range(3)]
                        specs[-1]['roi'] = [min(p[k], hit['hit_position'][k])-margin[k] for k in range(3)]+[
                            max(p[k], hit['hit_position'][k])+margin[k] for k in range(3)]
                        specs[-1]['receiver_surface'] = hit['hit_surface']-1
                    else:
                        specs[-1]['receiver_path_unavailable'] = 'No known geometric opaque receiver along fixture outward direction'
                finally:
                    bridge.close()
            if positions:
                specs[-1]['source_positions'] = positions
            if a.sparse_neighbours:
                neighbours, seen = [], set()
                for c in discovery['candidates']:
                    if (c['agreeing_distinct_records'] >= 3 and c['surfaces'][0] not in seen and
                            all(specs[-1]['roi'][k] <= c['position'][k] <= specs[-1]['roi'][k+3] for k in range(3))):
                        neighbours.append(c)
                        seen.add(c['surfaces'][0])
                    if len(neighbours) == 8:
                        break
                if not neighbours:
                    raise ValueError('No independently supported visible neighbours in frozen ROI')
                specs[-1].update(source_positions=[c['position'] for c in neighbours],
                                 source_surfaces=[c['surfaces'][0] for c in neighbours],
                                 source_component_ids=[c['fixture_id'] for c in neighbours],
                                 source_fixtures=[next(f for f in cat['fixtures'] if f['id'] == c['fixture_id']) for c in neighbours],
                                 sparse=True)
            folder = a.work/name
            folder.mkdir()
            if specs[-1].get('source_fixtures'):
                write(folder/'source-fixtures.json', specs[-1]['source_fixtures'])
                for neighbour in specs[-1]['source_fixtures']:
                    for layer in neighbour['layers']:
                        mask = cat['masks'][layer['texture']]
                        data, _ = assets.read(mask['path'])
                        if digest(data) != mask['sha256']:
                            raise ValueError('Original neighbouring fixture image hash mismatch')
            # Original source image and shader provenance; no fitted lighting enters.
            for i, layer in enumerate(f['layers']):
                mask = cat['masks'][layer['texture']]
                image, _ = assets.read(mask['path'])
                if digest(image) != mask['sha256']:
                    raise ValueError('Original source texture hash mismatch')
                from PIL import Image
                Image.open(io.BytesIO(image)).save(folder/('source-glow-'+str(i)+'.png'))
            write(folder/'fixture-evidence.json', dict(fixture=f,
                  shader=cat['shader_manifest'].get(f['material']), masks=cat['masks'],
                  interpretation=cat['interpretation']))
        # Freeze all selected boundaries and lamp hypotheses BEFORE seeing fit scores.
        policy = dict(POLICY)
        if a.area_groups:
            policy.update(hypothesis='Uniform Lambertian original-face triangle group; optical mask remains conditional',
                          power_upper=100000, source_standoff='Exact original face for fitting; 4 WU grid-only reference')
        if a.common_normals:
            policy.update(receiver_design='Same material and signed normal bin; maximize minimum known-positive geometric responses across folds')
        if a.point_groups:
            policy.update(hypothesis='Up to four individually mounted visible luminous components on one original fixture surface; enumerate all subsets')
        if a.sparse_neighbours:
            policy.update(hypothesis='ASR21 sparse selection of up to eight distinct grid-supported original visible fixture surfaces in the frozen ROI',
                          sparse=dict(max_sources=4, max_models=64, training_penalty=1e-4, validation_penalty=1e-4))
        if a.receiver_path:
            policy.update(roi='Fixture to first known geometric opaque hit along original outward normal, maximum 768 WU; tangential margin 192, axial margin 32',
                          receiver_design='Exact originally hit receiver surface; geometry-only fold design; unknown/mover intersections remain unavailable')
        protocol = dict(schema='org.openjk.stock-aesthetic-protocol.v1', policy=policy, maps=maps,
                        regions=specs, sources=sources(), tools={str(p.resolve()): sha(p) for p in
                        (a.tool, a.geometry_tool, a.solar_tool, a.encoder_tool)}, frozen_before_scoring=True)
        write(a.work/'protocol.json', protocol)
        for path in protocol['sources']:
            target = a.work/'implementation'/path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT/path, target)
        for path in protocol['tools']:
            target = a.work/'implementation/bin'/Path(path).name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        for m, record in maps.items():
            cohorts = [s for s in specs if s['map'] == m]
            pools = {s['name']: [] for s in cohorts}
            outdoor_geometry = {}
            for row in read_rows(record['rows']):
                if not eligible(row):
                    continue
                cell = tuple(math.floor(x/512) for x in row['position'])
                if cell not in outdoor_geometry or row['id'] < outdoor_geometry[cell]['id']:
                    outdoor_geometry[cell] = row
                for s in cohorts:
                    if all(s['roi'][k] <= row['position'][k] <= s['roi'][k+3] for k in range(3)):
                        pools[s['name']].append(row)
            if m == 'hoth2':
                folder = a.work/'hoth2-exterior'
                folder.mkdir()
                points = sorted(outdoor_geometry.values(), key=lambda r: r['id'])[:256]
                (folder/'outdoor.txt').write_text('JKSOLAROUTDOOR1 '+str(len(points))+'\n'+
                      '\n'.join(' '.join(map(str, r['position'])) for r in points)+'\n')
                try:
                    invoke([a.solar_tool, record['capture'], '--find-outdoor', folder/'outdoor.txt',
                            folder/'outdoor.json'], folder, 'outdoor')
                    outdoor = load(folder/'outdoor.json')
                    h = POLICY['roi_half_extent']
                    outdoor['roi'] = [x-h for x in outdoor['centre']]+[x+h for x in outdoor['centre']]
                    outdoor['status'] = 'Unresolved'
                    outdoor['reason'] = 'Geometric sky reach supports an exterior, not a numeric sun fit or historical direction'
                    write(folder/'selection.json', outdoor)
                except (ValueError, subprocess.TimeoutExpired) as error:
                    write(folder/'selection.json', dict(status='Unresolved', reason=str(error)))
            for s in cohorts:
                folder = a.work/s['name']
                if s.get('receiver_path_unavailable'):
                    write(folder/'selection.json', dict(available=False, reason=s['receiver_path_unavailable']))
                    continue
                facing = [row for row in pools[s['name']] if
                          ('receiver_surface' not in s or row['surface'] == s['receiver_surface']) and
                          dot(sub(s['source_position'], row['position']), row['normal']) > 0 and
                          dot(sub(row['position'], s['fixture']['centroid']), s['fixture']['outward']) > 0]
                # Existing modern forward provider, geometry ONLY: discard unknown
                # optical coverage before designing folds. Known blocked zeros stay.
                # Dummy RGB is always zero and cannot select a bright receiver.
                covered = []
                coverage_counts = Counter()
                for start in range(0, len(facing), 512):
                    batch = facing[start:start+512]
                    lines = fixture_lines(s)
                    header = ['JKASR24-GEOMETRY-ONLY-1', '1', '255 255 255', str(len(lines)),
                              *lines, str(len(batch))]
                    header += [' '.join(map(str, [int(r['id'][:15], 16)+1, 1, r['surface'],
                              *r['position'], *r['normal'], r['area'], 0, 0, 0])) for r in batch]
                    path = folder/('coverage-'+str(start)+'.txt')
                    path.write_text('\n'.join(header)+'\n')
                    raw = invoke([a.tool, record['capture'], path, '--coverage'], folder, 'coverage-'+str(start))
                    response = json.loads(raw)['rows']
                    by_id = {str(int(r['id'][:15], 16)+1): r for r in batch}
                    for c in response:
                        coverage_counts[str((c['available'], c['coefficient'] > 0, c['visibility_issues']))] += 1
                        if c['available']:
                            covered.append(dict(by_id[c['id']], unit_response=c['coefficient']))
                write(folder/'coverage-summary.json', dict(requested=len(facing), known=len(covered),
                      unknown_excluded=len(facing)-len(covered), categories=dict(coverage_counts),
                      original_rgb_supplied=False, provider='Shape::Prepare + Block::Evaluate; fixed unit amplitude'))
                facing = covered
                # A finite geometric design step, frozen before numerical fitting.
                # It never sees target values or prior fit scores. All trials/counts
                # remain inspectable, including rejected low-coverage designs.
                layouts = []
                for offset in itertools.product(POLICY['geometry_only_offset_alternatives'], repeat=3):
                    groups = defaultdict(lambda: defaultdict(list))
                    for row in facing:
                        r = dict(row, partition_offset=offset)
                        role = split(r)
                        if role is not None:
                            key = (r['material'], normal_bin(r) if a.common_normals else None)
                            groups[key][role[0]].append(r)
                    for (material, bin_id), group in groups.items():
                        counts = {r: len(group[r]) for r in ('train', 'validation', 'heldout')}
                        informative = {r: sum(x['unit_response'] > 0 for x in group[r]) for r in counts}
                        layouts.append(dict(offset=offset, material=material, normal_bin=bin_id, counts=counts,
                                            informative=informative,
                                            minimum=min((informative if a.common_normals else counts).values())))
                layouts.sort(key=lambda x: (-x['minimum'], x['material'], x['offset']))
                write(folder/'geometric-layouts.json', layouts)
                chosen = layouts[0] if layouts else dict(minimum=0, material='', offset=POLICY['block_offset'])
                minimum, material = chosen['minimum'], chosen['material']
                discovery = []
                groups = defaultdict(list)
                for row in pools[s['name']]:
                    r = dict(row, partition_offset=chosen['offset'])
                    role = split(r)
                    if role is not None and role[0] == 'train':
                        discovery.append(r)
                for row in facing:
                    r = dict(row, partition_offset=chosen['offset'])
                    role = split(r)
                    if (role is not None and row['material'] == material and
                            (not a.common_normals or normal_bin(row) == chosen['normal_bin'])):
                        groups[role[0]].append(r)
                if minimum < 4:
                    write(folder/'selection.json', dict(available=False, reason='Insufficient separated same-material folds',
                          minimum_fold_rows=minimum, qualified_roi_rows=len(pools[s['name']])))
                    continue
                folds = {role: sorted(groups[role], key=lambda r: r['id'])[:POLICY['max_rows_per_fold']]
                         for role in ('train', 'validation', 'heldout')}
                lines = fixture_lines(s, s.get('sparse', False))
                fit = (['JKASR21-SPARSE-ENCODED-ASSUMED-MATERIAL-1', '1', '255 255 255',
                        '4 64 0.0001 0.0000001 0.0000001 0.0000001 0.0001 0.0001 asr24-original-grid-visible']
                       if s.get('sparse') else ['JKASR20-ENCODED-ASSUMED-MATERIAL-1', '1', '255 255 255'])
                fit += [str(len(lines)), *lines]
                fit += rows_text(folds['train'])+rows_text(folds['validation'])
                (folder/'fit.txt').write_text('\n'.join(fit)+'\n')
                (folder/'held.txt').write_text('\n'.join(rows_text(folds['heldout']))+'\n')
                (folder/'discovery.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in discovery))
                write(folder/'folds.json', folds)
                write(folder/'selection.json', dict(available=True, material=material,
                      selection='Finite geometry-only offset/material design maximizes minimum fold coverage; stable-ID cap',
                      partition_offset=chosen['offset'], geometry_layouts_tested=len(layouts),
                      normal_bin=chosen.get('normal_bin'), informative_counts=chosen.get('informative'),
                      forward_geometry_coverage=load(folder/'coverage-summary.json'),
                      counts={r: len(v) for r, v in folds.items()},
                      blocks={r: len({split(x)[1] for x in v}) for r, v in folds.items()},
                      qualified_roi_rows=len(pools[s['name']]), discovery_training_rows=len(discovery)))
        frozen = {str(p.relative_to(a.work)): sha(p) for p in a.work.rglob('*') if p.is_file()}
        write(a.work/'freeze.json', frozen)
    finally:
        assets.close()


def check_frozen(work):
    protocol = load(work/'protocol.json')
    for path, expected in load(work/'freeze.json').items():
        if sha(work/path) != expected:
            raise ValueError('Frozen input modified: '+path)
    if sources() != protocol['sources']:
        raise ValueError('Implementation changed after freeze; use a fresh cohort')
    for path, expected in protocol['tools'].items():
        if sha(path) != expected:
            raise ValueError('Native tool changed after freeze')
    for record in protocol['maps'].values():
        for key in ('catalogue', 'rows', 'source', 'capture'):
            if sha(record[key]) != record[key+'_sha256']:
                raise ValueError('Original input changed: '+key)
    return protocol


def grid_witness(bridge, rows, position):
    checks = []
    for row in rows:
        if (not row['present'] or row['issues'] & 64 or row['space'] != 'free' or row['boundary'] or
                len(row['styles']) != 1 or row['styles'][0]['weak']):
            continue
        d = sub(position, row['position'])
        angle = math.degrees(math.acos(max(-1, min(1, dot(d, row['consumer256'])/max(1e-30, length(d)*length(row['consumer256']))))))
        ray = bridge.query('segment', *row['position'], *position)
        checks.append(dict(ordinal=row['ordinal'], angle_degrees=angle,
                           correlation_key=row['correlation_key'], ray=ray))
    # Strict known-static witnesses and conditional-mover witnesses are separate.
    good = [c for c in checks if c['angle_degrees'] <= POLICY['grid_max_angle_degrees'] and c['ray']['static_state'] == 'clear']
    return dict(checks=checks, known_static_agreeing_records=len({c['correlation_key'] for c in good}),
                mover_independent_agreeing_records=len({c['correlation_key'] for c in good if not c['ray']['dynamic_possible']}))


def gate(result, witness, ablation=None):
    if result['selected'] < 0:
        if not any(c['available'] and c['validation']['available'] for c in result['configurations']):
            return 'Unresolved', 'Local configurations lack known forward coverage on training or validation'
        return 'Rejected', 'Validation selected the nuisance-only baseline'
    c = result['configurations'][result['selected']]
    active = [s for s in c['sources'] if max(s['relative_rgb']) > 0]
    if not c['converged']:
        return 'Unresolved', 'Fit reached its declared numerical budget without convergence'
    if (not c['available'] or not active or
            any(s['placement'] not in ('Free', 'Conditional') or not s['source_side_certified'] for s in c['sources'])):
        return 'Unresolved', 'Nonempty supported source gate failed'
    b, h = result['baseline']['heldout'], c['heldout']
    if not h['available'] or not b['available']:
        return 'Unresolved', 'Held-out forward evaluation unavailable'
    if not (h['mse'] < POLICY['heldout_mse_ratio_max']*b['mse'] and
            h['robust'] < POLICY['heldout_robust_ratio_max']*b['robust']):
        return 'Rejected', 'Frozen held-out improvement gate failed'
    if any(s['power_at_bound'] for s in c['sources']) or c['background_at_bound']:
        return 'Unresolved', 'Conditional model reached a declared bound'
    if witness['known_static_agreeing_records'] < POLICY['minimum_grid_supports']:
        return 'Unresolved', 'Insufficient independently agreeing original-grid records with known-static rays'
    if witness.get('components') and any(witness['components'][int(s['fixture'])-1]['known_static_agreeing_records'] < POLICY['minimum_grid_supports'] for s in active):
        return 'Unresolved', 'An active component lacks independent known-static grid supports'
    if not ablation or not ablation.get('available'):
        return 'Unresolved', 'Frozen source-removal audit unavailable'
    if not ablation['rows_with_at_least_one_code_response'] or not ablation['source_removal_worsens_mse']:
        return 'Rejected', 'Frozen fitted source has no substantive held-out contribution'
    return 'AestheticCandidate', 'Visible original fixture + admitted mount + independent grid + frozen held-out improvement'


def evaluate(a):
    protocol = check_frozen(a.work)
    results = []
    for spec in protocol['regions']:
        folder = a.work/spec['name']
        record = protocol['maps'][spec['map']]
        summary = dict(name=spec['name'], map=spec['map'], historical_source_certified=False)
        selection = load(folder/'selection.json')
        summary['selection'] = selection
        if not selection['available']:
            summary.update(status='Unresolved', reason=selection['reason'])
            results.append(summary)
            continue
        print('Evaluating '+spec['name'], flush=True)
        bridge = Bridge(a.geometry_tool, record['capture'])
        try:
            if bridge.metadata['map_sha256'] != record['bsp_sha256']:
                raise ValueError('Grid/geometry map identity mismatch')
            grid = bridge.query('cells', *spec['roi'])['rows']
            mesh = bridge.query('mesh', *spec['roi'])
            witness = grid_witness(bridge, grid, spec['source_position'])
            if spec.get('source_positions'):
                witness['components'] = [grid_witness(bridge, grid, position) for position in spec['source_positions']]
            write(folder/'geometry.json', dict(metadata=bridge.metadata, mesh=mesh,
                  point=bridge.point(spec['source_position']), grid=grid, grid_witness=witness))
            discovery = list(read_rows(folder/'discovery.jsonl'))
            patches = extract(discovery)
            patches['input'] = dict(map_sha256=record['bsp_sha256'], training_only=True, roi_world=spec['roi'])
            write(folder/'patches.json', patches)
            grid_obs, rejections = adapters.grid(grid)
            # Keep ASR15's conservative mover/endpoint behavior. The separate grid
            # witness reports static-only checks without upgrading these links.
            fs = adapters.fixtures(dict(schema='org.openjk.research.bsp-fixtures/1', fixtures=spec.get('source_fixtures', [spec['fixture']])))
            class Geometry:
                def ray(self, start, end):
                    r = bridge.ray(start, end)
                    return 'unknown' if r['dynamic_possible'] else r['static_state']
            try:
                observations, _ = adapters.patches(patches, discovery)
                associations = associate(fs, observations+grid_obs, Geometry())
                associations['available'] = True
            except ValueError as error:
                # An optional association budget failure is not an empty source
                # configuration. Preserve its refusal and continue the independent
                # admitted-fixture/grid/fit path; never publish a partial assignment.
                associations = dict(available=False, reason=str(error), active_fixtures=[],
                                    historical_source_certified=False)
            associations['grid_rejections'] = rejections
            write(folder/'associations.json', associations)
            folds = load(folder/'folds.json')
            train = folds['train']
            solar = ['JKSOLARAUDIT1', record['bsp_sha256'], *spec['fixture']['centroid'], 384, len(train)]
            solar_text = ' '.join(map(str, solar))+'\n'+'\n'.join(' '.join(map(str,
                         [r['surface']+1, *r['position'], *r['normal']])) for r in train)+'\n'
            (folder/'solar.txt').write_text(solar_text)
            invoke([a.solar_tool, record['capture'], folder/'solar.txt', folder/'solar.json'], folder, 'solar')
            raw = invoke([a.tool, record['capture'], folder/'fit.txt', folder/'held.txt'], folder, 'fit')
            repeat = invoke([a.tool, record['capture'], folder/'fit.txt', folder/'held.txt'], folder, 'repeat')
            if raw != repeat:
                raise ValueError('Nondeterministic native fit')
            result = json.loads(raw)
            write(folder/'result.json', result)
            if result['selected'] >= 0:
                from tools.rend2.stock_aesthetic.audit import audit
                try:
                    ablation = audit(a.work, spec['name'], a.encoder_tool)
                except (ValueError, subprocess.TimeoutExpired) as error:
                    ablation = dict(available=False, reason=str(error))
            else:
                ablation = dict(available=False, reason='No validation-selected local configuration')
            write(folder/'ablation.json', ablation)
            status, reason = gate(result, witness, ablation)
            selected_fit = result['configurations'][result['selected']] if result['selected'] >= 0 else None
            write(folder/'hypothesis.json', dict(schema='org.openjk.research.stock-source-configuration.v1',
                  canonical_asset=False, runtime_publication=False, historical_source_certified=False,
                  status=status, reason=reason, map_sha256=record['bsp_sha256'], roi_world=spec['roi'],
                  original_fixture_id=spec['fixture']['id'], original_surfaces=spec['fixture']['surfaces'],
                  original_fixture_model=spec['fixture']['model'], fixture_material=spec['fixture']['material'],
                  receiver_surface=spec.get('receiver_surface'),
                  available=selected_fit is not None, sources=selected_fit['sources'] if selected_fit else None,
                  background=selected_fit['background'] if selected_fit else None,
                  power_units='Arbitrary relative RGB with fixed gauge=1; not watts/candela/lux',
                  colour_domain='Forward encoded RGB8 appearance; stock transfer/material response conditional',
                  grid_known_static_records=witness['known_static_agreeing_records'],
                  source_removal=ablation,
                  confidence_limits=['Point approximation and 4 WU stand-off are fixed hypotheses',
                    'Original glow and grid directions do not certify historical bake participation',
                    'Unknown bake transfer, material response, indirect transport and filtering',
                    'Source/background trade-off; no unique intensity or source-count claim',
                    'Sparse spatial folds bound this demonstration, not the whole map']))
            summary.update(status=status, reason=reason, repeat_identical=True,
                           selected=result['selected'], baseline=result['baseline'],
                           configurations=[{k: v for k, v in c.items() if k != 'sources'} for c in result['configurations']],
                           grid_known_static_supports=witness['known_static_agreeing_records'],
                           grid_mover_independent_supports=witness['mover_independent_agreeing_records'],
                           patch_count=len(patches['patches']),
                           conservative_associations=len(associations['active_fixtures']) if associations['available'] else None,
                           association_unavailable_reason=associations.get('reason'),
                           source_removal=ablation,
                           geometry_complete=mesh['coverage_complete'])
            if result['selected'] >= 0:
                selected_sources = result['configurations'][result['selected']]['sources']
                source = next((s for s in selected_sources if max(s['relative_rgb']) > 0), selected_sources[0])
                summary['source'] = {k: v for k, v in source.items() if k != 'position'}
                summary['sources'] = [{k: v for k, v in s.items() if k != 'position'} for s in selected_sources]
            from tools.rend2.stock_aesthetic.report import report
            report(folder, spec)
        except (ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
            summary.update(status='Unresolved', reason=str(error))
        finally:
            bridge.close()
        write(folder/'assessment.json', summary)
        results.append(summary)
        print(spec['name']+': '+summary['status']+' — '+summary['reason'], flush=True)
    exterior_file = a.work/'hoth2-exterior/selection.json'
    exterior = load(exterior_file) if exterior_file.exists() else None
    if exterior is not None:
        audit_exterior(a, protocol, results, exterior)
    check_frozen(a.work)
    status = 'Demonstrated' if any(r['status'] == 'AestheticCandidate' for r in results) else 'NotDemonstrated'
    write(a.work/'summary.json', dict(schema='org.openjk.stock-aesthetic-summary.v1', status=status,
          regions=results, protocol_sha256=sha(a.work/'protocol.json'), freeze_sha256=sha(a.work/'freeze.json'),
          inputs_unchanged=True, historical_source_certified=False, runtime_changed=False))


def audit_exterior(a, protocol, results, exterior):
    exterior_summary = dict(name='hoth2-exterior', map='hoth2', status='Unresolved',
                            reason=exterior['reason'], historical_source_certified=False)
    if 'centre' in exterior:
        folder = a.work/'hoth2-exterior'
        record = protocol['maps']['hoth2']
        points = sorted((r for r in read_rows(record['rows'], roi=exterior['roi']) if eligible(r)), key=lambda r: r['id'])[:48]
        text = ' '.join(map(str, ['JKSOLARAUDIT1', record['bsp_sha256'], *exterior['centre'], 384, len(points)]))+'\n'
        text += '\n'.join(' '.join(map(str, [r['surface']+1, *r['position'], *r['normal']])) for r in points)+'\n'
        (folder/'solar.txt').write_text(text)
        try:
            invoke([a.solar_tool, record['capture'], folder/'solar.txt', folder/'solar.json'], folder, 'solar')
            solar = load(folder/'solar.json')
            exterior_summary['solar'] = solar
        except (ValueError, subprocess.TimeoutExpired) as error:
            exterior_summary['reason'] = str(error)
    write(a.work/'hoth2-exterior/assessment.json', exterior_summary)
    results.append(exterior_summary)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=['prepare', 'evaluate'])
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--base', type=Path, default=ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets/base')
    p.add_argument('--rows', type=Path, default=ROOT/'build/x02')
    p.add_argument('--catalogues', type=Path, default=ROOT/'build/asr24-original-catalogues')
    p.add_argument('--captures', type=Path, default=ROOT/'build/x02/runtime/run/home/rend2/lightrecon/v1/observations')
    p.add_argument('--stock-private', type=Path, default=Path.home()/'AppData/Local/Temp/jk-lightrecon-all-stock-20261031')
    p.add_argument('--discovery', type=Path, help='Frozen independent-grid fixture ranking for the separate top-three follow-up')
    p.add_argument('--all-stock-inputs', type=Path, help='Explicit original per-map capture/source/rows manifest; allow one to three supported fixtures with unchanged acceptance gates')
    p.add_argument('--area-groups', action='store_true', help='Fit original-face extended groups from the grid-selected surfaces')
    p.add_argument('--common-normals', action='store_true', help='Geometry-only same-normal/material subset with informative response in every fold')
    p.add_argument('--point-groups', action='store_true', help='Enumerate subsets of visible luminous component points on grid-selected fixture surfaces')
    p.add_argument('--sparse-neighbours', action='store_true', help='Use existing ASR21 sparse fitting on eight independently supported nearby visible fixtures')
    p.add_argument('--local', action='store_true', help='Separate local-surface study: 192 WU half extent and 128 WU partition blocks')
    p.add_argument('--receiver-path', action='store_true', help='Freeze source-to-native-opaque-receiver ROIs and fit that original receiver surface')
    p.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_aesthetic_tool.exe')
    p.add_argument('--geometry-tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_grid_geometry_tool.exe')
    p.add_argument('--solar-tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_solar_audit.exe')
    p.add_argument('--encoder-tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_bake_forward_tool.exe')
    a = p.parse_args()
    if a.local:
        POLICY.update(roi_half_extent=192, block_size=128, split_seed='asr24-local128-v1',
                      geometry_only_offset_alternatives=[17, 49, 81, 113])
    a.work = a.work.resolve()
    (prepare if a.phase == 'prepare' else evaluate)(a)


if __name__ == '__main__':
    main()
