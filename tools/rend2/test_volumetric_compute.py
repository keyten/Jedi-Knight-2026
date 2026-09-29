"""GPU regression checks using a hidden SDL GL context (Windows, bundled SDL2).

Run from any directory: python tools/rend2/test_volumetric_compute.py
Compiles actual raster/compute sources (and the compute media kernel), checks column
integration against Beer-Lambert on a grid with partial workgroups, including zero
extinction, and compares raster and compute injection with history, jitter and
reprojection.
"""
import ctypes as C
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
U, I, F, P = C.c_uint, C.c_int, C.c_float, C.c_void_p
SDL = C.CDLL(str(ROOT / 'lib/SDL2/bin/x64/SDL2.dll'))
for name, args, result in [
    ('SDL_CreateWindow', [C.c_char_p, I, I, I, I, U], P),
    ('SDL_GL_CreateContext', [P], P),
    ('SDL_GL_GetProcAddress', [C.c_char_p], P),
    ('SDL_GL_DeleteContext', [P], None),
    ('SDL_DestroyWindow', [P], None),
    ('SDL_GetError', [], C.c_char_p),
]:
    fn = getattr(SDL, name)
    fn.argtypes, fn.restype = args, result


def gl(name, result, *args):
    address = SDL.SDL_GL_GetProcAddress(name.encode())
    assert address, name
    return C.WINFUNCTYPE(result, *args)(address)


