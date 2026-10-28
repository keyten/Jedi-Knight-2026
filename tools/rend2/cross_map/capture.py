#!/usr/bin/env python3
"""X06 bounded actual map-load capture; existing renderer exporter, no fitting.
All licensed payloads remain outside the checkout. GPL-2.0-or-later.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from types import SimpleNamespace

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent));sys.path.insert(0,str(HERE.parent/'datasets'))
from test_lightrecon_legacy_runtime import prepare_runtime
from measure import peak_memory


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def capture(private,availability,policy,names=None,mapped_images=0,tag='',roi=None):
    private=Path(private).resolve()
    if private.is_relative_to(ROOT):raise ValueError('Licensed X06 outputs must be outside Git checkout')
    binaries=private/'binaries';binaries.mkdir(exist_ok=True)
    for source,target in [(ROOT/'build/x02/runtime/run/openjk_sp.x86_64.exe','openjk_sp.x86_64.exe'),
        (ROOT/'build/x02/runtime/run/rend2_glslc.exe','rend2_glslc.exe'),
        (ROOT/'build/x02/runtime/run/home/OpenJK/jagamex86_64.dll','jagamex86_64.dll')]:
        if source.stat().st_size<10000:raise ValueError('Placeholder binary '+str(source))
        if not (binaries/target).exists():shutil.copy2(source,binaries/target)
    result={};total=time.monotonic()
    for name in names or availability['selected_new']:
        slug=name.replace('/','_');folder=private/('capture-'+slug+tag);record=folder/'capture.json'
        if record.exists():result[name]=json.loads(record.read_text());continue
        folder.mkdir();runtime=folder/'run'
        args=SimpleNamespace(output=folder,installation=ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets',
            binaries=binaries,renderer=ROOT/'build/lightrecon-core-renderers/Release/rdsp-rend2_x86_64.dll')
        config,archives=prepare_runtime(args,runtime,'legacy',3)
        # Omit arbitrary camera ROI: the existing exporter uses world AABB centre.
        spatial='' if roi is None else ' '+' '.join(map(str,roi))
        commands=['wait 30',f'devmap {name}','wait 150',f'echo X06_{slug}_BEGIN','r_lightReconEvidence',
            f'r_lightReconExport {slug}_all {mapped_images}{spatial}','r_lightReconInfo',f'echo X06_{slug}_END','echo X06_DONE','wait 5','quit']
        (runtime/'home/OpenJK/autoexec_sp.cfg').write_text('\n'.join(commands)+'\n',encoding='ascii')
        argv=[str(runtime/'openjk_sp.x86_64.exe')]
        for k in ('fs_basepath','fs_homepath','fs_game'):argv+=['+set',k,config[k]]
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        start=time.monotonic();reason=None
        with (folder/'launch.log').open('w') as log:
            process=subprocess.Popen(argv,cwd=runtime,startupinfo=startup,stdout=log,stderr=subprocess.STDOUT)
            peak=0
            while process.poll() is None:
                current=peak_memory(process);peak=max(peak,current or 0)
                if time.monotonic()-start>policy['budgets']['per_stage_seconds'] or time.monotonic()-total>policy['budgets']['engine_total_seconds']:
                    reason='TimeBudgetExceeded'
                if peak>policy['budgets']['process_rss_limit_bytes']:reason='MemoryBudgetExceeded'
                if reason:process.terminate();process.wait(timeout=20);break
                time.sleep(.25)
            code=process.wait();peak=max(peak,peak_memory(process) or 0)
        console=runtime/'home/OpenJK/qconsole.log';text=console.read_text(errors='replace') if console.exists() else ''
        files=list((runtime/'home').rglob(slug+'_all.jkobs'))
        complete='complete=1' in text
        ok=code==0 and 'X06_DONE' in text and len(files)==1
        result[name]=dict(status=('Captured' if complete else 'CapturedPartial') if ok else reason or 'CaptureFailed',exit_code=code,
            mapped_images_requested=mapped_images,derived_complete=complete,
            export_region_roi=roi,
            command=argv,wall_seconds=time.monotonic()-start,peak_working_set_bytes=peak,
            dataset=str(files[0]) if len(files)==1 else None,dataset_bytes=files[0].stat().st_size if len(files)==1 else None,
            dataset_sha256=sha(files[0]) if len(files)==1 else None,console=str(console),settings=config,
            engine_sha256=sha(binaries/'openjk_sp.x86_64.exe'),renderer_sha256=sha(args.renderer),
            compiler_known=False,native_mp_export=False,source='Actual SP map loader, also reading original MP BSPs; no native MP material equivalence claim')
        write(record,result[name]);write(private/('captures'+tag+'.json'),result)
        print(name,result[name]['status'],'seconds=',round(result[name]['wall_seconds'],2),'peak=',peak,flush=True)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--maps',nargs='+');p.add_argument('--mapped-images',type=int,default=0);p.add_argument('--tag',default='')
    p.add_argument('--roi',nargs=4,type=float)
    a=p.parse_args();capture(a.private,json.loads((HERE/'availability.json').read_text()),json.loads((HERE/'policy.json').read_text()),a.maps,a.mapped_images,a.tag,a.roi)
