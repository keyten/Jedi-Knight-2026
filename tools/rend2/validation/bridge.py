"""V02 compiler-controlled blind discovery bridge; no Python prediction formula.

Author/controller owns source answers. Native Hidden reads only geometry and
TRAIN RGB. Native Joint receives only frozen discovered positions. Oracle XYZ is
used in a separately labelled fixed-geometry control. GPL-2.0-or-later.
"""
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
for folder in ('bake_model', 'shadow_footprint', 'datasets'):
    sys.path.insert(0, str(ROOT/'tools/rend2'/folder))
from compare_bakes import brush, entity, make_assets
from compiler_boundary import load as geometry
from prepare_blind import prepare
from solver_view import TrainingView
from evaluation_view import EvaluationView
from tools.rend2.all_stock.run import execute, sha, write
from tools.rend2.joint.compiled import select, score
from tools.rend2.evaluation.metrics import detection
from tools.rend2.evaluation.x05.fixtures import publish_rows
from validate_cpp import source as oracle_source, request as oracle_request, run_tool as oracle_tool


def author(base):
    make_assets(base)
    # Four computational spaces, two genuine openings, a sealed third room,
    # finite emissive geometry and receivers of six normal orientations.
    solids=[((-768,-512,-16),(768,512,0)),
            ((-768,-512,0),(-752,512,336)),
            ((752,-512,0),(768,512,336)),
            ((-752,-512,0),(752,-496,336)),
            ((-752,496,0),(752,512,336)),
            ((-768,-512,320),(768,512,336)),
            ((-8,-496,0),(8,-96,320)),((-8,96,0),(8,496,320)),
            ((-8,-96,224),(8,96,320)),
            ((256,-496,0),(272,-256,320)),((256,-128,0),(272,496,320)),
            ((256,-256,224),(272,-128,320)),
            ((272,160,0),(752,176,320))]
    text='{\n"classname" "worldspawn"\n"gridsize" "64 64 64"\n'+''.join(brush(a,b) for a,b in solids)
    text+=brush((-528,-192,312),(-400,-144,316),'panel')+'}\n'
    for p in [(-640,-256,64),(128,-256,64),(512,-256,64),(512,256,64)]:
        text+=entity(dict(classname='info_player_start',origin=' '.join(map(str,p))))
    sources=[dict(kind='point',position=[-456,-300,208],rgb=[1.,.2,.1],intensity=450.),
             dict(kind='point',position=[448,-320,192],rgb=[.1,.4,1.],intensity=500.),
             dict(kind='spot',position=[448,352,256],rgb=[.2,1.,.25],intensity=600.,
                  axis=[0.,0.,-1.],target=[448,352,96],radius=80.)]
    for i,s in enumerate(sources):
        fields=dict(classname='light',origin=' '.join(map(str,s['position'])),
                    light=str(s['intensity']),_color=' '.join(map(str,s['rgb'])))
        if s['kind']=='spot':
            fields.update(target='v02_axis_'+str(i),radius=str(s['radius']),spawnflags='1')
            text+=entity(dict(classname='info_null',targetname=fields['target'],
                             origin=' '.join(map(str,s['target']))))
        text+=entity(fields)
    return text, sources


