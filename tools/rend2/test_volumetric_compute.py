"""GPU regression checks using a hidden SDL GL context (Windows, bundled SDL2).

Run from any directory: python tools/rend2/test_volumetric_compute.py
Compiles actual raster/compute sources, then checks column integration against
Beer-Lambert on a grid with partial workgroups, including zero extinction.
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


def program(name, compute, rgb, shadows):
    constants = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
    defines = ''.join(f'#define {key} {value}\n' for key, value in re.findall(
        r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', constants, re.M))
    defines += '#define MAX_DLIGHTS 32\n#define M_PI 3.14159265358979323846\n#define USE_FROXEL_FOG\n'
    if name == 'volumetric_inject':
        defines += '#define USE_FROXEL_NOISE\n#define USE_FROXEL_PARTICLES\n'
    defines += '#define USE_FROXEL_RGB\n' if rgb else ''
    defines += '#define USE_SHADOWS2\n' if shadows else ''
    defines += '#define USE_FROXEL_COMPUTE\n' if compute else ''
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
        assert ok.value, (name, compute, rgb, shadows, log.value.decode())
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


def integration(prog, rgb):
    width, height, depth = 7, 5, 17
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
        'u_FroxelSliceParams': [8, 128, 4, 128],
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
                near = 8 * 2 ** (4 * z / depth) if z else 0
                far = 8 * 2 ** (4 * (z + 1) / depth)
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
    print(f'PASS: {"RGB" if rgb else "scalar"} integration, zero extinction, partial workgroups, all 17 slices')


def injection(prog, rgb):
    width, height, depth = 7, 5, 17
    gl('glUseProgram', None, U)(prog)
    buffers = [uniform_buffer(prog, 'VolumetricFog', {
        'u_FroxelGridSize': [width, height, depth, 0],
        'u_FroxelSliceParams': [8, 128, 4, 128],
        'u_FroxelRayForward': [0, 0, 1, 0],
        'u_FroxelRayRight': [1, 0, 0, 0],
        'u_FroxelRayUp': [0, 1, 0, 0],
        'u_FroxelLightParams': [0, 1, 1, 1],
        'u_FroxelHeightFog': [.01, 0, 0, 0],
        'u_FroxelHeightFogColor': [.5, .5, .5, 0],
        'u_FroxelHeightFogTop': [0, .5, 1, 1.5],
    })]
    for slot, block in enumerate(['Lights', 'VolumetricParticles'], 1):
        buffers.append(uniform_buffer(prog, block, {}, slot))
    # Give every sampler a distinct unit, as the renderer does. Disabled
    # shadow/cookie/light-list branches do not dereference their textures.
    samplers = ['u_FroxelHistory', 'u_VolumetricStaticGrid', 'u_VolumetricSunGrid',
                'u_VolumetricDirGrid', 'u_VolumetricDirVecGrid', 'u_VolumetricLegacyGrid',
                'u_ShadowMap', 'u_ShadowMap2', 'u_FroxelNoise', 'u_FroxelMedia',
                'u_FroxelExtinction', 'u_FPlusLights', 'u_FPlusGridMap', 'u_LightCookieMap']
    black, baked = texture(1, 1, 1, [0] * 4), texture(1, 1, 1, [2, 3, 4, 0])
    for unit, name in enumerate(samplers):
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
        gl('glUniform1i', None, I, I)(loc, unit)
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(0x806F, baked if name == 'u_VolumetricStaticGrid' else black)
    outputs = [texture(width, height, depth, internal=fmt) for fmt in [0x881A, 0x8C3A, 0x8C3A, 0x881A]]
    media = texture(width, height, depth, internal=0x822D)
    tail = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tail))
    gl('glBindTexture', None, U, U)(0x0DE1, tail)
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x881A, width, height, 0, 0x1908, 0x1406, None)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2801, 0x2600)
    for unit, tex, fmt, layered in [(i, tex, fmt, 1) for i, (tex, fmt) in enumerate(zip(outputs, [0x881A, 0x8C3A, 0x8C3A, 0x881A]))] + [(4, tail, 0x881A, 0), (5, media, 0x822D, 1)]:
        gl('glBindImageTexture', None, U, U, I, U, I, U, U)(unit, tex, 0, layered, 0, 0x88B9, fmt)
    particle = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_ParticleLight')
    gl('glUniform4f', None, I, F, F, F, F)(particle, 0, 0, 1, 0)
    gl('glDispatchCompute', None, U, U, U)(2, 2, 5)
    gl('glMemoryBarrier', None, U)(0x28)
    medium = read(media, width * height * depth * 4)
    assert all(abs(medium[i] - .01) < .00001 for i in range(0, len(medium), 4))
    gl('glUniform4f', None, I, F, F, F, F)(particle, 1, 0, 0, 0)
    gl('glDispatchCompute', None, U, U, U)(2, 2, 5)
    gl('glMemoryBarrier', None, U)(0x28)
    expected = [.005, .015, .03, .01] if rgb else [.01, .015, .02, .01]
    for i, value in enumerate(read(outputs[0], len(medium))):
        assert abs(value - expected[i % 4]) < .00003, (rgb, i, value, expected[i % 4])
    for i, value in enumerate(read(outputs[2], len(medium))):
        assert abs(value - [2, 3, 4, 1][i % 4]) < .00003, (i, value)
    gl('glMemoryBarrier', None, U)(0x100)
    gl('glBindTexture', None, U, U)(0x0DE1, tail)
    data = (F * (width * height * 4))()
    gl('glGetTexImage', None, U, I, U, U, P)(0x0DE1, 0, 0x1908, 0x1406, data)
    assert gl('glGetError', U)() == 0, 'tail readback GL error'
    assert all(abs(value - [2, 3, 4, 1][i % 4]) < .00003 for i, value in enumerate(data)), list(data)[:28]
    if rgb:
        for i, value in enumerate(read(outputs[3], len(medium))):
            assert abs(value - [.005, .01, .015, .01][i % 4]) < .00003
    assert gl('glGetError', U)() == 0
    for buffer in buffers:
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
    print(f'PASS: {"RGB" if rgb else "scalar"} injection, media, tail, particle lighting, partial workgroups')


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
        for rgb in [False, True]:
            for shadows in [False, True]:
                for name in ['volumetric_inject', 'volumetric_integrate']:
                    for compute in [False, True]:
                        prog = program(name, compute, rgb, shadows)
                        if compute and name == 'volumetric_integrate' and not shadows:
                            integration(prog, rgb)
                        if compute and name == 'volumetric_inject' and not shadows:
                            injection(prog, rgb)
                        gl('glDeleteProgram', None, U)(prog)
        print('PASS: 16 raster/compute shader permutations compiled and linked')
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()


if __name__ == '__main__':
    main()
