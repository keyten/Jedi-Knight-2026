"""GPU checks of the vegetation deformation on a hidden SDL GL core context.

Run from any directory: python tools/rend2/test_foliage_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)

- compiles and links the programs that carry the foliage vertex libraries as rend2 builds them: lightall,
  generic, fogpass, velocity (leaf_flutter + plant_skin, also skeletal / vertex animation permutations),
  every valid surface_sprites permutation (foliage_interact), the bend field update and debug passes
- plant skin (glsl/plant_skin.glsl): a frond chain of a stock fern (yavin/plant_b.md3 when the assets are
  installed, else a synthetic frond) is bent by a capsule with the XPBD chain of tr_plantsim.cpp (numpy
  mirror); bones as WriteBones, per vertex skin as R_BuildPlantSkeleton; the GPU skinned positions must match
  the float64 reference, the rest pose must be exact, and the mesh edges must keep their length (the old
  root bend is measured for comparison)
- grass blade (FoliageBladeBend): rest = 0, the curve keeps the blade length, the contact point moves by the
  field displacement, the wind tip by the wind displacement, the part above the contact stays straight
- bend field update (foliage_field.glsl): a capsule pushes the patches inside it exactly out (hard contact),
  nothing outside it moves (no force field), the grass springs back towards the trampled part after the
  character left, and the trampled part fades
- Force push / pull waves (r_foliageForceWaves): the field takes exactly the kick of the waves (python mirror
  of tr_foliageinteract.cpp), away from the caster for a push and towards it for a pull, nothing behind the
  cone or beyond the reach, and springs back; the leaves (leaf_flutter.glsl) swing from the arrival of the
  front only, also with the flutter off
"""
import ctypes as C
from pathlib import Path
import math
import struct
import sys
import zipfile

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
ASSETS = [ROOT.parent / 'OpenJK/build-rend2/base/assets1.pk3']
GL_VERTEX, GL_FRAGMENT = 0x8B31, 0x8B30
TEX2D, TEXBUFFER = 0x0DE1, 0x8C2A
RGBA, RG, FLOAT = 0x1908, 0x8227, 0x1406
RGBA32F, RGBA16F, RG16F = 0x8814, 0x881A, 0x822F
FB = 0x8D40
ARRAY_BUFFER, UNIFORM_BUFFER = 0x8892, 0x8A11
STATIC_DRAW = 0x88E4

# attribute locations of rend2 (ATTR_INDEX_*)
ATTRIBUTES = ['attr_Position', 'attr_TexCoord0', 'attr_TexCoord1', 'attr_TexCoord2', 'attr_TexCoord3',
              'attr_TexCoord4', 'attr_Tangent', 'attr_Normal', 'attr_Color', 'attr_LightDirection',
              'attr_BoneIndexes', 'attr_BoneWeights', 'attr_Position2', 'attr_Tangent2', 'attr_Normal2']
OUTPUTS = ('out_Color', 'out_Glow', 'out_SSRNormal', 'out_SSRSpecular', 'out_SSRCubemap', 'out_SSGIAlbedo',
           'out_SSGIRadiance', 'out_SkinDiffuse')
FROXEL = ['USE_FROXEL_FOG', 'MAX_GPU_FOG_VOLUMES=64', 'FROXEL_MAX_SLICES=128', 'FROXEL_LOCAL_POOL=2048',
          'FROXEL_EXTINCTION_PALETTE=16']


def read(name):
    return (GLSL / f'{name}.glsl').read_text().replace('\r\n', '\n')


def stage(name, which):
    vs, fs = read(name).split('/*[Fragment]*/') if '/*[Fragment]*/' in read(name) else (read(name), '')
    return vs.split('/*[Vertex]*/')[1] if which == 'vertex' else fs


def header(defines):
    """GLSL_GetShaderHeader, the parts these programs use"""
    text = ('#version 150 core\n#define M_PI 3.14159265358979323846\n'
            '#define DEFORM_NONE 0\n#define DEFORM_WAVE 1\n#define DEFORM_NORMALS 2\n#define DEFORM_BULGE 3\n'
            '#define DEFORM_BULGE_UNIFORM 4\n#define DEFORM_MOVE 5\n#define DEFORM_PROJECTION_SHADOW 6\n'
            '#define DEFORM_DISINTEGRATION 7\n#define WF_NONE 0\n#define WF_SIN 1\n#define WF_SQUARE 2\n'
            '#define WF_TRIANGLE 3\n#define WF_SAWTOOTH 4\n#define WF_INVERSE_SAWTOOTH 5\n'
            '#define TCGEN_LIGHTMAP 2\n#define TCGEN_LIGHTMAP1 3\n#define TCGEN_LIGHTMAP2 4\n#define TCGEN_LIGHTMAP3 5\n'
            '#define TCGEN_TEXTURE 6\n#define TCGEN_ENVIRONMENT_MAPPED 7\n#define TCGEN_ENVIRONMENT_MAPPED_SP 8\n'
            '#define TCGEN_ENVIRONMENT_MAPPED_SP_FP 9\n#define TCGEN_FOG 10\n#define TCGEN_VECTOR 11\n'
            '#define CGEN_LIGHTING_DIFFUSE 12\n#define CGEN_DISINTEGRATION_1 16\n#define CGEN_DISINTEGRATION_2 17\n'
            '#define AGEN_LIGHTING_SPECULAR 6\n#define AGEN_LIGHTING_SPECULAR_STATIC 7\n#define AGEN_PORTAL 9\n'
            '#define ALPHA_TEST_GT0 1\n#define ALPHA_TEST_LT128 2\n#define ALPHA_TEST_GE128 3\n'
            '#define ALPHA_TEST_GE192 4\n#define ALPHA_TEST_E255 5\n'
            '#define MAX_G2_BONES 72\n#define MAX_GPU_FOGS 16\n#define MAX_DLIGHTS 32\n#define DSHADOW_MAP_SIZE 512\n'
            '#define r_FBufScale vec2(320.0, 240.0)\n#define USE_ALPHA_TEST\n')
    for d in defines:
        text += '#define ' + d.replace('=', ' ', 1) + '\n'
    return text


