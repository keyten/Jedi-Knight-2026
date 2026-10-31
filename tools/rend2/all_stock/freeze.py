#!/usr/bin/env python3
"""Freeze aggregate provenance/checks without copying licensed data."""
import argparse
import json
import re
import subprocess
from pathlib import Path
from run import ROOT, sha, write
from verify import verify


def freeze(private, out):
    for revision in ('v2','local-v3','v4'):
        verify(out/revision)
    old=json.loads((out/'v2/maps.json').read_text())['maps']
    new=json.loads((out/'v4/maps.json').read_text())['maps']
    agreement=[]
    for a,b in zip(old,new):
        assert a['map']==b['map']
        keys=('census','folds','geometry','geometry_rays','reconstruction_verdict')
        record=dict(map=a['map'], **{k:a[k]==b[k] for k in keys})
        assert all(record[k] for k in keys)
        agreement.append(record)
    repeats=json.loads((private/'determinism-v2.json').read_text())
    assert all(v['identical'] for r in repeats.values() for v in r['semantic_files'].values())
    write(out/'determinism.json',dict(schema='org.openjk.all-stock-reproducibility.v1',
          all_maps_before_after_diagnostic_fix=agreement, same_binary_eight_file_repeats=repeats,
          note='Time/RSS are outside semantic comparison; failed-work reporting differs by design'))
    checks={}
    for name,pattern in (
        ('all-stock-ctest.log',r'100% tests passed out of 31'),
        ('all-stock-final-focused-tests.log',r'100% tests passed out of 4'),
        ('all-stock-blind-tests.log',r'Ran 6 tests'),
        ('all-stock-evaluation-tests.log',r'Ran 39 tests'),
        ('all-stock-positive-tests.log',r'Ran 16 tests')):
        p=ROOT/'build'/name;text=p.read_text(errors='replace')
        assert re.search(pattern,text),name
        checks[name]=dict(sha256=sha(p),expected_summary=pattern)
    shader=json.loads((ROOT/'build/all-stock-disabled-comparison/report.json').read_text())
    assert shader['source_differences']==0 and shader['gpu_compiled']
    build=ROOT/'build/all-stock-final-renderer-build.log'
    text=build.read_text(errors='replace')
    assert 'rdsp-rend2_x86_64.dll' in text and 'rd-rend2_x86_64.dll' in text
    write(out/'validation.json',dict(schema='org.openjk.all-stock-validation.v1',tests=checks,
          renderer_build_log_sha256=sha(build),shader=shader,
          stock_visual_reconstruction='Not executed',new_q3map2_bakes='Not executed'))
    sources=[ROOT/'shared/rd-rend2/lightrecon/producer.cpp',ROOT/'shared/rd-rend2/lightrecon/offline_bsp.cpp',
             ROOT/'shared/rd-rend2/tr_lightrecon.cpp',ROOT/'tools/lightrecon_test/lightrecon_map_solver.cpp',
             ROOT/'tools/lightrecon_test/source_footprint.h']
    sources+=sorted((ROOT/'tools/rend2/all_stock').glob('*.py'))
    manifest=dict(schema='org.openjk.all-stock-witness.v1',
        base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        branch=subprocess.check_output(['git','branch','--show-current'],cwd=ROOT,text=True).strip(),
        source_sha256={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in sources},
        binaries={'initial_map_adapter':sha(private/'binaries/map-solver-v2.exe'),
                  'final_map_adapter':sha(ROOT/'build/s06-gcc/lightrecon_map_solver.exe')},
        files={str(p.relative_to(out)).replace('\\','/'):sha(p) for p in sorted(out.rglob('*'))
               if p.is_file() and p.name!='manifest.json'},
        claims=dict(full_map_census=True,all_stock_producer_attempted=True,
                    whole_map_joint_optimization_completed=False,stock_physical_reconstruction_verified=False,
                    runtime_consumer_enabled=False))
    write(out/'manifest.json',manifest)
    print('Frozen 57-map audit; no claim of completed stock reconstruction')


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--out',type=Path,default=ROOT/'tools/rend2/all_stock/results')
    a=p.parse_args();freeze(a.private,a.out)
