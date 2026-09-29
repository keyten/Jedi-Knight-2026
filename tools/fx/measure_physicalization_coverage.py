#!/usr/bin/env python3
"""Measure asset coverage using the real SP parser and shared classifier.

No PlayEffect calls. Outputs/private baseline hardlinks stay in ignored build/.
Counts eligibility, ignoring master/opt lists; not play frequency or GPU visibility.
"""
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from audit_stock_fx import Corpus, effects_inventory

OWNED_TEST_FIXTURES = {'effects/test/physicalization_smoke.efx',
                      'effects/test/physicalization_explosion.efx',
                      'effects/test/physicalization_cluster.efx',
                      'effects/test/physicalization_sources_smoke.efx'}

def parse_report(log, requested):
    rows = defaultdict(list)
    for match in re.finditer(r'FXAUDIT\|([^|\r\n]+)\|((?:\d+\|){8}\d+)', log):
        rows[match[1]].append([int(v) for v in match[2].split('|')])
    completed = set(re.findall(r'FXAUDIT_END\|([^|\r\n]+)\|\d+', log))
    completed &= set(requested)
    rows = {p: rs for p, rs in rows.items() if p in completed}
    failed = sorted(set(requested)-completed)
    stages = {}
    for stage in ['stage1', 'composite', 'emission']:
        positive_effects = set()
        positive_primitives = 0
        positive_alternatives = 0
        for path, ps in rows.items():
            for ordinal, sprite, alternatives, exact, family, composite, adaptive, emission, authored in ps:
                n = exact+family + (composite if stage != 'stage1' else 0) + (emission if stage == 'emission' else 0)
                # Current emission rules and medium rules are disjoint.
                if n:
                    positive_effects.add(path)
                    positive_primitives += 1
                    positive_alternatives += n
        stages[stage] = {'effects': len(positive_effects), 'sprite_primitives': positive_primitives,
                         'shader_alternatives': positive_alternatives, 'effect_paths': sorted(positive_effects)}
    return {'requested_effects': len(requested), 'parsed_effects': len(completed),
            'failed_effect_paths': failed,
            'supported_primitives': sum(len(ps) for ps in rows.values()),
            'sprite_primitives': sum(p[1] for ps in rows.values() for p in ps),
            'shader_alternatives': sum(p[2] for ps in rows.values() for p in ps),
            'authored_media_primitives': sum(p[8] for ps in rows.values() for p in ps),
            'adaptive_alternatives': sum(p[6] for ps in rows.values() for p in ps),
            'stages': stages, 'rows': dict(rows)}


