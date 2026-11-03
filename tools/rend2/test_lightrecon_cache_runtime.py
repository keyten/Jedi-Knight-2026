#!/usr/bin/env python3
"""V01 real SP map-load cache boundary, using a clearly labelled empty I/O witness.

This does NOT create a fitted stock model or certify absent lighting. CPU tests
separately exercise actual field/shadow payloads. Each owned child uses minimum
render settings, no illumination consumers and a private cache/home directory.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import hashlib
import os
from pathlib import Path
import re
import subprocess
import time
from test_lightrecon_legacy_runtime import ROOT, CONSUMERS, prepare_runtime
from test_ssr_runtime import game_processes


def run(args, runtime, config, label, policy, exposure=0):
    if game_processes():
        raise RuntimeError('Another game process is running; preserving its state')
    home = runtime / 'home/OpenJK'
    applied = dict(config, r_lightReconBuild=policy, r_cameraExposure=exposure)
    (home / 'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in applied.items()), encoding='ascii')
    commands = ['wait 30', f'r_lightReconBuild {policy}', f'r_cameraExposure {exposure}',
                f'map {args.map}', 'wait 180', 'uimenu recon_freeze',
                'echo CACHE_RUNTIME_BEGIN', 'r_lightReconInfo dependencies',
                'echo CACHE_RUNTIME_END', 'disconnect', 'wait 15', 'r_lightReconInfo',
                'echo CACHE_RUNTIME_DONE', 'quit']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands) + '\n', encoding='ascii')
    command = [str(runtime / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, config[key]]
    command += ['+set', 'r_lightReconBuild', str(policy)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    started = time.perf_counter()
    peak = 0
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    class Memory(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD),
                    ('peak', ctypes.c_size_t), ('working', ctypes.c_size_t)] + [
                    (name, ctypes.c_size_t) for name in ('pp', 'p', 'np', 'n', 'pf', 'peakpf')]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    with (runtime / f'{label}-launch.log').open('w') as log:
        process = subprocess.Popen(command, cwd=runtime, startupinfo=startup,
                                   stdout=log, stderr=subprocess.STDOUT)
        handle = kernel.OpenProcess(0x410, False, process.pid)
        try:
            deadline = time.monotonic() + args.timeout
            while process.poll() is None and time.monotonic() < deadline:
                m = Memory()
                m.cb = ctypes.sizeof(m)
                if handle and psapi.GetProcessMemoryInfo(handle, ctypes.byref(m), m.cb):
                    peak = max(peak, m.peak)
                time.sleep(.2)
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=20)
                raise TimeoutError(label)
        finally:
            if handle:
                kernel.CloseHandle(handle)
    text = (home / 'qconsole.log').read_text(errors='replace')
    (runtime / f'{label}-console.log').write_text(text)
    if process.returncode or 'CACHE_RUNTIME_DONE' not in text:
        raise RuntimeError(f'{label}: incomplete map load; {text[-2500:]}')
    block = re.search('CACHE_RUNTIME_BEGIN(.*?)CACHE_RUNTIME_END', text, re.S)[1]
    if re.search(r'r_lightRecon\w+ requested=\d+ effective=[1-9]', text):
        raise AssertionError('A renderer consumer became active')
    if not re.search(r'lifecycle=Unloaded', text.split('CACHE_RUNTIME_END')[-1]):
        raise AssertionError('World unload did not release publication')
    def match(pattern):
        m = re.search(pattern, block)
        if not m:
            raise AssertionError(f'{label}: missing diagnostic {pattern}')
        return m.groups()
    result = dict(label=label, policy=policy, process_seconds=time.perf_counter()-started,
                  peak_working_set_bytes=peak, state=match(r'lifecycle=(\w+)')[0],
                  cache=match(r'cache=(\w+) key-complete=(\d+) key=(\S+)'),
                  build=match(r'build applied=(\d+) result=(\w+) cache-called=(\d+) build-called=(\d+)'),
                  steps=int(match(r'session=\d+ steps=(\d+)')[0]),
                  work=match(r'cache-preflight early=(\d+) pending=(\d+) dependencies=(\d+) missing=(\d+) hashed=(\d+) dependency-seconds=([\d.]+) cache-read=(\d+) cache-reads=(\d+) selected-chunks=(\d+) selected-bytes=(\d+) open-seconds=([\d.]+) prepare-seconds=([\d.]+)'),
                  interpretation='SP material context, including when loading an MP BSP')
    publication = re.search(r'first-publication-seconds=([\d.]+) available=(\d+)', block)
    result['first_publication_seconds'] = float(publication[1]) if publication and publication[2]=='1' else None
    print(json.dumps(result), flush=True)
    return result, runtime / f'{label}-console.log'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--binaries', type=Path, required=True)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--native-test', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--map', default='hoth3')
    p.add_argument('--timeout', type=int, default=300)
    args = p.parse_args()
    runtime = args.output.resolve() / 'runtime'
    config, archives = prepare_runtime(args, runtime, 'legacy', 0)
    config.update(r_customwidth=320, r_customheight=240, r_picmip=3, r_texturebits=16,
                  r_hdr=0, r_dynamiclight=0, r_subdivisions=80, r_shadowMapSize=256,
                  r_lightReconUse=0, com_maxfps=60)
    config.update({c: 0 for c in CONSUMERS})
    home = runtime / 'home/OpenJK'
    (home / 'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k, v in config.items()), encoding='ascii')
    disabled, _ = run(args, runtime, config, 'disabled', 0)
    miss, log = run(args, runtime, config, 'miss', 1)
    if disabled['cache'][1] != '0' or any(int(disabled['work'][i]) for i in (0,2,3,4,6,7,8,9)):
        raise AssertionError('Disabled path performed reconstruction/cache work')
    if miss['cache'][:2] != ('Missing', '1') or miss['build'] != ('1','Missing','1','0'):
        raise AssertionError('Production dependency collection/real cache miss is unsupported')
    root = runtime / 'home/rend2/lightrecon/v1'
    subprocess.run([str(args.native_test.resolve()), str(root), 'seed-diagnostic', str(log)], check=True)
    warm, _ = run(args, runtime, config, 'warm', 1)
    exposure, _ = run(args, runtime, config, 'warm-exposure', 2, 2)
    for result in (warm, exposure):
        if result['state'] != 'Available' or result['cache'][:2] != ('Ok','1') or result['steps'] or result['build'][3] != '0':
            raise AssertionError('Validated warm hit reran fitting or failed publication')
        if result['cache'][2] != miss['cache'][2]:
            raise AssertionError('Display exposure changed semantic cache key')
    report = dict(schema='org.openjk.cache-runtime-v01/1',
                  revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  dirty=True, archives=archives, settings=config, map=args.map,
                  material_context='rend2-sp/source-materials/1',
                  renderer_sha256=hashlib.sha256((runtime/'rdsp-rend2_x86_64.dll').read_bytes()).hexdigest(),
                  scope='Actual renderer metadata cache I/O witness only, no fitted stock lighting',
                  cases=[disabled,miss,warm,exposure])
    (args.output / 'result.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
