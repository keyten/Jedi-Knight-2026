"""GPU checks of the r_clouds shaders on a hidden SDL GL 4.3 core context (Windows, bundled SDL2).

Run from any directory: python tools/rend2/test_clouds_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)

- compiles and links the cloud programs (noise, march, resolve, composite, shadow map) as GLSL_LoadGPUProgramClouds
  splices them, and lightall / volumetric_inject (raster + compute) with the cloud shadow lookup (USE_CLOUD_SHADOWS)
- noise: deterministic (two runs byte identical), tiling seams, statistics; histogram equalized coverage fractions
- shell intersection (below / inside / above the layer, grazing, ground) against a float64 reference
- homogeneous shell (test define CLOUD_TEST_HOMOGENEOUS): T = exp(-sigma L) for any step count, sigma 0 bit exact
- energy: isotropic single scattering without self shadow S = E (1 - T) / 4, multiple scattering octaves = 1.75 x,
  self shadow never adds light, T in [0, 1], S >= 0
- phase normalization of the dual lobe phase for several g
- NaN / Inf sweep over edge parameters with the real noise
- temporal resolve: reprojection (rotation, translation, wind) against the CPU, weight 0 exact, neighbourhood
  clamp, sentinels, convergence of a jittered coarse march towards a fine reference
- composite: S + T dst on sky pixels only, bit exact for an empty cloud, upsampling ignores pixels without sky,
  aerial perspective against the CPU model (atmosphere_check.py), sun ray mask
- cloud shadow map of a homogeneous layer against exp(-sigma H / sun z), legacy fx_cloudlayer masks
"""
import ctypes as C
import math
from pathlib import Path
import re
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import atmosphere_check as M  # noqa: E402

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
GL_VERTEX, GL_FRAGMENT, GL_GEOMETRY, GL_COMPUTE = 0x8B31, 0x8B30, 0x8DD9, 0x91B9
TEX2D, TEX3D = 0x0DE1, 0x806F
RGBA, RED, RG, FLOAT, UBYTE = 0x1908, 0x1903, 0x8227, 0x1406, 0x1401
RGBA32F, R32F, R8, RG8 = 0x8814, 0x822E, 0x8229, 0x822B
RGBA16F, R16F = 0x881A, 0x822D
FB = 0x8D40

FULL_W, FULL_H = 32, 24
LOW_W, LOW_H = 16, 12
CLOUD_VEC4S = 20
R_PLANET = 6360.0

# units of GLSL_LoadGPUProgramClouds
UNITS = {'u_ScreenDepthMap': 0, 'u_AtmosphereTransmittanceMap': 1, 'u_AtmosphereMultiScatterMap': 2,
         'u_AtmosphereSkyViewMap': 3, 'u_CloudShapeMap': 4, 'u_CloudDetailMap': 5, 'u_CloudWeatherMap': 6,
         'u_CloudCurrentMap': 7, 'u_CloudCurrentDepthMap': 8, 'u_CloudHistoryMap': 9,
         'u_CloudHistoryDepthMap': 10, 'u_CloudShadowMap': 9}


def read(name):
    return (GLSL / f'{name}.glsl').read_text()


def vertex(name):
    return read(name).split('/*[Vertex]*/')[1].split('/*[')[0]


def fragment(name):
    return read(name).split('/*[Fragment]*/')[1]


def compile_program(sources, label, outputs=('out_Color', 'out_Glow')):
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
    if all(kind != GL_COMPUTE for kind, _ in sources):
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


def cloud_program(name, defines='', body=None):
    libraries = {'clouds_noise': [], 'clouds_resolve': ['clouds_common'], 'clouds_shadow': ['clouds_common']}
    library = ''.join(fragment(l) + '\n' for l in libraries.get(name, ['atmosphere_common', 'clouds_common']))
    header = ('#version 150 core\n#define M_PI 3.14159265358979323846\n'
              f'#define r_FBufScale vec2({FULL_W}.0, {FULL_H}.0)\n' + defines)
    frag = body if body is not None else fragment(name)
    prog = compile_program([(GL_VERTEX, header + vertex('clouds_march')),
                            (GL_FRAGMENT, header + library + frag)], name + ' ' + defines.replace('\n', ' '))
    gl('glUseProgram', None, U)(prog)
    for sampler, unit in UNITS.items():
        loc = uloc(prog, sampler)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    return prog


# --- textures / framebuffers ------------------------------------------------------------------------------------

def tex2d(w, h, internal=RGBA32F, fmt=RGBA, kind=FLOAT, data=None, linear=False):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(TEX2D, tex)
    raw = None
    if data is not None:
        arr = np.ascontiguousarray(data, dtype=np.float32 if kind == FLOAT else np.uint8)
        raw = arr.ctypes.data_as(P)
    gl('glPixelStorei', None, U, I)(0x0CF5, 1)
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, internal, w, h, 0, fmt, kind, raw)
    filt = 0x2601 if linear else 0x2600
    for parameter, value in [(0x2800, filt), (0x2801, filt), (0x2802, 0x812F), (0x2803, 0x812F)]:
        gl('glTexParameteri', None, U, U, I)(TEX2D, parameter, value)
    return tex.value


def tex3d(w, h, d, internal, fmt, data=None, mips=False):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(TEX3D, tex)
    raw = np.ascontiguousarray(data, dtype=np.uint8).ctypes.data_as(P) if data is not None else None
    gl('glPixelStorei', None, U, I)(0x0CF5, 1)
    gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(TEX3D, 0, internal, w, h, d, 0, fmt, UBYTE, raw)
    for parameter in [0x2802, 0x2803, 0x8072]:
        gl('glTexParameteri', None, U, U, I)(TEX3D, parameter, 0x2901)  # repeat
    gl('glTexParameteri', None, U, U, I)(TEX3D, 0x2801, 0x2703 if mips else 0x2601)
    gl('glTexParameteri', None, U, U, I)(TEX3D, 0x2800, 0x2601)
    if mips and data is not None:
        gl('glGenerateMipmap', None, U)(TEX3D)
    return tex.value


def fbo(textures):
    f = U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(f))
    gl('glBindFramebuffer', None, U, U)(FB, f)
    for i, tex in enumerate(textures):
        gl('glFramebufferTexture2D', None, U, U, U, U, I)(FB, 0x8CE0 + i, TEX2D, tex, 0)
    bufs = (U * len(textures))(*[0x8CE0 + i for i in range(len(textures))])
    gl('glDrawBuffers', None, I, P)(len(textures), bufs)
    assert gl('glCheckFramebufferStatus', U, U)(FB) == 0x8CD5
    return f.value


def read2d(tex, w, h, channels=4):
    gl('glBindTexture', None, U, U)(TEX2D, tex)
    data = np.zeros((h, w, 4), dtype=np.float32)
    gl('glPixelStorei', None, U, I)(0x0D05, 4)
    gl('glGetTexImage', None, U, I, U, U, P)(TEX2D, 0, RGBA, FLOAT, data.ctypes.data_as(P))
    return data.astype(np.float64)[:, :, :channels]


def read3d(tex, w, h, d, channels):
    gl('glBindTexture', None, U, U)(TEX3D, tex)
    data = np.zeros((d, h, w, channels), dtype=np.uint8)
    gl('glPixelStorei', None, U, I)(0x0D05, 1)
    gl('glGetTexImage', None, U, I, U, U, P)(TEX3D, 0, RED if channels == 1 else RG, UBYTE, data.ctypes.data_as(P))
    return data


def bind(unit, tex, target=TEX2D):
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    gl('glBindTexture', None, U, U)(target, tex)
    gl('glActiveTexture', None, U)(0x84C0)


def draw(framebuffer, x, y, w, h):
    gl('glBindFramebuffer', None, U, U)(FB, framebuffer)
    gl('glViewport', None, I, I, I, I)(x, y, w, h)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    gl('glFinish', None)()


def set_vec4s(prog, name, rows):
    loc = uloc(prog, name)
    if loc < 0:
        return
    flat = np.ascontiguousarray(np.asarray(rows, dtype=np.float32).reshape(-1))
    gl('glUniform4fv', None, I, I, P)(loc, len(flat) // 4, flat.ctypes.data_as(P))


def set_matrix(prog, name, m):
    loc = uloc(prog, name)
    if loc < 0:
        return
    flat = np.ascontiguousarray(np.asarray(m, dtype=np.float32).T.reshape(-1))  # column major
    gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0, flat.ctypes.data_as(P))


# --- camera -----------------------------------------------------------------------------------------------------

