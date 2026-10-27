"""Run one trusted command and record actual OS process peak memory + wall time."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def peak_memory(process):
    if os.name != 'nt':
        return None
    class Counters(ctypes.Structure):
        _fields_ = [('cb', ctypes.c_uint32), ('faults', ctypes.c_uint32)] + [(n, ctypes.c_size_t) for n in ('peak_working', 'working', 'peak_paged', 'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile')]
    counters = Counters(); counters.cb = ctypes.sizeof(counters)
    function = ctypes.WinDLL('psapi').GetProcessMemoryInfo
    function.argtypes = [ctypes.c_void_p, ctypes.POINTER(Counters), ctypes.c_uint32]
    if function(int(process._handle), ctypes.byref(counters), counters.cb):
        return counters.peak_working
    return None


def run(command, log, allow_failure=False):
    start = time.perf_counter()
    with Path(log).open('w', encoding='utf-8') as stream:
        process = subprocess.Popen(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT)
        code = process.wait()
        peak = peak_memory(process)
    result = dict(command=list(map(str, command)), exit_code=code,
                  wall_seconds=time.perf_counter()-start, peak_working_set_bytes=peak)
    if code and not allow_failure:
        raise RuntimeError(f'{result}: {Path(log).read_text(encoding="utf-8", errors="replace")[-2000:]}')
    return result


if __name__ == '__main__':
    output = Path(sys.argv[1]); output.parent.mkdir(parents=True, exist_ok=True)
    result = run(sys.argv[2:], output.with_suffix('.log'), allow_failure=True)
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
    sys.exit(result['exit_code'])
