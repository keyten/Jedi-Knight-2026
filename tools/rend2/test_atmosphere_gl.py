"""GPU checks of the r_atmosphere shaders on a hidden SDL GL 3.2 core context (Windows, bundled SDL2).

Run from any directory: python tools/rend2/test_atmosphere_gl.py

- compiles and links every atmosphere program (LUT passes, composite with and without the froxel
  functions, scalar and RGB extinction) with the real sources, as GLSL_LoadGPUProgramAtmosphere splices them
- transmittance LUT against the CPU model (atmosphere_check.py)
- multiple scattering LUT: finite, positive, bounded
- sky-view LUT (multiple scattering bound to zero) against a CPU single scattering ray march
- composite: both blend draws on a synthetic depth buffer and scene colour against the CPU analytic aerial
  perspective (geometry, view model, overlay sky, analytic sky)
"""
import ctypes as C
import math
from pathlib import Path
import re
import struct
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import atmosphere_check as M  # noqa: E402

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

_gl_cache = {}


def gl(name, result, *args):
    key = (name, result, args)
    if key not in _gl_cache:
        address = SDL.SDL_GL_GetProcAddress(name.encode())
        assert address, name
        _gl_cache[key] = C.WINFUNCTYPE(result, *args)(address)
    return _gl_cache[key]


GLSL = ROOT / 'shared/rd-rend2/glsl'
COMPOSITE_W, COMPOSITE_H = 8, 4


def block(name, kind):
    text = (GLSL / f'{name}.glsl').read_text()
    if kind == 'vertex':
        return text.split('/*[Vertex]*/')[1].split('/*[')[0]
    return text.split('/*[Fragment]*/')[1]


def program(name, froxel=False, rgb=False):
    defines = f'#define r_FBufScale vec2({COMPOSITE_W}.0, {COMPOSITE_H}.0)\n#define M_PI 3.14159265358979323846\n'
    library = ''
    if froxel:
        constants = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
        defines += ''.join(f'#define {key} {value}\n' for key, value in re.findall(
            r'^#define\s+(MAX_GPU_\w+|FROXEL_\w+|VOL_PARTICLE_POOL)\s+(\d+)\b', constants, re.M))
        defines += '#define MAX_DLIGHTS 32\n#define USE_FROXEL_FOG\n'
        defines += '#define USE_FROXEL_RGB\n' if rgb else ''
        library += block('volumetric_common', 'fragment') + '\n'
    library += block('atmosphere_common', 'fragment') + '\n'
    sources = [(0x8B31, '#version 150 core\n' + defines + block(name, 'vertex')),
               (0x8B30, '#version 150 core\n' + defines + library + block(name, 'fragment'))]
    prog = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(text.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(16384)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, (name, froxel, rgb, log.value.decode())
        gl('glAttachShader', None, U, U)(prog, shader)
        gl('glDeleteShader', None, U)(shader)
    for index, output in enumerate(['out_Color', 'out_Glow']):
        gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, index, output.encode())
    gl('glLinkProgram', None, U)(prog)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(prog, len(log), None, log)
    assert ok.value, (name, log.value.decode())
    gl('glUseProgram', None, U)(prog)
    for sampler, unit in [('u_ScreenDepthMap', 0), ('u_AtmosphereTransmittanceMap', 1),
                          ('u_AtmosphereMultiScatterMap', 2), ('u_AtmosphereSkyViewMap', 3)]:
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, sampler.encode())
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    return prog


def texture2d(width, height, internal=0x881A, fmt=0x1908, kind=0x1406, values=None):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    raw = (F * len(values))(*values) if values is not None else None
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(0x0DE1, 0, internal, width, height, 0, fmt, kind, raw)
    for parameter, value in [(0x2800, 0x2601), (0x2801, 0x2601), (0x2802, 0x812F), (0x2803, 0x812F)]:
        gl('glTexParameteri', None, U, U, I)(0x0DE1, parameter, value)
    return tex.value


def framebuffer(textures):
    fbo = U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glBindFramebuffer', None, U, U)(0x8D40, fbo)
    for i, tex in enumerate(textures):
        gl('glFramebufferTexture2D', None, U, U, U, U, I)(0x8D40, 0x8CE0 + i, 0x0DE1, tex, 0)
    bufs = (U * len(textures))(*[0x8CE0 + i for i in range(len(textures))])
    gl('glDrawBuffers', None, I, P)(len(textures), bufs)
    assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
    return fbo.value


