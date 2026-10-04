"""GPU checks of the liquid media (r_volumetricWater, glsl/liquid_common.glsl, tr_liquid.cpp).

Run from any directory: python tools/rend2/test_liquids_gl.py   (NVIDIA Optimus: set SHIM_MCCOMPAT=0x800000001)
GPU cost of the coverage: python tools/rend2/test_liquids_gl.py --bench

Hidden SDL GL 4.3 context (bundled SDL2), the real sources:
- compiles and links the froxel injection (raster with its geometry stage, compute, media pass) and the debug
  program with the liquid library (USE_LIQUIDS), RGB extinction off / on, sun shadows off / on
- a compute probe runs LiquidCoverage / LiquidSunTransmittance / LiquidPointClass on the GPU against an exact
  CPU reference (double precision): a doubled brush does not double the covered length, overlapping and touching
  brushes give the union with no seam, a froxel segment crossed by the water surface gets the exact fraction, a
  sloped (non axial) side clips exactly, more than LIQUID_MAX_HITS intervals keep the earliest (the sun path from a
  point under them still starts there), and the medium slot (optics) is independent of the gameplay class
  (u_LiquidMaxs.w = planes + 64 * medium + 256 * class)
- the render classes under water (docs "Render classes under water"): generic with the froxel fog of every blend
  (USE_LIQUID_FOG_BLENDS), weather / weatherSplash with the liquid cull (USE_LIQUIDS) compile and link, with
  and without the defines; the cull test of weather.glsl (WeatherInLiquid) runs on the GPU against the exact
  point test; the blend algebra of the premultiplied / filter / additive froxel fog is checked on the CPU
"""
import ctypes as C
import math
import random
import re
import struct
import sys

from test_volumetric_compute import ROOT, SDL, U, I, F, P, gl, fragment, program, uniform_buffer

CONSTANTS = (ROOT / 'shared/rd-rend2/tr_local.h').read_text()
MAX_GPU_LIQUIDS = int(__import__('re').search(r'#define MAX_GPU_LIQUIDS\s+(\d+)', CONSTANTS)[1])
FROXEL_MAX_SLICES = int(__import__('re').search(r'#define FROXEL_MAX_SLICES\s+(\d+)', CONSTANTS)[1])
MAX_HITS = 8

PROBE = '''
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Cases { vec4 cases[]; };
layout(std430, binding = 1) writeonly buffer Results { vec4 results[]; };
uniform int u_NumCases;
void main()
{
	int i = int(gl_GlobalInvocationID.x);
	if (i >= u_NumCases)
		return;
	vec4 a = cases[i * 3], b = cases[i * 3 + 1], c = cases[i * 3 + 2];
	int mode = int(a.w);
	if (mode == 0)
		results[i] = vec4(LiquidCoverage(a.xyz, b.xyz, b.w, c.x, floatBitsToInt(c.y)), 0.0);
	else if (mode == 1)
	{
		float pathLength;
		vec3 T = LiquidSunTransmittance(a.xyz, b.xyz, 1.0, pathLength);
		results[i] = vec4(T, pathLength);
	}
	else
		results[i] = vec4(float(LiquidPointClass(a.xyz)), 0.0, 0.0, 0.0);
}
'''

BENCH = '''
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 1) writeonly buffer Results { vec4 results[]; };
uniform ivec3 u_Grid;
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID);
	if (any(greaterThanEqual(cell, u_Grid)))
		return;
	// a camera at the origin looking +x through a 90 degree frustum, slices of 32 units
	vec2 ndc = (vec2(cell.yz) + 0.5) / vec2(u_Grid.yz) * 2.0 - 1.0;
	vec3 dir = normalize(vec3(1.0, ndc.x, ndc.y * 0.5625));
	float t0 = float(cell.x) * 32.0, t1 = t0 + 32.0;
	vec3 covered = LiquidCoverage(vec3(0.0), dir, t0, t1, -1);
	if (covered.x + covered.y + covered.z < -1.0)
		results[0] = vec4(covered, 1.0);
}
'''