def fragment(name):
    return (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text().split('/*[Fragment]*/')[1]


def program(name, compute, rgb, shadows, media=False):
    constants = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
    defines = ''.join(f'#define {key} {value}\n' for key, value in re.findall(
        r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', constants, re.M))
    defines += '#define MAX_DLIGHTS 32\n#define M_PI 3.14159265358979323846\n#define USE_FROXEL_FOG\n'
    if name == 'volumetric_inject':
        defines += '#define USE_FROXEL_NOISE\n#define USE_FROXEL_PARTICLES\n'
    defines += '#define USE_FROXEL_RGB\n' if rgb else ''
    defines += '#define USE_SHADOWS2\n' if shadows else ''
    defines += '#define USE_FROXEL_COMPUTE\n' if compute else ''
    defines += '#define USE_FROXEL_MEDIA_PASS\n' if media else ''
    body = fragment('volumetric_common') + fragment(name)
    sources = [(0x91B9, '#version 430 core\n' + defines + body)] if compute else [
        (0x8B31, '#version 150 core\n' + defines + (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text().split('/*[Vertex]*/')[1].split('/*[')[0]),
        (0x8B30, '#version 150 core\n' + defines + body),
    ]
    if not compute and name == 'volumetric_inject':
        sources.append((0x8DD9, '#version 150 core\n' + defines + (ROOT / 'shared/rd-rend2/glsl/volumetric_inject.glsl').read_text().split('/*[Geometry]*/')[1].split('/*[')[0]))
    result = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(text.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(16384)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, (name, compute, rgb, shadows, media, log.value.decode())
        gl('glAttachShader', None, U, U)(result, shader)
        gl('glDeleteShader', None, U)(shader)
    for index, output in enumerate(['out_Color', 'out_Glow', 'out_SSRNormal', 'out_SSRSpecular']):
        if not compute:
            gl('glBindFragDataLocation', None, U, U, C.c_char_p)(result, index, output.encode())
    gl('glLinkProgram', None, U)(result)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(result, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(result, len(log), None, log)
    assert ok.value, log.value.decode()
    return result


def uniform_buffer(prog, block, values, slot=0):
    index = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, block.encode())
    assert index != 0xFFFFFFFF
    size = I()
    gl('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(I))(prog, index, 0x8A40, C.byref(size))
    data = bytearray(size.value)
    for name, value in values.items():
        names = (C.c_char_p * 1)(name.encode())
        member, offset = U(), I()
        gl('glGetUniformIndices', None, U, I, C.POINTER(C.c_char_p), C.POINTER(U))(prog, 1, names, C.byref(member))
        assert member.value != 0xFFFFFFFF, name
        gl('glGetActiveUniformsiv', None, U, I, C.POINTER(U), U, C.POINTER(I))(prog, 1, C.byref(member), 0x8A3B, C.byref(offset))
        struct.pack_into('f' * len(value), data, offset.value, *value)
    buffer = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
    gl('glBindBuffer', None, U, U)(0x8A11, buffer)
    raw = C.create_string_buffer(bytes(data))
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8A11, len(data), raw, 0x88E4)
    gl('glUniformBlockBinding', None, U, U, U)(prog, index, slot)
    gl('glBindBufferBase', None, U, U, U)(0x8A11, slot, buffer)
    return buffer


def texture(width, height, depth, values=None, internal=0x881A):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x806F, tex)
    raw = (F * len(values))(*values) if values is not None else None
    gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(0x806F, 0, internal, width, height, depth, 0, 0x1908, 0x1406, raw)
    for parameter in [0x2800, 0x2801]:
        gl('glTexParameteri', None, U, U, I)(0x806F, parameter, 0x2600)
    return tex


def read(tex, count):
    gl('glMemoryBarrier', None, U)(0x100)  # texture update/readback
    gl('glBindTexture', None, U, U)(0x806F, tex)
    data = (F * count)()
    gl('glGetTexImage', None, U, I, U, U, P)(0x806F, 0, 0x1908, 0x1406, data)
    return list(data)


def integration(prog, rgb, depth=17, far_depth=128):
    width, height = 7, 5
    near_depth = 8
    log_ratio = math.log2(far_depth / near_depth)
    values, dynamic, sigma = [], [], []
    for z in range(depth):
        for y in range(height):
            for x in range(width):
                density = 0 if x == 0 else 0.001 * (1 + z % 3)
                values.extend([0.003, 0.005, 0.007, density])
                dynamic.extend([0.001, 0.002, 0.003, 0])
                sigma.extend([density * 0.5, density, density * 1.5, density])
    inputs = [texture(width, height, depth, v) for v in [values, dynamic, sigma]]
    outputs = [texture(width, height, depth) for _ in range(2)]
    gl('glUseProgram', None, U)(prog)
    buffer = uniform_buffer(prog, 'VolumetricFog', {
        'u_FroxelGridSize': [width, height, depth, 0],
        'u_FroxelSliceParams': [near_depth, far_depth, log_ratio, far_depth],
        'u_FroxelRayForward': [0, 0, 1, 0],
        'u_FroxelRayRight': [1, 0, 0, 0],
        'u_FroxelRayUp': [0, 1, 0, 0],
    })
    for unit, (tex, name) in enumerate(zip(inputs, ['u_FroxelSource', 'u_FroxelDynamic', 'u_FroxelExtinction'])):
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(0x806F, tex)
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
        gl('glUniform1i', None, I, I)(loc, unit)
    for unit, tex in enumerate(outputs[:2 if rgb else 1]):
        gl('glBindImageTexture', None, U, U, I, U, I, U, U)(unit, tex, 0, 1, 0, 0x88B9, 0x881A)
    gl('glDispatchCompute', None, U, U, U)(1, 1, 1)
    gl('glMemoryBarrier', None, U)(0x28)
    actual = read(outputs[0], width * height * depth * 4)
    trans = read(outputs[1], len(actual)) if rgb else None
    # Read uploaded half-float inputs, so reference includes source quantization.
    source, light, extinction = [read(tex, len(actual)) for tex in inputs]
    for y in range(height):
        for x in range(width):
            S, T, scalar = [0.0] * 3, [1.0] * 3, 1.0
            ray = math.sqrt(1 + ((x + .5) / width * 2 - 1) ** 2 + ((y + .5) / height * 2 - 1) ** 2)
            for z in range(depth):
                base = ((z * height + y) * width + x) * 4
                near = near_depth * 2 ** (log_ratio * z / depth) if z else 0
                far = near_depth * 2 ** (log_ratio * (z + 1) / depth)
                length = (far - near) * ray
                for channel in range(3):
                    density = extinction[base + channel] if rgb else source[base + 3]
                    tau = density * length
                    phi = -math.expm1(-tau) / tau if tau else 1
                    S[channel] += T[channel] * (source[base + channel] + light[base + channel]) * length * phi
                    T[channel] *= math.exp(-tau)
                scalar *= math.exp(-source[base + 3] * length)
                for a, e in zip(actual[base:base + 4], S + [scalar]):
                    assert abs(a - e) <= max(0.00002, abs(e) * 0.0011), (rgb, x, y, z, a, e)
                if rgb:
                    for a, e in zip(trans[base:base + 4], T + [scalar]):
                        assert abs(a - e) <= max(0.00002, abs(e) * 0.0011), (rgb, x, y, z, a, e)
    assert gl('glGetError', U)() == 0
    gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
    print(f'PASS: {"RGB" if rgb else "scalar"} integration, zero extinction, partial workgroups, '
          f'all {depth} slices, far={far_depth}')


W, H, D = 7, 5, 17
SAMPLERS = ['u_FroxelHistory', 'u_VolumetricStaticGrid', 'u_VolumetricSunGrid',
            'u_VolumetricDirGrid', 'u_VolumetricDirVecGrid', 'u_VolumetricLegacyGrid',
            'u_ShadowMap', 'u_ShadowMap2', 'u_FroxelNoise', 'u_FroxelMedia',
            'u_FroxelExtinction', 'u_FPlusLights', 'u_FPlusGridMap', 'u_LightCookieMap']
# world = (ndc.x * d, ndc.y * d, d); clip = (x + .1 z, y - .05 z, 0, z): the history
# lookup lands between froxel centers, as after a small camera turn
REPROJECT = [1, 0, 0, 0, 0, 1, 0, 0, .1, -.05, 0, 1, 0, 0, 0, 0]
OUTPUT_FORMATS = [0x881A, 0x8C3A, 0x8C3A, 0x881A]  # inject, dynamic, particle light, sigma_t.rgb


def block_values(temporal):
    values = {
        'u_FroxelGridSize': [W, H, D, 0],
        'u_FroxelSliceParams': [8, 128, 4, 128],
        'u_FroxelRayForward': [0, 0, 1, 0],
        'u_FroxelRayRight': [1, 0, 0, 0],
        'u_FroxelRayUp': [0, 1, 0, 0],
        'u_FroxelLightParams': [0, 1, 1, 1],
        'u_FroxelHeightFog': [.01, 0, 0, 0],
        'u_FroxelHeightFogColor': [.5, .5, .5, 0],
        'u_FroxelHeightFogTop': [0, .5, 1, 1.5],
    }
    if temporal:
        values.update({
            'u_FroxelViewProjection': REPROJECT,
            'u_FroxelPrevViewProjection': REPROJECT,
            'u_FroxelJitter': [.25, -.25, .4, 1],
            'u_FroxelTemporalParams': [.75, 1, 0, 4],
            'u_FroxelGridScale': [1 / 64, 1 / 64, 1 / 128, 0],
        })
    return values


def bind_program(prog, textures, temporal):
    """Blocks and one texture unit per sampler, as the renderer does."""
    gl('glUseProgram', None, U)(prog)
    buffers = [uniform_buffer(prog, 'VolumetricFog', block_values(temporal))]
    for slot, block in enumerate(['Lights', 'VolumetricParticles'], 1):
        buffers.append(uniform_buffer(prog, block, {}, slot))
    for unit, name in enumerate(SAMPLERS):
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
        gl('glUniform1i', None, I, I)(loc, unit)
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(0x806F, textures.get(name, textures['black']))
    return buffers


def uniform4(prog, name, *values):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
    gl('glUniform4f', None, I, F, F, F, F)(loc, *values)


def tail_texture():
    tail = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tail))
    gl('glBindTexture', None, U, U)(0x0DE1, tail)
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x881A, W, H, 0, 0x1908, 0x1406, None)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2801, 0x2600)
    return tail


