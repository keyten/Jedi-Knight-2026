"""GPU checks of the modern water surface (glsl/watersurface.glsl, r_waterSurface) on a hidden SDL GL core context.

Run from any directory: python tools/rend2/test_watersurface_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)
GPU timings at 1920x1080: python tools/rend2/test_watersurface_gl.py --bench

- compiles and links every permutation as GLSL_LoadGPUProgramWaterSurface builds it: deform, USE_SHADOWS2, the SSR ray
  march library (USE_SSR), the froxel lookup library (scalar / RGB), cubemaps
- a synthetic opaque scene (shallow floor z = -16 for y < 0, deep floor z = -400 for y > 0 with a far shore wall, a
  pillar through the surface: red below, magenta above, sky beyond the shallow floor) is rendered into color + depth,
  the water program draws the plane z = 0 over it from those copies, as RB_WaterSurfacePrepare provides them
- refracted path length under the surface against the float64 reference H / cos(theta_t) (Snell, n = 1.333), shallow
  and deep; grazing views (< 7 degrees) are reported, not judged (the screen-space search is approximate there)
- reflection weight of the smooth surface (test LUT: F0 + (1 - F0) (1 - NV)^5, F0 of n = 1.333)
- sky behind the water: the longest path; transmittance: the shallow part much clearer than the deep part
- depth rejection: the part of the pillar above the water is never sampled under it (and is without the rejection)
- waves: the unresolved slope variance of the mips raises the roughness with distance, no NaN / Inf
- SSR (ssr_common.glsl ray march): the reflection rays find the pillar above the water, in its screen columns
- from inside the liquid: total internal reflection outside Snell's window, the scene above inside it
- r_waterSnell (USE_WATER_SNELL): TIR exactly beyond the critical angle (IOR 1.333 / 2), reflection weight = exact
  water -> air Fresnel (continuous, no mask), the scene above refracted into the window, TIR reflection of the liquid
  without SSR / cubemap and of the pillar with SSR (attenuated), unchanged from above, camera crossing the plane,
  steep ripples, IOR 1, the debug views
"""
import ctypes as C
import itertools
import math
from pathlib import Path
import sys
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
U, I, F, P = C.c_uint, C.c_int, C.c_float, C.c_void_p
SDL = C.CDLL(str(ROOT / 'lib/SDL2/bin/x64/SDL2.dll'))
for _name, _args, _result in [
    ('SDL_CreateWindow', [C.c_char_p, I, I, I, I, U], P),
    ('SDL_GL_CreateContext', [P], P),
    ('SDL_GL_GetProcAddress', [C.c_char_p], P),
    ('SDL_GL_DeleteContext', [P], None),
    ('SDL_DestroyWindow', [P], None),
    ('SDL_GetError', [], C.c_char_p),
]:
    _fn = getattr(SDL, _name)
    _fn.argtypes, _fn.restype = _args, _result

_gl_cache = {}


def gl(name, result, *args):
    key = (name, result, args)
    if key not in _gl_cache:
        address = SDL.SDL_GL_GetProcAddress(name.encode())
        assert address, name
        _gl_cache[key] = C.WINFUNCTYPE(result, *args)(address)
    return _gl_cache[key]


GLSL = ROOT / 'shared/rd-rend2/glsl'
GL_VERTEX, GL_FRAGMENT = 0x8B31, 0x8B30
TEX2D, TEX2D_ARRAY = 0x0DE1, 0x8C1A
RGBA, RED, FLOAT = 0x1908, 0x1903, 0x1406
RGBA32F, RGBA16F, R32F = 0x8814, 0x881A, 0x822E
DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8 = 0x88F0, 0x84F9, 0x84FA
FB, READ_FB, DRAW_FB = 0x8D40, 0x8CA8, 0x8CA9
WATER_VEC4S = 36
N_WATER = 1.333
MAX_PATH = 8192.0
W, H = 320, 240

# units of GLSL_LoadGPUProgramWaterSurface
UNITS = {'u_WaterSceneMap': 0, 'u_WaterDepthMap': 1, 'u_WaterNormalMap': 2, 'u_EnvBrdfMap': 3, 'u_CubeMap': 4,
         'u_ShadowMap': 5, 'u_FroxelVolume': 6, 'u_FroxelTail': 7, 'u_SSRHiZMap': 10, 'u_SSRSceneMap': 11,
         'u_FroxelTransmittance': 26, 'u_GlowMap': 9, 'u_SSRHistoryMap': 12,
         'u_SSRHistoryGeomMap': 14, 'u_SSRPrevHitMap': 15, 'u_WaterInteractionMap': 19}

# u_Water[6].z flags (RB_WaterSurfaceSetupDraw)
FLAG_CUBEMAP, FLAG_SUN, FLAG_FROXEL, FLAG_REJECT, FLAG_SSR, FLAG_ENVBRDF = 1, 2, 4, 8, 16, 512
FLAG_CAMERA_LIQUID = 1024


def read(name):
    return (GLSL / f'{name}.glsl').read_text().replace('\r\n', '\n')


def fragment(name):
    return read(name).split('/*[Fragment]*/')[1]


def header(defines, size):
    """the parts of GLSL_GetShaderHeader the water program uses"""
    text = ('#version 150 core\n#define M_PI 3.14159265358979323846\n'
            '#define DEFORM_NONE 0\n#define DEFORM_WAVE 1\n#define DEFORM_NORMALS 2\n#define DEFORM_BULGE 3\n'
            '#define DEFORM_BULGE_UNIFORM 4\n#define DEFORM_MOVE 5\n#define DEFORM_PROJECTION_SHADOW 6\n'
            '#define DEFORM_DISINTEGRATION 7\n#define WF_NONE 0\n#define WF_SIN 1\n#define WF_SQUARE 2\n'
            '#define WF_TRIANGLE 3\n#define WF_SAWTOOTH 4\n#define WF_INVERSE_SAWTOOTH 5\n'
            '#define MAX_G2_BONES 72\n#define MAX_GPU_FOGS 16\n#define MAX_DLIGHTS 32\n'
            f'#define r_FBufScale vec2({size[0]}.0, {size[1]}.0)\n#define USE_ALPHA_TEST\n')
    for d in defines:
        text += '#define ' + d.replace('=', ' ') + '\n'
    return text + f'#define WATER_UNIFORM_VEC4S {WATER_VEC4S}\n'


def water_sources(deform=False, shadows2=False, ssr=False, froxel=0, cubemap=False, hiz=False, size=(W, H), body=None,
                  snell=False):
    defines = ['USE_HIZ'] if hiz else []
    if snell:
        defines.append('USE_WATER_SNELL')
    if cubemap:
        defines += ['CUBEMAP_RESOLUTION=float(256)', 'ROUGHNESS_MIPS=float(6)']
    if froxel:
        defines += ['USE_FROXEL_FOG', 'MAX_GPU_FOG_VOLUMES=64', 'FROXEL_MAX_SLICES=128', 'FROXEL_LOCAL_POOL=2048',
                    'FROXEL_EXTINCTION_PALETTE=16', 'MAX_GPU_LIQUIDS=32']
        if froxel == 2:
            defines.append('USE_FROXEL_RGB')
    if ssr:
        defines.append('USE_SSR')
    if shadows2:
        defines.append('USE_SHADOWS2')
    if deform:
        defines.append('USE_DEFORM_VERTEXES')
    library = (fragment('volumetric_common') + '\n' if froxel else '') + (fragment('ssr_common') + '\n' if ssr else '')
    src = read('watersurface')
    vs, fs = src.split('/*[Fragment]*/')
    vs = vs.split('/*[Vertex]*/')[1]
    if body is not None:
        fs = body(fs)
    h = header(defines, size)
    common = read('water_surface_common')
    common_vs, common_fs = common.split('/*[Fragment]*/')
    common_vs = common_vs.split('/*[Vertex]*/')[1]
    return h + common_vs + vs, h + common_fs + library + fs


def compile_program(sources, label, attributes=(), outputs=('out_Color', 'out_Glow', 'out_SSRNormal')):
    prog = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(text.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(65536)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, (label, hex(kind), log.value.decode(errors='replace')[:4000])
        gl('glAttachShader', None, U, U)(prog, shader)
        gl('glDeleteShader', None, U)(shader)
    for index, name in enumerate(attributes):
        gl('glBindAttribLocation', None, U, U, C.c_char_p)(prog, index, name.encode())
    for index, output in enumerate(outputs):
        gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, index, output.encode())
    gl('glLinkProgram', None, U)(prog)
    ok, log = I(), C.create_string_buffer(65536)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(prog, len(log), None, log)
    assert ok.value, (label, log.value.decode(errors='replace')[:4000])
    return prog


def uloc(prog, name):
    return gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())


def water_program(**kw):
    vs, fs = water_sources(**kw)
    prog = compile_program([(GL_VERTEX, vs), (GL_FRAGMENT, fs)], 'watersurface ' + repr(kw),
                           attributes=('attr_Position', 'attr_Normal', 'attr_TexCoord0', 'attr_TexCoord1'))
    gl('glUseProgram', None, U)(prog)
    for sampler, unit in UNITS.items():
        loc = uloc(prog, sampler)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    return prog


