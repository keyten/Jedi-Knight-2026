#!/usr/bin/env python3
"""Verify public X10 witnesses without licensed assets or an installed game."""
import hashlib
import json
from pathlib import Path


def verify(root=None):
    root = root or Path(__file__).parent / 'results/v1'
    def load(name):
        return json.loads((root / name).read_text())
    manifest = load('manifest.json')
    for name, expected in manifest['artifacts'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise ValueError('Changed numerical witness: ' + name)
    tests = load('controlled.json')
    assert tests['checks'] >= 400 and tests['failures'] == 0
    maps = load('stock.json')['maps']
    assert len(maps) == 7
    for m in maps:
        r = m['sampling']
        assert not r['folds_changed'] and not r['gaps_shrunk']
        assert not r['heldout_rgb_used_for_selection'] and not r['physical_source_recovery']
        assert r['historical_profile'] == 'UnknownProfile'
        assert len(r['jobs']) <= 8
        assert m['resources']['peak_process_rss_bytes'] <= 1024**3
        assert m['proposals']['stock_source_recall'] is None
    cases = load('compiled.json')['cases']
    assert len(cases) == 18
    for c in cases:
        assert not c['heldout_colours_accessible']
        for metric in c['trusted_source_metrics'].values():
            assert metric['physical_emitters_published'] == 0
    points = [x for x in cases if x['name'] == 'point_room']
    assert all(p['trusted_source_metrics']['hierarchical']['recall_at_grid_radius'] == 1 for p in points)
    assert all(p['trusted_source_metrics']['old']['candidates'] == 0 for p in points)
    no_lamp = [x for x in cases if x['name'] == 'bounce_colour_left']
    assert all(x['no_local_source_counterexample']['true_sources_inside'] == 0 for x in no_lamp)
    assert any(x['no_local_source_counterexample']['proposed_inside']['hierarchical'] > 0 for x in no_lamp)
    print('X10 witnesses verified: candidate coverage is not physical source recovery')


if __name__ == '__main__':
    verify()