def run(root, installation, stock):
    home = root / 'home'
    game = home / 'OpenJK'
    game.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[2]
    exe_dir = root / 'bin'
    exe_dir.mkdir(exist_ok=True)
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/openjk_sp.x86_64.exe', exe_dir)
    shutil.copy2(repo/'build/msvc/RelWithDebInfo/rdsp-rend2_x86_64.dll', exe_dir)
    for dependency in installation.glob('SDL2*.dll'):
        shutil.copy2(dependency, exe_dir)
    shutil.copy2(repo/'build/msvc-all/RelWithDebInfo/jagamex86_64.dll', game)
    if stock:
        basepath = root / 'stock-baseline'
        base = basepath/'base'
        base.mkdir(parents=True, exist_ok=True)
        archives = []
        for i in range(4):
            src = installation/'base'/('assets%d.pk3' % i)
            dst = base/src.name
            if not dst.exists():
                try:
                    os.link(src, dst)  # Read only; never modify these links.
                except OSError:
                    shutil.copy2(src, dst)  # Restricted Windows hosts may deny links.
            archives.append(dst)
    else:
        basepath = installation
        archives = sorted((installation/'base').glob('*.pk3'), key=lambda p: p.name.lower())
        archives += sorted((installation/'OpenJK').glob('*.pk3'), key=lambda p: p.name.lower())
    corpus = Corpus(archives)
    errors = []
    effects = effects_inventory(corpus, errors)
    assert not errors, errors
    requested = sorted(e['file'] for e in effects)
    if not stock:
        requested = sorted(set(requested) | {p.relative_to(installation/'OpenJK').as_posix().lower()
                           for p in (installation/'OpenJK/effects').rglob('*.efx')})
    corpus.close()
    excluded = sorted(set(requested) & OWNED_TEST_FIXTURES)
    requested = [p for p in requested if p not in OWNED_TEST_FIXTURES]
    commands = ['wait 90', 'set developer 1', 'wait 8']
    for start in range(0, len(requested), 48):
        name = 'fx-audit-batch-%d.cfg' % start
        (game/name).write_text('\n'.join('fxaudit '+p[8:-4] for p in requested[start:start+48])+'\n')
        commands.append('exec '+name)
    commands += ['echo FXAUDIT_DONE', 'quit']
    (game/'fx-audit.cfg').write_text('\n'.join(commands)+'\n')
    settings = {'fs_basepath': str(basepath), 'fs_homepath': str(home), 'fs_game': 'OpenJK',
                'cl_renderer': 'rdsp-rend2', 'r_fullscreen': '0', 'r_mode': '3', 's_initsound': '0',
                'r_glslCache': '1', 'r_normalMapping': '0', 'r_specularMapping': '0',
                'r_volumetricFog': '0', 'r_volumetricParticles': '0', 'fx_freeze': '1',
                'fx_physicalization': '0', 'fx_physicalizationComposite': '1',
                'fx_physicalizationAdaptive': '1', 'fx_physicalizationEmission': '1',
                'fx_physicalizationAggregate': '0', 'fx_physicalizationDebug': '0',
                'developer': '0', 'logfile': '2', 'com_maxfps': '60'}
    args = [str(exe_dir/'openjk_sp.x86_64.exe')]
    for k, v in settings.items():
        args += ['+set', k, v]
    args += ['+devmap', 't1_fatal', '+exec', 'fx-audit.cfg']
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 0
    process = subprocess.Popen(args, cwd=exe_dir, startupinfo=info)
    try:
        code = process.wait(timeout=240)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait()
        raise
    log = (game/'qconsole.log').read_text(errors='replace')
    assert code == 0 and 'FXAUDIT_DONE' in log, log[-2500:]
    result = parse_report(log, requested)
    result['excluded_owned_test_fixtures'] = excluded
    result['scope'] = 'stock assets0-3' if stock else 'installed base/fs_game PK3s and fs_game loose EFX; isolated home'
    result['limitations'] = ['Eligibility only, not play frequency, reachability, master/opt policy or GPU visibility.',
                             'SP parser; files failing registration are reported separately.',
                             'Shader/texture hash guards do not detect definitions in unrelated scripts or every renderer image-format override.']
    path = root / ('stock-coverage.json' if stock else 'installed-coverage.json')
    path.write_text(json.dumps(result, indent=2), encoding='utf8')
    for stage, counts in result['stages'].items():
        print('%s %s: %d/%d effects (%.2f%%), %d/%d sprite primitives, %d/%d shader alternatives; failed %d' %
              ('stock' if stock else 'installed', stage, counts['effects'], len(requested),
               100*counts['effects']/len(requested), counts['sprite_primitives'], result['sprite_primitives'],
               counts['shader_alternatives'], result['shader_alternatives'], len(result['failed_effect_paths'])), flush=True)
    print('Report: '+str(path), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--installation', type=Path, required=True)
    ap.add_argument('--scope', choices=['stock', 'installed', 'both'], default='both')
    args = ap.parse_args()
    if os.name != 'nt': ap.error('Windows SP runtime required')
    root = Path(__file__).resolve().parents[2]/'build/fx-coverage'
    root.mkdir(parents=True, exist_ok=True)
    for stock in [True, False]:
        if args.scope == 'both' or (args.scope == 'stock') == stock:
            run(root, args.installation.resolve(), stock)


if __name__ == '__main__':
    main()
