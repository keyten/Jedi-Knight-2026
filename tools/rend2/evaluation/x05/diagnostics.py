"""Existing shadow/compiler witnesses made explicit acceptance diagnostics.
Controller only. No target-copy output is a source reconstruction.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from dataclasses import replace
import math
from acceptance import evaluate, predictions, PROFILE, digest
from fixtures import ROOT, HERE, load_geometry, floor_rows, reference, write
from analytic import rectangle_reference
from geometry import rectangle, Triangle
from integrate import integrated, sampled, Source, Scene
from compiler_boundary import run as replay_boundary
from bake_model import encode_color, spot_factor
from hypotheses import continuous_encode


def shadow_contract(output, old22):
    # Re-run all 35 original observations, without dropping the documented band.
    compiler=replay_boundary(old22/'authoring/base/maps/multi_sun.bsp',output/'boundary')
    fp=rectangle(1.3,1.4,-.05,.05)
    wall=tuple(Triangle('wall'+str(i),100+i,tuple(((1.,-10.,0.),(1.,10.,0.),(1.,10.,1.),(1.,-10.,1.))[j] for j in ids),
        ((0.,0.),(1.,0.),(0.,1.)),((1.,0.,0.),)*3) for i,ids in enumerate(((0,1,2),(0,2,3))))
    scene=Scene(wall);records=[]
    for x in (-.1,0.,.1):
        source=Source(position=(x,0.,4.));exact=rectangle_reference((1.3,1.4,-.05,.05),source.position)
        value=integrated(fp,source,scene,tolerance=1e-10)
        def integrated_value(dx):return integrated(fp,replace(source,position=(x+dx,0.,4.)),scene,tolerance=1e-10)['visibility_upper']
        def sampled_value(dx):return sampled(fp,replace(source,position=(x+dx,0.,4.)),scene,n=2)['visibility_upper']
        h=1e-5;fd=(integrated_value(h)-integrated_value(-h))/(2*h);fixed=(sampled_value(h)-sampled_value(-h))/(2*h)
        # At x=.1 the edge meets the footprint endpoint. A unique derivative
        # does not exist: central differences converge to the mean of the two
        # one-sided slopes, not the closed form's selected branch derivative.
        kink=abs(x-.1)<1e-12
        records.append(dict(source_x=x,visibility=exact['visibility'],reference_derivative=None if kink else exact['visibility_jacobian'][0],
            derivative_status='Nondifferentiable' if kink else 'Differentiable',one_sided_derivatives=[10/3,0.] if kink else None,
            integrated_visibility=value['visibility_upper'],integrated_derivative=fd,fixed_2x2_derivative=fixed,
            work=value['work'],reference_value=exact['value'],integrated_value=value['contribution_upper'][0]))
    ok=(compiler['exact_affected_count']==35 and compiler['replay_all_floor_max_error']==0 and
        all(abs(r['visibility']-r['integrated_visibility'])<1e-9 and
            (abs(r['reference_derivative']-r['integrated_derivative'])<1e-6 if r['reference_derivative'] is not None
             else abs(r['integrated_derivative']-sum(r['one_sided_derivatives'])/2)<1e-6) for r in records) and
        any(abs(r['integrated_derivative'])>1 and r['fixed_2x2_derivative']==0 for r in records))
    return dict(id='moving_shadow_boundary',status='Passed' if ok else 'Failed',categories=['ShadowBoundary'],
        compiler=dict(bsp_sha256=compiler['bsp_sha256'],boundary_file_sha256=digest(output/'boundary/boundary.json'),
            texels=35,channels=105,old_max_discrepancy=28,replay_max_error=compiler['replay_all_floor_max_error']),
        analytic_records=records,comparison='SI analytic moving-edge visibility and source-locked encoded compiler replay are separate tests',
        unsupported='Not a universal Raven bias rule or general historical bake replay')


def spot_contract(old22,policy,private_cases=None):
    rows,(_,_,triangles)=floor_rows(old22/'authoring/base/maps/spot.bsp',policy)
    truth=dict(profile='spot',sources=[dict(origin=[0,0,192],rgb=[1,1,1],intensity=300)])
    # Quadrants, fixed by receiver geometry only, keep the annulus' two sides
    # separate. Gap is the same conservative filter support as X02.
    edge=[]
    for r in rows:
        p=r['position'];margin=3*r['footprint_radius']+1
        factor=spot_factor([p[0],p[1],1.],[0,0,192],[0,0,-1],128,64)
        if not 0<factor<1 or min(abs(p[0]),abs(p[1]))<=margin:continue
        r['partition']='heldout' if p[1]>0 else 'train';r['block']=','.join(str(v>0) for v in p[:2])
        r['groups']=['angular_edge'];edge.append(r)
    held=[r for r in edge if r['partition']=='heldout']
    contract=[dict(id='spot_angular_edge',kind='PositiveLight',group='angular_edge')]
    correct=predictions(held,reference(held,triangles,truth)[0]);wrong=predictions(held,reference(held,triangles,truth,'wrong_spot')[0])
    if private_cases is not None:private_cases.append(dict(id='spot_angular_edge',rows=held,contracts=contract,
        bsp_sha256=digest(old22/'authoring/base/maps/spot.bsp'),categories=['ShadowBoundary','PositiveLight']))
    controls=dict(reference=evaluate(held,correct,contract,policy),wrong_no_cone=evaluate(held,wrong,contract,policy),
        black=evaluate(held,predictions(held),contract,policy))
    return dict(id='spot_angular_edge',status='Passed' if controls['reference']['success'] and not controls['wrong_no_cone']['success'] and not controls['black']['success'] else 'Failed',
        controls=controls,train_count=len(edge)-len(held),heldout_count=len(held),
        bsp_sha256=digest(old22/'authoring/base/maps/spot.bsp'),categories=['PositiveLight','ShadowBoundary'],
        semantics='All source rays geometrically clear; angular edge is NOT an opaque shadow boundary',
        partition='Opposite Y half spaces with 3-footprint-radius filter exclusion; X halves are two independent heldout blocks')


def encoding_contract():
    values=[[0.,0.,0.],[.9,12.9,23.1],[254.99,127.8,1.2],[1000.,500.,200.],[256.,128.,0.],[255.,255.,255.]]
    records=[]
    for value in values:
        encoded=encode_color(value);continuous=[min(255.,x) for x in continuous_encode(value)]
        good=all(b<=v<=min(255,b+1) for b,v in zip(encoded,continuous))
        bad=[min(255.,x) for x in value]
        wrong=any(not b<=v<=min(255,b+1) for b,v in zip(encoded,bad))
        records.append(dict(input_relative=value,encoded=encoded,continuous=continuous,interval_matches=good,independent_clamp_rejected=wrong))
    return dict(id='correct_encoding',status='Passed' if all(r['interval_matches'] for r in records) and any(r['independent_clamp_rejected'] for r in records) else 'Failed',
        categories=['EncodingQuantization'],profile=PROFILE,records=records,
        endpoint='255 is the encoded shared-normalized endpoint; independent pre-encoding channel lower bounds are not asserted')


def unknown_contracts(policy):
    r=dict(id='opaque-missing-geometry',raw=[32]*3,area=1.,block='fixture',groups=['unknown'])
    contract=[dict(id='unknown_geometry',group='unknown',kind='ExpectedUnknown')]
    footprint=rectangle(1.3,1.4,-.05,.05)
    value=integrated(footprint,Source(),Scene(unbounded_unknown=True))
    unknown=predictions([r],status='Unknown',reason='Potential unbounded opaque blocker; no certified visibility')
    good=evaluate([r],unknown,contract,policy);black=evaluate([r],predictions([r]),contract,policy)
    geometry=dict(id='unknown_geometry',status='Passed' if good['success'] and not black['success'] and value['visibility_lower']==0 and value['visibility_upper']==1 else 'Failed',
        expected_unknown=good,black=black,visibility_interval=[value['visibility_lower'],value['visibility_upper']],categories=['InvalidUnsupported'])
    photo=evaluate([r],predictions([r]),contract,policy,encoding='UnknownProfile/unknown-primaries')
    photometry=dict(id='unknown_photometry',status='Passed' if photo['status']=='NotComparable' and not photo['success'] else 'Failed',result=photo,categories=['InvalidUnsupported'])
    return [geometry,photometry,dict(id='gi_operator_unavailable',status='Passed',capability='Unsupported',
        reason='No verified physical bounce observation operator is implemented here. Existing bounce controls remain mixed direct+bounce diagnostics; no black GI prediction is accepted.',
        positive_forward_pass=False,categories=['GIIndirect','InvalidUnsupported'])]


def run_diagnostics(output,old22,policy):
    private=[]
    records=[shadow_contract(output,old22),spot_contract(old22,policy,private),encoding_contract(),*unknown_contracts(policy)]
    boundary=__import__('json').loads((output/'boundary/boundary.json').read_text())
    rows=[dict(id=r['id'],raw=r['compiler_rgb8'],area=r['footprint_area'],groups=['boundary'],block=str(math.floor(r['world'][1]/128))) for r in boundary['records']]
    private.append(dict(id='moving_shadow_boundary',rows=rows,bsp_sha256=boundary['bsp_sha256'],
        contracts=[dict(id='moving_shadow_boundary',group='boundary',kind='PositiveLight')],categories=['ShadowBoundary']))
    encoded=records[2]
    rows=[dict(id='encoding/'+str(i),raw=r['encoded'],area=1.,groups=['encoding'],block=str(i)) for i,r in enumerate(encoded['records'])]
    private.append(dict(id='correct_encoding',rows=rows,bsp_sha256='self-authored-encoding-domain-v1',
        contracts=[dict(id='correct_encoding',group='encoding',kind='EncodingQuantization')],categories=['EncodingQuantization']))
    for key,kind in [('unknown_geometry','ExpectedUnknown'),('unknown_photometry','ExpectedNotComparable'),('gi_operator_unavailable','ExpectedUnknown')]:
        private.append(dict(id=key,rows=[dict(id=key,raw=[0,0,0],area=1.,groups=['query'],block='synthetic-query')],
            bsp_sha256='self-authored-capability-query-v1',categories=['InvalidUnsupported'],
            contracts=[dict(id=key,group='query',kind=kind)],
            semantics='Capability/status request only; no measured target RGB or physical bounce accuracy is asserted'))
    for name,profile in [('linear','linear'),('no_angle','noangle')]:
        rows,(_,_,triangles)=floor_rows(old22/'authoring/base/maps'/(name+'.bsp'),policy)
        held=[dict(r,groups=['direct']) for r in rows if r['partition']=='heldout']
        private.append(dict(id='profile_kernel_'+name,rows=held,bsp_sha256=digest(old22/'authoring/base/maps'/(name+'.bsp')),
            # Kernel conformance includes dark cutoff receivers. It is not a
            # positive-support or source-identification claim; the independent
            # broad profile experiment supplies that discrimination evidence.
            categories=['ProfileDiscrimination'],contracts=[dict(id='profile_kernel_'+name,group='direct',kind='KernelConformance')]))
    manifest=[]
    for case in private:
        path=output/(case['id']+'-controller-case.json')
        key=case['id'];rows=case['rows']
        if key=='moving_shadow_boundary':values=[r['epsilon_replay_rgb8'] for r in boundary['records']]
        elif key=='correct_encoding':values=[r['continuous'] for r in encoded['records']]
        elif key=='spot_angular_edge':
            _,_,triangles=load_geometry(old22/'authoring/base/maps/spot.bsp')
            values=reference(rows,triangles,dict(profile='spot',sources=[dict(origin=[0,0,192],rgb=[1,1,1],intensity=300)]))[0]
        elif key.startswith('profile_kernel_'):
            name=key.removeprefix('profile_kernel_');_,_,triangles=load_geometry(old22/'authoring/base/maps'/(name+'.bsp'))
            values=reference(rows,triangles,dict(profile='linear' if name=='linear' else 'noangle',sources=[dict(origin=[0,0,192],rgb=[1,1,1],intensity=300)]))[0]
        else:values=None
        reference_prediction=(predictions(rows,values) if values is not None else predictions(rows,
            status='NotComparable' if key=='unknown_photometry' else 'Unsupported' if key=='gi_operator_unavailable' else 'Unknown',
            reason='Explicitly missing optical/physical/transport evidence; not a black prediction'))
        controls=dict(reference=evaluate(rows,reference_prediction,case['contracts'],policy),
            black=evaluate(rows,predictions(rows),case['contracts'],policy),missing=evaluate(rows,[],case['contracts'],policy))
        if key=='moving_shadow_boundary':
            controls['old_centre_analytic']=evaluate(rows,predictions(rows,[r['old_analytic_rgb8'] for r in boundary['records']]),case['contracts'],policy)
        records.append(dict(id=key+'-prediction-contract',categories=case['categories'],controls=controls,
            status='Passed' if controls['reference']['success'] and not controls['black']['success'] and not controls['missing']['success']
                and (key!='moving_shadow_boundary' or not controls['old_centre_analytic']['success']) else 'Failed'))
        write(path,dict(case,schema='org.openjk.forward-private-case.v1',map_sha256=case['bsp_sha256'],output_profile=PROFILE))
        manifest.append(dict(id=case['id'],required=True,role='diagnostic',bsp_sha256=case['bsp_sha256'],output_profile=PROFILE,
            private_case=str(path.resolve()),private_case_sha256=digest(path),categories=case['categories'],contracts=case['contracts']))
    write(output/'diagnostics.json',records);return records,manifest
