#!/usr/bin/env python3
"""S09 production CPU repetition, archive diagnostics and frozen X04 replay.
Standard library only. No Python emitter fitting or shadow association.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.amplitude_fit.benchmark import peak


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate(w):
    if w.get('schema') != 'org.openjk.shadow-fit-s09.v1' or w.get('failures') != 0 or w.get('checks', 0) < 200:
        raise ValueError('Failed or partial S09 witness')
    cases = w['ablations']
    if len(cases) != 15 or len({(c['seed_x'], c['mode']) for c in cases}) != 15:
        raise ValueError('Missing or duplicate A-E/multistart control')
    for seed in (-52, -44, -20):
        group = {c['mode']: c for c in cases if c['seed_x'] == seed}
        if set(group) != set(range(5)):
            raise ValueError('Incomplete ablation')
        for c in group.values():
            if not 0 <= c['heldout_mse'] < c['black_mse'] or c['black_mse'] <= 0:
                raise ValueError('Missing, black or invalid positive prediction')
        if group[4]['position_error_world'] >= 1 or group[4]['position_error_world'] + 2 >= group[0]['position_error_world']:
            raise ValueError('Automatic localization did not improve')
        if abs(group[4]['position_error_world'] - group[3]['position_error_world']) >= .5:
            raise ValueError('Unexpected oracle/automatic gap')
    edges = w['edge_candidate_confusion']
    if any(e['verified_cast_shadow'] for e in edges):
        raise ValueError('Gradient certified as a physical shadow')
    if not any(e['name'] == 'undisclosed_albedo_same_RGB' and e['candidate_admitted'] for e in edges):
        raise ValueError('Unidentifiable material control lost')
    associations = w['reported_association_confusion']
    if (associations['tp'], associations['fp'], associations['fn'], associations['tn']) != (2, 0, 0, 2):
        raise ValueError('Automatic source/caster confusion control failed')
    finite = w['finite_source']
    if abs(finite['reference_geometric_width'] - finite['predicted_geometric_width']) > 1e-5:
        raise ValueError('Finite-source width control failed')
    json.dumps(w, allow_nan=False)


def run(command, resources, label, timeout=180):
    start = time.perf_counter()
    with subprocess.Popen([str(x) for x in command], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as child:
        try:
            stdout, stderr = child.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise RuntimeError(f'{label}: time budget exceeded')
        rss = peak(child)
        if child.returncode:
            raise RuntimeError(label + '\n' + stdout + stderr)
    resources.append(dict(stage=label, seconds=time.perf_counter()-start, peak_process_rss_bytes=rss))
    return stdout


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--inspect', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--datasets', type=Path, default=ROOT/'build/x02/compiled-fixtures-final')
    p.add_argument('--bakes', type=Path, default=ROOT/'build/q3map2-bake-verified')
    p.add_argument('--boundary-bsp', type=Path, default=ROOT/'build/bake-model-comparison-final/authoring/base/maps/multi_sun.bsp')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    files = ['shared/rd-rend2/lightrecon/'+f for f in ('fit_evidence.h', 'shadow_evidence.h', 'shadow_evidence.cpp',
             'emitter_shape.h', 'emitter_shape.cpp', 'point_refine.h', 'point_refine.cpp', 'visibility.h', 'visibility.cpp')]
    files += ['tools/lightrecon_test/lightrecon_shadow_test.cpp', 'tools/lightrecon_test/lightrecon_shadow_inspect.cpp',
              'tools/rend2/shadow_fit/benchmark.py']
    sources = {f: sha(ROOT/f) for f in files}
    resources, witnesses = [], []
    for repeat in range(2):
        prefix = a.out/f'cpu-{repeat}'
        log = run([a.tool.resolve(), prefix.resolve()], resources, f'CPU-{repeat}')
        prefix.with_suffix('.log').write_text(log)
        w = json.loads(Path(str(prefix)+'-results.json').read_text())
        validate(w)
        witnesses.append(w)
    if witnesses[0] != witnesses[1]:
        raise ValueError('Repeated CPU results differ')
    compiled = {}
    manifest = json.loads((ROOT/'tools/rend2/datasets/q3map2-validation.json').read_text())
    for name in ('point_room', 'two_colour', 'door_occlusion'):
        bsp = a.bakes/'authoring/base/maps'/f'{name}.bsp'
        recipe = json.loads((a.bakes/'answers'/f'{name}.q3map-recipe.json').read_text())
        if sha(bsp) != recipe['compiled_bsp_sha256'] or sha(bsp) != manifest['maps'][name]['bsp_sha256']:
            raise ValueError('Compiled fixture changed: '+name)
        positions = recipe['ground_truth']['sources'] if 'sources' in recipe['ground_truth'] else None
        # Trusted controller supplies only the first fixture position, no secret RGB.
        entity = next(e for e in recipe['entities'] if e.get('classname') == 'light')
        xyz = entity['origin'].split()
        prefix = (a.out/name).resolve()
        archive = a.datasets/f'{name}.jkobs'
        run([a.inspect.resolve(), archive.resolve(), *xyz, prefix], resources, name)
        diagnostic = json.loads(prefix.with_suffix('.json').read_text())
        if not diagnostic['training_only'] or diagnostic['compiler_faithful'] or diagnostic['samples'] <= 0:
            raise ValueError('Invalid compiled diagnostic')
        compiled[name] = dict(bsp_sha256=sha(bsp), archive_sha256=sha(archive), supplied_point_xyz=[float(x) for x in xyz],
                              protocol='training-only diagnostic; first point prior, no source discovery', **diagnostic)
    boundary_out = a.out/'boundary'
    run([sys.executable, ROOT/'tools/rend2/shadow_footprint/compiler_boundary.py', '--bsp', a.boundary_bsp.resolve(),
         '--output', boundary_out.resolve()], resources, 'X04-unchanged-replay')
    b = json.loads((boundary_out/'boundary.json').read_text())
    if b['exact_affected_count'] != 35 or b['replay_all_floor_max_error'] != 0:
        raise ValueError('X04 boundary regression')
    if sources != {f: sha(ROOT/f) for f in files}:
        raise ValueError('Sources changed during benchmark')
    result = dict(schema='org.openjk.s09-benchmark.v1', deterministic=True, source_sha256=sources,
                  executable_sha256=sha(a.tool), inspect_sha256=sha(a.inspect), owned=witnesses[0], compiled=compiled,
                  boundary=dict(bsp_sha256=b['bsp_sha256'], former_excluded_texels=35, formerly_unchecked_channels=105,
                                full_floor_samples=1225, scoped_replay_max_rgb8_error=0,
                                old_physical_centre_max_rgb8_error=28, scope=b['replay_scope']))
    (a.out/'numerical.json').write_text(json.dumps(result, sort_keys=True, indent=2, allow_nan=False)+'\n')
    (a.out/'resources.json').write_text(json.dumps(resources, sort_keys=True, indent=2)+'\n')
    print(json.dumps(dict(checks=witnesses[0]['checks'], deterministic=True, compiled=compiled, resources=resources), indent=2))


if __name__ == '__main__':
    main()