def mapped_rows(bsp):
    lumps,surfaces,triangles=geometry(bsp, strict_receiver_uv=False)
    digest=sha(bsp);rows=[];seen=set()
    for surface in surfaces:
        if surface['page']<0 or surface['shader']['name'].endswith('/panel'):
            continue
        for t in surface['triangles']:
            n=t.geometric_normal
            for y in range(max(0,math.ceil(min(v[1] for v in t.uv)*128)),min(128,math.floor(max(v[1] for v in t.uv)*128)),2):
                for x in range(max(0,math.ceil(min(v[0] for v in t.uv)*128)),min(128,math.floor(max(v[0] for v in t.uv)*128)),2):
                    uv=((x+.5)/128,(y+.5)/128)
                    # Complete footprint on one original planar triangle.
                    if any(min(t.bary(((x+dx)/128,(y+dy)/128)))<1e-7
                           for dx,dy in [(0,0),(0,1),(1,0),(1,1)]):
                        continue
                    key=(surface['page'],x,y)
                    if key in seen:continue
                    seen.add(key);p,normal=t.evaluate(uv)
                    if math.dist(n,normal)>1e-4:continue
                    at=(surface['page']*128*128+y*128+x)*3
                    raw=list(lumps[14][at:at+3])
                    if len(raw)!=3:raise ValueError('Original RGB span')
                    radius=max(math.dist(p,t.evaluate(((x+dx)/128,(y+dy)/128))[0])
                               for dx,dy in [(0,0),(0,1),(1,0),(1,1)])
                    ident=hashlib.sha256(f'{digest}/{key}/{surface["ordinal"]}/V02-mapped-v1'.encode()).hexdigest()
                    rows.append(dict(id=ident,image=surface['page'],page=surface['page']//2,
                        x=x,y=y,surface=surface['ordinal'],slot=0,style=0,position=list(p),
                        normal=list(n),shading_normal=list(normal),area=t.jacobian/128**2,
                        area_error=0.,footprint_radius=radius,coverage=2,issues=0,
                        classification=25,fit_candidate=1,material_signals=1,
                        material_uncertainty=23,kind=0,material=surface['shader']['name'],
                        raw=raw,encoding=0,channel_flags=[1|(8 if v==255 else 0)|(2 if v==0 else 0) for v in raw],
                        transfer=1,primaries=1,source=[v/255 for v in raw],scene=None,
                        deluxe_state=1,deluxe=None,positive_region=0,negative_region=0,
                        region_state=4,region_reasons=0,local_geometry_complete=True,
                        local_geometry_intersects=True,local_geometry_issues=0))
    return rows,(lumps,surfaces,triangles)


def hidden_request(rows,map_hash,areas,policy,direct):
    lo=policy['bounds_world'][:3];hi=policy['bounds_world'][3:]
    h=policy['hidden']
    text=['JKHIDDEN1 '+map_hash,' '.join(map(str,lo+hi)),
          f'{h["max_nodes_per_area"]} {h["coarse_divisions"]} {h["refinement_rounds"]} 1 {int(direct)} 1',
          str(len(areas))]
    for ident,bounds in areas:
        text.append(' '.join(map(str,[ident,*bounds,*bounds])))
    text.append(str(len(rows)))
    for r in rows:
        text.append(' '.join(map(str,[int(r['id'][:15],16)+1,r['surface'],
            *r['position'],*r['normal'],*r['raw'],r['area'],1])))
    text+=['2','0 1']
    return '\n'.join(text)+'\n'


def joint(tool,inputs,folds,positions,folder,wrong=False):
    folder.mkdir();m=json.loads((inputs/'manifest.json').read_text());training=TrainingView(inputs,m['map_sha256'])
    lines=['JKJOINT1',str(int(wrong)),str(len(positions))]
    for j,p in enumerate(positions):lines.append(' '.join(map(str,[j+1,*p])))
    indices={}
    for role in ['train','validation','heldout']:
        indices[role]={};lines.append(str(len(folds[role])))
        for j,r in enumerate(folds[role]):
            ident=int(r['id'][:15],16)+1;indices[role][ident]=j+1
            block=1+sum((math.floor(v/128)+1000)*10000**k for k,v in enumerate(r['position']))
            row=[ident,r['surface'],0,r['x'],r['y'],*r['position'],*r['normal'],r['area'],block,r['surface']+1]
            if role=='train':row+=r['raw']
            lines.append(' '.join(map(str,row)))
    path=folder/'request.txt';path.write_text('\n'.join(lines)+'\n')
    events=[];callbacks=[]
    with subprocess.Popen([str(tool),str(training.geometry_path),str(path)],stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1) as child:
        expired=[]
        def stop():
            expired.append(True)
            if child.poll() is None:child.kill()
        timer=threading.Timer(180,stop);timer.start()
        try:
            for line in child.stdout:
                event=json.loads(line);events.append(event)
                if event['event'] in ('validation','final'):
                    role='validation' if event['event']=='validation' else 'heldout'
                    predicted=[dict(p,id=indices[role][p['id']]) for p in event['predictions']]
                    receipt=score(folds[role],predicted);callbacks.append(dict(role=role,score=receipt))
                    child.stdin.write(('SCORE ' if role=='validation' else 'FINAL ')+
                        ' '.join(map(str,[int(receipt['available']),int(receipt['positive']),int(receipt['required']),
                                        receipt['mse'],receipt['black'],receipt['objective'],receipt['worst']]))+'\n')
                    child.stdin.flush()
            _,stderr=child.communicate(timeout=10)
        finally:timer.cancel()
    (folder/'events.json').write_text(json.dumps(events,indent=2)+'\n')
    if child.returncode or expired:raise RuntimeError('Native bridge joint: '+stderr[-1500:])
    result=events[-1]
    if result.get('event')!='result':raise ValueError('No native result')
    heldout=None
    if result.get('preferred_available'):
        heldout=score(folds['heldout'],[dict(p,id=indices['heldout'][p['id']]) for p in result['heldout_predictions']])
    return dict(result=result,heldout=heldout,callbacks=callbacks,request_sha256=sha(path),
                capability='FixedGeometryBlindIntensityFit' if 'oracle' in folder.name else 'BlindDiscoveryThenFixedCandidateJointFit',
                geometry_count=len(positions),native_predictions=True)


