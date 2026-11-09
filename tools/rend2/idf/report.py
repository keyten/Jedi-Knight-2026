#!/usr/bin/env python3
"""Post-freeze tables/checks; cannot select candidates or change the experiment.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.idf.run import read,write,sha
from tools.rend2.hidden_candidates.run import semantic


def main(a):
    p=read(a.report/'protocol.json');r=read(a.report/'results.json');m=read(a.report/'manifest.json')
    checks={}
    checks['artifacts']=all(sha(a.report/name)==value for name,value in m['artifacts'].items())
    checks['protocol']=sha(a.report/'protocol.json')==r['protocol_sha256']
    checks['sources']=all(sha(ROOT/name)==value for name,value in p['sources'].items())
    checks['binaries']=all(sha(v['path'])==v['sha256'] for v in p['tools'].values())
    checks['dependencies']=all(sha(v['path'])==v['sha256'] for v in p['dependencies'].values())
    checks['source_answers_after_proposals']=r['source_answers_opened_after_all_proposals'] and not r['heldout_rgb_opened']
    checks['bounded_search']=True;checks['repeats']=True;checks['known_free_centres']=True
    policy=read(p['policy']['path'])
    lines=['# ASR02 frozen measurements','',
           'Position-only diagnostic matching at 32 WU; proposals are not accepted emitters.', '',
           '| Case | Method | Proposals | TP / FP / FN | Precision | Recall |',
           '|---|---|---:|---|---:|---:|']
    for name,c in r['cases'].items():
        if c['state']!='Evaluated':continue
        for mode,d in c['methods'].items():
            checks['repeats'] &= d['repeat_equal']
            if mode!='hidden':
                first=a.work/name/(mode+'-0.json');repeat=a.work/name/(mode+'-1.json')
                raw=read(first)
                checks['repeats'] &= sha(first)==sha(repeat)==d['semantic_sha256']
                checks['bounded_search'] &= raw['spatial_queries']<=policy['spatial_query_cap']
                checks['known_free_centres'] &= all(x['free'] for x in raw['candidates'])
            else:
                checks['repeats'] &= semantic(read(a.work/name/'hidden-0.log'))==semantic(read(a.work/name/'hidden-1.log'))
            metric=d.get('metrics',{}).get('32')
            if metric:
                counts=' / '.join(str(metric[k]) for k in ('true_positive','false_positive','false_negative'))
                precision=metric['precision']['value'];recall=metric['recall']['value']
                precision=f'{precision:.3f}' if precision is not None else 'N/A'
                recall=f'{recall:.3f}' if recall is not None else 'N/A'
            else:counts=precision=recall='Unavailable'
            lines.append(f'| {name} | {mode} | {len(d["candidates"])} | {counts} | {precision} | {recall} |')
    lines+=['','## Cost','',
            '| Case | Method | Spatial samples / native reported nodes | Rays | Work |',
            '|---|---|---:|---:|---:|']
    for name,c in r['cases'].items():
        if c['state']!='Evaluated':continue
        for mode,d in c['methods'].items():
            if mode=='hidden':nodes=sum(v['nodes'] for v in d['areas']);rays=d['counters']['rays'];work=d['counters']['work']
            else:nodes=d['spatial_queries'];rays=d['rays'];work=d['work']
            lines.append(f'| {name} | {mode} | {nodes} | {rays} | {work} |')
    lines+=['','Same declared caps, different actual work; IDF stencil positions are charged.',
            'Hidden reports cell nodes and separately probes types/amplitudes; these counters are not identical work units.',
            'RGB8 results have no calibrated intensity-bound or historical-source interpretation.','']
    with (a.report/'tables.md').open('w',encoding='utf-8',newline='\n') as f:f.write('\n'.join(lines))
    command=[sys.executable,'-X','utf8','-m','unittest','tools.rend2.idf.test_idf','-v']
    tests=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    checks['mathematical_counterexamples']=tests.returncode==0
    # Production source identities sealed by ASR01 must still be unchanged.
    baseline=read(ROOT/'tools/rend2/aesthetic_baseline/results/v1/protocol.json')
    production={n:v for n,v in baseline['sources'].items() if n.startswith('shared/') or n.startswith('tools/lightrecon_test/')}
    checks['production_unchanged']=all(sha(ROOT/n)==v for n,v in production.items())
    write(a.report/'checks.json',dict(checks=checks,all_passed=all(checks.values()),
          test_command=command,test_exit_code=tests.returncode,test_output=tests.stdout+tests.stderr,
          report_source_sha256=sha(Path(__file__))))
    write(a.report/'manifest.json',dict(artifacts={f.name:sha(f) for f in sorted(a.report.iterdir()) if f.name!='manifest.json' and f.is_file()},
                                      licensed_payloads=False,production_modified=False))
    print(json.dumps(checks,indent=2))
    if not all(checks.values()):raise SystemExit(1)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True);parser.add_argument('--work',type=Path,required=True)
    main(parser.parse_args())