def read2d(tex):
    gl('glMemoryBarrier', None, U)(0x100)
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    data = (F * (W * H * 4))()
    gl('glGetTexImage', None, U, I, U, U, P)(0x0DE1, 0, 0x1908, 0x1406, data)
    return list(data)


def delete_buffers(buffers):
    for buffer in buffers:
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))


def run_compute(inject, media, textures, temporal):
    """Media kernel, then the injection kernel, as RB_VolumetricBuild dispatches them."""
    outputs = [texture(W, H, D, internal=fmt) for fmt in OUTPUT_FORMATS]
    media_tex, tail = texture(W, H, D, internal=0x822D), tail_texture()
    bind_image = gl('glBindImageTexture', None, U, U, I, U, I, U, U)
    buffers = bind_program(media, textures, temporal)
    bind_image(5, media_tex, 0, 1, 0, 0x88B9, 0x822D)
    gl('glDispatchCompute', None, U, U, U)(2, 2, 5)
    gl('glMemoryBarrier', None, U)(0x08)  # texture fetch
    delete_buffers(buffers)
    buffers = bind_program(inject, dict(textures, u_FroxelMedia=media_tex), temporal)
    for unit, (tex, fmt) in enumerate(zip(outputs, OUTPUT_FORMATS)):
        bind_image(unit, tex, 0, 1, 0, 0x88B9, fmt)
    bind_image(4, tail, 0, 0, 0, 0x88B9, 0x881A)
    uniform4(inject, 'u_ParticleLight', 1, 0, 0, 0)
    gl('glDispatchCompute', None, U, U, U)(2, 2, 5)
    gl('glMemoryBarrier', None, U)(0x28)
    for unit in range(6):
        bind_image(unit, 0, 0, 0, 0, 0x88B9, 0x881A)
    delete_buffers(buffers)
    return media_tex, outputs, tail


