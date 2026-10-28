#!/usr/bin/env python3
"""Reuse all-page CPU scans, quality, blind views and metrics; licensed data external.
GPL-2.0-or-later. No new observation encoding or stock physical light inference.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import zipfile

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'datasets'));sys.path.insert(0,str(HERE.parent/'evaluation'))
from measure import peak_memory
from prepare_blind import prepare,rows
from adapters import quality_row
from metrics import readiness
from plot_coverage import plot


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def measured(argv,log,timeout=600):
    start=time.monotonic();peak=0;reason=None
    with Path(log).open('w',encoding='utf-8') as stream:
        process=subprocess.Popen(list(map(str,argv)),stdout=stream,stderr=subprocess.STDOUT)
        while process.poll() is None:
            peak=max(peak,peak_memory(process) or 0)
            if time.monotonic()-start>timeout:reason='TimeBudgetExceeded'
            if peak>4*1024**3:reason='MemoryBudgetExceeded'
            if reason:process.terminate();process.wait(timeout=20);break
            time.sleep(.1)
        code=process.wait();peak=max(peak,peak_memory(process) or 0)
    record=dict(command=list(map(str,argv)),exit_code=code,seconds=time.monotonic()-start,peak_working_set_bytes=peak,limit=reason)
    if code or reason:raise RuntimeError(json.dumps(record)+' '+Path(log).read_text(errors='replace')[-1500:])
    return record


def analyze(private):
    private=Path(private);available=json.loads((HERE/'availability.json').read_text());policy=json.loads((HERE/'policy.json').read_text())
    captured=json.loads((private/'captures.json').read_text())
    if (private/'captures-prefix.json').exists():captured.update(json.loads((private/'captures-prefix.json').read_text()))
    tool=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_tool.exe';quality=tool.with_name('lightrecon_dataset_quality.exe')
    results={};analysis=private/'analysis';analysis.mkdir(exist_ok=True)
    for name in available['selected_new']:
        slug=name.replace('/','_');folder=analysis/slug;folder.mkdir(exist_ok=True);resultfile=folder/'result.json'
        if resultfile.exists():results[name]=json.loads(resultfile.read_text());continue
        rec=captured.get(name,{})
        if not rec.get('dataset'):results[name]=dict(status='Unavailable',reason=rec.get('status'));continue
        dataset=Path(rec['dataset']);stages={};start=time.monotonic()
        try:
            source=available['available'][name];bsp=folder/(slug+'.bsp')
            with zipfile.ZipFile(source['archive']) as z:bsp.write_bytes(z.read(source['member']))
            stages['audit']=measured([tool,'audit-source-bsp',dataset,bsp],folder/'source-audit.log')
            if not rec.get('derived_complete',True):
                derived=folder/'all-pages.jkobs'
                stages['derive']=measured([quality,'derive',dataset,derived,0,0,32],folder/'derive.log');dataset=derived
            stages['validate']=measured([tool,'validate',dataset],folder/'validate.log')
            prefix=folder/slug
            stages['scan']=measured([quality,'scan',dataset,prefix],folder/'scan.log')
            stages['facts']=measured([quality,'facts',dataset,prefix],folder/'facts.log')
            stages['public-check']=measured([quality,'public-check',prefix.with_suffix('.public.jkobs'),'unused'],folder/'public-check.log')
            s=policy['split'];views=folder/'views'
            argv=[sys.executable,'-X','utf8',str(HERE.parent/'datasets/prepare_blind.py'),str(prefix),str(views),
                '--block',str(s['block_world']),'--gap',str(s['gap_world']),'--seed',str(s['seed']),'--origin',*map(str,s['origin_world'])]
            stages['prepare']=measured(argv,folder/'prepare.log')
            # Existing evaluator streams readiness, including explicit known-area
            # denominators. Geographic/material tables remain local, no pixels in Git.
            source_info=json.loads(prefix.with_suffix('.source.json').read_text())
            ready=readiness(quality_row(r,source_info['map_sha256']) for r in rows(prefix.with_suffix('.rows.jsonl')))
            write(folder/'readiness.json',ready)
            coverage=json.loads((views/'coverage.json').read_text());plot(views/'coverage.json',folder/'coverage.svg')
            facts=json.loads(prefix.with_suffix('.facts.json').read_text());source_info=json.loads(prefix.with_suffix('.source.json').read_text())
            results[name]=dict(status='Complete' if coverage['all_pages_complete'] else 'Partial',map_sha256=sha(bsp),
                source=source_info,counts=coverage['groups']['map'],partitions=coverage['partitions'],grid_partitions=coverage['grid_partitions'],
                readiness={k:v for k,v in ready.items() if k!='groups'},area_quality=ready['groups']['map'],
                material_signals=facts.get('material_signal_counts'),surface_kinds=facts.get('surface_kinds'),
                style_counts=facts.get('style_counts'),facts_keys=sorted(facts),stages=stages,seconds=time.monotonic()-start,
                dataset=str(dataset),prefix=str(prefix),views=str(views),bsp=str(bsp),
                disk_bytes=sum(p.stat().st_size for p in folder.rglob('*') if p.is_file()),
                capture_attempt=rec,historical_profile='UnknownProfile',ground_truth_source_xyz=None)
        except Exception as e:results[name]=dict(status='Failed',reason=str(e),stages=stages,seconds=time.monotonic()-start,capture_attempt=rec)
        write(resultfile,results[name]);write(private/'census.json',results)
        print(name,results[name]['status'],'seconds=',round(results[name]['seconds'],2),flush=True)
    write(private/'census.json',results);return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private',type=Path,required=True)
    a=p.parse_args();analyze(a.private)
