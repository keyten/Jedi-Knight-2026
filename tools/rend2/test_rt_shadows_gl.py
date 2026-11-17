"""GPU checks of the ray traced shadows (r_sunShadowMode 2, r_dlightMode 3) on a hidden SDL GL 3.2 context.

Run from any directory: python tools/rend2/test_rt_shadows_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)
Needs g++ (w64devkit) to build the BVH of shared/rd-rend2/tr_rtbvh.cpp into a temporary DLL, see --gxx.

rt_shadows.glsl RT_TRACE: a synthetic scene (ground, floating boxes) is ray cast on the CPU into a depth
buffer; the shader reconstructs the receivers from it and traces the sun / point lights through the BVH
buffers exactly as tr_rtscene.cpp lays them out. Each pixel is compared with a brute force CPU ray cast
(silhouette pixels, where the depth derived normal is ambiguous, are left out):
  world      the static tree, no instances
  rigid      a box as an instance with rotation, scale and translation over a model space tree
  skinned    a deformed (Ghoul2 like) instance: the tree of the rest pose refitted on the CPU
  lights     two point lights and a spot light with the lights permutation
RT_RESOLVE / RT_FILTER: upsampling of a half resolution trace, temporal blend with clamping, the
depth aware filter keeps a depth edge. ao_composite multiplies the traced sun into the contact channel.
"""
import argparse
import ctypes as C
import math
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np

from test_watersurface_gl import (context, compile_program, tex2d, fbo, read_color, bind, gl, uloc,
    SDL, U, I, F, P, GL_VERTEX, GL_FRAGMENT, RGBA16F, R32F, RED, RGBA, FLOAT)

ROOT = Path(__file__).resolve().parents[2]
GL_TEXTURE_BUFFER = 0x8C2A
GL_RGBA32F = 0x8814
W, H = 160, 120
NEAR, FAR = 4.0, 8192.0


# ---------------------------------------------------------------- BVH (production C++)

def build_capi(gxx):
    out = Path(tempfile.gettempdir()) / 'rt_bvh_capi_test.dll'
    sources = [ROOT / 'tools/rend2/rt_bvh_capi.cpp', ROOT / 'shared/rd-rend2/tr_rtbvh.cpp']
    if not out.exists() or any(s.stat().st_mtime > out.stat().st_mtime for s in sources):
        subprocess.check_call([gxx, '-O2', '-std=c++11', '-shared', '-static', '-I', str(ROOT / 'shared/rd-rend2'),
            *map(str, sources), '-o', str(out)])
    lib = C.CDLL(str(out))
    lib.rt_build.argtypes = [P, I, P, I, C.POINTER(I), P]
    lib.rt_build.restype = I
    lib.rt_refit.argtypes = [P, I]
    return lib


def bvh(lib, tris):
    """tris (n, 3, 3) -> texels (k, 4), node count, slot order"""
    tris = np.ascontiguousarray(tris, dtype=np.float32).reshape(-1, 9)
    n = len(tris)
    cap = max(64, n * 8)
    texels = np.zeros((cap, 4), dtype=np.float32)
    order = np.zeros(max(n, 1), dtype=np.int32)
    nodes = I()
    count = lib.rt_build(tris.ctypes.data_as(P), n, texels.ctypes.data_as(P), cap, C.byref(nodes),
                         order.ctypes.data_as(P))
    assert count > 0
    return texels[:count].copy(), nodes.value, order[:n]


# ---------------------------------------------------------------- scene

def box(lo, hi):
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    c = [np.array([hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]]) for i in range(8)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    tris = []
    for a, b, cc, d in faces:
        tris += [(c[a], c[b], c[cc]), (c[a], c[cc], c[d])]
    return np.array(tris)


def ground(y=-60.0, size=4000.0):
    a, b, c, d = [(-size, y, -size), (size, y, -size), (size, y, size), (-size, y, size)]
    return np.array([(a, b, c), (a, c, d)], dtype=float)


def rotation_x(deg):
    r = math.radians(deg)
    return np.array([[1, 0, 0], [0, math.cos(r), -math.sin(r)], [0, math.sin(r), math.cos(r)]])


def ray_tris(origins, dirs, tris, t_min, t_max):
    """brute force any hit (two sided Moeller-Trumbore); origins/dirs (n, 3); t_min/t_max scalars or (n,)"""
    n = len(origins)
    hit = np.zeros(n, bool)
    t_min = np.broadcast_to(np.asarray(t_min, float), (n,))
    t_max = np.broadcast_to(np.asarray(t_max, float), (n,))
    for v0, v1, v2 in tris:
        e1, e2 = v1 - v0, v2 - v0
        p = np.cross(dirs, e2)
        det = p @ e1
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s = origins - v0
        u = np.einsum('ij,ij->i', s, p) * inv
        q = np.cross(s, e1)
        v = np.einsum('ij,ij->i', dirs, q) * inv
        t = (q @ e2) * inv
        hit |= ok & (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > t_min) & (t < t_max)
    return hit


def closest_hit(origins, dirs, tris):
    n = len(origins)
    best = np.full(n, np.inf)
    normal = np.zeros((n, 3))
    for v0, v1, v2 in tris:
        e1, e2 = v1 - v0, v2 - v0
        p = np.cross(dirs, e2)
        det = p @ e1
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s = origins - v0
        u = np.einsum('ij,ij->i', s, p) * inv
        q = np.cross(s, e1)
        v = np.einsum('ij,ij->i', dirs, q) * inv
        t = (q @ e2) * inv
        m = ok & (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > 1e-3) & (t < best)
        best = np.where(m, t, best)
        nrm = np.cross(e1, e2)
        nrm = nrm / np.linalg.norm(nrm)
        normal[m] = nrm
    return best, normal


