"""GPU checks of the underwater particles (r_waterParticles, glsl/waterparticles.glsl, tr_waterparticles.cpp).

Run from any directory: python tools/rend2/test_waterparticles_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)

Hidden SDL GL context (bundled SDL2), the real source with its geometry stage:
- compiles and links the program as the renderer does: without the froxel fog, with it (volumetric_common.glsl
  spliced into the fragment stage) and with RGB extinction
- draws points into an RGBA16F color + glow target with the renderer's blend (ONE, ONE_MINUS_SRC_ALPHA) and
  compares every pixel with a CPU reference: the procedural speck, the bubble texture branch, the near fade, the
  far fade at the volume radius, the sub-pixel clamp (opacity traded for area, so the integrated opacity keeps
  the true area), the procedural bubble ring without the texture, the froxel medium in front (color * T + S * alpha, mode 1), mode 0 (no fog), the placement
  debug view, and the glow target left untouched
"""
import ctypes as C
import math
import struct
import sys

from test_volumetric_compute import ROOT, SDL, U, I, F, P, gl, fragment, uniform_buffer

SIZE = 256
BACKGROUND = (0.2, 0.3, 0.4, 1.0)
GLOW = (0.05, 0.1, 0.15, 0.5)
NEAR = 1.0
FAR = 1000.0


def stage(name, kind):
    return (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text().split(f'/*[{kind}]*/')[1].split('/*[')[0]


def program(froxel, rgb):
    constants = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
    import re
    defines = ''
    if froxel:
        defines = ''.join(f'#define {key} {value}\n' for key, value in re.findall(
            r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', constants, re.M))
        defines += '#define MAX_DLIGHTS 32\n#define M_PI 3.14159265358979323846\n#define USE_FROXEL_FOG\n'
        defines += '#define r_FBufScale vec2(1.0 / 7.0, 1.0 / 5.0)\n'
        defines += '#define USE_FROXEL_RGB\n' if rgb else ''
    # GLSL_LoadGPUShader: the fragment library goes in front of the fragment stage
    frag = (fragment('volumetric_common') if froxel else '') + stage('waterparticles', 'Fragment')
    sources = [(0x8B31, stage('waterparticles', 'Vertex')), (0x8DD9, stage('waterparticles', 'Geometry')),
               (0x8B30, frag)]
    prog = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(('#version 150 core\n' + defines + text).encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(16384)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, (froxel, rgb, kind, log.value.decode())
        gl('glAttachShader', None, U, U)(prog, shader)
        gl('glDeleteShader', None, U)(shader)
    gl('glBindAttribLocation', None, U, U, C.c_char_p)(prog, 0, b'attr_Position')
    gl('glBindAttribLocation', None, U, U, C.c_char_p)(prog, 1, b'attr_Color')
    gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, 0, b'out_Color')
    gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, 1, b'out_Glow')
    gl('glLinkProgram', None, U)(prog)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(prog, len(log), None, log)
    assert ok.value, log.value.decode()
    return prog


def texture2d(width, height, rgba):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    data = (F * (width * height * 4))(*(rgba * (width * height)))
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, 0x881A, width, height, 0, 0x1908, 0x1406, data)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2801, 0x2601)
    gl('glTexParameteri', None, U, U, I)(0x0DE1, 0x2800, 0x2601)
    return tex


def texture3d(size, rgba):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x806F, tex)
    data = (F * (size ** 3 * 4))(*(rgba * size ** 3))
    gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(0x806F, 0, 0x881A, size, size, size, 0, 0x1908, 0x1406, data)
    gl('glTexParameteri', None, U, U, I)(0x806F, 0x2801, 0x2601)
    gl('glTexParameteri', None, U, U, I)(0x806F, 0x2800, 0x2601)
    return tex


class Target:
    def __init__(self):
        self.fbo = U()
        gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(self.fbo))
        gl('glBindFramebuffer', None, U, U)(0x8D40, self.fbo)
        self.textures = []
        for attachment in range(2):
            tex = texture2d(SIZE, SIZE, [0.0, 0.0, 0.0, 0.0])
            gl('glFramebufferTexture2D', None, U, U, U, U, I)(0x8D40, 0x8CE0 + attachment, 0x0DE1, tex, 0)
            self.textures.append(tex)
        bufs = (U * 2)(0x8CE0, 0x8CE1)
        gl('glDrawBuffers', None, I, P)(2, bufs)
        assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5

    def clear(self):
        gl('glBindFramebuffer', None, U, U)(0x8D40, self.fbo)
        for index, value in enumerate((BACKGROUND, GLOW)):
            gl('glClearBufferfv', None, U, I, P)(0x1800, index, (F * 4)(*value))

    def read(self, attachment):
        gl('glBindTexture', None, U, U)(0x0DE1, self.textures[attachment])
        data = (F * (SIZE * SIZE * 4))()
        gl('glGetTexImage', None, U, I, U, U, P)(0x0DE1, 0, 0x1908, 0x1406, data)
        return data


