"""Publish ASR25 aggregate receipts, never licensed coordinates, images or RGB rows."""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path

from .run import ROOT, load, sha, write


def cause(reason):
    text = reason.lower()
    for needle, label in (
        ('capture absent', 'MissingOriginalCapture'),
        ('static-map geometry', 'DynamicMapUnsupported'),
        ('work limit', 'CatalogueWorkBudget'),
        ('polygon limit', 'CataloguePolygonBudget'),
        ('overlapping, invalid or oversized section', 'CaptureSectionUnsupported'),
        ('256-fixture', 'FixtureCountBudget'),
        ('4096 original cells', 'GridQueryBudget'),
        ('numerical budget', 'FitNotConverged'),
        ('insufficient separated', 'ReceiverFoldCoverage'),
        ('opaque receiver', 'ReceiverHitUnavailable'),
        ('local configurations lack known forward coverage', 'UnknownForwardSupport'),
        ('nuisance-only', 'NuisanceOnlySelected'),
        ('held-out improvement', 'HeldoutGateFailed'),
        ('unknown', 'UnknownSupport'),
        ('wall-time budget', 'ProcessTimeBudget'),
        ('input scan/line budget', 'ReceiverScanBudget'),
    ):
        if needle in text:
            return label
    return 'OtherUnavailable'


def region_counts(region):
    """Explicit allowlist: no fixture XYZ, original pixels or ROI bounds escape."""
    selection = region.get('selection', {})
    result = {k:region[k] for k in ('name','status','reason','grid_known_static_supports',
              'grid_mover_independent_supports','repeat_identical') if k in region}
    result['selection'] = {k:selection[k] for k in ('available','counts','blocks',
        'qualified_roi_rows','minimum_fold_rows','forward_geometry_coverage') if k in selection}
    if region.get('baseline'):
        result['baseline'] = region['baseline']
    index = region.get('selected', -1)
    if index >= 0:
        c = region['configurations'][index]
        result['selected_fit'] = {k:c[k] for k in ('available','converged','training','validation','heldout',
                                                   'power_at_bound','background_at_bound') if k in c}
    if 'source_removal' in region:
        result['source_removal'] = region['source_removal']
    return result


def public_map(item, receipt, old):
    result = dict(map=item['map'], mode=item['mode'], bsp_sha256=item['sha256'],
                  bsp_bytes=item['bytes'], archive=Path(item['archive']).name, member=item['member'])
    for k in ('status','capture_available','capture_sha256','capture_bytes','capture_context','accepted_lights','model_presence',
              'historical_source_count','historical_xyz','absolute_si','unsupported_shapes',
              'mp_material_interpretation','catalogue','discovery','independent_roi_coverage',
              'accepted_source_coverage','stages','wall_seconds'):
        if k in receipt:
            result[k] = receipt[k]
    result['blockers'] = receipt['blockers']
    result['blocker_classes'] = sorted({cause(s) for s in receipt['blockers']})
    result['regions'] = [region_counts(r) for r in receipt.get('regions', [])]
    # Unavailable accepted coverage is null, never a zero-filled solution.
    if 'accepted_source_coverage' not in result:
        result['accepted_source_coverage'] = None
    census = old.get('census') or {}
    if census and old['bsp_sha256'] == item['sha256']:
        result['archived_observation_census'] = {k:census[k] for k in (
            'derived_texels','coverage','eligible_planar','source_candidates','moving_receivers',
            'unknown_transfer','unknown_primaries','mapped_area_world_squared') if k in census}
        result['archived_observation_census']['provenance'] = 'Preserved all_stock/results/v4; not a fresh capture or accepted-source result'
    return result