def compile_program(sources, label, outputs=OUTPUTS):
    prog = gl('glCreateProgram', U)()
    for kind, text in sources:
        shader = gl('glCreateShader', U, U)(kind)
        source = C.c_char_p(text.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
        gl('glCompileShader', None, U)(shader)
        ok, log = I(), C.create_string_buffer(65536)
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        if not ok.value:
            lines = text.split('\n')
            raise AssertionError((label, hex(kind), log.value.decode(errors='replace')[:3000]))
        gl('glAttachShader', None, U, U)(prog, shader)
        gl('glDeleteShader', None, U)(shader)
    for index, name in enumerate(ATTRIBUTES):
        gl('glBindAttribLocation', None, U, U, C.c_char_p)(prog, index, name.encode())
    for index, output in enumerate(outputs):
        gl('glBindFragDataLocation', None, U, U, C.c_char_p)(prog, index, output.encode())
    gl('glLinkProgram', None, U)(prog)
    ok, log = I(), C.create_string_buffer(65536)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    gl('glGetProgramInfoLog', None, U, I, P, P)(prog, len(log), None, log)
    assert ok.value, (label, log.value.decode(errors='replace')[:3000])
    return prog


def foliage_vertex_library():
    return stage('leaf_flutter', 'vertex') + '\n' + stage('plant_skin', 'vertex') + '\n'


def build(name, defines, vertex_library, fragment_library='', froxel=False):
    h = header(defines + (FROXEL if froxel else []))
    vs = h + vertex_library + stage(name, 'vertex')
    fs = h + fragment_library + stage(name, 'fragment')
    return compile_program([(GL_VERTEX, vs), (GL_FRAGMENT, fs)], f'{name} {defines}')


def check_permutations():
    count = 0
    lib = foliage_vertex_library()
    volumetric = stage('volumetric_common', 'fragment') + '\n'
    pbr = stage('pbr_common', 'fragment') + '\n'
    sets = {
        'lightall': [[], ['USE_LIGHT', 'USE_LIGHT_VECTOR'], ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_SKELETAL_ANIMATION'],
                     ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_VERTEX_ANIMATION'], ['USE_LIGHT', 'USE_LIGHTMAP'],
                     ['USE_DEFORM_VERTEXES', 'USE_TCGEN', 'USE_TCMOD'], ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_FAST_LIGHT']],
        'generic': [[], ['USE_SKELETAL_ANIMATION'], ['USE_VERTEX_ANIMATION'], ['USE_RGBAGEN'],
                    ['USE_DEFORM_VERTEXES', 'USE_TCGEN', 'USE_TCMOD'], ['USE_FOG']],
        'fogpass': [[], ['USE_SKELETAL_ANIMATION'], ['USE_VERTEX_ANIMATION'], ['USE_DEFORM_VERTEXES'],
                    ['USE_FALLBACK_GLOBAL_FOG']],
        'velocity': [[], ['USE_SKELETAL_ANIMATION'], ['USE_VERTEX_ANIMATION'], ['USE_DEFORM_VERTEXES'],
                     ['USE_ALPHA_TEST', 'USE_TCGEN', 'USE_TCMOD']],
    }
    for name, variants in sets.items():
        for defines in variants:
            for froxel in ((False, True) if name in ('generic', 'fogpass') else (False,)):
                fragment_lib = volumetric if name in ('generic', 'fogpass') else (pbr if name == 'lightall' else '')
                prog = build(name, defines, lib, fragment_lib, froxel)
                gl('glDeleteProgram', None, U)(prog)
                count += 1

    # surface sprites, the valid permutations of GLSL_LoadGPUProgramSurfaceSprites
    FACE_CAMERA, FACE_UP, FX, FOG, FOG_MODULATE, ADDITIVE, FLATTENED, VELOCITY, AUTO_GRASS = (
        1, 2, 4, 8, 16, 32, 64, 128, 256)
    interact = stage('foliage_interact', 'vertex') + '\n'
    for i in range(512):
        if (i & FACE_CAMERA) and (i & FACE_UP):
            continue
        if (i & AUTO_GRASS) and (i & (FACE_CAMERA | FACE_UP | FX | FOG_MODULATE | ADDITIVE | FLATTENED)):
            continue
        # one of each structural combination is enough (fog modulate and additive only touch the fragment)
        if i & (FOG_MODULATE | ADDITIVE):
            continue
        defines = []
        if i & FACE_CAMERA:
            defines.append('FACE_CAMERA')
        elif i & FACE_UP:
            defines.append('FACE_UP')
        elif i & FLATTENED:
            defines.append('FACE_FLATTENED')
        if i & FX:
            defines.append('FX_SPRITE')
        if i & FOG:
            defines.append('USE_FOG')
        if i & VELOCITY:
            defines.append('VELOCITY_PASS')
        if i & AUTO_GRASS:
            defines.append('AUTO_GRASS')
        prog = build('surface_sprites', defines, interact, volumetric, froxel=bool(i & FOG))
        gl('glDeleteProgram', None, U)(prog)
        count += 1

    for defines in ([], ['DEBUG_VIEW']):
        prog = build('foliage_field', defines, '', interact)
        gl('glDeleteProgram', None, U)(prog)
        count += 1
    return count


# ---------------------------------------------------------------------------
# numpy mirrors of tr_plantskel.cpp / tr_plantsim.cpp
# ---------------------------------------------------------------------------

def load_frond():
    """vertices, triangles of one frond of yavin/plant_b.md3 (or a synthetic arched frond)"""
    for pk3 in ASSETS:
        if pk3.exists():
            data = zipfile.ZipFile(pk3).read('models/map_objects/yavin/plant_b.md3')
            o = struct.unpack('<4si64siiiiiiiii', data[:108])[10]
            hdr = struct.unpack('<4s64siiiiiiiiii', data[o:o + 108])
            nv, nt, otri, oxyz = hdr[5], hdr[6], hdr[7], hdr[10]
            tris = np.array([struct.unpack('<3i', data[o + otri + 12 * i:o + otri + 12 * i + 12]) for i in range(nt)])
            xyz = np.array([struct.unpack('<3h', data[o + oxyz + 8 * i:o + oxyz + 8 * i + 6]) for i in range(nv)]) / 64.0
            # the first connected piece is one frond
            parent = list(range(nv))

            def find(a):
                while parent[a] != a:
                    parent[a] = parent[parent[a]]
                    a = parent[a]
                return a
            for t in tris:
                parent[find(t[0])] = find(t[1])
                parent[find(t[1])] = find(t[2])
            root = find(0)
            keep = [v for v in range(nv) if find(v) == root]
            remap = {v: i for i, v in enumerate(keep)}
            ftris = np.array([[remap[v] for v in t] for t in tris if find(t[0]) == root])
            return xyz[keep], ftris, 'yavin/plant_b.md3 frond'
    # synthetic: an arched strip, 7 pairs of vertices
    rows = []
    for k in range(7):
        s = k / 6.0
        center = np.array([120.0 * s, 0.0, 50.0 * math.sin(math.pi * s * 0.8)])
        w = 6.0 + 18.0 * math.sin(math.pi * s)
        rows += [center + [0, w, 0], center - [0, w, 0]]
    xyz = np.array(rows)
    tris = []
    for k in range(6):
        a, b, c, d = 2 * k, 2 * k + 1, 2 * k + 2, 2 * k + 3
        tris += [[a, b, c], [b, d, c]]
    return xyz, np.array(tris), 'synthetic frond'


def skeleton(xyz):
    """centerline of one frond as R_BuildPlantSkeleton (base: the inner end of a radial leaf)"""
    root = np.array([0.0, 0.0, xyz[:, 2].min()])
    radial = np.hypot(xyz[:, 0] - root[0], xyz[:, 1] - root[1])
    zext = xyz[:, 2].max() - xyz[:, 2].min()
    rext = radial.max() - radial.min()
    if zext > rext:
        score, ext = xyz[:, 2] - xyz[:, 2].min(), zext
    else:
        score, ext = radial - radial.min(), rext
    seeds = np.where(score <= max(0.08 * ext, 0.5))[0]
    attach = xyz[seeds].mean(axis=0)
    s0 = np.min(np.linalg.norm(xyz[:, None, :] - xyz[None, seeds, :], axis=2), axis=1)
    s0[seeds] = 0.0
    smax = s0.max()
    bw = smax / 5.0
    arc = [attach]
    for k in range(1, 17):
        u = smax * k / 16
        wts = np.exp(-((s0 - u) / bw) ** 2)
        p = (xyz * wts[:, None]).sum(axis=0) / wts.sum()
        if np.linalg.norm(p - arc[-1]) > 0.25:
            arc.append(p)
    arc = np.array(arc)
    cum = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(arc, axis=0), axis=1))])
    j = len(arc) - 1
    while j > 0 and cum[-1] - cum[j] < 0.25 * cum[-1]:
        j -= 1
    d = arc[-1] - arc[j]
    d /= np.linalg.norm(d)
    ext = max(0.0, ((xyz - arc[-1]) @ d).max())
    if ext > 0:
        arc = np.vstack([arc, arc[-1] + d * ext])
        cum = np.append(cum, cum[-1] + ext)
    length = cum[-1]
    segments = max(1, min(7, int(math.floor(length / 16.0 + 0.5)), len(xyz) - 1))
    nodes = np.array([np.array([np.interp(length * k / segments, cum, arc[:, c]) for c in range(3)])
                      for k in range(segments + 1)])
    # projection of the vertices
    us = np.zeros(len(xyz))
    for v, p in enumerate(xyz):
        best, bu = 1e30, 0.0
        for k in range(segments):
            a, ab = nodes[k], nodes[k + 1] - nodes[k]
            t = np.clip(np.dot(p - a, ab) / max(np.dot(ab, ab), 1e-9), 0, 1)
            dd = np.linalg.norm(p - (a + ab * t))
            if dd < best - 1e-4:
                best, bu = dd, k + t
        us[v] = bu
    return nodes, us


