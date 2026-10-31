#!/usr/bin/env python3
"""Validate published audit coverage and honest missing-metric semantics."""
import argparse
import json
from pathlib import Path


def verify(folder):
    summary = json.loads((folder/'summary.json').read_text())
    rows = json.loads((folder/'maps.json').read_text())['maps']
    assert len({r['map'] for r in rows}) == len(rows)
    assert summary['attempted_maps'] == len(rows)
    total_raw = total_mapped = 0
    for row in rows:
        assert len(row['bsp_sha256']) == 64
        census = row['census']
        assert census and row['bsp_sha256'] == census['bsp_sha256']
        assert sum(census['coverage']) == census['derived_texels']
        assert sum(sum(p['counts']) for p in census['pages']) == census['derived_texels']
        assert census['raw_colour_pages'] == len(census['pages'])
        assert census['raw_colour_pages']*128*128 == census['derived_texels']
        assert census['eligible_planar'] <= census['coverage'][2]
        assert census['unknown_transfer'] == census['unknown_primaries'] == census['derived_texels']
        assert row['historical_profile'] == 'UnknownProfile'
        assert all(row['physical_truth'][k] is None for k in (
            'emitter_precision','emitter_recall','source_xyz_error','source_type_accuracy','physical_intensity_error'))
        for profile in row['profiles'].values():
            p = profile.get('producer', {})
            if profile.get('joint_state') == 'NotStarted':
                assert p['training_interval_mse'] is None
                assert p['heldout_interval_mse'] is None
                assert not profile['final_evaluation_performed']
            if p.get('state') in ('Failed','Budget'):
                assert not p['asset_available']
        total_raw += census['derived_texels']
        total_mapped += census['coverage'][2]
    assert total_raw == summary['raw_texels']
    assert total_mapped == summary['mapped_luxels']
    assert summary['architecture']['dependency_check_pass']
    print(f'{len(rows)} map records: identity, all-pages counts, missing metrics and no false publication verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('folder', type=Path)
    verify(parser.parse_args().folder)