def collect(work, out):
    protocol, run = load(work/'protocol.json'), load(work/'summary.json')
    old_path = ROOT/'tools/rend2/all_stock/results/v4/maps.json'
    old = {r['map']:r for r in load(old_path)['maps']}
    receipts = {r['map']:r for r in run['maps']}
    maps = [public_map(item, receipts[item['map']], old.get(item['map'], {})) for item in protocol['inventory']['maps']]
    for r in maps:
        folder = work/'maps'/r['map'].replace('/','_')
        work_counts = dict(independent_roi_ray_primitive_tests=sum(x['ray_work'] for x in r.get('independent_roi_coverage',[])))
        if (folder/'discovery.json').exists():
            candidates = load(folder/'discovery.json')['candidates']
            work_counts.update(discovery_logical_point_checks=len(candidates),
                               discovery_logical_segment_checks=sum(len(c['supports']) for c in candidates))
        fits = [load(p) for p in folder.glob('fit/*/result.json')]
        r['numerically_fitted_regions'] = len(fits)
        rois = r.get('independent_roi_coverage', [])
        solar = [load(p) for p in folder.glob('fit/*/solar.json')]
        r['sky_sun'] = dict(has_known_upward_sky_case=any(x['upward_sky_reached'] for x in rois) if rois else None,
            upward_sky_reached=sum(x['upward_sky_reached'] for x in rois) if rois else None,
            upward_sky_blocked=sum(x['upward_sky_blocked'] for x in rois) if rois else None,
            upward_sky_unknown=sum(x['upward_sky_unknown'] for x in rois) if rois else None,
            count_scope='Separate bounded ROI samples, possibly overlapping; not unique map-wide coverage',
            native_solar_states=dict(Counter(x['state'] for x in solar)),
            numeric_sun='Unavailable: this audit does not accept or publish a numeric sun',
            historical_direction='Unknown')
        work_counts['native_fit_work_one_pass'] = sum(x['work'] for x in fits)
        work_counts['native_fit_work_with_identical_repeat'] = 2*work_counts['native_fit_work_one_pass']
        work_counts['scope'] = 'Separate work units; do not add logical queries, primitive tests and fitter work. Native rays may be cached. Catalogue reports caps rather than actual work; ablation work not serialized.'
        r['work'] = work_counts
    if len(maps) != 57 or len({r['map'] for r in maps}) != 57:
        raise ValueError('Missing or repeated stock map receipts')
    reasons = Counter(c for r in maps for c in r['blocker_classes'])
    reasons.update(cause(region['reason']) for r in maps for region in r['regions'] if region['status'] != 'AestheticCandidate')
    summary = dict(schema='org.openjk.stock-aesthetic-audit.v1',
        base_commit=protocol['inventory']['base_commit'], inventory_maps=57,
        modes=dict(Counter(r['mode'] for r in maps)), current_capture_files=sum(r['capture_available'] for r in maps),
        independent_roi_maps=sum('independent_roi_coverage' in r for r in maps),
        independent_rois=sum(len(r.get('independent_roi_coverage', [])) for r in maps),
        maps_with_known_upward_sky_case=sum(r['sky_sun']['has_known_upward_sky_case'] is True for r in maps),
        statuses=dict(Counter(r['status'] for r in maps)), blocker_and_rejection_classes=dict(reasons),
        accepted_maps=[r['map'] for r in maps if r['accepted_lights']],
        accepted_bounded_configurations=sum(r['accepted_lights'] or 0 for r in maps),
        selected_regions=sum(r.get('discovery',{}).get('selected_components',0) for r in maps),
        numerically_fitted_regions=sum(r['numerically_fitted_regions'] for r in maps),
        accepted_counts_unavailable_maps=sum(r['accepted_lights'] is None for r in maps),
        elapsed_seconds=run['elapsed_seconds'], summed_map_seconds=run['summed_map_seconds'],
        stage_seconds={stage:sum(r['stages'].get(stage,{}).get('seconds',0) for r in maps)
                       for stage in ('roi','catalogue','discovery','scan','prepare','evaluate')},
        work={key:sum(r['work'].get(key,0) for r in maps) for key in (
            'independent_roi_ray_primitive_tests','discovery_logical_point_checks','discovery_logical_segment_checks',
            'native_fit_work_one_pass','native_fit_work_with_identical_repeat')},
        substantive_multi_map_demonstration=len([r for r in maps if r['accepted_lights']]) > 1,
        source_count_truth='Unknown', exact_historical_positions='Unknown', absolute_si='Unknown', unsupported_shapes='Unavailable',
        historical_source_certified=False, canonical_assets_written=False, runtime_changed=False,
        protocol_sha256=sha(work/'protocol.json'), policy_sha256=sha(Path(__file__).with_name('policy-v1.json')),
        archived_census_sha256=sha(old_path), private_summary_sha256=sha(work/'summary.json'),
        inputs_unchanged=run['inputs_unchanged'],
        caveats=['Accepted lights counts bounded conditional configurations; duplicate/overlapping hypotheses are not historical lamp counts',
                 'Independent ROI observation coverage does not measure accepted-source coverage or source recall',
                 'Each ROI count is separate; overlap is not a unique summed denominator',
                 'Sky reach diagnostics do not establish a numeric sun or an exact historical direction',
                 'Dynamic maps and external-model source discovery remain explicitly unsupported by this frozen path',
                 'MP uses qualified SP material interpretation; encoded RGB8 is not calibrated irradiance'])
    out.mkdir(parents=True, exist_ok=False)
    write(out/'summary.json', summary)
    write(out/'maps.json', dict(schema='org.openjk.stock-aesthetic-audit-maps.v1', maps=maps))
    write(out/'protocol.json', dict(policy=protocol['policy'], implementation=protocol['implementation'],
        tools={Path(path).name:h for path,h in protocol['tools'].items()}, frozen_before_discovery=True,
        inventory=[{k:r[k] for k in ('map','mode','sha256','bytes','member')} for r in protocol['inventory']['maps']]))
    if (work/'verification.json').exists():
        write(out/'checks.json', load(work/'verification.json'))
    fields = ('map','mode','capture','mapped_archived','roi_count','fixtures','candidates','supported','selected','fitted',
              'accepted','accepted_receivers','accepted_heldout','status','causes','seconds')
    rows = []
    for r in maps:
        accepted = r.get('accepted_source_coverage') or {}
        discovery = r.get('discovery') or {}
        rows.append(dict(map=r['map'], mode=r['mode'], capture=r['capture_available'],
             mapped_archived=(r.get('archived_observation_census',{}).get('coverage') or [None]*3)[2],
             roi_count=len(r.get('independent_roi_coverage',[])), fixtures=r.get('catalogue',{}).get('counts',{}).get('fixtures'),
             candidates=discovery.get('eligible_components'), supported=discovery.get('grid_supported_components'),
             selected=discovery.get('selected_components'), fitted=r['numerically_fitted_regions'], accepted=r['accepted_lights'],
             accepted_receivers=accepted.get('unique_fitted_receivers'), accepted_heldout=accepted.get('unique_heldout_receivers'),
             status=r['status'], causes='; '.join(r['blocker_classes']+sorted({cause(x['reason']) for x in r['regions'] if x['status'] != 'AestheticCandidate'})),
             seconds=round(r['wall_seconds'],3)))
    with (out/'maps.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f,fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    lines = ['| Map | Current capture / ROIs | Fixtures / candidates / supported | Selected / fitted / accepted | Accepted receivers / held-out | Outcome / causes |',
             '|---|---:|---:|---:|---:|---|']
    def value(v): return 'Unavailable' if v is None else str(v)
    for r in rows:
        lines.append('| `'+r['map']+'` | '+('yes' if r['capture'] else 'absent')+' / '+str(r['roi_count'])+
            ' | '+' / '.join(value(r[k]) for k in ('fixtures','candidates','supported'))+
            ' | '+' / '.join(value(r[k]) for k in ('selected','fitted','accepted'))+
            ' | '+' / '.join(value(r[k]) for k in ('accepted_receivers','accepted_heldout'))+
            ' | '+r['status']+('; '+r['causes'] if r['causes'] else '')+' |')
    (out/'maps.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    write(out/'manifest.json', dict(files={p.name:sha(p) for p in out.iterdir() if p.is_file()},
        exporter_sha256=sha(Path(__file__))))
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a = p.parse_args()
    collect(a.work,a.out)
