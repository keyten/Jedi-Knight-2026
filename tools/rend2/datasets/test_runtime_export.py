#!/usr/bin/env python3
"""Export original stock CPU observations using actual SP, then validate offline.

Licensed installed assets remain local. No screenshots, GPU readback, fitting or
consumers are used. Two exports per map must be byte-identical.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_lightrecon_legacy_runtime import ROOT, CAMERAS, prepare_runtime
from run_pipeline import sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--installation', type=Path, required=True)
    p.add_argument('--binaries', type=Path, required=True)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--private-archives', type=Path, help='existing private stock-assets/base to hardlink')
    p.add_argument('--timeout', type=int, default=600)
    args = p.parse_args()
    args.output = args.output.resolve()
    if args.private_archives:
        base = args.output / 'stock-assets/base'
        base.mkdir(parents=True, exist_ok=True)
        for source in args.private_archives.glob('assets?.pk3'):
            os.link(source, base / source.name)
    runtime = args.output / 'run'
    config, archives = prepare_runtime(args, runtime, 'legacy', 3)
    home = runtime / 'home/OpenJK'
    commands = ['wait 30']
    for name in ('hoth3', 'hoth2'):
        x, y, z, _ = CAMERAS[name]
        if name == 'hoth3':
            x, y, z = 3320, 700, 123
        commands += [f'devmap {name}', 'wait 150', f'echo OFFLINE_{name}_BEGIN',
                     f'r_lightReconExport {name}_a 2 {x} {y} {z} 256',
                     f'r_lightReconExport {name}_b 2 {x} {y} {z} 256',
                     'r_lightReconInfo', f'echo OFFLINE_{name}_END']
    commands += ['echo OFFLINE_RUNTIME_DONE', 'wait 5', 'quit']
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands) + '\n')
    cmd = [str(runtime / 'openjk_sp.x86_64.exe')]
    for k in ('fs_basepath', 'fs_homepath', 'fs_game'):
        cmd += ['+set', k, config[k]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (runtime / 'launch.log').open('w') as log:
        proc = subprocess.Popen(cmd, cwd=runtime, startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=20)
            raise
    text = (home / 'qconsole.log').read_text(errors='replace')
    if proc.returncode or 'OFFLINE_RUNTIME_DONE' not in text:
        raise AssertionError('SP did not complete map exports: ' + text[-2500:])
    result = dict(renderer_sha256=sha(args.renderer),
                  settings=config, archives=archives, maps={})
    for name in ('hoth3', 'hoth2'):
        matches = list((runtime / 'home').rglob(f'{name}_a.jkobs'))
        if len(matches) != 1:
            section = re.search(f'OFFLINE_{name}_BEGIN(.*?)OFFLINE_{name}_END', text, re.S)
            raise AssertionError('Missing export: ' + (section[1] if section else text[-2000:]))
        first = matches[0]
        second = first.with_name(f'{name}_b.jkobs')
        digest = sha(first)
        assert digest == sha(second), f'Non-deterministic {name}'
        entry = dict(bytes=first.stat().st_size, sha256=digest, repeated_bytes_identical=True)
        for command in ('inspect', 'validate'):
            run = subprocess.run([str(args.tool.resolve()), command, str(first)],
                                 capture_output=True, text=True)
            (args.output / f'{name}.{command}.txt').write_text(run.stdout + run.stderr)
            if run.returncode:
                raise AssertionError(f'{name} {command}: {run.stderr}')
            entry[command] = run.stdout.strip()
        result['maps'][name] = entry
        print(name, entry, flush=True)
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
