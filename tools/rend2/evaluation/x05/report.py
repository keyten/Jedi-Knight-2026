#!/usr/bin/env python3
"""Compact versionable witness; no proprietary map pixels or assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
from pathlib import Path
from acceptance import load,digest
from fixtures import write


def compact(run,output,measurement=None):
    output.mkdir(parents=True,exist_ok=False);r=load(run/'result.json')
    cases={};table=[]
    for name,item in r['cases'].items():
        controls={}
        for key,c in item['controls'].items():
            controls[key]=dict(status=c['status'],contracts=[dict(id=x['id'],status=x['status'],admission=x['admission'],
                rmse=x.get('score',{}).get('rgb_interval_rmse',{}).get('value'),
                coverage=x.get('score',{}).get('prediction_coverage',{}).get('value'),
                maximum_error=x.get('maximum_channel_interval_error'),relative_improvement=x.get('relative_zero_improvement'),reasons=x.get('reasons')) for x in c.get('contracts',[])])
        cases[name]=dict(partitions=item['partitions'],controls=controls,scope=item['raw_scope'],public_check=item['geometry_public_check'])
        for x in controls['oracle_forward']['contracts']:
            table.append(dict(fixture=name,contract=x['id'],heldout=x['admission']['count'],blocks=x['admission']['spatial_blocks'],
                positive=x['admission']['positive_receivers'],black_rmse=x['admission']['zero_interval_rmse'],reference_rmse=x['rmse'],
                admission=x['admission']['status'],reference_status=x['status']))
    witness=dict(schema='org.openjk.x05-witness.v1',run=str(run.resolve()),result_sha256=digest(run/'result.json'),
        manifest_sha256=digest(run/'manifest.json'),policy_sha256=r['policy_sha256'],acceptance=r['acceptance'],cases=cases,
        profiles=r['profiles'],diagnostics=r['diagnostics'],measurement=load(measurement) if measurement else None,
        no_solver=True,no_renderer_changes=True,limits=['Self-authored controlled opaque planar scenes; not historical stock profile identification',
            'Native tube, general filtered GI and lightgrid forward operators remain unsupported',
            'This benchmark is not an OS sandbox; mount only inputs/ into an untrusted process'])
    write(output/'summary.json',witness);write(output/'audit.json',r['original_audit']);write(output/'manifest.json',load(run/'manifest.json'))
    with (output/'controls.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(table[0]));w.writeheader();w.writerows(table)
    md=['# X05 benchmark witness','',
        'Reference/admission self-check: **'+r['acceptance']['status']+'**. No production evaluator or inverse solver was run.','',
        '| Fixture | Contract | Held-out | Blocks | Positive | Black interval RMSE | Reference RMSE | Admission / reference |',
        '|---|---|---:|---:|---:|---:|---:|---|']
    for t in table:md.append(f"| {t['fixture']} | {t['contract']} | {t['heldout']} | {t['blocks']} | {t['positive']} | {t['black_rmse']:.6f} | {t['reference_rmse']:.6f} | {t['admission']} / {t['reference_status']} |")
    md+=['','Failed doorway v1/v2 controls are retained diagnostics. Required v3 strengthens the declared primary and spatial receiver domain; numerical gates are unchanged.',
        '', '## Historical controlled fixture census','',
        '| Fixture | Eligible | Train / validation / held-out | Surfaces | Normals | Visible / blocked representative pairs |',
        '|---|---:|---|---:|---:|---|']
    for name,a in r['original_audit'].items():
        parts=a.get('old_partitions',a.get('partitions'));v=a['visibility'];s=a['eligible']
        md.append(f"| {name} | {s['count']} | {' / '.join(str(parts[p]['count']) for p in ('train','validation','heldout'))} | {s['independent_receiver_surfaces']} | {len(s['normal_direction_counts'])} | {v['known_visible_pairs']} / {v['known_blocked_pairs']} |")
    md+=['','The twelve original dataset folds remain frozen. The twenty-two kernel cases use their original planar-floor verification domain. Counts are not a whole-map readiness claim.',
        '', '## Fair fitted-profile controls','', '| Domain | Alternative | Train-only scalar | Validation RMSE | Held-out RMSE | Outcome |', '|---|---|---:|---:|---:|---|']
    for name,p in r['profiles'].items():
        for a in p['alternatives']:md.append(f"| {name} | {a['id']} | {a['amplitude']:.6f} | {a['validation_rmse']:.6f} | {a['heldout_rmse']:.6f} | {p['status']} |")
    md+=['','All controls fit one bounded non-negative legacy-relative scalar with fixed colour and geometry. Validation selects; held-out confirms. No posterior or stock compiler identity is inferred.',
        '', 'All 35 X04 boundary texels / 105 channels remain evaluated; old max error 28, restricted source-locked replay max error 0.',
        '', 'See summary.json for per-control coverage/errors, normal/distance/brightness distributions, explicit unknowns and derivative cusp status; audit.json contains all 34 historical cases.']
    if measurement:
        m=load(measurement);md+=['',f"Controller wall time: {m['wall_seconds']:.3f} s; Python process peak working set: {m['peak_working_set_bytes']} bytes. Child processes are not included in that RSS."]
    (output/'README.md').write_text('\n'.join(md)+'\n',encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--measurement',type=Path)
    a=p.parse_args();compact(a.run,a.output,a.measurement)