def perspective(fov_y, aspect, near, far):
    y = 1.0 / math.tan(math.radians(fov_y) * 0.5)
    return np.array([[y / aspect, 0, 0, 0], [0, y, 0, 0],
                     [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)], [0, 0, -1, 0]])


def view(eye, forward, up_hint=(0.0, 0.0, 1.0)):
    forward = np.asarray(forward, dtype=np.float64)
    forward = forward / np.linalg.norm(forward)
    up_hint = np.asarray(up_hint, dtype=np.float64)
    if abs(forward @ up_hint) > 0.99:
        up_hint = np.array([1.0, 0.0, 0.0])
    left = np.cross(up_hint, forward)
    left /= np.linalg.norm(left)
    up = np.cross(forward, left)
    R = np.array([-left, up, -forward])
    m = np.eye(4)
    m[:3, :3] = R
    m[:3, 3] = -R @ np.asarray(eye, dtype=np.float64)
    return m


class Camera:
    def __init__(self, eye, forward, fov=90.0):
        self.eye = np.asarray(eye, dtype=np.float64)
        projection = perspective(fov, LOW_W / LOW_H, 4.0, 65536.0)
        self.vp = projection @ view(eye, forward)
        # camera relative inverse, as RB_CloudsComposite (u_CloudInvViewProjection)
        self.inv = np.linalg.inv(projection @ view((0.0, 0.0, 0.0), forward))

    def ray(self, px, py, w=LOW_W, h=LOW_H):
        ndc = np.array([(px + 0.5) / w * 2 - 1, (py + 0.5) / h * 2 - 1])
        near = self.inv @ np.array([ndc[0], ndc[1], -1.0, 1.0])
        far = self.inv @ np.array([ndc[0], ndc[1], 1.0, 1.0])
        d = far[:3] / far[3] - near[:3] / near[3]
        return d / np.linalg.norm(d)


KM_PER_UNIT = 0.03 * 0.001
GROUND_Z = 0.0


def params(**kw):
    """u_Cloud as RB_CloudsUniforms packs it, defaults of the cvars"""
    p = dict(base=1.5, top=4.0, coverage=0.5, extinction=60.0, g=0.6, back_g=-0.3, forward=0.8,
             shape_tile=12.0, detail_tile=1.5, weather_tile=40.0, detail=1.0,
             shape_off=(0.0, 0.0, 0.0), detail_off=(0.0, 0.0, 0.0), weather_off=(0.0, 0.0), max_distance=60.0,
             octaves=3, light_length=2.5, sun=(0.3, 0.2, 0.93), shadow_steps=5, sun_color=(1.0, 1.0, 1.0),
             atmosphere=0.0, ambient_sky=(0.0, 0.0, 0.0), ambient_scale=0.0, ambient_ground=(0.0, 0.0, 0.0),
             ground_altitude=0.0, camera=(0.0, 0.0, 300.0), steps=64, step_length=0.15, min_steps=16,
             legacy=(0.0, 0.0, 0.0, 0.0), legacy_mode=(0.0, 0.0, 0.0, 1.0), debug=0, weight=0.0, frame=0,
             scale=2.0, shadow=(0.0, 0.0, 8.0, 10.0), mode=0.0, wind=(0.0, 0.0), dt=0.0)
    p.update(kw)
    sun = np.asarray(p['sun'], dtype=np.float64)
    sun /= np.linalg.norm(sun)
    cam = np.asarray(p['camera'], dtype=np.float64)
    cam_km = np.array([cam[0] * KM_PER_UNIT, cam[1] * KM_PER_UNIT, (cam[2] - GROUND_Z) * KM_PER_UNIT])
    u = np.zeros((CLOUD_VEC4S, 4))
    u[0] = [p['base'], p['top'], R_PLANET, p['coverage']]
    u[1] = [p['extinction'], p['g'], p['back_g'], p['forward']]
    u[2] = [p['shape_tile'], p['detail_tile'], p['weather_tile'], p['detail']]
    u[3] = [*p['shape_off'], 0.0]
    u[4] = [*p['detail_off'], p['max_distance']]
    u[5] = [*p['weather_off'], p['octaves'], p['light_length']]
    u[6] = [*sun, p['shadow_steps']]
    u[7] = [*p['sun_color'], p['atmosphere']]
    u[8] = [*p['ambient_sky'], p['ambient_scale']]
    u[9] = [*p['ambient_ground'], p['ground_altitude']]
    u[10] = [*cam_km, p['steps']]
    u[11] = [p['step_length'], p['min_steps'], KM_PER_UNIT, GROUND_Z]
    u[12] = list(p['legacy'])
    u[13] = list(p['legacy_mode'])
    u[14] = [0.0, 0.0, 1.0, 1.0]
    u[15] = [0.0, 0.0, LOW_W, LOW_H]
    u[16] = [p['debug'], p['weight'], p['frame'], p['scale']]
    u[17] = list(p['shadow'])
    u[18] = [p['mode'], p['atmosphere'], *p['wind']]
    u[19] = [*cam, p['dt']]
    return u, cam_km, sun


# --- float64 references -------------------------------------------------------------------------------------------

def altitude(p):
    return math.sqrt(p[0] ** 2 + p[1] ** 2 + (p[2] + R_PLANET) ** 2) - R_PLANET


def sphere(o, d, h):
    oc = np.array([o[0], o[1], o[2] + R_PLANET])
    b = oc @ d
    c = oc @ oc - (R_PLANET + h) ** 2
    disc = b * b - c
    if disc < 0:
        return None
    s = math.sqrt(disc)
    return -b - s, -b + s


def interval(o, d, base, top, max_distance):
    """CloudInterval in float64"""
    ho = altitude(o)
    if top <= base:
        return None
    ground = 1e9
    g = sphere(o, d, 0.0)
    if ho > 0 and d[2] < 0 and g is not None and g[0] > 0:
        ground = g[0]
    b = sphere(o, d, base)
    t = sphere(o, d, top)
    if ho < base:
        if b is None or t is None:
            return None
        start, end = b[1], t[1]
        if ground < start:
            return None
    elif ho <= top:
        if t is None:
            return None
        start, end = 0.0, t[1]
        if b is not None and b[0] > 0:
            end = min(end, b[0])
        end = min(end, ground)
    else:
        if t is None or t[1] <= 0:
            return None
        start = max(t[0], 0.0)
        end = b[0] if (b is not None and b[0] > 0) else t[1]
    end = min(end, max_distance)
    return (start, end) if end > start else None


# --- the passes ---------------------------------------------------------------------------------------------------

class Rig:
    def __init__(self):
        self.depth = tex2d(FULL_W, FULL_H, R32F, RED, FLOAT, np.ones((FULL_H, FULL_W), dtype=np.float32))
        self.march = tex2d(LOW_W, LOW_H)
        self.march_d = tex2d(LOW_W, LOW_H, R32F, RED)
        self.march_fbo = fbo([self.march, self.march_d])
        self.hist = [tex2d(LOW_W, LOW_H) for _ in range(2)]
        self.hist_d = [tex2d(LOW_W, LOW_H, R32F, RED) for _ in range(2)]
        self.hist_fbo = [fbo([self.hist[i], self.hist_d[i]]) for i in range(2)]
        blank3 = np.full((1, 1, 1, 1), 255, dtype=np.uint8)
        self.white3d = tex3d(1, 1, 1, R8, RED, blank3)
        self.white_rg = tex3d(1, 1, 1, RG8, RG, np.full((1, 1, 1, 2), 255, dtype=np.uint8))

    def bind_noise(self, shape=None, detail=None, weather=None):
        bind(4, shape or self.white3d, TEX3D)
        bind(5, detail or self.white3d, TEX3D)
        bind(6, weather or self.white_rg, TEX3D)

    def run_march(self, prog, u, cam, noise=(None, None, None)):
        gl('glUseProgram', None, U)(prog)
        set_vec4s(prog, 'u_Cloud', u)
        set_matrix(prog, 'u_CloudInvViewProjection', cam.inv)
        bind(0, self.depth)
        self.bind_noise(*noise)
        draw(self.march_fbo, 0, 0, LOW_W, LOW_H)
        return read2d(self.march, LOW_W, LOW_H), read2d(self.march_d, LOW_W, LOW_H, 1)[:, :, 0]


def check(name, ok, detail=''):
    print(f'{"PASS" if ok else "FAIL"}: {name}{(" - " + detail) if detail else ""}')
    return ok


# --- noise ------------------------------------------------------------------------------------------------------

RG32F = 0x8230


