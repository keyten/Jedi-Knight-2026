#!/usr/bin/env python3
"""Optional local CPU/allocator measurements, deliberately outside stable witness.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import ctypes
import platform
import statistics
import time
import tracemalloc
from pathlib import Path
import experiment as ex


def peak_rss():
    if platform.system()=='Windows':
        from ctypes import wintypes
        class Counters(ctypes.Structure):
            _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[
                (name,ctypes.c_size_t) for name in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage',
                    'QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage')]
        c=Counters(); c.cb=ctypes.sizeof(c)
        kernel=ctypes.WinDLL('kernel32'); kernel.GetCurrentProcess.restype=wintypes.HANDLE
        psapi=ctypes.WinDLL('psapi'); psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD]
        if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(),ctypes.byref(c),c.cb): return None
        return c.PeakWorkingSetSize
    try:
        import resource
        value=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return int(value if platform.system()=='Darwin' else value*1024)
    except ImportError: return None


def run():
    methods={}
    for name,fn in (('network_construction',ex.network),('profile_stability',ex.stability),
                    ('emitter_suppression',ex.emitter_suppression),('finite_area_reference',ex.finite_reflector_reference)):
        times=[]
        for _ in range(3):
            start=time.perf_counter(); fn(); times.append(time.perf_counter()-start)
        tracemalloc.start(); fn(); _,peak=tracemalloc.get_traced_memory(); tracemalloc.stop()
        methods[name]=dict(median_seconds=statistics.median(times),python_peak_traced_bytes=peak)
    return dict(methods=methods,platform=platform.platform(),python=platform.python_version(),
                process_peak_rss_bytes=peak_rss(),
                warning='Tiny research scenes, not production full-map scaling; timings are not deterministic validation fields')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--output',type=Path,default=Path('build/inverse-gi-research/performance.json'))
    args=p.parse_args(); result=run(); ex.write_json(args.output,result); print(result)
