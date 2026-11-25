"""Hidden GL checks of the rend2 FX upgrade programs (docs/rend2-fx-upgrades.md).

Run: python tools/rend2/test_fx_upgrades_gl.py   (NVIDIA: set SHIM_MCCOMPAT=0x800000001 first)
Compiles and links the production sources of fx_distortion (offset + composite),
fx_particles (draw) and fx_particles_sim (GL 4.3 compute), then runs the simulation
kernel: spawning into the ring, gravity, and a bounce off a depth buffer plane.
No game window, input capture or audio.
"""
import ctypes as C
import math
import struct

from test_volumetric_compute import ROOT, SDL, U, I, F, P, gl

GLSL = ROOT / 'shared/rd-rend2/glsl'


def stage(name, marker):
    text = (GLSL / f'{name}.glsl').read_text()
    return text.split(marker)[1].split('/*[')[0]


def link(sources, outputs=(b'out_Color',)):
    program = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(text.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(16384)
        gl('glGetShaderiv', None, U, U, P)(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, log.value.decode()
        if log.value.strip():
            print('  log:', log.value.decode().strip()[:400])
        gl('glAttachShader', None, U, U)(program, shader)
        gl('glDeleteShader', None, U)(shader)
    if sources[0][0] != 0x91B9:
        for i, name in enumerate(outputs):
            gl('glBindFragDataLocation', None, U, U, C.c_char_p)(program, i, name)
    gl('glLinkProgram', None, U)(program)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetProgramiv', None, U, U, P)(program, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(program, len(log), None, log)
    assert ok.value, log.value.decode()
    return program


def raster(name, defines=''):
    head = '#version 150 core\n' + defines
    return link([(0x8B31, head + stage(name, '/*[Vertex]*/')), (0x8B30, head + stage(name, '/*[Fragment]*/'))])


def uniform4(program, name, rows):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(program, name.encode())
    flat = [v for row in rows for v in row]
    gl('glUniform4fv', None, I, I, P)(loc, len(rows), (F * len(flat))(*flat))


def matrices(program, mats):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(program, b'u_FxMatrix')
    flat = [v for m in mats for v in m]
    gl('glUniformMatrix4fv', None, I, I, U, P)(loc, len(mats), 0, (F * len(flat))(*flat))


def invert(m):
    # column-major 4x4 general inverse via Gauss-Jordan
    a = [[m[c * 4 + r] for c in range(4)] + [1.0 if r == k else 0.0 for k in range(4)] for r in range(4)]
    for col in range(4):
        piv = max(range(col, 4), key=lambda r: abs(a[r][col]))
        a[col], a[piv] = a[piv], a[col]
        d = a[col][col]
        a[col] = [v / d for v in a[col]]
        for r in range(4):
            if r != col:
                f = a[r][col]
                a[r] = [x - f * y for x, y in zip(a[r], a[col])]
    return [a[r][4 + c] for c in range(4) for r in range(4)]


def simulation_check(sim):
    """Camera at the origin looking down -z (GL convention), floor plane at z = -100
    in front of it (depth texture), one spark moving toward the floor: it must
    reflect (velocity sign flips) and stay in front of the plane."""
    n, far = 4.0, 4096.0
    a, b = -(far + n) / (far - n), -2 * far * n / (far - n)
    proj = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, -1, 0, 0, b, 0]  # view = world
    inv = invert(proj)
    floor_depth = ((a * -100.0 + b) / 100.0) * 0.5 + 0.5
    W = 8
    tex = U()
    gl('glGenTextures', None, I, P)(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    data = (F * (W * W))(*([floor_depth] * (W * W)))
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x8CAC, W, W, 0, 0x1902, 0x1406, data)
    for pname, value in [(0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)]:
        gl('glTexParameteri', None, U, U, I)(0x0DE1, pname, value)

    pool = 64
    out = U()
    gl('glGenTextures', None, I, P)(1, C.byref(out))
    gl('glBindTexture', None, U, U)(0x0DE1, out)
    gl('glTexStorage2D', None, U, I, U, I, I)(0x0DE1, 1, 0x8814, pool, 4)
    gl('glBindTexture', None, U, U)(0x0DE1, tex)

    record = struct.calcsize('16f')
    bufs = (U * 2)()
    gl('glGenBuffers', None, I, P)(2, bufs)
    gl('glBindBuffer', None, U, U)(0x90D2, bufs[0])
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, pool * record, (C.c_char * (pool * record))(), 0x88EA)
    # one spark at z = -95 moving down the view axis toward the floor
    spark = struct.pack('16f', 0, 0, -95, 2.0, 0, 0, -300, 2.0, 1, 1, 1, 0, 1, 1, 0.5, 0.25)
    gl('glBindBuffer', None, U, U)(0x90D2, bufs[1])
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, record, spark, 0x88E4)

    gl('glUseProgram', None, U)(sim)
    gl('glUniform1i', None, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(sim, b'u_ScreenDepthMap'), 0)
    matrices(sim, [proj, inv])

    def step(head, count, dt, gravity=0.0):
        uniform4(sim, 'u_FxParams', [(head, count, pool, dt), (0.0, 0.0, 1.0, gravity), (0, 0, a, b), (0, 0, 0, 0)])
        gl('glBindBufferBase', None, U, U, U)(0x90D2, 0, bufs[0])
        gl('glBindBufferBase', None, U, U, U)(0x90D2, 1, bufs[1])
        gl('glBindImageTexture', None, U, U, I, U, I, U, U)(0, out, 0, 0, 0, 0x88B9, 0x8814)
        gl('glDispatchCompute', None, U, U, U)(1, 1, 1)
        gl('glMemoryBarrier', None, U)(0xFFFFFFFF)

    def read(slot):
        gl('glBindBuffer', None, U, U)(0x90D2, bufs[0])
        raw = (C.c_char * record)()
        gl('glGetBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(0x90D2, slot * record, record, raw)
        return struct.unpack('16f', raw.raw)

    step(5, 1, 0.0)   # spawn into ring slot 5
    s = read(5)
    assert abs(s[2] + 95) < 1e-3 and abs(s[6] + 300) < 1e-3, s
    assert read(4)[3] == 0.0 and read(6)[3] == 0.0, 'only the ring range is written'
    for _ in range(6):
        step(5, 0, 1.0 / 60.0)
    s = read(5)
    assert s[6] > 0.0, f'the spark must bounce off the floor: vz={s[6]}'
    assert s[2] > -100.0, f'the spark must stay in front of the floor: z={s[2]}'
    assert 1.8 < s[3] < 2.0, f'life must count down: {s[3]}'
    # gravity: a free particle accelerates along -z (world up is +z in the kernel)
    step(9, 1, 0.0)
    gl('glBindBuffer', None, U, U)(0x90D2, bufs[1])
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, record,
        struct.pack('16f', 0, 0, -50, 1.0, 0, 0, 0, 1.0, 1, 1, 1, 0, 1, 1, 0.5, 0.0), 0x88E4)
    step(9, 1, 0.0)
    step(9, 0, 0.1, 800.0)
    s = read(9)
    assert s[6] < -50.0, f'gravity: vz={s[6]}'
    print('  simulation: spawn ring, bounce and gravity OK')


def main():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'FX upgrades test', 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    context = SDL.SDL_GL_CreateContext(window)
    assert context, SDL.SDL_GetError()
    renderer = gl('glGetString', C.c_char_p, U)(0x1F01)
    print('GL_RENDERER:', renderer.decode())
    raster('fx_distortion')
    raster('fx_distortion', '#define USE_COMPOSITE\n')
    print('  fx_distortion offset + composite: linked')
    raster('fx_particles')
    print('  fx_particles: linked')
    sim = link([(0x91B9, '#version 430 core\n' + stage('fx_particles_sim', '/*[Compute]*/'))])
    print('  fx_particles_sim: linked')
    simulation_check(sim)
    SDL.SDL_GL_DeleteContext(context)
    SDL.SDL_DestroyWindow(window)
    print('PASS')


if __name__ == '__main__':
    main()