def skin(us, segments, first_bone=1):
    """bone A, bone B, weight byte as R_BuildPlantSkeleton"""
    out = []
    for u in us:
        u = min(max(u, 0.0), float(segments))
        j = min(int(math.floor(u)), segments - 1)
        t = u - j
        if t < 0.5:
            a = first_bone if j == 0 else first_bone + j - 1
            b = first_bone + j
            w = 0.0 if j == 0 else t + 0.5
        else:
            a = first_bone + j
            b = first_bone + j + 1 if j + 1 < segments else a
            w = t - 0.5
        out.append((a, b, int(min(max(math.floor(w * 255 + 0.5), 0), 255)), 255))
    return np.array(out, dtype=np.uint8)


def laterals(xyz, nodes, us):
    """lamina direction across every segment (R_BuildPlantSkeleton): the principal direction of the
    vertex offsets from the centerline (perpendicular to it) over the whole frond, made perpendicular
    to each segment; horizontal when the frond has no width"""
    segments = len(nodes) - 1
    cov = np.zeros((3, 3))
    for p, u in zip(xyz, us):
        k = min(max(u, 0.0), float(segments))
        i = min(int(k), segments - 1)
        t = nodes[i + 1] - nodes[i]
        t /= np.linalg.norm(t)
        o = p - (nodes[i] + (nodes[i + 1] - nodes[i]) * (k - i))
        o -= t * (o @ t)
        cov += np.outer(o, o)
    axis = np.array([1.0, 0.0, 0.0])
    chord = nodes[-1] - nodes[0]
    fallback = np.cross([0.0, 0.0, 1.0], chord)
    if fallback @ fallback > 1e-6:
        axis = fallback / np.linalg.norm(fallback)
    if np.trace(cov) > 1e-3:
        for _ in range(16):
            axis = cov @ axis
            axis /= np.linalg.norm(axis)
    out = []
    for j in range(segments):
        t = nodes[j + 1] - nodes[j]
        t /= np.linalg.norm(t)
        l = axis - t * (axis @ t)
        if l @ l < 1e-6:
            l = np.cross([0.0, 0.0, 1.0], t)
            if l @ l < 1e-6:
                l = np.array([1.0, 0.0, 0.0])
        out.append(l / np.linalg.norm(l))
    return np.array(out)


def qmul(a, b):
    w1, x1, y1, z1 = a[3], a[0], a[1], a[2]
    w2, x2, y2, z2 = b[3], b[0], b[1], b[2]
    return np.array([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                     w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2])


def qrot(q, v):
    u = q[:3]
    t = 2.0 * np.cross(u, v)
    return v + q[3] * t + np.cross(u, t)


def qbetween(a, b):
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    c = np.dot(a, b)
    if c < -0.9999:
        axis = np.cross(a, [1.0, 0.0, 0.0])
        if np.dot(axis, axis) < 1e-6:
            axis = np.cross(a, [0.0, 1.0, 0.0])
        axis /= np.linalg.norm(axis)
        return np.array([*axis, 0.0])
    axis = np.cross(a, b)
    q = np.array([*axis, 1.0 + c])
    return q / np.linalg.norm(q)


def closest_segments(p0, p1, q0, q1):
    d1, d2, r = p1 - p0, q1 - q0, p0 - q0
    a, e, f = d1 @ d1, d2 @ d2, d2 @ r
    c, b = d1 @ r, d1 @ d2
    den = a * e - b * b
    s = np.clip((b * f - c * e) / den, 0, 1) if den > 1e-9 else 0.0
    t = (b * s + f) / e if e > 1e-9 else 0.0
    if t < 0:
        t, s = 0.0, (np.clip(-c / a, 0, 1) if a > 1e-9 else 0.0)
    elif t > 1:
        t, s = 1.0, (np.clip((b - c) / a, 0, 1) if a > 1e-9 else 0.0)
    return s, p0 + d1 * s, q0 + d2 * t


