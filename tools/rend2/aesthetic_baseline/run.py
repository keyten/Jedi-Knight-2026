#!/usr/bin/env python3
"""Freeze/replay the existing S04 source-discovery baseline, without a solver change.

All requests and dependencies freeze before any native evaluation. Licensed raw
diagnostics stay in --work; --out contains only counts, IDs, hashes and blockers.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.hidden_candidates.run import choose, request, semantic, TrainingView
from tools.rend2.aesthetic_baseline.acceptance import assess

HERE = Path(__file__).resolve().parent
TOOLS = ('hidden_tool', 'map_solver', 'hidden_test', 'sun_test', 'joint_test', 'producer_test')
REFERENCES = ('docs/rend2-reusable-lighting-validation-v02.md',
              'docs/rend2-stock-joint-fits-x12.md', 'docs/rend2-blind-discovery-x16.md',
              'tools/rend2/validation/policy-v1.json', 'tools/rend2/validation/results/v1/manifest.json',
              'tools/rend2/stock_joint_fits/results/v1/protocol.json',
              'tools/rend2/stock_joint_fits/results/v1/manifest.json',
              'tools/rend2/discovery_x16/results/v1/manifest.json',
              'tools/rend2/bake_model/source-lock.json')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def pin(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=sha(path), bytes=path.stat().st_size)


def verify(item):
    if sha(item['path']) != item['sha256']:
        raise ValueError('Frozen input changed: ' + item['path'])


def freeze(a):
    policy = read(HERE / 'policy-v1.json')
    a.work.mkdir(parents=True, exist_ok=False)
    native = a.work / 'native'; native.mkdir()
    tools = {}
    for name in TOOLS:
        filename = 'lightrecon_' + name + '.exe'
        shutil.copyfile(a.native / filename, native / filename)
        tools[name] = pin(native / filename)
    protocol = dict(schema='org.openjk.aesthetic-baseline-protocol/1',
                    base_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    policy=pin(HERE / 'policy-v1.json'), tools=tools, cases={},
                    environment=dict(os=platform.platform(), cpu=platform.processor(),
                                     python=platform.python_version(), logical_cpus=os.cpu_count()),
                    references={name: pin(ROOT / name) for name in REFERENCES})
    files = list((ROOT / 'shared/rd-rend2/lightrecon').glob('*.[ch]*'))
    files += list((ROOT / 'tools/lightrecon_test').glob('*.[ch]*'))
    files += list(HERE.glob('*.py'))
    files += list((ROOT / 'shared/rd-rend2').glob('tr_lightrecon*.[ch]*'))
    files += [ROOT / name for name in ('shared/rd-rend2/tr_pbr_contract.h',
              'shared/rd-rend2/lightrecon/CMakeLists.txt', 'tools/lightrecon_test/CMakeLists.txt')]
    files += [ROOT / name for name in ('tools/rend2/hidden_candidates/run.py',
              'tools/rend2/path_support/run.py', 'tools/rend2/all_stock/run.py',
              'tools/rend2/datasets/solver_view.py', 'tools/rend2/datasets/evaluation_view.py')]
    protocol['sources'] = {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(files) if p.is_file()}
    for name in [*policy['positive_cases'], *policy['negative_cases'], 'hoth3_declared_roi']:
        stock = name == 'hoth3_declared_roi'
        inputs = a.stock_roi if stock else a.fixtures / (name + '-views/inputs')
        if stock and not inputs.exists():
            protocol['cases'][name] = dict(state='Blocked', reason='Licensed stock TrainingView not mounted')
            continue
        manifest = read(inputs / 'manifest.json')
        training = TrainingView(inputs, manifest['map_sha256'])
        rows = choose(training, policy['stock_roi']['centre'] if stock else None,
                      policy['stock_roi']['radius_world'], 64)
        if len(rows) < 6:
            raise ValueError('Insufficient predeclared training geometry: ' + name)
        if stock:
            centre = policy['stock_roi']['centre']; radius = policy['stock_roi']['radius_world']
            bounds = [*[v-radius for v in centre], *[v+radius for v in centre]]
        else:
            bounds = [*[min(r['position'][i] for r in rows)-128 for i in range(3)],
                      *[max(r['position'][i] for r in rows)+128 for i in range(3)]]
        folder = a.work / name; folder.mkdir()
        req = folder / 'training.request'
        # Only the declared positive direct-only controls attest direct lighting.
        req.write_text(request(rows, manifest['map_sha256'], bounds, bounds,
                               name in policy['positive_cases']), encoding='ascii')
        partitions = {}
        for role in ('train', 'validation', 'heldout'):
            directory = inputs if role == 'train' else inputs.parent / 'evaluator'
            m = read(directory / 'manifest.json')
            target = directory / (role + '.jsonl')
            item = pin(target)
            if item['sha256'] != m['sha256'][target.name]:
                raise ValueError('Partition hash mismatch')
            partitions[role] = dict(file=item, manifest=pin(directory / 'manifest.json'),
                                    count=m['counts'][role], seed=m['seed'], block_world=m['block_world'],
                                    gap_world=m['gap_world'], footprint_filter_multiplier=m['footprint_filter_multiplier'])
        case = dict(state='Frozen', stock=stock, map_sha256=manifest['map_sha256'],
                    geometry=pin(training.geometry_path), request=pin(req), partitions=partitions,
                    selected_training_ids=[r['id'] for r in rows],
                    selected_surfaces=len({r['surface'] for r in rows}),
                    search_bounds=bounds if not stock else 'Private declared ROI; see policy')
        if not stock:
            case['answer'] = pin(a.answers / (name + '.q3map-recipe.json'))
            bsp = a.answers.parent / 'authoring/base/maps' / (name + '.bsp')
            case['compiled_bsp'] = pin(bsp)
            if case['compiled_bsp']['sha256'] != manifest['map_sha256']:
                raise ValueError('Original compiled BSP does not match TrainingView')
            count = dict(point_room=1, two_colour=2, skylight=0)[name]
            case['source_ids'] = [manifest['map_sha256'] + '/local-light/' + str(i) for i in range(count)]
        protocol['cases'][name] = case
    dataset = a.private / 'runs/mp_duel4/all-pages.jkobs'
    if not dataset.exists():
        dataset = a.private / 'runs-v4/mp_duel4/all-pages.jkobs'
    captures = a.private / 'captures.json'
    if not dataset.exists() and captures.exists():
        captured = read(captures).get(policy['stock_scene'], {}).get('dataset')
        if captured:
            dataset = Path(captured)
    inventory_path = a.private / 'inventory.json'
    if dataset.exists() and inventory_path.exists():
        inventory = read(inventory_path)
        item = next(m for m in inventory['maps'] if m['map'] == policy['stock_scene'])
        expected = read(ROOT / 'tools/rend2/stock_joint_fits/results/v1/protocol.json')
        old = next(m for m in expected['maps'] if m['map'] == policy['stock_scene'])
        if sha(dataset) != old['dataset_sha256'] or sha(inventory_path) != expected['inventory_sha256']:
            raise ValueError('Stock dataset differs from frozen X12 input')
        protocol['stock_scene'] = dict(state='Frozen', dataset=pin(dataset), inventory=pin(inventory_path),
                                      bsp_sha256=item['sha256'], mode='scene-receiver-preflight')
    else:
        protocol['stock_scene'] = dict(state='Blocked', reason='Licensed original stock all-pages archive/inventory not mounted')
    write(a.work / 'protocol.json', protocol)
    print('Frozen protocol:', a.work / 'protocol.json', flush=True)


def evaluate(a):
    protocol_path = a.work / 'protocol.json'; p = read(protocol_path)
    verify(p['policy'])
    for relative, digest in p['sources'].items():
        if sha(ROOT / relative) != digest:
            raise ValueError('Source changed after freeze: ' + relative)
    for item in [*p['tools'].values(), *p['references'].values()]:
        verify(item)
    # Verify every experiment input before starting any native child.
    for c in p['cases'].values():
        if c['state'] != 'Frozen':
            continue
        for item in [c['geometry'], c['request'], *([c['answer'], c['compiled_bsp']] if 'answer' in c else [])]:
            verify(item)
        for fold in c['partitions'].values():
            verify(fold['file']); verify(fold['manifest'])
    if p['stock_scene']['state'] == 'Frozen':
        verify(p['stock_scene']['dataset']); verify(p['stock_scene']['inventory'])
    a.out.mkdir(parents=True, exist_ok=False)
    policy = read(p['policy']['path']); results = {}; resources = []
    for name, c in p['cases'].items():
        if c['state'] != 'Frozen':
            results[name] = c; continue
        folder = a.work / name
        values = []
        for label in ('first', 'repeat'):
            receipt = execute([p['tools']['hidden_tool']['path'], c['geometry']['path'], c['request']['path']],
                              folder, label, seconds=180)
            resources.append(receipt)
            if receipt['exit_code'] or receipt['state'] != 'Completed':
                raise RuntimeError('Native discovery process failed: ' + name)
            values.append(read(folder / (label + '.log')))
        d = values[0]; candidates = d['candidates']
        summary = dict(code=d['code'], reason=d['reason'], candidates=len(candidates),
                       search_complete=d['search_complete'], geometry_global_complete=d['geometry_global_complete'],
                       candidate_ids=[x['id'] for x in candidates],
                       candidate_kinds=dict(Counter(str(x['kind']) for x in candidates)),
                       repeat_equal=semantic(d) == semantic(values[1]),
                       counters={k: d[k] for k in ('forward_calls', 'rays', 'work', 'accounted_bytes')},
                       areas=d['areas'], diagnostics=d['diagnostics'],
                       output_sha256=[sha(folder / (label + '.log')) for label in ('first', 'repeat')],
                       semantic_sha256=hashlib.sha256(json.dumps(semantic(d), sort_keys=True).encode()).hexdigest(),
                       accepted_local_sources=0, accepted_source_ids=[],
                       accepted_source_evaluation='Unperformed: proposal-only adapter',
                       wall_leaks=None, source_fixture_consistency=None, final_lightmap_agreement=None)
        # Private answers first open after both proposal outputs are immutable.
        if not c['stock']:
            answer = read(c['answer']['path'])
            if answer['compiled_bsp_sha256'] != c['map_sha256']:
                raise ValueError('Answer belongs to another BSP')
            lights = answer['ground_truth']['lights']
            if len(lights) != len(c['source_ids']):
                raise ValueError('Source cardinality differs from frozen definition')
            nearest = [min((math.dist(s['position_world'], x['position']) for x in candidates), default=None) for s in lights]
            summary['nearest_proposal_world'] = dict(zip(c['source_ids'], nearest))
            summary['proposal_coverage'] = {str(t): dict(found=sum(v is not None and v <= t for v in nearest),
                    required=len(lights), recall=sum(v is not None and v <= t for v in nearest)/len(lights) if lights else None)
                    for t in policy['matching_world']}
            summary['no_local_light_control'] = not lights
            summary['unmatched_control_proposals'] = len(candidates) if not lights else None
        results[name] = summary
        print(name, 'proposals', len(candidates), 'repeat', summary['repeat_equal'], flush=True)
    scene = p['stock_scene']
    if scene['state'] == 'Frozen':
        from tools.rend2.path_support.run import summarize
        folder = a.work / 'stock-scene'; folder.mkdir()
        receipt = execute([p['tools']['map_solver']['path'], scene['dataset']['path'], folder,
                           scene['bsp_sha256'], scene['mode']], folder, 'native', seconds=900)
        resources.append(receipt)
        if receipt['exit_code'] or receipt['state'] != 'Completed':
            raise RuntimeError('Stock scene process failed')
        scene_result = summarize(folder)
        write(a.out / 'stock.json', scene_result)
    else:
        scene_result = scene
        write(a.out / 'stock.json', scene)
    suites = {}
    for name in ('hidden_test', 'sun_test', 'joint_test', 'producer_test'):
        folder = a.work / name; folder.mkdir()
        command = [p['tools'][name]['path']]
        if name == 'producer_test':
            command.append(folder / 'cache')
        command.append(folder / 'result.json')
        receipt = execute(command, folder, 'native', seconds=600)
        resources.append(receipt)
        suites[name] = dict(process=receipt['state'], exit_code=receipt['exit_code'],
                            result=read(folder / 'result.json') if (folder / 'result.json').exists() else None)
        print(name, receipt['state'], receipt['exit_code'], flush=True)
    metrics = {name: None for name in policy['required_metrics']}
    positive = [results[n] for n in policy['positive_cases']]
    metrics.update(owned_cases_evaluated=sum(n in results for n in policy['positive_cases'] + policy['negative_cases']),
                   minimum_positive_proposal_recall_32=min(c['proposal_coverage']['32']['recall'] for c in positive),
                   accepted_positive_scenes=0, accepted_local_sources=0,
                   repeat_semantics_equal=int(all(c.get('repeat_equal', False) for c in results.values())),
                   stock_scene_evaluated=1 if scene['state'] == 'Frozen' else None)
    write(a.out / 'results.json', dict(cases=results, metrics=metrics, acceptance=assess(metrics, policy),
              native_suites=suites, process_passed=all(r['exit_code'] == 0 and r['state'] == 'Completed' for r in resources),
              protocol_sha256=sha(protocol_path), resources=resources,
              scope='Proposal replay plus stock rejection audit; no accepted discovery fit is manufactured'))
    # The protocol includes paths/IDs but no stock positions, RGB or licensed geometry.
    shutil.copyfile(protocol_path, a.out / 'protocol.json')
    write(a.out / 'manifest.json', dict(artifacts={f.name: sha(f) for f in sorted(a.out.iterdir()) if f.is_file()},
                                        licensed_payloads=False, production_modified=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('freeze', 'evaluate'))
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--native', type=Path, default=ROOT / 'build/s06-gcc')
    parser.add_argument('--fixtures', type=Path, default=ROOT / 'build/x02/compiled-fixtures-frozen')
    parser.add_argument('--answers', type=Path, default=ROOT / 'build/q3map2-bake-verified/answers')
    parser.add_argument('--stock-roi', type=Path, default=ROOT / 'build/x02/hoth3-views/inputs')
    parser.add_argument('--private', type=Path, default=Path(os.environ.get('TEMP', '/tmp')) / 'jk-lightrecon-all-stock-20261031')
    args = parser.parse_args()
    args.work = args.work.resolve()
    if args.phase == 'evaluate' and args.out is None:
        parser.error('evaluate requires --out')
    (freeze if args.phase == 'freeze' else evaluate)(args)
