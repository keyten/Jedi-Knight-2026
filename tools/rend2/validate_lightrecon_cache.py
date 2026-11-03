#!/usr/bin/env python3
"""Repeat native cache witnesses and abrupt-process interruption, retaining timings separately."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def execute(executable, arguments, log, expected=0):
    started = time.perf_counter()
    with log.open('w') as stream:
        child = subprocess.Popen([str(executable.resolve()), *map(str, arguments)], stdout=stream,
                                 stderr=subprocess.STDOUT)
        code = child.wait(timeout=600)
        cpu = None
        if os.name == 'nt':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)]*4
            values = [wintypes.FILETIME() for _ in range(4)]
            if kernel.GetProcessTimes(int(child._handle), *(ctypes.byref(v) for v in values)):
                cpu = sum((v.dwHighDateTime<<32)+v.dwLowDateTime for v in values[2:])/1e7
    text = log.read_text(errors='replace')
    if code != expected:
        raise AssertionError(f'{log}: exit {code}, expected {expected}\n{text[-3000:]}')
    return dict(wall_seconds=time.perf_counter()-started, cpu_seconds=cpu, exit=code), text


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native-test', type=Path, required=True)
    p.add_argument('--producer-test', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    runs = []
    witnesses = []
    for label in ('a', 'b'):
        perf, text = execute(args.native_test, [folder/label], folder/f'{label}.log')
        witness = re.search(r'^WITNESS (.*)$', text, re.M)[1]
        witnesses.append(witness)
        metrics = dict(token.split('=', 1) for token in re.search(r'^METRICS (.*)$', text, re.M)[1].split())
        perf['metrics'] = {k: float(v) if any(c in v for c in '.eE') else int(v) for k,v in metrics.items()}
        perf['checks'] = re.search(r'(\d+) checks, (\d+) failures', text).groups()
        runs.append(perf)
    if witnesses[0] != witnesses[1]:
        raise AssertionError('Repeated semantic bytes/key differ')
    interrupted, _ = execute(args.native_test, [folder/'interrupted', 'crash'], folder/'crash.log', 23)
    verified, _ = execute(args.native_test, [folder/'interrupted', 'verify-crash'], folder/'verify-crash.log')
    report = dict(schema='org.openjk.persistent-cache-validation/1',
                  revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  working_tree=True, semantic_witness=witnesses[0], deterministic=True,
                  runs=runs, abrupt_interruption=dict(child=interrupted, old_cache_verified=verified))
    if args.producer_test:
        perf, text = execute(args.producer_test, [folder/'producer-cache', folder/'producer.json'], folder/'producer.log')
        report['production_cpu_producer'] = dict(perf=perf,
            metrics=re.search(r'^V01_PRODUCER_CACHE (.*)$', text, re.M)[1])
    sources = ['shared/rd-rend2/lightrecon/'+n for n in
               ('cache.cpp','dependencies.cpp','persistent.cpp','lifecycle.cpp','field_codec.cpp','field_prepared.cpp')]
    sources += ['shared/rd-rend2/tr_lightrecon.cpp','shared/rd-rend2/tr_shader.cpp']
    report['implementation_sha256'] = {path: hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in sources}
    (folder/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