def bend_chain(X, lateral, capsule, steps=120, h=1.0 / 120.0):
    """StepFrond of tr_plantsim.cpp: leaf class, contact only"""
    K = len(X) - 1
    D = np.diff(X, axis=0)
    # rest basis of every segment: tangent, across the lamina, lamina normal
    T = D / np.linalg.norm(D, axis=1)[:, None]
    B = np.array([lateral[j] - T[j] * (lateral[j] @ T[j]) for j in range(K)])
    B /= np.linalg.norm(B, axis=1)[:, None]
    N = np.cross(T, B)
    L = np.linalg.norm(D, axis=1)
    total = L.sum()
    mass = np.zeros(K + 1)
    s = 0.0
    for j in range(K):
        dens = 1.0 - 0.6 * (s + 0.5 * L[j]) / total
        mass[j] += 0.5 * L[j] * dens
        mass[j + 1] += 0.5 * L[j] * dens
        s += L[j]
    w = np.concatenate([[0.0], 1.0 / mass[1:]])
    alpha, aniso, s = [], [], 0.0
    for j in range(K):
        freq = 4.0 + (1.6 - 4.0) * s / total
        alpha.append(1.0 / (mass[j + 1:].sum() * (2 * math.pi * freq) ** 2))
        e = min(max(s / total / 0.3, 0.0), 1.0)
        aniso.append(1.0 + 24.0 * e * e * (3 - 2 * e))
        s += L[j]
    maxcos = [math.cos(math.radians(75 if j == 0 else 55)) for j in range(K)]
    chord = X[-1] - X[0]
    flat = np.array([chord[0], chord[1], 0.0])
    flat /= np.linalg.norm(flat)
    side_dir = np.array([-flat[1], flat[0], 0.0])
    x, v = X.copy(), np.zeros_like(X)
    a0, a1, radius, velocity = capsule
    contact_side = 0
    damping = 2.0 * 0.3 * 2.0 * math.pi * 1.6 + 2.0

    def project_contact(p):
        nonlocal contact_side
        for j in range(K):
            sp, cpt, apt = closest_segments(p[j], p[j + 1], a0, a1)
            reach = radius + 6.0
            d = cpt - apt
            if d @ d >= reach * reach:
                continue
            if contact_side == 0:
                g = np.array([d[0], d[1], 0.0]) @ side_dir
                contact_side = (1 if g > 0 else -1) if abs(g) > 0.3 * radius else (1 if velocity @ side_dir >= 0 else -1)
            sd = side_dir * contact_side
            n = d / np.linalg.norm(d) if np.linalg.norm(d) > 1e-6 else sd
            if n @ sd < 0:
                n = n - sd * (2 * (n @ sd))
                n /= np.linalg.norm(n)
            pen = reach - d @ n
            if pen <= 0:
                continue
            w0, w1 = w[j] * (1 - sp), w[j + 1] * sp
            ws = w0 * (1 - sp) + w1 * sp
            if ws > 1e-9:
                p[j] += n * (pen / ws * w0)
                p[j + 1] += n * (pen / ws * w1)

    for _ in range(steps):
        v[1:] *= math.exp(-damping * h)
        p = x + v * h
        p[0] = X[0]
        lam = np.zeros((K, 3))
        for _it in range(4):
            q = np.array([0.0, 0.0, 0.0, 1.0])
            for j in range(K):
                target = p[j] + qrot(q, D[j])
                Cv = p[j + 1] - target
                basis = (qrot(q, T[j]), qrot(q, B[j]), qrot(q, N[j]))
                for axis, (e, stiff) in enumerate(zip(basis, (1.0, aniso[j], 1.0))):
                    c = Cv @ e
                    at = alpha[j] / (stiff * h * h)
                    dl = (-c - lam[j][axis] * at) / (w[j] + w[j + 1] + at)
                    lam[j][axis] += dl
                    p[j + 1] += e * (dl * w[j + 1])
                    p[j] -= e * (dl * w[j])
                rest_dir = qrot(q, D[j])
                rest_dir /= np.linalg.norm(rest_dir)
                cur = p[j + 1] - p[j]
                cd = cur / np.linalg.norm(cur)
                c = cd @ rest_dir
                if c < maxcos[j]:
                    ortho = cd - rest_dir * c
                    ortho /= np.linalg.norm(ortho)
                    p[j + 1] = p[j] + (rest_dir * maxcos[j] + ortho * math.sqrt(1 - maxcos[j] ** 2)) * np.linalg.norm(cur)
                q = qmul(qbetween(qrot(q, D[j]), p[j + 1] - p[j]), q)
            project_contact(p)
            for j in range(K):
                d = p[j + 1] - p[j]
                ln = np.linalg.norm(d)
                corr = d * ((ln - L[j]) / ((w[j] + w[j + 1]) * ln))
                p[j] += corr * w[j]
                p[j + 1] -= corr * w[j + 1]
        # final production polishing pass: hard length, then unilateral contact
        for j in range(K):
            d = p[j + 1] - p[j]
            ln = np.linalg.norm(d)
            corr = d * ((ln - L[j]) / ((w[j] + w[j + 1]) * ln))
            p[j] += corr * w[j]
            p[j + 1] -= corr * w[j + 1]
        project_contact(p)
        v = (p - x) / h
        x = p
        x[0] = X[0]
    return x, contact_side


def bones(X, x):
    """WriteBones: bone 0 rest, bone 1 + j = segment j"""
    out = [np.array([0, 0, 0, 1.0, 0, 0, 0, 0])]
    q = np.array([0.0, 0.0, 0.0, 1.0])
    for j in range(len(X) - 1):
        q = qmul(qbetween(qrot(q, X[j + 1] - X[j]), x[j + 1] - x[j]), q)
        t = x[j] - qrot(q, X[j])
        out.append(np.array([*q, *t, 0.0]))
    return np.array(out)


def skin_reference(xyz, skins, bone_data):
    out = []
    for p, (a, b, wb, _) in zip(xyz, skins):
        w = wb / 255.0
        qa, ta = bone_data[a][:4], bone_data[a][4:7]
        qb, tb = bone_data[b][:4], bone_data[b][4:7]
        out.append((qrot(qa, p) + ta) * (1 - w) + (qrot(qb, p) + tb) * w)
    return np.array(out)


def legacy_bend(xyz, root, size, bend):
    """the removed per vertex root bend (plant_bend.glsl) with a uniform push"""
    out = []
    for p in xyz:
        v = p - root
        ln = np.linalg.norm(v)
        t = min(ln / size, 1.0)
        b = bend * t * math.sqrt(t)
        nv = v / ln
        bent = nv + np.array([b[0], b[1], 0.0])
        bent /= np.linalg.norm(bent)
        out.append(root + bent * ln)
    return np.array(out)


def edge_stretch(xyz, deformed, tris):
    """largest elongation and largest shortening of the mesh edges (a bent inextensible sheet only
    shortens chords across its bend lines, it never elongates them)"""
    edges = set()
    for t in tris:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            edges.add((min(a, b), max(a, b)))
    longer, shorter = 0.0, 0.0
    for a, b in edges:
        l0 = np.linalg.norm(xyz[a] - xyz[b])
        if l0 > 1e-3:
            r = np.linalg.norm(deformed[a] - deformed[b]) / l0 - 1.0
            longer, shorter = max(longer, r), max(shorter, -r)
    return longer, shorter


# ---------------------------------------------------------------------------
# GPU helpers
# ---------------------------------------------------------------------------

def buffer(target, data):
    handle = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(handle))
    gl('glBindBuffer', None, U, U)(target, handle)
    raw = np.ascontiguousarray(data)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(target, raw.nbytes, raw.ctypes.data, STATIC_DRAW)
    return handle.value


def texture(w, h, internal, fmt, data=None, linear=False):
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(TEX2D, tex)
    ptr = None if data is None else np.ascontiguousarray(data, dtype=np.float32).ctypes.data
    gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(TEX2D, 0, internal, w, h, 0, fmt, FLOAT, ptr)
    f = 0x2601 if linear else 0x2600
    for pname, value in ((0x2801, f), (0x2800, f), (0x2802, 0x812F), (0x2803, 0x812F)):
        gl('glTexParameteri', None, U, U, I)(TEX2D, pname, value)
    return tex.value


