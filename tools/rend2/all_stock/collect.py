#!/usr/bin/env python3
"""Publish aggregate audit metrics only; no BSPs, lightmaps or geometry.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import collections
import csv
import json
from pathlib import Path
from architecture import audit
from run import sha, write


def load(path):
    return json.loads(path.read_text()) if path.exists() else None


def verdict(result):
    profiles = result.get('profiles', {})
    if not profiles:
        return 'NotEvaluated'
    if any(v.get('producer', {}).get('state') == 'Completed' for v in profiles.values()):
        return 'ConditionalFitOnly'
    return 'NoAcceptedFit'


def collect(private, out, revision='v2', attempted_only=False):
    out.mkdir(parents=True, exist_ok=True)
    inventory = load(private / 'inventory.json')
    captures = load(private / 'captures.json')
    runs = load(private / ('runs-' + revision + '.json')) or {}
    rows, flat = [], []
    for item in inventory['maps']:
        name = item['map']
        if attempted_only and name not in runs:
            continue
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        result = runs.get(name, dict(state='NotAttempted'))
        capture = captures.get(name, {})
        # Strip host paths/commands. Timing/RSS is explicitly nondeterministic.
        resources = result.get('resources', {})
        r = dict(map=name, mode=item['mode'], bsp_sha256=item['sha256'], bsp_bytes=item['bytes'],
                 archive=Path(item['archive']).name, member=item['member'],
                 capture=dict(state=capture.get('status'), seconds=capture.get('seconds'),
                              peak_process_rss_bytes=capture.get('peak_process_rss_bytes'),
                              material_context=capture.get('material_context', 'reused original capture; see X02/X06')),
                 run_state=result['state'], reconstruction_verdict=verdict(result),
                 dataset_bytes=result.get('dataset_bytes'),
                 wall_seconds=resources.get('wall_seconds'), peak_process_rss_bytes=resources.get('peak_process_rss_bytes'),
                 native_exit_code=resources.get('exit_code'),
                 census=load(folder/'census.json'), geometry=load(folder/'geometry.json'),
                 folds=load(folder/'folds.json'), geometry_rays=load(folder/'ray-summary.json'),
                 selection_scope=load(folder/'selection-scope.json'),
                 profiles=result.get('profiles', {}),
                 physical_truth=dict(emitter_precision=None, emitter_recall=None, source_xyz_error=None,
                                     source_type_accuracy=None, physical_intensity_error=None,
                                     reason='No authoritative original stock sources/photometry'),
                 historical_profile='UnknownProfile')
        if result.get('reason'):
            r['failure'] = result['reason']
        for profile, value in r['profiles'].items():
            areas = folder / (profile + '-hidden-areas.tsv')
            if areas.exists():
                with areas.open(newline='') as f:
                    value['hidden_area_diagnostics'] = [
                        {k: int(v) for k, v in row.items()}
                        for row in csv.DictReader(f, delimiter='\t')]
            value['work_scope'] = 'Joint counters and hidden-provider counters are separate; solar work not serialized by this adapter'
        rows.append(r)
        c, g = r['census'] or {}, r['geometry_rays'] or {}
        profiles = [v.get('producer', v) for v in r['profiles'].values()]
        flat.append(dict(map=name, pages=c.get('raw_colour_pages'), texels=c.get('derived_texels'),
                         mapped=(c.get('coverage') or [None]*5)[2], eligible=c.get('eligible_planar'),
                         mapped_area=c.get('mapped_area_world_squared'),
                         candidate_evidence=c.get('source_candidates'),
                         train=(c.get('selected') or [None]*3)[0],
                         validation=(c.get('selected') or [None]*3)[1],
                         heldout=(c.get('selected') or [None]*3)[2],
                         grid_indices=c.get('grid_indices'), region_assigned=c.get('region_assigned'),
                         rays_visible=g.get('visible'), rays_blocked=g.get('blocked'), rays_unknown=g.get('unknown'),
                         result=r['reconstruction_verdict'],
                         profile_states='; '.join(p.get('state', '') for p in profiles),
                         reasons='; '.join(sorted(set(p.get('reason', '') for p in profiles))),
                         wall_seconds=r['wall_seconds'], peak_rss_bytes=r['peak_process_rss_bytes']))
    profile_reasons = collections.Counter(p.get('producer', p).get('reason', 'Unspecified')
                                         for row in rows for p in row['profiles'].values())
    summary = dict(schema='org.openjk.all-stock-audit.v2', base_commit=inventory['base_commit'],
                   adapter_revision=revision, inventory_maps=len(inventory['maps']), reported_maps=len(rows),
                   attempted_maps=len(runs), captured_maps=sum(bool(c.get('dataset')) for c in captures.values()),
                   modes=dict(collections.Counter(r['mode'] for r in rows)),
                   verdicts=dict(collections.Counter(r['reconstruction_verdict'] for r in rows)),
                   profile_stop_reasons=dict(profile_reasons),
                   mapped_luxels=sum((r['census'] or {}).get('coverage', [0]*5)[2] for r in rows),
                   raw_texels=sum((r['census'] or {}).get('derived_texels', 0) for r in rows),
                   source_evidence_candidates=sum((r['census'] or {}).get('source_candidates', 0) for r in rows),
                   caveats=['Attempted does not mean solved; early failures have null numerical prediction metrics',
                            'Full-map census, bounded selected observations; no full-luxel joint solve claim',
                            'Original compiler, transfer, primaries and SI scale remain unknown',
                            'Geometry-only fixture rays are not fitted-source validation',
                            'Region counts cover captured ROIs only',
                            'Performance is actual OS process RSS/wall time, outside deterministic numerical witnesses'],
                   architecture=audit(), policy=load(private/('policy-'+revision+'.json')))
    write(out/'summary.json', summary)
    write(out/'maps.json', dict(schema='org.openjk.all-stock-maps.v2', maps=rows))
    with (out/'maps.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(flat[0]))
        writer.writeheader(); writer.writerows(flat)
    # Geometry/readiness table is separate from actual fitting outcomes.
    lines = ['| Map | Pages | Mapped / raw | Eligible planar | Train / val / held-out | Fixture rays V / B / U | Native s / MiB |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for r in flat:
        c = next(v['census'] for v in rows if v['map'] == r['map']) or {}
        val = lambda v: '—' if v is None else f'{v:,}'
        wall = '—' if r['wall_seconds'] is None else f"{r['wall_seconds']:.1f}"
        rss = '—' if r['peak_rss_bytes'] is None else f"{r['peak_rss_bytes']/1024**2:.1f}"
        lines.append(f"| `{r['map']}` | {val(r['pages'])} | {val(r['mapped'])} / {val(r['texels'])} | {val(r['eligible'])} | {val(r['train'])} / {val(r['validation'])} / {val(r['heldout'])} | {val(r['rays_visible'])} / {val(r['rays_blocked'])} / {val(r['rays_unknown'])} | {wall} / {rss} |")
    (out/'coverage-table.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print(json.dumps({k: summary[k] for k in ('attempted_maps', 'inventory_maps', 'verdicts', 'profile_stop_reasons')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--private', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--revision', default='v2')
    parser.add_argument('--attempted-only', action='store_true')
    args = parser.parse_args()
    collect(args.private, args.out, args.revision, args.attempted_only)
