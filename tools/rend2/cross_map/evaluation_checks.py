#!/usr/bin/env python3
"""Existing X05 gates and blind export on the NEW compiler cases, no solver.
GPL-2.0-or-later. Answers are private controller inputs.
"""
import argparse
import copy
import json
from pathlib import Path
import sys

from controlled import ROOT,HERE,x05,floor_rows,forward,recipe,score
from census import measured,write
from prepare_blind import prepare
sys.path.append(str(HERE.parent/'evaluation/x05'))
from acceptance import evaluate,predictions,PROFILE
from adapters import audit_training
from metrics import detection


def run(work,sky,output):
    output.mkdir(parents=True,exist_ok=False);policy=json.loads((HERE.parent/'evaluation/x05/policy.json').read_text())
    tool=ROOT/'build/lightrecon-core-renderers/Release/lightrecon_dataset_quality.exe';inspector=tool.with_name('lightrecon_regions_inspect.exe')
    result={}
    for name,r in {**json.loads((work/'result.json').read_text()),**json.loads((sky/'result.json').read_text())}.items():
        root=sky if name=='sky_roof_v2' else work;source=root/name/'authoring/base/maps'/(name+'.bsp')
        truth=json.loads((root/'answers'/(name+'.json')).read_text());rows,(_,_,triangles)=floor_rows(source,policy)
        folder=output/name;folder.mkdir();geometry=folder/'geometry.jkobs'
        measured([inspector,source,folder/'geometry',0,0,192,384,256,128,1,geometry],folder/'geometry.log')
        measured([tool,'scan',geometry,folder/'sanitized'],folder/'sanitize.log')
        # Actual source transfer is not automatically linear just because these
        # are controlled gamma fixtures. Preserve raw and keep unavailable types.
        if truth['gamma']!=1.:
            for row in rows:row.update(source=None,scene=None,transfer=0,primaries=0)
        prefix=folder/'samples';x05.publish_rows(prefix,rows,truth['bsp_sha256'],folder/'sanitized.public.jkobs')
        split=policy['split'];views=folder/'views';report=prepare(prefix,views,split['block_world'],split['gap_world'],split['seed'],split['origin_world'])
        measured([tool,'public-check',views/'inputs/geometry.jkobs','unused'],folder/'public-check.log');audit=audit_training(views/'inputs',truth['bsp_sha256'])
        held=[dict(row,groups=['all']) for row in rows if row['partition']=='heldout'];contract=[dict(id=name,kind='PositiveLight',group='all')]
        if truth['gamma']==1.:
            controls={label:evaluate(held,predictions(held,values),contract,policy) for label,values in (
                ('black',[[0.]*3]*len(held)),('known_direct',forward(held,triangles,truth)),
                ('wrong_attenuation',forward(held,triangles,truth,'linear')),('all_visible',forward(held,triangles,truth,all_visible=True)))}
            controls['low_coverage']=evaluate(held,predictions(held[:1],[held[0]['raw']]),contract,policy)
            if not truth['sources']:
                controls['wrong_attenuation']=dict(status='NotApplicable',success=False,
                    reason='Sky-only operator has no local point attenuation parameter; equality is not profile discrimination')
        else:controls=dict(status='NotComparableToGamma1Gate',reason='Different frozen output profile; correctly declared gamma2.2 byte metrics remain in controlled result, no threshold relabel')
        sources=[dict(kind='point',position=s['origin'],photometry=dict(value=s['intensity'],unit='relative',domain='x06-compiler-relative',kind='UncalibratedRelative')) for s in truth['sources']]
        false=sources+[dict(kind='point',position=[10000+i*128,10000,10000]) for i in range(5)]
        policy_match=dict(position_world=8.,angle_degrees=5.,cone_degrees=3.,dimension_relative=.1,roll_degrees=5.)
        det={label:detection(sources,pred,policy_match,32.) for label,pred in [('oracle_source_copy',list(reversed(sources))),('false_extra_same_rgb',false),('indirect_only_same_rgb',[])]}
        result[name]=dict(map_sha256=truth['bsp_sha256'],blind=audit,partitions=report['partitions'],all_pages_complete=report['all_pages_complete'],
            scope='Independent mapped floor subset, complete raw BSP remains PRIVATE; not full-map derivation',controls=controls,metric_detection=det,
            interpretation='Metric sanity: all variants can be assigned identical oracle RGB, but source counts differ; no sources fitted',
            legitimate_domain='Gamma/output hypothesis explicitly conditional',unknown_scale_absolute_error=None,
            source_count_nonunique=name in ('single_coincident','split_coincident'),calibrated_existence_probability=None,
            gi_direct_error_is_not_bounce=truth['bounce']!=0)
        write(output/'result.json',result)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--sky',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.work,a.sky,a.output)
