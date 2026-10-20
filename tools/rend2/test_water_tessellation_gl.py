"""GPU checks of r_waterTessellation (glsl/watersurface.glsl USE_WATER_TESSELLATION, docs/rend2-water-geometry.md)
on a hidden SDL GL 4.3 core context.

Run from any directory: python tools/rend2/test_water_tessellation_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)
GPU timings at 1920x1080: python tools/rend2/test_water_tessellation_gl.py --bench

The programs are assembled as GLSL_LoadGPUProgramWaterSurface builds the _TESS permutations: #version 430 in every
stage, the vertex block as the vertex stage and again (WATER_TESS_EVALUATION) as the evaluation stage, the TessControl
block, the usual fragment stage. The scene, the uniforms and the draw come from test_watersurface_gl.py.

- every tessellated permutation (deform, SSR linear / Hi-Z, Snell, water motion, froxel, cubemap) compiles and links;
  the untessellated program still compiles in the same 4.3 context
- no cracks: a base grid seen at a grazing angle, its patches split from 1 to the max level across the view, with
  geometry waves; every pixel whose ray meets the base plane well inside the mesh is covered. The same check with
  levels that depend on the patch (not only on the edge) must find holes, so it can see them
- r_waterTessForce N on a base grid matches the untessellated program on the static grid N times finer (depth)
- the geometry weights of the two candidate waves: 1 near the camera, 0 far away, monotonic in between
- patches outside the view are dropped (GL_PRIMITIVES_GENERATED); a skirt patch stays one strip of its top edge
- the shore attenuation comes from the interpolated distance: a coarse base triangle keeps the fine falloff
"""
import ctypes as C
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_watersurface_gl as T  # noqa: E402

U, I, F, P = C.c_uint, C.c_int, C.c_float, C.c_void_p
gl, SDL, check = T.gl, T.SDL, T.check
GL_TESS_CONTROL, GL_TESS_EVALUATION = 0x8E88, 0x8E87
GL_PATCHES, GL_TRIANGLES = 0x000E, 0x0004
GL_PRIMITIVES_GENERATED = 0x8C87
MAX_LEVEL = 16.0
PIXELS = 12.0


def tess_sources(**kw):
    """vertex, control, evaluation and fragment sources of a _TESS permutation"""
    original = T.header
    headers = []

    def modern_header(defines, size):
        text = original(list(defines) + ['USE_WATER_TESSELLATION'], size)
        text = text.replace('#version 150 core\n', '#version 430 core\n', 1)
        headers.append(text)
        return text
    T.header = modern_header
    try:
        vs, fs = T.water_sources(**kw)
    finally:
        T.header = original
    h = headers[0]
    src = T.read('watersurface')
    tcs = src.split('/*[TessControl]*/')[1].split('/*[Fragment]*/')[0]
    assert vs.startswith(h)
    tes = h + '#define WATER_TESS_EVALUATION\n' + vs[len(h):]
    return vs, h + tcs, tes, fs


def tess_program(control=None, **kw):
    vs, tcs, tes, fs = tess_sources(**kw)
    if control is not None:
        tcs = control(tcs)
    prog = T.compile_program([(T.GL_VERTEX, vs), (GL_TESS_CONTROL, tcs), (GL_TESS_EVALUATION, tes),
                              (T.GL_FRAGMENT, fs)], 'watersurface_TESS ' + repr(kw),
                             attributes=('attr_Position', 'attr_Normal', 'attr_TexCoord0', 'attr_TexCoord1'))
    gl('glUseProgram', None, U)(prog)
    for sampler, unit in T.UNITS.items():
        loc = T.uloc(prog, sampler)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    return prog


