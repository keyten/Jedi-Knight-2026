#!/usr/bin/env python3
"""Versioned aggregate X06 evidence; never copy licensed pixels/geometry to Git.
GPL-2.0-or-later.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'datasets'))
from census import write


def load(p):return json.loads(Path(p).read_text())
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def compact(private,controlled,evaluation,output,fields=None,census_measurement=None,compiler_measurement=None,invalid_sky=None):
    output.mkdir(parents=True,exist_ok=False);c=load(private/'census.json');maps={}
    captures={tag:load(private/('captures'+tag+'.json')) for tag in ('','-prefix','-small-roi') if (private/('captures'+tag+'.json')).exists()}
    for name,r in c.items():
        slug=name.replace('/','_')
        if r['status']!='Complete':
            maps[name]=dict(status=r['status'],reason=r.get('reason'),raw_census=load(private/'ctf1-failure/raw-census.json') if name=='mp/ctf1' else None)
            continue
        prefix=Path(r['prefix']);coverage=load(Path(r['views'])/'coverage.json');facts=load(prefix.with_suffix('.facts.json'))
        positive={}
        for role,directory in [('train','inputs'),('validation','evaluator'),('heldout','evaluator')]:
            # Controller-only census AFTER source selection was frozen and trials
            # completed; these colours never enter stock trial selection/fitting.
            count=pos=0;energy=area=0.
            for line in (Path(r['views'])/directory/(role+'.jsonl')).read_text().splitlines():
                row=json.loads(line);count+=1;pos+=max(row['raw'])>=8;area+=row['area'];energy+=row['area']*sum(v*v for v in row['raw'])/3
            positive[role]=dict(count=count,above8_code=pos,black_raw_code_rmse=(energy/area)**.5 if area else None,
                domain='Raw encoded code diagnostic; unknown transfer, not physical irradiance or a profile fit')
        maps[name]=dict(status='Complete',map_sha256=r['map_sha256'],source=r['source'],counts=r['counts'],
            readiness=r['readiness'],area_quality=r['area_quality'],partitions=r['partitions'],grid_partitions=r['grid_partitions'],
            kind_counts=facts['kind_counts'],motion_counts=facts['motion_counts'],material_signals=facts['material_signal_counts'],
            nonzero_styles={str(i):n for i,n in enumerate(facts['source_style_counts']) if n},
            source_hint_count=len(facts['fixture_geometry_priors']),positive_signal=positive,stages=r['stages'],
            census_seconds=r['seconds'],derived_disk_bytes=r['disk_bytes'],capture=r['capture_attempt'],
            public_manifest_sha256=sha(Path(r['views'])/'inputs/manifest.json'),
            source_stream_sha256=sha(prefix.with_suffix('.rows.jsonl')),
            distribution={key:dict(counts=value['counts'],surfaces=value['distinct_receiver_surfaces'],normals=value['normal_orientation_counts'],
                orientation_span_proxy=value['orientation_span_proxy']) for key,value in coverage['groups'].items() if key.startswith(('page/','surface_type/','style/','normal/','height256/'))},
            geographic_groups=len([k for k in coverage['groups'] if k.startswith('spatial256/')]),
            calibration='UnknownProfile; source transfer/primaries/absolute scale remain unknown',native_mp_materials_verified=False if name.startswith('mp/') else None)
        # Existing standard-library visualizer: aggregate counts only.
        (output/(slug+'-coverage.svg')).write_bytes((prefix.parent/'coverage.svg').read_bytes())
    stock=load(private/'stock-trials/result.json');trials={}
    for name,r in stock.items():
        trials[name]={k:v for k,v in r.items() if k!='trials'}
        trials[name]['trials']=[dict(centre=t.get('centre'),fold_counts=t.get('fold_counts'),ray_states=t.get('ray_states'),
            status=t['status'],candidate_count=len(t.get('candidates',[])),admitted_pairs=t.get('admitted_pairs'),
            best_min_common_visible=max((min(x['visible'].values()) for x in t.get('pair_support',[])),default=0),
            maximum_training_positive_common_visible=max((x['training_positive'] for x in t.get('pair_support',[])),default=0),
            fits=t.get('fits'),measurement=t.get('query_measurement'),
            reason='No nested receiver samples near these fixture priors' if sum(t.get('fold_counts',{}).values())==0 else None) for t in r.get('trials',[])]
    regression={}
    old=load(ROOT/'tools/rend2/lightrecon_baseline/observations-x02-validation.json')
    for name in ('hoth3','hoth2','mp_ffa3'):
        source=load(ROOT/'build/x02/final'/(name+'.source.json'));coverage=load(ROOT/'build/x02/final'/(name+'-views/coverage.json'))
        regression[name]=dict(map_sha256=source['map_sha256'],source=source,counts=coverage['groups']['map'],
            baseline_witness_sha256=sha(ROOT/'tools/rend2/lightrecon_baseline/observations-x02-validation.json'),
            repeated_capture=False,scope='Frozen original all-page regression capture; new independent geometry ROI below')
    result=dict(schema='org.openjk.x06-cross-map.v1',baseline_sha256=sha(HERE/'baseline.json'),policy_sha256=sha(HERE/'policy.json'),
        maps=maps,regression=regression,capture_attempts=captures,stock_direct_trials=trials,
        geometry=load(private/'stress/result.json'),regression_geometry=load(private/'regression/result.json'),
        controlled=load(controlled/'result.json'),controlled_pairs=load(controlled/'pairs.json'),
        invalid_sky_attempt=load(invalid_sky or ROOT/'build/x06-controlled/result.json').get('sky_offset'),
        field_checks=load(fields or ROOT/'build/x06-fields.json'),evaluation=load(evaluation/'result.json'),
        census_measurement=load(census_measurement or ROOT/'build/x06-census-measurement.json'),
        controlled_measurement=load(compiler_measurement or ROOT/'build/x06-compiler-measurement.json'),
        source_hashes={p.name:sha(p) for p in sorted(HERE.glob('*.py'))},
        no_production_solver=True,no_renderer_changes=True,stock_physical_source_truth=False,
        artifacts_private=str(private),limitations=['One stock exporter failure remains (three attempts); no all-map success claim',
            'MP BSPs loaded through SP; native MP material semantics not certified',
            'Per-ray geometry and source hypotheses remain conditional; no historical stock profile identified',
            'Optics, large-scale GI/SH field generation and full production fitting remain unimplemented'])
    write(output/'results.json',result)
    lines=['# X06 aggregate witness','', 'No inverse solver was run. All licensed payloads remain in the recorded private temporary directory.','',
        '| Map | Raw colour pages | Mapped / raw texels | Ambiguous | Unsupported | Censored samples | Train / validation / held-out |',
        '|---|---:|---:|---:|---:|---:|---|']
    for name,r in maps.items():
        if r['status']!='Complete':lines.append(f"| {name} | unavailable | {r['status']}: Section chunk limit | — | — | — | — |");continue
        q=r['counts']['counts'];s=r['source'];lines.append(f"| {name} | {len(s['pages'])} | {q['mapped']:,} / {q['denominator_candidate_texels']:,} | {q.get('ambiguous_overlap',0):,} | {q.get('unsupported_geometry',0):,} | {q.get('censored',0):,} | {' / '.join(str(r['partitions'][p]) for p in ('train','validation','heldout'))} |")
    lines+=['','Counts of ambiguous/unsupported/uncovered are separate. The known-footprint area denominator is not total world receiver area. See JSON for area-based predicates, normals, styles, per-page/height distributions, stages and hashes.',
        '', '## Direct-light attempt', '', '| Map | Outcome | ROIs | Rays | Visible / blocked / unknown |', '|---|---|---:|---:|---|']
    for name,r in trials.items():
        counter=Counter()
        for t in r['trials']:counter.update(t.get('ray_states') or {})
        lines.append(f"| {name} | {r['status']} | {r.get('attempted_rois',0)} | {r.get('ray_queries',0)} | {counter['visible']} / {counter['blocked']} / {counter['unknown']} |")
    lines+=['','No new stock region was admitted by the frozen common-visible/multi-normal/spatial-fold rules. This is a bounded candidate-selection result, not a conclusion that stock direct lights cannot be identified.',
        '', '## Independent geometry ROIs', '', '| Map / ROI | Regions | Free / solid / unknown cells | Rays visible / blocked / unknown | Opaque reference hits / false clear | Peak process bytes |',
        '|---|---:|---|---|---|---:|']
    for name,r in result['geometry'].items():
        for x in r.get('rois',[]):
            v=x.get('report',{})
            if not v:lines.append(f"| {name} / {x['selection']['name']} | unavailable | failed | — | — | — |");continue
            lines.append(f"| {name} / {x['selection']['name']} | {v['regions']} | {v['free']} / {v['solid']} / {v['unknown']} | {v['ray_visible']} / {v['ray_blocked']} / {v['ray_unknown']} | {v['reference_opaque_face_hits']} / {v['reference_false_clear']} | {x['measurement']['peak_working_set_bytes']} |")
    lines+=['','Adaptive cell counts have unequal volumes; exact clipped Free/Solid/Unknown volumes and contact errors are in JSON. Unknown is not transparency.',
        '', '## Controlled new bakes', '', '| Case | Known direct RMSE | Zero RMSE | Fitted isq held-out | Fitted linear held-out | Outcome |', '|---|---:|---:|---:|---:|---|']
    for name,r in result['controlled'].items():lines.append(f"| {name} | {r['reference']['rgb_interval_rmse']['value']:.6f} | {r['black']['rgb_interval_rmse']['value']:.6f} | {r['fitted_profiles']['isq']['heldout']['value']:.6f} | {r['fitted_profiles']['linear']['heldout']['value']:.6f} | {r['profile_outcome']} |")
    lines+=['','Encoded intervals and shared-peak operators are profile-specific. Bounce rows deliberately evaluate a direct-only model and do not certify an indirect solver. Co-located split answers produce identical original lightmaps. Fully white saturation preserves competing attenuation hypotheses.',
        '', 'Exact reproduction, failures, decision changes and remaining prerequisites: [technical report](../../../../docs/rend2-inverse-lighting-cross-map-validation.md).']
    (output/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--private',type=Path,required=True);p.add_argument('--controlled',type=Path,required=True)
    p.add_argument('--evaluation',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--fields',type=Path);p.add_argument('--census-measurement',type=Path);p.add_argument('--compiler-measurement',type=Path);p.add_argument('--invalid-sky',type=Path)
    a=p.parse_args();compact(a.private,a.controlled,a.evaluation,a.output,a.fields,a.census_measurement,a.compiler_measurement,a.invalid_sky)