def generate_noise(prog):
    """RB_CloudsGenerateNoise before the equalization: float32 fields per target"""
    out = {}
    gl('glUseProgram', None, U)(prog)
    for target, size, depth, channels, internal, fmt in [(0, 128, 128, 1, R32F, RED), (1, 32, 32, 1, R32F, RED),
                                                          (2, 512, 1, 2, RG32F, RG)]:
        tex = U()
        gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
        gl('glBindTexture', None, U, U)(TEX3D, tex)
        gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(TEX3D, 0, internal, size, size, depth, 0, fmt, FLOAT, None)
        gl('glTexParameteri', None, U, U, I)(TEX3D, 0x2801, 0x2600)
        gl('glTexParameteri', None, U, U, I)(TEX3D, 0x2800, 0x2600)
        tex = tex.value
        f = U()
        gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(f))
        gl('glBindFramebuffer', None, U, U)(FB, f)
        gl('glViewport', None, I, I, I, I)(0, 0, size, size)
        for z in range(depth):
            gl('glFramebufferTexture3D', None, U, U, U, U, I, I)(FB, 0x8CE0, TEX3D, tex, 0, z)
            if z == 0:
                assert gl('glCheckFramebufferStatus', U, U)(FB) == 0x8CD5
            u = np.zeros((CLOUD_VEC4S, 4))
            u[0] = [target, z, size, 0]
            set_vec4s(prog, 'u_Cloud', u)
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        gl('glFinish', None)()
        gl('glBindTexture', None, U, U)(TEX3D, tex)
        data = np.zeros((depth, size, size, channels), dtype=np.float32)
        gl('glPixelStorei', None, U, I)(0x0D05, 1)
        gl('glGetTexImage', None, U, I, U, U, P)(TEX3D, 0, fmt, FLOAT, data.ctypes.data_as(P))
        out[target] = data
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(tex)))
        gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(f))
    return out


def equalize(values, bins=4096):
    """RB_CloudsEqualize: rank of each value (bins between min and max, linear inside a bin) -> bytes"""
    flat = values.reshape(-1).astype(np.float32)
    lo, hi = float(flat.min()), float(flat.max())
    scale = np.float32(bins / (hi - lo)) if hi > lo else np.float32(0.0)
    x = (flat - np.float32(lo)) * scale
    b = np.minimum(bins - 1, x.astype(np.int64))
    hist = np.bincount(b, minlength=bins).astype(np.float64)
    below = np.concatenate([[0.0], np.cumsum(hist)[:-1]])
    rank = (below[b] + hist[b] * np.clip(x - b, 0.0, 1.0)) / flat.size
    return np.clip((rank * 256.0).astype(np.int64), 0, 255).astype(np.uint8).reshape(values.shape)


def check_noise(prog):
    first = generate_noise(prog)
    second = generate_noise(prog)
    ok = True
    for target in range(3):
        same = np.array_equal(first[target], second[target])
        ok = check(f'noise target {target} deterministic (two runs bit identical)', same) and ok
    shape = first[0][..., 0].astype(np.float64)
    detail = first[1][..., 0].astype(np.float64)
    weather = first[2][0].astype(np.float64)
    for label, field, axes in [('shape', shape, (0, 1, 2)), ('detail', detail, (0, 1, 2)), ('weather r', weather[..., 0], (0, 1))]:
        ratios = []
        for axis in axes:
            interior = np.mean(np.abs(np.diff(field, axis=axis)))
            seam = np.mean(np.abs(np.take(field, 0, axis=axis) - np.take(field, -1, axis=axis)))
            ratios.append(seam / max(interior, 1e-9))
        ok = check(f'{label} noise tiles (seam / interior neighbour difference {", ".join(f"{r:.2f}" for r in ratios)})',
                   all(0.75 < r < 1.33 for r in ratios)) and ok
        print(f'      {label}: mean {field.mean():.3f}, std {field.std():.3f}, min {field.min():.2f}, max {field.max():.2f}')
    # equalized bytes (as the engine): even use of the 256 levels, coverage reads as a cloud fraction
    textures = {}
    for target, size, depth, channels, internal, fmt in [(0, 128, 128, 1, R8, RED), (1, 32, 32, 1, R8, RED),
                                                          (2, 512, 1, 2, RG8, RG)]:
        data = np.zeros(first[target].shape, dtype=np.uint8)
        data[..., 0] = equalize(first[target][..., 0])
        if channels > 1:
            data[..., 1] = np.clip((first[target][..., 1] * 255.0 + 0.5).astype(np.int64), 0, 255)
        counts = np.bincount(data[..., 0].reshape(-1), minlength=256)
        spread = counts.max() / max(counts.mean(), 1e-9)
        eq = data[..., 0].astype(np.float64) / 255.0
        fractions = [(c, float(np.mean(eq > 1.0 - c))) for c in (0.25, 0.5, 0.75)]
        ok = check(f'equalized target {target}: {np.count_nonzero(counts)} levels used (fullest {spread:.2f} x the mean), '
                   'coverage c leaves ~c above 1 - c (' + ', '.join(f'{c}: {f:.3f}' for c, f in fractions) + ')',
                   np.count_nonzero(counts) == 256 and spread < 1.6 and all(abs(f - c) < 0.02 for c, f in fractions)) and ok
        textures[target] = tex3d(size, size, depth, internal, fmt, data, mips=True)
    return ok, textures


# --- tests ---------------------------------------------------------------------------------------------------------

def check_interval(rig, prog):
    ok = True
    worst = 0.0
    misses = 0
    for eye_z, forward, label in [(300.0, (1.0, 0.2, 0.15), 'below the layer'),
                                  (2.5 / KM_PER_UNIT, (1.0, 0.0, 0.0), 'inside the layer'),
                                  (6.0 / KM_PER_UNIT, (1.0, 0.0, -0.2), 'above the layer'),
                                  (300.0, (1.0, 0.0, -0.4), 'below, looking down')]:
        cam = Camera((1000.0, -500.0, eye_z), forward, 100.0)
        u, cam_km, _ = params(debug=1, camera=cam.eye, max_distance=400.0)
        color, dist = rig.run_march(prog, u, cam)
        for py in range(LOW_H):
            for px in range(LOW_W):
                d = cam.ray(px, py)
                ref = interval(cam_km, d, 1.5, 4.0, 400.0)
                hit = color[py, px, 2] < 0.1
                if (ref is not None) != hit:
                    # grazing rays may flip within float precision
                    misses += 1
                    continue
                if ref is None:
                    continue
                t0 = dist[py, px]
                t1 = t0 + color[py, px, 1] * 400.0 / 4.0
                # conditioning: how far the interval moves for a 3e-5 rad error of the float32 ray direction (camera
                # relative inverse view-projection; the full inverse of a camera ~80000 units out was ~1e-4 rad)
                spread = 0.0
                for axis in range(3):
                    for sign in (-1.0, 1.0):
                        dp = d.copy()
                        dp[axis] += sign * 3e-5
                        dp /= np.linalg.norm(dp)
                        rp = interval(cam_km, dp, 1.5, 4.0, 400.0)
                        if rp is None:
                            spread = 1e9
                        else:
                            spread = max(spread, abs(rp[0] - ref[0]), abs(rp[1] - ref[1]))
                if spread > 1.0:
                    continue    # grazing: ill conditioned
                err = max(abs(t0 - ref[0]), abs(t1 - ref[1])) / (2e-3 + spread)
                worst = max(worst, err)
        print(f'      interval {label}: worst error / (2 m + conditioning) {worst:.2f}, hit/miss disagreements {misses}')
    ok = check('shell intersection (below / inside / above / ground) against float64', worst < 1.0 and misses <= 2) and ok
    return ok