def grid(x0, y0, x1, y1, cell, top=1.0, shore=65000.25):
    """non-indexed triangles of a regular grid (the diagonals alternate), rend2 water vertex layout"""
    nx, ny = int(round((x1 - x0) / cell)), int(round((y1 - y0) / cell))
    xs = [x0 + (x1 - x0) * i / nx for i in range(nx + 1)]
    ys = [y0 + (y1 - y0) * j / ny for j in range(ny + 1)]
    out = []

    def v(x, y):
        s = shore(x, y) if callable(shore) else shore
        return [x, y, 0.0, 0.5, 0.5, 1.0, x / 256.0, y / 256.0, top, s]
    for j in range(ny):
        for i in range(nx):
            a, b, c, d = (xs[i], ys[j]), (xs[i + 1], ys[j]), (xs[i + 1], ys[j + 1]), (xs[i], ys[j + 1])
            quad = [a, b, c, a, c, d] if (i + j) % 2 == 0 else [a, b, d, b, c, d]
            out += [v(*p) for p in quad]
    return np.array(out, dtype=np.float32)


def max_edge(cell):
    return cell * math.sqrt(2.0)


class TessRig(T.Rig):
    def set_mesh(self, mesh, patches):
        self.water_vao, self.water_count = T.vertex_buffer(mesh, (3, 3, 2, 2))
        self.water_mode = GL_PATCHES if patches else GL_TRIANGLES


def wave_params(rig, base_edge, wavelength=192.0, shore=False):
    u = rig.params()
    u[13] = [1.0, 1.0, 1.0, 1.0]
    u[14] = [1.0, 1.0, 1.0, 0.0]
    u[15] = [20.0, wavelength, 1.0, 1.0]
    u[18] = [0.0, 1.0, 0.2, 1.0]
    u[19, 1] = 1.0          # the body has a render mesh
    u[55] = [2.0, base_edge, base_edge * 4.0, 0.0]
    if shore:
        u[34] = [1.0, 96.0, 4.0, 0.0]
    return u


def set_tess(prog, rig, force=0.0, extent=200.0, pixels=PIXELS, min_edge=0.0, lod_distance=0.0):
    scale = rig.cam.proj[0, 0] * rig.size[0] * 0.5 / pixels
    T.set_vec4s(prog, 'u_WaterTess', [[scale, MAX_LEVEL, force, extent]])
    T.set_vec4s(prog, 'u_WaterTessRange', [[min_edge, lod_distance, 0.0, 0.0]])


