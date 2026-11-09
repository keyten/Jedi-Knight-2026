"""Owned compiled indirect-only receiver region, paired bounce=0/1 originals.

The local search volume excludes the authored primary. Original codes are never
subtracted. Counterexample scope is frozen by geometry, not positive RGB values.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

from tools.rend2.validation.bridge import brush, entity, make_assets, mapped_rows
from tools.rend2.lightmap_patches.run import sha, write
from .run import spatial
from tools.rend2.hidden_candidates.run import request


def execute(command, log):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, errors='replace', timeout=180)
    log.write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode or re.search(r'Entity leaked|\*+\s*ERROR\b', result.stdout+result.stderr, re.I):
        raise ValueError('Compiled GI control failed: '+str(log))


def build(output, compiler, native):
    from tools.rend2.bake_model.bake_model import COMPILER_SHA256
    if sha(compiler) != COMPILER_SHA256:
        raise ValueError('Pinned JA compiler hash required')
    output = output.resolve(); output.mkdir(parents=True, exist_ok=False)
    base = output/'authoring/base'; make_assets(base)
    solid = [((-208, -144, -16), (208, 144, 0)), ((-208, -144, 0), (-192, 144, 272)),
             ((192, -144, 0), (208, 144, 272)), ((-192, -144, 0), (192, -128, 272)),
             ((-192, 128, 0), (192, 144, 272)), ((-208, -144, 256), (208, 144, 272)),
             ((-8, -128, 0), (8, 128, 128))]
    text = '{\n"classname" "worldspawn"\n"gridsize" "64 64 64"\n'+''.join(brush(a, b) for a, b in solid)+'}\n'
    text += entity(dict(classname='info_player_start', origin='-96 0 64'))
    text += entity(dict(classname='info_player_deathmatch', origin='96 0 64'))
    text += entity(dict(classname='light', origin='-96 0 192', light='600', _color='1 .45 .2'))
    originals = []
    for bounce in (0, 1):
        mp = base/'maps'/('asr16_gi_'+str(bounce)+'.map'); mp.write_text(text, encoding='ascii')
        for i, stage in enumerate((['-meta', '-leaktest', '-noflares', '-samplesize', '16'], ['-vis'],
                ['-light', '-deluxe', '-deluxemode', '0', '-gamma', '1', '-compensate', '1', '-exposure', '0',
                 '-bounce', str(bounce), '-super', '1', '-nocollapse'])):
            execute([compiler, '-game', 'ja', '-fs_basepath', base.parent, '-fs_game', 'base', '-threads', '1', *stage, mp],
                    output/('compile-'+str(bounce)+'-'+str(i)+'.log'))
        bsp = mp.with_suffix('.bsp')
        rows, (_, surfaces, _) = mapped_rows(bsp)
        # Right floor behind a half-height wall. Fixed geometric interior only.
        selected = spatial(sorted([r for r in rows if r['normal'][2] > .99 and
                           32 <= r['position'][0] <= 128 and abs(r['position'][1]) <= 96], key=lambda r: r['id']), 64)
        originals.append(selected)
        if bounce == 1:
            prefix = output/'geometry'; raw = output/'original.jkobs'
            materials = output/'controlled-materials.tsv'
            materials.write_text('JKCONTROLLEDMATERIAL1 '+sha(bsp)+' '+str(len(surfaces))+'\n'+
                '\n'.join(str(s['ordinal'])+' '+s['shader']['name']+' 0' for s in surfaces)+'\n')
            execute([native/'lightrecon_regions_inspect.exe', bsp, prefix, 0, 0, 128, 512, 128, 32, 1, raw, materials], output/'capture.log')
            execute([native/'lightrecon_dataset_quality.exe', 'scan', raw, output/'sanitized'], output/'sanitize.log')
            bounds = [16, -112, 1, 176, 112, 248]
            receiver_bounds = [16, -128, -1, 176, 128, 256]
            training = request(selected, sha(bsp), receiver_bounds, bounds, False)
            # Preserve enough analysis space for local sources, with floor support.
            path = output/'gi.request'; path.write_text(training, encoding='ascii')
            specification = dict(name='compiled_gi_only_region', request=str(path),
                geometry=str(output/'sanitized.public.jkobs'), provenance=dict(
                    kind='fresh_owned_compiled_GI_only_receiver_scope', compiler_sha256=sha(compiler),
                    original_bsp_sha256=sha(bsp), row_selection='Fixed right-floor geometry; no RGB-driven crop',
                    bounce_iterations=1, primary_outside_search_volume=True))
            write(output/'specification.json', specification)
    if len(originals[0]) < 6 or len(originals[1]) < 6:
        raise ValueError('Insufficient original GI receiver support')
    counts = [dict(rows=len(rows), positive=sum(max(r['raw']) > 0 for r in rows),
                   maximum=max(max(r['raw']) for r in rows)) for rows in originals]
    if counts[0]['positive'] != 0 or counts[1]['positive'] < 6:
        raise ValueError('Owned GI-only control did not establish zero-direct / positive-bounce support')
    write(output/'validation.json', dict(schema='org.openjk.asr16-compiled-gi-control.v1',
        paired_originals=counts, subtraction_performed=False, map_source_sha256=sha(base/'maps/asr16_gi_1.map'),
        compiler_sha256=sha(compiler), source_outside_local_volume=True,
        photometry='Encoded original RGB8, no irradiance calibration'))
    write(output/'private-answer.json', dict(sources=[],
        scope='No primary source inside the fixed local search volume; indirect-only right-floor evidence'))
    print(json.dumps(counts))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--native', type=Path, required=True)
    a = p.parse_args(); build(a.out, a.compiler.resolve(), a.native.resolve())
