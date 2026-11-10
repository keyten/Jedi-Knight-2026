"""ASR25: freeze, run and collect the same bounded discovery operations on all 57 maps.

Coordinates, original RGB and licensed geometry stay in ignored work directories.
Public export is an explicit allowlist of counts, reasons, losses and hashes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

from tools.rend2.all_stock.inventory import inventory
from tools.rend2.bsp_fixtures.catalogue import Assets, read_bsp
from tools.rend2.lightmap_patches.run import sha, write

ROOT = Path(__file__).resolve().parents[3]
POLICY = Path(__file__).with_name('policy-v1.json')


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def roi_design(data, identity, count):
    points = []
    for s in read_bsp(data):
        if s['model'] != 0 or s['kind'] != 1 or not s['vertices']:
            continue
        centre = [sum(v['p'][k] for v in s['vertices'])/len(s['vertices']) +
                  4*s['plane_normal'][k] for k in range(3)]
        key = hashlib.sha256((identity+'/'+str(s['ordinal'])).encode()).hexdigest()
        points.append(dict(centre=centre, surface=s['ordinal'], key=key))
    chosen = []
    if points:
        chosen.append(min(points, key=lambda p: p['key']))
    while points and len(chosen) < min(count, len(points)):
        remaining = [p for p in points if p not in chosen]
        def distance(p):
            return min(sum((a-b)**2 for a,b in zip(p['centre'], c['centre'])) for c in chosen)
        chosen.append(min(remaining, key=lambda p: (-distance(p), p['key'])))
    return chosen


def invoke(command, folder, stage, seconds, records):
    """Bound the entire subprocess tree, retaining stderr and wall-work receipts."""
    start = time.monotonic()
    with (folder/(stage+'.stdout')).open('w', encoding='utf-8') as out, (folder/(stage+'.stderr')).open('w', encoding='utf-8') as err:
        child = subprocess.Popen([str(x) for x in command], stdout=out, stderr=err, cwd=ROOT)
        try:
            code = child.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True)
            else:
                child.kill()
            child.wait()
            records[stage] = dict(seconds=time.monotonic()-start, status='BudgetExceeded')
            raise RuntimeError(stage+' exceeded frozen wall-time budget')
    records[stage] = dict(seconds=time.monotonic()-start, status='Completed' if code == 0 else 'Failed', exit_code=code)
    if code:
        reason = (folder/(stage+'.stderr')).read_text(encoding='utf-8').strip().splitlines()
        raise RuntimeError((reason[-1] if reason else stage+' failed')[:1000])
    return (folder/(stage+'.stdout')).read_text(encoding='utf-8')


def private_map(a, item, captures, policy):
    start = time.monotonic()
    name, key = item['map'], item['map'].replace('/', '_')
    folder = a.work/'maps'/key
    folder.mkdir(parents=True, exist_ok=True)
    receipt = dict(map=name, mode=item['mode'], bsp_sha256=item['sha256'], archive=Path(item['archive']).name,
                   stages={}, blockers=[], accepted_lights=None, status='Unresolved',
                   historical_source_count='Unknown', historical_xyz='Unknown', absolute_si='Unknown',
                   unsupported_shapes='Unavailable', historical_source_certified=False)
    stages = receipt['stages']
    limits = policy['limits_seconds']
    capture = Path(captures.get(name, {}).get('dataset', 'missing'))
    receipt['capture_available'] = capture.is_file()
    receipt['capture_context'] = captures.get(name, {}).get('material_context', 'Reused SP evidence; identity checked by native tools')
    if item['mode'] == 'mp':
        receipt['mp_material_interpretation'] = 'SP capture; exact MP runtime semantics Unknown'
    def command(args, stage):
        return invoke(args, folder, stage, limits[stage], stages)
    assets = Assets(a.base)
    try:
        data, _ = assets.read(item['member'])
        if data is None or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Original stock BSP identity mismatch')
        rois = roi_design(data, item['sha256'], policy['independent_rois']['count'])
        # Entity/model counts are presence diagnostics, not a geometry certificate.
        offset, length = struct.unpack_from('<ii', data, 8)
        entities = data[offset:offset+length].decode('latin1')
        import re
        models = re.findall(r'"model"\s+"([^"\r\n]+)"', entities, re.I)
        external = [m for m in models if not m.startswith('*')]
        receipt['model_presence'] = dict(brush_references=sum(m.startswith('*') for m in models),
                                        external_references=len(external), external_assets=len(set(external)),
                                        discovery_support='External model lamps are not covered by ASR24 BSP-only discovery')
        write(folder/'roi-design.json', dict(map_sha256=item['sha256'], original_rgb_read=False, rois=rois))
    except Exception as error:
        receipt['blockers'].append('Input: '+str(error))
        data = None
        rois = []
    finally:
        assets.close()
    if capture.is_file():
        receipt['capture_sha256'] = sha(capture)
        receipt['capture_bytes'] = capture.stat().st_size
        try:
            if not rois:
                raise ValueError('No eligible original world planar ROI anchors')
            args = [a.bin/'lightrecon_dataset_roi.exe', capture]
            for r in rois:
                args += [*r['centre'], policy['independent_rois']['half_extent']]
            result = json.loads(command(args, 'roi'))
            if result['map_sha256'] != item['sha256']:
                raise ValueError('ROI/capture identity mismatch')
            write(folder/'roi-coverage.json', result)
            receipt['independent_roi_coverage'] = [{k:v for k,v in r.items() if k != 'centre'} for r in result['rois']]
        except Exception as error:
            receipt['blockers'].append('IndependentROI: '+str(error))
    else:
        receipt['blockers'].append('Input: licensed original observation capture absent')
    cat_folder = a.work/'catalogues'/key
    try:
        command([sys.executable, '-X', 'utf8', '-m', 'tools.rend2.bsp_fixtures.catalogue',
                 '--base', a.base, '--map', item['member'], '--mask-axis', 32,
                 '--max-work', 1000000, '--output', cat_folder], 'catalogue')
        cat = load(cat_folder/'catalogue.json')
        if cat['map_sha256'] != item['sha256']:
            raise ValueError('Catalogue identity mismatch')
        receipt['catalogue'] = dict(counts=cat['counts'], sha256=sha(cat_folder/'catalogue.json'),
                                    issues=dict(Counter(i for f in cat['fixtures'] for i in f['issues'])))
        if not capture.is_file():
            raise FileNotFoundError('Original capture absent')
        command([sys.executable, '-X', 'utf8', '-m', 'tools.rend2.stock_aesthetic.discover',
                 '--catalogue', cat_folder/'catalogue.json', '--capture', capture,
                 '--tool', a.bin/'lightrecon_grid_geometry_tool.exe', '--out', folder/'discovery.json'], 'discovery')
        discovery = load(folder/'discovery.json')
        candidates = discovery['candidates']
        selected = [c for c in candidates[:3] if c['agreeing_distinct_records'] >= 3]
        receipt['discovery'] = dict(eligible_components=len(candidates),
                                    grid_supported_components=sum(c['agreeing_distinct_records'] >= 3 for c in candidates),
                                    selected_components=len(selected), sha256=sha(folder/'discovery.json'),
                                    rejection_causes=dict(Counter('Source support not free' if c['point']['space'] != 'free' or c['point']['boundary']
                                                               else 'Fewer than three independent static grid supports'
                                                               for c in candidates if c['agreeing_distinct_records'] < 3)))
        if not selected:
            receipt['accepted_lights'] = 0
            receipt['status'] = 'NoSupportedCandidate'
        else:
            prefix = a.work/'rows'/key
            prefix.parent.mkdir(parents=True, exist_ok=True)
            command([a.bin/'lightrecon_dataset_quality.exe', 'scan', capture, prefix], 'scan')
            if load(prefix.with_suffix('.source.json'))['map_sha256'] != item['sha256']:
                raise ValueError('Receiver scan identity mismatch')
            # Compress without changing a single receiver, then retain for replay.
            raw = prefix.with_suffix('.rows.jsonl')
            zipped = prefix.with_suffix('.rows.jsonl.gz')
            with raw.open('rb') as source, gzip.open(zipped, 'wb', compresslevel=1) as target:
                shutil.copyfileobj(source, target)
            raw.unlink()
            manifest = folder/'inputs.json'
            write(manifest, {key: dict(source=str(prefix.with_suffix('.source.json').resolve()),
                                     rows=str(zipped.resolve()), capture=str(capture.resolve()))})
            cohort = folder/'fit'
            args = [sys.executable, '-X', 'utf8', '-m', 'tools.rend2.stock_aesthetic.run',
                    '--work', cohort, '--base', a.base, '--catalogues', a.work/'catalogues',
                    '--discovery', folder/'discovery.json', '--all-stock-inputs', manifest,
                    '--tool', a.bin/'lightrecon_aesthetic_tool.exe',
                    '--geometry-tool', a.bin/'lightrecon_grid_geometry_tool.exe',
                    '--solar-tool', a.bin/'lightrecon_solar_audit.exe',
                    '--encoder-tool', a.bin/'lightrecon_bake_forward_tool.exe',
                    '--local', '--common-normals', '--receiver-path']
            command([*args, 'prepare'], 'prepare')
            command([*args, 'evaluate'], 'evaluate')
            result = load(cohort/'summary.json')
            receipt['regions'] = result['regions']
            accepted = [r for r in result['regions'] if r['status'] == 'AestheticCandidate']
            receipt['accepted_lights'] = len(accepted)
            receipt['status'] = 'AestheticCandidate' if accepted else 'NoAcceptedCandidate'
            ids, heldout = set(), set()
            for r in accepted:
                folds = load(cohort/r['name']/'folds.json')
                ids.update(x['id'] for rows in folds.values() for x in rows)
                heldout.update(x['id'] for x in folds['heldout'])
            receipt['accepted_source_coverage'] = dict(unique_fitted_receivers=len(ids), unique_heldout_receivers=len(heldout),
                 scope='Unique original samples in accepted bounded ROIs only; geometric presence elsewhere is not accepted coverage',
                 whole_map_source_recall=None)
    except Exception as error:
        receipt['blockers'].append('DiscoveryPipeline: '+str(error))
        receipt['status'] = 'Blocked' if receipt['accepted_lights'] is None else 'Unresolved'
    receipt['wall_seconds'] = time.monotonic()-start
    write(folder/'receipt.json', receipt)
    print(name, receipt['status'], receipt['accepted_lights'], receipt['blockers'], flush=True)
    return receipt


def implementation():
    paths = [Path(__file__).resolve(), POLICY]
    from tools.rend2.stock_aesthetic.run import sources
    return dict(sources(), **{p.relative_to(ROOT).as_posix(): sha(p) for p in paths})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--base', type=Path, default=ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets/base')
    p.add_argument('--stock-private', type=Path, default=Path(os.environ['TEMP'])/'jk-lightrecon-all-stock-20261031')
    p.add_argument('--bin', type=Path, default=ROOT/'build/s06-gcc')
    a = p.parse_args()
    for field in ('work', 'base', 'stock_private', 'bin'):
        setattr(a, field, getattr(a, field).resolve())
    a.work.mkdir(parents=True, exist_ok=False)
    policy = load(POLICY)
    inv = inventory(a.base, subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip())
    if dict(Counter(x['mode'] for x in inv['maps'])) != {k:policy['inventory'][k] for k in ('sp','mp')}:
        raise ValueError('Expected exactly the frozen 34 SP / 23 MP stock inventory')
    if sorted(p.name for p in a.base.iterdir()) != sorted(policy['inventory']['archives']):
        raise ValueError('Use an assets0-3-only licensed input view')
    captures = load(a.stock_private/'captures.json')
    tools = {str(path): sha(path) for path in a.bin.glob('lightrecon_*.exe') if path.name in (
        'lightrecon_dataset_roi.exe','lightrecon_dataset_quality.exe','lightrecon_aesthetic_tool.exe',
        'lightrecon_grid_geometry_tool.exe','lightrecon_solar_audit.exe','lightrecon_bake_forward_tool.exe')}
    frozen = dict(policy=policy, inventory=inv, implementation=implementation(), tools=tools,
                  captures_manifest_sha256=sha(a.stock_private/'captures.json'), frozen_before_discovery=True)
    write(a.work/'protocol.json', frozen)
    for path in frozen['implementation']:
        target = a.work/'implementation'/path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT/path, target)
    start = time.monotonic()
    with ThreadPoolExecutor(max_workers=policy['parallel_maps']) as pool:
        futures = [pool.submit(private_map, a, item, captures, policy) for item in inv['maps']]
        results = [f.result() for f in as_completed(futures)]
    if implementation() != frozen['implementation'] or any(sha(Path(path)) != h for path,h in tools.items()):
        raise ValueError('Frozen implementation/tools changed during audit')
    write(a.work/'summary.json', dict(maps=sorted(results,key=lambda r:r['map']),
          elapsed_seconds=time.monotonic()-start, summed_map_seconds=sum(r['wall_seconds'] for r in results),
          protocol_sha256=sha(a.work/'protocol.json'), inputs_unchanged=True))


if __name__ == '__main__':
    main()