class View:
    def __init__(self, pitch=18.0):
        self.p0 = 1.0 / math.tan(math.radians(90.0) * 0.5)
        self.p5 = self.p0 * W / H
        self.p10 = -(FAR + NEAR) / (FAR - NEAR)
        self.p14 = -2.0 * FAR * NEAR / (FAR - NEAR)
        self.rot = rotation_x(pitch)   # view (x right, y up, z forward) -> world
        self.origin = np.array([0.0, 0.0, 0.0])
        self.view_to_world = np.eye(4)
        self.view_to_world[:3, :3] = self.rot
        self.view_to_world[:3, 3] = self.origin

    def pixel_dirs(self):
        x = (np.arange(W) + 0.5) / W * 2 - 1
        y = (np.arange(H) + 0.5) / H * 2 - 1
        X, Y = np.meshgrid(x, y)
        v = np.stack([X / self.p0, Y / self.p5, np.ones_like(X)], -1).reshape(-1, 3)
        return v   # view space, z = 1

    def render(self, tris):
        """hardware depth (H, W), world positions (n, 3), camera facing normals (n, 3), mask of surfaces"""
        v = self.pixel_dirs()
        d = v @ self.rot.T
        z, normal = closest_hit(np.repeat(self.origin[None], len(d), 0), d, tris)
        surface = np.isfinite(z) & (z < FAR * 0.5)
        zz = np.where(surface, z, 1.0)
        ndc = -self.p10 + self.p14 / zz
        depth = np.where(surface, 0.5 * ndc + 0.5, 1.0)
        pos = self.origin + d * zz[:, None]
        flip = np.einsum('ij,ij->i', normal, d) > 0
        normal[flip] *= -1
        return depth.reshape(H, W).astype(np.float32), pos, normal, surface, zz

    def pixel_size(self):
        return 2.0 / (self.p0 * W)


def silhouette(depth):
    """pixels whose depth derived normal may pick another surface: a kink or a jump of the linear depth"""
    v = View()
    surface = depth < 1.0
    z = np.where(surface, v.p14 / (2.0 * depth.astype(np.float64) - 1.0 + v.p10), 0.0)
    edge = ~surface.copy()
    for axis in (0, 1):
        a, b = np.roll(z, 1, axis), np.roll(z, -1, axis)
        sa, sb = np.roll(surface, 1, axis), np.roll(surface, -1, axis)
        edge |= ~sa | ~sb | (np.abs(a + b - 2.0 * z) > 0.01 * z)
    grown = edge.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            grown |= np.roll(np.roll(edge, dy, 0), dx, 1)
    grown[0, :] = grown[-1, :] = grown[:, 0] = grown[:, -1] = True
    return grown & surface | grown


# ---------------------------------------------------------------- GL plumbing

def tbo(texels):
    data = np.ascontiguousarray(texels, dtype=np.float32)
    buf, tex = U(), U()
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(buf))
    gl('glBindBuffer', None, U, U)(GL_TEXTURE_BUFFER, buf)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(GL_TEXTURE_BUFFER, max(16, data.nbytes),
        data.ctypes.data_as(P) if data.size else None, 0x88E4)
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(GL_TEXTURE_BUFFER, tex)
    gl('glTexBuffer', None, U, U, U)(GL_TEXTURE_BUFFER, GL_RGBA32F, buf)
    return tex.value


def source(name):
    return (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text()


UNITS = [('u_RtDepthMap', 0), ('u_ScreenDepthMap', 1), ('u_RtCascadeMap', 3), ('u_SSRTraceMap', 5),
         ('u_SSRHistoryMap', 4), ('u_SSRHistoryGeomMap', 6), ('u_VelocityMap', 9), ('u_RtStaticBuffer', 5),
         ('u_RtDynamicBuffer', 8)]


def set_units(prog):
    # tr_glsl.cpp GLSL_LoadGPUProgramRtShadows: linear depth 0, hardware depth 1 (linearize), cascades 3,
    # trace map 5, history 4 (cube unit in the game), history depth 6, velocity 9; the trace programs take
    # 5 / 8 for the buffers
    gl('glUseProgram', None, U)(prog)
    for name, unit in UNITS:
        loc = uloc(prog, name)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)


def rt_program(defines):
    # self contained since the GL 4.3 kernels compile the same block (no ssr_common in front)
    text = source('rt_shadows')
    vs = text.split('/*[Vertex]*/', 1)[1].split('/*[Fragment]*/')[0]
    fs = text.split('/*[Fragment]*/', 1)[1]
    header = '#version 150 core\n' + defines
    prog = compile_program([(GL_VERTEX, header + vs), (GL_FRAGMENT, header + fs)], 'rt_shadows ' + defines)
    set_units(prog)
    return prog


def compute_program(defines):
    fs = source('rt_shadows').split('/*[Fragment]*/', 1)[1]
    prog = compile_program([(0x91B9, '#version 430 core\n#define RT_COMPUTE\n' + defines + fs)], 'rt_shadows cs ' + defines)
    set_units(prog)
    return prog


def v4(prog, name, rows):
    rows = np.asarray(rows, dtype=np.float32).reshape(-1, 4)
    loc = uloc(prog, name)
    if loc >= 0:
        gl('glUniform4fv', None, I, I, P)(loc, len(rows), np.ascontiguousarray(rows).ctypes.data_as(P))


def m4(prog, name, m):
    loc = uloc(prog, name)
    if loc >= 0:
        gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0,
            np.ascontiguousarray(np.asarray(m, np.float32).T).ctypes.data_as(P))


def bind_buffer(unit, tex):
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    gl('glBindTexture', None, U, U)(GL_TEXTURE_BUFFER, tex)


