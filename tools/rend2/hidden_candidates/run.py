#!/usr/bin/env python3
"""S04 trusted candidate diagnostic controller; only TrainingView reaches C++.
SPDX-License-Identifier: GPL-2.0-or-later. Standard library only.
"""
import argparse
import ctypes
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'datasets'))
from solver_view import TrainingView,digest

def process_peak(handle):
    if sys.platform!='win32':return None
    class Memory(ctypes.Structure):
        _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t)for n in('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile')]
    m=Memory();m.cb=ctypes.sizeof(m);query=ctypes.WinDLL('psapi').GetProcessMemoryInfo;query.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
    return m.peak_ws if query(int(handle),ctypes.byref(m),m.cb)else None

def write(path,value):path.write_text(json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def semantic(value):
    if isinstance(value,dict):return {k:semantic(v) for k,v in value.items() if k not in ('resources','seconds','elapsed')}
    if isinstance(value,list):return [semantic(v) for v in value]
    return value

def choose(view,centre,radius,limit=64):
    """Geometry-only per-surface round robin; never inspect RGB for selection."""
    surfaces=defaultdict(list)
    for row in view.observations():
        p=row['position'];n=row['normal']
        if (p and n and row['area']>0 and row['style']==0 and
            (centre is None or all(abs(a-b)<=radius for a,b in zip(p,centre)))):
            if row['surface']not in surfaces and len(surfaces)>=256:
                raise ValueError('ROI exceeds bounded receiver-surface budget; request a smaller/sharded ROI')
            kept=surfaces[row['surface']]
            kept.append(row)
            kept.sort(key=lambda r:r['id'])
            if len(kept)>limit:kept.pop()
    # Bounded kept geometry: first stable IDs per source surface, independent of brightness.
    for s in surfaces:surfaces[s]=sorted(surfaces[s],key=lambda r:r['id'])[:limit]
    out=[];level=0
    while len(out)<limit:
        added=False
        for s in sorted(surfaces):
            if level<len(surfaces[s]):
                out.append(surfaces[s][level]);added=True
                if len(out)==limit:break
        if not added:break
        level+=1
    return out

def request(rows,map_sha,bounds,positions,known):
    lines=['JKHIDDEN1 '+map_sha,' '.join(map(str,bounds)),f'384 4 2 1 {int(known)} 1','1',
           ' '.join(map(str,[1,*positions,*bounds])),str(len(rows))]
    for row in rows:
        key=int(row['id'][:16],16)or 1
        lines.append(' '.join(map(str,[key,row['surface'],*row['position'],*row['normal'],*row['raw'],row['area'],int(row['transfer']==1 and row['primaries']==1)])))
    lines+=['2','0 1']
    return '\n'.join(lines)+'\n'

def run_case(tool,inputs,output,name,centre=None,radius=512,answers=None):
    manifest=json.loads((inputs/'manifest.json').read_text())
    view=TrainingView(inputs,manifest['map_sha256'])
    rows=choose(view,centre,radius)
    case=output/name;case.mkdir()
    if len(rows)<6:
        return dict(status='Unavailable',reason='Predeclared training ROI has fewer than six mapped receivers',requested=len(rows),map_sha256=manifest['map_sha256'])
    if centre is None:
        lo=[min(r['position'][a] for r in rows)for a in range(3)]
        hi=[max(r['position'][a] for r in rows)for a in range(3)]
        # Training can occupy only one side of a partition. A source can be in
        # neighbouring free space: never equate receiver bounds with source bounds.
        # This fixed 128-world-unit reach is declared before opening any answers.
        bounds=[*[v-128 for v in lo],*[v+128 for v in hi]]
        positions=bounds
    else:
        bounds=[*[v-radius for v in centre],*[v+radius for v in centre]]
        positions=bounds
    path=case/'training.request';path.write_text(request(rows,manifest['map_sha256'],bounds,positions,answers is not None),encoding='ascii')
    start=time.perf_counter()
    with subprocess.Popen([str(tool),str(view.geometry_path),str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)as process:
        stdout,stderr=process.communicate(timeout=180)
        cpp_peak=process_peak(process._handle)if sys.platform=='win32'else None
        if process.returncode:raise RuntimeError(name+': '+stderr)
    result=json.loads(stdout);write(case/'diagnostic.json',result)
    out=dict(status='CandidateDiagnostics'if result['code']==0 else'Unavailable',map_sha256=manifest['map_sha256'],training_manifest_sha256=digest(inputs/'manifest.json'),geometry_sha256=digest(view.geometry_path),request_sha256=digest(path),observations=len(rows),receiver_surfaces=len({r['surface']for r in rows}),
             input_scope='Blind training RGB + sanitized geometry only; no validation/held-out reads',diagnostic=result,
             resources=dict(cpp_peak_rss_bytes=cpp_peak,elapsed=time.perf_counter()-start))
    radius_at_declared_resolution=math.sqrt(sum((positions[a+3]-positions[a])**2 for a in range(3)))/(2*4*2**2)
    out['search_geometry']=dict(bounds=positions,coarse_divisions=4,refinement_rounds=2,
                                declared_finest_cell_radius_world=radius_at_declared_resolution,
                                meaning='Resolution scale, not a position confidence interval or guaranteed fully searched support')
    # The trusted controller opens answers ONLY after the C++ proposal is frozen.
    if answers is not None:
        truth=json.loads(answers.read_text())
        if truth['compiled_bsp_sha256']!=manifest['map_sha256']:raise ValueError('Private answer/map mismatch')
        if truth['ground_truth']['bounce_iterations']!=0:raise ValueError('Direct-only fixture attestation was invalid; do not publish this experiment')
        sources=truth['ground_truth']['lights'];distances=[]
        for s in sources:
            p=s['position_world'];distances.append(min((math.dist(p,c['position'])for c in result['candidates']),default=None))
        point_distances=[min((math.dist(s['position_world'],c['position'])for c in result['candidates']if c['source_kind']==0),default=None)for s in sources]
        out['trusted_answer_evaluation']=dict(nearest_proposal_world=distances,nearest_point_proposal_world=point_distances,
            recall_at_declared_grid_radius=sum(d is not None and d<=radius_at_declared_resolution for d in point_distances)/len(sources),
            recall_at_twice_grid_radius=sum(d is not None and d<=2*radius_at_declared_resolution for d in point_distances)/len(sources),
            source_count=len(sources),claim='Candidate-set coverage only, not recovered source XYZ/count/intensity',answer_sha256=digest(answers))
    else:out['stock_truth']='Unavailable: no physical-source recall or position accuracy'
    # Small own/local-only OBJ debug export; stock geometry/data stay in ignored build/.
    obj=['# Hidden source hypotheses, not recovered lights']
    for c in result['candidates']:obj.append('v '+' '.join(map(str,c['position'])))
    (case/'candidates.obj').write_text('\n'.join(obj)+'\n')
    print(name,out['status'],'candidates',len(result['candidates']),'rays',result['rays'],flush=True)
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--fixtures',type=Path,default=ROOT/'build/x02/compiled-fixtures-frozen')
    p.add_argument('--answers',type=Path,default=ROOT/'build/q3map2-bake-verified/answers')
    p.add_argument('--stock',type=Path,default=ROOT/'build/x02/hoth3-views/inputs');p.add_argument('--skip-stock',action='store_true')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);cases={}
    for name in ('point_room','two_colour'):
        inputs=a.fixtures/(name+'-views/inputs')
        cases[name]=run_case(a.tool.resolve(),inputs,a.output,name,answers=a.answers/(name+'.q3map-recipe.json'))
    if not a.skip_stock:
        cases['hoth3_declared_roi']=run_case(a.tool.resolve(),a.stock,a.output,'hoth3_declared_roi',centre=(3320,700,123),radius=512)
    result=dict(schema='org.openjk.s04-candidate-benchmark.v1',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),cases=cases,runtime_lighting_changed=False)
    write(a.output/'result.json',result)
if __name__=='__main__':main()
