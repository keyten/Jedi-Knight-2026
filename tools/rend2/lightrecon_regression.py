#!/usr/bin/env python3
"""Controlled disabled-path shader contracts; uses the existing SDL/GL test compiler.

This is a fixture assembler and C-preprocessor adapter, not a GLSL compiler.
The actual compiler/linker is test_watersurface_gl.compile_program (the driver).
"""
import argparse
import ctypes as C
import difflib
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASELINE = HERE / 'lightrecon_baseline'
TOKEN = re.compile(r'[A-Za-z_]\w*|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?[uUfF]?|==|!=|<=|>=|&&|\|\||<<|>>|\+\+|--|[^\s]')
FORBIDDEN = re.compile(r'(?i)(?:light_?recon|USE_ENTITY_GRID_L1|USE_FROXEL_STATIC_RECONSTRUCTION|u_VolumetricDirMoment[RGB])')


def read(relative):
    return (ROOT / relative).read_text(encoding='utf-8').replace('\r\n', '\n')


def stage(name, kind):
    return read(f'shared/rd-rend2/glsl/{name}.glsl').split(f'/*[{kind}]*/', 1)[1].split('/*[')[0]


def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: '\n' * m[0].count('\n'), text, flags=re.S)


def enum_values(text, typename):
    for body, name in re.findall(r'typedef\s+enum\s*\{([^{}]*)\}\s*(\w+)\s*;', strip_comments(text), re.S):
        if name != typename:
            continue
        result, value = {}, -1
        for entry in body.split(','):
            entry = entry.strip()
            if not entry:
                continue
            parts = entry.split('=')
            value = int(parts[1].strip(), 0) if len(parts) == 2 else value + 1
            result[parts[0].strip()] = value
        return result
    raise ValueError(f'Missing renderer enum {typename}')