def run_raster(prog, textures, temporal, rgb):
    """The raster path: layered media draw, layered injection, tail draw."""
    outputs = [texture(W, H, D, internal=fmt) for fmt in OUTPUT_FORMATS]
    media_tex, tail = texture(W, H, D, internal=0x822D), tail_texture()
    vao, fbo = U(), U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glBindFramebuffer', None, U, U)(0x8D40, fbo)
    gl('glViewport', None, I, I, I, I)(0, 0, W, H)
    attach = gl('glFramebufferTexture', None, U, U, U, I)
    draw_buffers = gl('glDrawBuffers', None, I, C.POINTER(U))
    draw = gl('glDrawArraysInstanced', None, U, I, I, I)
    slice_loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_FroxelSlice')

    buffers = bind_program(prog, textures, temporal)
    gl('glUniform1i', None, I, I)(slice_loc, 0)
    attach(0x8D40, 0x8CE0, media_tex, 0)
    draw_buffers(1, (U * 1)(0x8CE0))
    uniform4(prog, 'u_ParticleLight', 0, 0, 1, 0)
    draw(4, 0, 3, D)
    delete_buffers(buffers)

    buffers = bind_program(prog, dict(textures, u_FroxelMedia=media_tex), temporal)
    count = 4 if rgb else 3
    for index in range(count):
        attach(0x8D40, 0x8CE0 + index, outputs[index], 0)
    draw_buffers(count, (U * count)(*[0x8CE0 + i for i in range(count)]))
    assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
    uniform4(prog, 'u_ParticleLight', 1, 0, 0, 0)
    draw(4, 0, 3, D)

    # tail: the 2D tail alone (a layered attachment next to it is incomplete)
    for index in range(1, count):
        attach(0x8D40, 0x8CE0 + index, 0, 0)
    gl('glFramebufferTexture2D', None, U, U, U, U, I)(0x8D40, 0x8CE0, 0x0DE1, tail, 0)
    draw_buffers(1, (U * 1)(0x8CE0))
    gl('glUniform1i', None, I, I)(slice_loc, -1)
    draw(4, 0, 3, 1)
    delete_buffers(buffers)

    gl('glBindFramebuffer', None, U, U)(0x8D40, 0)
    gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glDeleteVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    return media_tex, outputs, tail