def read2d(tex, width, height):
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    data = (F * (width * height * 4))()
    gl('glGetTexImage', None, U, I, U, U, P)(0x0DE1, 0, 0x1908, 0x1406, data)
    return np.array(data, dtype=np.float64).reshape(height, width, 4)


def bind(unit, tex):
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    gl('glBindTexture', None, U, U)(0x0DE1, tex)
    gl('glActiveTexture', None, U)(0x84C0)


def set_atmosphere(prog, u, inv_view_projection=None):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_Atmosphere')
    assert loc >= 0
    flat = (F * 32)(*[float(x) for row in u for x in row])
    gl('glUniform4fv', None, I, I, P)(loc, 8, flat)
    if inv_view_projection is not None:
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_AtmosphereInvViewProjection')
        assert loc >= 0
        # column major, as the engine's matrix_t
        m = (F * 16)(*[float(x) for x in np.asarray(inv_view_projection).T.reshape(-1)])
        gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0, m)


def draw(fbo, width, height):
    gl('glBindFramebuffer', None, U, U)(0x8D40, fbo)
    gl('glViewport', None, I, I, I, I)(0, 0, width, height)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    gl('glFinish', None)()


# --- scene parameters ---------------------------------------------------------------------------------------

SUN_ELEVATION = 30.0
SUN_AZIMUTH = 20.0
SUN_DIR = np.array([math.cos(math.radians(SUN_AZIMUTH)) * math.cos(math.radians(SUN_ELEVATION)),
                    math.sin(math.radians(SUN_AZIMUTH)) * math.cos(math.radians(SUN_ELEVATION)),
                    math.sin(math.radians(SUN_ELEVATION))])
UNIT_SCALE = 0.03
AERIAL_SCALE = 20.0
GROUND_Z = -64.0
CAMERA = np.array([0.0, 0.0, 100.0])
CAMERA_ALT_KM = (CAMERA[2] - GROUND_Z) * UNIT_SCALE * 0.001
SKY_DISTANCE = 12000.0
SUN_TOA = np.array([1.2, 1.1, 0.9])
COS_SUN_RADIUS = math.cos(math.radians(0.53 * 0.5))


def uniforms(sky_mode=0, sun_glow=1.0, draw_pass=0.0, disc=0.0, debug=0):
    return [
        [1.0, 1.0, 1.0, 0.8],
        [UNIT_SCALE, AERIAL_SCALE, GROUND_Z, 0.0],
        [*SUN_DIR, COS_SUN_RADIUS],
        [*SUN_TOA, disc],
        [*CAMERA, SKY_DISTANCE],
        [float(sky_mode), 0.5, sun_glow, float(debug)],
        [0.0, 0.0, 1.0, 1.0],
        [0.0, CAMERA_ALT_KM, draw_pass, 0.0],
    ]


def texel_unit(i, n):
    """texel centre -> unit range, inverse of AtmoUnitToTexel"""
    u = (i + 0.5) / n
    return (u - 0.5 / n) / (1.0 - 1.0 / n)


def check_transmittance(lut):
    worst = 0.0
    for j in range(0, 64, 7):
        for i in range(0, 256, 17):
            r, mu = M.transmittance_rmu(texel_unit(i, 256), texel_unit(j, 64))
            h = r - M.R_GROUND
            # the last column is the horizon ray itself (tangent to the ground): the LUT stores the grazing
            # ray, the CPU ray test calls it a ground hit; lookups apply the horizon cut themselves
            if h < 0.0 or i == 255:
                continue
            ref = M.sun_transmittance_v(np.array([h]), mu, steps=400)[0]
            e = float(np.max(np.abs(lut[j, i, :3] - ref)))
            if e > worst:
                worst, where = e, (i, j, h, mu, lut[j, i, :3], ref)
    print('    worst at texel %d,%d h %.0f m mu %.4f: gpu %s cpu %s' % where)
    print(f'  transmittance LUT vs CPU: worst abs error {worst:.2e}')
    return worst < 6e-3