def common_header(defines, compute=False):
    """Controlled subset of GLSL_GetShaderHeader; enum values come from production.

    Optional technologies are EXPLICIT fixture inputs, never recon policy inputs.
    The audited source dependency fence protects the production decision boundary.
    """
    local = read('shared/rd-rend2/tr_local.h')
    constants = {}
    for typename in ('deform_t', 'genFunc_t', 'alphaGen_t', 'colorGen_t', 'texCoordGen_t'):
        for name, value in enum_values(local, typename).items():
            constants[name.replace('GF_', 'WF_') if name.startswith('GF_') else name] = value
    constants.update(MAX_G2_BONES=72, MAX_GPU_FOGS=16, MAX_DLIGHTS=32, DSHADOW_MAP_SIZE=512,
                     ALPHA_TEST_GT0=1, ALPHA_TEST_LT128=2, ALPHA_TEST_GE128=3, ALPHA_TEST_GE192=4, ALPHA_TEST_E255=5)
    constants.update({k: int(v) for k, v in re.findall(
        r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', local, re.M)})
    header = '#version %s core\n' % ('430' if compute else '150')
    header += '#define M_PI 3.14159265358979323846\nuniform vec2 r_FBufScale;\n'
    header += ''.join(f'#define {k} {v}\n' for k, v in sorted(constants.items()))
    header += '#define USE_ALPHA_TEST\n'
    if 'USE_PHYSICAL_PBR' in defines:
        header += '#define PBR_METERS_PER_WORLD_UNIT_SQUARED 0.0009\n'
    return header + ''.join(f'#define {d}\n' for d in defines)


def sources(case):
    defines = case['defines']
    name = case['program']
    compute = case.get('compute', False)
    h = common_header(defines, compute)
    fragment_lib = stage('pbr_common', 'Fragment') if name in ('lightall', 'volumetric_inject') else ''
    if 'USE_FROXEL_FOG' in defines:
        fragment_lib += stage('volumetric_common', 'Fragment')
    if name == 'lightall':
        vertex_lib = stage('leaf_flutter', 'Vertex') + stage('plant_skin', 'Vertex')
    elif name in ('generic', 'fogpass'):
        vertex_lib = stage('leaf_flutter', 'Vertex') + stage('plant_skin', 'Vertex')
    else:
        vertex_lib = ''
    if compute:
        return {'compute': h + fragment_lib + stage(name, case.get('source_stage', 'Fragment'))}
    result = {'vertex': h + vertex_lib + stage(name, 'Vertex'),
              'fragment': h + fragment_lib + stage(name, 'Fragment')}
    if name == 'volumetric_inject':
        result['geometry'] = h + stage(name, 'Geometry')
    return result


def binding_inventory(contracts):
    """Actual renderer sampler assignment expressions and block slots, not a port.

    Avoid freezing whole C++ files: unrelated code/formatting changes are free.
    This records initialization intent; it is not a per-draw GPU binding trace.
    """
    text = strip_comments(read('shared/rd-rend2/tr_glsl.cpp'))
    local = strip_comments(read('shared/rd-rend2/tr_local.h'))
    names = set(s[1] for program in contracts.values() for data in program['stages'].values() for s in data['samplers'])
    blocks = set(b['name'] for program in contracts.values() for data in program['stages'].values() for b in data['blocks'])
    info = re.search(r'uniformsInfo\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    uniform_names = re.findall(r'\{\s*"(\w+)"', info[1])
    indices = enum_values(local, 'uniform_t')
    if len(uniform_names) != indices['UNIFORM_COUNT']:
        raise ValueError('Uniform metadata parser no longer matches the production table')
    mapping = {name: uniform_names[index] for name, index in indices.items() if name != 'UNIFORM_COUNT'}
    def body(name):
        start = text.index(name + '(') if name + '(' in text else text.index(name + '( ')
        begin = text.index('{', start)
        # Ignore braces in literals while finding the end of a C++ function.
        lexical = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m: ' ' * len(m[0]), text)
        depth = 1
        for end in range(begin + 1, len(text)):
            depth += (lexical[end] == '{') - (lexical[end] == '}')
            if depth == 0:
                return text[begin:end]
        raise ValueError('Unterminated production loader ' + name)
    result = {}
    for name in ('GLSL_LoadGPUProgramGeneric', 'GLSL_LoadGPUProgramFogPass', 'GLSL_LoadGPUProgramLightAll',
                 'GLSL_LoadGPUProgramVolumetric', 'GLSL_SetFroxelLookupUnits'):
        calls = re.findall(r'GLSL_SetUniformInt\s*\(([^;]+)\)\s*;', body(name))
        result[name] = [' '.join(TOKEN.findall(m)) for m in calls if mapping[re.search(r'\bUNIFORM_\w+', m)[0]] in names]
    block_table = re.search(r'uniformBlocksInfo\[UNIFORM_BLOCK_COUNT\]\s*=\s*\{(.*?)\n\};', text, re.S)
    result['uniform_block_slots'] = [' '.join(TOKEN.findall(entry)) for entry in re.findall(r'\{([^{}]+)\}', block_table[1])
                                     if re.search(r'"(\w+)"', entry)[1] in blocks]
    used_units = set(re.findall(r'\bTB_\w+', json.dumps(result)))
    result['texture_unit_slots'] = {k: v for k, v in re.findall(r'\b(TB_\w+)\s*=\s*(\d+)\b', local) if k in used_units}
    result['cpu_buffer_layouts'] = {}
    for name in ('CameraBlock', 'SceneBlock', 'LightsBlock', 'EntityBlock', 'VolumetricFogBlock'):
        match = re.search(r'\bstruct\s+' + name + r'\s*\{(.*?)\n\};', local, re.S)
        if not match:
            raise ValueError('Missing CPU buffer layout ' + name)
        result['cpu_buffer_layouts'][name] = ' '.join(TOKEN.findall(match[1]))
    return result


def cpp_path(explicit=None):
    candidate = explicit or os.environ.get('REND2_CPP') or shutil.which('gcc') or shutil.which('clang')
    if not candidate:
        bundled = ROOT.parent / 'OpenJK/build/toolchain/w64devkit/bin/gcc.exe'
        if bundled.is_file():
            candidate = str(bundled)
    if not candidate:
        raise RuntimeError('Set REND2_CPP or pass --cpp with a GCC/Clang C preprocessor')
    return candidate


def preprocess(source, cpp):
    # GLSL directives are handled by the driver, not the C preprocessor. No GLSL
    # conditional parser is reimplemented here. #line IDs do not affect contracts.
    text = re.sub(r'^\s*#\s*(?:version|extension|line)\b[^\n]*', '', source, flags=re.M)
    args = [cpp, '-E', '-P', '-x', 'c', '-']
    def run(extra):
        result = subprocess.run(args[:-1] + extra + ['-'], input=text, text=True, capture_output=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return result.stdout
    expanded = run([])
    # Normalize whitespace/comments, NOT expressions, identifiers, values, order
    # or layouts. One source line per normalized statement makes diffs readable.
    tokens = TOKEN.findall(expanded)
    normalized = ' '.join(tokens).replace(' ; ', ' ;\n').replace(' { ', ' {\n').replace(' } ', ' }\n') + '\n'
    macros = dict(re.findall(r'^#define\s+(\w+)(?:\s+([^\n]*))?$', run(['-dM']), re.M))
    macros = {k: v.strip() for k, v in macros.items() if k.startswith(('USE_', 'PER_PIXEL', 'RGBM_'))}
    samplers = sorted(set(re.findall(r'\buniform\s+([iu]?sampler\w+)\s+(\w+)\s*(\[[^;]+\])?\s*;', expanded)))
    blocks = []
    for match in re.finditer(r'((?:layout\s*\([^)]*\)\s*)?(?:(?:readonly|writeonly|coherent|restrict|volatile)\s+)*)\b(uniform|buffer)\s+(\w+)\s*\{([^}]+)\}', expanded, re.S):
        blocks.append({'name': match[3], 'storage': match[2],
                       'layout': ' '.join(TOKEN.findall(match[1] or '')),
                       'members': ' '.join(TOKEN.findall(match[4]))})
    return {'source': normalized, 'defines': macros, 'samplers': [list(s) for s in samplers],
            'blocks': sorted(blocks, key=lambda b: b['name']), 'tokens': len(tokens),
            'texture_calls': len(re.findall(r'\b(?:texture\w*|texelFetch\w*)\s*\(', expanded))}


def lifecycle_audit():
    """Fence main-world ownership separately from legacy/grid reconstruction."""
    failures = []
    bsp = strip_comments(read('shared/rd-rend2/tr_bsp.cpp'))
    # The definitions (not forward declarations) are unambiguous in this file.
    main_start = bsp.find('void RE_LoadWorldMap( const char *name ) {')
    lower_start = bsp.find('world_t *R_LoadBSP(')
    if main_start < 0 or lower_start < 0:
        return ['Main/sub-BSP lifecycle definitions were not found; review the ownership audit']
    main = bsp[main_start:]
    lower = bsp[lower_start:main_start]
    required = ['LightReconWorldLoad reconstructedWorld(name)', 'R_LoadBSP(name)',
                'R_LoadAreaLights(', 'R_BuildStaticLighting(', 'R_CalibrateEntitySun(',
                'reconstructedWorld.Finish(']
    positions = [main.find(token) for token in required]
    if min(positions) < 0 or positions != sorted(positions):
        failures.append('Canonical main-world guard/commit must bracket BSP, area lights and legacy initialization')
    if re.search(r'LightReconWorldLoad|R_(?:Load|Clear)ReconstructedLighting', lower):
        failures.append('R_LoadBSP/sub-BSPs must not replace the canonical main-world context')
    legacy = strip_comments(read('shared/rd-rend2/tr_staticlighting.cpp'))
    if re.search(r'R_(?:Load|Clear)ReconstructedLighting', legacy):
        failures.append('Legacy static-light rebuild/clear must not own canonical asset loading')
    for variant in ('code', 'codemp'):
        init = strip_comments(read(f'{variant}/rd-rend2/tr_init.cpp'))
        for entry in ('void R_ClearTr(void)', 'void RE_Shutdown('):
            start = init.find(entry)
            body = init[init.find('{', start) + 1:]
            if start < 0 or not body.lstrip().startswith('R_ClearReconstructedLighting();'):
                failures.append(f'{variant}: {entry} must unconditionally release the canonical context first')
    return failures


def observation_audit():
    """Capture raw loader inputs, never renderer-converted/atlas pixels."""
    bsp = strip_comments(read('shared/rd-rend2/tr_bsp.cpp'))
    failures = []
    load = bsp[bsp.find('static\tvoid R_LoadLightmaps('):bsp.find('static float FatPackU(')]
    required = ['observations.Embedded(', 'R_CreateImage(', 'observations.External(',
                'R_ColorShiftLightingFloats(', 'ColorToRGBA16F(', 'R_ColorShiftLightingBytes(']
    positions = [load.find(token) for token in required]
    if min(positions) < 0 or positions != sorted(positions):
        failures.append('Original lightmap observation hooks must precede atlas upload and colour transforms')
    lower = bsp[bsp.find('world_t *R_LoadBSP('):bsp.find('void RE_LoadWorldMap( const char *name ) {')]
    tokens = ['LightReconBspCapture observations(name, bspIndex == nullptr, buffer.b, sourceBytes)',
              '((int *)header)[i] =', 'R_LoadLightmaps(', 'observations.GridEmbedded(', 'observations.Surfaces(',
              'observations.ReleaseSource()', 'R_LoadSurfaces(', 'R_LoadLightGrid(']
    positions = [lower.find(token) for token in tokens]
    if min(positions) < 0 or positions != sorted(positions):
        failures.append('Original BSP capture must be main-world scoped, before byte swapping, and release scratch before surfaces')
    if load.count('observations.External(') != 2 or 'R_LoadImageSource(' not in load:
        failures.append('Both lighting and deluxe loaders must preserve original samples and resolved source paths')
    grid = bsp[bsp.find('void R_LoadLightGrid('):bsp.find('void R_LoadLightGridArray(')]
    tokens = ['observations.GridLayout(', 'R_ColorShiftLightingBytes(', 'ri.FS_ReadFile(',
              'observations.GridHdr(', 'worldData->hdrLightGrid =', 'ri.FS_FreeFile(']
    positions = [grid.find(token) for token in tokens]
    if min(positions) < 0 or positions != sorted(positions) or 'observations.GridHdrSidecar(' not in grid:
        failures.append('Original grid layout/HDR hooks must preserve pre-conversion inputs and observation-only sidecars')
    surfaces = bsp[bsp.find('static\tvoid R_LoadSurfaces('):bsp.find('static\tvoid R_LoadMarksurfaces(')]
    if 'observations.SurfaceMaterial(' not in surfaces:
        failures.append('Original surface capture must retain parse-time deformation/material exclusions')
    return failures


def production_audit():
    """Fence current absence of canonical GPU integration, with actionable errors.

    A future adapter must explicitly extend coverage before relaxing this fence.
    This is a dependency assertion, not hashes of unrelated C++ functions.
    """
    allowed = {'shared/rd-rend2/tr_lightrecon.cpp', 'shared/rd-rend2/tr_lightrecon.h',
               'shared/rd-rend2/tr_lightrecon_cvars.inc', 'shared/rd-rend2/tr_lightrecon_register.inc',
               'shared/rd-rend2/tr_local.h', 'shared/rd-rend2/tr_staticlighting.h'}
    failures = lifecycle_audit() + observation_audit()
    for directory in ('shared/rd-rend2', 'code/rd-rend2', 'codemp/rd-rend2'):
        for path in (ROOT / directory).rglob('*'):
            if path.suffix not in ('.cpp', '.h', '.glsl', '.inc') or not path.is_file():
                continue
            relative = path.relative_to(ROOT).as_posix()
            if relative in allowed or '/lightrecon/' in relative:
                continue
            # Core API declarations/definitions are not renderer consumers.
            if path.name.startswith(('tr_lightrecon_types.', 'tr_lightrecon_api.', 'tr_lightrecon_manager.')):
                continue
            text = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
            for m in re.finditer(r'\br_lightRecon\w+|\bRend2LightRecon\b|\bR_LightReconConsumer\s*\(|\bR_ReconstructedLighting\s*\(', text):
                if m[0] in ('r_lightReconInfo', 'r_lightReconLuxels') and path.name == 'tr_init.cpp':
                    continue # diagnostic command registration, not a feature input
                line = text[:m.start()].count('\n') + 1
                failures.append(f'{relative}:{line}: canonical GPU consumer dependency {m[0]}; extend disabled-path coverage before integration')
    adapter = strip_comments(read('shared/rd-rend2/tr_lightrecon.cpp'))
    if re.search(r'\b(?:qgl\w+|GLSL_\w+|R_Volumetric\w+|R_ForwardPlus\w+)\s*\(|Cvar_(?:Set|Get)', adapter):
        failures.append('Canonical control adapter must not create GPU resources, shader permutations or force another cvar')
    debug = adapter[adapter.find('void R_LuxelDebugDraw('):]
    if not re.search(r'if\s*\(s_luxelDebug.empty\(\).*?\)\s*return;', debug, re.S):
        failures.append('Luxel debug must return before shader registration/submission with no explicit markers')
    if not re.search(r'RE_RegisterShaderFromImage\("\*luxelDebug",\s*lightmapsVertex\b', debug):
        failures.append('Luxel markers must use the depth-tested vertex-colour shader, not a depth-disabled 2D shader')
    return failures


def differences(expected, actual, label):
    if expected == actual:
        return []
    a = json.dumps(expected, indent=2, sort_keys=True).splitlines(keepends=True)
    b = json.dumps(actual, indent=2, sort_keys=True).splitlines(keepends=True)
    return list(difflib.unified_diff(a, b, fromfile=f'baseline/{label}', tofile=f'current/{label}'))


def snapshot(cases, cpp):
    result = {}
    for case in cases:
        result[case['name']] = {'configuration': case, 'stages': {k: preprocess(v, cpp) for k, v in sources(case).items()}}
        for name, data in result[case['name']]['stages'].items():
            forbidden = FORBIDDEN.search('\n'.join(data['defines']) + '\n' + data['source'])
            if forbidden:
                raise AssertionError(f"{case['name']}/{name}: disabled path includes {forbidden[0]}")
    return result


def gpu_compile(cases, contracts):
    # Reuse the existing hidden-context test infrastructure and driver compiler.
    from test_watersurface_gl import context, compile_program, gl, SDL, U, I, P
    window, ctx = context(b'Rend2 disabled reconstruction contracts')
    reflected = {'driver': [gl('glGetString', C.c_char_p, U)(kind).decode() for kind in (0x1F00, 0x1F01, 0x1F02)], 'programs': {}}
    try:
        for case in cases:
            stages = sources(case)
            types = dict(vertex=0x8B31, fragment=0x8B30, geometry=0x8DD9, compute=0x91B9)
            program = compile_program([(types[k], v) for k, v in stages.items()], case['name'],
                                      outputs=('out_Color', 'out_Glow', 'out_SSRNormal', 'out_SSRSpecular'))
            count = I()
            gl('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8B86, C.byref(count))
            uniforms = []
            sampler_names = set(re.findall(r'\buniform\s+[iu]?(?:sampler|image)\w*\s+(\w+)', '\n'.join(stages.values())))
            for index in range(count.value):
                name, size, typ = C.create_string_buffer(1024), I(), U()
                gl('glGetActiveUniform', None, U, U, I, P, C.POINTER(I), C.POINTER(U), P)(program, index, len(name), None, C.byref(size), C.byref(typ), name)
                decoded = name.value.decode()
                if FORBIDDEN.search(decoded):
                    raise AssertionError('Reconstruction uniform in disabled GPU program: ' + decoded)
                if decoded.split('[')[0] in sampler_names:
                    uniforms.append([decoded, typ.value, size.value])
            blocks = []
            gl('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8A36, C.byref(count))
            for index in range(count.value):
                name, size = C.create_string_buffer(1024), I()
                gl('glGetActiveUniformBlockName', None, U, U, I, P, P)(program, index, len(name), None, name)
                gl('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(I))(program, index, 0x8A40, C.byref(size))
                blocks.append([name.value.decode(), size.value])
            storage = []
            for name in sorted({b['name'] for data in contracts[case['name']]['stages'].values() for b in data['blocks'] if b['storage'] == 'buffer'}):
                index = gl('glGetProgramResourceIndex', U, U, U, C.c_char_p)(program, 0x92E6, name.encode())
                if index == 0xFFFFFFFF:
                    continue
                props, values = (U * 2)(0x9302, 0x9303), (I * 2)()
                gl('glGetProgramResourceiv', None, U, U, U, I, P, I, P, P)(program, 0x92E6, index, 2, props, 2, None, values)
                storage.append([name, values[0], values[1]])
            reflected['programs'][case['name']] = dict(uniforms=sorted(uniforms), uniform_blocks=sorted(blocks), storage_blocks=storage)
            gl('glDeleteProgram', None, U)(program)
            print('PASS compile/link: ' + case['name'], flush=True)
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return reflected


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpp')
    parser.add_argument('--no-gpu', action='store_true', help='Check preprocessing/contracts only; NOT a shader compilation pass')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/lightrecon-regression')
    parser.add_argument('--export-baseline', type=Path, help='export readable preprocessed GLSL/metadata, without running a test')
    parser.add_argument('--write-baseline', metavar='REVIEW_REASON', help='Explicitly replace the controlled baseline with this review rationale')
    args = parser.parse_args(argv)
    if args.export_baseline:
        expected = json.loads(gzip.decompress((BASELINE / 'preprocessed.json.gz').read_bytes()))
        for name, case in expected.items():
            directory = args.export_baseline / name
            directory.mkdir(parents=True, exist_ok=True)
            for kind, data in case['stages'].items():
                (directory / (kind + '.glsl')).write_text(data['source'])
            (directory / 'manifest.json').write_text(json.dumps({
                'configuration': case['configuration'], 'stages': {k: {n: v for n, v in d.items() if n != 'source'}
                    for k, d in case['stages'].items()}}, indent=2) + '\n')
        print(f'Exported {len(expected)} baseline programs to {args.export_baseline}')
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    audit = production_audit()
    if audit:
        raise AssertionError('\n'.join(audit))
    cases = json.loads((BASELINE / 'matrix.json').read_text())
    current = snapshot(cases, cpp_path(args.cpp))
    bindings = binding_inventory(current)
    reflection = gpu_compile(cases, current) if not args.no_gpu else None
    frozen = BASELINE / 'preprocessed.json.gz'
    if args.write_baseline:
        if args.no_gpu:
            parser.error('Baseline update requires actual driver compilation')
        metadata = {'schema': 1, 'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    'review_reason': args.write_baseline, 'reflection_note': 'Active sampler names/types/counts, std140 block sizes and std430 block bindings/sizes; no driver locations/IDs',
                    'reflection': reflection}
        frozen.write_bytes(gzip.compress(json.dumps(current, sort_keys=True).encode(), mtime=0))
        (BASELINE / 'manifest.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
        (BASELINE / 'bindings.json').write_text(json.dumps(bindings, indent=2, sort_keys=True) + '\n')
    expected = json.loads(gzip.decompress(frozen.read_bytes()))
    diff = differences(json.loads((BASELINE / 'bindings.json').read_text()), bindings, 'renderer-initialization-bindings')
    for name in sorted(set(expected) | set(current)):
        a, b = expected.get(name), current.get(name)
        if a is None or b is None:
            diff += differences(a, b, name)
            continue
        diff += differences(a['configuration'], b['configuration'], name + '/permutation')
        for kind in sorted(set(a['stages']) | set(b['stages'])):
            left, right = a['stages'].get(kind), b['stages'].get(kind)
            if left and right:
                # Real GLSL lines rather than escaped JSON strings for code diffs.
                diff += list(difflib.unified_diff(left['source'].splitlines(True), right['source'].splitlines(True),
                    fromfile=f'baseline/{name}/{kind}.glsl', tofile=f'current/{name}/{kind}.glsl'))
                diff += differences({k: v for k, v in left.items() if k != 'source'},
                                    {k: v for k, v in right.items() if k != 'source'}, name + '/' + kind + '/bindings-defines-work')
            else:
                diff += differences(left, right, name + '/' + kind)
    interface_comparison = 'not run (--no-gpu)'
    if reflection:
        manifest = json.loads((BASELINE / 'manifest.json').read_text())
        if manifest['reflection']['driver'] == reflection['driver']:
            diff += differences(manifest['reflection']['programs'], reflection['programs'], 'active-GPU-interface')
            interface_comparison = 'matching-driver baseline compared'
        else:
            interface_comparison = 'different driver; declared interfaces compared, active optimization differences not judged'
        if FORBIDDEN.search(json.dumps(reflection)):
            diff += ['Reconstruction symbol in active GPU interface\n']
    (args.output / 'comparison.diff').write_text(''.join(diff))
    (args.output / 'current.json.gz').write_bytes(gzip.compress(json.dumps(current, sort_keys=True).encode(), mtime=0))
    report = dict(passed=not diff, programs=len(cases), stages=sum(len(v['stages']) for v in current.values()),
                  gpu_compiled=bool(reflection), driver=reflection['driver'] if reflection else None, gpu_interface_comparison=interface_comparison,
                  production_dependency_audit='passed', differences=len(diff),
                  baseline_revision=json.loads((BASELINE / 'manifest.json').read_text())['source_revision'])
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if diff:
        print(''.join(diff[:150]))
        print('Full diff: ' + str(args.output / 'comparison.diff'))
    return 1 if diff else 0


if __name__ == '__main__':
    raise SystemExit(main())