def check_homogeneous(rig):
    ok = True
    progs = {
        'plain': cloud_program('clouds_march', '#define CLOUD_TEST_HOMOGENEOUS\n#define CLOUD_TEST_NO_SHADOW\n'),
        'shadow': cloud_program('clouds_march', '#define CLOUD_TEST_HOMOGENEOUS\n'),
    }
    cam = Camera((0.0, 0.0, 300.0), (0.2, 0.1, 1.0), 70.0)
    sigma = 0.8
    worst = 0.0
    for steps, step_length in [(8, 5.0), (64, 0.15), (256, 0.01), (1, 10.0)]:
        u, cam_km, _ = params(extinction=sigma, steps=steps, min_steps=min(16, steps), step_length=step_length,
                              camera=cam.eye, max_distance=500.0, sun=(0, 0, 1), g=0.0, back_g=0.0, octaves=1)
        color, _ = rig.run_march(progs['plain'], u, cam)
        for py in range(LOW_H):
            for px in range(LOW_W):
                d = cam.ray(px, py)
                ref = interval(cam_km, d, 1.5, 4.0, 500.0)
                T = math.exp(-sigma * (ref[1] - ref[0]))
                worst = max(worst, abs(color[py, px, 3] - T) / T)
    ok = check(f'homogeneous shell: T = exp(-sigma L) for 1 / 8 / 64 / 256 steps (worst relative error {worst:.1e})',
               worst < 1e-4) and ok

    u, _, _ = params(extinction=0.0, camera=cam.eye, max_distance=500.0)
    color, _ = rig.run_march(progs['plain'], u, cam)
    ok = check('sigma 0: S = 0 and T = 1 exactly', bool(np.all(color[:, :, :3] == 0.0) and np.all(color[:, :, 3] == 1.0))) and ok

    # energy: isotropic, no self shadow, no ambient, white sun at the zenith: S = E pi (1 / 4 pi)(1 - T)
    base = dict(extinction=sigma, camera=cam.eye, max_distance=500.0, sun=(0, 0, 1), g=0.0, back_g=0.0)
    u1, _, _ = params(octaves=1, **base)
    single, _ = rig.run_march(progs['plain'], u1, cam)
    expected = (1.0 - single[:, :, 3]) / 4.0
    err = np.max(np.abs(single[:, :, 0] - expected) / np.maximum(expected, 1e-9))
    ok = check(f'single scattering energy S = E (1 - T) / 4 (worst relative error {err:.1e})', err < 1e-3) and ok
    u3, _, _ = params(octaves=3, **base)
    multi, _ = rig.run_march(progs['plain'], u3, cam)
    ratio = multi[:, :, 0] / np.maximum(single[:, :, 0], 1e-12)
    ok = check(f'multiple scattering octaves without self shadow: 1 + 0.5 + 0.25 = 1.75 x (got {ratio.min():.5f} - {ratio.max():.5f})',
               bool(np.all(np.abs(ratio - 1.75) < 1e-4))) and ok
    us, _, _ = params(octaves=3, **base)
    shadowed, _ = rig.run_march(progs['shadow'], us, cam)
    ok = check('self shadow never adds light, T and S in range',
               bool(np.all(shadowed[:, :, :3] <= multi[:, :, :3] + 1e-6) and np.all(shadowed[:, :, :3] >= 0.0) and
                    np.all((shadowed[:, :, 3] >= 0.0) & (shadowed[:, :, 3] <= 1.0)))) and ok
    for prog in progs.values():
        gl('glDeleteProgram', None, U)(prog)
    return ok


def check_phase():
    n = 8192
    body = '''
out vec4 out_Color;
out vec4 out_Glow;
void main()
{
	float mu = -1.0 + 2.0 * gl_FragCoord.x / float(%d);
	out_Color = vec4(CloudPhase(mu, 1.0), CloudPhase(mu, 0.5), CloudHG(u_Cloud[1].y, mu), 1.0);
	out_Glow = vec4(0.0);
}
''' % n
    prog = cloud_program('clouds_march', body=body)
    tex = tex2d(n, 1)
    tex_b = tex2d(n, 1)
    f = fbo([tex, tex_b])
    ok = True
    results = []
    for g in (-0.5, 0.0, 0.6, 0.9):
        u, _, _ = params(g=g)
        gl('glUseProgram', None, U)(prog)
        set_vec4s(prog, 'u_Cloud', u)
        draw(f, 0, 0, n, 1)
        values = read2d(tex, n, 1)[0]
        integrals = 2.0 * math.pi * values[:, :3].sum(axis=0) * (2.0 / n)
        results.append((g, integrals))
    worst = max(abs(i - 1.0) for _, ints in results for i in ints)
    ok = check('phase functions integrate to 1 (' + ', '.join(f'g {g}: {ints[0]:.4f} / {ints[1]:.4f} / HG {ints[2]:.4f}'
                                                            for g, ints in results) + ')', worst < 2e-3) and ok
    gl('glDeleteProgram', None, U)(prog)
    return ok


def check_sweep(rig, prog, noise):
    ok = True
    cases = [
        dict(), dict(sun=(0.2, 0.0, -0.3)), dict(sun=(0, 0, 1)), dict(coverage=0.0), dict(coverage=1.0),
        dict(extinction=6e5), dict(top=1.5, base=1.5), dict(steps=1, min_steps=1), dict(max_distance=0.001),
        dict(detail=0.0), dict(octaves=4, shadow_steps=16), dict(legacy_mode=(1, 1, 0, 1)),
        dict(legacy_mode=(2, 1, 1, 0.25), legacy=(0.0, 0.0, 3.0, 1.0)), dict(ambient_scale=1.0, ambient_sky=(0.2, 0.3, 0.4)),
        dict(shape_off=(0.999, 0.5, 0.25), weather_off=(0.99, 0.01), detail_off=(0.5, 0.5, 0.999)),
    ]
    cameras = [Camera((0, 0, 300.0), (1.0, 0.0, 0.0), 120.0), Camera((0, 0, 2.5 / KM_PER_UNIT), (0.3, 1.0, 0.1), 120.0),
               Camera((0, 0, 9.0 / KM_PER_UNIT), (0.0, 1.0, -1.0), 120.0), Camera((0, 0, 300.0), (0.0, 0.0, 1.0), 120.0)]
    bad = 0
    for case in cases:
        for cam in cameras:
            for debug in (0, 2, 5, 9):
                u, _, _ = params(camera=cam.eye, debug=debug, **case)
                color, dist = rig.run_march(prog, u, cam, (noise[0], noise[1], noise[2]))
                finite = np.all(np.isfinite(color)) and np.all(np.isfinite(dist))
                ranges = debug != 0 or (np.all(color[:, :, :3] >= 0.0) and np.all((color[:, :, 3] >= 0.0) & (color[:, :, 3] <= 1.0)))
                if not (finite and ranges):
                    bad += 1
                    print('      bad case', case, cam.eye, 'debug', debug)
    return check(f'NaN / Inf / range sweep: {len(cases) * len(cameras) * 4} marches with the real noise', bad == 0) and ok