def skyview_reference(h_km, cos_zenith, cos_light, steps=2000):
    """Single scattering sky radiance per unit illuminance (no multiple scattering), CPU."""
    h = h_km * 1000.0
    mu_s = SUN_DIR[2]
    sun = np.array([math.sqrt(max(1.0 - mu_s * mu_s, 0.0)), 0.0, mu_s])
    sz = math.sqrt(max(1.0 - cos_zenith ** 2, 0.0))
    sl = math.sqrt(max(1.0 - cos_light ** 2, 0.0))
    d = np.array([sz * cos_light, sz * sl, cos_zenith])
    r = M.R_GROUND + max(h, 10.0)
    t_ground = M.ray_sphere(r, cos_zenith, M.R_GROUND) if cos_zenith < 0.0 else -1.0
    length = t_ground if t_ground > 0.0 else M.ray_sphere(r, cos_zenith, M.R_TOP)
    # quadratic distribution, as the shader
    x = (np.arange(steps + 1) / steps) ** 2 * length
    t = 0.5 * (x[1:] + x[:-1])
    dt = x[1:] - x[:-1]
    p = np.array([0.0, 0.0, r])[None, :] + t[:, None] * d[None, :]
    rs = np.linalg.norm(p, axis=1)
    hs = rs - M.R_GROUND
    mus = (p @ sun) / rs
    ts = np.array([M.transmittance_to_top(rr, mm, steps=60) for rr, mm in zip(rs, mus)])
    nu = float(d @ sun)
    rho_r = np.exp(-hs / M.RAYLEIGH_H)[:, None]
    rho_m = np.exp(-hs / M.MIE_H)[:, None]
    rho_o = np.maximum(0.0, 1.0 - np.abs(hs - 25e3) / 15e3)[:, None]
    sr = M.RAYLEIGH_SCATTERING[None, :] * rho_r
    sm = M.MIE_SCATTERING * rho_m
    st = sr + M.MIE_EXTINCTION * rho_m + M.OZONE_ABSORPTION[None, :] * rho_o
    j = (sr * M.phase_rayleigh(nu) + sm * M.phase_mie(nu)) * ts
    seg = np.exp(-st * dt[:, None])
    T = np.vstack([np.ones((1, 3)), np.cumprod(seg, axis=0)[:-1]])
    # per metre: the CPU constants are per metre, the GPU per km; radiance per unit illuminance matches
    return (T * j / st * (1.0 - seg)).sum(axis=0)


def skyview_dir(i, j, h_km):
    u = texel_unit(i, 192)
    v = texel_unit(j, 108)
    return M.skyview_dir(u, v, M.R_GROUND + max(h_km, 0.01) * 1000.0)


def check_skyview(lut):
    worst = 0.0
    near_horizon = 0.0
    rows = []
    for j in (5, 30, 50, 53, 54, 60, 80, 100):
        for i in (0, 40, 120, 191):
            cz, cl = skyview_dir(i, j, CAMERA_ALT_KM)
            ref = skyview_reference(CAMERA_ALT_KM, cz, cl, steps=800)
            gpu = lut[j, i, :3]
            # relative, with a floor: views straight down from 10 m see ~1e-5 (11 m of air)
            err = float(np.max(np.abs(gpu - ref)) / max(np.max(ref), 1e-3))
            r = M.R_GROUND + max(CAMERA_ALT_KM, 0.01) * 1000.0
            horizon = math.acos(-math.sqrt(r * r - M.R_GROUND ** 2) / r)
            # within 0.2 degrees of the horizon the path length changes by ~1e4 km per unit of cos zenith:
            # a GPU acos(cos(zenith)) round trip was off by 7 % there before the parameterization used the
            # angle from the horizon (atmosphere_common.glsl, AtmoSkyViewUV)
            if abs(math.acos(max(-1.0, min(1.0, cz))) - horizon) < math.radians(0.2):
                near_horizon = max(near_horizon, err)
                continue
            worst = max(worst, err)
            rows.append((i, j, cz, cl, gpu, ref, err))
    rows.sort(key=lambda r: -r[6])
    for i, j, cz, cl, gpu, ref, err in rows[:4]:
        print(f'    texel {i:3d},{j:3d} cos zenith {cz:+.3f} cos azimuth {cl:+.3f}: gpu {gpu[0]:.4f} {gpu[1]:.4f} {gpu[2]:.4f}'
              f'  cpu {ref[0]:.4f} {ref[1]:.4f} {ref[2]:.4f}  ({100 * err:.2f} %)')
    print(f'  sky-view LUT (single scattering) vs CPU: worst relative error {100 * worst:.2f} % '
          f'(within 0.2 deg of the horizon: {100 * near_horizon:.2f} %)')
    return worst < 0.05 and near_horizon < 0.02


