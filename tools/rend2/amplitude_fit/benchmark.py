#!/usr/bin/env python3
"""Repeat production S06 CPU tests; deterministic witness separate from process RSS/time."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
ROOT=Path(__file__).resolve().parents[3]

def peak(process):
    if sys.platform!='win32':return None
    class Memory(ctypes.Structure):
        _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t)for n in('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
    m=Memory();m.cb=ctypes.sizeof(m);query=ctypes.WinDLL('psapi').GetProcessMemoryInfo;query.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
    return m.peak_ws if query(int(process._handle),ctypes.byref(m),m.cb)else None

def main():
    p=argparse.ArgumentParser();p.add_argument('--tool',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();tool=a.tool.resolve()
    witnesses=[];resources=[]
    with tempfile.TemporaryDirectory(prefix='s06-')as d:
        for repeat in range(2):
            output=Path(d)/f'{repeat}.json';start=time.perf_counter()
            with subprocess.Popen([str(tool),str(output)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)as child:
                stdout,stderr=child.communicate(timeout=180);rss=peak(child)
                if child.returncode:raise RuntimeError(stdout+stderr)
            witness=json.loads(output.read_text());assert witness['failures']==0
            witnesses.append(witness);resources.append(dict(seconds=time.perf_counter()-start,peak_process_rss_bytes=rss))
    if witnesses[0]!=witnesses[1]:raise ValueError('Identical production fits differ semantically')
    files=['shared/rd-rend2/lightrecon/'+n for n in ('amplitude_fit.h','amplitude_fit.cpp','bake_forward.h','bake_forward.cpp')]+['tools/lightrecon_test/lightrecon_amplitude_test.cpp']
    report=dict(schema='org.openjk.s06-unit-benchmark',version=1,witness=witnesses[0],resources=resources,deterministic=True,
        source_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest()for s in files},tool_sha256=hashlib.sha256(tool.read_bytes()).hexdigest())
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,sort_keys=True,indent=2,allow_nan=False)+'\n');print(json.dumps(report,sort_keys=True,indent=2))

if __name__=='__main__':main()
