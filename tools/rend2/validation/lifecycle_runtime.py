#!/usr/bin/env python3
"""Reload/vid_restart of the actual V01 diagnostic-only warm cache.

Reuses a prior private cache-runtime directory, preserving its saved console
receipts. No stock fit or consumer is manufactured. GPL-2.0-or-later.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/rend2'))
from test_ssr_runtime import game_processes


def run(a):
    if game_processes():raise RuntimeError('Independent game running; preserved')
    runtime=a.cache_run.resolve()/'runtime';home=runtime/'home/OpenJK'
    initial=json.loads((a.cache_run/'result.json').read_text())
    config=initial['settings'];name=initial['map']
    config.update(r_lightReconBuild=1,r_lightReconUse=0)
    (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in config.items()))
    commands=['wait 30',f'map {name}','wait 180']
    for label,operation in [('BEFORE',[]),('RESTART',['vid_restart','wait 180']),
                             ('RELOAD',[f'map {name}','wait 180'])]:
        commands+=operation+[f'echo V02_{label}_BEGIN','r_lightReconInfo',f'echo V02_{label}_END']
    commands+=['disconnect','wait 15','echo V02_UNLOAD_BEGIN','r_lightReconInfo','echo V02_UNLOAD_END',
               'echo V02_LIFECYCLE_DONE','quit']
    (home/'autoexec_sp.cfg').write_text('\n'.join(commands)+'\n')
    cmd=[str(runtime/'openjk_sp.x86_64.exe')]
    for k in ('fs_basepath','fs_homepath','fs_game'):cmd+=['+set',k,config[k]]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    start=time.perf_counter()
    with (runtime/'v02-lifecycle-launch.log').open('w') as out:
        child=subprocess.Popen(cmd,cwd=runtime,stdout=out,stderr=subprocess.STDOUT,startupinfo=startup)
        while child.poll() is None and time.perf_counter()-start<180:time.sleep(.25)
        if child.poll() is None:child.terminate();child.wait(timeout=10);raise TimeoutError('Owned lifecycle test')
    text=(home/'qconsole.log').read_text(errors='replace')
    (a.cache_run/'lifecycle-console.log').write_text(text)
    if child.returncode or 'V02_LIFECYCLE_DONE' not in text:raise ValueError('Incomplete lifecycle run')
    records=[]
    for label in ('BEFORE','RESTART','RELOAD','UNLOAD'):
        found=re.search('V02_'+label+'_BEGIN(.*?)V02_'+label+'_END',text,re.S)
        if not found:raise ValueError('Missing lifecycle marker '+label)
        section=found[1]
        state=re.search(r'lifecycle=(\w+)',section)
        key=re.search(r'cache=(\w+) key-complete=(\d+) key=(\S+)',section)
        steps=re.search(r'session=\d+ steps=(\d+)',section)
        if re.search(r'r_lightRecon\w+ requested=\d+ effective=[1-9]',section):raise ValueError('Consumer enabled')
        row=dict(label=label,state=state[1] if state else None,
                 cache=list(key.groups()) if key else None,steps=int(steps[1]) if steps else None)
        records.append(row)
    if any(r['state']!='Available' or r['cache'][:2]!=['Ok','1'] or r['steps'] for r in records[:3]):
        raise ValueError('Warm snapshot not restored through lifecycle: '+str(records))
    if records[-1]['state']!='Unloaded':raise ValueError('World ownership not released')
    if len({r['cache'][2] for r in records[:3]})!=1:raise ValueError('Identical dependency key changed')
    result=dict(status='Passed',scope='Actual diagnostic-only cache, no recovered lighting',records=records,
                seconds=time.perf_counter()-start,command=cmd,consumers_enabled=False)
    (a.cache_run/'lifecycle.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cache-run',type=Path,required=True)
    run(p.parse_args())
