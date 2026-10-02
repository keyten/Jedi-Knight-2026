#!/usr/bin/env python3
"""Build shared C++ policy harness; optionally replay every installed stock EFX.

Pass --compiler and optionally --stock-base. Build outputs stay under build/.
"""
import argparse
import collections
import os
from pathlib import Path
import subprocess
from audit_stock_fx import Corpus, effects_inventory
from generate_physicalization_stock import REVIEWED, fingerprint


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', required=True, type=Path)
    ap.add_argument('--stock-base', type=Path)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / 'build' / 'fx-physicalization-tests.exe'
    output.parent.mkdir(exist_ok=True)
    env = {k.upper(): v for k, v in os.environ.items()}
    env['PATH'] = str(args.compiler.parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([str(args.compiler), '-std=c++11', '-O2', '-Wall', '-Wextra',
                    '-I' + str(root / 'shared'), str(root / 'tests/fx_physicalization_tests.cpp'),
                    '-o', str(output), '-static-libstdc++', '-static-libgcc'], env=env, check=True)
    subprocess.run([str(output)], env=env, check=True)
    if not args.stock_base:
        return
    corpus = Corpus([args.stock_base / ('assets%d.pk3' % i) for i in range(4)])
    try:
        errors = []
        effects = effects_inventory(corpus, errors)
        assert not errors, errors
        inputs, expected, labels = [], [], []
        counts = collections.Counter()
        for e in effects:
            short = e['file'][8:-4]
            checksum = fingerprint(corpus.read(e['file']))
            ordinal = 0
            for p in e['primitives']:
                if not p['engine_supported_type']:
                    continue
                # Exercise exact path for EVERY alternative. Generic parameters
                # stay zero, so unreviewed alternatives must remain legacy.
                for shader in p['shaders']:
                    inputs.append('%s %d %d %s' % (e['file'], checksum, ordinal, shader))
                    labels.append((e['file'], ordinal, shader))
                    material = REVIEWED.get(short, {}).get(ordinal)
                    if isinstance(material, dict):
                        material = material.get(shader)
                    positive = material is not None
                    expected.append((['None', 'DarkSmoke', 'LightSmoke', 'Mist', 'DustCloud', 'Gas'].index(material) if positive else 0,
                                     1 if positive else 0))
                    counts['reviewed' if positive else 'legacy'] += 1
                ordinal += 1
        result = subprocess.run([str(output), '--corpus'], input='\n'.join(inputs) + '\n',
                                text=True, capture_output=True, check=True, env=env)
        got = [tuple(int(v) for v in line.split()) for line in result.stdout.splitlines()]
        assert len(got) == len(expected)
        for label, actual, target in zip(labels, got, expected):
            assert actual == target, (label, actual, target)
        print('PASS: %d stock media alternatives replayed (%s)' % (len(got), dict(counts)))
    finally:
        corpus.close()


if __name__ == '__main__':
    main()
