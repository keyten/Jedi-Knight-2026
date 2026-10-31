#!/usr/bin/env python3
"""Bounded stock producer attempts from legitimate glow geometry and TRAIN data.
Licensed inputs/outputs stay local. No stock XYZ/photometric truth is assumed.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,json,math,subprocess,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'tools/rend2/datasets'))
from solver_view import TrainingView
from evaluation_view import EvaluationView
from prepare_blind import rows as original_rows
from tools.rend2.joint.compiled import select,score,sha
from tools.rend2.amplitude_fit.benchmark import peak

def attempt(tool,rays,prefix,views,folder):
    folder.mkdir(parents=True,exist_ok=False)
    source=json.loads(prefix.with_suffix('.source.json').read_text());coverage=json.loads((views/'coverage.json').read_text())
    train=TrainingView(views/'inputs',source['map_sha256'])
    # Hints contain only original static glow geometry. No inferred/true amplitudes.
    hintfile=folder/'hints.tsv'
    subprocess.run([str(rays.resolve()),str(train.geometry_path),'--hints',str(hintfile.resolve())],check=True,capture_output=True,text=True)
    hints=[]
    for line in hintfile.read_text().splitlines():
        f=line.split();p=list(map(float,f[1:4]));n=list(map(float,f[4:7]));hints.append((int(f[0]),[v+16*a for v,a in zip(p,n)]))
    supported={r['id'] for r in original_rows(prefix.with_suffix('.rows.jsonl')) if r.get('kind')==0 and r.get('shading_normal') is not None}
    rows=[r for r in train.observations() if r['id'] in supported and r['position'] and r['normal'] and r['area'] and r['area']>0]
    # Fixed geometry bins, normal diversity + positive TRAIN support only.
    bins={}
    for r in rows:bins.setdefault(tuple(math.floor(v/512) for v in r['position']),[]).append(r)
    ranked=[]
    for cell,rs in bins.items():
        centre=[(v+.5)*512 for v in cell]
        if not any(math.dist(p,centre)<768 for _,p in hints):continue
        positive=[r for r in rs if max(r['raw'])>=8]
        normals={tuple(round(v,1) for v in r['normal']) for r in positive}
        ranked.append((-len(normals),-len(positive),cell,centre))
    centre=min(ranked)[3] if ranked else (next(iter(rows))['position'] if rows else [0,0,0])
    candidates=[]
    for ident,p in sorted(hints,key=lambda h:(math.dist(h[1],centre),h[0])):
        if math.dist(p,centre)>1024:break
        if all(math.dist(p,q)>32 for _,q in candidates):candidates.append((ident,p))
        if len(candidates)==2:break
    # Geometry-only filtering precedes exposing private RGB to trusted scorer.
    evaluator=EvaluationView(views/'evaluator',source['map_sha256'],permitted=('validation','heldout'))
    folds={'train':select([r for r in rows if math.dist(r['position'],centre)<768],24)}
    for role in ('validation','heldout'):
        folds[role]=select([r for r in evaluator.observations(role) if r['id'] in supported and math.dist(r['position'],centre)<768 and r['area'] and r['normal']],24)
    if min(map(len,folds.values()))<4:
        folds={'train':select(rows,24),'validation':select([r for r in evaluator.observations('validation') if r['id'] in supported and r['area'] and r['normal']],24),'heldout':select([r for r in evaluator.observations('heldout') if r['id'] in supported and r['area'] and r['normal']],24)}
        if min(map(len,folds.values()))<4:raise ValueError('Even full-map geometry strata have insufficient independent coverage')
    hashes=[sha(views/('inputs' if role=='train' else 'evaluator')/(role+'.jsonl')) for role in folds]
    def protocol(selector):
        lines=['JKJOINT13',' '.join(hashes),' '.join(map(str,[len(source['pages']),source['raw_pixels'],coverage['derived_texel_denominator'],coverage['groups']['map']['counts'].get('mapped',0),int(coverage['all_pages_complete'])])),str(selector),str(len(candidates))]
        for ident,p in candidates:lines.append(' '.join(map(str,[ident,*p])))
        ids={};seen=set()
        for role,rs in folds.items():
            lines.append(str(len(rs)));ids[role]={}
            for i,r in enumerate(rs):
                ident=int(r['id'][:15],16)+1
                if ident in seen:raise ValueError('Cross-partition duplicate')
                seen.add(ident);ids[role][ident]=i+1
                block=1+sum((math.floor(v/128)+10000)*100000**k for k,v in enumerate(r['position']))
                v=[ident,r['surface'],r['slot'],r['x'],r['y'],*r['position'],*r['normal'],r['area'],block,0]
                if role=='train':v+=r['raw']
                lines.append(' '.join(map(str,v)))
        return '\n'.join(lines)+'\n',ids
    attempts=[]
    # Conditional alternatives first; UnknownProfile diagnostic is a separate
    # output, never a numerical substitute or a successful fit.
    for selector in (2,3):
        text,ids=protocol(selector);request=folder/f'stock-{selector}.txt';request.write_text(text)
        output=folder/f'cache-{selector}';output.mkdir();start=time.perf_counter();events=[];expired=[]
        command=[str(tool.resolve()),str(train.geometry_path),str(request.resolve()),str(output.resolve()),'--stock']
        with subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1) as child:
            def stop():
                expired.append(True)
                if child.poll() is None:child.kill()
            timer=threading.Timer(120,stop);timer.start()
            try:
                for line in child.stdout:
                    event=json.loads(line);events.append(event)
                    if event['event'] in ('validation','final'):
                        role='validation' if event['event']=='validation' else 'heldout'
                        s=score(folds[role],[dict(p,id=ids[role][p['id']]) for p in event['predictions']])
                        child.stdin.write(('SCORE ' if role=='validation' else 'FINAL ')+' '.join(map(str,[int(s['available']),int(s['positive']),int(s['required']),s['mse'],s['black'],s['objective'],s['worst']]))+'\n');child.stdin.flush()
                _,stderr=child.communicate(timeout=5);rss=peak(child)
            finally:timer.cancel()
        (folder/f'stderr-{selector}.txt').write_text(stderr)
        attempts.append(dict(selector=selector,exit_code=child.returncode,time_budget_exceeded=bool(expired),seconds=time.perf_counter()-start,peak_process_rss_bytes=rss,
            result=events[-1] if events else None,stderr=stderr,validation_attempts=sum(e['event']=='validation' for e in events)))
    return dict(state='Attempted',map_sha256=source['map_sha256'],historical_profile='UnknownProfile',centre=centre,candidate_ids=[v[0] for v in candidates],glow_hints=len(hints),counts={k:len(v) for k,v in folds.items()},source=source,coverage=coverage['groups']['map'],attempts=attempts,
        conditional_on='Point hypotheses offset 16wu from legitimate glow geometry; unknown bake profile/transfer/primaries, no source XYZ truth',geometry_sha256=sha(train.geometry_path),partition_sha256=hashes)

def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--tool',type=Path,required=True);p.add_argument('--rays',type=Path,required=True)
    p.add_argument('--private',type=Path,default=Path('C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-x06-20261028'));p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.out.resolve().is_relative_to(ROOT):raise ValueError('Keep licensed outputs external')
    a.out.mkdir(parents=True,exist_ok=False);census=json.loads((a.private/'census.json').read_text());results={}
    for name in ('hoth3','hoth2','vjun2','kor1','yavin1b','mp/ffa1'):
        if name in census:
            prefix=Path(census[name]['prefix']);views=Path(census[name]['views'])
        else:prefix=ROOT/'build/x02/final'/name;views=ROOT/'build/x02/final'/(name+'-views')
        try:results[name]=attempt(a.tool,a.rays,prefix,views,a.out/name.replace('/','_'))
        except Exception as e:results[name]=dict(state='Failed',reason=str(e))
        (a.out/'results.json').write_text(json.dumps(results,indent=2,allow_nan=False)+'\n')
        print(name,results[name]['state'],flush=True)
if __name__=='__main__':main()
