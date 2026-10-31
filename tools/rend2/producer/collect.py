#!/usr/bin/env python3
"""Versionable S13 aggregate witness. Never copies licensed pixels/geometry.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,x):p.write_text(json.dumps(x,indent=2,sort_keys=True,allow_nan=False)+'\n',encoding='utf-8')
def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--stock',type=Path,required=True);p.add_argument('--preview',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 for src,target in [('build/s13-compiled-release/compiled.json','compiled.json'),('build/s13-publication-suite-b/numerical.json','publication-suite.json'),('build/s13-producer-release.json','producer-tests.json'),('build/s13-disabled-final/report.json','disabled-path.json'),('build/s13-forward-verified/summary.json','forward-oracles.json')]:shutil.copyfile(ROOT/src,a.out/target)
 stock={}
 for name,v in load(a.stock/'results.json').items():
  if v.get('state')!='Attempted':stock[name]=dict(state=v['state'],reason=v.get('reason'));continue
  s=v['source'];attempts=[]
  for x in v['attempts']:
   r=x.get('result') or {};attempts.append(dict(selector=x['selector'],exit_code=x['exit_code'],validation_attempts=x['validation_attempts'],budget_exhausted=x['time_budget_exceeded'],producer=r.get('producer'),
       rejected_geometry_attempts=x['stderr'].count('Physically invalid source support/path/bounds')))
  stock[name]=dict(map_sha256=v['map_sha256'],geometry_sha256=v['geometry_sha256'],historical_profile='UnknownProfile',
   raw_colour_pages=len(s['pages']),raw_pixels=s['raw_pixels'],source_records=s['rows'],grid_indices=s['grid_cells'],visibility_scene_complete=bool(s['visibility_complete']),
   counts=v['coverage']['counts'],partitions=v['counts'],partition_sha256=v['partition_sha256'],legitimate_glow_hints=v['glow_hints'],supplied_hypotheses=len(v['candidate_ids']),attempts=attempts,source_xyz_error=None,absolute_photometry_error=None)
 write(a.out/'stock-summary.json',dict(schema='org.openjk.producer-stock-summary.v1',maps=stock,claim='Bounded failed/conditional attempts; no stock source truth or accepted exact emitters'))
 resource=dict(compiled=load(ROOT/'build/s13-compiled-release/resources.json'),publication_suite=load(ROOT/'build/s13-publication-suite-b/resources.json'),
   stock_controller=load(ROOT/'build/s13-stock-measurement.json'),evaluation_framework=load(ROOT/'build/s13-evaluation-measurement.json'))
 resource['stock_native']={k:[dict(selector=x['selector'],seconds=x['seconds'],peak_process_rss_bytes=x['peak_process_rss_bytes']) for x in v.get('attempts',[])] for k,v in load(a.stock/'results.json').items()}
 write(a.out/'resources.json',resource)
 preview=load(a.preview/'result.json');write(a.out/'visual.json',dict(schema='org.openjk.producer-visual.v1',alignment_verified=False,
   observations={name:{k:v for k,v in r.items() if k in ('exit_code','budget_exhausted','seconds','peak_process_rss_bytes','done','marker_import','markers','claim')} for name,r in preview.items()},
   comparison=load(a.preview/'comparison.json'),limitations=['Unfitted priors only','Occluded markers/unstable or obstructed camera','No successful recovered-stock-emitter alignment demonstration']))
 evaluation=load(ROOT/'build/s13-evaluation/metrics.json')
 write(a.out/'evaluation-summary.json',dict(schema='org.openjk.producer-evaluation-summary.v1',metric_examples=len(evaluation['cases']),analytical_answers=len(evaluation['existing_data']['analytical']),compiled_datasets=len(evaluation['existing_data']['compiled']),stock_censuses=len(evaluation['existing_data']['stock']),solver_executed_by_metric_runner=evaluation['solver_executed'],
   caveat='Metric/oracle/readiness assertions, not autonomous producer fits; actual fits are separate witnesses',source_metrics=load(ROOT/'build/s13-publication-suite-b/numerical.json')['source_metrics']))
 files=list((ROOT/'tools/rend2/producer').glob('*.py'))+[ROOT/f for f in ('shared/rd-rend2/lightrecon/producer.h','shared/rd-rend2/lightrecon/producer.cpp','shared/rd-rend2/lightrecon/joint.cpp','shared/rd-rend2/tr_lightrecon.cpp')]
 tests={}
 for f in ('s13-ctest-release.log','s13-ctest-affected.log','s13-renderers-preview-final.log','s13-python-evaluation.log','s13-python-blind.log','s13-python-positive.log','s13-python-controller.log'):
  path=ROOT/'build'/f;tests[f]=dict(sha256=sha(path),tail=path.read_text(errors='replace').splitlines()[-7:])
 write(a.out/'manifest.json',dict(schema='org.openjk.producer-witness.v1',base_commit='0aa1757cf',semantic_schema=4,binaries={name:dict(sha256=sha(ROOT/name),bytes=(ROOT/name).stat().st_size) for name in ('build/s06-gcc/lightrecon_joint_tool.exe','build/s06-gcc/lightrecon_producer_suite.exe','build/s13-renderers/Release/rdsp-rend2_x86_64.dll','build/s13-renderers/Release/rd-rend2_x86_64.dll')},source_sha256={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in files},tests=tests,
   acceptance=dict(pipeline='Conditional operational',stock_physical_reconstruction='Not demonstrated',production_cache='NotCacheable',runtime_consumers='Unavailable',visual_alignment='Not verified'),
   historical_failures=['Salted Joint/S12 training identity mismatch fixed','Source/metadata ID collision fixed','GI-link uncertainty incorrectly poisoned direct publication fixed','Frozen first-patch transport export fixed','Historical shader baseline drift remains','MSBuild sandbox write denials; permitted builds succeed','Inconclusive stock marker camera runs retained']))
if __name__=='__main__':main()
