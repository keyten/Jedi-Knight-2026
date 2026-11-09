#!/usr/bin/env python3
"""Freeze/reproduce X14 and explain failures without changing admission semantics."""
import argparse
import csv
from collections import defaultdict
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.emitter_support.run import MAPS, dataset
from tools.rend2.emitter_support.diagnose import analyze, numerical_witnesses


def semantic(path):
    result = json.loads(path.read_text())
    result.pop('seconds', None)
    return result


def crosswalk(old, new):
    fields=('kind','support','x','y','z','width','height','length','radius','named_surfaces')
    before=list(csv.DictReader(old.open(),delimiter='\t'))
    after=list(csv.DictReader(new.open(),delimiter='\t'))
    index=defaultdict(list)
    for row in after:
        index[tuple(row[k] for k in fields)].append(row)
    matched=[]
    for row in before:
        matches=index[tuple(row[k] for k in fields)]
        if len(matches)==1:
            matched.append(dict(x14_source=row['source'],current_source=matches[0]['source'],
                                same_state=row['new']==matches[0]['new'],same_reason=row['reason']==matches[0]['reason']))
    return dict(scope='X14 six-significant-digit serialized geometry signature; not a full precision pose identity',
                x14_count=len(before),current_count=len(after),unique_matches=len(matched),
                state_reason_matches=sum(r['same_state'] and r['same_reason'] for r in matched),
                historical_witnesses=[r for r in matched if r['x14_source'] in ('378495349532519697','17291380550458404209')])


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    private_out = a.private/('runs-asr17-'+a.out.name)
    private_out.mkdir(exist_ok=False)
    prior = ROOT/'tools/rend2/emitter_support/results/v3'
    baseline = json.loads((prior/'stock.json').read_text())
    captures = json.loads((a.private/'captures.json').read_text())
    paths = list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    paths += [ROOT/'tools/lightrecon_test'/n for n in ('mount_audit.h', 'mount_diagnosis.h',
              'lightrecon_map_solver.cpp', 'lightrecon_emitter_support_test.cpp', 'CMakeLists.txt')]
    paths += list((ROOT/'tools/rend2/emitter_support').glob('*.py'))
    hashes = {p.relative_to(ROOT).as_posix():sha(p) for p in paths}
    frozen = private_out/'map-solver.exe'
    shutil.copyfile(a.tool, frozen)
    protocol = dict(schema='org.openjk.mount-diagnosis-protocol.v1',
                    commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
                    remote=subprocess.check_output(['git', 'remote', 'get-url', 'origin'], cwd=ROOT, text=True).strip(),
                    sources=hashes, native_sha256=sha(frozen), test_sha256=sha(a.test),
                    frozen_before_evaluation=True, maps=[baseline[n]['input'] for n in MAPS],
                    baseline_protocol_sha256=sha(prior/'protocol.json'),
                    historical_native_sha256=sha(a.private/'runs-x14-v3-native.exe'),
                    max_candidates_per_map=256, max_admission_work=16000000,
                    max_diagnostic_work=16000000, ray_count_per_candidate=2,
                    ray_probe_offsets_world=[[4, -4], [.125, 4]],
                    ray_policy='Unmodified default conservative optics/origin policy; no bias or endpoint exclusions',
                    process_seconds=300, process_bytes=1024**3,
                    observations_consumed=False, source_truth='Unavailable', profile='UnknownProfile',
                    purpose='Diagnosis only; neither rays nor independent arithmetic can grant admission')
    write(a.out/'protocol.json', protocol)
    x14_protocol = json.loads((prior/'protocol.json').read_text())
    if protocol['historical_native_sha256'] != x14_protocol['native_sha256']:
        raise ValueError('Frozen X14 native identity changed')
    for entry in protocol['maps']:
        if sha(dataset(a.private, captures, entry['map'])) != entry['dataset_sha256']:
            raise ValueError('X14 input identity changed')
    controls = []
    for label in ('controls-first', 'controls-repeat'):
        output = a.out/(label+'.json')
        r = execute([a.test.resolve(), output.resolve()], a.out, label, seconds=120, memory=256*1024**2)
        if r['exit_code'] or r['state'] != 'Completed':
            raise ValueError('Owned controls failed')
        controls.append(json.loads(output.read_text()))
    if controls[0] != controls[1]:
        raise ValueError('Owned controls changed on repeat')
    reports, witnesses = {}, {}
    for name in MAPS:
        folder = private_out/name.replace('/', '_')
        folder.mkdir()
        entry = baseline[name]['input']
        resources = execute([frozen, dataset(a.private, captures, name), folder, entry['bsp_sha256'],
                             'mounting-diagnosis'], folder, 'native', seconds=300, memory=1024**3)
        if resources['exit_code'] or resources['state'] != 'Completed':
            raise ValueError('Stock native diagnosis failed: '+name)
        control = private_out/(name.replace('/', '_')+'-control')
        control.mkdir()
        control_resources = execute([frozen, dataset(a.private, captures, name), control,
                                     entry['bsp_sha256'], 'mounting-only'], control, 'native')
        if control_resources['exit_code'] or control_resources['state'] != 'Completed':
            raise ValueError('Current admission control failed')
        if semantic(folder/'mounting.json') != semantic(control/'mounting.json') or sha(folder/'mounting-witnesses.tsv') != sha(control/'mounting-witnesses.tsv'):
            raise ValueError('Instrumentation changed current admission')
        historical = private_out/(name.replace('/', '_')+'-x14')
        historical.mkdir()
        historical_resources = execute([a.private/'runs-x14-v3-native.exe', dataset(a.private, captures, name),
                                        historical, entry['bsp_sha256'], 'mounting-only'], historical, 'native')
        if historical_resources['exit_code'] or historical_resources['state'] != 'Completed':
            raise ValueError('Frozen X14 reproduction failed')
        expected = dict(baseline[name]['admission'])
        expected.pop('seconds', None)
        if semantic(historical/'mounting.json') != expected or sha(historical/'mounting-witnesses.tsv') != baseline[name]['private_geometry_witness_sha256']:
            raise ValueError('Frozen X14 reproduction mismatch: '+name)
        report = analyze(folder)
        report['crosswalk']=crosswalk(historical/'mounting-witnesses.tsv',folder/'mounting-witnesses.tsv')
        witnesses[name]=numerical_witnesses(folder)
        write(a.out/'witnesses.json',witnesses)
        report.update(resources=resources, x14_frozen_semantic_match=True, x14_frozen_witness_byte_match=True,
                      instrumentation_semantic_match=True, instrumentation_witness_byte_match=True,
                      current_admission=semantic(folder/'mounting.json'),
                      current_matches_x14_summary=semantic(folder/'mounting.json') == expected,
                      current_matches_x14_ids=sha(folder/'mounting-witnesses.tsv') == baseline[name]['private_geometry_witness_sha256'],
                      control_resources=control_resources, historical_resources=historical_resources,
                      input=entry, private_artifacts={p.name:sha(p) for p in folder.glob('*') if p.is_file()})
        reports[name] = report
        write(a.out/'stock.json', reports)
        print(name, report['primary'], flush=True)
    name = MAPS[0]
    folder = private_out/'mp_duel4-repeat'
    folder.mkdir()
    resource = execute([frozen, dataset(a.private, captures, name), folder, baseline[name]['input']['bsp_sha256'],
                        'mounting-diagnosis'], folder, 'native', seconds=300, memory=1024**3)
    if resource['exit_code'] or resource['state'] != 'Completed':
        raise ValueError('Stock repeat failed')
    analyze(folder)
    checked = ('mounting-witnesses.tsv', 'diagnosis.jsonl', 'diagnosis-world.json', 'classification.json')
    if any(sha(folder/n) != reports[name]['private_artifacts'][n] for n in checked):
        raise ValueError('Geometry diagnosis nondeterminism')
    write(a.out/'repeat.json', dict(map=name, byte_identical=list(checked), resources=resource))
    if sha(frozen) != protocol['native_sha256'] or any(sha(ROOT/n) != h for n, h in hashes.items()):
        raise ValueError('Sources/executable changed during experiment')
    write(a.out/'manifest.json', dict(sources_unchanged=True, canonical_storage_changed=False,
          admission_semantics_changed=False, runtime_changed=False, licensed_geometry_committed=False,
          artifacts={p.name:sha(p) for p in a.out.iterdir() if p.is_file() and p.name != 'manifest.json'}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_emitter_support_test.exe')
    main(p.parse_args())
