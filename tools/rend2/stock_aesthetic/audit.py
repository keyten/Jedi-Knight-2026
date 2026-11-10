"""Replay frozen fitted contributions and source removal through existing native providers.

This is a post-fit diagnostic, not another optimizer or an acceptance-threshold
revision. Geometry-only Shape::Prepare and native Bake::Encode supply predictions.
Licensed per-receiver values remain private. No runtime/canonical asset writer.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import subprocess

from .run import ROOT, load, sha, write


def score(rows, predictions, blocks):
    """Independent audit of the documented ASR20 area/block/normal RGB8 loss."""
    mass = defaultdict(lambda: defaultdict(float))
    normal_keys = []
    for r in rows:
        axis = max(range(3), key=lambda k: abs(r['normal'][k]))
        normal = axis, r['normal'][axis] < 0
        normal_keys.append(normal)
        mass[normal][blocks[str(int(r['id'][:15], 16)+1)]] += r['area']
    mse = robust = 0.
    for r, predicted, normal in zip(rows, predictions, normal_keys):
        block = blocks[str(int(r['id'][:15], 16)+1)]
        weight = r['area']/mass[normal][block]/len(mass[normal])/len(mass)/3
        for value, code in zip(predicted, r['raw']):
            error = max(0., code-value, value-code-1) if code < 255 else max(0., 255-value)
            mse += weight*error*error
            robust += weight*(.5*error*error if error <= 4 else 4*(error-2))
    return dict(mse=mse, robust=robust)


def run_native(command):
    result = subprocess.run([str(p) for p in command], capture_output=True, text=True, timeout=300)
    if result.returncode:
        raise ValueError(result.stderr.strip())
    return result.stdout


def audit(work, name, encoder):
    protocol = load(work/'protocol.json')
    spec = next(s for s in protocol['regions'] if s['name'] == name)
    folder = work/name
    data = load(folder/'result.json')
    if data['selected'] < 0:
        raise ValueError('No validation-selected local configuration to ablate')
    config = data['configurations'][data['selected']]
    tool = work/'implementation/bin/lightrecon_aesthetic_tool.exe'
    expected_tool = next(value for path, value in protocol['tools'].items()
                         if Path(path).name == tool.name)
    if sha(tool) != expected_tool:
        raise ValueError('Frozen executable hash mismatch')
    for path, expected in load(work/'freeze.json').items():
        if sha(work/path) != expected:
            raise ValueError('Frozen input modified: '+path)
    capture = Path(protocol['maps'][spec['map']]['capture'])
    if sha(capture) != protocol['maps'][spec['map']]['capture_sha256']:
        raise ValueError('Original capture changed')
    inputs = [folder/p for p in ('fit.txt', 'held.txt', 'folds.json', 'result.json')]
    frozen = {p.name: sha(p) for p in inputs}
    write(folder/'ablation-protocol.json', dict(inputs=frozen, frozen_tool_sha256=sha(tool),
          encoder_sha256=sha(encoder), auditor_sha256=sha(Path(__file__)),
          parameters_refitted=False, historical_source_certified=False,
          rounding='Replay published float RGB/background; require native score agreement',
          diagnostic='Source removal holds the fitted background fixed; it is distinct from the refitted no-local baseline'))
    lines = (folder/'fit.txt').read_text().splitlines()
    first = 4 if lines[0].startswith('JKASR21-') else 3
    fixtures = lines[first+1:first+1+int(lines[first])]
    held = (folder/'held.txt').read_text().splitlines()
    geometry = [held[0]]+[' '.join(line.split()[:-3]+['0', '0', '0']) for line in held[1:]]
    blocks = {line.split()[0]: line.split()[1] for line in held[1:]}
    rows = load(folder/'folds.json')['heldout']
    coefficients = []
    for source in config['sources']:
        line = next(line for line in fixtures if line.split()[1] == source['fixture'])
        tokens = line.split()
        if lines[0].startswith('JKASR21-'):
            tokens = tokens[:-2]  # visible/hidden provenance is unused by geometry preparation
        path = folder/('ablation-geometry-'+source['fixture']+'.txt')
        path.write_text('\n'.join(['JKASR24-GEOMETRY-ONLY-1', '1', '255 255 255', '1',
                                   ' '.join(tokens)]+geometry)+'\n')
        result = json.loads(run_native([tool, capture, path, '--coverage']))
        if not all(r['available'] for r in result['rows']):
            raise ValueError('Unknown forward coverage cannot be an ablation zero')
        coefficients.append({r['id']: r['coefficient'] for r in result['rows']})
    raw = []
    for row in rows:
        key = str(int(row['id'][:15], 16)+1)
        raw.append([config['background'][k]+sum(c[key]*s['relative_rgb'][k]
                    for c, s in zip(coefficients, config['sources'])) for k in range(3)])
    raw += [config['background'] for _ in rows]
    path = folder/'ablation-encode.txt'
    path.write_text(str(len(raw))+'\n'+'\n'.join(' '.join(map(str, v)) for v in raw)+'\n')
    encoded = json.loads(run_native([encoder, '--encode', path]))['rows']
    on, off = encoded[:len(rows)], encoded[len(rows):]
    fitted, removed = score(rows, on, blocks), score(rows, off, blocks)
    tolerance = max(.001, 1e-5*max(1, config['heldout']['mse']))
    if abs(fitted['mse']-config['heldout']['mse']) > tolerance:
        raise ValueError('Published-parameter replay does not agree with frozen native score')
    changed = sum(max(abs(a-b) for a, b in zip(x, y)) >= 1 for x, y in zip(on, off))
    result = dict(available=True, rows=len(rows), on=fitted, source_removed=removed,
                  native_on_mse=config['heldout']['mse'], native_replay_agrees=True,
                  rows_with_at_least_one_code_response=changed,
                  maximum_code_response=max(abs(a-b) for x, y in zip(on, off) for a, b in zip(x, y)),
                  source_removal_worsens_mse=removed['mse'] > fitted['mse']+tolerance,
                  parameters_refitted=False, calibrated_irradiance=False,
                  original_target_domain='Encoded RGB8 intervals; saturation censored',
                  historical_source_certified=False)
    if any(sha(folder/name) != expected for name, expected in frozen.items()):
        raise ValueError('Frozen result/targets modified during audit')
    write(folder/'ablation.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--region', required=True)
    parser.add_argument('--encoder', type=Path, default=ROOT/'build/s06-gcc/lightrecon_bake_forward_tool.exe')
    args = parser.parse_args()
    print(json.dumps(audit(args.work, args.region, args.encoder), sort_keys=True))
