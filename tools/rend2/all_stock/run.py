#!/usr/bin/env python3
"""Bounded, resumable stock-map audit. Never distributes licensed payloads.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/rend2/datasets'))
from measure import peak_memory


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def execute(command, folder, label, seconds=600, memory=1024 * 1024 * 1024):
    start = time.monotonic()
    peak = 0
    status = 'Completed'
    with (folder / (label + '.log')).open('w', encoding='utf-8') as log:
        child = subprocess.Popen(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT)
        while child.poll() is None:
            peak = max(peak, peak_memory(child) or 0)
            if peak > memory:
                status = 'ProcessMemoryBudget'
            elif time.monotonic() - start > seconds:
                status = 'ProcessTimeBudget'
            if status != 'Completed':
                # This exact child only; never terminate another task's game/tool.
                child.terminate()
                child.wait(timeout=15)
                break
            time.sleep(.1)
        peak = max(peak, peak_memory(child) or 0)
    result = dict(command=list(map(str, command)), state=status, exit_code=child.returncode,
                  wall_seconds=time.monotonic() - start, peak_process_rss_bytes=peak)
    write(folder / (label + '-resources.json'), result)
    return result


def run(private, tool, quality, maps=None, force=False, revision='v2', local=False):
    inventory = json.loads((private / 'inventory.json').read_text())
    captures = json.loads((private / 'captures.json').read_text())
    policy = dict(schema='org.openjk.all-stock-policy.v1', base_commit=inventory['base_commit'],
                  native_sha256=sha(tool), quality_sha256=sha(quality),
                  split_seed=20261031, cell_world=256, split_origin=[17, 31, 47],
                  footprint_gap='3 * radius + 1 world unit', split=[.6, .2, .2],
                  sampling='minimum stable sample ID per 512-world-unit position/normal stratum',
                  max_rows=[512, 256, 256], profiles=['explicit NetRadiant inverse-square', 'explicit NetRadiant linear'],
                  historical_stock_profile='UnknownProfile', absolute_photometry='Unknown',
                  process_seconds=600, process_bytes=1024**3,
                  joint_seconds_per_profile=180, policy_frozen_before_run=True,
                  target_scope='full-map census; bounded whole-map solver attempt; not full-luxel optimization')
    policy['adapter_revision'] = revision
    if local:
        policy.update(sampling='64-WU position/normal strata inside one geometry-selected 512-WU cell',
                      target_scope='dense local follow-up; NOT full-map optimization',
                      roi_rule='most normal-direction bins, then mapped area, then lexicographic cell; no RGB',
                      source_prior='ROI plus 256 WU; distant source alternatives remain untested',
                      selected_maps=maps, local=True)
    policy_path = private / ('policy-' + revision + '.json')
    if policy_path.exists() and json.loads(policy_path.read_text()) != policy and not force:
        raise RuntimeError('Changed policy/executable: use a new private run root or explicit --force')
    write(policy_path, policy)
    record = private / ('runs-' + revision + '.json')
    results = json.loads(record.read_text()) if record.exists() else {}
    for item in inventory['maps']:
        name = item['map']
        if maps and name not in maps:
            continue
        if name in results and not force:
            continue
        folder = private / ('runs-' + revision) / name.replace('/', '_')
        folder.mkdir(parents=True, exist_ok=True)
        capture = captures.get(name, {})
        dataset = capture.get('dataset')
        if not dataset or not Path(dataset).is_file():
            result = dict(state='CaptureUnavailable', capture=capture.get('status'))
        else:
            # Reused older archives may contain only a prefix of derived pages.
            # The native preflight rejects these. Re-derive *all* pages once.
            if capture['status'] == 'ReusedNeedsIdentityCheck':
                prior = private / 'runs' / name.replace('/', '_') / 'all-pages.jkobs'
                derived = prior if prior.exists() else folder / 'all-pages.jkobs'
                if not derived.exists():
                    derivation = execute([quality, 'derive', dataset, derived, 0, 0, 32], folder, 'derive')
                    if derivation['exit_code']:
                        results[name] = dict(state='DerivationFailed', resources=derivation)
                        write(record, results)
                        print(name, 'DerivationFailed', flush=True)
                        continue
                dataset = str(derived)
            if sha(tool) != policy['native_sha256']:
                raise RuntimeError('Executable changed during frozen run')
            # A retry must not pick up a stale successful JSON from an earlier
            # failed attempt. Delete only this adapter's two tiny result files.
            for profile in ('isq', 'linear'):
                for suffix in ('.json', '-failure.json'):
                    (folder / (profile + suffix)).unlink(missing_ok=True)
            command = [tool, dataset, folder, item['sha256']]
            if local:
                command.append('local-density')
            resources = execute(command, folder, 'native')
            result = dict(state=resources['state'], dataset_bytes=Path(dataset).stat().st_size,
                          resources=resources, expected_bsp_sha256=item['sha256'])
            if resources['exit_code']:
                result['state'] = 'NativeRejected' if resources['state'] == 'Completed' else resources['state']
                result['reason'] = (folder / 'native.log').read_text(errors='replace')[-2000:]
            result['profiles'] = {}
            for profile in ('isq', 'linear'):
                for suffix in ('.json', '-failure.json'):
                    p = folder / (profile + suffix)
                    if p.exists():
                        result['profiles'][profile] = json.loads(p.read_text())
                        break
        results[name] = result
        write(record, results)
        print(name, result['state'],
              {k: v.get('producer', v).get('state') for k, v in result.get('profiles', {}).items()}, flush=True)
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--private', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_map_solver.exe')
    parser.add_argument('--quality', type=Path, default=ROOT / 'build/s06-gcc/lightrecon_dataset_quality.exe')
    parser.add_argument('--maps', nargs='*')
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--revision', default='v2')
    parser.add_argument('--local', action='store_true')
    args = parser.parse_args()
    run(args.private, args.tool.resolve(), args.quality.resolve(), args.maps, args.force, args.revision, args.local)