def local_root_oracle(tool,bsp,folds,truth,folder):
    """Supplied-answer forward control, never an input to source discovery.

    Local point/spot roots are supported here. The compiled surface prism and
    compiler bounce are deliberately absent, so this is not a complete oracle.
    """
    folder.mkdir();lumps,_,casters=geometry(bsp,strict_receiver_uv=False)
    sources=[]
    for s in truth:
        item=oracle_source(kind=int(s['kind']=='spot'),position=s['position'],
            axis=s.get('axis',[0,0,-1]),rgb=[s['intensity']*c for c in s['rgb']])
        if s['kind']=='spot':item.update(target=math.dist(s['target'],s['position']),radius=s['radius'])
        sources.append(item)
    results={}
    for role,rows in folds.items():
        query=[dict(world=r['position'],normal=r['normal'],surface=r['surface']) for r in rows]
        result=oracle_tool(tool,folder,role,oracle_request(bsp,lumps,casters,query,{},sources))
        predictions=[dict(id=i+1,available=r['available'],rgb=r.get('packed',[0,0,0]))
                     for i,r in enumerate(result['rows'])]
        results[role]=dict(score=score(rows,predictions),pairs=result['pairs'],rays=result['rays'],
                           available=sum(r['available'] for r in result['rows']),count=len(rows))
    return dict(roles=results,scope='Supplied local point/spot XYZ/type/Q; surface-prism and bounce omitted',
                complete_oracle=False,answers_exposed_to_discovery=False)


