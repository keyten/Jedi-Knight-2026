"""Check measured cases, frozen inputs, resource caps and repeat determinism."""
import argparse
import hashlib
import json
from pathlib import Path
from .run import pin, read, write


def verify(folder, repeat=None):
    protocol = read(folder/'protocol.json')
    policy = read(protocol['policy']['path'])
    result = read(folder/'results.json')
    checks = []

    def check(ok, description):
        if not ok: raise AssertionError(description)
        checks.append(description)

    for item in [protocol['policy'], *protocol['code'].values(),
                 *(item for scene in protocol['inputs'].values() for item in scene.values())]:
        check(pin(item['path'])['sha256'] == item['sha256'], 'Frozen input/code hash: '+str(item['path']))
    check(len(result['cases']) == 20, 'All 20 cases returned')
    for name, record in result['cases'].items():
        r = record['result']
        check(r['training']+r['heldout'] <= policy['max_samples'], name+': sample cap')
        check(set(r['methods']) == {'gradient', 'isophote', 'forward'}, name+': all methods returned')
        check(r['methods']['forward']['evaluations'] <= policy['forward_max_evaluations'], name+': forward budget')
        check(not r['physical_source_certified'] and not r['orientation_available'] and
              all(not m['physical_depth_available'] and not m['type_or_orientation_available']
                  for m in r['methods'].values()), name+': no physical/source/orientation certification')
    for surface in (3, 4, 5):
        methods = result['cases'][f'compiled/point_room/{surface}']['result']['methods']
        check(all(m['admission'] == 'Conditional' and m['position_error_world'] < 16 for m in methods.values()),
              f'Compiled point surface {surface}: errors below one receiver pitch')
    for name in ('material_seam', 'saturated', 'hard_shadow', 'normal_change', 'two_points'):
        methods = result['cases']['control/'+name]['result']['methods']
        check(all(m['admission'] == 'Unavailable' for m in methods.values()), name+': model/metadata gates reject')
    methods = result['cases']['control/gamma_half']['result']['methods']
    check(all(m['position_error_world'] > 64 and m['admission'] == 'Conditional' for m in methods.values()),
          'Gamma counterexample: small discrepancy does not identify height')
    methods = result['cases']['control/bounce_virtual']['result']['methods']
    check(all(m['position_error_world'] > 100 and m['admission'] == 'Conditional' for m in methods.values()),
          'Virtual reflection counterexample: primary-source identity unresolved')
    methods = result['cases']['compiled/bounce_colour/5']['result']['methods']
    check(methods['forward']['admission'] == 'Conditional' and methods['forward']['position_error_world'] > 32,
          'Actual one-bounce fit passes residual gate with more than 32 WU error')
    check(all(t['error_world'] is not None and t['error_world'] < 16 for t in result['triangulated'].values()),
          'Nonparallel-plane projection triangulation: sub-pitch errors')
    if repeat:
        for name in ('protocol.json', 'frozen-estimates.json', 'results.json', 'tables.md'):
            check((folder/name).read_bytes() == (repeat/name).read_bytes(), name+': second-process byte equality')
    report = dict(passed=len(checks), checks=checks, physical_sources_certified=0)
    write(folder/'checks.json', report)
    print(f'{len(checks)} measured checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--repeat', type=Path)
    args = parser.parse_args()
    verify(args.folder, args.repeat)
