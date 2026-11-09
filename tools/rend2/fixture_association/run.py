"""Associate visible fixture hypotheses with independent original observations.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import math
from pathlib import Path

from . import adapters
from .associate import associate
from tools.rend2.lightmap_patches.run import read_rows, sha, write


def read(path):
    if Path(path).stat().st_size > 64*1024*1024:
        raise ValueError('ASR15 JSON input byte limit (64 MiB)')
    return json.loads(Path(path).read_text(encoding='utf-8'))


def run(catalogues, output, patch_file=None, rows_file=None, evidence=None,
        geometry_tool=None, capture=None, grid_roi=None, interpretation='consumer256_unknown', fixture_roi=None):
    if len(catalogues) > 16:
        raise ValueError('ASR15 catalogue file-count limit')
    if fixture_roi is not None and not catalogues:
        raise ValueError('Fixture centroid ROI applies to catalogues; no catalogue supplied')
    if bool(geometry_tool) != bool(capture) or (grid_roi is not None and not capture):
        raise ValueError('Geometry tool/capture required together; original grid ROI requires capture')
    if rows_file and not patch_file:
        raise ValueError('Original rows require patch report with pinned row identity')
    if grid_roi is not None and (len(grid_roi) != 6 or
            any(not math.isfinite(v) for v in grid_roi) or
            any(grid_roi[k] >= grid_roi[k+3] for k in range(3))):
        raise ValueError('Invalid original-grid ROI')
    fs, observations, identities, inputs, diagnostics = [], [], [], [], {}
    for path in catalogues:
        data = read(path)
        fs.extend(adapters.fixtures(data, fixture_roi))
        identities.append(data['map_sha256'])
        inputs.append(dict(role='fixture_catalogue', sha256=sha(path), schema=data['schema']))
    if patch_file:
        data = read(patch_file)
        identities.append(data['input']['map_sha256'])
        if rows_file and sha(rows_file) != data['input']['rows_sha256']:
            raise ValueError('Original rows differ from patch source hash')
        os, rejected = adapters.patches(data, read_rows(rows_file, data['input'].get('surfaces', []),
                                       data['input'].get('roi_world')) if rows_file else ())
        observations.extend(os)
        diagnostics['receiver_rejections'] = rejected
        inputs.append(dict(role='patches', sha256=sha(patch_file), schema=data['schema']))
        if rows_file:
            inputs.append(dict(role='original_rows', sha256=sha(rows_file)))
    if evidence:
        data = read(evidence)
        if data.get('schema') != adapters.EVIDENCE_SCHEMA:
            raise ValueError('Unsupported normalized association evidence schema')
        identities.append(data['map_sha256'])
        observations.extend(data['observations'])
        fs.extend(data.get('fixtures', []))
        inputs.append(dict(role='normalized_evidence', sha256=sha(evidence), schema=data['schema']))
    if not identities or any(not isinstance(i, str) or not i for i in identities) or len(set(identities)) != 1:
        raise ValueError('Fixture and independent observation map identities must match')
    geometry = adapters.Geometry(geometry_tool, capture, identities[0]) if capture else None
    try:
        if grid_roi is not None:
            raw = geometry.bridge.query('cells', *grid_roi)['rows']
            os, rejected = adapters.grid(raw, interpretation)
            observations.extend(os)
            diagnostics['grid_rejections'] = rejected
        report = associate(fs, observations, geometry)
        report['input'] = dict(map_sha256=identities[0], sources=inputs, grid_roi_world=grid_roi,
                               fixture_centroid_roi_world=fixture_roi,
                               grid_interpretation=interpretation if grid_roi is not None else None)
        if geometry:
            report['input']['geometry'] = dict(tool_sha256=sha(geometry_tool), capture_sha256=sha(capture),
                                               metadata=geometry.metadata)
    finally:
        if geometry:
            geometry.close()
    report['adapter_diagnostics'] = diagnostics
    if len(json.dumps(report, allow_nan=False).encode('utf-8')) > 64*1024*1024:
        raise ValueError('ASR15 output byte limit; no partial report published')
    write(output, report)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixtures', type=Path, nargs='*', default=[])
    p.add_argument('--patches', type=Path)
    p.add_argument('--rows', type=Path)
    p.add_argument('--evidence', type=Path)
    p.add_argument('--geometry-tool', type=Path)
    p.add_argument('--capture', type=Path)
    p.add_argument('--grid-roi', type=float, nargs=6)
    p.add_argument('--fixture-roi', type=float, nargs=6,
                   help='Explicit geometry-only centroid selection for fixture catalogues')
    p.add_argument('--grid-interpretation', choices=['consumer256_unknown', 'writer255_merge4'], default='consumer256_unknown')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    report = run(args.fixtures, args.out, args.patches, args.rows, args.evidence,
                 args.geometry_tool, args.capture, args.grid_roi, args.grid_interpretation, args.fixture_roi)
    print(json.dumps(dict(associations=len(report['associations']), active_fixtures=len(report['active_fixtures']),
                          unmatched_observations=len(report['unmatched_observations']), historical_source_certified=False)))


if __name__ == '__main__':
    main()