def compile_probe(main_source):
    defines = (f'#define MAX_GPU_LIQUIDS {MAX_GPU_LIQUIDS}\n#define FROXEL_MAX_SLICES {FROXEL_MAX_SLICES}\n'
               '#define USE_LIQUIDS\n#define M_PI 3.14159265358979323846\n')
    text = '#version 430 core\n' + defines + fragment('liquid_common') + main_source
    prog = gl('glCreateProgram', U)()
    shader = gl('glCreateShader', U, U)(0x91B9)
    source = C.c_char_p(text.encode())
    gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
    gl('glCompileShader', None, U)(shader)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
    gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
    assert ok.value, log.value.decode()
    gl('glAttachShader', None, U, U)(prog, shader)
    gl('glLinkProgram', None, U)(prog)
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(prog, 0x8B82, C.byref(ok))
    assert ok.value
    return prog


class Brush:
    """a convex brush: the six axial sides first (as q3map sorts them), then any other plane (n, d): n.p <= d"""

    def __init__(self, mins, maxs, extra=(), cls=0, medium=None):
        self.mins, self.maxs, self.cls = mins, maxs, cls
        self.medium = cls if medium is None else medium
        self.planes = []
        for axis in range(3):
            for sign in (-1.0, 1.0):
                n = [0.0, 0.0, 0.0]
                n[axis] = sign
                self.planes.append((n, -mins[axis] if sign < 0 else maxs[axis]))
        for n, d in extra:
            length = math.sqrt(sum(x * x for x in n))
            self.planes.append(([x / length for x in n], d / length))

    def clip(self, o, d, t0, t1):
        enter, exit_ = t0, t1
        for n, dist in self.planes:
            denom = sum(n[i] * d[i] for i in range(3))
            s = sum(n[i] * o[i] for i in range(3)) - dist
            if abs(denom) < 1e-12:
                if s > 0:
                    return None
                continue
            t = -s / denom
            if denom < 0:
                enter = max(enter, t)
            else:
                exit_ = min(exit_, t)
        return (enter, exit_) if enter < exit_ else None

    def inside(self, p, eps=0.0):
        return all(sum(n[i] * p[i] for i in range(3)) - dist <= eps for n, dist in self.planes)


def cpu_coverage(brushes, o, d, t0, t1, mask=-1):
    """exact union per medium"""
    per = [[], [], []]
    for i, b in enumerate(brushes):
        if (mask >> i) & 1 == 0:
            continue
        hit = b.clip(o, d, t0, t1)
        if hit:
            per[b.medium].append(hit)
    out = []
    for intervals in per:
        total, end = 0.0, -1e30
        for a, e in sorted(intervals):
            if e > end:
                total += e - max(a, end)
                end = e
        out.append(total)
    return out


