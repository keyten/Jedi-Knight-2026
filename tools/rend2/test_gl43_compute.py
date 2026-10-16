"""GPU checks of the GL 4.3 kernels added by docs/rend2-gl43-review.md (hidden SDL GL 4.3 context).

Run from any directory: python tools/rend2/test_gl43_compute.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)

fplus_cluster.glsl: Forward+ cluster lists against a port of the CPU binning of tr_forwardplus.cpp: every GPU
list is the CPU list (same order) minus lights whose sphere misses the cluster, it keeps every light that reaches
a point of the cluster (dense samples of the real cluster frustum), the cap and the R16 packing are respected.

The FROXEL_LIGHT_LISTS permutation is checked against the volumetric CPU layout at 64/128 slices and tile sizes
4/8/16, including the jitter-expanded AABB, camera-plane spheres, the 32-light cap and exact last-slice lists.

water_intersection.glsl: depth contact sources against a port of RB_WaterIntersectionInject on a synthetic scene
(floor under shallow water, a pillar through the surface, sky) and the per-body maximum in the hit buffer.
"""
import ctypes as C
import math
from pathlib import Path
import random
import struct

import numpy as np

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

GL_SHADER_STORAGE_BUFFER = 0x90D2
GL_TEXTURE_2D = 0x0DE1


def gl(name, result, *args):
    address = SDL.SDL_GL_GetProcAddress(name.encode())
    assert address, name
    return C.WINFUNCTYPE(result, *args)(address)


def compute_program(name, defines=''):
    text = (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text()
    assert text.startswith('/*[Compute]*/'), name
    source = '#version 430 core\n' + defines + text.split('/*[Compute]*/')[1]
    shader = gl('glCreateShader', U, U)(0x91B9)
    src = C.c_char_p(source.encode())
    gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(src), None)
    gl('glCompileShader', None, U)(shader)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
    gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
    assert ok.value, (name, log.value.decode())
    prog = gl('glCreateProgram', U)()
    gl('glAttachShader', None, U, U)(prog, shader)
    gl('glLinkProgram', None, U)(prog)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(prog, len(log), None, log)
    assert ok.value, (name, log.value.decode())
    return prog


def uniform_vec4s(prog, name, values):
    flat = [float(v) for vec in values for v in vec]
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
    assert loc >= 0, name
    gl('glUniform4fv', None, I, I, P)(loc, len(values), (F * len(flat))(*flat))


def ssbo(data, binding):
    buffer = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(buffer))
    gl('glBindBuffer', None, U, U)(GL_SHADER_STORAGE_BUFFER, buffer)
    raw = C.create_string_buffer(bytes(data), len(data))
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(GL_SHADER_STORAGE_BUFFER, len(data), raw, 0x88E8)
    gl('glBindBufferBase', None, U, U, U)(GL_SHADER_STORAGE_BUFFER, binding, buffer)
    return buffer