def framebuffer(colors):
    fbo = U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(fbo))
    gl('glBindFramebuffer', None, U, U)(FB, fbo)
    for i, tex in enumerate(colors):
        gl('glFramebufferTexture2D', None, U, U, U, U, I)(FB, 0x8CE0 + i, TEX2D, tex, 0)
    bufs = (U * len(colors))(*[0x8CE0 + i for i in range(len(colors))])
    gl('glDrawBuffers', None, I, P)(len(colors), bufs)
    assert gl('glCheckFramebufferStatus', U, U)(FB) == 0x8CD5
    return fbo.value


def read_pixels(w, h, attachment=0, channels=4):
    gl('glReadBuffer', None, U)(0x8CE0 + attachment)
    out = np.zeros((h, w, 4), dtype=np.float32)
    gl('glReadPixels', None, I, I, I, I, U, U, P)(0, 0, w, h, RGBA, FLOAT, out.ctypes.data)
    return out


def bind(unit, tex, target=TEX2D):
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    gl('glBindTexture', None, U, U)(target, tex)


def uloc(prog, name):
    return gl('glGetUniformLocation', I, U, C.c_char_p)(prog, name.encode())


def vao():
    handle = U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(handle))
    gl('glBindVertexArray', None, U)(handle)
    return handle.value


PROBE_VS = """
out vec4 var_A;
out vec4 var_B;
uniform float u_Count;
void main()
{
    vec4 a, b;
    Probe(a, b);
    var_A = a;
    var_B = b;
    gl_Position = vec4((float(gl_VertexID) + 0.5) / u_Count * 2.0 - 1.0, 0.0, 0.0, 1.0);
}
"""
PROBE_FS = """
in vec4 var_A;
in vec4 var_B;
out vec4 out_Color;
out vec4 out_Glow;
void main()
{
    out_Color = var_A;
    out_Glow = var_B;
}
"""


def run_probe(vs_body, count, setup):
    """draws count points; each writes two vec4 (Probe) into its pixel of two RGBA32F targets"""
    prog = compile_program([(GL_VERTEX, vs_body + PROBE_VS), (GL_FRAGMENT, '#version 150 core\n' + PROBE_FS)],
                           'probe', outputs=('out_Color', 'out_Glow'))
    gl('glUseProgram', None, U)(prog)
    gl('glUniform1f', None, I, F)(uloc(prog, 'u_Count'), float(count))
    setup(prog)
    a = texture(count, 1, RGBA32F, RGBA)
    b = texture(count, 1, RGBA32F, RGBA)
    fbo = framebuffer([a, b])
    gl('glViewport', None, I, I, I, I)(0, 0, count, 1)
    gl('glDrawArrays', None, U, I, I)(0, 0, count)
    ra = read_pixels(count, 1, 0)[0]
    rb = read_pixels(count, 1, 1)[0]
    gl('glDeleteProgram', None, U)(prog)
    return ra, rb


