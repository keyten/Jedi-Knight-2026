#!/usr/bin/env python3
"""Freeze X07 semantic witnesses separately from platform/time/RSS measurements.

No observation pixels, private archives or stock assets are copied to Git.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from common import load,write,digest,canonical

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
VOLATILE={'timing','resources','repeat_resources','loading_seconds','frozen_fit_sha256'}

def semantic(value):
    if isinstance(value,dict):return {k:semantic(v) for k,v in value.items() if k not in VOLATILE}
    if isinstance(value,list):return [semantic(v) for v in value]
    return value

def snapshot():
    files=list(HERE.glob('*.py'))+[ROOT/'tools/lightrecon_test/bake_fit_server.h',ROOT/'tools/lightrecon_test/lightrecon_bake_forward_tool.cpp',ROOT/'tools/lightrecon_test/CMakeLists.txt']
    files+=list((ROOT/'shared/rd-rend2/lightrecon').glob('bake_*.cpp'))+list((ROOT/'shared/rd-rend2/lightrecon').glob('bake_*.h'))
    files+=[HERE.parent/'datasets'/name for name in ('prepare_blind.py','solver_view.py','evaluation_view.py','q3map2-validation.json')]
    files+=[HERE.parent/'bake_model'/name for name in ('validate_cpp.py','source-lock.json','profiles.json')]
    files+=[HERE.parent/'shadow_footprint/compiler_boundary.py']
    return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest() for p in sorted(files)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,required=True);p.add_argument('--compare',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--exe',type=Path,default=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_bake_forward_tool.exe')
    p.add_argument('--failed-pilot',type=Path)
    a=p.parse_args();first=load(a.run/'summary.json');second=load(a.compare/'summary.json');stable=semantic(first)
    if stable!=semantic(second):raise ValueError('Repeated full experiment differs semantically; do not overwrite the witness')
    a.output.mkdir(parents=True,exist_ok=True)
    sources=snapshot();source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    witness=dict(schema='org.openjk.x07-fixed-geometry-validation.v1',source_commit=source_commit,
        working_tree_is_authoritative=True,source_hash_policy='SHA256 of LF-normalized bytes',source_sha256=sources,
        repeated_full_experiment_semantically_identical=True,semantic_sha256=canonical(stable),experiment=stable,
        scope='Supplied fixed point XYZ/count/type, known controlled compiler profile; RGB amplitudes only; NOT physical-source discovery or a runtime asset')
    if a.failed_pilot:
        failed=load(a.failed_pilot)
        if failed['status']!='BudgetExceeded' or failed.get('fitted_rgb') is not None:raise ValueError('Expected genuine no-fit budget failure')
        witness['earlier_coordinate_only_pilot']=dict(status=failed['status'],reason=failed['reason'],work=failed['work'],runtime_publication=False,
            change='Added local production-C++ finite-difference amplitude proposals after training-side budget exhaustion. No held-out amplitude tuning.')
    write(a.output/'validation.json',witness)
    resources=dict(schema='org.openjk.x07-measurements.v1',platform='Windows x64, MSVC Release, Python standard library',binary_sha256=digest(a.exe),semantic_sha256=witness['semantic_sha256'],
                   metric='Peak process working set via GetProcessMemoryInfo; each process reported separately, not claimed as simultaneous total',runs=[])
    for directory,summary in ((a.run,first),(a.compare,second)):
        record=dict(directory=directory.as_posix(),controller=summary['resources'],cases={})
        for name,case in summary['cases'].items():
            entry=dict(source_loading_seconds=case['loading_seconds'],models={},input_sizes={})
            for model,m in case['models'].items():
                entry['models'][model]=dict(resources=m['resources'],worker=m['fit'].get('timing'),repeat=m.get('repeat_resources'))
            for member in (directory/name/'correct/inputs').iterdir():
                if member.is_file():entry['input_sizes'][member.name]=member.stat().st_size
            record['cases'][name]=entry
        resources['runs'].append(record)
    write(a.output/'measurements.json',resources)
    print(json.dumps(dict(semantic_sha256=witness['semantic_sha256'],repeated=True,cases=list(first['cases'])),sort_keys=True))
    return 0

if __name__=='__main__':raise SystemExit(main())
