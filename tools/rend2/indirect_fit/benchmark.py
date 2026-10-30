#!/usr/bin/env python3
"""Production C++ S10 witnesses and original paired q3map2 bounce diagnostics.
Standard library only. No Python replacement for production transport fitting.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.amplitude_fit.benchmark import peak

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def validate(w):
    if w.get('schema')!='org.openjk.indirect-s10.v1' or w.get('failures')!=0 or w.get('checks',0)<200:
        raise ValueError('Failed/partial production witness')
    scenes={r['scene']:r for r in w['scenes']}
    r=scenes['red_reflector']
    if not 0<=r['heldout_mse']<r['direct_only_mse']*.01<r['black_mse'] or r['direct_only_mse']<=0:
        raise ValueError('Black/missing or non-improving transport')
    if r['false_emitters']!=0 or max(abs(r['rho_'+c]-truth) for c,truth in zip('rgb',(.7,.18,.08)))>.001:
        raise ValueError('Incorrect attribution/material')
    if scenes['independent_finite_reflector']['maximum_relative_difference']>=.02:
        raise ValueError('Finite-area approximation outside declared fixture bound')
    if scenes['unresolved_fill']['fixed_direct_amplitudes_changed']:
        raise ValueError('Unresolved field modified frozen roots')
    json.dumps(w,allow_nan=False)

def run(command,label,resources,timeout=180):
    start=time.perf_counter()
    with subprocess.Popen([str(x) for x in command],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as child:
        try: stdout,stderr=child.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.kill();child.communicate();raise RuntimeError(label+' work/time limit')
        rss=peak(child)
        if child.returncode:raise RuntimeError(label+'\n'+stdout+stderr)
    resources.append(dict(stage=label,seconds=time.perf_counter()-start,peak_process_rss_bytes=rss))
    return stdout

def compiler_controls(directory):
    # Reuse independently compiled, hash-pinned zero/one controls. These are
    # byte-domain diagnostics, not a claim that physical radiosity replays q3map.
    sys.path.insert(0,str(ROOT/'tools/rend2/inverse_gi'))
    from compiler_controls import paired
    frozen=json.loads((ROOT/'tools/rend2/inverse_gi/results/compiler.json').read_text())
    rows={}
    for name in ('point_room','bounce_colour','door_occlusion','wall_leak','skylight'):
        zero=directory/f'{name}-b0-g0-w180/authoring/base/maps/{name}.bsp'
        one=directory/f'{name}-b1-g0-w180/authoring/base/maps/{name}.bsp'
        if sha(zero)!=frozen['inputs_and_outputs'][name+'-white180-b0']['bsp_sha256'] or \
           sha(one)!=frozen['inputs_and_outputs'][name+'-white180-b1']['bsp_sha256']:
            raise ValueError('Pinned compiler bytes changed: '+name)
        actual=paired(zero,one)
        expected=frozen['controls'][name+'-white180']
        if actual!=expected:raise ValueError('Original paired compiler control changed: '+name)
        rows[name]=dict(zero_sha256=sha(zero),one_sha256=sha(one),**actual)
    if rows['wall_leak']['right']['mean_delta_rgb8']!=[0.,0.,0.]:
        raise ValueError('Compiler sealed-wall control changed')
    if rows['door_occlusion']['right']['mean_delta_rgb8'][0]<=0:
        raise ValueError('Missing positive opening signal')
    return dict(compiler_sha256=frozen['compiler_sha256'],source_revision=frozen['source_revision'],
                original_witness_sha256=sha(ROOT/'tools/rend2/inverse_gi/results/compiler.json'),
                semantics='encoded RGB8 differences, NOT additive radiance or physical-GI accuracy',cases=rows)

def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--compiler-controls',type=Path,default=ROOT/'build/inverse-gi-controls')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    files=['shared/rd-rend2/lightrecon/indirect.h','shared/rd-rend2/lightrecon/indirect.cpp',
           'shared/rd-rend2/tr_lightrecon_types.h','shared/rd-rend2/tr_lightrecon_types.cpp',
           'shared/rd-rend2/lightrecon/disk.cpp','tools/lightrecon_test/lightrecon_indirect_test.cpp',
           'tools/rend2/indirect_fit/benchmark.py']
    sources={f:sha(ROOT/f) for f in files};resources=[];witnesses=[]
    for i in range(2):
        prefix=(a.out/f'cpu-{i}').resolve()
        log=run([a.tool.resolve(),prefix],f'CPU-{i}',resources)
        Path(str(prefix)+'.log').write_text(log)
        w=json.loads(Path(str(prefix)+'-results.json').read_text());validate(w);witnesses.append(w)
    if witnesses[0]!=witnesses[1]:raise ValueError('Nondeterministic numerical output')
    compiled=compiler_controls(a.compiler_controls)
    if sources!={f:sha(ROOT/f) for f in files}:raise ValueError('Sources changed during benchmark')
    result=dict(schema='org.openjk.s10-benchmark.v1',deterministic=True,owned=witnesses[0],
                compiled=compiled,source_sha256=sources,executable_sha256=sha(a.tool),
                limitations=['Physical one-bounce approximation is not empirical compiler bounce replay',
                             'Stock UnknownProfile not identified; no stock physical attribution benchmark',
                             'No source discovery or runtime publication'])
    (a.out/'numerical.json').write_text(json.dumps(result,sort_keys=True,indent=2,allow_nan=False)+'\n')
    (a.out/'resources.json').write_text(json.dumps(resources,sort_keys=True,indent=2)+'\n')
    print(json.dumps(dict(checks=witnesses[0]['checks'],deterministic=True,resources=resources),indent=2))

if __name__=='__main__':main()