def check(name, ok, detail=''):
    print(f'{"PASS" if ok else "FAIL"}: {name}{(" - " + detail) if detail else ""}')
    return ok


# --- GL objects ---------------------------------------------------------------------------------------------------

def tex2d(w, h, internal=RGBA32F, fmt=RGBA, kind=FLOAT, data=None, linear=False, repeat=False):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(TEX2D, tex)
    raw = np.ascontiguousarray(data, dtype=np.float32).ctypes.data_as(P) if data is not None else None
    gl('glPixelStorei', None, U, I)(0x0CF5, 1)
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, internal, w, h, 0, fmt, kind, raw)
    filt = 0x2601 if linear else 0x2600
    wrap = 0x2901 if repeat else 0x812F
    for parameter, value in [(0x2800, filt), (0x2801, filt), (0x2802, wrap), (0x2803, wrap)]:
        gl('glTexParameteri', None, U, U, I)(TEX2D, parameter, value)
    return tex.value


def mipmaps(tex):
    gl('glBindTexture', None, U, U)(TEX2D, tex)
    gl('glGenerateMipmap', None, U)(TEX2D)
    gl('glTexParameteri', None, U, U, I)(TEX2D, 0x2801, 0x2703)


def fbo(colors, depth=None):
    f = U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(f))
    gl('glBindFramebuffer', None, U, U)(FB, f)
    for i, tex in enumerate(colors):
        gl('glFramebufferTexture2D', None, U, U, U, U, I)(FB, 0x8CE0 + i, TEX2D, tex, 0)
    if depth is not None:
        gl('glFramebufferTexture2D', None, U, U, U, U, I)(FB, 0x821A, TEX2D, depth, 0)
    bufs = (U * len(colors))(*[0x8CE0 + i for i in range(len(colors))])
    gl('glDrawBuffers', None, I, P)(len(colors), bufs)
    assert gl('glCheckFramebufferStatus', U, U)(FB) == 0x8CD5
    return f.value


def read_color(framebuffer, w, h, attachment=0):
    gl('glBindFramebuffer', None, U, U)(FB, framebuffer)
    gl('glReadBuffer', None, U)(0x8CE0 + attachment)
    data = np.zeros((h, w, 4), dtype=np.float32)
    gl('glPixelStorei', None, U, I)(0x0D05, 4)
    gl('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, w, h, RGBA, FLOAT, data.ctypes.data_as(P))
    return data.astype(np.float64)


def read_depth(framebuffer, w, h):
    gl('glBindFramebuffer', None, U, U)(FB, framebuffer)
    data = np.zeros((h, w), dtype=np.float32)
    gl('glPixelStorei', None, U, I)(0x0D05, 4)
    gl('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, w, h, 0x1902, FLOAT, data.ctypes.data_as(P))
    return data.astype(np.float64)


def bind(unit, tex, target=TEX2D):
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    gl('glBindTexture', None, U, U)(target, tex)
    gl('glActiveTexture', None, U)(0x84C0)


def vertex_buffer(data, layout):
    vao, vbo = U(), U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(vbo))
    gl('glBindBuffer', None, U, U)(0x8892, vbo)
    arr = np.ascontiguousarray(data, dtype=np.float32)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8892, arr.nbytes, arr.ctypes.data_as(P), 0x88E4)
    stride = sum(layout) * 4
    offset = 0
    for index, count in enumerate(layout):
        gl('glEnableVertexAttribArray', None, U)(index)
        gl('glVertexAttribPointer', None, U, I, U, U, I, P)(index, count, FLOAT, 0, stride, P(offset))
        offset += count * 4
    return vao.value, arr.size // sum(layout)


def uniform_block(prog, name, binding, data):
    index = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, name.encode())
    if index == 0xFFFFFFFF:
        return
    gl('glUniformBlockBinding', None, U, U, U)(prog, index, binding)
    buf = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(buf))
    gl('glBindBuffer', None, U, U)(0x8A11, buf)
    arr = np.ascontiguousarray(data, dtype=np.float32)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8A11, arr.nbytes, arr.ctypes.data_as(P), 0x88E4)
    gl('glBindBufferBase', None, U, U, U)(0x8A11, binding, buf)


def set_vec4s(prog, name, rows):
    loc = uloc(prog, name)
    if loc < 0:
        return
    values = np.asarray(rows, dtype=np.float32)
    if name == 'u_Water' and values.shape == (WATER_VEC4S, 4):
        # Mirror RB_WaterSurfaceSetupDraw's per-body analytic wave constants.
        wavelength = max(float(values[15, 1] * values[13, 2]), 8.0)
        amplitude = float(values[15, 0] * values[13, 1])
        flow = values[17, :2].astype(np.float64)
        flow_length = float(np.linalg.norm(flow))
        main = flow / flow_length if flow_length > 0.001 else np.array([0.8, 0.6])
        direction_mix = 0.72 if flow_length > 0.001 else 0.22
        for i in range(8):
            angle = i * 2.3999632
            direction = (1.0 - direction_mix) * np.array([math.cos(angle), math.sin(angle)]) + direction_mix * main
            direction /= max(float(np.linalg.norm(direction)), 1.0e-8)
            scale = 1.0 - 0.15 * i if i < 4 else 0.34 - 0.035 * (i - 4)
            lambda_scale = 1.0 - 0.13 * i if i < 4 else 0.28 - 0.025 * (i - 4)
            values[20 + i] = [direction[0], direction[1],
                              amplitude * scale * (0.28 if i < 4 else 0.10),
                              2.0 * math.pi / max(wavelength * lambda_scale, 4.0)]
    flat = np.ascontiguousarray(values.reshape(-1))
    gl('glUniform4fv', None, I, I, P)(loc, len(flat) // 4, flat.ctypes.data_as(P))


def set_matrix(prog, name, m):
    loc = uloc(prog, name)
    if loc < 0:
        return
    flat = np.ascontiguousarray(np.asarray(m, dtype=np.float32).T.reshape(-1))  # column major
    gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0, flat.ctypes.data_as(P))


# --- camera, scene ------------------------------------------------------------------------------------------------

NEAR, FAR = 4.0, 8192.0


class Camera:
    def __init__(self, eye, target, size=(W, H), fov_y=70.0):
        self.eye = np.asarray(eye, dtype=np.float64)
        self.size = size
        f = np.asarray(target, dtype=np.float64) - self.eye
        self.forward = f / np.linalg.norm(f)
        r = np.cross(self.forward, [0.0, 0.0, 1.0])
        self.right = r / np.linalg.norm(r)
        self.up = np.cross(self.right, self.forward)
        self.t = math.tan(math.radians(fov_y) * 0.5)
        self.aspect = size[0] / size[1]
        p = np.zeros((4, 4))
        p[0, 0] = 1.0 / (self.t * self.aspect)
        p[1, 1] = 1.0 / self.t
        p[2, 2] = -(FAR + NEAR) / (FAR - NEAR)
        p[2, 3] = -2.0 * FAR * NEAR / (FAR - NEAR)
        p[3, 2] = -1.0
        self.proj = p
        v = np.eye(4)
        v[0, :3], v[1, :3], v[2, :3] = self.right, self.up, -self.forward
        v[:3, 3] = -v[:3, :3] @ self.eye
        self.vp = p @ v

    def rays(self):
        w, h = self.size
        x = ((np.arange(w) + 0.5) / w * 2 - 1) * self.t * self.aspect
        y = ((np.arange(h) + 0.5) / h * 2 - 1) * self.t
        X, Y = np.meshgrid(x, y)
        d = self.forward[None, None] + self.right[None, None] * X[..., None] + self.up[None, None] * Y[..., None]
        return d / np.linalg.norm(d, axis=-1, keepdims=True)


def box(lo, hi, color):
    p = [np.array([hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]]) for i in range(8)]
    out = []
    for a, b, c, d in [(0, 2, 6, 4), (1, 5, 7, 3), (0, 4, 5, 1), (2, 3, 7, 6), (0, 1, 3, 2), (4, 6, 7, 5)]:
        for k in (a, b, c, a, c, d):
            out.append(list(p[k]) + list(color))
    return out


def scene_triangles(underwater_camera):
    tris = []
    tris += box((-500, -3000, -17), (2000, 0, -16), (0.8, 0.7, 0.5))         # shallow floor, top z = -16
    tris += box((-500, 0, -401), (2000, 3000, -400), (0.2, 0.5, 0.9))        # deep floor, top z = -400
    tris += box((2000, 0, -400), (2040, 3000, 200), (0.4, 0.4, 0.4))         # far shore wall of the deep part
    tris += box((300, -20, -400), (340, 20, 0), (1.0, 0.0, 0.0))             # pillar under the surface
    tris += box((300, -20, 0), (340, 20, 60), (1.0, 0.0, 1.0))               # its part above the water
    if underwater_camera:
        tris += box((200, -3000, 150), (3000, 3000, 160), (0.9, 0.9, 0.2))  # a roof above the water
    return np.array(tris, dtype=np.float32)


