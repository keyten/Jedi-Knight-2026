#!/usr/bin/env python3
"""Freeze aggregate X11 witnesses without stock geometry or target pixels."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def load(p):
    return json.loads(Path(p).read_text(encoding='utf-8'))


def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def semantic(v):
    if isinstance(v, dict):
        return {k: semantic(x) for k, x in v.items() if k not in ('resources', 'tail')}
    if isinstance(v, list):
        return [semantic(x) for x in v]
    return v


def collect(private, revision, repeated, controlled, controlled_repeat, checks, output):
    first, second = (load(private / ('runs-' + x + '.json')) for x in (revision, repeated))
    if semantic(first) != semantic(second):
        raise ValueError('Frozen stock semantic outcomes changed on repeat')
    a, b = load(controlled), load(controlled_repeat)
    if a != b or a['failures'] or a['checks'] < 100:
        raise ValueError('Controlled scheduler assertions/determinism failed')
    a['observation_contract'] = dict(operator='Specified physical relative integral',
        rgb='Scene-linear relative; no SI intensity calibration',
        geometry='Owned analytical S10/S11/S13 fixtures; fixed XYZ supplied',
        historical_profile='UnknownProfile does not define this physical-integral law')
    rows = []
    for name, case in first.items():
        if case['resources']['exit_code']:
            raise ValueError('Unsuccessful native final run: ' + name)
        for profile in ('isq', 'linear'):
            value = case[profile]
            if not value.get('graph_key') or any(u['status'] == 'Pending' for u in value.get('units', [])):
                raise ValueError('Incomplete stock job execution: ' + name + '/' + profile)
        rows.append(dict(map=name, bsp_sha256=case['bsp_sha256'], native_sha256=case['native_sha256'],
                         dataset_sha256=case.get('census',{}).get('archive_sha256'),
                         private_witnesses=case.get('private_witnesses',{}),
                         historical_profile=case['historical_profile'], physical_source_truth=case['physical_source_truth'],
                         isq=case['isq'], linear=case['linear'],
                         resources={k:v for k,v in case['resources'].items() if k != 'command'},
                         repeated_resources={k:v for k,v in second[name]['resources'].items() if k != 'command'}))
    output.mkdir(parents=True, exist_ok=True)
    def write(name, v):
        (output / name).write_text(json.dumps(v, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    write('stock.json', dict(schema='org.openjk.scene-schedule-stock.v1', maps=rows,
                             semantic_repeat=True, stock_physical_recovery=False))
    write('controlled.json', a)
    write('checks.json', load(checks))
    files = ['shared/rd-rend2/lightrecon/' + x for x in ('scene_schedule.cpp','scene_schedule.h',
             'producer.cpp','producer.h','hidden_candidates.cpp','hidden_candidates.h',
             'shadow_evidence.cpp','shadow_evidence.h','CMakeLists.txt')]
    files += ['tools/lightrecon_test/' + x for x in ('scene_audit.h','sampling_audit.h',
              'lightrecon_map_solver.cpp','lightrecon_scene_schedule_test.cpp','lightrecon_shadow_test.cpp','CMakeLists.txt')]
    files += ['tools/rend2/scene_schedule/' + x for x in ('run.py','collect.py','verify.py','test_reports.py')]
    files += ['tools/lightrecon_test/' + x for x in ('joint_fixture.h','joint_test_scene.h','producer_test_scene.h')]
    files += ['shared/rd-rend2/lightrecon/' + x for x in ('candidate_pool.cpp','candidate_pool.h',
              'joint.cpp','joint.h','indirect.cpp','indirect.h','sampling.cpp','sampling.h')]
    write('manifest.json', dict(schema='org.openjk.scene-schedule-witness.v1',
          input_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          versions=dict(scene=1,producer=3,hidden=2,joint=4,shadow=2),
          sources={x:digest(ROOT / x) for x in files},
          artifacts={x:digest(output / x) for x in ('controlled.json','stock.json','checks.json')}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--private', type=Path, required=True)
    p.add_argument('--revision', required=True)
    p.add_argument('--repeat', required=True)
    p.add_argument('--controlled', type=Path, required=True)
    p.add_argument('--controlled-repeat', type=Path, required=True)
    p.add_argument('--checks', type=Path, required=True)
    p.add_argument('--output', type=Path, default=Path(__file__).parent/'results/v1')
    a = p.parse_args()
    collect(a.private,a.revision,a.repeat,a.controlled,a.controlled_repeat,a.checks,a.output)
