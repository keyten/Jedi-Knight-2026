"""Compile the C04 model permutations on the OpenGL driver."""
import json

from lightrecon_regression import BASELINE, cpp_path, gpu_compile, preprocess, sources


def main():
    cases = [case for case in json.loads((BASELINE / 'matrix.json').read_text())
             if 'USE_LIGHT_VECTOR' in case['defines']]
    cases += [
        dict(name='ja-md3', program='lightall',
             defines=['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_VERTEX_ANIMATION']),
        dict(name='pbr-ghoul2-split-sun-l1-grid', program='lightall',
             defines=['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_SKELETAL_ANIMATION',
                      'USE_NORMALMAP', 'USE_SPECULARMAP', 'USE_PHYSICAL_PBR',
                      'USE_ENTITY_GRID', 'USE_ENTITY_GPU_GRID', 'USE_ENTITY_GRID_L1',
                      'USE_SHADOWMAP', 'SHADOWMAP_MODULATE', 'USE_ENTITY_SUN_SPLIT',
                      'r_shadowMapSize 512', 'r_shadowCascadeZFar 4096.0',
                      'ENTITY_GRID_LDR_RANGE 4.0']),
    ]
    variants = [dict(case, name=case['name'] + '-c04',
                     defines=case['defines'] + ['USE_RECON_ENTITY_IRRADIANCE']) for case in cases]
    compiler = cpp_path()
    contracts = {case['name']: {'stages': {stage: preprocess(source, compiler)
                                         for stage, source in sources(case).items()}}
                 for case in cases + variants}
    reflection = gpu_compile(cases + variants, contracts)
    for case, variant in zip(cases, variants):
        original = reflection['programs'][case['name']]
        enabled = reflection['programs'][variant['name']]
        assert original == enabled, (case['name'], original, enabled)
    print(f'{len(variants)} C04 variants compiled/linked with the original GPU resource layouts')


if __name__ == '__main__':
    main()
