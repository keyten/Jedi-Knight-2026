#!/usr/bin/env python3
"""Reduce an X03 run to public scalar witnesses/tables; no stock observations.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from identifiability import write


def short(identifier):
    return identifier.replace('NetRadiant-d9338309-JA/', 'NR/')


def comparisons(result):
    for name, case in result['synthetic'].items():
        for protocol, comparison in case['protocols'].items():
            yield 'synthetic', name, protocol, case['truth_profile'], comparison
    for name, case in result['compiled']['comparisons'].items() if result['compiled'] else []:
        for protocol in ('fixed', 'fitted_rgb', 'variable_height'):
            yield 'compiled', name, protocol, 'known recipe (see fixture name)', case[protocol]
    for name, case in result['sensitivity'].items():
        yield 'sensitivity', name, 'sensitivity', 'NR/isq_cos', case['comparison']
    for name, case in result['stock'].items() if result['stock'] else []:
        for protocol, trial in case.get('trials', {}).items():
            if 'comparison' in trial:
                yield 'stock', name, protocol, 'UnknownProfile', trial['comparison']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--fresh-compiler-report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    raw = args.run.read_bytes()
    result = json.loads(raw)
    table = []
    witness = dict(schema='org.openjk.bake-identifiability-witness.v1',
                   run_sha256=hashlib.sha256(raw).hexdigest(), source_sha256=result['source_sha256'],
                   catalogue_sha256=result['input_catalogue_sha256'],
                   source_lock_sha256=result['source_lock_sha256'],
                   source_lock_validation=result['source_lock_validation'],
                   claims=result['claims'], analytic=result['analytic'],
                   comparisons=[], stock={}, compiler_replay={})
    for domain, name, protocol, truth, comparison in comparisons(result):
        items = []
        for item in comparison['results']:
            if not item.get('validation'):
                continue
            v, h = item['validation'], item['heldout']
            entry = dict(profile=short(item['id']), configuration=item['configuration'],
                         parameter_count=item['parameter_count'], parameters=item['parameters'],
                         encoding=item['encoding'], validation_rmse=v['interval_rmse'],
                         heldout_rmse=h['interval_rmse'], heldout_byte_rmse=h['byte_rmse'],
                         validation_score=item['validation_score'],
                         complexity_penalty=item['complexity_penalty'],
                         validation_blocks=v['blocks'], heldout_blocks=h['blocks'],
                         profile_sha256=item['hypothesis_sha256'],
                         evaluation_sha256=item['evaluation_sha256'])
            items.append(entry)
            table.append(dict(domain=domain, case=name, protocol=protocol, truth=short(truth), **entry))
        witness['comparisons'].append(dict(domain=domain, case=name, protocol=protocol,
            truth=short(truth), ranking=list(map(short, comparison['ranking'])),
            policy=comparison['policy'], results=items))
    if result['compiled']:
        witness['compiler_replay'] = dict(compiler_sha256=result['compiled']['compiler_sha256'],
            verified_cases=len(result['compiled']['verification']),
            full_bounce_replay=False, new_invocation_in_replay=False)
    if args.fresh_compiler_report:
        data = args.fresh_compiler_report.read_bytes()
        compiler = json.loads(data)
        witness['fresh_compiler_run'] = dict(sha256=hashlib.sha256(data).hexdigest(),
            compiler_sha256=compiler['compiler_sha256'],
            cases={name:dict(passed=case['passed'], bsp_sha256=case['bsp_sha256'])
                   for name, case in compiler['cases'].items()})
        if not all(v['passed'] for v in witness['fresh_compiler_run']['cases'].values()):
            raise ValueError('Fresh compiler witness has failed cases')
    for name, data in (result['stock'] or {}).items():
        witness['stock'][name] = {k: data[k] for k in ('source_map_sha256', 'partition_protocol',
            'outer_evaluation_targets_loaded', 'selected_counts', 'common_certified_counts',
            'unknown_rays_excluded', 'certified_ray_response', 'historical_profile', 'limitations') if k in data}
        witness['stock'][name]['trial_statuses']={k:v.get('status','ConditionalEvidence') for k,v in data.get('trials',{}).items()}
    write(args.output/'witness.json', witness)
    with (args.output/'metrics.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(table[0]))
        writer.writeheader()
        for row in table:
            writer.writerow({k: json.dumps(v, sort_keys=True) if isinstance(v, (list, dict)) else v
                             for k, v in row.items()})
    lines = ['<!-- Generated by report_x03.py; interval RMSE in encoded RGB8 bytes. -->',
             '| Domain | Case | Protocol | Validation preference | Validation / held-out RMSE |',
             '|---|---|---|---|---|']
    for item in witness['comparisons']:
        if not item['ranking']:
            continue
        best = next(r for r in item['results'] if r['profile'] == item['ranking'][0])
        lines.append('| {} | {} | {} | {} | {:.4f} / {:.4f} |'.format(item['domain'],
            item['case'], item['protocol'], best['profile'], best['validation_rmse'], best['heldout_rmse']))
    (args.output/'tables.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('Wrote scalar witness, full metric CSV and preference table; stock compiler remains UnknownProfile')


if __name__ == '__main__':
    main()