class Scene:
    """the Liquids block, the plane buffer texture and a probe program"""

    def __init__(self, prog, brushes, materials, sun_path=True):
        assert len(brushes) <= MAX_GPU_LIQUIDS
        self.prog, self.brushes = prog, brushes
        planes, values = [], {}
        for i, b in enumerate(brushes):
            first = len(planes)
            planes += [(n[0], n[1], n[2], dist) for n, dist in b.planes]
            values[f'u_LiquidMins[{i}]'] = (*b.mins, float(first))
            values[f'u_LiquidMaxs[{i}]'] = (*b.maxs, float(len(b.planes) + 64 * b.medium + 256 * b.cls))
        values['u_LiquidParams'] = (float(len(brushes)), -1.0, 1.0 if sun_path else 0.0, 7.0)
        values['u_LiquidCaustics'] = (1.0 / 160.0, 0.0, 48.0, 0.0)   # strength 0: no caustic texture needed
        values['u_LiquidView'] = (1e6, 1.0, 0.0, 0.0)
        for c, (sigma, color, albedo, g) in enumerate(materials):
            values[f'u_LiquidMaterial[{2 * c}]'] = (*color, sigma)
            values[f'u_LiquidMaterial[{2 * c + 1}]'] = (*albedo, g)
        values['u_LiquidSlices[0]'] = struct.pack('4i', -1, -1, -1, -1)
        gl('glUseProgram', None, U)(prog)
        self.ubo = uniform_buffer(prog, 'Liquids', values, slot=14)

        data = (F * (4 * max(len(planes), 1)))(*[x for p in planes for x in p] or [0.0, 0.0, 1.0, 0.0])
        self.tbo, self.tex = U(), U()
        gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(self.tbo))
        gl('glBindBuffer', None, U, U)(0x8C2A, self.tbo)
        gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8C2A, C.sizeof(data), data, 0x88E4)
        gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(self.tex))
        gl('glActiveTexture', None, U)(0x84C0 + 3)
        gl('glBindTexture', None, U, U)(0x8C2A, self.tex)
        gl('glTexBuffer', None, U, U, U)(0x8C2A, 0x8814, self.tbo)
        gl('glActiveTexture', None, U)(0x84C0)
        location = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_LiquidPlanes')
        gl('glUniform1i', None, I, I)(location, 3)

    def run(self, cases):
        """cases: (mode, o, dir, t0, t1, mask)"""
        raw = []
        for mode, o, d, t0, t1, mask in cases:
            mask_bits = struct.unpack('f', struct.pack('i', mask))[0]
            raw += [*o, float(mode), *d, t0, t1, mask_bits, 0.0, 0.0]
        inputs = (F * len(raw))(*raw)
        buffers = (U * 2)()
        gl('glGenBuffers', None, I, C.POINTER(U))(2, buffers)
        gl('glBindBuffer', None, U, U)(0x90D2, buffers[0])
        gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, C.sizeof(inputs), inputs, 0x88E4)
        gl('glBindBuffer', None, U, U)(0x90D2, buffers[1])
        gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, 16 * len(cases), None, 0x88E4)
        gl('glBindBufferBase', None, U, U, U)(0x90D2, 0, buffers[0])
        gl('glBindBufferBase', None, U, U, U)(0x90D2, 1, buffers[1])
        gl('glUseProgram', None, U)(self.prog)
        gl('glUniform1i', None, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(self.prog, b'u_NumCases'), len(cases))
        gl('glDispatchCompute', None, U, U, U)((len(cases) + 63) // 64, 1, 1)
        gl('glMemoryBarrier', None, U)(0x2000)
        out = (F * (4 * len(cases)))()
        gl('glBindBuffer', None, U, U)(0x90D2, buffers[1])
        gl('glGetBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(0x90D2, 0, C.sizeof(out), out)
        gl('glDeleteBuffers', None, I, C.POINTER(U))(2, buffers)
        return [tuple(out[4 * i:4 * i + 4]) for i in range(len(cases))]

    def delete(self):
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(self.ubo))
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(self.tbo))
        gl('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(self.tex))


WATER = (0.0014, (2.0, 0.75, 0.25), (0.10, 0.45, 0.75), 0.75)
SLIME = (0.005, (1.6, 0.5, 0.9), (0.25, 0.7, 0.2), 0.4)
LAVA = (0.02, (0.6, 1.2, 1.2), (0.6, 0.15, 0.02), 0.3)
MATERIALS = [WATER, SLIME, LAVA]


def normalize(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


def close(gpu, cpu, label, tolerance=0.05):
    for g, c in zip(gpu, cpu):
        assert abs(g - c) <= tolerance + 1e-4 * abs(c), (label, gpu, cpu)


def coverage_cases(prog):
    checks = 0
    # 1 doubled brush (the mapper's duplicate): the union, not twice the length
    box = ([0.0, -64.0, -256.0], [512.0, 64.0, 0.0])
    scene = Scene(prog, [Brush(*box), Brush(*box)], MATERIALS)
    got = scene.run([(0, [-100.0, 0.0, -100.0], [1.0, 0.0, 0.0], 0.0, 1000.0, -1)])[0]
    close(got[:3], [512.0, 0.0, 0.0], 'doubled brush')
    scene.delete()
    checks += 1

    # 2 overlapping and touching brushes: union, and no seam at the shared plane
    brushes = [Brush([0.0, -64.0, -256.0], [100.0, 64.0, 0.0]), Brush([50.0, -64.0, -256.0], [150.0, 64.0, 0.0]),
               Brush([150.0, -64.0, -256.0], [300.0, 64.0, 0.0])]
    scene = Scene(prog, brushes, MATERIALS)
    cases = [(0, [-50.0, 0.0, -100.0], [1.0, 0.0, 0.0], 0.0, 400.0, -1)]
    # short froxel segments sliding across the touching plane x = 150 (and the overlap edges)
    for k in range(64):
        a = 120.0 + k * 1.0
        cases.append((0, [-50.0, 0.0, -100.0], [1.0, 0.0, 0.0], a + 50.0, a + 58.0, -1))
    got = scene.run(cases)
    close(got[0][:3], [300.0, 0.0, 0.0], 'overlap + touching union')
    for k, g in enumerate(got[1:]):
        close(g[:3], cpu_coverage(brushes, [-50.0, 0.0, -100.0], [1.0, 0.0, 0.0], cases[k + 1][3], cases[k + 1][4]),
              f'seam segment {k}', 0.01)
        assert abs(g[0] - 8.0) < 0.01, ('seam', k, g)
    scene.delete()
    checks += 2

    # 3 a froxel segment crossed by the water surface z = 0: the covered fraction, any view angle
    brushes = [Brush([-4096.0, -4096.0, -368.0], [4096.0, 4096.0, 0.0])]
    scene = Scene(prog, brushes, MATERIALS)
    cases, expected = [], []
    for k in range(32):
        elevation = math.radians(-2.0 - k * 2.7)
        d = [math.cos(elevation), 0.0, math.sin(elevation)]
        o = [0.0, 0.0, 40.0]
        t_surface = -40.0 / d[2]
        for offset in (-0.75, -0.3, 0.0, 0.4):
            t0 = t_surface + offset * 64.0
            cases.append((0, o, d, t0, t0 + 64.0, -1))
            expected.append(cpu_coverage(brushes, o, d, t0, t0 + 64.0))
    got = scene.run(cases)
    for g, e in zip(got, expected):
        close(g[:3], e, 'surface fraction', 0.02)
    fractional = sum(1 for e in expected if 0.5 < e[0] < 63.5)
    assert fractional >= 60, fractional
    scene.delete()
    checks += 1

    # 4 a sloped side (the bank of a river) and random rays against the exact reference
    rng = random.Random(7)
    brushes = [Brush([0.0, 0.0, -200.0], [600.0, 400.0, 0.0], extra=[((1.0, 0.0, 1.0), 400.0), ((0.0, -1.0, 2.0), 0.0)]),
               Brush([550.0, 0.0, -150.0], [900.0, 400.0, 0.0], cls=0, medium=1)]
    scene = Scene(prog, brushes, MATERIALS)
    cases, expected = [], []
    for _ in range(512):
        o = [rng.uniform(-200, 1100), rng.uniform(-200, 600), rng.uniform(-300, 200)]
        d = normalize([rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)])
        t0 = rng.uniform(0, 400)
        t1 = t0 + rng.uniform(1, 600)
        cases.append((0, o, d, t0, t1, -1))
        expected.append(cpu_coverage(brushes, o, d, t0, t1))
    got = scene.run(cases)
    for g, e in zip(got, expected):
        close(g[:3], e, 'sloped side / random rays', 0.05)
    # 5 the medium slot: the second brush is water (class) with the slime medium
    got = scene.run([(0, [700.0, 200.0, 50.0], [0.0, 0.0, -1.0], 0.0, 400.0, -1),
                     (2, [700.0, 200.0, -10.0], [0.0, 0.0, 1.0], 0.0, 0.0, -1)])
    close(got[0][:3], [0.0, 150.0, 0.0], 'medium slot coverage')
    assert got[1][0] == 0.0, ('class of a slime-medium water brush', got[1])
    scene.delete()
    checks += 2
    return checks


def sun_cases(prog):
    checks = 0
    # 6 the sun path in a lake: exact length to the surface, rgb transmittance of the water
    brushes = [Brush([-2048.0, -2048.0, -368.0], [2048.0, 2048.0, 0.0])]
    scene = Scene(prog, brushes, MATERIALS)
    elevation = math.radians(60.0)
    L = [math.cos(elevation), 0.0, math.sin(elevation)]
    got = scene.run([(1, [0.0, 0.0, -100.0], L, 0.0, 0.0, -1), (1, [0.0, 0.0, 50.0], L, 0.0, 0.0, -1)])
    path = 100.0 / math.sin(elevation)
    close([got[0][3]], [path], 'sun path length', 0.05)
    sigma, color = WATER[0], WATER[1]
    close(got[0][:3], [math.exp(-sigma * c * path) for c in color], 'sun transmittance', 1e-3)
    close(got[1], [1.0, 1.0, 1.0, 0.0], 'sun above the water', 1e-6)
    scene.delete()
    checks += 1

    # 7a two touching brushes on the sun ray, listed top first (the register path sorts them), and a
    # second body above air (left to the geometry shadow)
    two = [Brush([-512.0, -512.0, -60.0], [512.0, 512.0, 0.0]), Brush([-512.0, -512.0, -200.0], [512.0, 512.0, -60.0]),
           Brush([-512.0, -512.0, 100.0], [512.0, 512.0, 300.0])]
    for brushes, label in [(two[:2], 'register path'), (two, 'hit list path, body above air')]:
        scene = Scene(prog, brushes, MATERIALS)
        got = scene.run([(1, [0.0, 0.0, -150.0], [0.0, 0.0, 1.0], 0.0, 0.0, -1)])
        close([got[0][3]], [150.0], f'sun path over two touching brushes ({label})', 0.01)
        scene.delete()
        checks += 1

    # 7 more than LIQUID_MAX_HITS intervals on the sun ray: 12 touching layers of 10 units, listed top first
    # (the CPU sorts by distance to the camera, so the GPU order says nothing about the ray): the earliest
    # are kept, the run from p is found and the coverage of all 12 is exact (the later ones in overflow)
    layers = [Brush([-512.0, -512.0, -120.0 + 10.0 * k], [512.0, 512.0, -110.0 + 10.0 * k]) for k in reversed(range(12))]
    scene = Scene(prog, layers, MATERIALS)
    got = scene.run([(1, [0.0, 0.0, -115.0], [0.0, 0.0, 1.0], 0.0, 0.0, -1),
                     (0, [0.0, 0.0, -200.0], [0.0, 0.0, 1.0], 0.0, 400.0, -1)])
    assert got[0][3] >= (MAX_HITS - 0.5) * 10.0 - 5.0 - 0.1 and got[0][0] < 1.0, ('sun path over 8 hits', got[0])
    close(got[1][:3], [120.0, 0.0, 0.0], 'coverage over 8 hits')
    scene.delete()
    checks += 2
    return checks


def compile_programs():
    count = 0
    for rgb in (False, True):
        for shadows in (False, True):
            for name, compute, media in [('volumetric_inject', False, False), ('volumetric_inject', True, False),
                                         ('volumetric_inject', True, True), ('volumetric_debug', False, False)]:
                if name == 'volumetric_debug' and shadows:
                    continue
                prog = program(name, compute, rgb, shadows, media=media, liquids=True)
                gl('glDeleteProgram', None, U)(prog)
                count += 1
    return count


def stage(name, kind):
    text = (ROOT / f'shared/rd-rend2/glsl/{name}.glsl').read_text().replace('\r\n', '\n')
    return text.split(f'/*[{kind}]*/')[1].split('/*[')[0]


def render_header(defines):
    """the parts of GLSL_GetShaderHeader the generic and weather programs use (enum values only need to differ)"""
    text = '#version 150 core\n#define M_PI 3.14159265358979323846\n'
    for i, name in enumerate(['DEFORM_NONE', 'DEFORM_WAVE', 'DEFORM_NORMALS', 'DEFORM_BULGE', 'DEFORM_BULGE_UNIFORM',
                              'DEFORM_MOVE', 'DEFORM_PROJECTION_SHADOW', 'DEFORM_DISINTEGRATION']):
        text += f'#define {name} {i}\n'
    for i, name in enumerate(['WF_NONE', 'WF_SIN', 'WF_SQUARE', 'WF_TRIANGLE', 'WF_SAWTOOTH', 'WF_INVERSE_SAWTOOTH']):
        text += f'#define {name} {i}\n'
    for i, name in enumerate(['TCGEN_LIGHTMAP', 'TCGEN_LIGHTMAP1', 'TCGEN_LIGHTMAP2', 'TCGEN_LIGHTMAP3', 'TCGEN_TEXTURE',
                              'TCGEN_ENVIRONMENT_MAPPED', 'TCGEN_ENVIRONMENT_MAPPED_SP', 'TCGEN_ENVIRONMENT_MAPPED_SP_FP',
                              'TCGEN_FOG', 'TCGEN_VECTOR']):
        text += f'#define {name} {i + 1}\n'
    for i, name in enumerate(['CGEN_LIGHTING_DIFFUSE', 'CGEN_DISINTEGRATION_1', 'CGEN_DISINTEGRATION_2',
                              'AGEN_LIGHTING_SPECULAR', 'AGEN_LIGHTING_SPECULAR_STATIC', 'AGEN_PORTAL']):
        text += f'#define {name} {i + 10}\n'
    for i, name in enumerate(['ALPHA_TEST_GT0', 'ALPHA_TEST_LT128', 'ALPHA_TEST_GE128', 'ALPHA_TEST_GE192', 'ALPHA_TEST_E255']):
        text += f'#define {name} {i + 1}\n'
    text += ('#define MAX_G2_BONES 72\n#define MAX_GPU_FOGS 16\n#define MAX_DLIGHTS 32\n#define DSHADOW_MAP_SIZE 512\n'
             '#define r_FBufScale vec2(1.0 / 640.0, 1.0 / 480.0)\n#define USE_ALPHA_TEST\n')
    froxel = re.findall(r'^#define\s+(MAX_GPU_FOG_VOLUMES|FROXEL_MAX_SLICES|FROXEL_LOCAL_POOL|FROXEL_EXTINCTION_PALETTE|'
                        r'MAX_GPU_LIQUIDS)\s+(\d+)\b', CONSTANTS, re.M)
    if 'USE_FROXEL_FOG' in defines:
        text += ''.join(f'#define {k} {v}\n' for k, v in froxel)
    return text + ''.join(f'#define {d}\n' for d in defines)


def compile_render_programs():
    """the programs this review changed outside the froxel passes, with and without the liquid defines:
    generic (USE_FOG + the froxel fog, USE_LIQUID_FOG_BLENDS), weather and weatherSplash (USE_LIQUIDS)"""
    from test_watersurface_gl import compile_program
    count = 0
    vertex_library = stage('leaf_flutter', 'Vertex') + stage('foliage_interact', 'Vertex') + stage('plant_bend', 'Vertex')
    for rgb in (False, True):
        for liquids in (False, True):
            for extra in ([], ['USE_RGBAGEN', 'USE_TCGEN', 'USE_TCMOD']):
                defines = ['USE_FROXEL_FOG', 'r_volumetricFogSamples 4', 'USE_FOG', 'USE_VOLUMETRIC_FOG'] + extra
                defines += ['USE_FROXEL_RGB'] if rgb else []
                defines += ['USE_LIQUID_FOG_BLENDS'] if liquids else []
                h = render_header(defines)
                prog = compile_program([(0x8B31, h + vertex_library + stage('generic', 'Vertex')),
                                        (0x8B30, h + fragment('volumetric_common') + stage('generic', 'Fragment'))],
                                       f'generic rgb={rgb} liquids={liquids} {extra}')
                gl('glDeleteProgram', None, U)(prog)
                count += 1
    for liquids in (False, True):
        defines = ['USE_FROXEL_FOG'] + (['USE_LIQUIDS'] if liquids else [])
        h = render_header(defines)
        for name in ('weather', 'weatherSplash'):
            prog = compile_program([(0x8B31, h + stage(name, 'Vertex')), (0x8DD9, h + stage(name, 'Geometry')),
                                    (0x8B30, h + stage(name, 'Fragment'))], f'{name} liquids={liquids}')
            if liquids:
                assert gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, b'Liquids') != 0xFFFFFFFF, name
            gl('glDeleteProgram', None, U)(prog)
            count += 1
    return count


