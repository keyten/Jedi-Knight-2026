#!/usr/bin/env python3
"""ASR18 same-pose admission comparison; licensed geometry stays private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.emitter_support.compare_orientation import occupied, rows
from tools.rend2.emitter_support.diagnose import analyze, exact_distance, numerical_witnesses
from tools.rend2.emitter_support.run import MAPS, dataset
from tools.rend2.emitter_support.run_diagnosis import semantic


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def intersects(polygon, triangle):
    """Independent exact rational SAT for the frozen planar stock supports."""
    p = [[Fraction(x) for x in v] for v in polygon]
    t = [[Fraction(x) for x in v] for v in triangle]
    e = [sub(p[(i+1) % len(p)], v) for i, v in enumerate(p)]
    te = [sub(t[(i+1) % 3], v) for i, v in enumerate(t)]
    n = cross(e[0], e[1]) if len(p) > 2 else [0]*3
    tn = cross(te[0], te[1])
    axes = [n, tn] + [cross(n, v) for v in e] + [cross(tn, v) for v in te]
    axes += [cross(a, b) for a in e for b in te]
    axes += [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    for axis in axes:
        a = [sum(x*y for x, y in zip(axis, v)) for v in p]
        b = [sum(x*y for x, y in zip(axis, v)) for v in t]
        if min(a) > max(b) or min(b) > max(a):
            return False
    return True


def prove_rear_contacts(row):
    """No RGB, floating tolerances, fitted offsets or lamp truth in this proof."""
    if row['kind'] != 3 or row['contract'] != 8 or not row['local']['complete']:
        raise ValueError('Unexpected newly admitted source: expected exact original sheet')
    n, anchor = row['normal'], row['anchor']
    plane = sum(Fraction(x)*Fraction(y) for x, y in zip(n, anchor))
    if sorted(map(abs, n)) != [0, 0, 1]:
        raise ValueError('New contact exception escaped exact axial scope')
    for s in row['supports']:
        if s['kind'] != 0 or any(exact_distance(n, p, plane) != 0 for p in s['vertices']):
            raise ValueError('Original emitting support moved off its mount')
    contacts = 0
    for t in row['local']['triangles']:
        if t['surface'] in row['surfaces']:
            continue
        if not any(intersects(s['vertices'], t['vertices']) for s in row['supports']):
            continue
        distances = [exact_distance(n, v, plane) for v in t['vertices']]
        if t['optical'] != 1 or t['error'] != 0 or max(distances) != 0 or min(distances) >= 0:
            raise ValueError('Independent visual blocker on a newly admitted sheet')
        contacts += 1
    if not contacts:
        raise ValueError('New admission has no independent rear contact explanation')
    return contacts


def run(command, folder, label, seconds=300):
    result = execute(command, folder, label, seconds=seconds, memory=1024**3)
    if result['state'] != 'Completed' or result['exit_code']:
        raise ValueError('Native audit failed: '+label)
    return result


def main(a):
    before = json.loads((a.before/'stock.json').read_text())
    baseline = json.loads((a.before/'protocol.json').read_text())
    if sha(a.before_tool) != baseline['native_sha256']:
        raise ValueError('Frozen ASR17 executable identity changed')
    a.out.mkdir(parents=True, exist_ok=False)
    private = a.private/('runs-asr18-'+a.out.name)
    private.mkdir(exist_ok=False)
    frozen = private/'map-solver.exe'
    shutil.copyfile(a.tool, frozen)
    sources = list((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    sources += list((ROOT/'tools/rend2/emitter_support').glob('*.py'))
    sources += [ROOT/'tools/lightrecon_test'/name for name in (
        'lightrecon_emitter_support_test.cpp', 'mount_audit.h', 'mount_diagnosis.h',
        'lightrecon_map_solver.cpp', 'CMakeLists.txt')]
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in sources}
    protocol = dict(schema='org.openjk.mounted-admission-protocol.v1',
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        sources=hashes, native_sha256=sha(frozen), test_sha256=sha(a.test),
        before_protocol_sha256=sha(a.before/'protocol.json'), before_native_sha256=sha(a.before_tool),
        frozen_before_evaluation=True, maps=[before[n]['input'] for n in MAPS],
        max_candidates_per_map=256, max_admission_work=16000000, max_diagnostic_work=16000000,
        source_geometry_changed=False, reads_target_rgb=False, joint_fit_attempted=False,
        profile='UnknownProfile', source_truth='Unavailable')
    write(a.out/'protocol.json', protocol)
    for label in ('controls-first', 'controls-repeat'):
        run([a.test.resolve(), (a.out/(label+'.json')).resolve()], a.out, label, 120)
    if (a.out/'controls-first.json').read_bytes() != (a.out/'controls-repeat.json').read_bytes():
        raise ValueError('Owned controls changed on repeat')
    run([a.before_test.resolve(), (a.out/'controls-before.json').resolve()], a.out, 'controls-before', 120)
    captures = json.loads((a.private/'captures.json').read_text())
    reports, comparison = {}, {}
    for name in MAPS:
        entry = before[name]['input']
        archive = dataset(a.private, captures, name)
        if sha(archive) != entry['dataset_sha256']:
            raise ValueError('Original archive identity changed')
        old = Path(before[name]['resources']['command'][2])
        for filename in ('mounting-witnesses.tsv', 'diagnosis.jsonl', 'diagnosis-world.json', 'classification.json'):
            if sha(old/filename) != before[name]['private_artifacts'][filename]:
                raise ValueError('Frozen private witness identity changed')
        folder = private/name.replace('/', '_')
        folder.mkdir()
        resources = run([frozen, archive, folder, entry['bsp_sha256'], 'mounting-diagnosis'], folder, 'native')
        control = private/(name.replace('/', '_')+'-control')
        control.mkdir()
        run([frozen, archive, control, entry['bsp_sha256'], 'mounting-only'], control, 'native')
        if semantic(folder/'mounting.json') != semantic(control/'mounting.json') or sha(folder/'mounting-witnesses.tsv') != sha(control/'mounting-witnesses.tsv'):
            raise ValueError('Instrumentation changed admission')
        report = analyze(folder)
        x, y = rows(old), rows(folder)
        if x.keys() != y.keys():
            raise ValueError('Candidate cohort changed')
        transitions, reasons, accepted_kinds = Counter(), Counter(), Counter()
        new_ids, contact_count, solids = [], 0, 0
        for key in sorted(x, key=int):
            b, c = x[key], y[key]
            if any(b[k] != c[k] for k in ('fixture', 'position', 'surfaces', 'normal', 'anchor', 'stand_off')) or occupied(b) != occupied(c):
                raise ValueError('Full-precision source geometry changed')
            transitions[b['state']+' -> '+c['state']] += 1
            reasons[b['state']+': '+b['reason']+' -> '+c['state']+': '+c['reason']] += 1
            admitted = c['state'] in ('Free', 'Clear', 'Conditional')
            if b['state'] == 'Solid':
                if c['state'] != 'Solid':
                    raise ValueError('Known solid rejection changed')
                solids += 1
            if admitted:
                accepted_kinds[c['kind']] += 1
                if b['state'] not in ('Free', 'Clear', 'Conditional'):
                    contact_count += prove_rear_contacts(c)
                    new_ids.append(key)
        details = json.loads((folder/'classification.json').read_text())
        if any('bsp-penetration-witness' in d['facets'] and y[d['source']]['state'] in ('Free', 'Clear', 'Conditional') for d in details):
            raise ValueError('Independent exact BSP penetration on an admitted source')
        world = json.loads((folder/'diagnosis-world.json').read_text())
        metadata = {s['id']: s for s in world['surface_metadata']}
        witnesses = []
        for key in new_ids[:3]:
            named = [metadata[s] for s in y[key]['surfaces']]
            if any(s['model'] != 0 or s['motion'] != 0 for s in named):
                raise ValueError('New source is not static world geometry')
            witnesses.append(dict(source=key, fixture=y[key]['fixture'], state=y[key]['state'],
                kind=y[key]['kind'], named_surfaces=y[key]['surfaces'],
                materials=sorted({s['material'] for s in named}), static_world=True,
                rear_contact_triangles=prove_rear_contacts(y[key]), historical_source_truth='Unavailable'))
        comparison[name] = dict(tested=len(x), full_precision_geometry_and_ids_unchanged=True,
            transitions=dict(sorted(transitions.items())), reason_transitions=dict(sorted(reasons.items())),
            before=before[name]['current_admission'], after=semantic(folder/'mounting.json'),
            newly_admitted=len(new_ids), newly_admitted_source_ids=new_ids,
            exact_rear_contact_triangles=contact_count, known_solid_rejections_preserved=solids,
            accepted_by_kind=dict(accepted_kinds), static_fixture_witnesses=witnesses,
            admitted_with_independent_penetration_witness=0)
        report.update(input=entry, resources=resources, current_admission=semantic(folder/'mounting.json'),
            instrumentation_semantic_and_witness_match=True,
            private_artifacts={p.name: sha(p) for p in folder.iterdir() if p.is_file()})
        reports[name] = report
        write(a.out/'stock.json', reports)
        write(a.out/'comparison.json', comparison)
        write(a.out/(name.replace('/', '_')+'-witnesses.json'), numerical_witnesses(folder))
        print(name, comparison[name]['transitions'], flush=True)
    name = MAPS[0]
    archive = dataset(a.private, captures, name)
    entry = before[name]['input']
    for label, tool in (('repeat', frozen), ('before-replay', a.before_tool.resolve())):
        folder = private/('mp_duel4-'+label)
        folder.mkdir()
        run([tool, archive, folder, entry['bsp_sha256'], 'mounting-only'], folder, 'native')
        reference = private/'mp_duel4' if label == 'repeat' else Path(before[name]['resources']['command'][2])
        if semantic(folder/'mounting.json') != semantic(reference/'mounting.json') or sha(folder/'mounting-witnesses.tsv') != sha(reference/'mounting-witnesses.tsv'):
            raise ValueError('Repeat or frozen before replay mismatch')
    if sha(frozen) != protocol['native_sha256'] or sha(a.test) != protocol['test_sha256'] or any(sha(ROOT/n) != h for n, h in hashes.items()):
        raise ValueError('Source or executable changed during audit')
    write(a.out/'manifest.json', dict(sources_unchanged_during_audit=True, canonical_storage_changed=False,
        source_geometry_changed=False, runtime_changed=False, historical_solver_changed=False,
        licensed_geometry_committed=False, repeat_and_frozen_before_replay_match=True,
        artifacts={p.name: sha(p) for p in a.out.iterdir() if p.is_file()}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--before', type=Path, default=ROOT/'tools/rend2/emitter_support/results/asr17-side-v2')
    p.add_argument('--before-tool', type=Path, default=ROOT/'build/asr18-before-map-solver.exe')
    p.add_argument('--before-test', type=Path, default=ROOT/'build/asr18-before-support-test.exe')
    p.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_map_solver.exe')
    p.add_argument('--test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_emitter_support_test.exe')
    main(p.parse_args())