def check_plant_skin():
    xyz, tris, label = load_frond()
    nodes, us = skeleton(xyz)
    segments = len(nodes) - 1
    skins = skin(us, segments)
    # a leg through the middle of the frond
    mid = nodes[len(nodes) // 2]
    chord = nodes[-1] - nodes[0]
    perp = np.array([-chord[1], chord[0], 0.0])
    perp /= np.linalg.norm(perp)
    feet = mid - perp * 4.0
    feet[2] = nodes[0][2] - 20.0
    capsule = (feet + [0, 0, 15.0], feet + [0, 0, 72.0 - 15.0], 15.0, perp * 200.0)
    x, side = bend_chain(nodes, laterals(xyz, nodes, us), capsule)
    seg_error = np.max(np.abs(np.linalg.norm(np.diff(x, axis=0), axis=1) / np.linalg.norm(np.diff(nodes, axis=0), axis=1) - 1))

    results = {}
    for pose_name, pose in (('rest', nodes), ('bent', x)):
        bone_data = bones(nodes, pose)
        reference = skin_reference(xyz, skins, bone_data)
        tbo_data = np.concatenate([bone_data.reshape(-1), bone_data.reshape(-1)]).astype(np.float32)

        def setup(prog, tbo_data=tbo_data, nbones=len(bone_data)):
            vao()
            pos = buffer(ARRAY_BUFFER, xyz.astype(np.float32))
            gl('glEnableVertexAttribArray', None, U)(0)
            gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(0, 3, FLOAT, 0, 12, None)
            sk = buffer(ARRAY_BUFFER, skins)
            gl('glEnableVertexAttribArray', None, U)(11)
            gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(11, 4, 0x1401, 1, 4, None)
            tb = buffer(TEXBUFFER, tbo_data)
            tex = U()
            gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
            bind(24, tex.value, TEXBUFFER)
            gl('glTexBuffer', None, U, U, U)(TEXBUFFER, RGBA32F, tb)
            gl('glUniform1i', None, I, I)(uloc(prog, 'u_PlantBones'), 24)
            gl('glUniform4f', None, I, F, F, F, F)(uloc(prog, 'u_PlantSkin'), 0.0, 2.0 * nbones, 1.0, 0.0)

        vs = header([]) + stage('plant_skin', 'vertex') + """
in vec3 attr_Position;
void Probe(out vec4 a, out vec4 b)
{
    vec4 rotation;
    a = vec4(PlantSkinPosition(attr_Position, false, rotation), 1.0);
    vec4 r2;
    b = vec4(PlantSkinPosition(attr_Position, true, r2), length(rotation));
}
"""
        gpu, gpu_prev = run_probe(vs, len(xyz), setup)
        err = np.max(np.abs(gpu[:, :3] - reference))
        err_prev = np.max(np.abs(gpu_prev[:, :3] - reference))
        assert err < 1e-2 and err_prev < 1e-2, (pose_name, err, err_prev)
        assert np.all(np.abs(gpu_prev[:, 3] - 1.0) < 1e-3), 'blended rotation not unit'
        if pose_name == 'rest':
            assert np.max(np.abs(gpu[:, :3] - xyz)) < 1e-3, 'rest pose moved the mesh'
        results[pose_name] = (err, reference)

    reference = results['bent'][1]
    tip_move = np.linalg.norm(x[-1] - nodes[-1])
    longer, shorter = edge_stretch(xyz, reference, tris)
    # the removed root bend with a push of the same tip displacement
    root = np.array([0.0, 0.0, xyz[:, 2].min()])
    size = max(xyz[:, 2].max() - xyz[:, 2].min(), np.linalg.norm(xyz.max(axis=0)[:2] - xyz.min(axis=0)[:2]) * 0.5)
    lo, hi = 0.0, 4.0
    for _ in range(40):
        mid_bend = 0.5 * (lo + hi)
        moved = legacy_bend(nodes[-1:], root, size, perp * mid_bend * side)[0]
        if np.linalg.norm(moved - nodes[-1]) < tip_move:
            lo = mid_bend
        else:
            hi = mid_bend
    legacy = legacy_bend(xyz, root, size, perp * lo * side)
    legacy_longer, legacy_shorter = edge_stretch(xyz, legacy, tris)
    # the frond went to one side of the leg: every centerline node on the push side
    a0 = capsule[0]
    sides = [np.sign((p - a0)[:2] @ (perp[:2] * side)) for p in x[1:]]
    return (f'{label}: {segments} segments, tip moved {tip_move:.0f} u, contact side {side:+d}, '
            f'chain length error {seg_error * 100:.2f} %, GPU vs reference {results["bent"][0]:.1e}, '
            f'mesh edges: elongated up to {longer * 100:.1f} %, chords shortened up to {shorter * 100:.1f} % '
            f'(old root bend at the same tip displacement: {legacy_longer * 100:.1f} % / {legacy_shorter * 100:.1f} %), '
            f'nodes on the push side {sum(1 for s in sides if s > 0)}/{len(sides)}')


def check_blade():
    vs_lib = header([]) + stage('foliage_interact', 'vertex')
    cases = []
    # stem (x, y, h), wind tip, delta
    for stem, wind, delta in (((0, 0, 40), (0, 0), (0, 0)), ((3, -2, 40), (0, 0), (0, 0)),
                              ((0, 0, 40), (8, 0), (0, 0)), ((0, 0, 40), (0, 0), (6, 0)),
                              ((0, 0, 40), (0, 0), (14, 0)), ((0, 0, 90), (0, 0), (10, 4)),
                              ((2, 1, 60), (5, 3), (-8, 2)), ((0, 0, 12), (0, 0), (20, 0))):
        cases.append((np.array(stem, float), np.array(wind, float), np.array(delta, float)))
    samples = 65
    count = len(cases) * samples
    data = []
    for stem, wind, delta in cases:
        for k in range(samples):
            data.append([*stem, k / (samples - 1), *wind, *delta])
    data = np.array(data, dtype=np.float32)

    def setup(prog):
        vao()
        b = buffer(ARRAY_BUFFER, data)
        for loc, off in ((0, 0), (1, 16), (2, 32)):
            gl('glEnableVertexAttribArray', None, U)(loc)
            gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(loc, 4, FLOAT, 0, 32, P(off) if off else None)
    # attribute names map to locations 0..2 (attr_Position, attr_TexCoord0, attr_TexCoord1)
    vs = vs_lib + """
in vec4 attr_Position;   // stem xyz, s
in vec4 attr_TexCoord0;  // wind xy, delta xy
void Probe(out vec4 a, out vec4 b)
{
    vec3 stem = attr_Position.xyz;
    float s = attr_Position.w;
    vec3 p = stem * s + FoliageBladeBend(stem, s, attr_TexCoord0.xy, attr_TexCoord0.zw);
    a = vec4(p, 0.0);
    b = vec4(0.0);
}
"""
    vs = vs.replace('in vec4 attr_TexCoord0;', 'in vec4 attr_TexCoord0;\n', 1)
    gpu, _ = run_probe(vs, count, setup)
    report = []
    for c, (stem, wind, delta) in enumerate(cases):
        pts = gpu[c * samples:(c + 1) * samples, :3].astype(np.float64)
        L = np.linalg.norm(stem)
        rest = np.outer(np.linspace(0, 1, samples), stem)
        arc = np.linalg.norm(np.diff(pts, axis=0), axis=1).sum()
        if not wind.any() and not delta.any():
            assert np.max(np.abs(pts - rest)) < 1e-3, ('rest moved', stem)
            continue
        assert abs(arc / L - 1.0) < 0.01, ('blade length', stem, wind, delta, arc, L)
        assert np.all(np.isfinite(pts))
        line = f'stem {stem.tolist()} wind {wind.tolist()} delta {delta.tolist()}: length {arc / L * 100:.2f} %'
        if delta.any() and not wind.any() and not stem[:2].any():
            sc = min(max(16.0 / L, 0.05), 1.0)
            k = int(round(sc * (samples - 1)))
            moved = pts[k, :2] - rest[k, :2]
            target = np.linalg.norm(delta)
            if target < 0.9 * L * sc:
                assert abs(np.linalg.norm(moved) / target - 1.0) < 0.15, ('contact point', moved, delta)
            # straight above the contact
            k_above = int(math.ceil(math.ceil(sc * 6.0 - 1e-6) / 6.0 * (samples - 1)))
            if k_above < samples - 2:
                tang = np.diff(pts[k_above:], axis=0)
                tang /= np.linalg.norm(tang, axis=1)[:, None]
                assert np.max(np.linalg.norm(tang - tang[-1], axis=1)) < 0.02, 'curved above the contact'
            line += f', contact moved {np.linalg.norm(moved):.1f} / {target:.1f}'
        if wind.any() and not delta.any() and not stem[:2].any():
            moved = np.linalg.norm(pts[-1, :2])
            assert abs(moved / np.linalg.norm(wind) - 1.0) < 0.15, ('wind tip', moved, wind)
            line += f', tip moved {moved:.1f} / {np.linalg.norm(wind):.1f}'
        report.append(line)
    return report


def check_field():
    size = 64
    extent = 256.0
    texel = extent / size
    interact = stage('foliage_interact', 'vertex')
    prog = build('foliage_field', [], '', interact)
    tex_a = [texture(size, size, RGBA16F, RGBA) for _ in range(2)]
    tex_b = [texture(size, size, RG16F, RG) for _ in range(2)]
    fbos = [framebuffer([tex_a[i], tex_b[i]]) for i in range(2)]
    for f in fbos:
        gl('glBindFramebuffer', None, U, U)(FB, f)
        gl('glClearColor', None, F, F, F, F)(0, 0, 0, 0)
        gl('glClear', None, U)(0x4000)
    ubo = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(ubo))
    block = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, b'FoliageInteraction')
    gl('glUniformBlockBinding', None, U, U, U)(prog, block, 3)
    gl('glUseProgram', None, U)(prog)
    gl('glUniform1i', None, I, I)(uloc(prog, 'u_FoliageFieldMap'), 23)
    gl('glUniform1i', None, I, I)(uloc(prog, 'u_FoliageFieldPrevMap'), 24)
    vao()
    radius = 15.0
    dt = 1.0 / 60.0
    zeta, recovery = 0.6, 0.8
    omega = 3.0 / (zeta * recovery)
    state = {'cur': 0}

    def step(capsule):
        rows = np.zeros((FIELD_BLOCK_ROWS, 4), dtype=np.float32)
        rows[0] = [1 if capsule else 0, 0, 1.0, 0]
        if capsule:
            rows[1] = [capsule[0], capsule[1], 0.0, 72.0]
            rows[2] = [radius, 0.0, 0.0, 0.0]
        rows[65] = [0.0, 0.0, 1.0 / extent, 1.0]
        rows[66] = rows[65]
        rows[67] = [dt, omega * omega, 2 * zeta * omega, 1.0]
        rows[68] = [0, 0, 0, 0]
        rows[69] = [0.25, 1.0 / 6.0, 0, 0]
        gl('glBindBuffer', None, U, U)(UNIFORM_BUFFER, ubo)
        gl('glBufferData', None, U, C.c_ssize_t, P, U)(UNIFORM_BUFFER, rows.nbytes, rows.ctypes.data, STATIC_DRAW)
        gl('glBindBufferBase', None, U, U, U)(UNIFORM_BUFFER, 3, ubo)
        src = state['cur']
        dst = 1 - src
        bind(23, tex_a[src])
        bind(24, tex_b[src])
        gl('glBindFramebuffer', None, U, U)(FB, fbos[dst])
        gl('glViewport', None, I, I, I, I)(0, 0, size, size)
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        state['cur'] = dst
        a = read_pixels(size, size, 0)
        b = read_pixels(size, size, 1)
        return a, b

    centers = (np.arange(size) + 0.5) / size * extent - extent / 2
    gx, gy = np.meshgrid(centers, centers)
    dist = np.hypot(gx - 1.3, gy - 0.7)
    for _ in range(120):
        a, b = step((1.3, 0.7))
    disp = np.hypot(a[..., 0], a[..., 1])
    inside = dist < radius - 1.0
    outside = dist > radius + 0.5 * texel
    # probe 16 units above the feet, above the capsule's lower sphere center (15): the cylinder part
    expected = radius - dist
    err_inside = np.max(np.abs(disp[inside] - expected[inside]))
    worst_at = np.argmax(np.where(inside, np.abs(disp - expected), 0.0))
    assert err_inside < 0.15, ('contact displacement', err_inside, 'at distance', dist.flat[worst_at],
                               'got', disp.flat[worst_at], 'expected', expected.flat[worst_at])
    assert np.max(disp[outside]) < 1e-3, ('grass outside the body moved', np.max(disp[outside]))
    # direction: away from the axis
    ring = inside & (dist > 3.0)
    radial = np.stack([(gx - 1.3) / dist, (gy - 0.7) / dist], -1)
    cosines = (a[..., :2] * radial).sum(-1) / np.maximum(disp, 1e-6)
    assert np.min(cosines[ring]) > 0.99, 'not pushed straight out'
    trample = np.hypot(b[..., 0], b[..., 1])
    trample_ratio = np.max(trample[ring] / disp[ring])
    held = disp.max()
    # leave: spring back towards the trampled part
    for _ in range(int(1.5 / dt)):
        a, b = step(None)
    after = np.hypot(a[..., 0], a[..., 1]).max()
    trample_after = np.hypot(b[..., 0], b[..., 1]).max()
    for _ in range(int(12.0 / dt)):
        a, b = step(None)
    final = np.hypot(a[..., 0], a[..., 1]).max()
    assert after < 0.35 * held and final < 0.05 * held, (held, after, final)
    gl('glDeleteProgram', None, U)(prog)
    return (f'field: contact error {err_inside:.3f} u, outside max {np.max(disp[outside]):.1e}, held {held:.1f} u, '
            f'trampled {trample_ratio * 100:.0f} % of it after 2 s, 1.5 s after leaving {after:.1f} u '
            f'(trampled {trample_after:.1f} u), 13.5 s after {final:.2f} u')


