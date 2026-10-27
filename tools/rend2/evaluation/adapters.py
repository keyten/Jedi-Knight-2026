"""Trusted evaluator adapters for existing JA fixtures and X02 streams.

No full .jkobs parser, colour decoder, visibility engine or forward solver is
duplicated. Controller-only functions can open answers/held-out data.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import csv
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent/'datasets'))
from prepare_blind import rows as source_rows, quality
from solver_view import TrainingView, digest
from evaluation_view import EvaluationView
import metrics as m


def load(path, limit=32*1024*1024):
    path = Path(path)
    if path.stat().st_size > limit: raise ValueError('JSON byte budget exceeded')
    return json.loads(path.read_text(encoding='utf-8'),
        parse_constant=lambda v: (_ for _ in ()).throw(ValueError('Nonfinite JSON '+v)))


def policy(position_world=8.):
    # A demonstration policy for the existing 16-unit compiler luxels only.
    # Real benchmarks must freeze scene/fixture-specific gates, not adopt this default.
    return dict(position_world=position_world, angle_degrees=5., cone_degrees=3.,
                dimension_relative=.1, roll_degrees=5.)


def analytical_answer(path):
    a = load(path)
    if a['schema'] != 'org.openjk.synthetic-lighting-answer' or a['version'] != 1:
        raise ValueError('Unsupported analytical answer')
    domain = 'self-authored-cpu-reference-v1'; sources = []
    for i, s in enumerate(a['lights']):
        kind = {'panel': 'rectangle'}.get(s['type'], s['type'])
        # The reference fixture multiplies intensity by RGB. Canonical intensity is Y.
        y = sum(c*x for c, x in zip((.2126, .7152, .0722), s['colour']))
        source = dict(id='truth-only-'+str(i), kind=kind, position=s['position'],
            photometry=dict(unit='nits' if s['unit'] == 'luminance_cd_per_m2' else 'candela',
                kind='PhysicallyAuthored', value=s['intensity']*y, rgb=s['colour'],
                primaries='Rec709Linear', domain=domain))
        if kind == 'spot': source.update(direction=s['axis_propagation'], direction_convention='propagation',
                                         cone_degrees=s['cone_degrees'])
        # Panel/tube fixture basis comes from offline_fixtures.cpp, not the answer's
        # generic axis_propagation field (which is not the tube's longitudinal axis).
        if kind == 'rectangle': source.update(dimensions=[2*s['half_width'], 2*s['half_height']],
            direction=[0, 0, -1], direction_convention='propagation', tangent=[1, 0, 0])
        if kind == 'tube': source.update(dimensions=[2*s['half_length'], s['radius']],
            direction=[1, 0, 0], direction_convention='axis')
        sources.append(source)
    if a['sun']['present']:
        sources.append(dict(kind='sun', direction=a['sun']['direction_to_source'], direction_convention='receiver_to_source',
            photometry=dict(unit='lux', kind='PhysicallyAuthored', value=a['sun']['illuminance_lux']*(.2126+.7152*.98+.0722*.9),
                            domain=domain, primaries='Rec709Linear', rgb=[1, .98, .9])))
    if a['sky']: sources.append(dict(kind='sky', component='environment'))
    return dict(scene=a['scene'], units_per_metre=a['units_per_metre'], sources=sources,
                answer_sha256=digest(path), backend=a['backend'])


def compiler_answer(path):
    recipe = load(path); a = recipe['ground_truth']
    if a['schema'] != 'org.openjk.q3map-ground-truth': raise ValueError('Not a compiler answer')
    sources = []
    for s in a['lights']:
        source = dict(kind=s['type'], position=s['position_world'], photometry=dict(
            unit=s['intensity_unit'], kind='UncalibratedRelative', value=s['authored_intensity'],
            rgb=s['rgb_parameter'], primaries='Rec709Linear', domain='q3map2-authored-parameters'))
        if s['type'] == 'spot': source.update(direction=s['axis_propagation'], direction_convention='propagation',
                                             cone_degrees=s['effective_half_angle_degrees'])
        sources.append(source)
    # Surface emitters are six-face prisms, not the analytical panel or tube answer.
    # Do not invent a single physical tube/rectangle or cd/nits truth from them.
    return dict(scene=a['scene'], sources=sources, units_per_metre=32.,
                surface_emitter=a['surface_emitter'], sun=a['sun'], sky=a['sky'],
                absolute_scale='Unknown', answer_sha256=digest(path),
                compiler_sha256=recipe['binary_sha256'], bsp_sha256=recipe['compiled_bsp_sha256'],
                geometry_scoring_limit='Point/spot source adapter only; prism/sun/sky recipe preserved, not coerced')


def quality_row(r, map_hash):
    flags = set(quality(r)); cov = r['coverage']
    return dict(id=map_hash+'/'+r['id'], area=r['area'] if r['area'] > 0 else None,
        position=r['position'], normal=r['normal'], surface=r['surface'],
        surface_type=r['kind'], material_style=str(r['material'] or 'Unknown')+'/'+str(r['style']),
        region=str(r['positive_region'])+':'+str(r['negative_region']),
        roi=','.join(str(int(x//256)) for x in r['position']) if r['position'] else 'Unmapped',
        page=r['page'], height=int(r['position'][2]//256) if r['position'] else 'Unmapped',
        brightness=int(max(r['raw']))//32 if r['raw'] else 'Unknown',
        quality=dict(raw_rgb=r['raw'] is not None, mapped=cov == 2,
            normal=r['normal'] is not None and cov == 2,
            footprint=r['area'] > 0 and cov == 2 and not (r['issues'] & 128),
            comparable_colour='source_rgb_comparable_conditional' in flags,
            material_style=False if 'material_style_inconsistent' in flags else
                None if 'material_uncertain' in flags else True,
            # A locally clear receiver box is not certification of any source ray.
            visibility=None, region=bool(r['positive_region'] or r['negative_region']),
            grid_direction=None, deluxe=r['deluxe'] is not None,
            censored='censored' in flags, unsupported=cov == 4, ambiguous=cov == 3))


def dataset_readiness(prefix):
    prefix = Path(prefix); source = load(prefix.with_suffix('.source.json'))
    path = prefix.with_suffix('.rows.jsonl')
    if not path.exists(): path = Path(str(path)+'.gz')
    report = m.readiness(quality_row(r, source['map_sha256']) for r in source_rows(path))
    report['source'] = source
    report['source_rows_sha256'] = digest(path)
    report['all_pages_declared_complete'] = bool(source['derived_complete'])
    report['derived_record_count_consistent'] = report['candidate_count'] == source['rows']
    report['complete_derived_stream'] = bool(source['derived_complete'] and report['derived_record_count_consistent'] and
        not report['duplicate_records'])
    report['grid'] = dict(logical_cells=source['grid_cells'], original_records_present=source['grid_present'],
        directions_quantized_uncalibrated=source['grid_directions_quantized_uncalibrated'],
        assigned_cells=source['grid_assigned'],
        caveat='Logical index count is not independent information or free-space certification')
    return report


def encoded_target(row):
    raw = row['raw']
    if len(raw) != 3 or any(not 0 <= m.finite(x) <= 255 for x in raw): raise ValueError('RGB8 range')
    return dict(id=row['id'], weight=row['area'], domain='EncodedRGB8', primaries='EncodedSourceRGB',
        comparison_id='original-rgb8-byte-domain-v1',
        lower=[max(0., x-.5) for x in raw], upper=[None if x == 255 else min(255., x+.5) for x in raw],
        region=row.get('region', 'Unknown'), lighting='compiler-mixture', brightness=int(max(raw))//32)


def audit_training(directory, map_hash):
    """Strict artifact boundary in addition to existing stream/role/hash checks.
    Not an OS sandbox: mount only this directory for an untrusted executable.
    """
    root = Path(directory)
    allowed = {'manifest.json', 'train.jsonl', 'grid.jsonl', 'geometry.jkobs'}
    if root.is_symlink() or set(p.name for p in root.iterdir()) != allowed:
        raise ValueError('Unexpected solver input artifact (answers/targets/recipes prohibited)')
    manifest = load(root/'manifest.json', 1024*1024)
    permitted = {'schema', 'role', 'map_sha256', 'counts', 'block_world', 'gap_world', 'seed',
        'footprint_filter_multiplier', 'image_alternative_policy', 'all_pages_complete', 'absolute_scale',
        'grid_counts', 'sha256'}
    if set(manifest)-permitted: raise ValueError('Unexpected solver metadata (private compiler/answers prohibited)')
    view = TrainingView(root, map_hash)
    observations = sum(1 for _ in view.observations()); grid = sum(1 for _ in view.grid_observations())
    return dict(status='Available', train=observations, grid=grid, geometry_sha256=digest(view.geometry_path),
        partition_hashes=manifest['sha256'],
        process_isolation='API/artifact isolation checked. OS filesystem isolation is required for hostile solvers; not supplied here')


def compiler_baselines(views):
    root = Path(views); manifest = load(root/'inputs/manifest.json'); sha = manifest['map_sha256']
    audit = audit_training(root/'inputs', sha)
    # Evaluator-only, after frozen fixture predictions. No evaluator object in TrainingView.
    private = EvaluationView(root/'evaluator', sha, permitted=('heldout',))
    targets = [encoded_target(r) for r in private.observations('heldout')]
    zero = [dict(id=t['id'], rgb=[0., 0., 0.], domain=t['domain'], primaries=t['primaries'],
                 comparison_id=t['comparison_id']) for t in targets]
    oracle = [dict(id=t['id'], rgb=[(a if b is None else (a+b)/2) for a, b in zip(t['lower'], t['upper'])],
                   domain=t['domain'], primaries=t['primaries'], comparison_id=t['comparison_id']) for t in targets]
    return dict(map_sha256=sha, audit=audit, heldout_observations=len(targets),
        heldout_sha256=digest(root/'evaluator/heldout.jsonl'),
        zero=m.prediction_metrics(targets, zero),
        target_copy_sanity=m.prediction_metrics(targets, oracle),
        physical_oracle=m.metric(reason='No new full forward evaluation performed'),
        original_grid_approximation=m.metric(reason='No profile-correct grid-to-surface evaluator available'),
        note='Encoded-domain sanity/zero baselines, NOT emitter fitting, compiler replay or source-linear accuracy')


def boundary_diagnostic(path):
    with Path(path).open(encoding='utf-8', newline='') as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) > 10000: raise ValueError('Boundary diagnostic limit')
    truth, old, replay = [], [], []
    residual = []; affected = 0
    for r in rows:
        rgb = json.loads(r['compiler_rgb8']); a = json.loads(r['old_analytic_rgb8']); b = json.loads(r['epsilon_replay_rgb8'])
        truth.append(encoded_target(dict(id=r['id'], area=float(r['footprint_area']), raw=rgb)))
        old.append(dict(id=r['id'], rgb=a, domain='EncodedRGB8', primaries='EncodedSourceRGB', comparison_id='original-rgb8-byte-domain-v1'))
        replay.append(dict(id=r['id'], rgb=b, domain='EncodedRGB8', primaries='EncodedSourceRGB', comparison_id='original-rgb8-byte-domain-v1'))
        residual.extend(x-y for x, y in zip(rgb, a)); affected += rgb != a
    return dict(source_sha256=digest(path), boundary_texels=len(rows), channels=len(rows)*3,
        nonzero_residual_texels=affected, max_byte_error=max(map(abs, residual)),
        signed_byte_residual_sum=sum(residual), old_analytic=m.prediction_metrics(truth, old),
        source_epsilon_replay=m.prediction_metrics(truth, replay),
        claim='Dedicated original X04 boundary witness, not a universal epsilon/boundary model')


def prior_profile_diagnostics(path):
    """Re-score the existing X03 witness; no new numerical/profile fitting is claimed."""
    witness = load(path); trials = []
    for c in witness['comparisons']:
        budgets = sorted(set(r['parameter_count'] for r in c['results']))
        for budget in budgets:
            records = [dict(id=r['profile'], validation_error=r['validation_rmse'],
                heldout_error=r['heldout_rmse'], fitted_nuisance=r['parameters'], domain='X03/'+c['domain'],
                parameter_budget=budget, partition_hash=c['case']+'/'+c['protocol'],
                verified_features_scope='Existing X03 witness; not general compiler verification')
                for r in c['results'] if r['parameter_count'] == budget]
            if len({r['id'] for r in records}) != len(records):
                # Multiple geometry/encoding configurations must not silently get
                # independent votes or be compared as if they used one budget.
                continue
            if len(records) < 2: continue
            selected = min(records, key=lambda r: r['validation_error'])['id']
            trials.append(dict(case=c['case'], protocol=c['protocol'], budget=budget,
                sensitivity={str(regret): m.profile_evidence(records, regret, c.get('truth'), selected)
                             for regret in (0., .5, 2.)}))
    return dict(source_sha256=digest(path), trials=trials,
        stock={name: dict(historical_profile='UnknownProfile', physical_source_accuracy=m.metric(status='NotComparable',
                   reason='No original stock source truth'), prior_trial_statuses=value.get('trial_statuses'))
               for name, value in witness['stock'].items()},
        claim='Previously executed X03 controls; regret sensitivity is not probability or new compiler execution')
