#!/usr/bin/env python3
"""Generate exact, reviewed emission rules and source-file guards from stock PK3s."""
import argparse
from pathlib import Path
from audit_stock_fx import Corpus, effects_inventory, shader_inventory, shader_path
from generate_physicalization_stock import fingerprint

# Supported primitive ordinal. No inference from orange steam, sparks or glow names.
REVIEWED_EMISSION = {'env/fire': [0], 'env/small_fire': [0, 3],
                     'env/small_fire_lod': [0, 3], 'rocket/explosion': [0]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archives', nargs='+', type=Path)
    ap.add_argument('--out', type=Path, default=Path('shared/fx/FxPhysicalizationEmissionStock.h'))
    args = ap.parse_args()
    corpus = Corpus(args.archives)
    try:
        errors = []
        effects = {e['file']: e for e in effects_inventory(corpus, errors)}
        shaders = shader_inventory(corpus, errors)
        assert not errors, errors
        assets, rules = [], []
        for short, ordinals in sorted(REVIEWED_EMISSION.items()):
            path = 'effects/' + short + '.efx'
            ps = [p for p in effects[path]['primitives'] if p['engine_supported_type']]
            for ordinal in ordinals:
                p = ps[ordinal]
                assert p['type'] == 'particle'
                for shader in p['shaders']:
                    assert shader in {'gfx/effects/fire2', 'gfx/effects/fire3', 'gfx/effects/fire4', 'gfx/exp/rocket_explosion', 'gfx/exp/slower_rocket_explosion', 'gfx/exp/explosion1'}
                    assert len(shaders[shader]) == 1
                    definition = shaders[shader][0]
                    paths = [definition['file']]
                    for stage in definition['stages']:
                        assert stage['blend'] == ['gl_one', 'gl_one'], shader
                        for image in stage['maps']:
                            base = shader_path(image)
                            paths.append(next(n for n in [image] + [base+e for e in ('.tga', '.jpg', '.png', '.dds')] if n in corpus.index))
                    first = len(assets)
                    for asset in sorted(set(paths)):
                        assets.append('    {"%s", 0x%016xULL},' % (asset, fingerprint(corpus.read(asset))))
                    tau = .12 if shader.startswith('gfx/exp/') else .18
                    rules.append('    {"%s", 0x%016xULL, %d, "%s", %.3ff, %d, %d},' %
                                 (path, fingerprint(corpus.read(path)), ordinal, shader, tau, first, len(assets)-first))
        lines = ['// Generated exact identifiers, checksums and artistic settings only.',
                 'static const EmissionAsset emissionAssets[] = {'] + assets + ['};',
                 'static const EmissionRule emissionRules[] = {'] + rules + ['};', '']
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text('\n'.join(lines), encoding='utf8')
        print('Generated %d reviewed emission alternatives' % len(rules))
    finally:
        corpus.close()


if __name__ == '__main__':
    main()