def perspective(fov_x, fov_y, near, far):
    x = 1.0 / math.tan(math.radians(fov_x) * 0.5)
    y = 1.0 / math.tan(math.radians(fov_y) * 0.5)
    return np.array([[x, 0, 0, 0], [0, y, 0, 0],
                     [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)], [0, 0, -1, 0]])


def view_matrix():
    # world z up, camera looking along +x (yaw 30): GL eye looks down -z, y up
    yaw = math.radians(30.0)
    forward = np.array([math.cos(yaw), math.sin(yaw), 0.05])
    forward /= np.linalg.norm(forward)
    left = np.cross([0, 0, 1], forward)
    left /= np.linalg.norm(left)
    up = np.cross(forward, left)
    R = np.array([-left, up, -forward])
    m = np.eye(4)
    m[:3, :3] = R
    m[:3, 3] = -R @ CAMERA
    return m


def composite_reference(depth, scene, inv_vp, sky_mode, sun_glow, skyview_lut, sun_lut_fn):
    """CPU version of atmosphere_composite.glsl (multiple scattering = 0) for one pixel."""
    out = []
    for (px, py), d in depth.items():
        tc = np.array([(px + 0.5) / COMPOSITE_W, (py + 0.5) / COMPOSITE_H])
        ndc = tc * 2.0 - 1.0
        sky = d >= 1.0
        if sky:
            p = inv_vp @ np.array([ndc[0], ndc[1], 1.0, 1.0])
            direction = p[:3] / p[3] - CAMERA
            direction /= np.linalg.norm(direction)
            dist = SKY_DISTANCE
        else:
            dd = d / 0.3 if d <= 0.3001 else d
            p = inv_vp @ np.array([ndc[0], ndc[1], dd * 2.0 - 1.0, 1.0])
            to = p[:3] / p[3] - CAMERA
            dist = np.linalg.norm(to)
            direction = to / dist
            if d <= 0.3001:
                dist = 0.0
        h0 = (CAMERA[2] - GROUND_Z) * UNIT_SCALE  # metres
        length = dist * UNIT_SCALE * AERIAL_SCALE  # metres
        nu = float(direction @ SUN_DIR)
        glow = sun_glow if sky else 1.0
        # aerial_analytic with this Mie glow and no multiple scattering
        a = direction[2] / AERIAL_SCALE
        T = np.ones(3)
        sR = np.zeros(3)
        sM = np.zeros(3)
        pr, pm = M.phase_rayleigh(nu), M.phase_mie(nu) * glow
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
            ts = sun_lut_fn(h, SUN_DIR[2])
            one = np.where(tau > 1e-6, (1.0 - seg) / np.maximum(tau, 1e-30), 1.0 - 0.5 * tau)
            sR += T * tau_r * pr * ts * one
            sM += T * tau_ms * pm * ts * one
            T *= seg
        E = SUN_TOA * math.pi
        keep, add = T, (sR + sM) * E
        if sky and sky_mode == 2:
            keep, add = np.zeros(3), skyview_lut(direction) * E
        out.append(((px, py), scene * keep + add))
    return out


