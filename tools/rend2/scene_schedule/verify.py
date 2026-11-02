#!/usr/bin/env python3
"""Offline verification of public X11 numerical witnesses; no licensed assets."""
import hashlib
import json
from pathlib import Path


def verify(root=None):
    root = root or Path(__file__).parent/'results/v1'
    manifest = json.loads((root/'manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != expected:
            raise ValueError('Corrupt witness ' + name)
    stock = json.loads((root/'stock.json').read_text())
    assert stock['semantic_repeat'] and not stock['stock_physical_recovery']
    maps = stock['maps']
    assert len(maps) >= 5
    assert any(len(v['isq']['units']) > 100 for v in maps)
    for case in maps:
        assert case['physical_source_truth'] == 'Unavailable'
        for profile in ('isq','linear'):
            result = case[profile]
            assert all(u['status'] != 'Pending' for u in result['units'])
            assert result['stock_source_truth'] is None
            assert result['local_calls'] <= 4096
    assert any(v['isq']['joint_calls'] for v in maps)
    control = json.loads((root/'controlled.json').read_text())
    assert control['failures'] == 0 and control['checks'] >= 100
    assert control['scene']['joint_calls'] and len(control['scene']['units']) > 32
    checks = json.loads((root/'checks.json').read_text())
    assert checks['cpu']['failed'] == 0
    print('X11 verified: bounded orchestration and conditional fitting; no stock lamp recovery claim')


if __name__ == '__main__':
    verify()
