#!/usr/bin/env python3
"""ASR20 frozen compiled/owned witnesses. No renderer or asset publication.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/'tools/rend2/datasets'))
from tools.rend2.datasets.solver_view import TrainingView
from tools.rend2.datasets.evaluation_view import EvaluationView
from tools.rend2.joint.compiled import select


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def block(row):
    cell = [math.floor(v/128) for v in row['position']]
    return 1+sum((v+1000)*10000**k for k, v in enumerate(cell))


def rows_text(rows):
    lines = [str(len(rows))]
    for row in rows:
        lines.append(' '.join(map(str, [int(row['id'][:15], 16)+1, block(row), row['surface'],
            *row['position'], *row['normal'], row['area'], *row['raw']])))
    return lines


def native(tool, geometry, fit, held):
    result = subprocess.run([str(tool), str(geometry), str(fit), str(held)],
                            capture_output=True, text=True, timeout=300)
    if result.returncode:
        raise ValueError(result.stderr)
    return result.stdout, json.loads(result.stdout)


def compiled(a, name, folder):
    answer_path = a.bakes/'answers'/(name+'.q3map-recipe.json')
    answer = load(answer_path)
    bsp = a.bakes/'authoring/base/maps'/(name+'.bsp')
    lock = load(ROOT/'tools/rend2/datasets/q3map2-validation.json')
    bsp_hash = sha(bsp)
    if (answer['compiled_bsp_sha256'] != bsp_hash or
            lock['maps'][name]['bsp_sha256'] != bsp_hash or
            answer['binary_sha256'] != lock['compiler']['executable_sha256']):
        raise ValueError('Frozen compiler/BSP mismatch')
    view = a.views/name/'views'
    train = TrainingView(view/'inputs', bsp_hash)
    evaluator = EvaluationView(view/'evaluator', bsp_hash, permitted=('validation', 'heldout'))
    partitions = [select(train.observations(), 48)]
    partitions += [select(evaluator.observations(role), 48) for role in ('validation', 'heldout')]
    # Membership, geometry, normal and stable ID only; target values never select rows.
    seen_ids, seen_blocks = set(), set()
    for rows in partitions:
        ids = {r['id'] for r in rows}
        blocks = {block(r) for r in rows}
        if len(rows) < 8 or seen_ids & ids or seen_blocks & blocks:
            raise ValueError('Missing/disjoint compiled spatial coverage')
        seen_ids |= ids
        seen_blocks |= blocks
    # Explicit fixture coordinates are a benchmark prior. Authored RGB/intensity
    # are deliberately not read into the native request.
    anchors = [source['position_world'] for source in answer['ground_truth']['lights']]
    if name == 'two_colour':
        # A third explicit rival is fixed geometrically before fitting. It tests
        # redundant fixture competition, not discovery of a historical third lamp.
        anchors.append([sum(p[k] for p in anchors)/len(anchors) for k in range(3)])
    lines = ['JKASR20-ENCODED-ASSUMED-MATERIAL-1', '1', '8 8 8', str(len(anchors))]
    lines += [' '.join(map(str, ['point', i+1, *p, 100000, 100000, 100000])) for i, p in enumerate(anchors)]
    lines += rows_text(partitions[0])+rows_text(partitions[1])
    fit, held = folder/(name+'-fit.txt'), folder/(name+'-held.txt')
    fit.write_text('\n'.join(lines)+'\n')
    held.write_text('\n'.join(rows_text(partitions[2]))+'\n')
    freeze = dict(bsp_sha256=bsp_hash, compiler_sha256=answer['binary_sha256'],
                  geometry_sha256=sha(train.geometry_path), fit_sha256=sha(fit), held_sha256=sha(held),
                  partitions={role: sha(view/('inputs' if role == 'train' else 'evaluator')/(role+'.jsonl'))
                              for role in ('train', 'validation', 'heldout')},
                  selection='Geometry-only stable spatial strata, maximum 48 rows per fold',
                  anchors=anchors, authored_rgb_or_intensity_in_request=False)
    write(folder/(name+'-protocol.json'), freeze)
    raw, report = native(a.tool, train.geometry_path, fit, held)
    repeat, _ = native(a.tool, train.geometry_path, fit, held)
    if raw != repeat:
        raise ValueError('Nondeterministic compiled report')
    (folder/(name+'.json')).write_text(raw)
    chosen = report['selected']
    if chosen < 0:
        raise ValueError('No validated local light on compiled positive '+name)
    score = report['configurations'][chosen]['heldout']
    base = report['baseline']['heldout']
    if not score['available'] or score['robust'] >= base['robust']*.5:
        raise ValueError('Compiled held-out local-light improvement failed '+name)
    if (sha(bsp) != freeze['bsp_sha256'] or sha(train.geometry_path) != freeze['geometry_sha256'] or
            sha(fit) != freeze['fit_sha256'] or sha(held) != freeze['held_sha256'] or
            any(sha(view/('inputs' if role == 'train' else 'evaluator')/(role+'.jsonl')) != digest
                for role, digest in freeze['partitions'].items())):
        raise ValueError('Compiled inputs changed during evaluation')
    return dict(name=name, protocol=freeze, result=report, repeat_identical=True)


def stock(a):
    # Fixed named region and radius; no search over RGB or post-score retries.
    folder = a.stock_work or ROOT/'build'/('asr20-stock-'+a.out.name)
    folder.mkdir(parents=True, exist_ok=False)
    command = [str(a.tool), str(a.stock_archive), '--export-region', '1027', str(folder), '512']
    selection = subprocess.run(command, capture_output=True, text=True, timeout=300)
    if selection.returncode:
        report = dict(available=False, reason=selection.stderr.strip(),
                      archive_sha256=sha(a.stock_archive), selection='Fixed mp/duel4 surface 1027, radius 512')
        write(a.out/'stock.json', report)
        return
    fit, held = folder/'fit.txt', folder/'held.txt'
    frozen = dict(archive_sha256=sha(a.stock_archive), fit_sha256=sha(fit), held_sha256=sha(held),
                  selection=json.loads(selection.stdout), radius=512,
                  spatial_blocks=dict(size=256, offset=[17, 31, 47], seed=20261109,
                                      gap='3 * exact source-UV footprint radius + 1 WU'),
                  raw_geometry_and_samples='Private work directory only', frozen_before_fitting=True)
    write(a.out/'stock-protocol.json', frozen)
    raw, report = native(a.tool, a.stock_archive, fit, held)
    repeated, _ = native(a.tool, a.stock_archive, fit, held)
    if raw != repeated:
        raise ValueError('Nondeterministic stock result')
    if (sha(a.stock_archive) != frozen['archive_sha256'] or sha(fit) != frozen['fit_sha256'] or
            sha(held) != frozen['held_sha256']):
        raise ValueError('Stock inputs changed during evaluation')
    (folder/'result.json').write_text(raw)
    for config in report['configurations']:
        for source in config['sources']:
            source.pop('position')
    write(a.out/'stock.json', dict(result=report, repeat_identical=True,
                                  private_result_sha256=sha(folder/'result.json')))


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    files = [ROOT/'tools/lightrecon_test'/n for n in
             ('aesthetic_fit.h', 'aesthetic_fit.cpp', 'lightrecon_aesthetic_test.cpp',
              'aesthetic_sparse.inc', 'aesthetic_test_scene.h',
              'lightrecon_aesthetic_tool.cpp', 'CMakeLists.txt')]
    files += [ROOT/'tools/lightrecon_test/aesthetic_region.h', Path(__file__).resolve()]
    core = sorted((ROOT/'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    core += [ROOT/'shared/rd-rend2'/name for name in ('tr_lightrecon_types.cpp', 'tr_lightrecon_types.h')]
    frozen = {p.relative_to(ROOT).as_posix(): sha(p) for p in files+core}
    prior = load(ROOT/'tools/rend2/shape_selection/results/asr19-v2/protocol.json')['sources']
    for path in core:
        name = path.relative_to(ROOT).as_posix()
        if prior.get(name) != frozen[name]:
            raise ValueError('Historical core or canonical storage changed since ASR19: '+name)
    protocol = dict(schema='org.openjk.aesthetic-protocol.v1', sources=frozen,
                    tool_sha256=sha(a.tool), test_sha256=sha(a.test),
                    frozen_before_evaluation=True, target_domain='Forward encoded RGB8 appearance',
                    modern_transport='DiffuseIntegral relative incident; fixed gauge=1',
                    validation_equivalent_slack=1e-4,
                    selection='Fewest fixtures among validation-equivalent improvements; all alternatives retained',
                    nuisance='Spatially constant nonnegative RGB, bounded [0,8] before encoding',
                    scope='Offline conditional aesthetic candidates only', runtime_changed=False)
    write(a.out/'protocol.json', protocol)
    for name in ('owned', 'owned-repeat'):
        subprocess.run([str(a.test), str((a.out/(name+'.json')).resolve())], check=True, timeout=300)
    if (a.out/'owned.json').read_bytes() != (a.out/'owned-repeat.json').read_bytes():
        raise ValueError('Nondeterministic owned report')
    cases = [compiled(a, name, a.out) for name in ('point_room', 'two_colour', 'door_occlusion')]
    write(a.out/'compiled.json', dict(cases=cases, historical_source_truth='Unavailable'))
    if a.stock_archive:
        stock(a)
    if any(sha(ROOT/p) != digest for p, digest in frozen.items()):
        raise ValueError('Sources changed during evaluation')
    if sha(a.tool) != protocol['tool_sha256'] or sha(a.test) != protocol['test_sha256']:
        raise ValueError('Native binaries changed during evaluation')
    write(a.out/'manifest.json', dict(sources_unchanged=True, historical_core_matches_asr19=True, owned_repeat_identical=True,
          compiled_repeat_identical=True, canonical_storage_changed=False, runtime_changed=False,
          artifacts={p.name: sha(p) for p in a.out.iterdir() if p.is_file()}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--views', type=Path, default=ROOT/'build/x07-verified')
    parser.add_argument('--bakes', type=Path, default=ROOT/'build/q3map2-bake-verified')
    parser.add_argument('--tool', type=Path, default=ROOT/'build/s06-gcc/lightrecon_aesthetic_tool.exe')
    parser.add_argument('--test', type=Path, default=ROOT/'build/s06-gcc/lightrecon_aesthetic_test.exe')
    parser.add_argument('--stock-archive', type=Path)
    parser.add_argument('--stock-work', type=Path, help='Private, fresh directory for licensed stock rows/geometry')
    main(parser.parse_args())
