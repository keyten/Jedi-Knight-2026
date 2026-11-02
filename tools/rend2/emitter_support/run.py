#!/usr/bin/env python3
"""X14 bounded geometry-only stock admission audit; licensed witnesses stay private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import json
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write

MAPS = ('mp/duel4', 'mp/siege_desert', 'hoth3')


def dataset(private, captures, name):
    path = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
    return path if path.exists() else Path(captures[name]['dataset'])


def main(a):
    inventory = json.loads((a.private / 'inventory.json').read_text())
    captures = json.loads((a.private / 'captures.json').read_text())
    a.out.mkdir(parents=True, exist_ok=False)
    native = a.test.resolve()
    tool = a.tool.resolve()
    revision = 'x14-' + a.out.name
    policy = dict(schema='org.openjk.emitter-support-protocol.v1',
                  input_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  native_sha256=sha(tool), test_sha256=sha(native),
                  inventory_sha256=sha(a.private / 'inventory.json'),
                  frozen_before_evaluation=True, maps=[], mode='mounting-only',
                  ordering='stable consolidated source ID, at most 256 named-surface models / 16M work per map',
                  process_seconds=300, process_bytes=1024**3,
                  source_truth='Unavailable', stock_profile='UnknownProfile',
                  reads_observed_rgb=False, joint_fitting=False,
                  environment=dict(os=platform.platform(), cpu=platform.processor(), python=platform.python_version()))
    for name in MAPS:
        item = next(x for x in inventory['maps'] if x['map'] == name)
        path = dataset(a.private, captures, name)
        policy['maps'].append(dict(map=name, bsp_sha256=item['sha256'], dataset_sha256=sha(path), dataset_bytes=path.stat().st_size))
    write(a.out / 'protocol.json', policy)
    frozen = a.private / ('runs-' + revision + '-native.exe')
    if frozen.exists():
        raise ValueError('Preserve previous frozen native runs')
    shutil.copyfile(tool, frozen)
    tests = []
    resources = []
    for suffix in ('first', 'repeat'):
        path = a.out / ('native-' + suffix + '.json')
        resource = execute([native, path], a.out, 'native-' + suffix, seconds=120, memory=256*1024**2)
        if resource['exit_code'] or resource['state'] != 'Completed':
            raise ValueError('Controlled source-support test failed')
        tests.append(json.loads(path.read_text()))
        resources.append(resource)
    if tests[0] != tests[1]:
        raise ValueError('Nondeterministic controlled result')
    write(a.out / 'native.json', dict(result=tests[0], repeat_semantics=True, resources=resources))
    result = {}
    for entry in policy['maps']:
        name = entry['map']
        path = dataset(a.private, captures, name)
        if sha(path) != entry['dataset_sha256'] or sha(frozen) != policy['native_sha256']:
            raise ValueError('Changed immutable run input')
        folder = a.private / ('runs-' + revision) / name.replace('/', '_')
        folder.mkdir(parents=True, exist_ok=False)
        r = execute([frozen, path, folder, entry['bsp_sha256'], policy['mode']], folder, 'native', seconds=policy['process_seconds'], memory=policy['process_bytes'])
        record = dict(input=entry, resources=r)
        if (folder / 'mounting.json').exists():
            record['admission'] = json.loads((folder / 'mounting.json').read_text())
            record['private_geometry_witness_sha256'] = sha(folder / 'mounting-witnesses.tsv')
        else:
            record['status'] = 'Unavailable'
            record['reason'] = (folder / 'native.log').read_text(errors='replace')[-1000:]
        result[name] = record
        write(a.out / 'stock.json', result)
        print(name, r['state'], record.get('admission', {}).get('newly_admitted'), flush=True)
    # Repeat the predeclared small MP map. Timing/RSS are excluded from semantic comparison.
    item = policy['maps'][0]
    folder = a.private / ('runs-' + revision) / 'mp_duel4-repeat'
    folder.mkdir(parents=True, exist_ok=False)
    repeat = execute([frozen, dataset(a.private, captures, item['map']), folder, item['bsp_sha256'], policy['mode']], folder, 'native', seconds=300, memory=1024**3)
    compared = json.loads((folder / 'mounting.json').read_text())
    expected = dict(result[item['map']]['admission'])
    compared.pop('seconds', None)
    expected.pop('seconds', None)
    if compared != expected or sha(folder / 'mounting-witnesses.tsv') != result[item['map']]['private_geometry_witness_sha256']:
        raise ValueError('Stock support identity/admission nondeterminism')
    write(a.out / 'repeat.json', dict(map=item['map'], semantic_match=True, private_witness_match=True, resources=repeat))
    sources = list((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    sources += [ROOT / 'tools/lightrecon_test' / x for x in ('mount_audit.h', 'lightrecon_map_solver.cpp', 'lightrecon_emitter_support_test.cpp')]
    sources.append(Path(__file__).resolve())
    write(a.out / 'manifest.json', dict(sources={p.relative_to(ROOT).as_posix(): sha(p) for p in sources},
                                       artifacts={p.name: sha(p) for p in a.out.iterdir() if p.is_file() and p.name != 'manifest.json'},
                                       licensed_geometry_committed=False, renderer_lighting_changed=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--private', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    parser.add_argument('--test', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_emitter_support_test.exe')
    main(parser.parse_args())