def wave_slopes(waves, size=256):
    slopes = np.zeros((size, size, 4), dtype=np.float32)
    if waves:
        y, x = np.mgrid[0:size, 0:size]
        sx = 1.4 * np.cos(2 * np.pi * (3 * x + 2 * y) / size)
        sy = 1.4 * np.cos(2 * np.pi * (x - 4 * y) / size)
        slopes[..., 0], slopes[..., 1], slopes[..., 2], slopes[..., 3] = sx, sy, sx * sx, sy * sy
    return slopes


class Rig:
    """scene copies and the water pass of one camera"""

    def __init__(self, cam, underwater=False, size=(W, H)):
        w, h = size
        self.cam, self.size = cam, size
        self.ssr_steps = 48
        self.scene_prog = compile_program(
            [(GL_VERTEX, '#version 150 core\nin vec3 pos; in vec3 col; uniform mat4 vp; out vec3 c;\n'
                         'void main(){ c = col; gl_Position = vp * vec4(pos, 1.0); }\n'),
             (GL_FRAGMENT, '#version 150 core\nin vec3 c; out vec4 o; void main(){ o = vec4(c, 1.0); }\n')],
            'scene', attributes=('pos', 'col'), outputs=('o',))
        self.scene_vao, self.scene_count = vertex_buffer(scene_triangles(underwater), (3, 3))
        self.color = tex2d(w, h, RGBA16F, linear=True)
        self.depth = tex2d(w, h, DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8)
        self.scene_fbo = fbo([self.color], self.depth)
        self.out = tex2d(w, h, RGBA32F)
        self.glow = tex2d(w, h, RGBA16F)
        self.out_depth = tex2d(w, h, DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8)
        self.water_fbo = fbo([self.out, self.glow], self.out_depth)
        self.input_glow = tex2d(w, h, data=np.zeros((h, w, 4), dtype=np.float32), linear=True)
        # the water plane z = 0, normal up packed as rend2 (n * 0.5 + 0.5)
        quad = []
        for x, y in [(-200, -3000), (3000, -3000), (3000, 3000), (-200, -3000), (3000, 3000), (-200, 3000)]:
            quad.append([x, y, 0.0, 0.5, 0.5, 1.0, x / 256.0, y / 256.0, 1.0, 0.0])
        self.water_vao, self.water_count = vertex_buffer(quad, (3, 3, 2, 2))
        lut = np.zeros((64, 64, 4), dtype=np.float32)
        nv = (np.arange(64) + 0.5) / 64.0
        fc = (1 - nv) ** 5
        lut[..., 0], lut[..., 1] = (1 - fc)[:, None], fc[:, None]
        self.lut = tex2d(64, 64, RGBA16F, data=lut, linear=True)
        self.waves = {}
        for waves in (False, True):
            t = tex2d(256, 256, RGBA16F, data=wave_slopes(waves), linear=True, repeat=True)
            mipmaps(t)
            self.waves[waves] = t
        self.render_scene()

    def render_scene(self):
        w, h = self.size
        gl('glBindFramebuffer', None, U, U)(FB, self.scene_fbo)
        gl('glViewport', None, I, I, I, I)(0, 0, w, h)
        gl('glClearColor', None, F, F, F, F)(0.5, 0.7, 1.0, 1.0)  # sky
        gl('glClear', None, U)(0x4100)
        gl('glEnable', None, U)(0x0B71)
        gl('glDepthFunc', None, U)(0x0203)
        gl('glDisable', None, U)(0x0B44)
        gl('glUseProgram', None, U)(self.scene_prog)
        set_matrix(self.scene_prog, 'vp', self.cam.vp)
        gl('glBindVertexArray', None, U)(self.scene_vao)
        gl('glDrawArrays', None, U, I, I)(4, 0, self.scene_count)
        # SSR inputs: linear view depth (sky 1e20) and the opaque color pyramid
        d = read_depth(self.scene_fbo, w, h)
        p = self.cam.proj
        z = np.where(d >= 0.999999, 1.0e20, p[2, 3] / (d * 2.0 - 1.0 + p[2, 2]))
        self.hiz = tex2d(w, h, R32F, RED, data=z.astype(np.float32))
        # closest depth of the 2x2 texels below (RB_ScreenBuildDepth), SCREEN_HIZ_MIPS levels
        level = z.astype(np.float32)
        for k in range(1, 7):
            hh, ww = max(1, level.shape[0] // 2), max(1, level.shape[1] // 2)
            padded = np.full((hh * 2, ww * 2), 1.0e20, dtype=np.float32)
            padded[:level.shape[0], :level.shape[1]] = level[:hh * 2, :ww * 2]
            level = padded.reshape(hh, 2, ww, 2).min(axis=(1, 3))
            gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, k, R32F, ww, hh, 0, RED, FLOAT,
                                                              np.ascontiguousarray(level).ctypes.data_as(P))
        gl('glTexParameteri', None, U, U, I)(TEX2D, 0x813D, 6)       # max level
        gl('glTexParameteri', None, U, U, I)(TEX2D, 0x2801, 0x2700)  # nearest mipmap nearest
        self.pyramid = tex2d(w, h, RGBA16F, linear=True)
        gl('glBindFramebuffer', None, U, U)(READ_FB, self.scene_fbo)
        pyramid_fbo = fbo([self.pyramid])
        gl('glBindFramebuffer', None, U, U)(READ_FB, self.scene_fbo)
        gl('glBindFramebuffer', None, U, U)(DRAW_FB, pyramid_fbo)
        gl('glBlitFramebuffer', None, I, I, I, I, I, I, I, I, U, U)(0, 0, w, h, 0, 0, w, h, 0x4000, 0x2600)
        mipmaps(self.pyramid)

    def params(self, flags=FLAG_REJECT | FLAG_ENVBRDF, ssr=0.0):
        c = self.cam
        mean = (2.0 + 0.75 + 0.25) / 3.0
        u = np.zeros((WATER_VEC4S, 4))
        u[0] = [N_WATER, 0.06, 1.0, 1.0]
        u[1] = [1.0, ssr, 1.0, 1.0]
        # r_volumetricWater defaults: extinction 0.0014 * (2.0 0.75 0.25) / mean, albedo (0.10 0.45 0.75), g 0.6
        u[2] = [0.0014 * 2.0 / mean, 0.0014 * 0.75 / mean, 0.0014 * 0.25 / mean, 0.0]
        u[3] = [0.10, 0.45, 0.75, 0.6]
        u[4] = [c.proj[0, 0], c.proj[1, 1], c.proj[0, 2], c.proj[1, 2]]
        u[5] = [c.proj[2, 3], c.proj[2, 2], 0.0, 1.0]
        u[6] = [0.0, -1.0, flags, MAX_PATH]
        u[7] = list(c.right) + [0.0]
        u[8] = list(c.up) + [1.0]
        u[9] = [0.3, 0.35, 0.4, 0.0]
        u[10] = [1.0, 1.0 / 192.0, 0.0, 0.0]
        u[11] = [0.0, 0.0, 0.37, 0.71]
        u[12] = [0.0, 0.0, 1.0, 1.0]
        u[33] = [0.0, 0.0, 1.0, 1.0]  # one-tile interaction atlas in the harness
        return u

    def draw_water(self, prog, u, waves=False, ssr=False, reflection=None):
        w, h = self.size
        c = self.cam
        gl('glUseProgram', None, U)(prog)
        scene = np.zeros(16)
        scene[0:3] = [0.3, 0.2, 0.9]
        scene[8:11] = [1.0, 0.95, 0.9]
        uniform_block(prog, 'Scene', 0, scene)
        cam = np.zeros(36)
        cam[0:16] = c.vp.T.reshape(-1)
        cam[16:18] = [FAR / NEAR, FAR]
        cam[20:23] = c.eye
        cam[24:27] = c.forward * FAR
        uniform_block(prog, 'Camera', 1, cam)
        uniform_block(prog, 'Entity', 2, np.concatenate([np.eye(4).reshape(-1), np.zeros(16)]))
        uniform_block(prog, 'Lights', 3, np.zeros(1024))
        set_vec4s(prog, 'u_DiffuseTexMatrix', [[1, 0, 0, 1]])
        set_vec4s(prog, 'u_DiffuseTexOffTurb', [[0, 0, 0, 0]])
        set_vec4s(prog, 'u_Water', u)
        set_vec4s(prog, 'u_WaterPass', [reflection['pass'] if reflection else [0, 0, 0, 0]])
        if reflection:
            for unit, texture in zip((12, 14, 15), reflection['history']):
                bind(unit, texture)
            gl('glUniformMatrix4fv', None, I, I, U, P)(uloc(prog, 'u_SSRReproject'), 1, 0,
                np.ascontiguousarray(c.vp.T, dtype=np.float32).ctypes.data_as(P))
        if ssr:
            p = c.proj
            set_vec4s(prog, 'u_SSRProjection', [[p[0, 0], p[1, 1], p[0, 2], p[1, 2]]])
            set_vec4s(prog, 'u_SSRDepthParams', [[p[2, 3], p[2, 2], FAR, 2.0 / (p[0, 0] * w)]])
            set_vec4s(prog, 'u_SSRViewport', [[0, 0, 1, 1]])
            set_vec4s(prog, 'u_SSRTexelSize', [[1 / w, 1 / h, 1 / w, 1 / h]])
            set_vec4s(prog, 'u_SSRSettings', [[self.ssr_steps, 6, 2000, 24]])
            set_vec4s(prog, 'u_SSRSettings2', [[0.6, 0.1, 6, 0]])
            set_vec4s(prog, 'u_SSRSettings3', [[6, 0, NEAR, self.ssr_steps * 3]])
            bind(10, self.hiz)
            bind(11, self.pyramid)
        bind(0, self.color)
        bind(1, self.depth)
        bind(2, self.waves[waves])
        bind(3, self.lut)
        bind(9, self.input_glow)
        # the water is depth tested against the scene (RB_WaterSurfacePrepare keeps renderFbo's depth)
        gl('glBindFramebuffer', None, U, U)(READ_FB, self.scene_fbo)
        gl('glBindFramebuffer', None, U, U)(DRAW_FB, self.water_fbo)
        gl('glBlitFramebuffer', None, I, I, I, I, I, I, I, I, U, U)(0, 0, w, h, 0, 0, w, h, 0x100, 0x2600)
        target = reflection['fbo'] if reflection and reflection['pass'][0] else self.water_fbo
        out_size = reflection['size'] if reflection and reflection['pass'][0] else (w, h)
        gl('glBindFramebuffer', None, U, U)(FB, target)
        gl('glViewport', None, I, I, I, I)(0, 0, *out_size)
        if reflection and reflection['pass'][0]:
            gl('glDepthMask', None, U)(1)
            gl('glClear', None, U)(0x100)
            zero = np.zeros(4, dtype=np.float32)
            for i in range(3):
                gl('glClearBufferfv', None, U, I, P)(0x1800, i, zero.ctypes.data_as(P))
        gl('glClearColor', None, F, F, F, F)(0.0, 0.0, 0.0, -1.0)
        gl('glClear', None, U)(0x4000)
        gl('glEnable', None, U)(0x0B71)
        gl('glDepthFunc', None, U)(0x0203)
        gl('glDisable', None, U)(0x0B44)
        gl('glBindVertexArray', None, U)(self.water_vao)
        gl('glDrawArrays', None, U, I, I)(4, 0, self.water_count)
        gl('glFinish', None)()
        return read_color(target, *out_size)


