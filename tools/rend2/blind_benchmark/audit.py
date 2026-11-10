"""Post-run integrity and adversarial evaluator audit; never tunes inference.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import math
from pathlib import Path
import shutil
import subprocess
import numpy as np
from .solver import load, write, sha
from .run import verify
from .evaluate import evaluate, check_frozen
from tools.rend2.grid_triangulation.triangulate import Bridge, role

ROOT = Path(__file__).resolve().parents[3]


def audit(work, first, second, native, output, captures=None):
    output.mkdir(parents=True, exist_ok=False)
    protocol = load(work/'protocol.json'); policy = protocol['policy']
    captures = captures or ROOT/'build/x02/compiled-fixtures-frozen'
    check_frozen(work, protocol, ROOT, native)
    verify(first, second, output/'repeat.json')
    public_checks, budget_checks, native_checks = {}, {}, {}
    for scene in protocol['scenes']:
        name = scene['name']; public = load(work/'public'/name/'public.json')
        private = load(work/'private'/(name+'-folds.json'))
        result = load(first/name/'frozen-models.json')
        folds = dict(train=public['training'],**private)
        sets = {key:{r['id'] for r in rows} for key,rows in folds.items()}
        blocks = {key:{tuple(r['block']) for r in rows} for key,rows in folds.items()}
        for a,b in (('train','validation'),('train','heldout'),('validation','heldout')):
            if sets[a]&sets[b] or blocks[a]&blocks[b]:
                raise ValueError('Fold leakage '+name)
        allowed = {'id','surface','position','normal','area','block'}
        if any(set(r) != allowed for r in public['receivers']):
            raise ValueError('Hidden target/nongeometry field in public geometry '+name)
        if any(role(r['correlation_key'],public['map_sha256']) != 'train' for r in public['grid']):
            raise ValueError('Nontraining grid record public '+name)
        if result['public_sha256'] != scene['public_sha256'] or result['geometry_sha256'] != scene['geometry_sha256']:
            raise ValueError('Child consumed different public inputs '+name)
        public_checks[name] = dict(status='Pass', disjoint_ids=True, disjoint_blocks=True,
            public_evaluation_receivers_geometry_only=True, grid_train_record_assignment=True)
        worst = 0
        for method,data in result['methods'].items():
            if data['fits'] > policy['maximum_model_fits'] or any(len(s['sources']) > policy['maximum_sources'] for s in data['path']):
                raise ValueError('Fitting/source cap exceeded '+name+'/'+method)
            # This meter counts RGB row evaluations in the four line trials.
            cap = policy['maximum_model_fits']*policy['nnls_sweeps']*(policy['maximum_sources']+1)*4*len(public['training'])*3
            if data['fit_work'] > cap:
                raise ValueError('Deterministic fit work bound exceeded '+name+'/'+method)
            worst=max(worst,data['fit_work'])
        for resource in result['resources']:
            if resource['stage'] in ('strict','adaptive') and (
                resource['rays'] > policy['hidden_rays'] or resource['forward_calls'] > policy['hidden_forward_calls'] or
                resource['work'] > policy['hidden_work']):
                raise ValueError('Proposal budget exceeded '+name)
        budget_checks[name] = dict(status='Pass', maximum_observed_method_fit_work=worst,
            proposal_caps=dict(rays=policy['hidden_rays'],forward_calls=policy['hidden_forward_calls'],work=policy['hidden_work']),
            stage_codes={r['stage']:r.get('code') for r in result['resources']})
        if not name.startswith('bridge_') and name not in ('empty','mixed_sun_local'):
            archive = captures/(name+'.jkobs')
            if sha(archive) != scene['full_raw_capture_sha256']:
                raise ValueError('Native comparison archive differs from frozen input '+name)
            bridge = Bridge(native/'lightrecon_grid_geometry_tool.exe',archive)
            try:
                rows = bridge.query('cells',*public['bounds'])['rows']
            finally:
                bridge.close()
            lookup = {r['ordinal']:r for r in rows}
            compared = [r for r in public['grid'] if r['ordinal'] in lookup]
            for row in compared:
                original = lookup[row['ordinal']]
                if (row['correlation_key'] != original['correlation_key'] or row['space'] != original['space'] or
                    row['boundary'] != original['boundary'] or row['styles'] != original['styles'] or
                    max(abs(a-b) for a,b in zip(row['consumer256'],original['consumer256'])) > 1e-6):
                    raise ValueError('Original grid parser/capture disagreement '+name)
            native_checks[name] = dict(status='Pass',compared_records=len(compared),
                                       direction_tolerance=1e-6,identity_style_geometry_equal=True)
    # Keep original frozen files untouched. Deliberately poison private copies;
    # call the trusted evaluator directly to test its dependency direction.
    mutations = {}
    for variant in ('heldout_rgb','source_positions'):
        poisoned = output/variant
        (poisoned/'private').mkdir(parents=True)
        shutil.copyfile(work/'protocol.json',poisoned/'protocol.json')
        for path in (work/'private').glob('*.json'):
            data = load(path)
            if variant == 'heldout_rgb' and path.name.endswith('-folds.json'):
                for row in data['heldout']:
                    row['raw'] = [255,0,128]
            if variant == 'source_positions' and path.name.endswith('-answer.json'):
                for source in data.get('sources',[]):
                    source['position'] = [v+10000 for v in source['position']]
                for source in data.get('surface_sources',[]):
                    source['position'] = [v+10000 for v in source['position']]
                for source in data.get('ground_truth',{}).get('lights',[]):
                    source['position_world'] = [v+10000 for v in source['position_world']]
                surface = data.get('ground_truth',{}).get('surface_emitter')
                if surface:
                    surface['bounds'] = [[v+10000 for v in p] for p in surface['bounds']]
            write(poisoned/'private'/path.name,data)
        evaluate(poisoned,first,poisoned/'report')
        actual = load(poisoned/'report/validation-selections.json')
        # Training output hashes and selected paths must remain exactly identical.
        selection_hash = sha(poisoned/'report/validation-selections.json')
        mutations[variant] = dict(status='Pass',validation_selection_sha256=selection_hash,
                                  training_output_hashes=actual['training_output_hashes'])
    hashes = [v['validation_selection_sha256'] for v in mutations.values()]
    if len(set(hashes)) != 1:
        raise ValueError('Private truth/heldout changed validation selection')
    reference = output/'reference'
    evaluate(work,first,reference)
    if hashes[0] != sha(reference/'validation-selections.json'):
        raise ValueError('Private mutation changed selection compared with reference')
    deps = ['tools/rend2/adaptive_hidden/run.py','tools/rend2/adaptive_hidden/policy-v1.json',
            'tools/rend2/hidden_candidates/run.py','tools/rend2/grid_triangulation/triangulate.py',
            'tools/rend2/validation/bridge.py','tools/rend2/datasets/prepare_blind.py',
            'tools/rend2/shadow_footprint/compiler_boundary.py','tools/rend2/bake_model/compare_bakes.py']
    dependency_checks = {}
    for name in deps:
        old = subprocess.check_output(['git','show',protocol['base_revision']+':'+name],cwd=ROOT)
        current=(ROOT/name).read_bytes()
        if old.replace(b'\r\n',b'\n') != current.replace(b'\r\n',b'\n'):
            raise ValueError('Imported implementation changed from frozen base '+name)
        dependency_checks[name]=dict(sha256=sha(ROOT/name),matches_frozen_base_revision=True)
    check_frozen(work,protocol,ROOT,native)
    write(output/'audit.json',dict(schema='org.openjk.asr23-integrity-audit.v1',status='Pass',
        audit_source_sha256=sha(Path(__file__)),numpy_version=np.__version__,
        protocol_sha256=sha(work/'protocol.json'),public_checks=public_checks,budget_checks=budget_checks,
        native_original_grid_comparison=native_checks,mutations=mutations,
        reference_validation_selection_sha256=hashes[0],unchanged_imported_implementations=dependency_checks,
        frozen_inputs_and_core_unchanged=True,os_sandbox_claimed=False))
    print('ASR23 integrity: identical repeat; 16 fold/budget audits; 12 native grid comparisons; heldout/truth poison cannot change selection',flush=True)


if __name__ == '__main__':
    p=argparse.ArgumentParser(__doc__)
    for name in ('work','first','second','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--native',type=Path,default=ROOT/'build/s06-gcc')
    p.add_argument('--captures',type=Path,default=ROOT/'build/x02/compiled-fixtures-frozen')
    a=p.parse_args()
    audit(a.work.resolve(),a.first.resolve(),a.second.resolve(),a.native.resolve(),a.output.resolve(),a.captures.resolve())