def check_composite(prog, lut_textures, luts, sky_mode, sun_glow):
    depth_values = {(0, 0): 0.9990, (1, 0): 0.99995, (2, 0): 0.999995, (3, 0): 0.9999995, (4, 0): 0.25,
                    (5, 0): 1.0, (0, 3): 1.0, (7, 3): 1.0, (6, 2): 0.9999990, (2, 1): 0.99999}
    depth = np.ones((COMPOSITE_H, COMPOSITE_W), dtype=np.float32)
    for (px, py), d in depth_values.items():
        depth[py, px] = d
    scene = np.array([0.5, 0.4, 0.3])

    depth_tex = texture2d(COMPOSITE_W, COMPOSITE_H, 0x81A6, 0x1902, 0x1406, list(depth.reshape(-1)))
    color = texture2d(COMPOSITE_W, COMPOSITE_H, 0x881A, values=[*scene, 1.0] * (COMPOSITE_W * COMPOSITE_H))
    glow = texture2d(COMPOSITE_W, COMPOSITE_H, 0x881A, values=[0.0, 0.0, 0.0, 1.0] * (COMPOSITE_W * COMPOSITE_H))
    fbo = framebuffer([color, glow])

    vp = perspective(90.0, 60.0, 4.0, 20000.0) @ view_matrix()
    inv_vp = np.linalg.inv(vp)

    gl('glUseProgram', None, U)(prog)
    bind(0, depth_tex)
    bind(1, lut_textures[0])
    bind(2, lut_textures['zero'])
    bind(3, lut_textures[2])
    gl('glEnable', None, U)(0x0BE2)
    gl('glColorMask', None, U, U, U, U)(1, 1, 1, 0)
    for pass_index, (src, dst) in enumerate([(0, 0x0300), (1, 1)]):  # ZERO,SRC_COLOR then ONE,ONE
        gl('glBlendFunc', None, U, U)(src, dst)
        set_atmosphere(prog, uniforms(sky_mode, sun_glow, float(pass_index)), inv_vp)
        draw(fbo, COMPOSITE_W, COMPOSITE_H)
    gl('glDisable', None, U)(0x0BE2)
    gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
    result = read2d(color, COMPOSITE_W, COMPOSITE_H)

    trans_lut = luts[0]

    def sun_lut(h_m, mu):
        # bilinear lookup of the GPU transmittance LUT, as the shader (h in metres)
        u, v = M.transmittance_uv(M.R_GROUND + h_m, mu)
        x = (0.5 / 256 + u * (1 - 1 / 256)) * 256 - 0.5
        y = (0.5 / 64 + v * (1 - 1 / 64)) * 64 - 0.5
        x0, y0 = int(math.floor(x)), int(math.floor(y))
        fx, fy = x - x0, y - y0
        x0c, x1c = min(max(x0, 0), 255), min(max(x0 + 1, 0), 255)
        y0c, y1c = min(max(y0, 0), 63), min(max(y0 + 1, 0), 63)
        a = trans_lut[y0c, x0c, :3] * (1 - fx) + trans_lut[y0c, x1c, :3] * fx
        b = trans_lut[y1c, x0c, :3] * (1 - fx) + trans_lut[y1c, x1c, :3] * fx
        horizon = -math.sqrt(h_m / 1000.0 * (2 * 6360.0 + h_m / 1000.0)) / (6360.0 + h_m / 1000.0)
        soft = max(1.0 - COS_SUN_RADIUS, 1e-5) * 2.0
        tt = min(max((mu - (horizon - soft)) / (2 * soft), 0.0), 1.0)
        return (a * (1 - fy) + b * fy) * tt * tt * (3 - 2 * tt)

    sky_lut = luts[2]

    def skyview(direction):
        cl = 1.0
        a, b = direction[:2], SUN_DIR[:2]
        if np.dot(a, a) > 1e-8:
            cl = float(np.dot(a, b) / math.sqrt(np.dot(a, a) * np.dot(b, b)))
        u, v = M.skyview_uv(direction[2], cl, M.R_GROUND + max(CAMERA_ALT_KM, 0.01) * 1000.0)
        x = (0.5 / 192 + u * (1 - 1 / 192)) * 192 - 0.5
        y = (0.5 / 108 + v * (1 - 1 / 108)) * 108 - 0.5
        x0, y0 = int(math.floor(x)), int(math.floor(y))
        fx, fy = x - x0, y - y0
        x0c, x1c = min(max(x0, 0), 191), min(max(x0 + 1, 0), 191)
        y0c, y1c = min(max(y0, 0), 107), min(max(y0 + 1, 0), 107)
        a = sky_lut[y0c, x0c, :3] * (1 - fx) + sky_lut[y0c, x1c, :3] * fx
        b = sky_lut[y1c, x0c, :3] * (1 - fx) + sky_lut[y1c, x1c, :3] * fx
        return a * (1 - fy) + b * fy

    worst = 0.0
    for (px, py), ref in composite_reference(depth_values, scene, inv_vp, sky_mode, sun_glow, skyview, sun_lut):
        gpu = result[py, px, :3]
        err = float(np.max(np.abs(gpu - ref)) / max(np.max(np.abs(ref)), 1e-3))
        worst = max(worst, err)
        if err > 0.01:
            print(f'    pixel {px},{py} depth {depth_values[(px, py)]}: gpu {gpu} cpu {ref} ({100 * err:.2f} %)')
    print(f'  composite (sky mode {sky_mode}, sun glow {sun_glow}): worst relative error {100 * worst:.3f} %')
    for tex in (depth_tex, color, glow):
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(tex)))
    gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(U(fbo)))
    return worst < 0.01