FINAL = '\tout_Color = vec4(LinearToScene(color), sceneHere.a);'


def probe(expression):
    """the final output replaced by a probe expression of main()'s locals"""
    def body(fs):
        assert fs.count(FINAL) == 1
        return fs.replace(FINAL, f'\tout_Color = {expression};')
    return body


def no_reject(fs):
    return fs.replace('g_flags = int(u_Water[6].z + 0.5);', 'g_flags = int(u_Water[6].z + 0.5) & ~8;')


def probe_program(expression, extra=None, **kw):
    def body(fs):
        fs = probe(expression)(fs)
        return extra(fs) if extra else fs
    return water_program(body=body, **kw)


# --- checks -------------------------------------------------------------------------------------------------------

def check_permutations():
    count = 0
    for deform, shadows2, ssr, froxel, cubemap, hiz, snell in itertools.product(
            [0, 1], [0, 1], [0, 1], [0, 1, 2], [0, 1], [0, 1], [0, 1]):
        if hiz and not ssr:
            continue  # GLSL_LoadGPUProgramWaterSurface: the Hi-Z walk only with the SSR inputs
        prog = water_program(deform=bool(deform), shadows2=bool(shadows2), ssr=bool(ssr), froxel=froxel,
                             cubemap=bool(cubemap), hiz=bool(hiz), snell=bool(snell))
        gl('glDeleteProgram', None, U)(prog)
        count += 1
    return check(f'{count} water surface permutations compiled and linked (deform, shadows2, SSR linear / Hi-Z, froxel scalar / RGB, cubemap, Snell)', True)


def check_geometry_depth(rig):
    """The vertex wave must move raster depth, not merely fragment shading."""
    prog = water_program()
    u = rig.params()
    u[13] = [1.0, 1.0, 1.0, 1.0]
    u[14] = [1.0, 1.0, 1.0, 0.0]
    u[15] = [20.0, 192.0, 1.0, 1.0]
    u[18] = [0.0, 1.0, 0.2, 1.0]
    u[19, 1] = 0.0
    rig.draw_water(prog, u, waves=True)
    flat = read_depth(rig.water_fbo, *rig.size)
    u[19, 1] = 1.0
    rig.draw_water(prog, u, waves=True)
    displaced = read_depth(rig.water_fbo, *rig.size)
    visible = (flat < 0.9999) & (displaced < 0.9999)
    ok = check('vertex displacement changes real water depth',
               visible.sum() > 100 and np.max(np.abs(flat[visible] - displaced[visible])) > 1e-6)

    field = np.zeros((64, 64, 4), dtype=np.float32)
    field[..., 0] = 8.0
    field[..., 3] = 1.0
    bind(19, tex2d(64, 64, RGBA16F, data=field, linear=True))
    u[15, 0] = 0.0
    u[19, 1] = 1.0
    u[30] = [-200, -3000, 1 / 3200, 1 / 6000]
    u[31] = [50, 93.75, 0, 0]
    rig.draw_water(prog, u, waves=True)
    interaction_flat = read_depth(rig.water_fbo, *rig.size)
    u[31, 2] = 1.0
    rig.draw_water(prog, u, waves=True)
    interaction_displaced = read_depth(rig.water_fbo, *rig.size)
    visible = (interaction_flat < 0.9999) & (interaction_displaced < 0.9999)
    ok = check('interactive height changes real water depth', visible.sum() > 100 and
               np.max(np.abs(interaction_flat[visible] - interaction_displaced[visible])) > 1e-6) and ok

    field[..., 0] = np.sin(np.arange(64, dtype=np.float32)[None, :] * np.pi / 4.0) * 8.0
    bind(19, tex2d(64, 64, RGBA16F, data=field, linear=True))
    u[19, 1] = 0.0
    u[31, 2] = 0.0
    optics_flat = rig.draw_water(prog, u, waves=True)
    u[31, 2] = 1.0
    optics_disturbed = rig.draw_water(prog, u, waves=True)
    ok = check('interactive slope changes the shared reflection/refraction normal',
               np.max(np.abs(optics_flat - optics_disturbed)) > 0.001) and ok
    return ok


def check_ambient_waves(rig):
    prog = water_program()
    try:
        u = rig.params()
        u[13] = [1.0, 1.0, 1.0, 1.0]
        u[14] = [1.0, 2.0, 1.0, 1.0]
        u[15] = [2.4, 192.0, 1.0, 1.0]
        u[16] = [-1000.0, -1000.0, 1000.0, 1000.0]
        u[17] = [0.0, 0.0, 80.0, 3.0]
        u[18] = [1.0, 1.0, 0.2, 1.0]
        a = rig.draw_water(prog, u, waves=True)
        u[5, 2] = 3.0
        b = rig.draw_water(prog, u, waves=True)
        moving = np.max(np.abs(a - b)) > 0.001
        ok = check('ambient analytical height changes with time', moving)
        u[15, 0] = 0.0
        flat = rig.draw_water(prog, u, waves=True)
        water_pixels = (flat[..., 0] > 0.49) & (flat[..., 0] < 0.51)
        ok = check('zero body amplitude removes analytical height',
                   water_pixels.sum() > 1000 and np.max(np.abs(flat[water_pixels, :3] - 0.5)) < 1e-4) and ok
        u[15, 0] = 2.4
        deep = rig.draw_water(prog, u, waves=True)
        u[17, 2] = 8.0
        shallow = rig.draw_water(prog, u, waves=True)
        deep_energy = np.mean(np.abs(deep[water_pixels, 0] - 0.5))
        shallow_energy = np.mean(np.abs(shallow[water_pixels, 0] - 0.5))
        ok = check('stable body depth attenuates analytical height',
                   deep_energy > shallow_energy * 2.0) and ok
        u[17, 2] = 80.0
        u[15, 0] = 0.2
        pool = rig.draw_water(prog, u, waves=True)
        pool_energy = np.mean(np.abs(pool[water_pixels, 0] - 0.5))
        ok = check('body amplitude changes visible ambient energy',
                   deep_energy > pool_energy * 2.0) and ok
        u[15, 0] = 0.0
        u[18, 0] = 0.0
        u[13, 0] = 0.0
        old = rig.draw_water(prog, u, waves=True)
        u[13, 0] = 1.0
        zero = rig.draw_water(prog, u, waves=True)
        ok = check('enabled zero-amplitude model preserves previous micro shading',
                   np.max(np.abs(old - zero)) < 1e-5) and ok
        return ok
    finally:
        gl('glDeleteProgram', None, U)(prog)


def above_geometry(cam):
    d = cam.rays()
    s = -cam.eye[2] / d[..., 2]
    Pw = cam.eye[None, None] + d * s[..., None]
    cos_i = -d[..., 2]
    sin_t = np.sqrt(np.maximum(1 - cos_i ** 2, 0)) / N_WATER
    cos_t = np.sqrt(1 - sin_t ** 2)
    return d, s, Pw, cos_i, cos_t