WEATHER_PROBE = '''
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Cases { vec4 cases[]; };
layout(std430, binding = 1) writeonly buffer Results { vec4 results[]; };
uniform int u_NumCases;
void main()
{
	int i = int(gl_GlobalInvocationID.x);
	if (i >= u_NumCases)
		return;
	results[i] = vec4(WeatherInLiquid(cases[i * 3].xyz) ? 1.0 : 0.0);
}
'''


def weather_cases():
    """WeatherInLiquid of weather.glsl (the rain / snow / splash cull) against the exact point test: inside a
    sloped bank, at the surface, above, beside, and off with u_LiquidSurface.x 0"""
    text = stage('weather', 'Vertex')
    library = text[text.index('#if defined(USE_LIQUIDS)'):text.index('#endif', text.index('bool WeatherInLiquid')) + 6]
    assert library == (lambda t: t[t.index('#if defined(USE_LIQUIDS)'):t.index('#endif', t.index('bool WeatherInLiquid')) + 6])(
        stage('weatherSplash', 'Vertex')), 'the liquid test of weather.glsl and weatherSplash.glsl differ'
    defines = (f'#define MAX_GPU_LIQUIDS {MAX_GPU_LIQUIDS}\n#define FROXEL_MAX_SLICES {FROXEL_MAX_SLICES}\n'
               '#define USE_LIQUIDS\n')
    prog = gl('glCreateProgram', U)()
    shader = gl('glCreateShader', U, U)(0x91B9)
    source = C.c_char_p(('#version 430 core\n' + defines + library + WEATHER_PROBE).encode())
    gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(source), None)
    gl('glCompileShader', None, U)(shader)
    ok, log = I(), C.create_string_buffer(16384)
    gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
    gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
    assert ok.value, log.value.decode()
    gl('glAttachShader', None, U, U)(prog, shader)
    gl('glLinkProgram', None, U)(prog)

    brushes = [Brush([0.0, 0.0, -200.0], [600.0, 400.0, 0.0], extra=[((1.0, 0.0, 1.0), 400.0)]),
               Brush([700.0, 0.0, -50.0], [900.0, 400.0, 0.0])]
    scene = Scene(prog, brushes, MATERIALS)
    rng = random.Random(11)
    points = [[100.0, 100.0, -1.0], [100.0, 100.0, 1.0], [500.0, 100.0, -50.0], [500.0, 100.0, -150.0],
              [650.0, 100.0, -10.0], [800.0, 200.0, -10.0], [800.0, 200.0, 10.0]]
    points += [[rng.uniform(-100, 1000), rng.uniform(-100, 500), rng.uniform(-250, 50)] for _ in range(1000)]
    location = gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_LiquidSurface')
    checks = 0
    for enabled in (1.0, 0.0):
        gl('glUseProgram', None, U)(prog)
        gl('glUniform4f', None, I, F, F, F, F)(location, enabled, 0.0, 0.0, 0.0)
        got = scene.run([(0, p, [0.0, 0.0, 1.0], 0.0, 0.0, -1) for p in points])
        for p, g in zip(points, got):
            # points within 0.01 of a plane may go either way in fp32
            margin = min(abs(sum(n[i] * p[i] for i in range(3)) - d) for b in brushes for n, d in b.planes)
            if margin < 0.01:
                continue
            expected = enabled > 0.5 and any(b.inside(p) for b in brushes)
            assert (g[0] > 0.5) == expected, ('weather in liquid', p, g[0], enabled)
        checks += 1
    assert sum(1 for p in points if any(b.inside(p) for b in brushes)) > 200
    scene.delete()
    gl('glDeleteProgram', None, U)(prog)
    return checks


