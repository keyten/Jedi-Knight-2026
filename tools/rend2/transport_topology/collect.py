#!/usr/bin/env python3
"""Freeze aggregate X09 evidence without copying licensed geometry/observations.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAPS = ('mp/siege_desert', 't2_dpred', 't3_bounty')


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def semantic(value):
    if isinstance(value, dict):
        return {k: semantic(v) for k, v in value.items()
                if k not in ('resources', 'build_seconds', 'mesh_seconds', 'transfer_seconds')}
    if isinstance(value, list):
        return [semantic(v) for v in value]
    return value


def collect(private, revision, repeat_revision, controlled, repeat, out, producer_revision=None):
    control, again = load(controlled), load(repeat)
    if control != again or control['failures'] or control['checks'] < 100:
        raise ValueError('Controlled numerical witness did not pass identically twice')
    current = load(private / ('runs-' + revision + '.json'))
    repeated = load(private / ('runs-' + repeat_revision + '.json'))
    if semantic(current) != semantic(repeated):
        raise ValueError('Stock geometry/transport semantics changed on repeat')
    before = load(private / 'runs-x09-before.json')
    maps = []
    for name in MAPS:
        record = current[name]
        if record['resources']['exit_code'] or record['transport']['transfer_code']:
            raise ValueError('Transfer not built for ' + name)
        if not record.get('contacts') or any(c['code'] for c in record['contacts']):
            raise ValueError('Original contact regression failed for ' + name)
        original = before[name]
        for profile in ('isq', 'linear'):
            if 'Coincident/overlapping transport nodes' not in original['profiles'][profile]['reason']:
                raise ValueError('Original failure was not reproduced')
        item = dict(map=name, bsp_sha256=record['bsp_sha256'],
                    dataset_sha256=record['dataset_sha256'],
                    before=dict(observations=record['transport']['observations'],
                                reason=original['profiles']['isq']['reason'],
                                wall_seconds=original['resources']['wall_seconds'],
                                peak_process_rss_bytes=original['resources']['peak_process_rss_bytes']),
                    after=record['transport'], contacts=record.get('contacts', []),
                    private_witness_hashes=record['private_witness_hashes'],
                    resources={k: v for k, v in record['resources'].items() if k != 'command'},
                    historical_profile='UnknownProfile', physical_source_recovery=False)
        maps.append(item)
    out.mkdir(parents=True, exist_ok=True)
    def write(name, value):
        (out / name).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    write('controlled.json', control)
    write('maps.json', dict(schema='org.openjk.transport-topology-results.v1', maps=maps))
    write('validation.json', dict(schema='org.openjk.transport-topology-validation.v1',
                                  controlled_assertions=control['checks'], failures=0,
                                  controlled_repeat_identical=True, stock_repeat_identical=True,
                                  original_failures_reproduced=3, stock_transfer_builds=3,
                                  stock_physical_source_recovery=False,
                                  runtime_consumers_modified=False))
    artifacts = ['controlled.json', 'maps.json', 'validation.json']
    if producer_revision:
        produced = load(private / ('runs-' + producer_revision + '.json'))
        reports = []
        for name in MAPS:
            r = produced[name]
            profiles = {}
            for p in ('isq', 'linear'):
                v = r.get(p, {})
                counts = {}
                for attempt in v.get('attempts', []):
                    reason = attempt.get('reason', '')
                    counts[reason] = counts.get(reason, 0) + 1
                profiles[p] = dict(code=v.get('code'),
                                   producer_state=v.get('producer', {}).get('state'),
                                   producer_reason=v.get('producer', {}).get('reason'),
                                   joint_state=v.get('joint_state'), joint_reason=v.get('joint_reason'),
                                   work=v.get('work'), attempt_reasons=counts)
            reports.append(dict(map=name, profiles=profiles, direct_only_conditional=True,
                                physical_source_recovery=False,
                                resources={k: v for k, v in r['resources'].items() if k != 'command'}))
        write('producer.json', dict(schema='org.openjk.transport-topology-producer.v1', maps=reports))
        artifacts.append('producer.json')
    code = ['shared/rd-rend2/lightrecon/' + x for x in
            ('transport_mesh.cpp', 'transport_mesh.h', 'indirect.cpp', 'indirect.h',
             'joint.cpp', 'joint.h', 'producer.cpp')]
    code += ['tools/lightrecon_test/lightrecon_transport_mesh_test.cpp',
             'tools/lightrecon_test/lightrecon_map_solver.cpp',
             'tools/lightrecon_test/lightrecon_indirect_test.cpp',
             'shared/rd-rend2/lightrecon/CMakeLists.txt', 'tools/lightrecon_test/CMakeLists.txt',
             'tools/rend2/all_stock/architecture.py',
             'tools/rend2/transport_topology/run.py', 'tools/rend2/transport_topology/collect.py',
             'tools/rend2/transport_topology/verify_results.py',
             'tools/rend2/transport_topology/test_results.py']
    write('manifest.json', dict(schema='org.openjk.transport-topology-manifest.v1',
                               base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                                                   cwd=ROOT, text=True).strip(),
                               topology_version=1, indirect_version=2, joint_version=3,
                               native_sha256=current[MAPS[0]]['native_sha256'],
                               sources={x: sha(ROOT / x) for x in code},
                               artifacts={x: sha(out / x) for x in artifacts}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--revision', required=True)
    p.add_argument('--repeat-revision', required=True)
    p.add_argument('--controlled', type=Path, required=True)
    p.add_argument('--repeat', type=Path, required=True)
    p.add_argument('--out', type=Path, default=Path(__file__).parent / 'results/v1')
    p.add_argument('--producer-revision')
    a = p.parse_args()
    collect(a.private, a.revision, a.repeat_revision, a.controlled, a.repeat, a.out, a.producer_revision)
