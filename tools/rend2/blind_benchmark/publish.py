"""Publish compact owned benchmark receipts. No licensed stock data or assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import shutil
from .solver import load, write, sha
from .evaluate import check_frozen

ROOT=Path(__file__).resolve().parents[3]


def publish(work, first, second, report, audit, output, native, initial_work=None, initial_report=None, initial_repeat=None):
    output.mkdir(parents=True,exist_ok=False)
    protocol=load(work/'protocol.json')
    check_frozen(work,protocol,ROOT,native)
    result=load(report/'results.json')
    integrity=load(audit/'audit.json')
    if result['protocol_sha256'] != sha(work/'protocol.json') or integrity['status'] != 'Pass':
        raise ValueError('Mismatched/failed benchmark receipt')
    for source,target in ((work/'protocol.json','protocol.json'),(report/'results.json','results.json'),
        (report/'validation-selections.json','validation-selections.json'),(audit/'repeat.json','repeat.json'),
        (audit/'audit.json','integrity.json'),(first/'resources.json','resources-a.json'),
        (second/'resources.json','resources-b.json')):
        shutil.copyfile(source,output/target)
    policy=protocol['policy']
    lines=['# ASR23 — measured source discovery', '',
        '16 owned compiled scenes; 9 common-budget configurations; 21 local source roots. ',
        'RGB8 errors are conditional encoded appearance, not calibrated irradiance. ',
        'The baseline uses unchanged strict JA Hidden proposals with the same benchmark fitter.', '',
        '| Method | Proposal recall | Proposal precision | Selected recall | Selected precision | False sources | Held-out / black MSE | Blind multisource successes |',
        '|---|---:|---:|---:|---:|---:|---:|---:|']
    for name in policy['methods']:
        r=result['aggregate'][name]
        def fmt(v): return 'Unavailable' if v is None else f'{v:.3f}'
        lines.append(f"| {name} | {fmt(r['proposal_recall'])} | {fmt(r['proposal_precision'])} | {fmt(r['recall'])} | {fmt(r['precision'])} | {r['false_positives']} | {fmt(r['mean_heldout_black_fraction'])} | {r['blind_multisource_successes']} |")
    lines += ['', 'The all-cues improvement is real for these frozen scenes, but its full acceptance gate fails: ',
              'five false local sources remain, and its two-color model includes a duplicate source. ',
              'Grid-only and adaptive-off pass the complete two-color blind multisource gate.', '',
              '| Scene | All-cues matched / true | Local + solar sources | Held-out / black MSE | Source positions and types |',
              '|---|---:|---:|---:|---|']
    for s in result['scenes']:
        r=s['methods']['all_cues']; sel=r['selected']; e=r['heldout']['black_fraction']
        positions='; '.join(x['type']+' ('+', '.join(f'{v:.1f}' for v in x['position'])+')' for x in r['sources'] if x['type'].lower() != 'sun') or 'None'
        lines.append(f"| {s['name']} | {sel['matched']} / {sel['truth_count']} | {r['local_source_count']} + {r['solar_source_count']} | {e:.5f}"+f" | {positions} |" if e is not None else
                     f"| {s['name']} | {sel['matched']} / {sel['truth_count']} | {r['local_source_count']} + {r['solar_source_count']} | Unavailable | {positions} |")
    lines += ['', '| Numerical gate | Status |', '|---|---|']+[f"| {name} | {value['status']} |" for name,value in result['gates'].items()]
    lines += ['', 'Proposal/selected precision uses maximum-cardinality one-to-one matching at 32 WU. ',
        'Types are scored separately. A blind multisource success additionally requires every type ',
        'correct, every true root matched, zero false sources, held-out/black MSE <= 0.10 and zero ',
        'geometry violations. Unknown/zero-energy scores and missing stock truth stay Unavailable.', '',
        '| Method | Model fits | Metered RGB row-channel trials | Fit seconds |',
        '|---|---:|---:|---:|']
    for name in policy['methods']:
        r=result['aggregate'][name]
        lines.append(f"| {name} | {r['fit_count']} | {r['fit_work']} | {r['fit_seconds']:.2f} |")
    lines += ['', 'Proposal/native geometry work is shared across configurations and recorded per scene in results.json. ',
        'Totals must not be added to each configuration as though independent experiments had been timed. ',
        'The common limits are caps, not promises of equal spent work. No wall-clock early stopping is used.']
    (output/'tables.md').write_text('\n'.join(line.rstrip() for line in lines)+'\n',encoding='utf-8')
    # Small owned selected parameter/prediction receipts make model freeze
    # independently inspectable without checking in every training trial path.
    selected={}
    for scene in protocol['scenes']:
        name=scene['name']; models=load(first/name/'frozen-models.json')
        selected[name]={}
        for method,selection in load(report/'validation-selections.json')['selections'][name].items():
            if 'selected_path' in selection:
                state=models['methods'][method]['path'][selection['selected_path']]
                selected[name][method]={key:state[key] for key in ('sources','powers','background','training_mse','sweeps')}
    write(output/'selected-models.json',selected)
    if initial_work:
        if not initial_report or not initial_repeat:
            raise ValueError('Initial witness requires work, report and repeat')
        initial=output.parent/'initial'
        initial.mkdir(parents=True,exist_ok=False)
        for source,target in ((initial_work/'protocol.json','protocol.json'),
            (initial_report/'results.json','results.json'),(initial_report/'tables.md','tables.md'),
            (initial_repeat,'repeat.json')):
            shutil.copyfile(source,initial/target)
        # Preserve the implementation of the pre-correction experiment. Shared
        # historical core bytes are already pinned and unchanged in this checkout.
        old_protocol=load(initial/'protocol.json')
        for name,digest in old_protocol['sources'].items():
            if not name.startswith('tools/rend2/blind_benchmark/') and name != 'tools/lightrecon_test/lightrecon_blind_basis_tool.cpp':
                continue
            snapshot=initial_work/'code-snapshot'/name
            if sha(snapshot) != digest:
                raise ValueError('Initial source snapshot changed '+name)
            target=initial/'code'/name
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(snapshot,target)
        (initial/'README.md').write_text(
            '# Initial pre-correction experiment\n\n'
            'Retained failed witness, not the final ASR23 acceptance result. The source snapshot and '
            'protocol pin the first inference/evaluation before adapter corrections. Solar alternatives '
            'were deduplicated by position, dead zero-cosine solar terms were traced, and fresh compiler '
            'material facts were missing. Final seeds, penalties and numerical gates were not tuned. '
            'See docs/rend2-blind-benchmark-asr23.md for the limits of this post-evaluation correction.\n',encoding='utf-8')
        write(initial/'manifest.json',dict(artifacts={p.relative_to(initial).as_posix():sha(p) for p in sorted(initial.rglob('*')) if p.is_file()},
                local_frozen_inputs=str(initial_work),local_native_snapshot=str(initial_work/'bin-snapshot')))
    regression=ROOT/'build/asr23-native-regression.log'
    if regression.exists():
        shutil.copyfile(regression,output/'native-regression.log')
    write(output/'manifest.json',dict(schema='org.openjk.asr23-publication.v1',
        publish_source_sha256=sha(Path(__file__)),published_owned_receipts_only=True,licensed_payloads=False,
        historical_core_and_canonical_contracts_unchanged=True,runtime_changed=False,
        source_input_locations=str(work),detailed_models_location=str(first),repeat_location=str(second),
        artifacts={p.name:sha(p) for p in sorted(output.iterdir()) if p.is_file()},
        detailed_frozen_model_sha256={s['name']:sha(first/s['name']/'frozen-models.json') for s in protocol['scenes']}))
    print('Published',output,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__)
    for name in ('work','first','second','report','audit','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--native',type=Path,default=ROOT/'build/s06-gcc')
    for name in ('initial-work','initial-report','initial-repeat'):
        p.add_argument('--'+name,type=Path)
    a=p.parse_args()
    publish(a.work.resolve(),a.first.resolve(),a.second.resolve(),a.report.resolve(),a.audit.resolve(),a.output.resolve(),a.native.resolve(),
            a.initial_work.resolve() if a.initial_work else None,a.initial_report.resolve() if a.initial_report else None,
            a.initial_repeat.resolve() if a.initial_repeat else None)
