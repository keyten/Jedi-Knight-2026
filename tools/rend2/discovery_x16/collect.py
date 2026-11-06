#!/usr/bin/env python3
"""Collect X16 numerical receipts after fitting; no discovery or game launch."""
import argparse
import json
import os
import platform
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.discovery_x16 import report
from tools.rend2.all_stock.run import sha,write


def collect(a):
    report.summarize(a)
    x=json.loads((a.controls/'compiled.json').read_text())
    controls=dict(schema='org.openjk.discovery-x16-supplied-controls/1',
        scope='Fixed supplied source XYZ; not blind recovery',tool_sha256=x['executable_sha256'],cases=[])
    keys=('state','training_interval_mse','validation_interval_mse','heldout_interval_mse',
          'heldout_black_mse','conditional_sources','resolved_sources','evaluations','rays','payload_bytes')
    for c in x['cases']:
        controls['cases'].append(dict(name=c['name'],wrong_profile=c['wrong_profile'],
            bsp_sha256=c['bsp_sha256'],compiler_sha256=c['compiler_sha256'],
            parameters=c['source_parameters'],**{k:c['result']['producer'][k] for k in keys}))
    controls['repeat_archive_sha256']=x['repeat_archive_sha256']
    controls['resources']=json.loads((a.controls/'resources.json').read_text())
    write(a.output/'supplied-controls.json',controls)
    write(a.output/'repeat.json',json.loads(a.repeat.read_text()))
    jobs={}
    for variant in ('direct','passive_bounce1'):
        jobs[variant]=json.loads((a.proposals/variant/'jobs.json').read_text())
    write(a.output/'proposal-resources.json',jobs)
    cpu=(a.logs/'x16-cpu-tests.log').read_text()
    match=re.search(r'(\d+)% tests passed, (\d+) tests failed out of (\d+)',cpu)
    seconds=re.search(r'Total Test time \(real\) =\s*([\d.]+)',cpu)
    renderer=(a.logs/'x16-renderers-no-reuse.log').read_text()
    dlls=('rdsp-rend2_x86_64.dll','rd-rend2_x86_64.dll')
    validation=dict(schema='org.openjk.discovery-x16-validation/1',
        cpu=dict(all_passed=bool(match and match[1]=='100' and match[2]=='0'),
            total=int(match[3]) if match else None,seconds=float(seconds[1]) if seconds else None),
        renderers={n:dict(build_log_completed=n in renderer,
            sha256=sha(a.renderers/'Release'/n) if (a.renderers/'Release'/n).exists() else None) for n in dlls},
        initial_build_failures='Reused-node MSBuild tracker AccessDenied; isolated /nr:false retry completed',
        python_contract_log=(a.logs/'x16-python-tests.log').read_text(),game_launches=0,
        logs={p.name:sha(p) for p in a.logs.glob('x16-*.log')})
    write(a.output/'validation.json',validation)
    source=list(Path(__file__).parent.glob('*.py'))+list(Path(__file__).parent.glob('policy-*.json'))
    changed=subprocess.check_output(['git','diff','HEAD','--name-only'],cwd=ROOT,text=True).splitlines()
    source+=[ROOT/p for p in changed if (ROOT/p).is_file() and '/results/' not in p]
    env=dict(parent_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        dirty_tree=True,report_environment=platform.platform(),
        processor=os.environ.get('PROCESSOR_IDENTIFIER'),logical_cpus=os.cpu_count(),
        cpu_compiler='GCC 16.2.0 Release C++11',renderer_compiler='MSVC 2019 x64 Release; SDK 10.0.17763',
        source_sha256={p.resolve().relative_to(ROOT).as_posix():sha(p) for p in source},
        environment_scope='Report generation; individual run policy/executable hashes remain separate',game_launches=0)
    write(a.output/'environment.json',env)
    # Portable committed receipts: hash canonical LF bytes, independent of
    # platform text-mode translation or Git autocrlf. Source/run hashes above
    # still identify the exact files actually executed in that environment.
    for path in a.output.iterdir():
        if path.is_file():path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))
    write(a.output/'manifest.json',dict(schema='org.openjk.discovery-x16-artifacts/1',
        artifact_text_encoding='UTF-8, LF',
        parent_revision=env['parent_revision'],
        policies={p.name:sha(p) for p in Path(__file__).parent.glob('policy-*.json')},
        proposal_manifest_sha256=sha(a.proposals/'manifest.json'),
        runs=[dict(name=p.name,manifest_sha256=sha(p/'results.json')) for p in a.fits],
        files={p.name:sha(p) for p in a.output.iterdir() if p.is_file() and p.name!='manifest.json'},
        original_V02_overwritten=False,private_answers_not_solver_input=True))
    path=a.output/'manifest.json';path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bridge','proposals','controls','oracle','repeat','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--fits',nargs='+',type=Path,required=True)
    p.add_argument('--logs',type=Path,default=ROOT/'build')
    p.add_argument('--renderers',type=Path,default=ROOT/'build/x16-renderers')
    collect(p.parse_args())