def view_uniforms(prog, view, tex_w=W, tex_h=H, dst_w=None, dst_h=None):
    dst_w, dst_h = dst_w or tex_w, dst_h or tex_h
    v4(prog, 'u_SSRProjection', [view.p0, view.p5, 0, 0])
    v4(prog, 'u_SSRDepthParams', [view.p14, view.p10, FAR, view.pixel_size()])
    v4(prog, 'u_SSRViewport', [0, 0, 1, 1])
    v4(prog, 'u_SSRTexelSize', [1 / W, 1 / H, 1 / dst_w, 1 / dst_h])
    m4(prog, 'u_RtViewToWorld', view.view_to_world)
    world_to_view = np.linalg.inv(view.view_to_world)
    m4(prog, 'u_SSRWorldToView', world_to_view)


def draw(prog, w, h, fmt=RGBA16F, attachments=1, keep=False):
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    targets = [tex2d(w, h, fmt) for _ in range(attachments)]
    target = fbo(targets)
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    gl('glUseProgram', None, U)(prog)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    out = read_color(target, w, h)
    assert np.isfinite(out).all()
    return (out, targets[0]) if keep else out


def tex(*args, **kw):
    """tex2d on a scratch unit: bind() leaves unit 0 active, a plain tex2d would replace what is bound there"""
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    return tex2d(*args, **kw)


_linearize = {}
LINEARIZE_ERRORS = []


def depth_texture(depth):
    """hardware depth -> RT_LINEARIZE -> linear view depth bound at unit 0, as RB_RenderRtShadows does"""
    if 'prog' not in _linearize:
        _linearize['prog'] = rt_program('#define RT_LINEARIZE\n')
    prog = _linearize['prog']
    view = View()
    bind(1, tex(W, H, R32F, RED, data=depth))
    gl('glUseProgram', None, U)(prog)
    v4(prog, 'u_SSRDepthParams', [view.p14, view.p10, FAR, view.pixel_size()])
    out, handle = draw(prog, W, H, fmt=R32F, keep=True)
    surface = depth < 1.0
    z = np.where(surface, view.p14 / (2.0 * depth.astype(np.float64) - 1.0 + view.p10), -1.0)
    LINEARIZE_ERRORS.append(float(np.max(np.abs(out[..., 0] - z) / np.maximum(np.abs(z), 1.0))))
    bind(0, handle)
    return handle


# ---------------------------------------------------------------- reference

BIAS = 1.0


def offsets(view, z):
    pixel = view.pixel_size()
    return 0.1 * BIAS + z * BIAS * (1.5 * pixel + 3.0e-4)


def reference_sun(view, pos, normal, surface, z, sun, occluders):
    off = offsets(view, z)
    origin = pos + normal * off[:, None]
    lit = np.ones(len(pos))
    facing = normal @ sun > -0.15
    test = surface & facing
    hit = ray_tris(origin[test], np.repeat(sun[None], test.sum(), 0), occluders, 0.5 * off[test], 131072.0)
    lit[test] = np.where(hit, 0.0, 1.0)
    lit[surface & ~facing] = 0.0
    return lit.reshape(H, W)


def reference_lights(view, pos, normal, surface, z, lights, occluders):
    off = offsets(view, z)
    origin = pos + normal * off[:, None]
    out = np.ones((len(pos), 4))
    for k, (lp, radius, spot) in enumerate(lights):
        to = lp[None] - origin
        dist = np.linalg.norm(to, axis=1)
        d = to / np.maximum(dist, 1e-9)[:, None]
        test = surface & (dist < radius) & (dist > 1e-3) & (np.einsum('ij,ij->i', normal, d) > -0.15)
        if spot is not None:
            axis, cos_outer = spot
            test &= -(d @ axis) >= cos_outer - 0.05
        hit = ray_tris(origin[test], d[test], occluders, 0.5 * off[test], np.maximum(dist[test] - off[test], 0.5 * off[test]))
        out[test, k] = np.where(hit, 0.0, 1.0)
    return out.reshape(H, W, 4)


def compare(name, got, want, edge, results, max_mismatch=0.01):
    valid = ~edge
    bad = (np.abs(got - want) > 0.5) & valid
    frac = bad.sum() / max(1, valid.sum())
    if os.environ.get('RT_DEBUG'):
        print(name, 'want shadowed', int((want < 0.5).sum()), 'got shadowed', int((got < 0.5).sum()), 'valid', int(valid.sum()))
    shadowed = (want < 0.5) & valid
    ok = frac <= max_mismatch and shadowed.sum() > 50 and (valid & (want > 0.5)).sum() > 50
    results[f'{name}: {bad.sum()} / {valid.sum()} pixels differ ({100 * frac:.2f}%), {shadowed.sum()} shadowed'] = ok


# ---------------------------------------------------------------- checks

SUN = np.array([0.35, 1.0, -0.25])
SUN = SUN / np.linalg.norm(SUN)


def scene_boxes():
    return [box((-60, -30, 140), (-10, -5, 200)), box((20, -45, 220), (70, -25, 300))]