def run(args,policy):
    out=args.output/'bridge';out.mkdir()
    q3=args.q3map.resolve()
    pinned=json.loads((ROOT/'tools/rend2/bake_model/source-lock.json').read_text())
    from bake_model import COMPILER_SHA256
    if sha(q3)!=COMPILER_SHA256:raise ValueError('Pinned q3map2 binary mismatch')
    base=out/'authoring/base';text,answers=author(base)
    private=out/'private';private.mkdir();answer_file=private/'source-answer.json'
    write(answer_file,dict(sources=answers,surface=dict(kind='compiled_surface_prism',
        bounds=[[-528,-192,312],[-400,-144,316]],physical_rectangle_or_tube=False),
        definition='Frozen authored source parameters, compiler-relative not SI'))
    results={}
    for variant in policy['variants']:
        name=policy['name']+'_'+variant;mp=base/'maps'/(name+'.map');mp.write_text(text)
        bounces=1 if variant=='passive_bounce1' else 0
        commands=[]
        for stage in [['-meta','-leaktest','-noflares','-samplesize','16'],['-vis'],
                      ['-light','-deluxe','-deluxemode','0','-gamma','1','-compensate','1',
                       '-exposure','0','-bounce',str(bounces),'-super','1','-nocollapse']]:
            cmd=[q3,'-game','ja','-fs_basepath',base.parent,'-fs_game','base','-threads','1',*stage,mp]
            receipt=execute(cmd,out,name+'-compile-'+str(len(commands)),seconds=180,memory=1024**3)
            log=(out/(name+'-compile-'+str(len(commands))+'.log')).read_text(errors='replace')
            if receipt['exit_code'] or re.search(r'Entity leaked|\*+\s*ERROR\b',log,re.I):raise RuntimeError('Compiler fixture failed')
            commands.append(list(map(str,cmd)))
        bsp=mp.with_suffix('.bsp');folder=out/variant;folder.mkdir()
        write(folder/'recipe.json',dict(bsp_sha256=sha(bsp),map_sha256=sha(mp),answer_sha256=sha(answer_file),
            compiler_sha256=sha(q3),commands=commands,bounces=bounces,
            inputs={str(p.relative_to(base)):sha(p) for p in base.rglob('*') if p.suffix in ('.shader','.tga','.txt')}))
        raw=folder/'raw-geometry.jkobs'
        _,source_surfaces,_=geometry(bsp,strict_receiver_uv=False)
        material_file=folder/'controlled-materials.tsv'
        material_file.write_text('JKCONTROLLEDMATERIAL1 '+sha(bsp)+' '+str(len(source_surfaces))+'\n'+
            '\n'.join(f'{s["ordinal"]} {s["shader"]["name"]} {int(s["shader"]["name"].endswith("/panel"))}' for s in source_surfaces)+'\n')
        r=execute([args.native/'lightrecon_regions_inspect.exe',bsp,folder/'geometry',0,0,160,800,128,32,1,raw,material_file],folder,'geometry',seconds=180)
        if r['exit_code']:raise RuntimeError('Controlled BSP geometry capture failed')
        r=execute([args.native/'lightrecon_dataset_quality.exe','scan',raw,folder/'sanitized'],folder,'sanitize')
        if r['exit_code']:raise RuntimeError('Controlled geometry sanitization failed')
        rows,_=mapped_rows(bsp);prefix=folder/'samples'
        publish_rows(prefix,rows,sha(bsp),folder/'sanitized.public.jkobs')
        s=policy['split'];prepare(prefix,folder/'views',s['block_world'],s['gap_world'],s['seed'],s['origin_world'])
        inputs=folder/'views/inputs';train=TrainingView(inputs,sha(bsp));training=list(train.observations())
        evaluator=EvaluationView(folder/'views/evaluator',sha(bsp),permitted=('validation','heldout'))
        # Geometry-only equal source-independent evaluation schedule, not the source-answer layout.
        folds={role:select(training if role=='train' else evaluator.observations(role),64)
               for role in ('train','validation','heldout')}
        jobs=[];candidates=[]
        # Spatial shards are derived from public geometry, fixed lattice and map bounds.
        for ix in range(4):
            for iy in range(2):
                bounds=[-752+ix*376,-496+iy*496,0,-752+(ix+1)*376,-496+(iy+1)*496,320]
                support=[r for r in training if all(bounds[k]-64<=r['position'][k]<=bounds[k+3]+64 for k in range(3))]
                selected=select(support,policy['hidden']['samples_per_job'])
                ident=ix*2+iy+1
                if len(selected)<6:jobs.append(dict(id=ident,state='Unavailable',reason='Too few independent training receivers'));continue
                request=folder/('search-'+str(ident)+'.request')
                request.write_text(hidden_request(selected,sha(bsp),[(ident,bounds)],policy,False))
                receipt=execute([args.native/'lightrecon_hidden_tool.exe',train.geometry_path,request],folder,'search-'+str(ident),seconds=180)
                if receipt['exit_code']:jobs.append(dict(id=ident,state='Failed',resources=receipt));continue
                result=json.loads((folder/('search-'+str(ident)+'.log')).read_text())
                write(folder/('search-'+str(ident)+'.json'),result)
                jobs.append(dict(id=ident,state='Produced' if result['candidates'] else 'NoSupportedEvidence',
                    count=len(result['candidates']),complete=result['search_complete'],work=result['work'],
                    rays=result['rays'],resources=receipt,request_sha256=sha(request)))
                candidates+=result['candidates']
        # Freeze before reading private answers for matching/model diagnostics.
        write(folder/'frozen-candidates.json',candidates)
        truth=json.loads(answer_file.read_text())['sources']
        nearest=[]
        for t in truth:
            ds=[math.dist(t['position'],c['position']) for c in candidates]
            nearest.append(min(ds) if ds else None)
        threshold=policy['matching']['position_world'];covered=sum(d is not None and d<=threshold for d in nearest)
        # Set coverage is not selected-emitter precision/recall or unique source identity.
        recall=covered/len(truth);false=sum(all(math.dist(t['position'],c['position'])>threshold for t in truth) for c in candidates)
        point_candidates=[c for c in candidates if c.get('source_kind')==0]
        chosen=[]
        for c in point_candidates:
            if all(math.dist(c['position'],p)>32 for p in chosen):chosen.append(c['position'])
            if len(chosen)==4:break
        fits={}
        if min(map(len,folds.values()),default=0)>=8:
            if chosen:fits['discovered']=joint(args.native/'lightrecon_joint_tool.exe',inputs,folds,chosen,folder/'discovered')
            fits['oracle_xyz_point_only']=joint(args.native/'lightrecon_joint_tool.exe',inputs,folds,[t['position'] for t in truth],folder/'oracle-xyz')
        oracle=local_root_oracle(args.native/'lightrecon_bake_forward_tool.exe',bsp,folds,truth,folder/'oracle-forward')
        selected_metrics=None
        if fits.get('discovered',{}).get('result',{}).get('preferred_available'):
            # This adapter fits point candidates only. It cannot claim spot
            # orientation or finite-source recovery from a point explanation.
            predicted=[dict(kind='point',position=chosen[s['id']-1])
                       for s in fits['discovered']['result']['sources']]
            policy_match=dict(position_world=threshold,angle_degrees=10,cone_degrees=10,
                              dimension_relative=.25,roll_degrees=15)
            selected_metrics=detection([dict(kind=s['kind'],position=s['position']) for s in truth],
                                       predicted,policy_match,1.)
            # No independently established metres mapping for this relative
            # compiler experiment. Do not publish the convenience conversion.
            selected_metrics['geometric_summary'].pop('position_metres',None)
            for m in selected_metrics['matches']:m.pop('position_metres',None);m.pop('xyz_metres',None)
        results[variant]=dict(bsp_sha256=sha(bsp),answer_sha256=sha(answer_file),mapped_rows=len(rows),
            partition_counts={role:len(values) for role,values in folds.items()},jobs=jobs,candidate_count=len(candidates),
            nearest_proposal_world=nearest,candidate_set_recall=recall,proposal_far_from_known_local_roots=false,
            recall_gate_passed=recall>=policy['recall_gate'],selected_source_metrics=selected_metrics,
            source_type_scope='Hidden proposes point/spot only; finite shader prism model unsupported in this adapter',
            fixed_geometry_control_scope='Oracle XYZ point-only is deliberately incomplete for true spot/surface source; no fake full oracle score',
            fits=fits,oracle_local_roots_forward=oracle,profile='Source-locked q3map2 controlled; candidate generic traces remain conditional',
            unknown_SI=True,source_inputs_exposed=False,heldout_rgb_exposed=False)
        write(out/'results.json',results)
    return results


