#!/usr/bin/env python3
"""Post-run X17 comparison; never selects a source or reads withheld targets."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.rend2.all_stock.run import sha, write
from tools.rend2.path_support.run import MAPS, summarize


def read(path): return json.loads(path.read_text(encoding='utf-8'))


def source_attempts(record):
    return [a for b in record['branches'] for a in b.get('attempts', []) if a['sources']]


def coverage(record):
    return sorted({(a['coverage_used'], a['coverage_admitted']) for a in source_attempts(record)
                   if a['coverage_admitted']})


def coverage_text(record):
    return ', '.join(f'{used}/{admitted}' for used, admitted in coverage(record)) or 'placement stop'


def cpu_result(text):
    match = re.search(r'(\d+)% tests passed(?:, (\d+) tests failed out of| out of) (\d+)', text)
    seconds = re.search(r'Total Test time \(real\) =\s*([\d.]+)', text)
    return dict(passed=bool(match and match[1] == '100' and match[2] in (None, '0')),
                total=int(match[3]) if match else None,
                seconds=float(seconds[1]) if seconds else None)


def main(a):
    a.out.mkdir(parents=True, exist_ok=False)
    before, after = read(a.before/'stock.json'), read(a.after/'stock.json')
    if tuple(before) != MAPS or tuple(after) != MAPS: raise ValueError('Incomplete cohort')
    checks = {}; comparison = {}; table = []
    frozen = read(a.after/'protocol.json')
    checks['final_source_identity'] = all(sha(ROOT/path) == digest
        for path, digest in frozen['source_hashes'].items())
    for name in MAPS:
        old, new = before[name], after[name]
        checks[name+'/input'] = old['input'] == new['input']
        checks[name+'/receiver_denominator'] = all(old['receivers'][k] == new['receivers'][k]
            for k in ('selected_manifest_key', 'selected_roles', 'full_census_key', 'training_positive_common'))
        comparison[name] = dict(input=new['input'], receivers=new['receivers'], profiles={},
                                before_resources=old['resources'], after_resources=new['resources'])
        for profile in ('isq', 'linear'):
            x, y = old['profiles'][profile], new['profiles'][profile]
            attempts = source_attempts(y)
            checks[name+'/'+profile+'/coverage_gate'] = all(
                t['status'] not in ('Converged', 'Partial') for t in attempts
                if t['coverage_admitted'] and t['coverage_used'] < t['coverage_admitted'])
            # Valuable raw geometry stays local; refreshed reports contain only
            # aggregate geometry/transport and bounded stable IDs/error reasons.
            raw_folder = Path(new['resources']['command'][2])
            comparison[name]['transport'] = read(raw_folder/'transport.json')
            comparison[name]['profiles'][profile] = dict(
                before_coverage=coverage(x), after_coverage=coverage(y),
                source_attempts=len(attempts),
                complete_nonempty_attempts=sum(t['status'] in ('Converged', 'Partial') for t in attempts),
                supported_positive_training_rows=sorted({t.get('supported_positive_training_rows', 0)
                    for t in attempts if t['coverage_admitted']}),
                placement=dict(checks=y['placement_checks'], conditional=y['conditional_placements'],
                               collision=y['collision_placements'], unknown=y['unknown_placements']),
                missing_path_issues=y['missing_path_issue_counts'],
                final_calls=y['controller']['final_calls'], heldout_interval_mse=None,
                canonical_asset=y['controller']['canonical_asset_available'],
                attempts=attempts)
            table.append(f'| {name} | {profile} | {coverage_text(x)} | {coverage_text(y)} | '
                         f'{y["fitting_forward_calls"]} | {y["controller"]["final_calls"]} |')
    if not all(checks.values()): raise ValueError('Input/domain/coverage gate regression: '+str(checks))
    write(a.out/'comparison.json', dict(schema='org.openjk.source-path-comparison/1', checks=checks,
          maps=comparison, stock_source_ground_truth=None, absolute_photometry=None,
          missing_predictions_are_zero=False, acceptance_gate_changed=False))
    for label, root in (('before', a.before), ('after', a.after)):
        # Original protocol/run reports remain byte-identical, with their hashes.
        write(a.out/(label+'.json'), read(root/'stock.json'))
        write(a.out/(label+'-protocol.json'), read(root/'protocol.json'))
    cpu = cpu_result((ROOT/'build/x17-ctest-done.log').read_text())
    render_log = (ROOT/'build/x17-renderers-done.log').read_text()
    validation = dict(cpu=cpu,
        renderer_dlls={n: dict(built=n in render_log, sha256=sha(ROOT/'build/x16-renderers/Release'/n))
                       for n in ('rdsp-rend2_x86_64.dll','rd-rend2_x86_64.dll')},
        controlled=read(a.controlled/'native.json'),
        supplied_geometry_controls=read(a.controls/'compiled.json'),
        disabled_delta=read(ROOT/'build/x17-disabled-delta/report.json'),
        historical_shader_baseline=read(ROOT/'build/x17-disabled-gpu/report.json'),
        intermediate_bias_fixture=dict(result=read(ROOT/'build/x17-scope-bias-native.json'),
            sha256=sha(ROOT/'build/x17-scope-bias-native.json'),
            reason='First fixture ray was tangent, so the static API applied no normal bias; corrected to a non-tangent ray and requires positive applied bias in both controls'),
        game_launches=0, renderer_consumers_enabled=False,
        logs={p.name: sha(p) for p in (ROOT/'build').glob('x17-*.log')})
    write(a.out/'validation.json', validation)
    repeat = read(a.repeat/'stock.json'); repeat_checks={}
    for name, rerun in repeat.items():
        original=after[name]
        for profile in ('isq','linear'):
            # Ray/work/model identities and admission are deterministic; time/RSS
            # and diagnostic extra-work timings are not numerical evidence.
            repeat_checks[name+'/'+profile] = rerun['profiles'][profile] == original['profiles'][profile]
        repeat_checks[name+'/input_domain'] = all(rerun['receivers'][k] == original['receivers'][k]
            for k in ('selected_manifest_key','selected_roles','full_census_key','categories'))
    write(a.out/'repeat.json', dict(checks=repeat_checks, passed=all(repeat_checks.values()),
          resources={name:r['resources'] for name,r in repeat.items()}))
    if not all(repeat_checks.values()): raise ValueError('Repeat numerical difference')
    (a.out/'tables.md').write_text('# X17 frozen common-domain comparison\n\n'
        '| Map | Profile | Before required rows | After required rows | Forward calls | Final calls |\n'
        '|---|---|---|---|---:|---:|\n'+'\n'.join(table)+'\n\n'
        'These are source-bearing attempts, including repeated scene waves. No accepted nonempty model; '
        'final held-out errors are unavailable. Unknown geometry is retained.\n', encoding='utf-8')
    for path in a.out.iterdir():
        if path.is_file(): path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))
    write(a.out/'manifest.json', dict(schema='org.openjk.source-path-artifacts/1',
          before_manifest_sha256=sha(a.before/'manifest.json'), after_manifest_sha256=sha(a.after/'manifest.json'),
          baseline_v02_sha256=sha(ROOT/'tools/rend2/validation/results/v1/stock.csv'),
          artifacts={p.name:sha(p) for p in a.out.iterdir() if p.is_file() and p.name!='manifest.json'},
          original_frozen_runs_overwritten=False, copyrighted_geometry_committed=False))
    path=a.out/'manifest.json'; path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('before','after','repeat','controlled','controls','out'):
        p.add_argument('--'+name,type=Path,required=True)
    main(p.parse_args())
