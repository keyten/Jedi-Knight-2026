#!/usr/bin/env python3
"""Freeze and repeat the owned ASR22 offline discovery witnesses.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def main(args):
    args.out.mkdir(parents=True, exist_ok=False)
    core = sorted((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    core += [ROOT / 'shared/rd-rend2' / name for name in
             ('tr_lightrecon_types.h', 'tr_lightrecon_types.cpp')]
    files = core + [ROOT / 'tools/lightrecon_test' / name for name in
                    ('iterative_discovery.h', 'iterative_discovery.cpp',
                     'lightrecon_discovery_test.cpp', 'joint_test_scene.h',
                     'joint_fixture.h', 'CMakeLists.txt')]
    files += [Path(__file__).resolve()]
    frozen = {path.relative_to(ROOT).as_posix(): sha(path) for path in files}
    prior = json.loads((ROOT / 'tools/rend2/sparse_selection/results/asr21-v1/protocol.json')
                       .read_text(encoding='utf-8'))['sources']
    for path in core:
        name = path.relative_to(ROOT).as_posix()
        if prior.get(name) != frozen[name]:
            raise ValueError('Historical core/canonical storage changed since ASR21: ' + name)
    binary = sha(args.test)
    write(args.out / 'protocol.json', dict(
        schema='org.openjk.iterative-discovery-protocol.v1', sources=frozen,
        test_sha256=binary, frozen_before_evaluation=True,
        target_domain='Attested relative incident or fixed forward-encoded appearance',
        historical_source_truth='Unavailable',
        scope='Owned synthetic witnesses; no stock-source or renderer claim'))
    for name in ('owned', 'owned-repeat'):
        result = subprocess.run([str(args.test.resolve()), str((args.out / (name + '.json')).resolve())],
                                check=True, capture_output=True, text=True, timeout=120)
        (args.out / (name + '.log')).write_text(result.stdout, encoding='utf-8')
    if (args.out / 'owned.json').read_bytes() != (args.out / 'owned-repeat.json').read_bytes():
        raise ValueError('Discovery output changed on repeat')
    result = json.loads((args.out / 'owned.json').read_text(encoding='utf-8'))
    if (result['failures'] or result['discovered_sources'] != result['supported_positive_sources']
            or result['added_negative_lights']):
        raise ValueError('Owned recall/negative-control gate failed')
    for scene in result['scenes']:
        for iteration in scene['receipt']['iterations']:
            limits = iteration['limits']
            if (not iteration['stop'] or not iteration['provenance']
                    or iteration['calls'] > 4 + limits['fits'] + limits['proposals']
                    or iteration['evaluations'] > iteration['calls'] * limits['evaluations_per_call']
                    or iteration['rays'] > iteration['calls'] * limits['rays_per_call']):
                raise ValueError('Missing/broken iteration work receipt')
    if sha(args.test) != binary or any(sha(ROOT / name) != digest for name, digest in frozen.items()):
        raise ValueError('Inputs changed during the frozen evaluation')
    write(args.out / 'manifest.json', dict(
        sources_unchanged=True, historical_core_matches_asr21=True,
        repeat_identical=True, supported_source_recall=[0, result['discovered_sources']],
        supported_source_count=result['supported_positive_sources'],
        added_negative_lights=result['added_negative_lights'],
        negative_controls=result['negative_controls'],
        artifacts={path.name: sha(path) for path in sorted(args.out.iterdir()) if path.is_file()}))
    print(f"ASR22: {result['checks']} checks; recall 0/{result['supported_positive_sources']} -> "
          f"{result['discovered_sources']}/{result['supported_positive_sources']}; "
          f"{result['added_negative_lights']} added lights in {result['negative_controls']} negatives; identical repeat")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--test', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_discovery_test.exe')
    main(parser.parse_args())