def draw(rig, prog, u, mesh, patches, force=0.0, query=False, tess=None, **kw):
    rig.set_mesh(mesh, patches)
    gl('glUseProgram', None, U)(prog)
    if patches:
        set_tess(prog, rig, force, **(tess or {}))
    q = U()
    if query:
        gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
        gl('glBeginQuery', None, U, U)(GL_PRIMITIVES_GENERATED, q)
    # draw_water sets u_Water / u_WaterPass after the program is bound; u_WaterTess stays
    color = rig.draw_water(prog, u, waves=True, **kw)
    primitives = None
    if query:
        gl('glEndQuery', None, U)(GL_PRIMITIVES_GENERATED)
        value = C.c_uint64()
        gl('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))(q, 0x8866, C.byref(value))
        primitives = value.value
    return color, primitives


def interior(rig, lo, hi, margin):
    """pixels whose ray meets z = 0 inside the box lo hi shrunk by margin"""
    d = rig.cam.rays()
    with np.errstate(divide='ignore', invalid='ignore'):
        t = np.where(d[..., 2] < -1e-4, -rig.cam.eye[2] / d[..., 2], np.inf)
    p = rig.cam.eye[None, None, :2] + d[..., :2] * t[..., None]
    inside = (p[..., 0] > lo[0] + margin) & (p[..., 0] < hi[0] - margin) & \
             (p[..., 1] > lo[1] + margin) & (p[..., 1] < hi[1] - margin) & np.isfinite(t)
    return inside


# --- checks -------------------------------------------------------------------------------------------------------

def check_permutations():
    count = 0
    for deform, ssr, hiz, snell, motion, froxel, cubemap in [
            (0, 0, 0, 0, 0, 0, 0), (1, 0, 0, 0, 0, 0, 0), (0, 1, 0, 0, 0, 0, 0), (0, 1, 1, 0, 0, 0, 0),
            (0, 0, 0, 1, 0, 0, 0), (0, 0, 0, 0, 1, 0, 0), (1, 1, 1, 1, 1, 0, 0), (0, 0, 0, 0, 0, 1, 1),
            (1, 1, 1, 1, 1, 2, 1)]:
        prog = tess_program(deform=bool(deform), ssr=bool(ssr), hiz=bool(hiz), snell=bool(snell),
                            motion=bool(motion), froxel=froxel, cubemap=bool(cubemap))
        gl('glDeleteProgram', None, U)(prog)
        count += 1
    prog = T.water_program(motion=True, deform=True)
    gl('glDeleteProgram', None, U)(prog)
    return check(f'{count} tessellated water permutations and the untessellated program compile and link in GL 4.3',
                 True)


def check_cracks(rig):
    ok = True
    lo, hi = (-200.0, -3000.0), (3000.0, 3000.0)
    cell = 200.0
    mesh = grid(lo[0], lo[1], hi[0], hi[1], cell)
    u = wave_params(rig, max_edge(cell))
    prog = tess_program()
    inside = interior(rig, lo, hi, 150.0)
    color, primitives = draw(rig, prog, u, mesh, True, query=True)
    holes = int(((color[..., 3] < 0.0) & inside).sum())
    ok = check('no cracks between patches of different levels (grazing view, geometry waves)',
               inside.sum() > 0.3 * inside.size and holes == 0,
               f'{holes} uncovered of {int(inside.sum())} interior pixels, {primitives} primitives from '
               f'{len(mesh) // 3} patches') and ok
    # levels spread over the view
    debug = tess_program(body=T.probe('vec4(var_TessInfo, 1.0)'))
    info, _ = draw(rig, debug, u, mesh, True)
    levels = info[..., 2][inside]
    ok = check('the levels span the range across the view', levels.max() >= MAX_LEVEL - 0.5 and levels.min() < 3.0,
               f'min {levels.min():.2f} max {levels.max():.2f}') and ok

    def per_patch(tcs):
        line = 'precise float d = max(distance((a + b) * 0.5, u_ViewOrigin) - u_WaterTessRange.y, 1.0);'
        assert tcs.count(line) == 1
        return tcs.replace(line, line[:-1] + ' * (1.0 + 1.5 * float(gl_PrimitiveID % 2));')
    broken = tess_program(control=per_patch)
    color, _ = draw(rig, broken, u, mesh, True)
    broken_holes = int(((color[..., 3] < 0.0) & inside).sum())
    ok = check('control: levels that depend on the patch leave holes the check finds', broken_holes > 0,
               f'{broken_holes} uncovered pixels') and ok
    for p in (prog, debug, broken):
        gl('glDeleteProgram', None, U)(p)
    return ok


def check_range(rig):
    """r_waterTessMinEdge caps the detail near the camera, r_waterTessLodDistance keeps full detail up to it;
    both stay crack free and the weights follow the same edge estimate"""
    ok = True
    lo, hi = (-200.0, -3000.0), (3000.0, 3000.0)
    cell = 200.0
    mesh = grid(lo[0], lo[1], hi[0], hi[1], cell)
    u = wave_params(rig, max_edge(cell))
    prog = tess_program()
    debug = tess_program(body=T.probe('vec4(var_TessInfo, 1.0)'))
    inside = interior(rig, lo, hi, 150.0)
    counts, levels = {}, {}
    for name, tess in (('default', {}), ('min edge 64', {'min_edge': 64.0}),
                       ('lod 1500', {'lod_distance': 1500.0}), ('both', {'min_edge': 64.0, 'lod_distance': 1500.0})):
        color, counts[name] = draw(rig, prog, u, mesh, True, query=True, tess=tess)
        holes = int(((color[..., 3] < 0.0) & inside).sum())
        ok = check(f'{name}: no cracks', holes == 0, f'{holes} uncovered, {counts[name]} primitives') and ok
        info, _ = draw(rig, debug, u, mesh, True, tess=tess)
        levels[name] = info[..., 2][inside]
    # a 200-unit edge (283 diagonal) never splits below 64 units: level <= 283 / 64
    ok = check('r_waterTessMinEdge 64 caps the levels and the primitives',
               levels['min edge 64'].max() <= max_edge(cell) / 64.0 + 1.01 and counts['min edge 64'] < counts['default'],
               f'max level {levels["min edge 64"].max():.2f}, {counts["min edge 64"]} vs {counts["default"]} primitives') and ok
    d = rig.cam.rays()
    t = np.where(d[..., 2] < -1e-4, -rig.cam.eye[2] / np.minimum(d[..., 2], -1e-4), np.inf)[inside]
    near = t < 1000.0
    ok = check('r_waterTessLodDistance 1500 keeps the max level within it, more primitives',
               levels['lod 1500'][near].min() >= MAX_LEVEL - 0.5 and counts['lod 1500'] > counts['default'],
               f'min level within 1000 units {levels["lod 1500"][near].min():.2f}, '
               f'{counts["lod 1500"]} vs {counts["default"]} primitives') and ok
    gl('glDeleteProgram', None, U)(prog)
    gl('glDeleteProgram', None, U)(debug)
    return ok


def linear_depth(rig, d):
    p = rig.cam.proj
    return p[2, 3] / (d * 2.0 - 1.0 + p[2, 2])


def check_forced_level(rig):
    """steeper view: no crest silhouettes, where the two meshes may legitimately see different crests"""
    ok = True
    lo, hi = (-200.0, -3000.0), (3000.0, 3000.0)
    tess = tess_program()
    static = T.water_program()
    for force, cell in ((4, 400.0), (8, 800.0)):
        fine = cell / force
        # long waves: both meshes resolve the two candidates in full
        u_static = wave_params(rig, max_edge(fine), wavelength=1500.0)
        u_tess = wave_params(rig, max_edge(cell), wavelength=1500.0)
        draw(rig, static, u_static, grid(lo[0], lo[1], hi[0], hi[1], fine), False)
        ref = T.read_depth(rig.water_fbo, *rig.size)
        draw(rig, tess, u_tess, grid(lo[0], lo[1], hi[0], hi[1], cell), True, force=float(force))
        got = T.read_depth(rig.water_fbo, *rig.size)
        both = (ref < 0.999999) & (got < 0.999999) & interior(rig, lo, hi, 150.0)
        a, b = linear_depth(rig, ref[both]), linear_depth(rig, got[both])
        rel = np.abs(a - b) / np.maximum(np.abs(a), 1.0)
        ok = check(f'r_waterTessForce {force} on {cell:.0f} cells matches the static {fine:.0f} grid',
                   both.sum() > 1000 and np.percentile(rel, 99) < 1e-3 and rel.max() < 5e-3,
                   f'99% {np.percentile(rel, 99):.2e}, max {rel.max():.2e} relative linear depth') and ok
    gl('glDeleteProgram', None, U)(tess)
    gl('glDeleteProgram', None, U)(static)
    return ok


def check_weights(rig):
    lo, hi = (-200.0, -3000.0), (3000.0, 3000.0)
    cell = 200.0
    mesh = grid(lo[0], lo[1], hi[0], hi[1], cell)
    u = wave_params(rig, max_edge(cell), wavelength=192.0)
    prog = tess_program(body=T.probe('vec4(var_TessInfo, 1.0)'))
    info, _ = draw(rig, prog, u, mesh, True)
    inside = interior(rig, lo, hi, 150.0)
    d = rig.cam.rays()
    t = np.where(d[..., 2] < -1e-4, -rig.cam.eye[2] / np.minimum(d[..., 2], -1e-4), np.inf)
    w0 = info[..., 0][inside]
    dist = (t * np.linalg.norm(d, axis=-1))[inside]
    near, far = w0[dist < np.percentile(dist, 10)], w0[dist > np.percentile(dist, 90)]
    order = np.argsort(dist)
    smooth = np.convolve(w0[order], np.ones(64) / 64, mode='valid')
    ok = check('geometry weight 1 near the camera, 0 far away, falling with distance',
               near.min() > 0.99 and far.max() < 0.01 and np.all(np.diff(smooth) < 0.02),
               f'near min {near.min():.3f}, far max {far.max():.3f}')
    w1 = info[..., 1][inside]
    ok = check('the shorter candidate leaves the mesh first', np.all(w1 <= w0 + 1e-4)) and ok
    gl('glDeleteProgram', None, U)(prog)
    return ok


def check_cull_and_skirt(rig):
    ok = True
    prog = tess_program()
    mesh = grid(-200.0, -3000.0, 3000.0, 3000.0, 200.0)
    u = wave_params(rig, max_edge(200.0))
    _, seen = draw(rig, prog, u, mesh, True, query=True)
    # the same grid moved behind the camera (it looks along -y from y = 0)
    behind = mesh.copy()
    behind[:, 1] = 6000.0 - behind[:, 1]
    behind[:, 1] += 400.0
    _, hidden = draw(rig, prog, u, behind, True, query=True)
    ok = check('patches outside the view are dropped', seen > 1000 and hidden == 0,
               f'{seen} primitives in view, {hidden} behind the camera') and ok
    # one skirt quad below a surface edge seen at a forced level 8
    top0, top1 = (1000.0, -400.0), (1400.0, -400.0)
    skirt = []
    for (x, y), top in ((top0, 1.0), ((top0[0], top0[1]), 0.0), (top1, 1.0),
                        (top1, 1.0), (top0, 0.0), (top1, 0.0)):
        skirt.append([x, y, 0.0 if top > 0.5 else -32.0, 0.5, 0.5, 1.0, x / 256.0, y / 256.0, top, 65000.25])
    skirt = np.array(skirt, dtype=np.float32)
    rig.set_mesh(skirt, True)
    _, strip = draw(rig, prog, u, skirt, True, force=8.0, query=True)
    surface = grid(1000.0, -800.0, 1400.0, -400.0, 400.0)
    _, full = draw(rig, prog, u, surface, True, force=8.0, query=True)
    ok = check('a skirt patch splits only its top edge (level 1 elsewhere)', strip <= 2 * 9 and full > 4 * strip,
               f'skirt quad {strip} primitives, surface quad {full}') and ok
    gl('glDeleteProgram', None, U)(prog)
    return ok


def check_shore(rig):
    """one natural boundary at x = -200: a coarse base keeps the shore falloff of the interpolated distance"""
    lo, hi = (-200.0, -2000.0), (600.0, -1000.0)

    def shore(x, y):
        return 1.0 * (abs(x - lo[0]) + 0.25)    # natural: positive
    probe = 'vec4(var_ShoreData, 1.0)'
    fine_prog = T.water_program(body=T.probe(probe))
    tess_prog = tess_program(body=T.probe(probe))
    # flat (no wave amplitude): the same surface point under each pixel
    u_fine = wave_params(rig, max_edge(8.0), shore=True)
    u_tess = wave_params(rig, max_edge(800.0), shore=True)
    u_fine[15, 0] = u_tess[15, 0] = 0.0
    fine, _ = draw(rig, fine_prog, u_fine, grid(lo[0], lo[1], hi[0], hi[1], 8.0, shore=shore), False)
    coarse, _ = draw(rig, tess_prog, u_tess, grid(lo[0], lo[1], hi[0], hi[1], 800.0, shore=shore), True)
    mask = (fine[..., 3] > 0.0) & (coarse[..., 3] > 0.0)
    distance_error = np.abs(fine[..., 0] - coarse[..., 0])[mask]
    # natural boundary, profile shore 0: mix(0.15, 1, smoothstep(0, 0.35 width, d))
    x = np.clip(coarse[..., 0] / (96.0 * 0.35), 0.0, 1.0)
    analytic = 0.15 + 0.85 * x * x * (3.0 - 2.0 * x)
    exact_error = np.abs(coarse[..., 1] - analytic)[mask]
    err = np.abs(fine[..., 1] - coarse[..., 1])[mask]
    ok = check('shore of an 800-unit base triangle: exact interpolated distance, per-pixel attenuation, as the '
               '8-unit static mesh', mask.sum() > 1000 and distance_error.max() < 0.5 and exact_error.max() < 2e-3
               and err.max() < 0.03, f'distance {distance_error.max():.3f}, attenuation vs analytic '
               f'{exact_error.max():.2e}, vs the static mesh {err.max():.4f}')
    gl('glDeleteProgram', None, U)(fine_prog)
    gl('glDeleteProgram', None, U)(tess_prog)
    return ok


def context(title, major=4, minor=3):
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, major)
    SDL.SDL_GL_SetAttribute(18, minor)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(title, 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    ctx = SDL.SDL_GL_CreateContext(window)
    assert ctx, SDL.SDL_GetError()
    print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
    return window, ctx


def main():
    window, ctx = context(b'Water tessellation test')
    ok = True
    try:
        ok = check_permutations() and ok
        # grazing view along -y from above the edge of the lake; the pillar of the scene is out of view
        rig = TessRig(T.Camera((1500.0, 0.0, 60.0), (1500.0, -3000.0, 0.0)))
        ok = check_cracks(rig) and ok
        ok = check_forced_level(TessRig(T.Camera((1500.0, 0.0, 900.0), (1500.0, -1500.0, 0.0)))) and ok
        ok = check_weights(rig) and ok
        ok = check_cull_and_skirt(rig) and ok
        ok = check_range(rig) and ok
        top = TessRig(T.Camera((200.0, -1500.0, 700.0), (201.0, -1500.0, 0.0)))
        ok = check_shore(top) and ok
        err = gl('glGetError', U)()
        ok = check('no GL error', err == 0, hex(err)) and ok
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


def bench():
    """GL_TIME_ELAPSED of the water pass at 1920x1080: a 6000 x 6000 lake as the static level-6-like mesh (47-unit
    cells, as t3_hevil body 9) against its 16x coarser tessellated base, grazing and top-down views"""
    window, ctx = context(b'Water tessellation bench')
    try:
        size = (1920, 1080)
        lo, hi = (-3000.0, -3000.0), (3000.0, 3000.0)
        static_cell, base_cell = 6000.0 / 128, 6000.0 / 8
        static_mesh = grid(lo[0], lo[1], hi[0], hi[1], static_cell)
        base_mesh = grid(lo[0], lo[1], hi[0], hi[1], base_cell)
        static = T.water_program()
        tess = tess_program()
        q = U()
        gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(q))
        for label, eye, target in (('grazing', (0.0, 2800.0, 60.0), (0.0, -3000.0, 0.0)),
                                   ('top-down', (0.0, 0.0, 1500.0), (10.0, 0.0, 0.0))):
            rig = TessRig(T.Camera(eye, target, size=size), size=size)
            for name, prog, mesh, patches, cell in (('static', static, static_mesh, False, static_cell),
                                                    ('tessellated', tess, base_mesh, True, base_cell)):
                u = wave_params(rig, max_edge(cell))
                best, primitives = 1e9, 0
                for _ in range(8):
                    rig.set_mesh(mesh, patches)
                    gl('glBeginQuery', None, U, U)(0x88BF, q)
                    _, primitives = draw(rig, prog, u, mesh, patches, query=True)
                    gl('glEndQuery', None, U)(0x88BF)
                    value = C.c_uint64()
                    gl('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))(q, 0x8866, C.byref(value))
                    best = min(best, value.value / 1e6)
                print(f'{label:9s} {name:12s} {len(mesh) // 3:7d} input triangles, {primitives:8d} drawn, '
                      f'{best:7.3f} ms (incl. the scene copy blit)')
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    sys.exit(bench() if '--bench' in sys.argv else main())