def check_temporal(rig, resolve, march, noise):
    ok = True
    gl('glUseProgram', None, U)(resolve)
    cur_tex, cur_d = rig.march, rig.march_d

    def upload(tex, data, channels=4):
        gl('glBindTexture', None, U, U)(TEX2D, tex)
        arr = np.ascontiguousarray(data, dtype=np.float32)
        gl('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, 0, 0, LOW_W, LOW_H, RGBA if channels == 4 else RED,
                                                              FLOAT, arr.ctypes.data_as(P))

    def run(u, cam, prev_vp):
        gl('glUseProgram', None, U)(resolve)
        set_vec4s(resolve, 'u_Cloud', u)
        set_matrix(resolve, 'u_CloudInvViewProjection', cam.inv)
        set_matrix(resolve, 'u_CloudPrevViewProjection', prev_vp)
        bind(7, cur_tex)
        bind(8, cur_d)
        bind(9, rig.hist[0])
        bind(10, rig.hist_d[0])
        draw(rig.hist_fbo[1], 0, 0, LOW_W, LOW_H)
        return read2d(rig.hist[1], LOW_W, LOW_H), read2d(rig.hist_d[1], LOW_W, LOW_H, 1)[:, :, 0]

    yy, xx = np.mgrid[0:LOW_H, 0:LOW_W]
    D = 5.0
    # checkerboard current: the 3x3 clamp spans [0, 1000] everywhere
    checker = ((xx + yy) % 2).astype(np.float64) * 1000.0
    current = np.stack([checker] * 4, axis=-1)
    upload(cur_tex, current)
    upload(cur_d, np.full((LOW_H, LOW_W), D), 1)
    history = np.stack([(xx + 0.5) * 10.0, (yy + 0.5) * 10.0, np.full_like(xx, 7.0), np.full_like(xx, 0.5)], axis=-1)
    upload(rig.hist[0], history)
    upload(rig.hist_d[0], np.full((LOW_H, LOW_W), D), 1)

    prev = Camera((100.0, 50.0, 300.0), (1.0, 0.03, 0.4), 90.0)
    cam = Camera((140.0, 20.0, 310.0), (1.0, -0.02, 0.42), 90.0)
    w = 0.9
    wind = (300.0, -200.0)
    dt = 0.05
    u, _, _ = params(camera=cam.eye, weight=w, wind=wind, dt=dt)
    out, out_d = run(u, cam, prev.vp)
    recovered = (out - (1.0 - w) * current) / w
    worst = 0.0
    checked = 0
    invalid_ok = True
    for py in range(LOW_H):
        for px in range(LOW_W):
            d = cam.ray(px, py)
            point = cam.eye + d * (D / KM_PER_UNIT)
            point[:2] -= np.array(wind) * dt
            clip = prev.vp @ np.array([*point, 1.0])
            uv = clip[:2] / clip[3] * 0.5 + 0.5
            pos = uv * np.array([LOW_W, LOW_H])
            if clip[3] > 0 and np.all(pos > 1.0) and np.all(pos < np.array([LOW_W, LOW_H]) - 1.0):
                err = np.max(np.abs(recovered[py, px, :2] / 10.0 - pos))
                worst = max(worst, err)
                checked += 1
            elif clip[3] <= 0 or np.any(uv < 0) or np.any(uv > 1):
                invalid_ok = invalid_ok and bool(np.all(out[py, px] == current[py, px]))
    ok = check(f'reprojection (rotation, translation, wind) of {checked} pixels against the CPU: worst {worst:.1e} px',
               checked > 40 and worst < 2e-3) and ok
    ok = check('history outside the previous view: weight 0, the current march exactly', invalid_ok) and ok

    u0, _, _ = params(camera=cam.eye, weight=0.0)
    out0, out0_d = run(u0, cam, prev.vp)
    ok = check('weight 0: resolve output == march exactly', bool(np.array_equal(out0, current) and np.all(out0_d == D))) and ok

    # clamp: uniform current c, history 2c -> c
    upload(cur_tex, np.full((LOW_H, LOW_W, 4), 0.25))
    upload(rig.hist[0], np.full((LOW_H, LOW_W, 4), 0.5))
    same = Camera((100.0, 50.0, 300.0), (1.0, 0.0, 0.3), 90.0)
    u, _, _ = params(camera=same.eye, weight=0.9)
    outc, _ = run(u, same, same.vp)
    ok = check('neighbourhood clamp keeps the history inside this frame\'s 3x3 range', bool(np.allclose(outc, 0.25, atol=1e-7))) and ok

    # sentinels: no sky in the current pixel -> (0, 0, 0, 1), distance -1
    dist = np.full((LOW_H, LOW_W), D)
    dist[3:6, 4:9] = -1.0
    upload(cur_d, dist, 1)
    outs, outs_d = run(u, same, same.vp)
    sentinel = bool(np.all(outs[3:6, 4:9] == np.array([0, 0, 0, 1])) and np.all(outs_d[3:6, 4:9] == -1.0))
    ok = check('pixels without sky stay sentinels through the resolve', sentinel) and ok

    # convergence: the history averages the jitter noise of a coarse march (static camera, real noise); the
    # reference is the mean of the same jittered estimator over its 64 frame cycle, the bias of the coarse steps
    # against a fine march is reported separately
    cam = Camera((0.0, 0.0, 300.0), (1.0, 0.3, 0.35), 90.0)
    coarse = dict(camera=cam.eye, steps=12, min_steps=12, step_length=10.0, coverage=0.6)
    raw = []
    for frame in range(64):
        u, _, _ = params(frame=frame, **coarse)
        raw.append(rig.run_march(march, u, cam, noise)[0])
    reference = np.mean(raw, axis=0)
    fine_u, _, _ = params(camera=cam.eye, steps=256, min_steps=256, step_length=0.001, coverage=0.6)
    fine, _ = rig.run_march(march, fine_u, cam, noise)
    errors = []
    current_hist = 0
    for frame in range(32):
        u, _, _ = params(frame=frame, weight=0.9 if frame > 0 else 0.0, **coarse)
        rig.run_march(march, u, cam, noise)
        gl('glUseProgram', None, U)(resolve)
        set_vec4s(resolve, 'u_Cloud', u)
        set_matrix(resolve, 'u_CloudInvViewProjection', cam.inv)
        set_matrix(resolve, 'u_CloudPrevViewProjection', cam.vp)
        bind(7, rig.march)
        bind(8, rig.march_d)
        bind(9, rig.hist[current_hist])
        bind(10, rig.hist_d[current_hist])
        draw(rig.hist_fbo[current_hist ^ 1], 0, 0, LOW_W, LOW_H)
        current_hist ^= 1
        resolved = read2d(rig.hist[current_hist], LOW_W, LOW_H)
        errors.append(math.sqrt(np.mean((resolved - reference) ** 2)))
    # single frames fluctuate: mean RMSE of the raw frames against the last 8 resolved ones
    raw_rmse = float(np.mean([math.sqrt(np.mean((r - reference) ** 2)) for r in raw]))
    resolved_rmse = float(np.mean(errors[-8:]))
    gain = raw_rmse / max(resolved_rmse, 1e-12)
    bias = math.sqrt(np.mean((reference - fine) ** 2))
    print('      RMSE per frame:', ' '.join(f'{e:.4f}' for e in errors[:6]), '...', ' '.join(f'{e:.4f}' for e in errors[-4:]))
    ok = check(f'temporal filter of the jitter noise (12 steps, weight 0.9, widened clamp): RMSE to the estimator mean, '
               f'raw {raw_rmse:.4f} -> resolved {resolved_rmse:.4f} ({gain:.1f} x); coarse step bias vs 256 steps {bias:.4f}',
               gain >= 1.75) and ok
    return ok