def main():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 3)
    SDL.SDL_GL_SetAttribute(18, 2)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'Atmosphere test', 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    context = SDL.SDL_GL_CreateContext(window)
    assert context, SDL.SDL_GetError()
    ok = True
    try:
        print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
        vao = U()
        gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
        gl('glBindVertexArray', None, U)(vao)

        programs = {name: program(name) for name in
                    ['atmosphere_transmittance', 'atmosphere_multiscatter', 'atmosphere_skyview', 'atmosphere_composite']}
        for rgb in (False, True):
            gl('glDeleteProgram', None, U)(program('atmosphere_composite', froxel=True, rgb=rgb))
        print('PASS: 6 atmosphere programs compiled and linked (composite without / with the froxel functions, scalar / RGB)')

        sizes = [(256, 64), (32, 32), (192, 108)]
        textures = {i: texture2d(w, h) for i, (w, h) in enumerate(sizes)}
        textures['zero'] = texture2d(32, 32, values=[0.0] * (32 * 32 * 4))
        fbos = [framebuffer([textures[i]]) for i in range(3)]
        gl('glDisable', None, U)(0x0BE2)

        # transmittance, multiple scattering, sky-view (multiple scattering bound to zero for the CPU comparison)
        for i, name in enumerate(['atmosphere_transmittance', 'atmosphere_multiscatter', 'atmosphere_skyview']):
            prog = programs[name]
            gl('glUseProgram', None, U)(prog)
            set_atmosphere(prog, uniforms())
            bind(1, textures[0])
            bind(2, textures['zero'] if i == 2 else textures[1])
            draw(fbos[i], *sizes[i])
        luts = [read2d(textures[i], *sizes[i]) for i in range(3)]

        ok = check_transmittance(luts[0]) and ok
        ms = luts[1][:, :, :3]
        ms_ok = bool(np.all(np.isfinite(ms)) and ms.min() >= 0.0 and ms.max() < 1.0)
        print(f'  multiple scattering LUT: min {ms.min():.2e} max {ms.max():.2e} (sun up, ground: '
              f'{ms[0, 31, 0]:.4f} {ms[0, 31, 1]:.4f} {ms[0, 31, 2]:.4f}) {"OK" if ms_ok else "FAIL"}')
        ok = ms_ok and ok
        ok = check_skyview(luts[2]) and ok

        comp = programs['atmosphere_composite']
        ok = check_composite(comp, textures, luts, 0, 1.0) and ok
        ok = check_composite(comp, textures, luts, 0, 0.25) and ok
        ok = check_composite(comp, textures, luts, 2, 1.0) and ok

        # full sky-view with multiple scattering: plausibility (zenith bluer than the horizon, finite)
        gl('glUseProgram', None, U)(programs['atmosphere_skyview'])
        set_atmosphere(programs['atmosphere_skyview'], uniforms())
        bind(1, textures[0])
        bind(2, textures[1])
        draw(fbos[2], 192, 108)
        sky = read2d(textures[2], 192, 108)[:, :, :3]
        zenith = sky[0, 96]
        horizon = sky[53, 96]
        ratio_z = zenith[2] / max(zenith[0], 1e-9)
        ratio_h = horizon[2] / max(horizon[0], 1e-9)
        plaus = bool(np.all(np.isfinite(sky)) and ratio_z > ratio_h > 0.0)
        print(f'  sky-view with multiple scattering (x pi): zenith {np.round(zenith * math.pi, 4)}, horizon '
              f'{np.round(horizon * math.pi, 4)}, blue/red zenith {ratio_z:.2f} > horizon {ratio_h:.2f}: {"OK" if plaus else "FAIL"}')
        ok = plaus and ok
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