def fog_blend_cases():
    """the over-blend algebra of the generic froxel fog (generic.glsl USE_LIQUID_FOG_BLENDS) on the CPU: what each
    blend leaves in the frame against the reference (the surface seen through the medium S, T: the background
    is already fogged, so the surface contribution becomes c * T and the medium in front of it adds S once,
    weighted by the coverage of the surface)"""
    checks = 0
    rng = random.Random(3)
    for _ in range(200):
        T, S = rng.uniform(0.0, 1.0), rng.uniform(0.0, 0.5)
        c, a, dst = rng.uniform(0.0, 1.0), rng.uniform(0.0, 1.0), rng.uniform(0.0, 1.0)
        # premultiplied ONE, ONE_MINUS_SRC_ALPHA: the surface c (premultiplied by a) covers a of the background;
        # the fogged background behind it is dst. Reference: (c + dst * (1 - a)) seen through T, S gives
        # T * c + a * S + dst * (1 - a) (the background part already holds its own fog)
        shader = c * T + S * a
        result = shader + dst * (1.0 - a)
        reference = c * T + S * a + dst * (1.0 - a)
        assert abs(result - reference) < 1e-6
        assert abs((c * T + S * a) - c * T) <= S * a + 1e-9  # a = 0: nothing added (no halo)
        # filter DST_COLOR, ZERO: the background times c; through the medium the filter fades to neutral 1
        filt = 1.0 + (c - 1.0) * T
        assert abs(dst * filt - dst * (T * c + (1.0 - T))) < 1e-6
        # additive SRC_ALPHA, ONE: the glow attenuated by the medium in front of it
        assert abs((c * T) * a - c * a * T) < 1e-9
        checks += 1
    return 1 if checks == 200 else 0


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