def check_composite(rig, composite, A):
    ok = True
    color = tex2d(FULL_W, FULL_H)
    glow = tex2d(FULL_W, FULL_H)
    f = fbo([color, glow])
    depth = np.ones((FULL_H, FULL_W), dtype=np.float32)
    depth[10:16, 6:20] = 0.5            # geometry
    depth_tex = tex2d(FULL_W, FULL_H, R32F, RED, FLOAT, depth)
    # binary exact values (the bit exact checks compare against these)
    dst = np.full((FULL_H, FULL_W, 4), 0.375)
    dst[..., 1] = 0.625
    dst[..., 3] = 0.875
    glow_dst = np.full((FULL_H, FULL_W, 4), 0.75)

    def upload(tex, data, w, h, fmt=RGBA):
        gl('glBindTexture', None, U, U)(TEX2D, tex)
        arr = np.ascontiguousarray(data, dtype=np.float32)
        gl('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, 0, 0, w, h, fmt, FLOAT, arr.ctypes.data_as(P))

    def run(clouds, dists, u, cam, atmosphere=None):
        upload(color, dst, FULL_W, FULL_H)
        upload(glow, glow_dst, FULL_W, FULL_H)
        upload(rig.hist[0], clouds, LOW_W, LOW_H)
        upload(rig.hist_d[0], dists, LOW_W, LOW_H, RED)
        gl('glUseProgram', None, U)(composite)
        set_vec4s(composite, 'u_Cloud', u)
        set_matrix(composite, 'u_CloudInvViewProjection', cam.inv)
        if atmosphere is not None:
            atmosphere()
        bind(0, depth_tex)
        bind(7, rig.hist[0])
        bind(8, rig.hist_d[0])
        gl('glEnable', None, U)(0x0BE2)
        gl('glBlendFunc', None, U, U)(1, 0x0302)  # ONE, SRC_ALPHA
        gl('glColorMask', None, U, U, U, U)(1, 1, 1, 0)
        draw(f, 0, 0, FULL_W, FULL_H)
        gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
        gl('glDisable', None, U)(0x0BE2)
        return read2d(color, FULL_W, FULL_H), read2d(glow, FULL_W, FULL_H)

    cam = Camera((0.0, 0.0, 300.0), (1.0, 0.0, 0.3), 90.0)
    rng = np.random.RandomState(7)
    clouds = np.zeros((LOW_H, LOW_W, 4))
    clouds[..., :3] = rng.uniform(0.0, 2.0, (LOW_H, LOW_W, 3))
    clouds[..., 3] = rng.uniform(0.0, 1.0, (LOW_H, LOW_W))
    dists = rng.uniform(2.0, 30.0, (LOW_H, LOW_W))
    dists[0:3, 0:4] = -1.0               # low resolution pixels without sky
    u, _, _ = params(camera=cam.eye)
    out, out_glow = run(clouds, dists, u, cam)

    # CPU: upsample over valid neighbours, S + T dst on sky pixels
    worst = 0.0
    untouched = True
    for py in range(FULL_H):
        for px in range(FULL_W):
            pos = np.array([(px + 0.5) / FULL_W * LOW_W, (py + 0.5) / FULL_H * LOW_H]) - 0.5
            base = np.floor(pos).astype(int)
            frac = pos - base
            acc, wsum = np.zeros(4), 0.0
            for oy in (0, 1):
                for ox in (0, 1):
                    cx, cy = min(max(base[0] + ox, 0), LOW_W - 1), min(max(base[1] + oy, 0), LOW_H - 1)
                    if dists[cy, cx] < 0:
                        continue
                    wt = (frac[0] if ox else 1 - frac[0]) * (frac[1] if oy else 1 - frac[1])
                    acc += wt * clouds[cy, cx]
                    wsum += wt
            if depth[py, px] < 1.0 or wsum < 1e-5:
                untouched = untouched and bool(np.array_equal(out[py, px], dst[py, px]) and
                                               np.array_equal(out_glow[py, px], glow_dst[py, px]))
                continue
            c = acc / wsum
            expected = c[:3] + c[3] * dst[py, px, :3]
            worst = max(worst, np.max(np.abs(out[py, px, :3] - expected)))
            worst = max(worst, np.max(np.abs(out_glow[py, px, :3] - c[3] * glow_dst[py, px, :3])))
            untouched = untouched and out[py, px, 3] == dst[py, px, 3]
    ok = check(f'composite S + T dst, glow T, alpha kept (worst error {worst:.1e})', worst < 1e-5) and ok
    ok = check('geometry pixels and pixels with no sky in the low resolution neighbourhood untouched', untouched) and ok

    empty = np.zeros((LOW_H, LOW_W, 4))
    empty[..., 3] = 1.0
    out_e, glow_e = run(empty, np.full((LOW_H, LOW_W), 5.0), u, cam)
    ok = check('an empty cloud (S 0, T 1) leaves the frame bit exact', bool(np.array_equal(out_e, dst) and np.array_equal(glow_e, glow_dst))) and ok

    # sun ray mask: T of the clouds into a single target (blend ZERO, SRC_COLOR)
    mask_tex = tex2d(FULL_W, FULL_H)
    mf = fbo([mask_tex])
    upload(mask_tex, np.full((FULL_H, FULL_W, 4), 2.0), FULL_W, FULL_H)
    upload(rig.hist[0], clouds, LOW_W, LOW_H)
    upload(rig.hist_d[0], dists, LOW_W, LOW_H, RED)
    um, _, _ = params(camera=cam.eye, mode=1.0)
    gl('glUseProgram', None, U)(composite)
    set_vec4s(composite, 'u_Cloud', um)
    bind(7, rig.hist[0])
    bind(8, rig.hist_d[0])
    gl('glEnable', None, U)(0x0BE2)
    gl('glBlendFunc', None, U, U)(0, 0x0300)  # ZERO, SRC_COLOR
    draw(mf, 0, 0, FULL_W, FULL_H)
    gl('glDisable', None, U)(0x0BE2)
    mask = read2d(mask_tex, FULL_W, FULL_H)
    ok = check('sun ray mask multiplies by T (no depth read)', bool(np.all(mask[..., 0] <= 2.0 + 1e-6) and
                                                                  np.all(mask[..., 0] >= 0.0) and mask[..., 0].min() < 1.9)) and ok

    # aerial perspective: opaque black cloud (S 0, T 0) shows S_ap(Dc); T 1, S 0 is still bit exact
    luts = A.build_luts()
    E = np.array(A.SUN_TOA) * math.pi

    def atmosphere():
        a = A.uniforms(draw_pass=1.0)
        a[1][1] = 1.0                      # RB_AtmosphereCloudUniforms: aerial scale 1
        a[1][2] = GROUND_Z
        a[4][:3] = list(cam.eye)
        a[7][1] = cam.eye[2] * 0.03 * 0.001
        A.set_atmosphere(composite, a)
        bind(1, luts['transmittance'])
        bind(2, luts['zero'])
        bind(3, luts['skyview'])

    ua, _, _ = params(camera=cam.eye, atmosphere=1.0, sun=tuple(A.SUN_DIR))
    black = np.zeros((LOW_H, LOW_W, 4))
    dist_ap = np.full((LOW_H, LOW_W), 20.0)
    out_ap, _ = run(black, dist_ap, ua, cam, atmosphere)
    worst = 0.0
    for py in range(0, FULL_H, 3):
        for px in range(0, FULL_W, 3):
            if depth[py, px] < 1.0:
                continue
            ndc = np.array([(px + 0.5) / FULL_W * 2 - 1, (py + 0.5) / FULL_H * 2 - 1])
            near = cam.inv @ np.array([*ndc, -1.0, 1.0])
            far = cam.inv @ np.array([*ndc, 1.0, 1.0])
            d = far[:3] / far[3] - near[:3] / near[3]
            d /= np.linalg.norm(d)
            S, _ = aerial_cpu(cam.eye[2] * 0.03, d[2], 20000.0, float(d @ A.SUN_DIR))
            expected = S * E
            worst = max(worst, np.max(np.abs(out_ap[py, px, :3] - expected) / np.maximum(expected, 1e-6)))
    ok = check(f'aerial perspective in front of the cloud against the CPU model (worst relative error {100 * worst:.2f} %)',
               worst < 0.02) and ok
    out_ae, _ = run(empty, dist_ap, ua, cam, atmosphere)
    ok = check('with aerial perspective an empty cloud is still bit exact', bool(np.array_equal(out_ae, dst))) and ok
    return ok


def aerial_cpu(h0, wz, length, nu):
    """AtmosphereAerial without multiple scattering (metres), aerial scale 1"""
    a = wz
    T = np.ones(3)
    S = np.zeros(3)
    pr, pm = M.phase_rayleigh(nu), M.phase_mie(nu)
    for s in range(3):
        t0, t1 = length * s / 3, length * (s + 1) / 3
        ir = M.exp_integral(h0, a, t0, t1, M.RAYLEIGH_H)
        im = M.exp_integral(h0, a, t0, t1, M.MIE_H)
        tau_r = M.RAYLEIGH_SCATTERING * ir
        tau_ms = M.MIE_SCATTERING * im
        tau = tau_r + M.MIE_EXTINCTION * im
        seg = np.exp(-tau)
        f = 0.5 if tau[1] < 1e-4 else -math.log(0.5 * (1.0 + seg[1])) / tau[1]
        h = max(h0 + a * (t0 + f * (t1 - t0)), 0.0)
        ts = M.sun_transmittance(h, SUN_MU)
        one = np.where(tau > 1e-6, (1.0 - seg) / np.maximum(tau, 1e-30), 1.0 - 0.5 * tau)
        S += T * (tau_r * pr + tau_ms * pm) * ts * one
        T *= seg
    return S, T


SUN_MU = 0.0


def check_shadow_map(rig):
    prog = cloud_program('clouds_shadow', '#define CLOUD_TEST_HOMOGENEOUS\n')
    size = 512
    tex = tex2d(size, size, R32F, RED)
    f = fbo([tex])
    ok = True
    worst = 0.0
    for sun_z in (1.0, 0.6, 0.3):
        sun = (math.sqrt(1 - sun_z * sun_z), 0.0, sun_z)
        u, _, _ = params(extinction=0.4, sun=sun, shadow=(0.0, 0.0, 8.0, 10.0))
        gl('glUseProgram', None, U)(prog)
        set_vec4s(prog, 'u_Cloud', u)
        rig.bind_noise()
        draw(f, 0, 0, size, size)
        T = read2d(tex, size, size, 1)[:, :, 0]
        # a flat homogeneous slab of 2.5 km seen along the sun (the planet curvature over 8 km is below 1e-3)
        expected = math.exp(-0.4 * 2.5 / sun_z)
        worst = max(worst, float(np.max(np.abs(T - expected)) / expected))
    ok = check(f'cloud shadow map of a homogeneous layer: exp(-sigma H / sun z) (worst relative error {worst:.1e})',
               worst < 5e-3) and ok
    gl('glDeleteProgram', None, U)(prog)
    return ok


def check_legacy():
    body = '''
out vec4 out_Color;
out vec4 out_Glow;
void main()
{
	vec2 xy = vec2((gl_FragCoord.x - 0.5) * 0.1, 0.0);
	out_Color = vec4(CloudLegacyMask(xy), 0.0, 0.0, 1.0);
	out_Glow = vec4(0.0);
}
'''
    prog = cloud_program('clouds_march', body=body)
    n = 64
    tex, tex_b = tex2d(n, 1), tex2d(n, 1)
    f = fbo([tex, tex_b])
    ok = True

    def run(mode, legacy, camera_km=(0.0, 0.0)):
        u, _, _ = params(legacy_mode=mode, legacy=legacy, camera=(camera_km[0] / KM_PER_UNIT, camera_km[1] / KM_PER_UNIT, 300.0))
        gl('glUseProgram', None, U)(prog)
        set_vec4s(prog, 'u_Cloud', u)
        draw(f, 0, 0, n, 1)
        return read2d(tex, n, 1)[0, :, 0]

    x = np.arange(n) * 0.1
    off = run((0, 0, 0, 1), (0, 0, 3, 1))
    disc = run((2, 0, 0, 1), (0.0, 0.0, 3.0, 1.0))
    ring = run((2, 1, 0, 1), (0.0, 0.0, 3.0, 1.0))
    hint_tube = run((1, 1, 0, 1), (0.0, 0.0, 3.0, 1.0))

    def ss(e0, e1, v):
        t = np.clip((v - e0) / (e1 - e0), 0, 1)
        return t * t * (3 - 2 * t)
    ref_disc = 1.0 - ss(1.8, 3.0, x)
    ref_ring = ref_disc * ss(1.0, 1.0 + 0.3, x)
    ref_hint = ss(1.0, 6.0, x)
    err = max(np.max(np.abs(disc - ref_disc)), np.max(np.abs(ring - ref_ring)), np.max(np.abs(hint_tube - ref_hint)))
    ok = check(f'legacy masks: off = 1, literal disc / ring, tube hint (worst error {err:.1e}; ring at 0 / 1.15 / 2 / 3.5 km: '
               f'{ring[0]:.2f} {ring[11]:.2f} {ring[20]:.2f} {ring[35]:.2f})', bool(np.all(off == 1.0)) and err < 1e-5) and ok
    gl('glDeleteProgram', None, U)(prog)
    return ok


# --- lightall / froxel injection with the cloud shadow lookup ------------------------------------------------------

def check_lit_programs():
    constants = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
    sizes = ''.join(f'#define {key} {value}\n' for key, value in re.findall(
        r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', constants, re.M))
    header = ('#define M_PI 3.14159265358979323846\n'
              '#define DEFORM_NONE 0\n#define DEFORM_WAVE 1\n#define DEFORM_NORMALS 2\n#define DEFORM_BULGE 3\n'
              '#define DEFORM_BULGE_UNIFORM 4\n#define DEFORM_MOVE 5\n#define DEFORM_PROJECTION_SHADOW 6\n'
              '#define DEFORM_DISINTEGRATION 7\n#define WF_NONE 0\n#define WF_SIN 1\n#define WF_SQUARE 2\n'
              '#define WF_TRIANGLE 3\n#define WF_SAWTOOTH 4\n#define WF_INVERSE_SAWTOOTH 5\n'
              '#define TCGEN_LIGHTMAP 0\n#define TCGEN_LIGHTMAP1 1\n#define TCGEN_LIGHTMAP2 2\n#define TCGEN_LIGHTMAP3 3\n'
              '#define TCGEN_TEXTURE 4\n#define TCGEN_ENVIRONMENT_MAPPED 5\n#define TCGEN_ENVIRONMENT_MAPPED_SP 6\n'
              '#define TCGEN_ENVIRONMENT_MAPPED_SP_FP 7\n#define TCGEN_FOG 8\n#define TCGEN_VECTOR 9\n'
              '#define CGEN_LIGHTING_DIFFUSE 0\n#define CGEN_DISINTEGRATION_1 1\n#define CGEN_DISINTEGRATION_2 2\n'
              '#define AGEN_LIGHTING_SPECULAR 0\n#define AGEN_LIGHTING_SPECULAR_STATIC 1\n#define AGEN_PORTAL 2\n'
              '#define ALPHA_TEST_GT0 1\n#define ALPHA_TEST_LT128 2\n#define ALPHA_TEST_GE128 3\n#define ALPHA_TEST_GE192 4\n'
              '#define ALPHA_TEST_E255 5\n#define MAX_G2_BONES 72\n#define MAX_DLIGHTS 32\n'
              '#define DSHADOW_MAP_SIZE 512\n#define r_FBufScale vec2(1920.0, 1080.0)\n#define USE_ALPHA_TEST\n'
              '#define USE_FROXEL_FOG\n#define USE_CLOUD_SHADOWS\n#define CUBEMAP_RESOLUTION float(256)\n#define ROUGHNESS_MIPS float(6)\n' + sizes)
    vertex_lib = ''.join(vertex(n) + '\n' for n in ['leaf_flutter', 'foliage_interact', 'plant_bend'])
    vs, fs = read('lightall').split('/*[Fragment]*/', 1)
    vs = vs.split('/*[Vertex]*/', 1)[1]
    sun = '#define USE_SHADOWMAP\n#define r_shadowMapSize 1024\n#define r_shadowCascadeZFar 4096.0\n'
    bases = {
        'lightmap': '#define USE_LIGHT\n#define USE_LIGHTMAP\n#define USE_SPECULARMAP\n#define USE_NORMALMAP\n#define USE_CUBEMAP\n',
        'grid': '#define USE_LIGHT\n#define USE_LIGHT_VECTOR\n#define USE_SPECULARMAP\n#define USE_NORMALMAP\n'
                '#define USE_SKELETAL_ANIMATION\n#define ENTITY_GRID_LDR_RANGE 4.0\n#define USE_ENTITY_GRID\n',
        'pom': '#define USE_LIGHT\n#define USE_LIGHTMAP\n#define USE_SPECULARMAP\n#define USE_NORMALMAP\n#define USE_PARALLAXMAP\n',
    }
    modes = ['#define SHADOWMAP_MODULATE\n', '#define USE_PRIMARY_LIGHT\n', '#define USE_PRIMARY_LIGHT\n#define USE_SHADOWS2\n',
             '#define SHADOWMAP_MODULATE\n#define USE_SHADOWS2\n#define USE_SSAO\n']
    count = 0
    for base in bases.values():
        for mode in modes:
            defines = header + base + sun + mode
            prog = compile_program([(GL_VERTEX, '#version 150 core\n' + defines + vertex_lib + vs),
                                    (GL_FRAGMENT, '#version 150 core\n' + defines + fs)], 'lightall ' + mode,
                                   ('out_Color', 'out_Glow', 'out_SSRNormal', 'out_SSRSpecular'))
            gl('glDeleteProgram', None, U)(prog)
            count += 1

    vol = fragment('volumetric_common')
    inject_fs = fragment('volumetric_inject')
    inject_vs = read('volumetric_inject').split('/*[Vertex]*/')[1].split('/*[')[0]
    inject_gs = read('volumetric_inject').split('/*[Geometry]*/')[1].split('/*[')[0]
    fog = header + '#define USE_FROXEL_NOISE\n#define USE_FROXEL_PARTICLES\n'
    for extra in ['', '#define USE_SHADOWS2\n', '#define USE_FROXEL_RGB\n', '#define USE_SHADOWS2\n#define USE_FROXEL_STATIC_RECONSTRUCTION\n']:
        d = fog + extra
        prog = compile_program([(GL_VERTEX, '#version 150 core\n' + d + inject_vs),
                                (GL_GEOMETRY, '#version 150 core\n' + d + inject_gs),
                                (GL_FRAGMENT, '#version 150 core\n' + d + vol + inject_fs)], 'inject raster ' + extra,
                               ('out_Color', 'out_Glow', 'out_SSRNormal', 'out_SSRSpecular'))
        gl('glDeleteProgram', None, U)(prog)
        for media in ('', '#define USE_FROXEL_MEDIA_PASS\n'):
            prog = compile_program([(GL_COMPUTE, '#version 430 core\n' + d + '#define USE_FROXEL_COMPUTE\n' + media + vol + inject_fs)],
                                   'inject compute ' + extra + media)
            gl('glDeleteProgram', None, U)(prog)
            count += 1
        count += 1
    return check(f'{count} lightall / volumetric_inject (raster + compute) programs with USE_CLOUD_SHADOWS compiled and linked', True)


def main():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'Clouds test', 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    context = SDL.SDL_GL_CreateContext(window)
    assert context, SDL.SDL_GetError()
    ok = True
    try:
        print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
        vao = U()
        gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
        gl('glBindVertexArray', None, U)(vao)
        gl('glDisable', None, U)(0x0BE2)

        programs = {name: cloud_program(name) for name in
                    ['clouds_noise', 'clouds_march', 'clouds_resolve', 'clouds_composite', 'clouds_shadow']}
        ok = check('5 cloud programs compiled and linked (noise, march, resolve, composite, shadow map)', True) and ok
        ok = check_lit_programs() and ok

        noise_ok, noise = check_noise(programs['clouds_noise'])
        ok = noise_ok and ok
        rig = Rig()
        ok = check_interval(rig, programs['clouds_march']) and ok
        ok = check_homogeneous(rig) and ok
        ok = check_phase() and ok
        ok = check_sweep(rig, programs['clouds_march'], (noise[0], noise[1], noise[2])) and ok
        ok = check_temporal(rig, programs['clouds_resolve'], programs['clouds_march'], (noise[0], noise[1], noise[2])) and ok

        import test_atmosphere_gl as A  # noqa: E402  (same SDL context)
        A.build_luts = lambda: build_luts(A)
        global SUN_MU
        SUN_MU = float(A.SUN_DIR[2])
        ok = check_composite(rig, programs['clouds_composite'], A) and ok
        ok = check_shadow_map(rig) and ok
        ok = check_legacy() and ok
        err = gl('glGetError', U)()
        ok = check('no GL error', err == 0, hex(err)) and ok
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


def build_luts(A):
    """the atmosphere transmittance and sky-view LUTs of test_atmosphere_gl.py (multiple scattering bound to zero)"""
    sizes = [(256, 64), (32, 32), (192, 108)]
    textures = {i: A.texture2d(w, h) for i, (w, h) in enumerate(sizes)}
    zero = A.texture2d(32, 32, values=[0.0] * (32 * 32 * 4))
    fbos = [A.framebuffer([textures[i]]) for i in range(3)]
    for i, name in enumerate(['atmosphere_transmittance', 'atmosphere_skyview']):
        index = 0 if i == 0 else 2
        prog = A.program(name)
        gl('glUseProgram', None, U)(prog)
        A.set_atmosphere(prog, A.uniforms())
        bind(1, textures[0])
        bind(2, zero)
        A.draw(fbos[index], *sizes[index])
        gl('glDeleteProgram', None, U)(prog)
    return {'transmittance': textures[0], 'skyview': textures[2], 'zero': zero}


# --- GPU timings (python tools/rend2/test_clouds_gl.py --bench) ------------------------------------------------------

def bench():
    """GL_TIME_ELAPSED of the passes at 1920x1080 (march at r_cloudScale 2 and 4), default cvars, real noise"""
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'Clouds bench', 0, 0, 32, 32, 10)
    context = SDL.SDL_GL_CreateContext(window)
    try:
        print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
        vao = U()
        gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
        gl('glBindVertexArray', None, U)(vao)
        programs = {name: cloud_program(name) for name in
                    ['clouds_noise', 'clouds_march', 'clouds_resolve', 'clouds_composite', 'clouds_shadow']}
        query = U()
        gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(query))

        def timed(fn, repeat=10):
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

        start = gl('glGetString', C.c_char_p, U)(0x1F00)
        import time
        t0 = time.perf_counter()
        gen_ms = timed(lambda: generate_noise(programs['clouds_noise']), repeat=1)
        cpu_gen = time.perf_counter() - t0
        raw = generate_noise(programs['clouds_noise'])
        t1 = time.perf_counter()
        textures = {}
        for target, size, depth, channels, internal, fmt in [(0, 128, 128, 1, R8, RED), (1, 32, 32, 1, R8, RED),
                                                              (2, 512, 1, 2, RG8, RG)]:
            data = np.zeros(raw[target].shape, dtype=np.uint8)
            data[..., 0] = equalize(raw[target][..., 0])
            if channels > 1:
                data[..., 1] = np.clip((raw[target][..., 1] * 255.0 + 0.5).astype(np.int64), 0, 255)
            textures[target] = tex3d(size, size, depth, internal, fmt, data, mips=True)
        print(f'  noise generation: GPU {gen_ms:.1f} ms (+ read back / equalize / upload, numpy {1000 * (time.perf_counter() - t1):.0f} ms)')

        full_w, full_h = 1920, 1080
        depth = tex2d(full_w, full_h, R32F, RED, FLOAT, np.ones((full_h, full_w), dtype=np.float32))
        # the engine's formats: RGBA16F scene / glow / march / history, R16F distances and shadow map
        color = tex2d(full_w, full_h, RGBA16F)
        glow = tex2d(full_w, full_h, RGBA16F)
        composite_fbo = fbo([color, glow])
        shadow_tex = tex2d(512, 512, R16F, RED)
        shadow_fbo = fbo([shadow_tex])
        for scale in (2, 4):
            w, h = full_w // scale, full_h // scale
            march_tex, march_d = tex2d(w, h, RGBA16F), tex2d(w, h, R16F, RED)
            march_fbo = fbo([march_tex, march_d])
            hist = [tex2d(w, h, RGBA16F) for _ in range(2)]
            hist_d = [tex2d(w, h, R16F, RED) for _ in range(2)]
            hist_fbo = [fbo([hist[i], hist_d[i]]) for i in range(2)]
            for label, forward, coverage in [('towards the horizon, coverage 0.5', (1.0, 0.2, 0.12), 0.5),
                                             ('towards the horizon, coverage 0.9', (1.0, 0.2, 0.12), 0.9),
                                             ('looking up, coverage 0.5', (0.3, 0.1, 1.0), 0.5)]:
                cam = Camera((0.0, 0.0, 300.0), forward, 90.0)
                cam.inv = np.linalg.inv(perspective(73.7, full_w / full_h, 4.0, 65536.0) @ view((0, 0, 0), forward))
                cam.vp = perspective(73.7, full_w / full_h, 4.0, 65536.0) @ view(cam.eye, forward)
                u, _, _ = params(camera=cam.eye, coverage=coverage, ambient_scale=1.0, ambient_sky=(0.2, 0.25, 0.35),
                                 scale=float(scale))
                u[15] = [0.0, 0.0, w, h]

                def march():
                    gl('glUseProgram', None, U)(programs['clouds_march'])
                    set_vec4s(programs['clouds_march'], 'u_Cloud', u)
                    set_matrix(programs['clouds_march'], 'u_CloudInvViewProjection', cam.inv)
                    bind(0, depth)
                    bind(4, textures[0], TEX3D)
                    bind(5, textures[1], TEX3D)
                    bind(6, textures[2], TEX3D)
                    gl('glBindFramebuffer', None, U, U)(FB, march_fbo)
                    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
                    gl('glDrawArrays', None, U, I, I)(4, 0, 3)

                def resolve():
                    ur = u.copy()
                    ur[16][1] = 0.9
                    gl('glUseProgram', None, U)(programs['clouds_resolve'])
                    set_vec4s(programs['clouds_resolve'], 'u_Cloud', ur)
                    set_matrix(programs['clouds_resolve'], 'u_CloudInvViewProjection', cam.inv)
                    set_matrix(programs['clouds_resolve'], 'u_CloudPrevViewProjection', cam.vp)
                    bind(7, march_tex)
                    bind(8, march_d)
                    bind(9, hist[0])
                    bind(10, hist_d[0])
                    gl('glBindFramebuffer', None, U, U)(FB, hist_fbo[1])
                    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
                    gl('glDrawArrays', None, U, I, I)(4, 0, 3)

                def composite():
                    gl('glUseProgram', None, U)(programs['clouds_composite'])
                    set_vec4s(programs['clouds_composite'], 'u_Cloud', u)
                    set_matrix(programs['clouds_composite'], 'u_CloudInvViewProjection', cam.inv)
                    bind(0, depth)
                    bind(7, hist[1])
                    bind(8, hist_d[1])
                    gl('glEnable', None, U)(0x0BE2)
                    gl('glBlendFunc', None, U, U)(1, 0x0302)
                    gl('glBindFramebuffer', None, U, U)(FB, composite_fbo)
                    gl('glViewport', None, I, I, I, I)(0, 0, full_w, full_h)
                    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
                    gl('glDisable', None, U)(0x0BE2)

                march()
                resolve()       # history 0 -> 1 once, so the timed resolve reads real data
                m, r, c = timed(march), timed(resolve), timed(composite)
                print(f'  {w}x{h} (scale {scale}), {label}: march {m:.2f} ms, resolve {r:.2f} ms, composite {c:.2f} ms')
            for tex in [march_tex, march_d] + hist + hist_d:
                gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(tex)))

        us, _, _ = params(shadow=(0.0, 0.0, 8.0, 10.0))

        def shadow():
            gl('glUseProgram', None, U)(programs['clouds_shadow'])
            set_vec4s(programs['clouds_shadow'], 'u_Cloud', us)
            bind(4, textures[0], TEX3D)
            bind(5, textures[1], TEX3D)
            bind(6, textures[2], TEX3D)
            gl('glBindFramebuffer', None, U, U)(FB, shadow_fbo)
            gl('glViewport', None, I, I, I, I)(0, 0, 512, 512)
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        print(f'  cloud shadow map 512x512, 10 steps: {timed(shadow):.2f} ms')
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    sys.exit(bench() if '--bench' in sys.argv else main())