# FoliageInteraction block: params, 2 x 32 colliders, 5 field rows, wave params, 4 waves x 3
FIELD_BLOCK_ROWS = 1 + 64 + 5 + 1 + 4 * 3
# tr_foliageinteract.cpp
WAVE_SPEED, WAVE_WIDTH, WAVE_GRASS_KICK, WAVE_LEAF_AMPLITUDE = 1400.0, 48.0, 180.0, 6.0


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def wave_weight(px, py, origin, radius, aim, cone):
    """ForceWaveWeight without the height falloff, and the direction n"""
    rx, ry = px - origin[0], py - origin[1]
    d = np.hypot(rx, ry)
    nx = np.where(d > 1e-3, rx / np.maximum(d, 1e-9), aim[0])
    ny = np.where(d > 1e-3, ry / np.maximum(d, 1e-9), aim[1])
    a = smoothstep(cone - 0.15, cone + 0.05, nx * aim[0] + ny * aim[1]) if cone > -1.0 else np.ones_like(d)
    a = a * (1.0 - smoothstep(0.6 * radius, radius, d)) * (1.0 - 0.5 * d / radius)
    return np.where(d < radius, a, 0.0), d, nx, ny


def check_force_wave_field():
    size = 64
    extent = 1024.0
    interact = stage('foliage_interact', 'vertex')
    prog = build('foliage_field', [], '', interact)
    tex_a = [texture(size, size, RGBA16F, RGBA) for _ in range(2)]
    tex_b = [texture(size, size, RG16F, RG) for _ in range(2)]
    fbos = [framebuffer([tex_a[i], tex_b[i]]) for i in range(2)]
    ubo = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(ubo))
    block = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, b'FoliageInteraction')
    gl('glUniformBlockBinding', None, U, U, U)(prog, block, 3)
    gl('glUseProgram', None, U)(prog)
    gl('glUniform1i', None, I, I)(uloc(prog, 'u_FoliageFieldMap'), 23)
    gl('glUniform1i', None, I, I)(uloc(prog, 'u_FoliageFieldPrevMap'), 24)
    vao()
    dt = 1.0 / 60.0
    zeta, recovery = 0.6, 0.8
    omega = 3.0 / (zeta * recovery)
    centers = (np.arange(size) + 0.5) / size * extent - extent / 2
    gx, gy = np.meshgrid(centers, centers)
    origin, radius, aim, cone = (-200.0, 30.0, 40.0), 420.0, (1.0, 0.0), 0.6
    report = []

    for kick in (1.0, -0.75):
        for f in fbos:
            gl('glBindFramebuffer', None, U, U)(FB, f)
            gl('glClearColor', None, F, F, F, F)(0, 0, 0, 0)
            gl('glClear', None, U)(0x4000)
        state = {'cur': 0}

        def step(elapsed, spring):
            rows = np.zeros((FIELD_BLOCK_ROWS, 4), dtype=np.float32)
            rows[65] = [0.0, 0.0, 1.0 / extent, 1.0]
            rows[66] = rows[65]
            rows[67] = [dt, omega * omega if spring else 0.0, 2 * zeta * omega if spring else 0.0, 1.0]
            rows[69] = [0.25, 1.0 / 6.0, 0, 0]
            rows[70] = [1.0, WAVE_WIDTH, WAVE_GRASS_KICK, WAVE_SPEED]
            rows[71] = [*origin, radius]
            rows[72] = [*aim, cone, kick]
            rows[73] = [100.0, elapsed, 0.0, 0.0]
            gl('glBindBuffer', None, U, U)(UNIFORM_BUFFER, ubo)
            gl('glBufferData', None, U, C.c_ssize_t, P, U)(UNIFORM_BUFFER, rows.nbytes, rows.ctypes.data, STATIC_DRAW)
            gl('glBindBufferBase', None, U, U, U)(UNIFORM_BUFFER, 3, ubo)
            src = state['cur']
            dst = 1 - src
            bind(23, tex_a[src])
            bind(24, tex_b[src])
            gl('glBindFramebuffer', None, U, U)(FB, fbos[dst])
            gl('glViewport', None, I, I, I, I)(0, 0, size, size)
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
            state['cur'] = dst
            return read_pixels(size, size, 0)

        # no spring: the velocity after the front passed is the whole kick
        frames = int(0.45 / dt)
        for n in range(1, frames + 1):
            a = step(n * dt, False)
        weight, d, nx, ny = wave_weight(gx, gy, origin, radius, aim, cone)
        part = 1.0 - smoothstep(-WAVE_WIDTH, WAVE_WIDTH, -d)
        expected = WAVE_GRASS_KICK * kick * weight * part
        vx, vy = a[..., 2], a[..., 3]
        err = np.max(np.hypot(vx - nx * expected, vy - ny * expected))
        assert err < 0.01 * WAVE_GRASS_KICK, ('wave kick', kick, err)
        hit = weight > 0.2
        speed = np.hypot(vx, vy)
        cosines = (vx * nx + vy * ny) / np.maximum(speed, 1e-6)
        assert np.min(cosines[hit] * np.sign(kick)) > 0.999, ('wave direction', kick)
        behind = (nx * aim[0] + ny * aim[1] < cone - 0.16) | (d > radius + 8.0)
        assert np.max(speed[behind]) < 1e-3, ('moved behind the cone / beyond the reach', np.max(speed[behind]))
        report.append(f'{"push" if kick > 0 else "pull"} {abs(kick):.2f}: kick error {err:.3f} u/s of '
                      f'{np.max(np.abs(expected)):.0f}, max displacement {np.hypot(a[..., 0], a[..., 1]).max():.1f} u '
                      f'(no spring)')

    # with the spring: a push bends the grass and it comes back
    for f in fbos:
        gl('glBindFramebuffer', None, U, U)(FB, f)
        gl('glClear', None, U)(0x4000)
    state['cur'] = 0
    kick = 1.0
    peak = 0.0
    for n in range(1, int(4.0 / dt) + 1):
        a = step(n * dt, True)
        disp = np.hypot(a[..., 0], a[..., 1]).max()
        peak = max(peak, disp)
        if n * dt <= 1.0:
            at_1s = disp
    final = np.hypot(a[..., 0], a[..., 1]).max()
    assert peak > 8.0 and final < 0.05 * peak, (peak, final)
    report.append(f'push with the spring: peak {peak:.1f} u, {at_1s:.1f} u after 1 s, {final:.2f} u after 4 s')
    gl('glDeleteProgram', None, U)(prog)
    return report