def projection():
    """camera at the origin looking +z, right +x, up +y, 90 degree fov (column major)"""
    a = (FAR + NEAR) / (FAR - NEAR)
    b = -2.0 * FAR * NEAR / (FAR - NEAR)
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, a, 1, 0, 0, b, 0]


SAMPLER_UNITS = ['u_DiffuseMap', 'u_FroxelVolume', 'u_FroxelTail', 'u_FroxelTransmittance', 'u_ParticleLightVolume']
PIXEL = 2.0 / SIZE			# world size of a pixel at distance 1 (2 tan(45) / height)
CAMERA_FAR = 4096.0	# camera block axis length at 90 degrees FOV
PARAMS = [PIXEL, 2.0, 10.0, 160.0, 0.2, 0.0, 0.75, 1.0]


def draw(prog, target, points, params=PARAMS, fog_mode=None, textures=None):
    """points: (x, y, z, signed radius, r, g, b, a)"""
    target.clear()
    gl('glUseProgram', None, U)(prog)
    buffers = [uniform_buffer(prog, 'Camera', {
        'u_viewProjectionMatrix': projection(), 'u_ViewOrigin': [0, 0, 0], 'u_ViewForward': [0, 0, 1],
        # RB_UpdateCameraConstants scales the axes by zFar * tan(fov / 2)
        'u_ViewLeft': [-CAMERA_FAR, 0, 0], 'u_ViewUp': [0, CAMERA_FAR, 0]}, 0)]
    if fog_mode is not None:
        # froxel camera: clip = (x, y, 0, z), forward +z; slices 8..128 over the volume
        buffers.append(uniform_buffer(prog, 'VolumetricFog', {
            'u_FroxelViewProjection': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0],
            'u_FroxelViewOrigin': [0, 0, 0, 1], 'u_FroxelViewForward': [0, 0, 1, 0],
            'u_FroxelGridSize': [8, 8, 8, 0], 'u_FroxelSliceParams': [8, 128, 4, 128]}, 1))
        gl('glUniform1i', None, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_FroxelFogMode'), fog_mode)
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_WaterParticles')
    gl('glUniform4fv', None, I, I, P)(loc, 2, (F * 8)(*params))
    # one unit per sampler, as GLSL_SetFroxelLookupUnits: samplers of different types on one unit make
    # the draw an error on NVIDIA (u_FroxelTransmittance of the RGB build is declared even when unused)
    for unit, name in enumerate(SAMPLER_UNITS):
        gl('glUniform1i', None, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode()), unit)
    for name, tex, kind in textures or []:
        gl('glActiveTexture', None, U)(0x84C0 + SAMPLER_UNITS.index(name))
        gl('glBindTexture', None, U, U)(kind, tex)
    gl('glActiveTexture', None, U)(0x84C0)

    vao, vbo = U(), U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(vbo))
    gl('glBindBuffer', None, U, U)(0x8892, vbo)
    flat = [v for p in points for v in p]
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8892, 4 * len(flat), (F * len(flat))(*flat), 0x88E0)
    for index in range(2):
        gl('glEnableVertexAttribArray', None, U)(index)
        gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(index, 4, 0x1406, 0, 32, index * 16)
    gl('glViewport', None, I, I, I, I)(0, 0, SIZE, SIZE)
    gl('glEnable', None, U)(0x0BE2)
    gl('glBlendFunc', None, U, U)(1, 0x0303)
    gl('glDrawArrays', None, U, I, I)(0, 0, len(points))
    gl('glDisable', None, U)(0x0BE2)
    gl('glFinish', None)()
    gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(vbo))
    gl('glDeleteVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    for buffer in buffers:
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
    return target.read(0), target.read(1)


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def reference(points, params=PARAMS, fog=None, bubble_texel=None):
    """CPU image of the draw (the geometry and fragment stages, pixel centers), fog = (S rgb, T)"""
    image = [list(BACKGROUND) for _ in range(SIZE * SIZE)]
    for x, y, z, signed_radius, r, g, b, alpha in points:
        radius = abs(signed_radius)
        distance = math.sqrt(x * x + y * y + z * z)
        alpha *= smoothstep(params[1], params[2], distance)
        alpha *= 1.0 - smoothstep(params[3] * (1.0 - params[4]), params[3], distance)
        min_radius = params[6] * distance * params[0]
        if radius < min_radius:
            alpha *= (radius / min_radius) ** 2
            radius = min_radius
        if alpha < 1.0 / 1024.0:
            continue
        # screen: ndc = (x / z, y / z), pixel = (ndc * 0.5 + 0.5) * SIZE; the quad is axis aligned
        cx, cy = (x / z * 0.5 + 0.5) * SIZE, (y / z * 0.5 + 0.5) * SIZE
        rp = radius / z * 0.5 * SIZE
        for py in range(max(0, int(cy - rp) - 1), min(SIZE, int(cy + rp) + 2)):
            for px in range(max(0, int(cx - rp) - 1), min(SIZE, int(cx + rp) + 2)):
                qx, qy = (px + 0.5 - cx) / rp, (py + 0.5 - cy) / rp
                if abs(qx) > 1.0 or abs(qy) > 1.0:
                    continue
                r2 = qx * qx + qy * qy
                if r2 >= 1.0:
                    continue
                color = [r, g, b]
                if signed_radius < 0.0 and params[7] > 0.5:
                    coverage = bubble_texel[3]
                    color = [color[i] * bubble_texel[i] for i in range(3)]
                elif signed_radius < 0.0:
                    rim = (math.sqrt(r2) - 0.78) / 0.14
                    coverage = (0.12 + 0.88 * math.exp(-rim * rim)) * (1.0 - r2 ** 4)
                else:
                    coverage = math.exp(-3.0 * r2) * (1.0 - r2)
                a = coverage * alpha
                src = [c * a for c in color]
                if fog is not None:
                    src = [src[i] * fog[1] + fog[0][i] * a for i in range(3)]
                dst = image[py * SIZE + px]
                image[py * SIZE + px] = [src[0] + dst[0] * (1 - a), src[1] + dst[1] * (1 - a),
                                         src[2] + dst[2] * (1 - a), a + dst[3] * (1 - a)]
    return image


def compare(label, got, expected, tolerance=4e-3, edge=None):
    """per pixel; pixels whose center lies within edge (pixels) of a quad border may go either way in raster"""
    worst = 0.0
    for i in range(SIZE * SIZE):
        e = expected[i]
        g = got[i * 4:i * 4 + 4]
        error = max(abs(g[c] - e[c]) for c in range(4))
        if error > tolerance and edge is not None and edge(i % SIZE, i // SIZE):
            continue
        worst = max(worst, error)
        assert error <= tolerance, (label, i % SIZE, i // SIZE, list(g), e)
    return worst


def border(points):
    """pixels near a quad border of the points (raster rounding there differs from the reference)"""
    boxes = []
    for x, y, z, signed_radius, *_ in points:
        radius = max(abs(signed_radius), PARAMS[6] * math.sqrt(x * x + y * y + z * z) * PIXEL)
        cx, cy = (x / z * 0.5 + 0.5) * SIZE, (y / z * 0.5 + 0.5) * SIZE
        boxes.append((cx, cy, radius / z * 0.5 * SIZE))
    def near(px, py):
        for cx, cy, rp in boxes:
            dx, dy = abs(px + 0.5 - cx), abs(py + 0.5 - cy)
            if max(dx, dy) <= rp + 1.0 and (abs(dx - rp) <= 1.0 or abs(dy - rp) <= 1.0):
                return True
        return False
    return near


def untouched_glow(label, glow):
    for i in range(0, SIZE * SIZE, 97):
        assert all(abs(glow[i * 4 + c] - GLOW[c]) < 1e-3 for c in range(4)), (label, i, list(glow[i * 4:i * 4 + 4]))


def cases(prog, froxel):
    target = Target()
    checks = 0
    white = texture2d(1, 1, [1.0, 1.0, 1.0, 1.0])
    bubble_texel = [0.8, 0.7, 0.9, 0.6]
    bubble = texture2d(4, 4, bubble_texel)
    textures = [('u_DiffuseMap', bubble, 0x0DE1)]

    # specks and a bubble at different depths, none overlapping
    points = [
        (0.0, 0.0, 40.0, 2.0, 1.0, 0.5, 0.25, 0.5),			# speck in the middle
        (-30.0, 20.0, 60.0, -3.0, 0.9, 1.0, 1.1, 0.45),		# bubble (texture branch)
        (20.0, -20.0, 30.0, 1.0, 0.3, 0.6, 0.9, 0.8),
        (0.0, 30.0, 144.0, 6.0, 1.0, 1.0, 1.0, 0.5),		# in the far fade band (half)
        (0.0, -40.0, 170.0, 6.0, 1.0, 1.0, 1.0, 1.0),		# beyond the volume radius: nothing
        (0.1, 0.1, 1.5, 0.5, 1.0, 0.0, 0.0, 1.0),			# inside the near fade: nothing
    ]
    color, glow = draw(prog, target, points, textures=textures)
    worst = compare('specks', color, reference(points, bubble_texel=bubble_texel), edge=border(points))
    untouched_glow('specks', glow)
    checks += 1

    # the sub-pixel clamp: specks under the minimum footprint are drawn at it with the opacity of their true
    # area, (r / min)^2: twice the radius gives four times the opacity at the same footprint. The speck sits
    # on a pixel center (coverage 1 there); the background is read back (RGBA16F rounds 0.2)
    target.clear()
    stored = target.read(0)[0]
    center = 100.0 * 0.5 / (0.5 * SIZE)
    deltas = []
    for radius in (0.05, 0.1):
        pts = [(center, center, 100.0, radius, 1.0, 1.0, 1.0, 1.0)]
        target_color, _ = draw(prog, target, pts, textures=textures)
        worst = max(worst, compare('sub-pixel', target_color, reference(pts), edge=border(pts)))
        index = (SIZE // 2) * SIZE + SIZE // 2
        deltas.append(target_color[index * 4] - stored)
    assert deltas[0] > 0.0 and 3.8 < deltas[1] / deltas[0] < 4.2, ('sub-pixel opacity', deltas)
    checks += 1

    # no bubble texture (u_WaterParticles[1].w 0): the procedural ring
    ring = list(PARAMS)
    ring[7] = 0.0
    color, _ = draw(prog, target, points[:3], params=ring, textures=textures)
    worst = max(worst, compare('procedural ring', color, reference(points[:3], params=ring), edge=border(points[:3])))
    checks += 1

    # placement debug view: opaque cyan specks, yellow bubbles
    debug = list(PARAMS)
    debug[5] = 1.0
    color, _ = draw(prog, target, points[:2], params=debug, textures=textures)
    for (x, y, z, r, *_), expected in zip(points[:2], ((0.1, 0.9, 1.0), (1.0, 0.9, 0.1))):
        px, py = int((x / z * 0.5 + 0.5) * SIZE), int((y / z * 0.5 + 0.5) * SIZE)
        got = color[(py * SIZE + px) * 4:(py * SIZE + px) * 4 + 4]
        assert all(abs(got[c] - expected[c]) < 2e-3 for c in range(3)) and abs(got[3] - 1.0) < 2e-3, ('debug', got)
    checks += 1

    if froxel:
        # a constant medium: the in-scattering S and transmittance T in front of every particle
        S, T = (0.06, 0.12, 0.18), 0.55
        volume = texture3d(8, [S[0], S[1], S[2], T])
        tail = texture2d(1, 1, [0.0, 0.0, 0.0, 0.0])
        fog_textures = textures + [('u_FroxelVolume', volume, 0x806F), ('u_FroxelTail', tail, 0x0DE1)]
        near_points = points[:3]
        color, glow = draw(prog, target, near_points, fog_mode=1, textures=fog_textures)
        worst = max(worst, compare('froxel fog', color, reference(near_points, fog=(S, T), bubble_texel=bubble_texel),
                                   edge=border(near_points)))
        untouched_glow('froxel fog', glow)
        # mode 0: no fog in front (no froxel view)
        color, _ = draw(prog, target, near_points, fog_mode=0, textures=fog_textures)
        worst = max(worst, compare('no fog', color, reference(near_points, bubble_texel=bubble_texel),
                                   edge=border(near_points)))
        # debug view 2: the fog skipped
        debug2 = list(PARAMS)
        debug2[5] = 2.0
        color, _ = draw(prog, target, near_points, params=debug2, fog_mode=1, textures=fog_textures)
        worst = max(worst, compare('debug 2', color, reference(near_points, bubble_texel=bubble_texel),
                                   edge=border(near_points)))
        checks += 3
        for tex in (volume, tail):
            gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    for tex in (white, bubble):
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    return checks, worst


def context(title):
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(title, 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    ctx = SDL.SDL_GL_CreateContext(window)
    assert ctx, SDL.SDL_GetError()
    print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '|', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
    return window, ctx


def main():
    window, ctx = context(b'Water particles test')
    try:
        checks = 0
        worst = 0.0
        for froxel, rgb in ((False, False), (True, False), (True, True)):
            prog = program(froxel, rgb)
            n, w = cases(prog, froxel)
            checks += n
            worst = max(worst, w)
            gl('glDeleteProgram', None, U)(prog)
        print(f'PASS: 3 waterparticles programs compiled and linked, {checks} raster checks, '
              f'worst pixel error {worst:.2e}')
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    sys.exit(main())
