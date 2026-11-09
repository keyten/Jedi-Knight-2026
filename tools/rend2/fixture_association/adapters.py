"""Read-only adapters for ASR09/11 fixtures, ASR14 patches and ASR12/13 grid.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import math

from .associate import POLICY as LIMITS, norm, sub, vector

EVIDENCE_SCHEMA = 'org.openjk.fixture-association-evidence.v1'


def fixtures(catalogue, roi=None):
    schema = catalogue.get('schema')
    if schema not in ('org.openjk.research.bsp-fixtures/1', 'org.openjk.research.md3-emission/1'):
        raise ValueError('Unsupported fixture catalogue')
    if len(catalogue['fixtures']) > 100000:
        raise ValueError('ASR15 fixture catalogue scan limit')
    if roi is not None and (len(roi) != 6 or any(not math.isfinite(v) for v in roi) or
                           any(roi[k] >= roi[k+3] for k in range(3))):
        raise ValueError('Invalid fixture ROI')
    regions = {r['id']: r for r in catalogue.get('regions', [])}
    result = []
    for f in catalogue['fixtures']:
        position = f['centroid']
        vector(position, 'catalogue fixture position')
        if roi is not None and not all(roi[k] <= position[k] <= roi[k+3] for k in range(3)):
            continue
        if len(result) >= LIMITS['max_fixtures']:
            raise ValueError('ASR15 fixture adapter limit; select an explicit spatial ROI')
        if schema.endswith('bsp-fixtures/1'):
            polygons = f['footprint'] if f['emitting_footprint_known'] else []
            layers = f['layers']
            # Unresolved overlapping composition must not acquire a colour prior.
            colour = layers[0]['colour_prior'] if len(layers) == 1 else None
            # Older ASR09 exports omit coordinate_space; only BSP model zero is
            # attested world geometry. Unplaced inline models remain ineligible.
            eligible = f['static_eligible'] and f.get('coordinate_space', 'world' if f['model'] == 0 else 'unplaced') == 'world'
        else:
            polygons = [p for rid in f['region_ids'] for p in regions[rid]['footprint']]
            colour, eligible = f['colour_prior'], f['static_eligible']
        points = [p for poly in polygons for p in poly]
        radius = max(norm(sub(p, position)) for p in points) if points else None
        result.append(dict(id=f['id'], position=position,
                           outward=None if f.get('two_sided') else f['outward'],
                           colour=colour, radius_world=radius, static_eligible=eligible,
                           uncertainty=sorted(f['issues']+['Fixture catalogue is a visible-emission hypothesis',
                               'A physical placement supplies its own conditional source; asset-template identity is not a shared lamp'])))
    return result


def patches(report, rows=()):
    if report.get('schema') != 'org.openjk.lightmap-patches.v1':
        raise ValueError('Unsupported patch report')
    if len(report['patches']) > LIMITS['max_observations']:
        raise ValueError('ASR15 patch adapter limit')
    # Original exporter IDs supply codes/material/style. Validate through ASR14,
    # including unsupported mapping and normal masks, before using dark controls.
    from tools.rend2.lightmap_patches.extract import POLICY, prepare
    groups, rejected, _ = prepare(rows, POLICY)
    receivers = {r['id']: r for group in groups.values() for r in group}
    if len(receivers)*len(report['patches']) > LIMITS['max_work']:
        raise ValueError('ASR15 dark-control scan limit; select an explicit spatial ROI')
    result = []
    sample_count, dark_count = 0, 0
    for p in report['patches']:
        if p['colour_domain'] != 'OriginalEncodedRGB8_NoIrradianceCalibration':
            raise ValueError('Patch colour domain is not original encoded RGB8')
        samples = []
        for g in sorted(p['gradients']['samples'], key=lambda g: g['receiver']):
            t = g['tangent_codes_per_world']
            if g['local_rms_codes'] > max(2, math.hypot(*t)*p['receiver_pitch_world']*.4):
                continue
            direction = [p['frame']['u'][k]*t[0]+p['frame']['v'][k]*t[1] for k in range(3)]
            row = receivers.get(g['receiver'])
            if row and row['saturated']:
                continue
            samples.append(dict(id=g['receiver'], position=g['position_world'],
                normal=p['normal_world'], gradient=direction, incoming=None,
                colour=p['excess_colour_direction'], rgb8=row['raw'] if row else None,
                material=p['chart']['material'], style=p['chart']['style'],
                correlation_key=p['id']))
        if not samples:
            samples.append(dict(id=p['peak_receiver'], position=p['maximum_world'],
                normal=p['normal_world'], gradient=None, incoming=None,
                colour=p['excess_colour_direction'], rgb8=p['peak_rgb8'],
                material=p['chart']['material'], style=p['chart']['style'], correlation_key=p['id']))
        sample_count += len(samples)
        if sample_count > LIMITS['max_samples']:
            raise ValueError('ASR15 patch support limit; select an explicit spatial ROI')
        c = p['constraint']
        projection = dict(foot=c['foot_world'], normal=c['normal_world'], radius_world=c['radius_world']) if c['state'] == 'Conditional' else None
        # The CLI's input/work budgets bound this geometry-only control selection.
        support_ids = {s['id'] for s in samples}
        dark = [dict(id=r['id'], position=r['position'], normal=r['normal'],
                     rgb8=r['raw'], material=r['material'], style=r['style'])
                for r in receivers.values() if r['id'] not in support_ids and
                r['material'] == p['chart']['material'] and r['style'] == p['chart']['style'] and
                max(r['raw']) <= 32 and norm(sub(r['position'], p['centroid_world'])) <=
                64+3*max(p['extent_sigma_world'])]
        dark_count += len(dark)
        if dark_count > LIMITS['max_dark_samples']:
            raise ValueError('ASR15 dark support limit; select an explicit spatial ROI')
        result.append(dict(id=p['id'], samples=samples, dark_samples=sorted(dark, key=lambda r: r['id']),
                           projection=projection, uncertainty=p['uncertainty']+
                           ['Patch luxels share a correlation group; they are not independent source votes']))
    return result, rejected


def grid(rows, interpretation='consumer256_unknown'):
    """ASR13 bridge original cells, not generated probes or triangulated answers.

    A single region retains spatially distinct supports, allowing several source
    fixtures to explain it. Shared source records share one normalized vote.
    """
    if interpretation not in ('consumer256_unknown', 'writer255_merge4'):
        raise ValueError('Explicit supported grid interpretation required')
    result, rejected = [], {}
    for r in sorted(rows, key=lambda r: r['ordinal']):
        reason = None
        if not r['present']:
            reason = 'missing_original_record'
        elif r['issues'] & 64:
            reason = 'malformed_styles'
        elif r['space'] != 'free' or r['boundary']:
            reason = 'unknown_or_nonfree_grid_geometry'
        elif r.get('dynamic_possible'):
            reason = 'dynamic_grid_occupancy_unknown'
        elif len(r['styles']) != 1 or r['styles'][0]['weak']:
            reason = 'aggregate_direction_multiple_styles_or_weak'
        elif interpretation == 'writer255_merge4' and not r['writer_possible']:
            reason = 'writer_interpretation_impossible'
        if reason:
            rejected[reason] = rejected.get(reason, 0)+1
            continue
        style = r['styles'][0]
        result.append(dict(id='grid/'+str(r['ordinal']), position=r['position'], normal=None,
            gradient=None, incoming=r['writer255' if interpretation == 'writer255_merge4' else 'consumer256'],
            colour=style['directed'], rgb8=None, correlation_key=r['correlation_key']))
    return ([dict(id='original-grid/'+interpretation, samples=result, dark_samples=[], projection=None,
                  uncertainty=['Original grid aggregate incoming direction; multiple source/indirect mixtures possible',
                               'Direction interpretation: '+interpretation,
                               'Shared lightgrid records are one correlation group'])] if result else []), rejected


class Geometry:
    """Conservative identity-checked ASR13 bridge. No endpoint pushes invented."""
    def __init__(self, tool, capture, map_sha256):
        from tools.rend2.grid_triangulation.triangulate import Bridge
        self.bridge = Bridge(tool, capture)
        self.metadata = self.bridge.metadata
        if self.metadata.get('map_sha256') != map_sha256:
            self.close()
            raise ValueError('Fixture/observation/geometry map identity mismatch')

    def ray(self, a, b):
        r = self.bridge.ray(a, b)
        return 'unknown' if r['dynamic_possible'] else r['static_state']

    def close(self):
        self.bridge.close()