def trace_checks(lib):
    results = {}
    view = View()
    sun_prog = rt_program('#define RT_TRACE\n')
    light_prog = rt_program('#define RT_TRACE\n#define RT_LIGHTS\n')
    empty_dynamic = tbo(np.zeros((4, 4)))

    def run(prog, static_tex, dynamic_tex, scene, lights=None):
        gl('glUseProgram', None, U)(prog)
        view_uniforms(prog, view)
        bind_buffer(5, static_tex)
        bind_buffer(8, dynamic_tex)
        v4(prog, 'u_RtScene', scene)
        params = [[*SUN, 0.0], [0.1 * BIAS, BIAS * (1.5 * view.pixel_size() + 3.0e-4), 131072.0, 0.0],
                  [1, 0, 0, 0], [len(lights or []), 0, 0, 0]]
        v4(prog, 'u_RtParams', params)
        rows = np.zeros((8, 4))
        for k, (lp, radius, spot) in enumerate(lights or []):
            rows[k * 2] = [*lp, radius]
            rows[k * 2 + 1] = [*spot[0], spot[1]] if spot is not None else [0, 0, 1, -2]
        v4(prog, 'u_RtLights', rows)
        v4(prog, 'u_RtHistory', [0, 0, 0, 0])
        return draw(prog, W, H, fmt=0x8814)

    # world: ground + boxes in the static tree
    world = np.concatenate([ground()] + scene_boxes())
    depth = view.render(world)
    depth_texture(depth[0])
    edge = silhouette(depth[0])
    texels, nodes, _ = bvh(lib, world)
    static = tbo(texels)
    got = run(sun_prog, static, empty_dynamic, [nodes, nodes * 2, 0, 0])[..., 0]
    want = reference_sun(view, *depth[1:4], depth[4], SUN, world)
    compare('world sun shadows', got, want, edge, results)

    # lights through the same world
    lights = [(np.array([-30.0, 30.0, 170.0]), 900.0, None), (np.array([60.0, 10.0, 330.0]), 600.0, None),
              (np.array([0.0, 40.0, 120.0]), 1200.0, (np.array([0.0, -0.8, 0.6]) / 1.0, 0.6))]
    lights = [(p, r, None if s is None else (s[0] / np.linalg.norm(s[0]), s[1])) for p, r, s in lights]
    got = run(light_prog, static, empty_dynamic, [nodes, nodes * 2, 0, 0], lights)
    want = reference_lights(view, *depth[1:4], depth[4], lights, world)
    for k in range(3):
        compare(f'world light {k} shadows', got[..., k], want[..., k], edge, results)

    # rigid instance: the second box comes from a model space tree under a transform
    model = box((-1, -1, -1), (1, 1, 1))
    rot = rotation_x(25.0) @ np.array([[math.cos(0.4), 0, math.sin(0.4)], [0, 1, 0], [-math.sin(0.4), 0, math.cos(0.4)]])
    scale = np.diag([25.0, 12.0, 30.0])
    local_to_world = np.eye(4)
    local_to_world[:3, :3] = rot @ scale
    local_to_world[:3, 3] = [40.0, -35.0, 250.0]
    placed = np.einsum('ij,tvj->tvi', local_to_world[:3, :3], model) + local_to_world[:3, 3]
    world_static = np.concatenate([ground(), scene_boxes()[0]])
    depth = view.render(np.concatenate([world_static, placed]))
    depth_texture(depth[0])
    edge = silhouette(depth[0])
    w_texels, w_nodes, _ = bvh(lib, world_static)
    m_texels, m_nodes, _ = bvh(lib, model)
    static = tbo(np.concatenate([w_texels, m_texels]))
    inv = np.linalg.inv(local_to_world)
    lo, hi = placed.reshape(-1, 3).min(0), placed.reshape(-1, 3).max(0)
    # dynamic buffer: one leaf node over one instance, then the instance at texel 2
    dyn = np.zeros((6, 4))
    dyn[0] = [*lo, 0.0]
    dyn[1] = [*hi, 1 * 8 + 1]
    dyn[2:5] = inv[:3]
    dyn[5] = [len(w_texels), len(w_texels) + m_nodes * 2, m_nodes, 0.0]
    got = run(sun_prog, static, tbo(dyn), [w_nodes, w_nodes * 2, 1, 2])[..., 0]
    want = reference_sun(view, *depth[1:4], depth[4], SUN, np.concatenate([world_static, placed]))
    compare('rigid instance sun shadows', got, want, edge, results)

    # skinned instance: rest pose tree, vertices bent like a limb, boxes refitted (tr_rtscene.cpp)
    rest = np.concatenate([box((-1, 0, -1), (1, 2, 1)), box((-1, 2, -1), (1, 4, 1))]) * np.array([14.0, 12.0, 14.0])
    texels, nodes, order = bvh(lib, rest)

    def bend(p):
        p = p.copy()
        a = np.clip(p[..., 1] / 40.0, 0, 1) * 0.9
        x, y = p[..., 0], p[..., 1]
        p[..., 0], p[..., 1] = x * np.cos(a) + y * np.sin(a), -x * np.sin(a) + y * np.cos(a)
        return p

    bent = bend(rest)
    tri_base = nodes * 2
    for slot, src in enumerate(order):
        v0, v1, v2 = bent[src]
        texels[tri_base + slot * 3] = [*v0, 0]
        texels[tri_base + slot * 3 + 1] = [*(v1 - v0), 0]
        texels[tri_base + slot * 3 + 2] = [*(v2 - v0), 0]
    texels = np.ascontiguousarray(texels, dtype=np.float32)
    stale = texels.copy()   # rest pose boxes around bent triangles
    lib.rt_refit(texels.ctypes.data_as(P), nodes)
    local_to_world = np.eye(4)
    local_to_world[:3, 3] = [-10.0, -50.0, 170.0]
    placed = bent + local_to_world[:3, 3]
    world_static = ground()
    depth = view.render(np.concatenate([world_static, placed]))
    depth_texture(depth[0])
    edge = silhouette(depth[0])
    w_texels, w_nodes, _ = bvh(lib, world_static)
    lo, hi = placed.reshape(-1, 3).min(0), placed.reshape(-1, 3).max(0)
    header = 8
    dyn = np.zeros((header + len(texels), 4))
    dyn[0] = [*lo, 0.0]
    dyn[1] = [*hi, 1 * 8 + 1]
    dyn[2:5] = np.linalg.inv(local_to_world)[:3]
    dyn[5] = [header, header + nodes * 2, nodes, 1.0]
    dyn[header:] = texels
    got = run(sun_prog, tbo(w_texels), tbo(dyn), [w_nodes, w_nodes * 2, 1, 2])[..., 0]
    want = reference_sun(view, *depth[1:4], depth[4], SUN, np.concatenate([world_static, placed]))
    compare('skinned (refitted) instance sun shadows', got, want, edge, results)
    # without the refit the rest pose boxes cut the bent limb's shadow short
    dyn[header:] = stale
    got = run(sun_prog, tbo(w_texels), tbo(dyn), [w_nodes, w_nodes * 2, 1, 2])[..., 0]
    wrong = ((np.abs(got - want) > 0.5) & ~edge).sum()
    results[f'refit is needed: {wrong} pixels wrong with the rest pose boxes'] = bool(wrong > 20)
    return results


