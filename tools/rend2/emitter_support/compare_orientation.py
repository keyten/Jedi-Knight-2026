#!/usr/bin/env python3
"""Compare the same archived candidate cohort before/after source-side correction.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.rend2.all_stock.run import sha,write


def rows(folder):
    return {r['source']:r for r in map(json.loads,(folder/'diagnosis.jsonl').read_text().splitlines())}


def occupied(row):
    keys=('kind','vertices','centre','axis','half_length','radius')
    return [{k:s[k] for k in keys} for s in row['supports']]


def compare(before,after):
    old=json.loads((before/'stock.json').read_text())
    new=json.loads((after/'stock.json').read_text())
    protocol=json.loads((before/'protocol.json').read_text())
    unchanged=[]
    for name in ('regions_support.cpp','point_refine.cpp','visibility.cpp'):
        path='shared/rd-rend2/lightrecon/'+name
        if sha(ROOT/path)!=protocol['sources'][path]:
            raise ValueError('Independent admission/ray predicate changed: '+path)
        unchanged.append(path)
    path='shared/rd-rend2/lightrecon/emitter_support.cpp'
    original=subprocess.check_output(['git','show',protocol['commit']+':'+path],cwd=ROOT,text=True)
    current=(ROOT/path).read_text()
    def check_body(text):return text.split('Error Check(',1)[1].split('Error FromSurfaces(',1)[0]
    if check_body(original)!=check_body(current):
        raise ValueError('SourceSupport::Check changed')
    result=dict(schema='org.openjk.source-orientation-comparison.v1',
                before_protocol_sha256=sha(before/'protocol.json'),after_protocol_sha256=sha(after/'protocol.json'),
                unchanged_predicates=unchanged+['SourceSupport::Check'],maps={},
                original_archives_unchanged=True,canonical_storage_changed=False,source_truth='Unavailable')
    for name,record in old.items():
        prior=Path(record['resources']['command'][2])
        fixed=Path(new[name]['resources']['command'][2])
        if record['input']!=new[name]['input']:
            raise ValueError('Archived input identity changed')
        a,b=rows(prior),rows(fixed)
        if a.keys()!=b.keys():
            raise ValueError('Candidate cohort changed')
        transitions=Counter()
        accepted=[]
        geometry_matches=0
        for key in sorted(a,key=int):
            x,y=a[key],b[key]
            if x['fixture']!=y['fixture']:
                raise ValueError('Fixture identity changed')
            if y['contract']!=10:
                if x['position']!=y['position'] or x['surfaces']!=y['surfaces'] or occupied(x)!=occupied(y):
                    raise ValueError('Occupied source geometry moved or changed')
                geometry_matches+=1
            transitions[x['state']+' -> '+y['state']]+=1
            if y['state'] in ('Free','Clear','Conditional'):
                accepted.append(key)
        details=json.loads((fixed/'classification.json').read_text())
        if len(accepted)!=new[name]['current_admission']['accepted']:
            raise ValueError('Admission count and state names disagree')
        if any('bsp-penetration-witness' in d['facets'] for d in details if d['source'] in accepted):
            raise ValueError('Independent exact penetration witness on an admitted source')
        detail_tsv=list(csv.DictReader((fixed/'mounting-witnesses.tsv').open(),delimiter='\t'))
        for r in detail_tsv:
            if r['source'] in accepted and r['new'] not in ('Free','Clear','Conditional'):
                raise ValueError('Diagnostic/admission state mismatch')
        result['maps'][name]=dict(tested=len(a),full_precision_occupied_geometry_matches=geometry_matches,
                    same_candidate_and_fixture_ids=True,transitions=dict(sorted(transitions.items())),
                    accepted=len(accepted),accepted_source_ids=accepted,
                    accepted_with_independent_penetration_witness=0,
                    source_normal_agrees_with_all_captured_normals=sum(d['winding_vertex_normal_dot_range'] is not None and
                        d['winding_vertex_normal_dot_range'][0]>.999 for d in details),
                    before_states=record['states'],after_states=new[name]['states'])
    result['classified']=sum(v['tested'] for v in result['maps'].values())
    result['accepted']=sum(v['accepted'] for v in result['maps'].values())
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--before',type=Path,required=True)
    p.add_argument('--after',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    write(a.out,compare(a.before,a.after))
