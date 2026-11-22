"""Real generic fragment shader: hidden GL checks of soft/near particles and blends.

No game window, input capture, or audio. Tests the production shader, not a copy.
"""
import ctypes as C
from test_volumetric_compute import ROOT, SDL, U, I, F, P, gl, fragment
from test_waterparticles_gl import Target, texture2d

N, FAR = 4.0, 4096.0
A, B = -(FAR + N) / (FAR - N), -2 * FAR * N / (FAR - N)


def window_depth(z):
    return (B / z - A + 1.0) * 0.5


def program(enabled):
    vertex = '''
    uniform float depth;
    out vec2 var_DiffuseTex;
    out vec4 var_Color;
    out float var_LeafFlutter;
    void main() {
      vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
      gl_Position = vec4(p * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
      var_DiffuseTex = p; var_Color = vec4(1); var_LeafFlutter = 0;
    }'''
    p = gl('glCreateProgram', U)()
    for kind, body in [(0x8B31, vertex), (0x8B30, fragment('generic'))]:
        s = gl('glCreateShader', U, U)(kind)
        defines = '#define USE_PARTICLE_FADE\n' if enabled else ''
        src = C.c_char_p(('#version 150 core\n' + defines + body).encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(s, 1, C.byref(src), None)
        gl('glCompileShader', None, U)(s)
        ok, log = I(), C.create_string_buffer(8192)
        gl('glGetShaderiv', None, U, U, P)(s, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(s, len(log), None, log)
        assert ok.value, log.value.decode()
        gl('glAttachShader', None, U, U)(p, s)
        gl('glDeleteShader', None, U)(s)
    for i, name in enumerate([b'out_Color', b'out_Glow']):
        gl('glBindFragDataLocation', None, U, U, C.c_char_p)(p, i, name)
    gl('glLinkProgram', None, U)(p)
    ok, log = I(), C.create_string_buffer(8192)
    gl('glGetProgramiv', None, U, U, P)(p, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(p, len(log), None, log)
    assert ok.value, log.value.decode()
    return p


def uniform(p, name, values):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(p, name.encode())
    gl('glUniform%dfv' % len(values), None, I, I, P)(loc, 1, (F * len(values))(*values))


def depth_texture(z):
    tex = U()
    gl('glGenTextures', None, I, P)(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    data = (F * (256 * 256))(*([window_depth(z)] * (256 * 256)))
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x822E, 256, 256, 0, 0x1903, 0x1406, data)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2801, 0x2600)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2800, 0x2600)
    return tex


def draw(p, target, particle_z, scene_z, mode, near, soft, blend):
    target.clear()
    gl('glUseProgram', None, U)(p)
    uniform(p, 'depth', [window_depth(particle_z)])
    uniform(p, 'u_WeatherImpactClass', [-1])
    uniform(p, 'u_ParticleFade', [mode, near, soft, 0])
    uniform(p, 'u_ParticleDepth', [A, B, N, 0])
    for unit, name, tex in [(0, 'u_DiffuseMap', texture2d(1, 1, [0.6, 0.4, 0.2, 0.5])),
                            (1, 'u_ScreenDepthMap', depth_texture(scene_z))]:
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(0x0DE1, tex)
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(p, name.encode())
        gl('glUniform1i', None, I, I)(loc, unit)
    gl('glViewport', None, I, I, I, I)(0, 0, 256, 256)
    gl('glEnable', None, U)(0x0BE2)
    gl('glBlendFunc', None, U, U)(*blend)
    gl('glDrawArrays', None, U, I, I)(0x0004, 0, 3)
    data = target.read(0)
    return list(data[(128 * 256 + 128) * 4:(128 * 256 + 128) * 4 + 3])


def main():
    assert SDL.SDL_Init(0x20) == 0
    SDL.SDL_GL_SetAttribute(17, 3)
    SDL.SDL_GL_SetAttribute(18, 2)
    SDL.SDL_GL_SetAttribute(21, 1)
    win = SDL.SDL_CreateWindow(b'particle shader tests', 0, 0, 32, 32, 0x2 | 0x8)
    ctx = SDL.SDL_GL_CreateContext(win)
    assert ctx, SDL.SDL_GetError()
    vao = U()
    gl('glGenVertexArrays', None, I, P)(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    target = Target()
    p = program(True)
    program(False)  # ordinary generic materials have no particle dependency
    count = 0
    for mode, blend in [(1, (0x0302, 0x0303)), (2, (1, 0x0303)),
                        (1, (0x0302, 1)), (2, (1, 1))]:
        for particle_z, scene_z, near, soft in [(32, 32, 0, 0.125), (32, 34, 0, 0.125),
                (32, 36, 0, 0.125), (32, 48, 0, 0.125), (32, 30, 0, 0.125),
                (4, 64, 16, 0), (8, 64, 16, 0), (12, 64, 16, 0),
                (20, 64, 16, 0), (12, 16, 16, 0.125), (32, FAR, 0, 0.125)]:
            fade = min(1, max(0, (scene_z - particle_z) * soft)) if soft else 1
            t = min(1, max(0, (particle_z - N) / near)) if near else 1
            fade *= t * t * (3 - 2 * t)
            alpha = 0.5 * fade
            rgb = [v * (fade if mode == 2 else 1) for v in (0.6, 0.4, 0.2)]
            expected = [v * (alpha if blend[0] == 0x0302 else 1) + bg *
                        (1 - alpha if blend[1] == 0x0303 else 1)
                        for v, bg in zip(rgb, (0.2, 0.3, 0.4))]
            actual = draw(p, target, particle_z, scene_z, mode, near, soft, blend)
            assert max(abs(a-b) for a, b in zip(actual, expected)) < 0.004, (mode, particle_z, scene_z, actual, expected)
            count += 1
    assert gl('glGetError', U)() == 0
    print('PASS: %d production-shader intersection/near/blend cases; ordinary generic compiles' % count)
    SDL.SDL_GL_DeleteContext(ctx)
    SDL.SDL_DestroyWindow(win)
    SDL.SDL_Quit()


if __name__ == '__main__':
    main()
