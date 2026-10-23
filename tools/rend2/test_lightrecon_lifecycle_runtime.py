#!/usr/bin/env python3
"""SP main-world lifecycle smoke test using the existing stock-only runtime harness.

No movement, camera/FX manipulation or image assertions: only map registration,
pause, policy/use changes, vid_restart and disconnect. Missing inputs fail rather
than claiming a runtime pass. Actual cache hits/build completion are CPU-tested
with synthetic assets; production complete input hashing is still unavailable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from test_lightrecon_legacy_runtime import ROOT, CONSUMERS, prepare_runtime


def commands():
    def info(label):
        return [f'echo RECON_LIFECYCLE_{label}_BEGIN', 'r_lightReconInfo',
                f'echo RECON_LIFECYCLE_{label}_END']
    result = ['wait 30'] + info('UNLOADED')
    result += ['r_lightReconUse 1'] + [f'{c} 1' for c in CONSUMERS]
    # Client world registration follows server map setup asynchronously across
    # frames. Match the existing harness' settled registration interval before
    # querying or issuing another map/restart command; no player actions occur.
    result += ['r_lightReconBuild 0', 'map hoth3', 'wait 180', 'uimenu recon_freeze'] + info('DISABLED')
    result += ['r_lightReconBuild 3'] + info('POLICY_UNAPPLIED')
    result += ['map hoth2', 'wait 180', 'uimenu recon_freeze'] + info('FORCE')
    result += ['r_lightReconBuild 1', 'map hoth3', 'wait 180', 'uimenu recon_freeze'] + info('CACHEONLY')
    result += ['r_lightReconBuild 2', 'vid_restart', 'wait 180', 'uimenu recon_freeze'] + info('RESTART')
    result += ['disconnect', 'wait 30'] + info('DISCONNECTED')
    return result + ['echo RECON_LIFECYCLE_DONE', 'quit']


def parse(text):
    states = {}
    for label in ('UNLOADED', 'DISABLED', 'POLICY_UNAPPLIED', 'FORCE', 'CACHEONLY', 'RESTART', 'DISCONNECTED'):
        block = re.search(f'RECON_LIFECYCLE_{label}_BEGIN(.*?)RECON_LIFECYCLE_{label}_END', text, re.S)
        if not block:
            raise AssertionError('Missing lifecycle record ' + label)
        section = block[1]
        match = re.search(r'lifecycle=(\w+) world=(\S+) ticket=(\d+) generation=(\d+) identity=(\S+)', section)
        cache = re.search(r'cache=(\w+) key-complete=(\d+).*metadata-estimate=(\d+) temporary=([^\r\n]+)', section)
        rows = re.findall(r'\b(r_lightRecon\w+) requested=(\d+) effective=(\d+) capability=(\w+) feature=(\d+)', section)
        if not match or not cache or len(rows) != len(CONSUMERS) or {r[0] for r in rows} != set(CONSUMERS):
            raise AssertionError('Truncated/invalid lifecycle diagnostic ' + label)
        if any(r[2] != '0' or r[4] != '0' for r in rows):
            raise AssertionError('Consumer activated during lifecycle test')
        master = re.search(r'lightRecon: [^\n]*use=(\d+)', section)
        if not master or master[1] != ('0' if label == 'UNLOADED' else '1') or any(r[1] != '1' for r in rows):
            raise AssertionError('Master/consumer requests were not actually exercised')
        if (cache[2], cache[3]) != ('0', '0') or cache[4] != '0 bytes':
            raise AssertionError('Unexpected resident data or complete fingerprint')
        if re.search(r'session=[1-9]|steps=[1-9]|cache-called=[1-9]', section):
            raise AssertionError('Unexpected expensive/loading work with incomplete source hashing')
        states[label] = dict(state=match[1], world=match[2], ticket=int(match[3]), generation=int(match[4]),
                             identity=match[5], cache=cache[1])
        applied = re.search(r'build applied=(\d+) result=(\w+) cache-called=(\d+) build-called=(\d+)', section)
        policies = dict(DISABLED=('0', 'Disabled', '0', '0'), POLICY_UNAPPLIED=('0', 'Disabled', '0', '0'),
                        FORCE=('3', 'Unavailable', '0', '1'), CACHEONLY=('1', 'Unavailable', '0', '0'),
                        RESTART=('2', 'Unavailable', '0', '1'))
        if label in policies:
            if not applied or applied.groups() != policies[label]:
                raise AssertionError('Incorrect applied build/load policy ' + label)
            states[label]['applied'] = applied.groups()
        elif applied:
            raise AssertionError('Unloaded context claimed an applied map policy')
    expected = dict(UNLOADED=('Unloaded', '<none>'), DISABLED=('Disabled', 'maps/hoth3.bsp'),
                    POLICY_UNAPPLIED=('Disabled', 'maps/hoth3.bsp'), FORCE=('BuildRequired', 'maps/hoth2.bsp'),
                    CACHEONLY=('Unavailable', 'maps/hoth3.bsp'), RESTART=('BuildRequired', 'maps/hoth3.bsp'),
                    DISCONNECTED=('Unloaded', '<none>'))
    for label, (state, world) in expected.items():
        if (states[label]['state'], states[label]['world']) != (state, world):
            raise AssertionError(f'{label}: {states[label]} expected {state}/{world}')
    if states['DISABLED']['ticket'] != states['POLICY_UNAPPLIED']['ticket']:
        raise AssertionError('Changing policy mutated current context')
    if len({states[k]['ticket'] for k in ('DISABLED', 'FORCE', 'CACHEONLY')}) != 3:
        raise AssertionError('World load reused a ticket')
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/lightrecon-lifecycle-runtime')
    parser.add_argument('--label', default='smoke')
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if os.name != 'nt' or not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
        parser.error('Windows and a simple unique label are required')
    runtime = args.output.resolve() / ('lifecycle-' + args.label)
    config, archives = prepare_runtime(args, runtime, 'legacy', 0)
    home = runtime / 'home/OpenJK'
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands()) + '\n', encoding='ascii')
    command = [str(runtime / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, config[key]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (runtime / 'launch.log').open('w') as log:
        process = subprocess.Popen(command, cwd=runtime, startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(.5)
        if process.poll() is None:
            process.terminate() # only this invocation's child
            process.wait(timeout=20)
            raise TimeoutError(runtime)
    text = (home / 'qconsole.log').read_text(errors='replace')
    text = re.sub(r'(?m)^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ', '', text)
    text = re.sub(r'\^[0-9]', '', text)
    if process.returncode or 'RECON_LIFECYCLE_DONE' not in text:
        raise AssertionError('Incomplete actual-SP lifecycle run: ' + str(runtime))
    if re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|\bERROR:', text):
        raise AssertionError('Renderer error during map transitions')
    if text.count('RE_Shutdown(') < 3:
        raise AssertionError('Expected renderer teardown/restart did not occur')
    cache_files = list((runtime / 'home/rend2/lightrecon').rglob('*.jklr'))
    if cache_files:
        raise AssertionError('Incomplete production keys wrote canonical cache files')
    source_files = ('shared/rd-rend2/lightrecon/lifecycle.h', 'shared/rd-rend2/lightrecon/lifecycle.cpp',
                    'shared/rd-rend2/tr_lightrecon.h', 'shared/rd-rend2/tr_lightrecon.cpp',
                    'shared/rd-rend2/tr_lightrecon_api.cpp', 'shared/rd-rend2/tr_lightrecon_manager.h',
                    'shared/rd-rend2/tr_bsp.cpp', 'shared/rd-rend2/tr_staticlighting.cpp',
                    'code/rd-rend2/tr_init.cpp', 'codemp/rd-rend2/tr_init.cpp')
    result = dict(passed=True, states=parse(text), settings=config, archives=archives, command=command,
                  renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),
                  runtime=str(runtime), source_base_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_files_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in source_files},
                  driver=re.findall(r'GL_RENDERER:[^\r\n]*', text),
                  gameplay_actions=False, cache_hit_runtime_tested=False, canonical_cache_files=[])
    (runtime / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