def resolve_filter_checks():
    results = {}
    view = View()
    world = np.concatenate([ground()] + scene_boxes())
    depth = view.render(world)
    depth_texture(depth[0])

    # half resolution trace of a constant: the upsample is that constant on every surface
    prog = rt_program('#define RT_RESOLVE\n')
    gl('glUseProgram', None, U)(prog)
    view_uniforms(prog, view, dst_w=W // 2, dst_h=H // 2)
    v4(prog, 'u_RtParams', [[0, 1, 0, 0], [0, 0, 0, 0], [2, 0, 0, 0], [0, 0, 0, 0]])
    v4(prog, 'u_RtHistory', [0, 0, 0, 0])
    trace = tex(W // 2, H // 2, RGBA16F, RGBA, data=np.full((H // 2, W // 2, 4), 0.25, np.float32))
    bind(5, trace)
    out = draw(prog, W, H, attachments=2)
    surf = depth[3].reshape(H, W)
    results['resolve: half resolution constant upsampled'] = bool(np.allclose(out[surf][:, 0], 0.25, atol=2e-3))
    results['resolve: sky stays lit'] = bool(np.allclose(out[~surf][:, 0], 1.0))

    # temporal blend at rest (identity reprojection): mix(current, clamp(previous), w)
    m4(prog, 'u_SSRReproject', np.array([[view.p0, 0, 0, 0], [0, view.p5, 0, 0], [0, 0, 1, 0], [0, 0, 1, 0]]))
    v4(prog, 'u_RtParams', [[0, 1, 0, 0], [0, 0, 0, 0], [1, 0, 0, 0], [0, 0, 0, 0]])
    v4(prog, 'u_RtHistory', [0.75, 0.75, 0.75, 0.75])
    cur = np.zeros((H, W, 4), np.float32)
    cur[:, : W // 2] = 1.0
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    bind(5, tex(W, H, RGBA16F, RGBA, data=cur))
    hist = np.full((H, W, 4), 0.5, np.float32)
    bind(4, tex(W, H, RGBA16F, RGBA, data=hist, linear=True))
    z = np.where(surf, depth[4].reshape(H, W), 0.0).astype(np.float32)
    bind(6, tex(W, H, R32F, RED, data=z, linear=True))
    out = draw(prog, W, H, attachments=2)
    if os.environ.get('RT_DEBUG'):
        print('temporal sample', out[H * 3 // 4, W // 4], out[H * 3 // 4, 3 * W // 4], surf.sum())
    inner = surf & ~silhouette(depth[0])
    inner[:, W // 2 - 2: W // 2 + 2] = False
    left = inner.copy()
    left[:, W // 2:] = False
    right = inner.copy()
    right[:, : W // 2] = False
    if os.environ.get('RT_DEBUG'):
        bad = inner & (np.abs(out[..., 0] - np.where(np.arange(W)[None] < W // 2, 1.0, 0.0)) > 2e-3)
        ys, xs = np.nonzero(bad)
        print('temporal clamp bad', bad.sum(), list(zip(ys[:8], xs[:8])), out[ys[:4], xs[:4], 0] if len(ys) else '')
    # left: lit neighbourhood (min = max = 1) clamps the history to 1; right: 0
    results['temporal: history clamped to this frame\'s neighbourhood'] = bool(
        np.allclose(out[left][:, 0], 1.0, atol=2e-3) and np.allclose(out[right][:, 0], 0.0, atol=2e-3))
    cur2 = np.full((H, W, 4), 0.0, np.float32)
    cur2[(np.add.outer(np.arange(H), np.arange(W)) % 2) == 0] = 1.0   # checkerboard: every cross holds 0 and 1
    bind(5, tex(W, H, RGBA16F, RGBA, data=cur2))
    out = draw(prog, W, H, attachments=2)
    expect = 0.25 * cur2[..., 0] + 0.75 * 0.5
    if os.environ.get('RT_DEBUG'):
        bad = inner & (np.abs(out[..., 0] - expect) > 4e-3)
        ys, xs = np.nonzero(bad)
        print('temporal mix bad', bad.sum(), inner.sum(), list(zip(ys[:6], xs[:6])), out[ys[:4], xs[:4], 0] if len(ys) else '', expect[ys[:4], xs[:4]] if len(ys) else '')
    results['temporal: accumulation mix(current, history, 0.75)'] = bool(
        np.allclose(out[inner][:, 0], expect[inner], atol=4e-3))

    # filter: a constant stays, a shadow edge across a depth jump does not bleed
    prog = rt_program('#define RT_FILTER\n')
    gl('glUseProgram', None, U)(prog)
    view_uniforms(prog, view)
    v4(prog, 'u_RtParams', [[0, 1, 0, 0], [0, 0, 0, 0], [1, 0, 0, 0], [0, 0, 0, 0]])
    vis = np.where(surf, 0.6, 1.0).astype(np.float32)
    bind(5, tex(W, H, RGBA16F, RGBA, data=np.repeat(vis[..., None], 4, -1)))
    out = draw(prog, W, H)
    results['filter: constant visibility unchanged'] = bool(np.allclose(out[surf][:, 0], 0.6, atol=2e-3))
    # boxes black, ground white: the filter must not mix them at the silhouettes
    boxes = surf & (depth[1][:, 1].reshape(H, W) > -59.0)   # not the ground plane
    vis = np.where(boxes, 0.0, 1.0).astype(np.float32)
    bind(5, tex(W, H, RGBA16F, RGBA, data=np.repeat(vis[..., None], 4, -1)))
    out = draw(prog, W, H)
    # pixels next to the other object: the filter footprint straddles the depth jump there
    near = np.zeros_like(boxes)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            near |= np.roll(np.roll(boxes, dy, 0), dx, 1) != boxes
    jump = surf & near
    leak = np.abs(out[..., 0] - vis)[jump]
    results[f'filter: depth edges keep their side (mean leak {leak.mean():.3f}, max {leak.max():.3f})'] = bool(
        leak.mean() < 0.05 and leak.max() < 0.25)
    return results


def composite_checks():
    results = {}
    text = source('ao_composite')
    vs = text.split('/*[Vertex]*/', 1)[1].split('/*[Fragment]*/')[0]
    fs = text.split('/*[Fragment]*/', 1)[1]
    header = '#version 150 core\n'
    prog = compile_program([(GL_VERTEX, header + vs), (GL_FRAGMENT, header + fs)], 'ao_composite')
    gl('glUseProgram', None, U)(prog)
    for name, unit in [('u_AOMap', 0), ('u_ScreenDepthMap', 1), ('u_AODepthMap', 2), ('u_LegacyAOMap', 3),
                       ('u_AOBentMap', 4), ('u_RtShadowMap', 5)]:
        loc = uloc(prog, name)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    view = View()
    depth = view.render(np.concatenate([ground()] + scene_boxes()))
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    dtex = tex(W, H, R32F, RED, data=depth[0])
    white = tex(W, H, RGBA16F, RGBA, data=np.ones((H, W, 4), np.float32))
    for unit, handle in [(0, white), (1, dtex), (2, white), (3, white), (4, white)]:
        bind(unit, handle)
    vis = np.zeros((H, W, 4), np.float32)
    vis[:, W // 2:] = 1.0
    bind(5, tex(W, H, RGBA16F, RGBA, data=vis))
    v4(prog, 'u_AOProjection', [view.p0, view.p5, 0, 0])
    v4(prog, 'u_AODepthParams', [view.p14, view.p10, FAR, FAR * 0.99])
    v4(prog, 'u_AOViewport', [0, 0, 1, 1])
    v4(prog, 'u_AOTexelSize', [1 / W, 1 / H, 1 / W, 1 / H])
    v4(prog, 'u_AOSettings', [0, 0, 0, 1])
    v4(prog, 'u_AOSettings2', [16, 12, 6, view.pixel_size()])
    for w, label in [(1.0, 'traced sun multiplied into the contact channel'), (0.0, 'off: contact channel 1')]:
        v4(prog, 'u_AOSettings3', [0, 0, -1, w])
        out = draw(prog, W, H)
        want = vis[..., 0] if w else np.ones((H, W))
        results['ao_composite: ' + label] = bool(np.allclose(out[..., 1], want, atol=2e-3))
    return results


# ---------------------------------------------------------------- hybrid / GL 4.3 compute

def build_cascade(tris, center, half_size, half_depth, res):
    """an orthographic sun shadow map of the triangles, the way a cascade holds it (raw depth 0..1)"""
    f = -SUN
    a = np.array([0.0, 0.0, 1.0]) if abs(f[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    r = np.cross(f, a)
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    c = np.asarray(center, float)
    mvp = np.eye(4)
    mvp[0, :3], mvp[0, 3] = r / half_size, -r @ c / half_size
    mvp[1, :3], mvp[1, 3] = u / half_size, -u @ c / half_size
    mvp[2, :3], mvp[2, 3] = f / half_depth, -f @ c / half_depth
    g = (np.arange(res) + 0.5) / res * 2.0 - 1.0
    X, Y = np.meshgrid(g, g)
    starts = c + (X.reshape(-1, 1) * r + Y.reshape(-1, 1) * u) * half_size - f * half_depth
    t, _ = closest_hit(starts, np.repeat(f[None], len(starts), 0), tris)
    depth = np.where(np.isfinite(t), np.minimum(t / (2.0 * half_depth), 1.0), 1.0)
    return mvp, 2.0 * half_size / res, 2.0 * half_depth, depth.reshape(res, res).astype(np.float32)


def cascade_texture(depth):
    res = depth.shape[0]
    layers = np.ascontiguousarray(np.stack([depth] * 3), dtype=np.float32)
    handle = U()
    gl('glActiveTexture', None, U)(0x84C0 + 3)
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(handle))
    gl('glBindTexture', None, U, U)(0x8C1A, handle)
    gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(0x8C1A, 0, R32F, res, res, 3, 0, RED, FLOAT,
        layers.ctypes.data_as(P))
    for parameter, value in [(0x2800, 0x2600), (0x2801, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)]:
        gl('glTexParameteri', None, U, U, I)(0x8C1A, parameter, value)
    gl('glActiveTexture', None, U)(0x84C0)
    return handle.value


def setup(prog, view, static_tex, dynamic_tex, scene, lights=(), hybrid=None, debug=0.0, size=(W, H)):
    gl('glUseProgram', None, U)(prog)
    view_uniforms(prog, view)
    bind_buffer(5, static_tex)
    bind_buffer(8, dynamic_tex)
    v4(prog, 'u_RtScene', scene)
    v4(prog, 'u_RtParams', [[*SUN, 0.0], [0.1 * BIAS, BIAS * (1.5 * view.pixel_size() + 3.0e-4), 131072.0, 0.0],
                            [1, 0, 0, debug], [len(lights), 0, 0, 0]])
    rows = np.zeros((8, 4))
    for k, (lp, radius, spot) in enumerate(lights):
        rows[k * 2] = [*lp, radius]
        rows[k * 2 + 1] = [*spot[0], spot[1]] if spot is not None else [0, 0, 1, -2]
    v4(prog, 'u_RtLights', rows)
    v4(prog, 'u_RtHistory', [0, 0, 0, 0])
    if hybrid is None:
        v4(prog, 'u_RtHybrid', [0, 1.5, 1.0, 0])
    else:
        mvp, texel_world, span, res = hybrid
        v4(prog, 'u_RtHybrid', [1, 1.5, 1.0, 0])
        v4(prog, 'u_RtCascadeInfo', [[texel_world, span, 1.0 / res, 1.0], [0, 0, 0, 0], [0, 0, 0, 0]])
        loc = uloc(prog, 'u_RtCascadeMvp')
        mats = np.ascontiguousarray(np.stack([mvp.T] * 3), dtype=np.float32)
        gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 3, 0, mats.ctypes.data_as(P))


def ssbo(data_bytes, size):
    buf = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(buf))
    gl('glBindBuffer', None, U, U)(0x90D2, buf)
    raw = C.create_string_buffer(bytes(data_bytes).ljust(size, b'\0'), size)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, size, raw, 0x88E8)
    return buf.value


def run_compute(classify, trace_list, lights_kind):
    """RB_RtTraceCompute: classify writes / lists, the list goes through an indirect dispatch.
    Returns the trace image and the number of listed (traced) pixels."""
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    internal = 0x8058 if lights_kind else 0x8229
    image = tex2d(W, H, internal, RGBA if lights_kind else RED)
    listed = ssbo(b'', W * H * 4)
    args = ssbo(np.array([0, 1, 1, 0], np.uint32).tobytes(), 16)
    gl('glBindBufferBase', None, U, U, U)(0x90D2, 0, listed)
    gl('glBindBufferBase', None, U, U, U)(0x90D2, 1, args)
    gl('glBindImageTexture', None, U, U, I, U, I, U, U)(0, image, 0, 0, 0, 0x88B9, internal)
    gl('glUseProgram', None, U)(classify)
    gl('glDispatchCompute', None, U, U, U)((W + 7) // 8, (H + 7) // 8, 1)
    gl('glMemoryBarrier', None, U)(0x2000 | 0x40 | 0x20)
    gl('glUseProgram', None, U)(trace_list)
    gl('glBindBuffer', None, U, U)(0x90EE, args)
    gl('glDispatchComputeIndirect', None, C.c_ssize_t)(0)
    gl('glMemoryBarrier', None, U)(0xFFFFFFFF)
    gl('glBindBuffer', None, U, U)(0x90D2, args)
    out = C.create_string_buffer(16)
    gl('glGetBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(0x90D2, 0, 16, out)
    groups, _, _, count = np.frombuffer(out.raw, np.uint32)
    result = read_color(fbo([image]), W, H)
    return result, int(count), int(groups)


def hybrid_compute_checks(lib, compute):
    results = {}
    view = View()
    world = np.concatenate([ground()] + scene_boxes())
    depth = view.render(world)
    depth_texture(depth[0])
    edge = silhouette(depth[0])
    texels, nodes, _ = bvh(lib, world)
    static, dynamic = tbo(texels), tbo(np.zeros((4, 4)))
    scene = [nodes, nodes * 2, 0, 0]
    want = reference_sun(view, *depth[1:4], depth[4], SUN, world)
    surface = depth[3].reshape(H, W)

    mvp, texel_world, span, sm = build_cascade(world, (0.0, -40.0, 230.0), 260.0, 700.0, 512)
    cascade_texture(sm)
    hybrid = (mvp, texel_world, span, sm.shape[0])

    sun_prog = rt_program('#define RT_TRACE\n')
    setup(sun_prog, view, static, dynamic, scene)
    full = draw(sun_prog, W, H, fmt=0x8814)[..., 0]
    setup(sun_prog, view, static, dynamic, scene, hybrid=hybrid)
    mixed = draw(sun_prog, W, H, fmt=0x8814)[..., 0]
    compare('hybrid sun shadows (r_rtShadowsHybrid 1) against the reference', mixed, want, edge, results)
    setup(sun_prog, view, static, dynamic, scene, hybrid=hybrid, debug=5.0)
    classes = draw(sun_prog, W, H, fmt=0x8814)[..., 0]
    traced = np.abs(classes - 0.5) < 0.1
    frac = traced[surface].mean()
    results[f'hybrid: {100 * frac:.1f}% of the surface pixels still traced, the rest decided by the cascade'] = bool(frac < 0.5)
    agree = ((np.abs(mixed - full) > 0.5) & ~edge).sum()
    results[f'hybrid and full trace differ in {agree} pixels'] = bool(agree <= 0.005 * (~edge).sum())

    if not compute:
        results['GL 4.3 compute path: no GL 4.3 context (skipped)'] = True
        return results

    lights = [(np.array([-30.0, 30.0, 170.0]), 900.0, None), (np.array([60.0, 10.0, 330.0]), 600.0, None)]
    progs = {k: compute_program(d) for k, d in [
        ('cs', '#define RT_CLASSIFY\n#define RT_GROUP_X 8\n#define RT_GROUP_Y 8\n'),
        ('ts', '#define RT_TRACE_LIST\n#define RT_GROUP_X 64\n#define RT_GROUP_Y 1\n'),
        ('cl', '#define RT_CLASSIFY\n#define RT_LIGHTS\n#define RT_GROUP_X 8\n#define RT_GROUP_Y 8\n'),
        ('tl', '#define RT_TRACE_LIST\n#define RT_LIGHTS\n#define RT_GROUP_X 64\n#define RT_GROUP_Y 1\n')]}
    for hyb, label, raster in [(None, 'full', full), (hybrid, 'hybrid', mixed)]:
        for key in ('cs', 'ts'):
            setup(progs[key], view, static, dynamic, scene, hybrid=hyb)
        out, count, groups = run_compute(progs['cs'], progs['ts'], False)
        diff = (np.abs(out[..., 0] - raster) > 0.5).sum()
        results[f'compute {label} sun trace = raster ({diff} pixels differ, {count} pixels listed in {groups} groups)'] = bool(
            diff <= 0.001 * W * H and groups == (count + 63) // 64)
    light_prog = rt_program('#define RT_TRACE\n#define RT_LIGHTS\n')
    setup(light_prog, view, static, dynamic, scene, lights)
    raster = draw(light_prog, W, H, fmt=0x8814)
    for key in ('cl', 'tl'):
        setup(progs[key], view, static, dynamic, scene, lights)
    out, count, groups = run_compute(progs['cl'], progs['tl'], True)
    diff = (np.abs(out[..., :2] - raster[..., :2]) > 0.5).any(-1).sum()
    results[f'compute light trace = raster ({diff} pixels differ, {count} listed)'] = bool(diff <= 0.001 * W * H)
    return results


def context43(title):
    """GL 4.3 core where the driver has it (the compute checks), else 3.2"""
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    for major, minor in ((4, 3), (3, 2)):
        SDL.SDL_GL_SetAttribute(17, major)
        SDL.SDL_GL_SetAttribute(18, minor)
        SDL.SDL_GL_SetAttribute(21, 1)
        window = SDL.SDL_CreateWindow(title, 0, 0, 32, 32, 10)
        ctx = SDL.SDL_GL_CreateContext(window)
        if ctx:
            print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
            return window, ctx, major == 4
        SDL.SDL_DestroyWindow(window)
    raise RuntimeError(SDL.SDL_GetError())


def lightall_checks():
    """lightall permutations of mode 1, mode 2 (USE_RT_SHADOWS) and r_dlightMode 3 (USE_RT_DSHADOWS)"""
    import re
    from test_liquids_gl import render_header, stage
    results = {}
    vertex_library = stage('leaf_flutter', 'Vertex') + stage('plant_skin', 'Vertex')
    pbr = stage('pbr_common', 'Fragment')
    ids = re.findall(r'^COMPARE_FEATURE\(\s*(RC_\w+)', (ROOT / 'shared/rd-rend2/tr_compare.inc').read_text(), re.M)
    base = ['USE_LIGHT', 'USE_SHADOWMAP', 'USE_SHADOWS2', 'USE_SSAO']
    cases = [
        base + ['USE_LIGHTMAP', 'SHADOWMAP_MODULATE'],                              # mode 1 (unchanged)
        base + ['USE_LIGHTMAP', 'SHADOWMAP_MODULATE', 'USE_SUN_SHADOW_SPLIT'],      # mode 1, model layer
        base + ['USE_LIGHTMAP', 'SHADOWMAP_MODULATE', 'USE_RT_SHADOWS'],            # mode 2
        base + ['USE_LIGHT_VECTOR', 'USE_PRIMARY_LIGHT', 'USE_NORMALMAP', 'USE_SPECULARMAP', 'USE_RT_SHADOWS'],
        base + ['USE_LIGHTMAP', 'SHADOWMAP_MODULATE', 'USE_DSHADOWS'],              # dlight mode 2
        base + ['USE_LIGHTMAP', 'SHADOWMAP_MODULATE', 'USE_DSHADOWS', 'USE_RT_DSHADOWS', 'USE_RT_SHADOWS'],
        base + ['USE_LIGHT_VECTOR', 'USE_NORMALMAP', 'USE_SPECULARMAP', 'USE_DSHADOWS', 'USE_RT_DSHADOWS',
                'USE_LTC', 'USE_LTC_SHADOWS'],
        ['USE_LIGHT', 'USE_LIGHTMAP', 'USE_DSHADOWS', 'USE_RT_DSHADOWS'],          # legacy sun, traced lights
    ]
    for defines in cases:
        h = render_header(defines)
        h += '#define r_shadowMapSize 1024\n#define r_shadowCascadeZFar 3072\n'
        h += ''.join('#define {} {}\n'.format(key, i) for i, key in enumerate(ids))
        h += ('#define R_CompareOriginal(feature) (gl_FragCoord.x < u_CompareSplit.x && '
              '(u_CompareMask[(feature) >> 5] & (1 << ((feature) & 31))) != 0)\n')
        label = 'lightall ' + ' '.join(defines)
        prog = compile_program([(GL_VERTEX, h + vertex_library + stage('lightall', 'Vertex')),
                                (GL_FRAGMENT, h + pbr + stage('lightall', 'Fragment'))], label)
        if 'USE_RT_DSHADOWS' in defines:
            assert uloc(prog, 'u_RtShadowMap') >= 0, label
        gl('glDeleteProgram', None, U)(prog)
    results[f'{len(cases)} lightall permutations (mode 1, mode 2, traced lights) compile and link'] = True
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gxx', default=os.environ.get('GXX', str(Path(
        'C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/g++.exe'))))
    args = parser.parse_args()
    lib = build_capi(args.gxx)
    window, ctx, gl43 = context43(b'Ray traced shadows')
    vao = U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    try:
        outcomes = {**trace_checks(lib), **hybrid_compute_checks(lib, gl43), **resolve_filter_checks(),
                    **composite_checks(), **lightall_checks()}
        worst = max(LINEARIZE_ERRORS)
        outcomes[f'RT_LINEARIZE matches the CPU view depth (max relative error {worst:.1e})'] = worst < 1e-4
        for name, ok in outcomes.items():
            print(f'{"PASS" if ok else "FAIL"}: {name}', flush=True)
        assert gl('glGetError', U)() == 0
        assert all(outcomes.values())
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()


if __name__ == '__main__':
    main()