def close_context(window, ctx):
    SDL.SDL_GL_DeleteContext(ctx)
    SDL.SDL_DestroyWindow(window)
    SDL.SDL_Quit()


def main():
    window, ctx = context(b'Liquid media test')
    try:
        permutations = compile_programs()
        prog = compile_probe(PROBE)
        checks = coverage_cases(prog) + sun_cases(prog)
        gl('glDeleteProgram', None, U)(prog)
        render = compile_render_programs()
        checks += weather_cases() + fog_blend_cases()
        print(f'PASS: {permutations} liquid shader permutations + {render} generic / weather programs compiled and '
              f'linked, {checks} GPU / CPU checks')
    finally:
        close_context(window, ctx)
    return 0


def bench():
    """GL_TIME_ELAPSED of LiquidCoverage alone over a 160x90x128 froxel grid (1080p, 12 px tiles), every
    brush in every slice mask (the worst case: in game the per slice masks keep only the brushes near it)"""
    window, ctx = context(b'Liquid media bench')
    try:
        prog = compile_probe(BENCH)
        results = U()
        gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(results))
        gl('glBindBuffer', None, U, U)(0x90D2, results)
        gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x90D2, 16, None, 0x88E4)
        gl('glBindBufferBase', None, U, U, U)(0x90D2, 1, results)
        grid = (128, 160, 90)
        query = U()
        gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(query))
        for count in (1, 8, 32):
            rng = random.Random(count)
            brushes = []
            for _ in range(count):
                x, y = rng.uniform(64, 3000), rng.uniform(-1500, 1500)
                brushes.append(Brush([x, y, -400.0], [x + rng.uniform(200, 800), y + rng.uniform(200, 800), -40.0],
                                     extra=[((1.0, 0.3, 0.2), x + 2000.0)]))
            scene = Scene(prog, brushes, MATERIALS)
            gl('glUseProgram', None, U)(prog)
            gl('glUniform3i', None, I, I, I, I)(gl('glGetUniformLocation', I, U, C.c_char_p)(prog, b'u_Grid'), *grid)
            times = []
            for run in range(8):
                gl('glBeginQuery', None, U, U)(0x88BF, query)
                gl('glDispatchCompute', None, U, U, U)(grid[0] // 8, grid[1] // 8, grid[2])
                gl('glEndQuery', None, U)(0x88BF)
                ns = C.c_uint64()
                gl('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))(query, 0x8866, C.byref(ns))
                if run >= 2:
                    times.append(ns.value / 1e6)
            times.sort()
            print(f'{count:2d} brushes in every slice: LiquidCoverage {times[len(times) // 2]:.3f} ms (median of 6)')
            scene.delete()
        gl('glDeleteQueries', None, I, C.POINTER(U))(1, C.byref(query))
        gl('glDeleteBuffers', None, I, C.POINTER(U))(1, C.byref(results))
        gl('glDeleteProgram', None, U)(prog)
    finally:
        close_context(window, ctx)
    return 0


if __name__ == '__main__':
    sys.exit(bench() if '--bench' in sys.argv else main())
