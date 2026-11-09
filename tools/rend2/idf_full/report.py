"""Post-fit verification/tables only; never alters candidate selection.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.idf.run import read,write,sha


def main(a):
    p=read(a.report/'protocol.json');models=read(a.report/'models.json');metrics=read(a.report/'metrics.json')
    manifest=read(a.report/'manifest.json')
    checks=dict(artifact_hashes=all(sha(a.report/n)==v for n,v in manifest['artifacts'].items()),
                source_hashes=all(sha(ROOT/n)==v for n,v in p['sources'].items()),
                production_unchanged=all(sha(ROOT/n)==v for n,v in p['production'].items()),
                exact_repeats=True,finite_nonnegative_intensities=True)
    rows=['# Full IDF follow-up measurements','',
          'All source positions are research hypotheses; no canonical physical source is published.','',
          '| Scene | Variant | State | Active sources | TP/FP/FN | Held-out relative RMSE |',
          '|---|---|---|---:|---|---:|']
    stage_counts={}
    for name,variants in models.items():
        stage_counts[name]={}
        for variant,result in variants.items():
            first=a.work/name/(variant+'-0.json');repeat=a.work/name/(variant+'-1.json')
            checks['exact_repeats'] &= sha(first)==sha(repeat) and result['repeat_equal']
            model=result['model']
            if model: checks['finite_nonnegative_intensities'] &= all(q>=0 for row in model['intensities'] for q in row)
            m=metrics[name][variant];match=m['matching'].get('32_WU',m['matching']['0.05_width'])
            counts='/'.join(str(match[k]) for k in ('true_positive','false_positive','false_negative'))
            heldout=m['fold_errors'].get('heldout',{}).get('relative_rmse')
            error=f'{100*heldout:.6g}%' if heldout is not None else 'Not evaluated'
            rows.append(f'| {name} | {variant} | {result["state"]} | {m["active_sources"]} | {counts} | {error} |')
            stage_counts[name][variant]=dict(Counter(x['stage'] for x in result['history']))
    full=stage_counts['linear_four']['full']
    checks['complete_cycle_exercised']=all(full.get(stage,0)>0 for stage in
        ('initial_votes','discovery','intensity_increment','nnls_lm','leave_one_out_relocation'))
    checks['four_light_recovery']=metrics['linear_four']['full']['matching']['0.05_width']['true_positive']==4
    checks['two_light_recovery']=metrics['linear_two']['full']['matching']['0.05_width']['true_positive']==2
    checks['positive_scene_is_not_black']=all(models[n]['full']['model']['mse']<1e-6 and metrics[n]['full']['active_sources']>0 for n in ('linear_two','linear_four'))
    command=[sys.executable,'-X','utf8','-m','unittest','tools.rend2.idf_full.test_algorithm','tools.rend2.idf.test_idf','-v']
    tests=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
    checks['tests']=tests.returncode==0
    rows+=['','Source matching: 5% of room width on authored linear controls, 32 WU on compiler cases.',
           'Active means >1% of strongest fitted source and >1e-6 normalized amplitude; raw models retain negligible extra slots.',
           'All positive-source truth and held-out RGB are used only after every model freezes.','',
           '## Work','',
           '| Scene | Variant | New position queries | IDF transforms | Coefficient visits |',
           '|---|---|---:|---:|---:|']
    for name,variants in models.items():
        for variant,r in variants.items():
            c=r['cost'];rows.append(f'| {name} | {variant} | {c["source_position_queries"]} | {c["idf_transforms"]} | {c["idf_pair_visits"]} |')
    rows+=['','The shared geometry-only cache preparation costs another 14,749 positions per scene.',
           'Local refinements have a soft query cap; optimizer queries remain separately counted.','']
    with (a.report/'tables.md').open('w',encoding='utf-8',newline='\n') as f:f.write('\n'.join(rows))
    write(a.report/'checks.json',dict(checks=checks,all_passed=all(checks.values()),stages=stage_counts,
          tests=dict(command=command,exit_code=tests.returncode,output=tests.stdout+tests.stderr),report_sha256=sha(Path(__file__))))
    write(a.report/'manifest.json',dict(artifacts={f.relative_to(a.report).as_posix():sha(f) for f in sorted(a.report.rglob('*'))
              if f.is_file() and f.name!='manifest.json'},production_modified=False,historical_certification='NotCertified'))
    print(checks)
    if not all(checks.values()):raise SystemExit(1)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True);main(parser.parse_args())
