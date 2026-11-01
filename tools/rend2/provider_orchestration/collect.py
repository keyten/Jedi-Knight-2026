#!/usr/bin/env python3
"""Freeze CPU orchestration witnesses; stock observations remain private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def collect(controlled,repeat,stock,out):
    out.mkdir(parents=True,exist_ok=True)
    a=json.loads(controlled.read_text());b=json.loads(repeat.read_text())
    assert not a['failures'] and not b['failures']
    assert a==b,'Repeated controlled numerical/provider witness changed'
    write(out/'controlled.json',a)
    records={}
    for profile in ('isq','linear'):
        current=json.loads((stock/(profile+'.json')).read_text())
        producer=current['producer']
        assert current['joint_state']!='NotStarted'
        assert current['work']['models']>0
        assert not producer['asset_available']
        solar=next(p for p in producer['providers'] if p['name']=='solar.fit')
        assert solar['outcome']=='NoSupportedEvidence' and not solar['outputs_admitted']
        records[profile]=current
    census=json.loads((stock/'census.json').read_text())
    resources=json.loads((stock/'native-resources.json').read_text())
    resources.pop('command',None)
    write(out/'stock-t1-danger.json',dict(schema='org.openjk.provider-stock.v1',
        bsp_sha256=census['bsp_sha256'],archive_sha256=census['archive_sha256'],
        train_validation_heldout=census['selected'],profiles=records,resources=resources,
        source_xyz_error=None,physical_recovery_verified=False,
        note='Joint fitting reached; mandatory structural search budget exhausted. No stock sources recovered.'))
    sources=[ROOT/'shared/rd-rend2/tr_lightrecon_types.h',ROOT/'shared/rd-rend2/tr_lightrecon_types.cpp',
             ROOT/'shared/rd-rend2/lightrecon/disk.cpp',ROOT/'tools/lightrecon_test/lightrecon_producer_test.cpp']
    for name in ('producer','hidden_candidates','sun_fit'):
        sources += [ROOT/'shared/rd-rend2/lightrecon'/(name+suffix) for suffix in ('.cpp','.h')]
    sources.append(ROOT/'shared/rd-rend2/lightrecon/provider_outcome.h')
    sources.extend(sorted(Path(__file__).parent.glob('*.py')))
    validation={}
    for label,name,expected in (
        ('full_cpu_initial','provider-orchestration-ctest.log','1 tests failed out of 31'),
        ('schema_hash_disk_retest','provider-orchestration-disk-retest.log','100% tests passed'),
        ('final_receipt_validation','provider-orchestration-final-validation.log','100% tests passed')):
        path=ROOT/'build'/name;text=path.read_text(errors='replace')
        assert expected in text,(name,text[-1000:])
        validation[label]=dict(log_sha256=sha(path),summary=expected)
    build=ROOT/'build/provider-orchestration-renderers-sealed.log'
    text=build.read_text(errors='replace')
    assert 'rdsp-rend2_x86_64.dll' in text and 'rd-rend2_x86_64.dll' in text
    validation['sp_mp_release']=dict(log_sha256=sha(build),built=True)
    shader=json.loads((ROOT/'build/provider-orchestration-disabled-comparison/report.json').read_text())
    assert shader['source_differences']==0
    shader['gpu_compilation_record']='Reused preceding audit; no new GPU visual validation'
    validation['disabled_path']=shader
    validation['controlled_resources']=json.loads((ROOT/'build/provider-orchestration/controlled-accepted-resources.json').read_text())
    validation['controlled_resources'].pop('command',None)
    validation['controlled_assertions']=a['checks'];validation['controlled_failures']=a['failures']
    validation['initial_failure_resolution']='Schema-4 empty archive checksum updated for explicit schema 5; no numerical tolerance change'
    write(out/'validation.json',validation)
    write(out/'manifest.json',dict(schema='org.openjk.provider-orchestration-witness.v1',
        base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        branch=subprocess.check_output(['git','branch','--show-current'],cwd=ROOT,text=True).strip(),
        canonical_schema=5,assessment_version=2,producer_version=2,
        controlled_repeated_identical=True,controlled_sha256=sha(controlled),
        source_sha256={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in sources},
        no_renderer_consumer=True,no_stock_physical_recovery_claim=True))
    print(a['checks'],'controlled assertions; identical repeat; stock Joint::Solve reached')


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--controlled',type=Path,required=True)
    p.add_argument('--repeat',type=Path,required=True)
    p.add_argument('--stock',type=Path,required=True)
    p.add_argument('--out',type=Path,default=ROOT/'tools/rend2/provider_orchestration/results/v1')
    a=p.parse_args();collect(a.controlled,a.repeat,a.stock,a.out)
