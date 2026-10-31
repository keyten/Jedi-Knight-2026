#!/usr/bin/env python3
"""Compare disabled shader inputs with a named real source revision, no baseline rewrite.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,functools,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/rend2'))
import lightrecon_regression as r

def run(revision,gpu_report,out):
    out.mkdir(parents=True,exist_ok=False)
    audit=r.production_audit()
    if audit:raise AssertionError(audit)
    cases=json.loads((r.BASELINE/'matrix.json').read_text());cpp=r.cpp_path(None)
    current=r.snapshot(cases,cpp);bindings=r.binding_inventory(current)
    original=r.read
    @functools.lru_cache(None)
    def old(path):return subprocess.check_output(['git','show',revision+':'+path],cwd=ROOT).decode('utf-8').replace('\r\n','\n')
    try:
        r.read=old;previous=r.snapshot(cases,cpp);oldbindings=r.binding_inventory(previous)
    finally:r.read=original
    differences=r.differences(previous,current,'source-revision')+r.differences(oldbindings,bindings,'bindings')
    gpu=json.loads(gpu_report.read_text())
    report=dict(schema='org.openjk.producer-disabled-path.v1',revision=revision,source_differences=len(differences),programs=len(cases),gpu_compiled=gpu['gpu_compiled'],driver=gpu['driver'],
        historical_baseline_passed=gpu['passed'],historical_baseline_revision=gpu['baseline_revision'],
        note='Historical frozen baseline drift is reported separately; source revision comparison never rewrites it')
    (out/'comparison.diff').write_text(''.join(differences));(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if differences or not gpu['gpu_compiled']:raise AssertionError(report)
    print(json.dumps(report))
if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--revision',required=True);p.add_argument('--gpu-report',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();run(a.revision,a.gpu_report,a.out)