def check_above(rig):
    ok = True
    cam = rig.cam
    d, s, Pw, cos_i, cos_t = above_geometry(cam)
    img = rig.draw_water(probe_program('vec4(pathLength, W, rejected, inside ? 1.0 : 0.0)'), rig.params())
    water = img[..., 3] > -0.5
    depth = np.where(Pw[..., 1] < 0, 16.0, 400.0)
    expected = depth / np.maximum(cos_t, 0.05)
    safe = water & (Pw[..., 0] > 60) & (Pw[..., 0] < 1400) & (np.abs(Pw[..., 1]) > 250) & \
        ((np.abs(Pw[..., 0] - 320) > 260) | (np.abs(Pw[..., 1]) > 400))
    grazing = cos_i < 0.12
    for name, m, judged in [('shallow (16 units)', safe & (Pw[..., 1] < 0), True),
                            ('deep (400 units)', safe & (Pw[..., 1] > 0) & ~grazing, True),
                            ('deep, grazing (< 7 degrees)', safe & (Pw[..., 1] > 0) & grazing, False)]:
        err = np.abs(img[..., 0][m] / expected[m] - 1)
        detail = (f'{m.sum()} px, mean {img[..., 0][m].mean():.1f} (reference {expected[m].mean():.1f}), '
                  f'relative error p95 {np.percentile(err, 95):.4f} max {err.max():.4f}')
        if judged:
            ok = check(f'refracted path length, {name}', np.percentile(err, 95) < 0.02, detail) and ok
        else:
            print(f'INFO: refracted path length, {name} - {detail}')
    f0 = ((N_WATER - 1) / (N_WATER + 1)) ** 2
    expected_w = f0 * (1 - (1 - cos_i) ** 5) + (1 - cos_i) ** 5
    err = np.abs(img[..., 1] - expected_w)[safe]
    ok = check('reflection weight of the smooth surface', err.max() < 0.01,
               f'max abs error {err.max():.4f}, range {img[..., 1][safe].min():.3f}..{img[..., 1][safe].max():.3f}') and ok
    sky = water & (Pw[..., 0] > 2600) & (Pw[..., 1] < -200)
    ok = check('sky behind the water: the longest path', sky.any() and img[..., 0][sky].min() >= MAX_PATH - 1,
               f'{sky.sum()} px') and ok
    ok = check('camera above the water is outside the liquid', img[..., 3][water].max() < 0.5) and ok
    rej = img[..., 2][water]
    print(f'INFO: depth rejection - {(rej == 0.5).sum()} px with a shrunk offset, {(rej == 1.0).sum()} px without refraction')

    on = rig.draw_water(probe_program('vec4(rawRefracted, transmittance.g)'), rig.params())
    off = rig.draw_water(probe_program('vec4(rawRefracted, transmittance.g)', extra=no_reject), rig.params())

    def magenta(im):
        c = im[..., :3]
        return water & (c[..., 0] > 0.9) & (c[..., 2] > 0.9) & (c[..., 1] < 0.1)
    ok = check('depth rejection: the pillar above the water is never refracted under it',
               magenta(on).sum() == 0 and magenta(off).sum() > 0,
               f'with rejection {magenta(on).sum()} px, without {magenta(off).sum()} px') and ok
    t_shallow = on[..., 3][safe & (Pw[..., 1] < 0)].mean()
    t_deep = on[..., 3][safe & (Pw[..., 1] > 0)].mean()
    ok = check('transmittance: shallow water much clearer than deep water', t_shallow > 0.95 and t_deep < t_shallow - 0.1,
               f'green: shallow {t_shallow:.3f}, deep {t_deep:.3f}') and ok

    rough = rig.draw_water(probe_program('vec4(roughness, transmitted.r, reflection.r, F)'), rig.params(), waves=True)
    near, far = water & (s < 200), water & (s > 1500)
    finite = bool(np.isfinite(rough[water]).all())
    ok = check('waves: unresolved slopes raise the roughness with distance, finite',
               rough[..., 0][far].mean() > rough[..., 0][near].mean() and finite,
               f'roughness near {rough[..., 0][near].mean():.3f}, far {rough[..., 0][far].mean():.3f}') and ok

    for label, hiz, steps in [('linear march', False, 48), ('Hi-Z walk', True, 24)]:
        rig.ssr_steps = steps
        ssr = rig.draw_water(probe_program('vec4(reflection, ssrDebug.g)', ssr=True, hiz=hiz),
                             rig.params(flags=FLAG_REJECT | FLAG_ENVBRDF | FLAG_SSR, ssr=1.0), ssr=True)
        hit = water & (ssr[..., 0] > 0.7) & (ssr[..., 2] > 0.7) & (ssr[..., 1] < 0.3)
        cols = np.nonzero(hit)[1]
        detail = f'{(ssr[..., 3][water] > 0).sum()} px hit, {hit.sum()} reflect the pillar'
        if hit.any():
            detail += f' (columns {cols.min()}..{cols.max()}, screen center {W // 2})'
        ok = check(f'SSR ({label}, {steps} steps): the reflection rays find the pillar above the water', hit.sum() >= 10 and
                   abs(int(np.median(cols)) - W // 2) < 20 and bool(np.isfinite(ssr[water]).all()), detail) and ok
    rig.ssr_steps = 48
    return ok


def check_directional_flow(rig):
    """Master-off is bit-identical; enabled velocity advects the existing two-scale detail."""
    prog = water_program()
    try:
        u = rig.params()
        u[5, 2] = 2.0
        u[28] = [32.0, 0.0, 0.0, 0.0]
        u[29] = [32.0, 1.0, 4.0, 10.0]
        disabled_with_state = rig.draw_water(prog, u, waves=True)
        u[28, :3] = 0.0
        disabled_zero = rig.draw_water(prog, u, waves=True)
        ok = check('r_waterFlow 0 preserves the prior detail result',
                   np.array_equal(disabled_with_state, disabled_zero))
        u[28] = [32.0, 0.0, 0.0, 1.0]
        enabled = rig.draw_water(prog, u, waves=True)
        ok = check('resolved flow advects two-scale micro detail',
                   np.max(np.abs(enabled - disabled_zero)) > 0.01) and ok
    finally:
        gl('glDeleteProgram', None, U)(prog)
    return ok


def check_inside():
    cam = Camera((0.0, 0.0, -60.0), (400.0, 0.0, 120.0))
    rig = Rig(cam, underwater=True)
    img = rig.draw_water(probe_program('vec4(pathLength, W, rejected, inside ? 1.0 : 0.0)'), rig.params())
    d = cam.rays()
    water = (img[..., 3] > -0.5) & (d[..., 2] > 0.02)
    crit = math.sqrt(1 - (1 / N_WATER) ** 2)
    tir, window = water & (d[..., 2] < crit - 0.02), water & (d[..., 2] > crit + 0.02)
    ok = check('camera below the water is inside the liquid', img[..., 3][water].min() > 0.5)
    ok = check('total internal reflection outside Snell\'s window', tir.any() and img[..., 1][tir].min() > 0.999,
               f'{tir.sum()} px, min weight {img[..., 1][tir].min():.4f}') and ok
    ok = check('Snell\'s window transmits', window.any() and img[..., 1][window].max() < 0.9,
               f'{window.sum()} px, max weight {img[..., 1][window].max():.3f}') and ok
    col = rig.draw_water(probe_program('vec4(transmitted, W)'), rig.params())
    roof = window & (col[..., 0] > 0.8) & (col[..., 1] > 0.8) & (col[..., 2] < 0.3)
    ok = check('the scene above is refracted into the window', roof.sum() > 0.2 * window.sum(),
               f'{roof.sum()} of {window.sum()} px see the roof') and ok
    return ok


def fresnel_dielectric(cos_i, eta):
    """float64 reference of FresnelDielectric (unpolarized, smooth), 1 in total internal reflection"""
    sin_t2 = eta * eta * np.maximum(1 - cos_i ** 2, 0)
    cos_t = np.sqrt(np.maximum(1 - sin_t2, 0))
    rs = (eta * cos_i - cos_t) / (eta * cos_i + cos_t)
    rp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t)
    return np.where(sin_t2 >= 1, 1.0, 0.5 * (rs * rs + rp * rp))


def check_snell():
    """r_waterSnell (USE_WATER_SNELL): water -> air refraction, total internal reflection, the reflection sources"""
    ok = True
    cam = Camera((0.0, 0.0, -60.0), (400.0, 0.0, 120.0))
    rig = Rig(cam, underwater=True)
    d = cam.rays()
    cos_i = d[..., 2]                                  # N . V from inside, flat surface
    img = rig.draw_water(probe_program('vec4(tirFraction, W, float(inside), 0.0)', snell=True), rig.params())
    water = (img[..., 3] > -0.5) & (cos_i > 0.02)
    for n in (N_WATER, 2.0):
        # IOR 2: a 30 degree window, the steeper camera sees into it
        r = rig if n == N_WATER else Rig(Camera((0.0, 0.0, -60.0), (200.0, 0.0, 300.0)), underwater=True)
        u = r.params()
        u[0][0] = n
        im = img if n == N_WATER else r.draw_water(
            probe_program('vec4(tirFraction, W, float(inside), 0.0)', snell=True), u)
        ci = r.cam.rays()[..., 2]
        crit = math.sqrt(1 - 1 / n ** 2)                # cos of the critical angle
        band = np.abs(ci - crit) < 0.002                # pixels straddling the critical angle (float32)
        tir_ref = ci < crit
        m = (im[..., 3] > -0.5) & (ci > 0.02) & ~band
        wrong = (im[..., 0][m] > 0.5) != tir_ref[m]
        err = np.abs(im[..., 1] - fresnel_dielectric(ci, n))[m]
        ok = check(f'IOR {n}: TIR exactly beyond the critical angle ({math.degrees(math.acos(crit)):.1f} deg), '
                   'no window mask', m.any() and wrong.sum() == 0 and tir_ref[m].any() and (~tir_ref[m]).any(),
                   f'{m.sum()} px, {wrong.sum()} misclassified, {tir_ref[m].sum()} TIR') and ok
        tir = m & tir_ref
        ok = check(f'IOR {n}: reflection weight = exact water -> air Fresnel, 1 in TIR',
                   err.max() < 2e-3 and im[..., 1][tir].min() == 1.0,
                   f'max abs error {err.max():.2e}, TIR min {im[..., 1][tir].min()}') and ok
    # continuity: along the critical angle the weight rises to 1 without a step (Fresnel, not a mask)
    near = water & (cos_i > crit_w(N_WATER)) & (cos_i < crit_w(N_WATER) + 0.03)
    ok = check('Fresnel rises continuously to 1 at the critical angle', near.any() and img[..., 1][near].max() > 0.6
               and img[..., 1][near].min() < img[..., 1][near].max(),
               f'{near.sum()} px within 0.03 of cos(theta_c): W {img[..., 1][near].min():.3f}..{img[..., 1][near].max():.3f}') and ok

    # the scene above, refracted (water -> air) into the window
    col = rig.draw_water(probe_program('vec4(transmitted, W)', snell=True), rig.params())
    window = water & (cos_i > crit_w(N_WATER) + 0.02)
    roof = window & (col[..., 0] > 0.8) & (col[..., 1] > 0.8) & (col[..., 2] < 0.3)
    ok = check('Snell\'s window refracts the scene above', roof.sum() > 0.2 * window.sum(),
               f'{roof.sum()} of {window.sum()} px see the roof') and ok

    # TIR without SSR / cubemap: the liquid along an endless reflected path (no sun in the rig)
    refl = rig.draw_water(probe_program('vec4(reflection, debugSource.b)', snell=True), rig.params())
    u = rig.params()
    sigma, albedo, env = np.array(u[2][:3]), np.array(u[3][:3]), np.array(u[9][:3])
    expected = albedo * 0.5 * env * (1 - np.exp(-sigma * MAX_PATH))
    tir = water & (cos_i < crit_w(N_WATER) - 0.02)
    err = np.abs(refl[..., :3][tir] - expected).max()
    ok = check('TIR without SSR or cubemap reflects the liquid (endless path in-scattering)',
               tir.any() and err < 1e-4 and refl[..., 3][tir].min() == 1.0,
               f'{tir.sum()} px, expected {np.round(expected, 4)}, max abs error {err:.2e}') and ok

    # SSR under the surface: the reflection rays find the red pillar in the liquid, attenuated by the medium
    for label, hiz, steps in [('linear march', False, 48), ('Hi-Z walk', True, 24)]:
        rig.ssr_steps = steps
        s = rig.draw_water(probe_program('vec4(reflection, ssrDebug.g)', ssr=True, hiz=hiz, snell=True),
                           rig.params(flags=FLAG_REJECT | FLAG_ENVBRDF | FLAG_SSR, ssr=1.0), ssr=True)
        hit = tir & (s[..., 3] > 0.5) & (s[..., 0] > 0.5) & (s[..., 1] < 0.3)
        red_max = s[..., 0][hit].max() if hit.any() else 0.0
        ok = check(f'SSR from below ({label}): TIR reflects the pillar in the liquid, through the medium',
                   hit.sum() >= 10 and red_max < 1.0 and bool(np.isfinite(s[water]).all()),
                   f'{hit.sum()} px, red {s[..., 0][hit].min() if hit.any() else 0:.3f}..{red_max:.3f} (< 1: absorbed)') and ok
    rig.ssr_steps = 48

    # above the water the Snell program is the prompt-1 surface
    for eye, target in [((0.0, 0.0, 100.0), (400.0, 0.0, 0.0)), ((0.0, 0.0, 6.0), (800.0, 0.0, 0.0))]:
        r = Rig(Camera(eye, target))
        a = r.draw_water(water_program(), r.params(), waves=True)
        b = r.draw_water(water_program(snell=True), r.params(), waves=True)
        wm = a[..., 3] > -0.5
        diff = np.abs(a - b)[wm].max()
        ok = check(f'from above (eye z = {eye[2]:.0f}): same as without Snell', diff < 1e-5, f'max abs diff {diff:.2e}') and ok

    # camera crossing the plane: the side of each fragment follows the eye, finite everywhere
    prog = probe_program('vec4(color, float(inside))', snell=True)
    for z in (1.0, 0.01, -0.01, -1.0):
        r = Rig(Camera((0.0, 0.0, z), (400.0, 0.0, 30.0 if z < 0 else -30.0)), underwater=z < 0)
        for waves in (False, True):
            c = r.draw_water(prog, r.params(), waves=waves)
            wm = c[..., 3] > -0.5
            ok = check(f'camera at z = {z:+.2f} ({"waves" if waves else "flat"}): finite, side consistent',
                       wm.any() and bool(np.isfinite(c[wm]).all()) and
                       bool(((c[..., 3][wm] > 0.5) == (z < 0)).all()), f'{wm.sum()} px') and ok

    # steep ripples: the window breaks up (partial TIR shares), everything finite
    u = rig.params()
    u[0][2] = 4.0
    rip = rig.draw_water(probe_program('vec4(tirFraction, W, color.r + color.g + color.b, 0.0)', snell=True), u,
                         waves=True)
    partial = water & (rip[..., 0] > 0.0) & (rip[..., 0] < 1.0)
    ok = check('steep ripples: partial TIR shares break the window up, finite, weights in [0, 1]',
               partial.sum() > 0 and bool(np.isfinite(rip[water]).all()) and rip[..., 1][water].min() >= 0.0 and
               rip[..., 1][water].max() <= 1.0, f'{partial.sum()} px with a partial share') and ok

    # IOR 1: no interface, no TIR, the scene straight through
    u = rig.params()
    u[0][0] = 1.0
    one = rig.draw_water(probe_program('vec4(rawRefracted - SceneToLinear(sceneHere.rgb), tirFraction)', snell=True), u)
    ok = check('IOR 1: no total internal reflection, no refraction offset',
               one[..., 3][water].max() == 0.0 and np.abs(one[..., :3][water]).max() < 1e-3,
               f'max colour difference {np.abs(one[..., :3][water]).max():.2e}') and ok

    # debug views: finite; view 1 orange from inside with the camera in the liquid, striped magenta without
    for view in range(1, 7):
        for camera_liquid in ((True, False) if view == 1 else (True,)):
            u = rig.params(flags=FLAG_REJECT | FLAG_ENVBRDF | (FLAG_CAMERA_LIQUID if camera_liquid else 0))
            u[10][3] = view
            dbg = rig.draw_water(water_program(snell=True), u)
            good = bool(np.isfinite(dbg[water]).all())
            if view == 1:
                orange = (np.abs(dbg[..., :3][water] - [1.0, 0.5, 0.1]).max(axis=-1) < 1e-3).mean()
                magenta = (np.abs(dbg[..., :3][water] - [1.0, 0.0, 1.0]).max(axis=-1) < 1e-3).mean()
                good = good and (orange == 1.0 and magenta == 0.0 if camera_liquid else 0.2 < magenta < 0.5)
            ok = check(f'r_waterSnellDebug {view}{"" if view != 1 else " (camera contents " + ("liquid)" if camera_liquid else "air: mismatch stripes)")}',
                       good) and ok
    return ok


def check_integration():
    ok = True
    rig = Rig(Camera((0., 0., 100.), (400., 0., 0.)))
    flags = FLAG_REJECT | FLAG_ENVBRDF | FLAG_SSR
    program = water_program(ssr=True, hiz=True)
    size = (W // 2, H // 2)
    buffers = []
    for _ in range(2):
        textures = [tex2d(*size, RGBA16F), tex2d(*size, RGBA16F), tex2d(*size, RGBA16F)]
        depth = tex2d(*size, DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8)
        buffers.append((fbo(textures, depth), textures))
    u = rig.params(flags=flags, ssr=1.)
    trace = dict(fbo=buffers[0][0], size=size, history=buffers[1][1], **{'pass': [1, 0, .8, 0]})
    raw = rig.draw_water(program, u, ssr=True, reflection=trace)
    hit_count = (raw[..., 3] > 0).sum()
    ok = check('reduced water reflection pass produces finite radiance and confidence',
               np.isfinite(raw).all() and hit_count > 300, f'{hit_count} hit pixels at {size}') and ok
    geom = read_color(buffers[0][0], *size, attachment=1)
    hit = read_color(buffers[0][0], *size, attachment=2)
    active = geom[..., 0] > 0
    ok = check('water reflection history stores surface depth, normals and receiver-relative hits',
               active.any() and np.isfinite(geom[active]).all() and np.isfinite(hit[active]).all()
               and (hit[..., 3] > .5).sum() == hit_count) and ok
    resolve = dict(fbo=rig.water_fbo, size=rig.size, history=buffers[0][1], **{'pass': [0, 1, .8, 0]})
    image = rig.draw_water(probe_program('vec4(reflection, ssrDebug.g)', ssr=True, hiz=True),
                           u, ssr=True, reflection=resolve)
    reflected = (image[..., 0] > .7) & (image[..., 2] > .7) & (image[..., 1] < .3)
    ok = check('bilateral water reflection resolve preserves the reflected pillar',
               np.isfinite(image).all() and reflected.sum() > 150, f'{reflected.sum()} pixels') and ok
    trace.update(fbo=buffers[1][0], history=buffers[0][1], **{'pass': [1, 1, .8, 0]})
    second = rig.draw_water(program, u, ssr=True, reflection=trace)
    ok = check('static water reflection temporal history remains stable',
               np.isfinite(second).all() and np.max(np.abs(second - raw)) < .02) and ok
    # A different receiver depth must reject history rather than smearing it.
    changed_geom = geom.copy(); changed_geom[..., 0] += 1000.
    bind(14, buffers[0][1][1])
    gl('glActiveTexture', None, U)(0x84CE)
    gl('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, 0, 0, *size,
        RGBA, FLOAT, np.ascontiguousarray(changed_geom, dtype=np.float32).ctypes.data_as(P))
    gl('glActiveTexture', None, U)(0x84C0)
    rejected = rig.draw_water(program, u, ssr=True, reflection=trace)
    ok = check('water temporal history rejects a different receiver depth',
               np.max(np.abs(rejected - raw)) < .005) and ok
    # Smooth fade complement must cover a segment crossing the volume boundary,
    # even when its surface is before the fade start (the old code returned zero).
    fade_probe = probe_program('vec4(WaterMissingMedium(100., 2000.), WaterMissingMedium(850., 900.), WaterMissingMedium(1200., 2000.), 1.)')
    optical = rig.params(); optical[7, 3] = 800.; optical[8, 3] = 1. / 200.
    result = rig.draw_water(fade_probe, optical)
    valid = result[..., 3] > .5
    reference = []
    for z0, z1 in ((100., 2000.), (850., 900.), (1200., 2000.)):
        z = np.linspace(z0, z1, 20001)
        t = np.clip((z - 800.) / 200., 0., 1.)
        reference.append(np.trapezoid(t*t*(3.-2.*t), z)/(z1-z0))
    ok = check('whole-segment froxel fade agrees with independent numerical integration',
               np.max(np.abs(result[valid, :3] - reference)) < 2e-5, str(reference)) and ok
    # Dedicated glow must transmit through water even when scene bloom is disabled.
    glow = np.zeros((H, W, 4), dtype=np.float32); glow[..., :3] = [.5, .2, .1]
    rig.input_glow = tex2d(W, H, data=glow, linear=True)
    rig.draw_water(water_program(), rig.params())
    transmitted_glow = read_color(rig.water_fbo, W, H, attachment=1)
    ok = check('water preserves underwater glow with medium attenuation',
               np.isfinite(transmitted_glow).all() and (transmitted_glow[..., 0] > .01).sum() > 1000) and ok
    # Production depth sampling must be exact for the selected texel; changing
    # the texture filter should not affect thickness (texelFetch protects it).
    depth_probe = probe_program('vec4(pathLength, rejected, roughness, 1.)')
    nearest = rig.draw_water(depth_probe, rig.params(), waves=True)
    gl('glActiveTexture', None, U)(0x84C1)
    gl('glBindTexture', None, U, U)(TEX2D, rig.depth)
    for param in (0x2800, 0x2801):
        gl('glTexParameteri', None, U, U, I)(TEX2D, param, 0x2601)
    gl('glActiveTexture', None, U)(0x84C0)
    linear = rig.draw_water(depth_probe, rig.params(), waves=True)
    ok = check('water thickness is independent of accidental depth filtering',
               np.max(np.abs(nearest - linear)) < 1e-5) and ok
    viewport = rig.params(); viewport[12] = [.125, 1./6., .625, 2./3.]
    projected = rig.draw_water(probe_program('vec4(WaterProject(P), 0., 1.)'), viewport)
    mask = projected[..., 3] > .5
    yy, xx = np.mgrid[:H, :W]
    expected = np.stack(((xx+.5)/W, (yy+.5)/H), axis=-1) * viewport[12, 2:] + viewport[12, :2]
    ok = check('water projection respects an offset, reduced viewport',
               np.max(np.abs(projected[mask, :2] - expected[mask])) < 1e-5) and ok
    return ok


def crit_w(n):
    return math.sqrt(1 - 1 / n ** 2)


def context(title):
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 3)
    SDL.SDL_GL_SetAttribute(18, 2)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(title, 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    ctx = SDL.SDL_GL_CreateContext(window)
    assert ctx, SDL.SDL_GetError()
    print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
    return window, ctx


def main():
    window, ctx = context(b'Water surface test')
    ok = True
    try:
        ok = check_permutations() and ok
        rig = Rig(Camera((0.0, 0.0, 100.0), (400.0, 0.0, 0.0)))
        ok = check_geometry_depth(rig) and ok
        ok = check_ambient_waves(rig) and ok
        ok = check_directional_flow(rig) and ok
        ok = check_above(rig) and ok
        ok = check_inside() and ok
        ok = check_snell() and ok
        ok = check_integration() and ok
        err = gl('glGetError', U)()
        ok = check('no GL error', err == 0, hex(err)) and ok
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


# --- GPU timings ----------------------------------------------------------------------------------------------------

def bench():
    """GL_TIME_ELAPSED at 1920x1080: the scene copy (color + depth blit) and the water pass covering the whole view
    (camera looking down at the plane), without and with SSR (48 steps), with waves"""
    window, ctx = context(b'Water surface bench')
    try:
        size = (1920, 1080)
        cam = Camera((0.0, 0.0, 300.0), (300.0, 0.0, 0.0), size=size)
        rig = Rig(cam, size=size)
        query = U()
        gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(query))

        def timed(fn, repeat=20):
            fn()
            gl('glFinish', None)()
            total = 0
            for _ in range(repeat):
                gl('glBeginQuery', None, U, U)(0x88BF, query)
                fn()
                gl('glEndQuery', None, U)(0x88BF)
                value = C.c_uint64()
                gl('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))(query, 0x8866, C.byref(value))
                total += value.value
            return total / repeat * 1e-6

        copy_color = tex2d(*size, RGBA16F)
        copy_depth = tex2d(*size, DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8)
        copy_fbo = fbo([copy_color], copy_depth)

        def copy():
            gl('glBindFramebuffer', None, U, U)(READ_FB, rig.scene_fbo)
            gl('glBindFramebuffer', None, U, U)(DRAW_FB, copy_fbo)
            gl('glBlitFramebuffer', None, I, I, I, I, I, I, I, I, U, U)(0, 0, *size, 0, 0, *size, 0x4100, 0x2600)
        print(f'  scene copy (RGBA16F color + D24S8 depth) {size[0]}x{size[1]}: {timed(copy):.3f} ms')

        d = cam.rays()
        coverage = (d[..., 2] < 0).mean()
        for label, ssr, hiz, steps in [('no SSR', False, False, 0), ('SSR linear, 40 steps', True, False, 40),
                                       ('SSR Hi-Z, 24 steps (r_ssrQuality 1)', True, True, 24),
                                       ('SSR Hi-Z, 40 steps (r_ssrQuality 2)', True, True, 40)]:
            rig.ssr_steps = steps
            prog = water_program(ssr=ssr, hiz=hiz, size=size)
            flags = FLAG_REJECT | FLAG_ENVBRDF | (FLAG_SSR if ssr else 0)
            u = rig.params(flags=flags, ssr=1.0 if ssr else 0.0)

            def water():
                rig.draw_water(prog, u, waves=True, ssr=ssr)
            # draw_water reads back: time the draw only
            rig.draw_water(prog, u, waves=True, ssr=ssr)

            def draw_only():
                gl('glBindFramebuffer', None, U, U)(FB, rig.water_fbo)
                gl('glBindVertexArray', None, U)(rig.water_vao)
                gl('glDrawArrays', None, U, I, I)(4, 0, rig.water_count)
            gl('glDepthFunc', None, U)(0x0207)  # always: every water pixel shades
            print(f'  water pass {size[0]}x{size[1]}, {coverage * 100:.0f}% of the view, waves, {label}: {timed(draw_only):.3f} ms')
            if not ssr:
                u[13] = [1, 1, 1, 1]
                u[15] = [2.4, 192, 1, 1]
                u[17] = [0, 0, 80, 3]
                for quality in (0, 1, 2):
                    u[14] = [1, quality, 1, 1]
                    set_vec4s(prog, 'u_Water', u)
                    print(f'  ambient quality {quality}, fullscreen: {timed(draw_only):.3f} ms')
                gl('glEnable', None, U)(0x0C11)  # SCISSOR_TEST: quarter-screen water
                gl('glScissor', None, I, I, I, I)(size[0]//4, size[1]//4, size[0]//2, size[1]//2)
                u[13, 0] = 0
                set_vec4s(prog, 'u_Water', u)
                print(f'  existing micro, quarter-screen: {timed(draw_only):.3f} ms')
                u[13, 0] = 1
                u[14, 1] = 1
                set_vec4s(prog, 'u_Water', u)
                print(f'  ambient quality 1, quarter-screen: {timed(draw_only):.3f} ms')
                gl('glDisable', None, U)(0x0C11)
                alternating = []
                for _ in range(4):
                    u[13, 0] = 0
                    set_vec4s(prog, 'u_Water', u)
                    old_ms = timed(draw_only, repeat=10)
                    u[13, 0] = 1
                    set_vec4s(prog, 'u_Water', u)
                    new_ms = timed(draw_only, repeat=10)
                    alternating.append((old_ms, new_ms))
                old_mean = sum(pair[0] for pair in alternating) / len(alternating)
                new_mean = sum(pair[1] for pair in alternating) / len(alternating)
                print(f'  interleaved full-screen micro {old_mean:.3f} ms, ambient quality 1 {new_mean:.3f} ms, delta {new_mean-old_mean:+.3f} ms')
                shoreline_pairs = []
                for _ in range(4):
                    u[34] = [0, 96, 6, 0]
                    set_vec4s(prog, 'u_Water', u)
                    shoreline_off = timed(draw_only, repeat=10)
                    u[34, 0] = 1
                    set_vec4s(prog, 'u_Water', u)
                    shoreline_on = timed(draw_only, repeat=10)
                    shoreline_pairs.append((shoreline_off, shoreline_on))
                shoreline_off = sum(pair[0] for pair in shoreline_pairs) / len(shoreline_pairs)
                shoreline_on = sum(pair[1] for pair in shoreline_pairs) / len(shoreline_pairs)
                print(f'  interleaved shoreline off {shoreline_off:.3f} ms, on {shoreline_on:.3f} ms, delta {shoreline_on-shoreline_off:+.3f} ms')
                u[34] = [0, 0, 0, 0]
                flow_pairs = []
                u[13, 0] = 1
                u[14, 1] = 1
                u[28] = [32, 8, 0, 0]
                u[29] = [33, 1, 0, 10]
                for _ in range(4):
                    u[28, 3] = 0
                    set_vec4s(prog, 'u_Water', u)
                    flow_off = timed(draw_only, repeat=10)
                    u[28, 3] = 1
                    set_vec4s(prog, 'u_Water', u)
                    flow_on = timed(draw_only, repeat=10)
                    flow_pairs.append((flow_off, flow_on))
                flow_off = sum(pair[0] for pair in flow_pairs) / len(flow_pairs)
                flow_on = sum(pair[1] for pair in flow_pairs) / len(flow_pairs)
                print(f'  interleaved directional flow off {flow_off:.3f} ms, on {flow_on:.3f} ms, delta {flow_on-flow_off:+.3f} ms')
                iy, ix = np.mgrid[0:64, 0:64]
                ir = np.sqrt(((ix - 31.5) / 31.5) ** 2 + ((iy - 31.5) / 31.5) ** 2)
                interaction_data = np.zeros((64, 64, 4), dtype=np.float32)
                interaction_data[..., 0] = np.sin(ir * 20.0) * np.maximum(1.0 - ir, 0.0)
                interaction_data[..., 1] = np.cos(ir * 20.0) * np.maximum(1.0 - ir, 0.0)
                interaction_data[..., 2] = 0.1
                interaction_data[..., 3] = 1.0
                interaction_tex = tex2d(64, 64, RGBA16F, data=interaction_data, linear=True)
                bind(19, interaction_tex)
                u[30] = [-200, -3000, 1 / 3200, 1 / 6000]
                u[31] = [50, 93.75, 0, 0]
                interaction_pairs = []
                for _ in range(4):
                    u[31, 2] = 0
                    set_vec4s(prog, 'u_Water', u)
                    interaction_off = timed(draw_only, repeat=10)
                    u[31, 2] = 1
                    set_vec4s(prog, 'u_Water', u)
                    interaction_on = timed(draw_only, repeat=10)
                    interaction_pairs.append((interaction_off, interaction_on))
                interaction_off = sum(pair[0] for pair in interaction_pairs) / len(interaction_pairs)
                interaction_on = sum(pair[1] for pair in interaction_pairs) / len(interaction_pairs)
                print(f'  interleaved interaction off {interaction_off:.3f} ms, on {interaction_on:.3f} ms, delta {interaction_on-interaction_off:+.3f} ms')
                def two_bodies():
                    gl('glBindFramebuffer', None, U, U)(FB, rig.water_fbo)
                    gl('glBindVertexArray', None, U)(rig.water_vao)
                    gl('glEnable', None, U)(0x0C11)
                    for half, amplitude in ((0, 0.2), (1, 2.4)):
                        gl('glScissor', None, I, I, I, I)(half * size[0]//2, 0, size[0]//2, size[1])
                        u[15, 0] = amplitude
                        set_vec4s(prog, 'u_Water', u)
                        gl('glDrawArrays', None, U, I, I)(4, 0, rig.water_count)
                    gl('glDisable', None, U)(0x0C11)
                print(f'  two visible synthetic bodies, half-screen each: {timed(two_bodies):.3f} ms')
            if ssr and hiz and steps == 24:
                reduced = tuple((n+1)//2 for n in size)
                maps = [tex2d(*reduced, RGBA16F), tex2d(*reduced, RGBA16F), tex2d(*reduced, RGBA16F)]
                depth = tex2d(*reduced, DEPTH24_STENCIL8, DEPTH_STENCIL, UINT_24_8)
                target = fbo(maps, depth)
                previous = [tex2d(*reduced, RGBA16F), tex2d(*reduced, RGBA16F), tex2d(*reduced, RGBA16F)]
                rig.draw_water(prog, u, waves=True, ssr=True,
                    reflection=dict(fbo=target, size=reduced, history=previous, **{'pass': [1, 0, .8, 2]}))
                gl('glDepthFunc', None, U)(0x0207)

                def reduced_pipeline():
                    for unit, texture in zip((12, 14, 15), previous): bind(unit, texture)
                    set_vec4s(prog, 'u_WaterPass', [[1, 0, .8, 2]])
                    gl('glBindFramebuffer', None, U, U)(FB, target)
                    gl('glViewport', None, I, I, I, I)(0, 0, *reduced)
                    gl('glDrawArrays', None, U, I, I)(4, 0, rig.water_count)
                    for unit, texture in zip((12, 14, 15), maps): bind(unit, texture)
                    set_vec4s(prog, 'u_WaterPass', [[0, 1, .8, 2]])
                    gl('glBindFramebuffer', None, U, U)(FB, rig.water_fbo)
                    gl('glViewport', None, I, I, I, I)(0, 0, *size)
                    gl('glDrawArrays', None, U, I, I)(4, 0, rig.water_count)
                print(f'  water half-resolution SSR + full-resolution shading/resolve (cold history): {timed(reduced_pipeline):.3f} ms')
            gl('glDepthFunc', None, U)(0x0203)
            gl('glDeleteProgram', None, U)(prog)

        # from below: the prompt-1 inside view against r_waterSnell (the window, TIR reflection sources)
        under = Rig(Camera((0.0, 0.0, -300.0), (300.0, 0.0, 0.0), size=size), underwater=True, size=size)
        coverage = (under.cam.rays()[..., 2] > 0).mean()
        for label, snell, ssr, hiz, steps in [('prompt-1', False, False, False, 0), ('Snell, no SSR', True, False, False, 0),
                                              ('Snell, SSR Hi-Z 24 steps', True, True, True, 24)]:
            under.ssr_steps = steps
            prog = water_program(ssr=ssr, hiz=hiz, size=size, snell=snell)
            u = under.params(flags=FLAG_REJECT | FLAG_ENVBRDF | (FLAG_SSR if ssr else 0), ssr=1.0 if ssr else 0.0)
            under.draw_water(prog, u, waves=True, ssr=ssr)

            def draw_under():
                gl('glBindFramebuffer', None, U, U)(FB, under.water_fbo)
                gl('glBindVertexArray', None, U)(under.water_vao)
                gl('glDrawArrays', None, U, I, I)(4, 0, under.water_count)
            gl('glDepthFunc', None, U)(0x0207)
            print(f'  water pass from below {size[0]}x{size[1]}, {coverage * 100:.0f}% of the view, waves, {label}: {timed(draw_under):.3f} ms')
            gl('glDepthFunc', None, U)(0x0203)
            gl('glDeleteProgram', None, U)(prog)
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    if '--ambient-only' in sys.argv:
        window, ctx = context(b'Water ambient waves')
        try:
            rig = Rig(Camera((0.0, 0.0, 100.0), (400.0, 0.0, 0.0)))
            sys.exit(0 if check_ambient_waves(rig) else 1)
        finally:
            SDL.SDL_GL_DeleteContext(ctx)
            SDL.SDL_DestroyWindow(window)
            SDL.SDL_Quit()
    sys.exit(bench() if '--bench' in sys.argv else main())