def read_buffer(buffer, size):
    gl('glMemoryBarrier', None, U)(0x200 | 0x2000)  # buffer update, shader storage
    gl('glBindBuffer', None, U, U)(GL_SHADER_STORAGE_BUFFER, buffer)
    out = C.create_string_buffer(size)
    gl('glGetBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(GL_SHADER_STORAGE_BUFFER, 0, size, out)
    return out.raw


def perspective(fov_x, aspect, near, far, jitter=(0.0, 0.0)):
    p = [0.0] * 16
    p[0] = 1.0 / math.tan(math.radians(fov_x) * 0.5)
    p[5] = p[0] * aspect
    p[8], p[9] = jitter
    p[10] = -(far + near) / (far - near)
    p[11] = -1.0
    p[14] = -2.0 * far * near / (far - near)
    return p


# ---------------------------------------------------------------- Forward+

def fplus_slice(depth, slices, near_slice, scale, bias):
    if slices <= 1 or depth <= near_slice:
        return 0
    s = 1 + math.floor(math.log(depth) * scale + bias)
    return max(1, min(slices - 1, s))


def fplus_range(eye, radius, p, view, slicing, tile, tiles_x, tiles_y):
    """R_ForwardPlusLightRange for one sphere (eye space)."""
    slices, near_slice, scale, bias = slicing
    near_depth, far_depth = -eye[2] - radius, -eye[2] + radius
    if far_depth <= view['znear'] or near_depth >= view['zfar']:
        return None
    r = dict(z0=fplus_slice(max(near_depth, 0.0), *slicing), z1=fplus_slice(far_depth, *slicing),
             x0=0, x1=tiles_x - 1, y0=0, y1=tiles_y - 1)
    if near_depth <= view['znear']:
        return r
    xs, ys = [], []
    for c in range(8):
        x = eye[0] + (radius if c & 1 else -radius)
        y = eye[1] + (radius if c & 2 else -radius)
        z = eye[2] + (radius if c & 4 else -radius)
        cx = p[0] * x + p[4] * y + p[8] * z + p[12]
        cy = p[1] * x + p[5] * y + p[9] * z + p[13]
        cw = p[3] * x + p[7] * y + p[11] * z + p[15]
        if cw <= 1e-4:
            return r
        xs.append(cx / cw)
        ys.append(cy / cw)
    if max(xs) < -1 or min(xs) > 1 or max(ys) < -1 or min(ys) > 1:
        return None
    clamp = lambda lo, hi, v: max(lo, min(hi, v))
    w, h = view['width'], view['height']
    r['x0'] = clamp(0, tiles_x - 1, math.floor((min(xs) * 0.5 + 0.5) * w / tile))
    r['x1'] = clamp(0, tiles_x - 1, math.floor((max(xs) * 0.5 + 0.5) * w / tile))
    r['y0'] = clamp(0, tiles_y - 1, math.floor((min(ys) * 0.5 + 0.5) * h / tile))
    r['y1'] = clamp(0, tiles_y - 1, math.floor((max(ys) * 0.5 + 0.5) * h / tile))
    return r


def fplus_case(prog, seed, cap, num_lights=96, jitter=(0.0, 0.0), area_every=0):
    rng = random.Random(seed)
    view = dict(width=1280, height=720, znear=4.0, zfar=8192.0)
    tile, slices, near_slice = 64, 16, 48.0
    tiles_x, tiles_y = (view['width'] + tile - 1) // tile, (view['height'] + tile - 1) // tile
    far_z = max(view['zfar'], near_slice * 2)
    scale = (slices - 1) / math.log(far_z / near_slice)
    bias = -math.log(near_slice) * scale
    slicing = (slices, near_slice, scale, bias)
    p = perspective(90.0, view['width'] / view['height'], view['znear'], view['zfar'], jitter)
    num_clusters = tiles_x * tiles_y * slices

    lights, ranges = [], []
    for k in range(num_lights):
        depth = math.exp(rng.uniform(math.log(8.0), math.log(3000.0)))
        eye = [rng.uniform(-1.2, 1.2) * depth, rng.uniform(-0.8, 0.8) * depth, -depth]
        radius = rng.uniform(16.0, 400.0)
        r = fplus_range(eye, radius, p, view, slicing, tile, tiles_x, tiles_y)
        if r is None:
            continue
        # area lights of the CPU keep their box only (no sphere test)
        r.update(light=k, eye=eye, radius=radius, sphere=not (area_every and k % area_every == 0))
        ranges.append(r)
        lights.append((eye, radius))

    def in_box(r, x, y, z):
        return r['x0'] <= x <= r['x1'] and r['y0'] <= y <= r['y1'] and r['z0'] <= z <= r['z1']

    def slice_depths(z):
        if z == 0:
            return 0.0, near_slice
        d1 = far_z if z == slices - 1 else math.exp((z - bias) / scale)
        return math.exp((z - 1 - bias) / scale), d1

    # GPU
    records = bytearray()
    for r in ranges:
        records += struct.pack('4I4f', r['x0'] | (r['x1'] << 16), r['y0'] | (r['y1'] << 16),
                               r['z0'] | (r['z1'] << 16), r['light'] | ((1 if r['sphere'] else 0) << 16),
                               *r['eye'], r['radius'])
    grid_base, index_capacity = 37, 1 << 20
    gl('glUseProgram', None, U)(prog)
    uniform_vec4s(prog, 'u_FPlusCluster', [
        (tiles_x, tiles_y, slices, num_clusters), (tile, view['width'], view['height'], len(ranges)),
        (scale, bias, near_slice, far_z), (p[0], p[5], p[8], p[9]), (p[12], p[13], 1.0, cap),
        (grid_base, 0, index_capacity, 0), (0, 0, 0, 0), (0, 0, 0, 0)])
    grid_buf = ssbo(bytes((grid_base + num_clusters) * 8), 0)
    index_buf = ssbo(bytes(index_capacity * 2), 1)
    range_buf = ssbo(records or bytes(32), 2)
    counter_buf = ssbo(bytes(4), 3)
    gl('glDispatchCompute', None, U, U, U)((num_clusters + 63) // 64, 1, 1)
    grid = struct.unpack(f'{2 * (grid_base + num_clusters)}I', read_buffer(grid_buf, (grid_base + num_clusters) * 8))
    used = struct.unpack('I', read_buffer(counter_buf, 4))[0]
    indices = struct.unpack(f'{used}H', read_buffer(index_buf, used * 2)) if used else ()
    for b in (grid_buf, index_buf, range_buf, counter_buf):
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(b))

    checked = cpu_refs = gpu_refs = 0
    spans = set()
    t = np.linspace(0.0, 1.0, 5)
    for c in range(num_clusters):
        x, y, z = c % tiles_x, (c // tiles_x) % tiles_y, c // (tiles_x * tiles_y)
        offset, count = grid[2 * (grid_base + c)], grid[2 * (grid_base + c) + 1]
        assert offset % 2 == 0 and offset + count <= used, (c, offset, count, used)
        if count:
            assert offset not in spans, c
            spans.add(offset)
        gpu = list(indices[offset:offset + count])
        boxed = [r for r in ranges if in_box(r, x, y, z)]
        cpu_full = [r['light'] for r in boxed]
        # in order, a subsequence of the uncapped CPU walk, and at most cap
        it = iter(cpu_full)
        assert all(any(l == m for m in it) for l in gpu), (c, gpu, cpu_full)
        assert count <= cap
        # every light that really reaches the cluster (samples of its frustum chunk) is kept
        d0, d1 = slice_depths(z)
        d0, d1 = max(d0, view['znear']), min(d1, view['zfar'])
        sx = (x * tile + t * (min((x + 1) * tile, view['width']) - x * tile)) / view['width'] * 2 - 1
        sy = (y * tile + t * (min((y + 1) * tile, view['height']) - y * tile)) / view['height'] * 2 - 1
        sd = d0 + t * (d1 - d0)
        NX, NY, SD = np.meshgrid(sx, sy, sd, indexing='ij')
        samples = np.stack([(NX + p[8]) * SD / p[0], (NY + p[9]) * SD / p[5], -SD], axis=-1).reshape(-1, 3)
        reaching = [r['light'] for r in boxed if not r['sphere'] or
                    np.min(np.sum((samples - np.array(r['eye'])) ** 2, axis=1)) <= r['radius'] ** 2]
        # the GPU matches hold every reaching light; uncapped lists therefore contain them all
        assert count >= min(cap, len(reaching)), (c, count, reaching)
        if count < cap:
            missing = [l for l in reaching if l not in gpu]
            assert not missing, (c, missing, gpu)
        checked += 1
        cpu_refs += min(cap, len(cpu_full))
        gpu_refs += count
    return checked, len(ranges), cpu_refs, gpu_refs, used


# ---------------------------------------------------------------- froxel fog light lists

def froxel_slice(depth, near, far, slices):
    first = near * (far / near) ** (1 / slices)
    if depth <= first:
        return 0
    return max(0, min(slices - 1, math.floor(slices * math.log(depth / near) / math.log(far / near))))


def froxel_range(eye, radius, p, width, height, near, far, slices, tile):
    depth = -eye[2]
    if depth + radius <= 0 or depth - radius >= far:
        return None
    tiles_x, tiles_y = (width + tile - 1) // tile, (height + tile - 1) // tile
    r = dict(z0=froxel_slice(depth - radius, near, far, slices),
             z1=froxel_slice(depth + radius, near, far, slices),
             x0=0, x1=tiles_x - 1, y0=0, y1=tiles_y - 1)
    if depth - radius <= 1:
        return r
    xs, ys = [], []
    for c in range(8):
        x = eye[0] + (radius if c & 1 else -radius)
        y = eye[1] + (radius if c & 2 else -radius)
        z = eye[2] + (radius if c & 4 else -radius)
        cw = -z
        if cw <= 1e-4:
            return r
        xs.append(p[0] * x / cw)
        ys.append(p[5] * y / cw)
    if max(xs) < -1 or min(xs) > 1 or max(ys) < -1 or min(ys) > 1:
        return None
    clamp = lambda lo, hi, v: max(lo, min(hi, v))
    r['x0'] = clamp(0, tiles_x - 1, math.floor((min(xs) * .5 + .5) * width / tile))
    r['x1'] = clamp(0, tiles_x - 1, math.floor((max(xs) * .5 + .5) * width / tile))
    r['y0'] = clamp(0, tiles_y - 1, math.floor((min(ys) * .5 + .5) * height / tile))
    r['y1'] = clamp(0, tiles_y - 1, math.floor((max(ys) * .5 + .5) * height / tile))
    return r


def froxel_case(prog, slices, tile, seed):
    rng = random.Random(seed)
    width, height, near, far = 96, 54, 8.0, 4096.0
    tiles_x, tiles_y = (width + tile - 1) // tile, (height + tile - 1) // tile
    num_clusters = tiles_x * tiles_y * slices
    p = perspective(90, width / height, near, far)
    lights = [([0.0, 0.0, -0.5], 24.0), ([0.0, 0.0, -1200.0], 900.0),
              ([80.0, -20.0, -180.0], 18.0), ([-140.0, 50.0, -350.0], 45.0)]
    for _ in range(20):
        depth = math.exp(rng.uniform(math.log(4), math.log(3600)))
        lights.append(([rng.uniform(-1.1, 1.1) * depth, rng.uniform(-.7, .7) * depth, -depth],
                       rng.choice([rng.uniform(2, 24), rng.uniform(100, 700)])))
    ranges = []
    for k, (eye, radius) in enumerate(lights):
        r = froxel_range(eye, radius, p, width, height, near, far, slices, tile)
        if r is not None:
            r.update(light=k, eye=eye, radius=radius)
            ranges.append(r)

    records = b''.join(struct.pack('4I4f', r['x0'] | r['x1'] << 16, r['y0'] | r['y1'] << 16,
                                   r['z0'] | r['z1'] << 16, r['light'] | 1 << 16,
                                   *r['eye'], r['radius']) for r in ranges)
    capacity = num_clusters * 32
    gl('glUseProgram', None, U)(prog)
    uniform_vec4s(prog, 'u_FPlusCluster', [
        (tiles_x, tiles_y, slices, num_clusters), (tile, width, height, len(ranges)),
        (0, 0, near, far), (p[0], p[5], 0, 0), (0, 0, 1, 32),
        (0, 0, capacity, 0), (0, 0, 0, 0), (0, 0, 0, 0)])
    list_buf = ssbo(bytes((num_clusters + capacity) * 4), 0)
    range_buf = ssbo(records or bytes(32), 2)
    counter_buf = ssbo(bytes(4), 3)
    gl('glDispatchCompute', None, U, U, U)((num_clusters + 63) // 64, 1, 1)
    raw = struct.unpack(f'{num_clusters + capacity}I', read_buffer(list_buf, (num_clusters + capacity) * 4))
    used = struct.unpack('I', read_buffer(counter_buf, 4))[0]
    for b in (list_buf, range_buf, counter_buf):
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(b))

    cpu_refs = gpu_refs = 0
    for c in range(num_clusters):
        x, y, z = c % tiles_x, (c // tiles_x) % tiles_y, c // (tiles_x * tiles_y)
        boxed = [r for r in ranges if r['x0'] <= x <= r['x1'] and r['y0'] <= y <= r['y1'] and r['z0'] <= z <= r['z1']]
        cpu = [r['light'] for r in boxed][:32]
        header = raw[c]
        first, count = header & 0xffffff, header >> 24
        assert count <= 32 and (count == 0 or num_clusters <= first < num_clusters + used)
        gpu = list(raw[first:first + count]) if count else []
        it = iter([r['light'] for r in boxed])
        assert all(any(light == candidate for candidate in it) for light in gpu), (c, gpu, cpu)
        if z == slices - 1:
            assert gpu == cpu, (c, gpu, cpu)
        elif count < 32 and boxed:
            # Dense samples across the jitter-expanded cluster frustum. Any
            # sphere reaching one must survive the GPU AABB test.
            k0, k1 = z - .5, z + 1.5
            d0 = 0 if k0 <= 0 else near * (far / near) ** (k0 / slices)
            d1 = near * (far / near) ** (k1 / slices)
            sx = np.linspace(x * tile - .5, min((x + 1) * tile, width) + .5, 3) / width * 2 - 1
            sy = np.linspace(y * tile - .5, min((y + 1) * tile, height) + .5, 3) / height * 2 - 1
            sd = np.linspace(d0, d1, 3)
            NX, NY, SD = np.meshgrid(sx, sy, sd, indexing='ij')
            samples = np.stack([NX * SD / p[0], NY * SD / p[5], -SD], axis=-1).reshape(-1, 3)
            reaching = [r['light'] for r in boxed if np.min(np.sum((samples - np.array(r['eye'])) ** 2, axis=1)) <= r['radius'] ** 2]
            assert all(light in gpu for light in reaching), (c, reaching, gpu)
        cpu_refs += len(cpu)
        gpu_refs += count
    return num_clusters, len(ranges), cpu_refs, gpu_refs


# ---------------------------------------------------------------- water contacts

def water_case(prog):
    W, H = 320, 240
    origin = np.array([20.0, -260.0, 110.0])
    target = np.array([0.0, 0.0, -10.0])
    forward = target - origin
    forward /= np.linalg.norm(forward)
    left = np.cross([0.0, 0.0, 1.0], forward)
    left /= np.linalg.norm(left)
    up = np.cross(forward, left)
    znear, zfar = 4.0, 4096.0
    p = perspective(80.0, W / H, znear, zfar)
    pm = np.array(p, dtype=np.float64).reshape(4, 4).T
    mv = np.identity(4)
    mv[0, :3], mv[1, :3], mv[2, :3] = -left, up, -forward
    mv[:3, 3] = -mv[:3, :3] @ origin
    vp = pm @ mv

    # scene: floor z = -24 (under the water), a pillar through the surface, nothing behind (sky)
    def trace(direction):
        best = math.inf
        if direction[2] < 0:
            t = (-24.0 - origin[2]) / direction[2]
            q = origin + t * direction
            if abs(q[0]) < 400 and abs(q[1]) < 400:
                best = t
        lo, hi = np.array([-30.0, -10.0, -24.0]), np.array([30.0, 50.0, 140.0])
        with np.errstate(divide='ignore', invalid='ignore'):
            t0, t1 = (lo - origin) / direction, (hi - origin) / direction
        tmin, tmax = np.max(np.minimum(t0, t1)), np.min(np.maximum(t0, t1))
        if tmax >= max(tmin, 0.0):
            best = min(best, tmin)
        return best

    depth = np.ones((H, W), dtype=np.float32)
    for y in range(H):
        for x in range(W):
            ndc = np.array([(x + 0.5) / W * 2 - 1, (y + 0.5) / H * 2 - 1])
            ray = forward + (-left) * (ndc[0] / p[0]) + up * (ndc[1] / p[5])
            t = trace(ray / np.linalg.norm(ray))
            if math.isfinite(t):
                q = origin + t * ray / np.linalg.norm(ray)
                clip = vp @ np.append(q, 1.0)
                depth[y, x] = clip[2] / clip[3] * 0.5 + 0.5

    sim_w = sim_h = 48
    bmin, texel = np.array([-192.0, -192.0]), 8.0
    wet = np.zeros((sim_h, sim_w), dtype=bool)
    for gy in range(sim_h):
        for gx in range(sim_w):
            px, py = bmin + (np.array([gx, gy]) + 0.5) * texel
            wet[gy, gx] = px * px + py * py < 170 ** 2
    band, strength = 8.0, 1.0
    n, plane_dist = np.array([0.0, 0.0, 1.0]), 0.0

    # CPU port (RB_WaterIntersectionInject), float32 depth values as uploaded
    def world(x, y, d):
        u, v = (x + 0.5) / W, (y + 0.5) / H
        ndcx, ndcy = u * 2 - 1, v * 2 - 1
        z = p[14] / (d * 2.0 - 1.0 + p[10])
        side = (ndcx + p[8]) * z / p[0]
        upv = (ndcy + p[9]) * z / p[5]
        return origin - side * left + upv * up + z * forward

    expected = np.zeros((sim_h, sim_w))
    for gy in range(sim_h):
        for gx in range(sim_w):
            if not wet[gy, gx]:
                continue
            wp = np.array([*(bmin + (np.array([gx, gy]) + 0.5) * texel), 0.0])
            clip = vp @ np.append(wp, 1.0)
            if clip[3] <= 0.001:
                continue
            sx = math.floor((clip[0] / clip[3] * 0.5 + 0.5) * W)
            sy = math.floor((clip[1] / clip[3] * 0.5 + 0.5) * H)
            if sx <= 0 or sy <= 0 or sx >= W - 1 or sy >= H - 1:
                continue
            dc = float(depth[sy, sx])
            if dc >= 0.999999:
                continue
            scene = world(sx, sy, dc)
            sd = scene @ n - plane_dist
            if abs(sd) >= band:
                continue
            ds = [float(depth[sy, sx - 1]), float(depth[sy, sx + 1]), float(depth[sy - 1, sx]), float(depth[sy + 1, sx])]
            if max(ds) >= 0.999999:
                continue
            dx = world(sx + 1, sy, ds[1]) - world(sx - 1, sy, ds[0])
            dy = world(sx, sy + 1, ds[3]) - world(sx, sy - 1, ds[2])
            normal = np.cross(dx, dy)
            length = np.linalg.norm(normal)
            if length < 1e-4:
                continue
            normal /= length
            vertical = 1 - abs(normal @ n)
            normal_conf = min(max((vertical - 0.15) / 0.55, 0), 1)
            depth_conf = 1 - min(max(max(np.linalg.norm(dx), np.linalg.norm(dy)) / (band * 8), 0), 1)
            band_conf = 1 - min(max(abs(sd) / band, 0), 1)
            source = band_conf * normal_conf * depth_conf * strength
            if source > 0.01:
                expected[gy, gx] = min(source, 1.0)

    # GPU
    def tex2d(internal, fmt, kind, w, h, data):
        tex = U()
        gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
        gl('glBindTexture', None, U, U)(GL_TEXTURE_2D, tex)
        gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, kind,
                                                           data.ctypes.data if data is not None else None)
        gl('glTexParameteri', None, U, U, I)(GL_TEXTURE_2D, 0x2801, 0x2600)
        gl('glTexParameteri', None, U, U, I)(GL_TEXTURE_2D, 0x2800, 0x2600)
        return tex

    depth_tex = tex2d(0x8CAC, 0x1902, 0x1406, W, H, np.ascontiguousarray(depth))
    state = np.zeros((sim_h, sim_w, 4), dtype=np.float32)
    state[..., 3] = np.where(wet, 0.0, -1.0)
    state_tex = tex2d(0x881A, 0x1908, 0x1406, sim_w, sim_h, state)
    out_tex = tex2d(0x822D, 0x1903, 0x1406, sim_w, sim_h, np.full((sim_h, sim_w), 7.0, dtype=np.float32))
    gl('glUseProgram', None, U)(prog)
    for unit, (tex, name) in enumerate([(state_tex, 'u_WaterInteractionState'), (depth_tex, 'u_WaterDepthMap')]):
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(GL_TEXTURE_2D, tex)
        gl('glUniform1i', None, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode()), unit)
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_ModelViewProjectionMatrix')
    gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0, (F * 16)(*vp.T.reshape(-1)))
    slot = 3
    uniform_vec4s(prog, 'u_WaterIntersection', [
        (*bmin, texel, texel), (*n, plane_dist), (0, 0, W, H), (0, 0, W, H), (W, H, band, strength),
        (p[0], p[5], p[8], p[9]), (p[10], p[14], slot, 0), (*origin, 0), (*forward, 0), (*left, 0), (*up, 0)])
    hits = ssbo(bytes(32 * 4), 0)
    gl('glBindImageTexture', None, U, U, I, U, I, U, U)(0, out_tex, 0, 0, 0, 0x88B9, 0x822D)
    gl('glDispatchCompute', None, U, U, U)((sim_w + 7) // 8, (sim_h + 7) // 8, 1)
    gl('glMemoryBarrier', None, U)(0x100 | 0x200)
    out = np.zeros((sim_h, sim_w), dtype=np.float32)
    gl('glBindTexture', None, U, U)(GL_TEXTURE_2D, out_tex)
    gl('glGetTexImage', None, U, I, U, U, P)(GL_TEXTURE_2D, 0, 0x1903, 0x1406, out.ctypes.data)
    hit_values = struct.unpack('32f', read_buffer(hits, 32 * 4))

    assert np.all(np.isfinite(out)) and np.all(out != 7.0), 'cells not written'
    assert np.all(out[~wet] == 0.0), 'dry cells got a source'
    diff = np.abs(out - expected)
    # R16F output; float32 vs float64 reconstruction only matters at the 0.01 cut
    strict = diff[(expected > 0.02) | (out > 0.02)]
    assert strict.size == 0 or strict.max() < 3e-3, strict.max()
    contacts = int(np.count_nonzero(expected))
    assert contacts >= 8, f'scene produced only {contacts} contact cells'
    # the hit buffer holds the float source, the image its R16F rounding
    assert abs(hit_values[slot] - out.max()) < 1e-3 and abs(hit_values[slot] - expected.max()) < 3e-3, \
        (hit_values[slot], out.max(), expected.max())
    assert all(v == 0.0 for i, v in enumerate(hit_values) if i != slot)
    for tex in (depth_tex, state_tex, out_tex):
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    return contacts, float(expected.max())


# ---------------------------------------------------------------- consumers (GLSL 150 raster programs)

def raster_program(name):
    text = (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text()
    vertex = text.split('/*[Vertex]*/')[1].split('/*[Fragment]*/')[0]
    fragment = text.split('/*[Fragment]*/')[1]
    prog = gl('glCreateProgram', U)()
    for kind, body in [(0x8B31, vertex), (0x8B30, fragment)]:
        shader = gl('glCreateShader', U, U)(kind)
        src = C.c_char_p(('#version 150 core\n' + body).encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(src), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(16384)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert ok.value, (name, log.value.decode())
        gl('glAttachShader', None, U, U)(prog, shader)
    gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, 0, b'out_Color')
    gl('glLinkProgram', None, U)(prog)
    ok = I()
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    assert ok.value, name
    return prog


def float_texture(w, h, rgba):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(GL_TEXTURE_2D, tex)
    data = np.ascontiguousarray(rgba, dtype=np.float32)
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(GL_TEXTURE_2D, 0, 0x881A, w, h, 0, 0x1908, 0x1406, data.ctypes.data)
    for parameter in (0x2801, 0x2800):
        gl('glTexParameteri', None, U, U, I)(GL_TEXTURE_2D, parameter, 0x2600)
    return tex


def render(prog, w, h, samplers, vec4s):
    target = float_texture(w, h, np.zeros((h, w, 4)))
    fbo, vao = U(), U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glBindFramebuffer', None, U, U)(0x8D40, fbo)
    gl('glFramebufferTexture2D', None, U, U, U, U, I)(0x8D40, 0x8CE0, GL_TEXTURE_2D, target, 0)
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    gl('glUseProgram', None, U)(prog)
    for unit, (name, tex) in enumerate(samplers.items()):
        gl('glActiveTexture', None, U)(0x84C0 + unit)
        gl('glBindTexture', None, U, U)(GL_TEXTURE_2D, tex)
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    for name, value in vec4s.items():
        loc = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())
        if loc >= 0:
            gl('glUniform4f', None, I, F, F, F, F)(loc, *value)
    gl('glDrawArrays', None, U, I, I)(0x0004, 0, 3)
    out = np.zeros((h, w, 4), dtype=np.float32)
    gl('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, w, h, 0x1908, 0x1406, out.ctypes.data)
    gl('glBindFramebuffer', None, U, U)(0x8D40, 0)
    gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glDeleteVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(target))
    return out


def consumers_case():
    n = 16
    contact = np.zeros((n, n, 4))
    contact[5, 9, 0] = 0.75
    contact_tex = float_texture(n, n, contact)
    garbage_tex = float_texture(n, n, np.full((n, n, 4), 0.9))
    zero_tex = float_texture(n, n, np.zeros((n, n, 4)))

    # interaction step: the contact map joins the source alpha only with u_WaterBathymetryParams.w
    step = raster_program('water_interaction_field')
    common = {'u_WaterInteractionSim0': (1 / 60, 64, 0.3, 0), 'u_WaterInteractionSim1': (8, 8, 0, 0),
              'u_WaterInteractionDomain': (0, 0, 1.5, 1), 'u_WaterFlowFieldParams': (0, 0, 0, 0)}
    results = {}
    for flag, tex in [(1.0, contact_tex), (0.0, garbage_tex)]:
        out = render(step, n, n, {'u_WaterInteractionState': zero_tex, 'u_WaterInteractionSource': zero_tex,
                                  'u_WaterBathymetryMap': zero_tex, 'u_WaterFlowField': zero_tex,
                                  'u_WaterIntersectionMap': tex},
                     dict(common, u_WaterBathymetryParams=(0, 8, 800, flag)))
        results[flag] = out[..., 3]
    persistence = math.exp(-(1 / 60) / 1.5)
    assert results[1.0][5, 9] > 0.75 * persistence * 0.6, results[1.0][5, 9]
    assert np.all(results[0.0] == 0.0), 'contact map read without its flag'
    gl('glDeleteProgram', None, U)(step)

    # foam update: green source from the nearest interaction cell, scaled, only with the scale set
    foam = raster_program('water_foam_field')
    m = 32
    foam_state = np.zeros((m, m, 4))
    foam_state[..., 1] = 1.0
    foam_tex = float_texture(m, m, foam_state)
    small = np.zeros((m, m, 4))
    small_tex = float_texture(m, m, small)
    vec4s = {'u_WaterFoamParams': (0.1, 0, 0, 0), 'u_WaterFoamFlow': (0, 0, 0, 0),
             'u_WaterWhitewaterParams': (0.9, 0, 0, 0), 'u_WaterFoamSources': (0, 0, 0, 0),
             'u_WaterFoamInteractionRect': (0, 0, 0, 0), 'u_WaterFlowFieldParams': (0, 0, 0, 0)}
    outs = {}
    for scale, tex in [(2.0, contact_tex), (0.0, garbage_tex)]:
        outs[scale] = render(foam, m, m, {'u_WaterFoamMap': foam_tex, 'u_WaterFoamSourceMap': small_tex,
                                          'u_WaterFoamInteraction': zero_tex, 'u_WaterFlowField': zero_tex,
                                          'u_WaterFoamIntersectionMap': tex},
                             dict(vec4s, u_WaterFoamIntersectionParams=(scale, 0, 0, 0)))[..., 0]
    # interaction cell (9, 5) covers foam cells x 18-19, y 10-11 (x * 16 / 32)
    source = math.floor(255 * min(0.75 * 2.0, 1.0)) / 255
    expected = 0.1 * source * 0.72  # rate * dt, breakup >= 0.72
    for y in range(m):
        for x in range(m):
            inside = (x // 2, y // 2) == (9, 5)
            if inside:
                assert outs[2.0][y, x] >= expected * 0.99, (x, y, outs[2.0][y, x])
            else:
                assert outs[2.0][y, x] == 0.0, (x, y, outs[2.0][y, x])
    assert np.all(outs[0.0] == 0.0), 'foam read the contact map at scale 0'
    gl('glDeleteProgram', None, U)(foam)
    for tex in (contact_tex, garbage_tex, zero_tex, foam_tex, small_tex):
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(tex))


def main():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 4)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    window = SDL.SDL_CreateWindow(b'GL 4.3 kernels test', 0, 0, 32, 32, 10)
    assert window, SDL.SDL_GetError()
    context = SDL.SDL_GL_CreateContext(window)
    assert context, SDL.SDL_GetError()
    try:
        print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
        fplus = compute_program('fplus_cluster')
        for seed, cap, jitter, area in [(1, 255, (0.0, 0.0), 0), (2, 255, (0.0011, -0.0007), 5), (3, 4, (0.0, 0.0), 0),
                                        (4, 255, (0.0, 0.0), 3)]:
            checked, lights, cpu_refs, gpu_refs, slots = fplus_case(fplus, seed, cap, jitter=jitter, area_every=area)
            print(f'fplus_cluster seed {seed} cap {cap}: {checked} clusters, {lights} lights in view, '
                  f'light refs CPU {cpu_refs} -> GPU {gpu_refs} ({slots} index slots)')
        froxel = compute_program('fplus_cluster', '#define FROXEL_LIGHT_LISTS\n')
        for slices, tile, seed in [(64, 4, 11), (64, 8, 12), (128, 16, 13)]:
            checked, lights, cpu_refs, gpu_refs = froxel_case(froxel, slices, tile, seed)
            print(f'froxel_cluster {slices} slices tile {tile}: {checked} clusters, {lights} lights in view, '
                  f'light refs CPU {cpu_refs} -> GPU {gpu_refs}')
        water = compute_program('water_intersection')
        contacts, peak = water_case(water)
        print(f'water_intersection: {contacts} contact cells, peak source {peak:.3f}, hit buffer matches')
        consumers_case()
        print('water_interaction_field / water_foam_field: contact map consumed only when flagged')
        print('PASS')
    finally:
        SDL.SDL_GL_DeleteContext(context)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()


if __name__ == '__main__':
    main()