def check_force_wave_leaves():
    vs_lib = header([]) + stage('leaf_flutter', 'vertex')
    origin, radius, aim, cone, kick, t0 = (100.0, -50.0, 20.0), 500.0, (0.6, 0.8), 0.5, 1.25, 10.0
    pts = []
    for d, ang in ((0.0, 0.0), (60.0, 0.0), (150.0, 0.3), (300.0, -0.2), (420.0, 0.1), (200.0, 2.5), (600.0, 0.0)):
        yaw = math.atan2(aim[1], aim[0]) + ang
        pts.append((origin[0] + d * math.cos(yaw), origin[1] + d * math.sin(yaw), origin[2] + 180.0))
    times = np.linspace(t0 - 0.1, t0 + 3.0, 40)
    data = np.array([[*p, t] for p in pts for t in times], dtype=np.float32)
    count = len(data)

    def setup(prog):
        vao()
        buffer(ARRAY_BUFFER, data)
        gl('glEnableVertexAttribArray', None, U)(0)
        gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(0, 4, FLOAT, 0, 16, None)
        # the flutter off: only the wave
        gl('glUniform4f', None, I, F, F, F, F)(uloc(prog, 'u_LeafFlutter'), 1.0, 0.0, 0.0, 1.0)
        gl('glUniform4f', None, I, F, F, F, F)(uloc(prog, 'u_LeafForceWave'), 1.0, WAVE_SPEED, WAVE_LEAF_AMPLITUDE, 0.0)
        waves = np.zeros((12, 4), dtype=np.float32)
        waves[0] = [*origin, radius]
        waves[1] = [*aim, cone, kick]
        waves[2] = [t0, 0.0, 0.0, 0.0]
        gl('glUniform4fv', None, I, I, P)(uloc(prog, 'u_LeafForceWaves'), 12, waves.ctypes.data)

    vs = vs_lib + """
in vec4 attr_Position;   // world xyz, time
void Probe(out vec4 a, out vec4 b)
{
    vec2 bands;
    a = vec4(LeafFlutterOffset(attr_Position.xyz, 0.3, attr_Position.w, 1.0, bands), LeafFlutterEnabled() ? 1.0 : 0.0);
    b = vec4(0.0);
}
"""
    gpu, _ = run_probe(vs, count, setup)
    assert np.all(gpu[:, 3] == 1.0), 'leaves not enabled with the flutter off'
    px, py, t = data[:, 0].astype(np.float64), data[:, 1].astype(np.float64), data[:, 3].astype(np.float64)
    weight, d, nx, ny = wave_weight(px, py, origin, radius, aim, cone)
    tau = t - t0 - d / WAVE_SPEED
    swing = np.where(tau > 0.0, 1.35 * np.exp(-1.8 * np.maximum(tau, 0.0)) * np.sin(8.8 * np.maximum(tau, 0.0)), 0.0)
    amount = WAVE_LEAF_AMPLITUDE * kick * weight * swing
    err = np.max(np.hypot(gpu[:, 0] - nx * amount, gpu[:, 1] - ny * amount))
    assert err < 1e-3 * WAVE_LEAF_AMPLITUDE * kick + 1e-4, ('leaf offset', err)
    assert np.max(np.abs(gpu[:, 2])) == 0.0
    before = tau <= 0.0
    assert np.max(np.abs(gpu[before, :2])) == 0.0, 'moved before the front arrived'
    peak = np.max(np.hypot(gpu[:, 0], gpu[:, 1]))
    tail = np.max(np.hypot(gpu[t > t0 + 2.9, 0], gpu[t > t0 + 2.9, 1]))
    assert peak <= WAVE_LEAF_AMPLITUDE * kick * 1.01 and tail < 0.01 * peak, (peak, tail)
    return f'leaves: offset error {err:.1e} u, peak {peak:.2f} u, {tail:.3f} u after 2.9 s, still before the front'


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
    window, ctx = context(b'Foliage test')
    try:
        programs = check_permutations()
        print(f'compiled and linked {programs} programs')
        print(check_plant_skin())
        for line in check_blade():
            print('blade:', line)
        print(check_field())
        for line in check_force_wave_field():
            print('force wave field:', line)
        print(check_force_wave_leaves())
        print('PASS')
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    sys.exit(main())