def injection(inject, media, rgb):
    count = W * H * D * 4
    black, baked = texture(1, 1, 1, [0] * 4), texture(1, 1, 1, [2, 3, 4, 0])
    media_tex, outputs, tail = run_compute(inject, media, {'black': black, 'u_VolumetricStaticGrid': baked}, False)
    medium = read(media_tex, count)
    assert all(abs(medium[i] - .01) < .00001 for i in range(0, len(medium), 4))
    expected = [.005, .015, .03, .01] if rgb else [.01, .015, .02, .01]
    for i, value in enumerate(read(outputs[0], count)):
        assert abs(value - expected[i % 4]) < .00003, (rgb, i, value, expected[i % 4])
    for i, value in enumerate(read(outputs[2], count)):
        assert abs(value - [2, 3, 4, 1][i % 4]) < .00003, (i, value)
    data = read2d(tail)
    assert gl('glGetError', U)() == 0, 'tail readback GL error'
    assert all(abs(value - [2, 3, 4, 1][i % 4]) < .00003 for i, value in enumerate(data)), data[:28]
    if rgb:
        for i, value in enumerate(read(outputs[3], count)):
            assert abs(value - [.005, .01, .015, .01][i % 4]) < .00003
    assert gl('glGetError', U)() == 0
    print(f'PASS: {"RGB" if rgb else "scalar"} injection, media kernel, tail, particle lighting, partial workgroups')


def pseudo_random(count, seed, low, high):
    values, state = [], seed
    for _ in range(count):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        values.append(low + (high - low) * state / 0x7FFFFFFF)
    return values


def raster_compute_match(inject, media, raster, rgb):
    """Same inputs through both paths: temporal history, jitter, reprojection, a varying light grid."""
    count = W * H * D * 4
    history = pseudo_random(count, 7, .001, .05)
    textures = {
        'black': texture(1, 1, 1, [0] * 4),
        'u_VolumetricStaticGrid': texture(3, 3, 3, pseudo_random(27 * 4, 3, .5, 4)),
        'u_FroxelHistory': texture(W, H, D, history),
        'u_FroxelExtinction': texture(W, H, D, pseudo_random(count, 11, .002, .02)),
    }
    names = ['media', 'inject', 'dynamic', 'particle light', 'sigma_t'][:5 if rgb else 4] + ['tail']
    results = []
    for media_tex, outputs, tail in [run_compute(inject, media, textures, True),
                                     run_raster(raster, textures, True, rgb)]:
        results.append([read(media_tex, count)] + [read(tex, count) for tex in outputs[:len(names) - 2]] +
                       [read2d(tail)])
    assert gl('glGetError', U)() == 0
    changed = sum(abs(a - b) > 1e-3 for a, b in zip(results[0][1], read(textures['u_FroxelHistory'], count)))
    assert changed > count // 4, ('the history had no effect', changed)
    for name, computed, rastered in zip(names, results[0], results[1]):
        for i, (a, b) in enumerate(zip(computed, rastered)):
            assert math.isfinite(a) and abs(a - b) <= 1e-5 + 2e-3 * abs(b), (name, rgb, i, a, b)
    print(f'PASS: {"RGB" if rgb else "scalar"} raster and compute injection match (history, jitter, reprojection)')


def main():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'Volumetric compute test', 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    context = SDL.SDL_GL_CreateContext(window)
    assert context, SDL.SDL_GetError()
    try:
        print(gl('glGetString', C.c_char_p, U)(0x1F02).decode())
        permutations = 0
        for rgb in [False, True]:
            for shadows in [False, True]:
                programs = {(name, compute): program(name, compute, rgb, shadows)
                            for name in ['volumetric_inject', 'volumetric_integrate'] for compute in [False, True]}
                media = program('volumetric_inject', True, rgb, shadows, media=True)
                permutations += len(programs) + 1
                if not shadows:
                    for depth, far_depth in [(17, 128), (16, 32), (32, 4096), (48, 4096),
                                             (64, 4096), (128, 65536)]:
                        integration(programs['volumetric_integrate', True], rgb, depth, far_depth)
                    injection(programs['volumetric_inject', True], media, rgb)
                    raster_compute_match(programs['volumetric_inject', True], media,
                                         programs['volumetric_inject', False], rgb)
                for prog in list(programs.values()) + [media]:
                    gl('glDeleteProgram', None, U)(prog)
        print(f'PASS: {permutations} raster/compute/media shader permutations compiled and linked')
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()


if __name__ == '__main__':
    main()