def replay(source,out,native):
    """Exactly identical public BSP-derived inputs; no compiler rebake."""
    out.mkdir(parents=True,exist_ok=False);checks={};resources=[]
    for variant in ('direct','passive_bounce1'):
        folder=source/'bridge'/variant;target=out/variant;target.mkdir()
        inputs=folder/'views/inputs';manifest=json.loads((inputs/'manifest.json').read_text())
        train=TrainingView(inputs,manifest['map_sha256'])
        for request in sorted(folder.glob('search-*.request')):
            label=request.stem
            receipt=execute([native/'lightrecon_hidden_tool.exe',train.geometry_path,request],target,label,seconds=180)
            resources.append(receipt)
            value=json.loads((target/(label+'.log')).read_text())
            first=json.loads((folder/(label+'.json')).read_text())
            # Wall-clock load/search timings are not semantic determinism.
            checks[variant+'/'+label]=receipt['exit_code']==0 and {
                k:v for k,v in value.items() if k!='resources'}=={
                k:v for k,v in first.items() if k!='resources'}
        evaluator=EvaluationView(folder/'views/evaluator',manifest['map_sha256'],permitted=('validation','heldout'))
        folds={role:select(list(train.observations()) if role=='train' else evaluator.observations(role),64)
               for role in ('train','validation','heldout')}
        for kind in ('discovered','oracle-xyz'):
            previous=folder/kind
            if not previous.exists():continue
            lines=(previous/'request.txt').read_text().splitlines();count=int(lines[2])
            positions=[list(map(float,line.split()[1:])) for line in lines[3:3+count]]
            joint(native/'lightrecon_joint_tool.exe',inputs,folds,positions,target/kind)
            checks[variant+'/'+kind]=json.loads((previous/'events.json').read_text())==json.loads((target/kind/'events.json').read_text())
    result=dict(schema='org.openjk.blind-bridge-replay/1',checks=checks,all_passed=all(checks.values()),
                resources=resources,scope='Identical immutable public inputs, including identical BSP hash')
    write(out/'results.json',result)
    if not result['all_passed']:raise AssertionError('Exact bridge replay changed')
    return result
