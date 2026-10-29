"""GPU regression checks for opaque SSR cache, temporal rejection and color coverage.

Uses the production GLSL on a hidden SDL context. --baseline-ref also runs the
regressions against a Git revision.
"""
import argparse
import ctypes as C
import math
from pathlib import Path
import subprocess

import numpy as np

from test_watersurface_gl import (context, compile_program, tex2d, fbo,
    read_color, bind, mipmaps, gl, uloc, SDL, U, I, F, P, GL_VERTEX, GL_FRAGMENT,
    RGBA16F, R32F, RED, RGBA, FLOAT, TEX2D)

ROOT = Path(__file__).resolve().parents[2]
SAMPLERS = ('u_SSRHiZMap', 'u_SSRPrevHitMap', 'u_VelocityMap',
    'u_SSRNormalMap', 'u_SSRCubemapMap', 'u_SSRSpecularMap',
    'u_SSRTraceMap', 'u_SSRHistoryMap', 'u_SSRHistoryGeomMap',
    'u_SSRHitMap', 'u_SSRSceneMap')


def source(name, revision=None):
    path = f'shared/rd-rend2/glsl/{name}.glsl'
    if revision:
        return subprocess.check_output(['git', 'show', f'{revision}:{path}'],
                                       cwd=ROOT).decode('utf-8')
    return (ROOT / path).read_text()


def fragment(name, revision=None):
    return source(name, revision).split('/*[Fragment]*/', 1)[1]


def program(name, defines='', revision=None, body=None):
    common = fragment('ssr_common', revision)
    if 'SSR_REFLECTED_FOG' in defines:
        # r_ssrReflectedFog: the froxel functions go in front of the library (tr_glsl.cpp)
        common = fragment('volumetric_common', revision) + common
    fs = body if body is not None else fragment(name, revision)
    vs = source(name, revision).split('/*[Vertex]*/', 1)[1].split('/*[Fragment]*/')[0]
    header = '#version 150 core\n' + defines
    prog = compile_program([(GL_VERTEX, header + vs),
        (GL_FRAGMENT, header + common + fs)], name + ' ' + defines)
    gl('glUseProgram', None, U)(prog)
    for unit, name in enumerate(SAMPLERS):
        loc = uloc(prog, name)
        if loc >= 0:
            gl('glUniform1i', None, I, I)(loc, unit)
    return prog


def v4(prog, name, values):
    loc = uloc(prog, name)
    if loc >= 0:
        gl('glUniform4fv', None, I, I, P)(loc, 1, (F * 4)(*values))


def mat(prog, name, values=None):
    loc = uloc(prog, name)
    if loc >= 0:
        values = np.eye(4, dtype=np.float32) if values is None else values
        gl('glUniformMatrix4fv', None, I, I, U, P)(loc, 1, 0,
            np.ascontiguousarray(values.T, dtype=np.float32).ctypes.data_as(P))


def uniforms(prog, w, h):
    gl('glUseProgram', None, U)(prog)
    v4(prog, 'u_SSRProjection', (1, 1, 0, 0))
    v4(prog, 'u_SSRPrevProjection', (1, 1, 0, 0))
    v4(prog, 'u_SSRDepthParams', (0, 0, 4096, 2 / w))
    v4(prog, 'u_SSRPrevDepthParams', (0, 0, 4096, 2 / w))
    v4(prog, 'u_SSRViewport', (0, 0, 1, 1))
    v4(prog, 'u_SSRTexelSize', (1 / w, 1 / h, 1 / w, 1 / h))
    for name in ('u_SSRWorldToView', 'u_SSRReproject', 'u_SSRPrevViewToView'):
        mat(prog, name)


def texture(name, data, linear=False, levels=False):
    data = np.asarray(data, dtype=np.float32)
    h, w = data.shape[:2]
    scalar = data.ndim == 2
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    tex = tex2d(w, h, R32F if scalar else RGBA16F,
                RED if scalar else RGBA, data=data, linear=linear)
    if levels:
        # Closest-depth hierarchy, including odd sizes like ssr_hiz.glsl.
        level = 0
        while min(data.shape[:2]) > 1:
            nh, nw = max(1, data.shape[0] // 2), max(1, data.shape[1] // 2)
            reduced = np.full((nh, nw), 1e20, dtype=np.float32)
            for y in range(nh):
                for x in range(nw):
                    block = data[y*2:None if y == nh-1 else y*2+2,
                                 x*2:None if x == nw-1 else x*2+2]
                    valid = block[block > 0]
                    if valid.size:
                        reduced[y, x] = valid.min()
            data = reduced
            level += 1
            gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(
                TEX2D, level, R32F, nw, nh, 0, RED, FLOAT, data.ctypes.data_as(P))
        gl('glTexParameteri', None, U, U, I)(TEX2D, 0x813D, level)
    bind(SAMPLERS.index(name), tex)
    return tex


def draw(prog, w, h, attachments=1):
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    target = fbo([tex2d(w, h, RGBA16F) for _ in range(attachments)])
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    gl('glUseProgram', None, U)(prog)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    result = read_color(target, w, h)
    assert np.isfinite(result).all()
    return result


def cache_checks(revision=None):
    body = fragment('ssr_trace', revision).split('void main()', 1)[0]
    body += '''void main() {
        vec3 Q; vec2 pixel; float depth;
        bool valid = CachedHit(vec3(-70, 0, 100), vec2(.15, .5),
            vec3(-70, 0, 100), vec3(1, 0, 0), 1024, 0, .1, Q, pixel, depth);
        out_Color = vec4(valid ? 1 : 0);
    }'''
    prog = program('ssr_trace', '#define USE_HIZ\n', revision, body)
    w, h = 256, 64
    uniforms(prog, w, h)
    v4(prog, 'u_SSRSettings', (24, 5, 1024, 8))
    v4(prog, 'u_SSRSettings2', (.6, .1, 1, 0))
    v4(prog, 'u_SSRSettings3', (6, .002, 4, 72))
    v4(prog, 'u_SSRSettings4', (1, 1, 0, 0))
    v4(prog, 'u_SSRSettings5', (0, 0, 0, 0))
    texture('u_SSRPrevHitMap', np.array([[[.85, .5, math.log2(100)/12, 1]]]))
    texture('u_VelocityMap', np.zeros((h, w, 4)))
    outcomes = {}
    for label, blocker, expected in [('clear', None, True),
            ('deep blocker at old tap', 110, False),
            ('thin blocker between old taps', 128, False)]:
        depth = np.full((h, w), 1000, dtype=np.float32)
        depth[:, 217:] = 100
        if blocker is not None:
            depth[:, blocker] = 50
        texture('u_SSRHiZMap', depth, levels=True)
        valid = draw(prog, 1, 1)[0, 0, 0] > .5
        outcomes[label] = valid == expected
    texture('u_SSRHiZMap', np.where(np.arange(w)[None, :] >= 217, 100,
            np.full((h, w), 1000, dtype=np.float32)), levels=True)
    v4(prog, 'u_SSRDepthParams', (0, 0, 8192, 2 / w))
    outcomes['cached depth uses previous zFar'] = draw(prog, 1, 1)[0, 0, 0] > .5
    v4(prog, 'u_SSRDepthParams', (0, 0, 4096, 2 / w))
    motion = np.zeros((h, w, 4), dtype=np.float32)
    motion[..., 0] = 3 / w
    texture('u_VelocityMap', motion)
    outcomes['moving reflected surface invalidates hit'] = draw(prog, 1, 1)[0, 0, 0] < .5
    texture('u_VelocityMap', np.zeros((h, w, 4)))
    v4(prog, 'u_SSRSettings3', (0, .002, 4, 72))
    outcomes['exhausted visibility budget never accepts untested tail'] = draw(prog, 1, 1)[0, 0, 0] < .5
    gl('glDeleteProgram', None, U)(prog)
    return outcomes


def cache_gate_checks():
    # The clear cached hit of cache_checks is ~180 pixels from the receiver:
    # reused at the production minimum of 24 pixels, traced again at 1000.
    body = fragment('ssr_trace').split('void main()', 1)[0]
    body += '''void main() {
        vec3 Q; vec2 pixel; float depth;
        bool valid = CachedHit(vec3(-70, 0, 100), vec2(.15, .5),
            vec3(-70, 0, 100), vec3(1, 0, 0), 1024, 0, .1, Q, pixel, depth);
        out_Color = vec4(valid ? 1 : 0);
    }'''
    prog = program('ssr_trace', '#define USE_HIZ\n', body=body)
    w, h = 256, 64
    uniforms(prog, w, h)
    v4(prog, 'u_SSRSettings', (24, 5, 1024, 8))
    v4(prog, 'u_SSRSettings2', (.6, .1, 1, 0))
    v4(prog, 'u_SSRSettings3', (6, .002, 4, 72))
    v4(prog, 'u_SSRSettings4', (1, 1, 0, 0))
    texture('u_SSRPrevHitMap', np.array([[[.85, .5, math.log2(100)/12, 1]]]))
    texture('u_VelocityMap', np.zeros((h, w, 4)))
    texture('u_SSRHiZMap', np.where(np.arange(w)[None, :] >= 217, 100,
            np.full((h, w), 1000, dtype=np.float32)), levels=True)
    v4(prog, 'u_SSRSettings5', (0, 24, 0, 0))
    reused = draw(prog, 1, 1)[0, 0, 0] > .5
    v4(prog, 'u_SSRSettings5', (0, 1000, 0, 0))
    retraced = draw(prog, 1, 1)[0, 0, 0] < .5
    gl('glDeleteProgram', None, U)(prog)
    return {'cached rays shorter than the minimum screen length are traced again': reused and retraced}


def temporal_checks(revision=None):
    prog = program('ssr_temporal', revision=revision)
    w = h = 16
    uniforms(prog, w, h)
    v4(prog, 'u_SSRSettings', (1, .9, 0, 1))
    v4(prog, 'u_SSRSettings2', (0, 0, 0, 0))
    reproj = np.eye(4, dtype=np.float32)
    reproj[3] = (0, 0, 1, 0)  # receiver's previous clip.w = linear depth
    mat(prog, 'u_SSRReproject', reproj)
    texture('u_SSRHiZMap', np.full((h, w), 100))
    texture('u_SSRNormalMap', np.tile((.5, .5, .02, 1), (h, w, 1)))
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    texture('u_SSRHistoryGeomMap', np.tile((100, .5, .5, .02), (h, w, 1)))
    current = np.zeros((h, w, 4), dtype=np.float32)
    current[..., 3] = 1
    current[::2, ::2, :3] = 1  # keep white history inside neighborhood clamp
    texture('u_SSRTraceMap', current)
    texture('u_SSRHistoryMap', np.ones((h, w, 4)), linear=True)
    outcomes = {}
    for label, previous_depth, expected in [('static hit', 500, True),
            ('same UV, changed hit depth', 100, False)]:
        texture('u_SSRHitMap', np.tile((.5, .5, math.log2(500)/12, 1), (h, w, 1)))
        texture('u_SSRPrevHitMap', np.tile((.5, .5, math.log2(previous_depth)/12, 1), (h, w, 1)))
        result = draw(prog, w, h, 2)
        retained = result[7, 7, 0] > .5
        outcomes[label] = retained == expected
    gl('glDeleteProgram', None, U)(prog)
    return outcomes


def coverage_checks():
    prog = program('ssr_downsample', '#define MASK_LEVEL\n')
    w = h = 8
    uniforms(prog, w, h)
    depth = np.full((h, w), 100, dtype=np.float32)
    depth[:, :4] = -1
    color = np.tile((0, 1, 0, 1), (h, w, 1)).astype(np.float32)
    color[:, :4] = (100, 0, 0, 1)  # bright weapon must not contaminate filtering
    texture('u_SSRHiZMap', depth)
    texture('u_SSRSceneMap', color)
    result = draw(prog, w, h)
    assert np.max(np.abs(result[:, :4])) == 0
    assert np.max(np.abs(result[:, 4:] - (0, 1, 0, 1))) < 1e-4
    gl('glDeleteProgram', None, U)(prog)
    texture('u_SSRSceneMap', result, linear=True)
    body = '''out vec4 out_Color;
    void main() {
        out_Color = SSRHitRadiance(vec4(.5, .5, log2(100.0)/12.0, 1.0),
            vec3(0, 0, 10), 0.0, 0.0, 0.0);
    }'''
    prog = program('ssr_resolve', body=body)
    uniforms(prog, w, h)
    filtered = draw(prog, 1, 1)[0, 0]
    assert filtered[0] == 0 and filtered[1] > .5 and filtered[2] == 0, filtered
    gl('glDeleteProgram', None, U)(prog)
    return {'masked mip 0 excludes weapon, retains valid HDR scene': True,
            'sharp bilinear sample at weapon boundary contains no weapon color': True}


def phase_checks():
    body = '''out vec4 out_Color;
    void main() {
        vec2 uv = gl_FragCoord.xy * u_SSRTexelSize.xy;
        ivec2 q = SSRNearestTraceTexel(uv, 2.0, u_SSRSettings.x, ivec2(4));
        out_Color = vec4(q, 0, 1);
    }'''
    prog = program('ssr_resolve', body=body)
    uniforms(prog, 8, 8)
    for phase in range(4):
        v4(prog, 'u_SSRSettings', (phase, 0, 0, 0))
        result = draw(prog, 8, 8)
        # A traced source pixel always retrieves its own trace texel, even
        # when the neighbouring frame sampled the opposite corner.
        for y in range(4):
            for x in range(4):
                assert np.array_equal(result[2*y+(phase>>1), 2*x+(phase&1), :2], (x, y))
    gl('glDeleteProgram', None, U)(prog)
    return {'all four phased grids retrieve their actual ray samples': True}


def resolve_checks():
    prog = program('ssr_resolve')
    w = h = 8
    uniforms(prog, w, h)
    v4(prog, 'u_SSRSettings', (2, 1024, 0, 0))
    texture('u_SSRHiZMap', np.full((h, w), 100))
    texture('u_SSRNormalMap', np.tile((.5, .5, .1, 1), (h, w, 1)))
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    texture('u_SSRSceneMap', np.tile((.25, .5, 1, 1), (h, w, 1)), linear=True)
    texture('u_SSRTraceMap', np.tile((.5, .5, math.log2(500)/12, .8), (4, 4, 1)))
    for phase in range(4):
        v4(prog, 'u_SSRSettings2', (.6, phase, 0, 0))
        result = draw(prog, w, h)
        assert np.max(np.abs(result - (.2, .4, .8, .8))) < .002
    gl('glDeleteProgram', None, U)(prog)
    for phase in range(4):
        # Resolve the same ray once, then upsample the premultiplied cache.
        texture('u_SSRTraceMap', np.tile((.5, .5, math.log2(500)/12, .8), (4, 4, 1)))
        radiance = program('ssr_resolve', '#define TRACE_RADIANCE\n')
        uniforms(radiance, w, h)
        v4(radiance, 'u_SSRSettings', (2, 1024, 0, 0))
        v4(radiance, 'u_SSRSettings2', (.6, phase, 0, 0))
        cached = draw(radiance, 4, 4)
        assert np.max(np.abs(cached - (.2, .4, .8, .8))) < .002
        texture('u_SSRTraceMap', cached)
        upsample = program('ssr_resolve', '#define UPSAMPLE_RADIANCE\n')
        uniforms(upsample, w, h)
        v4(upsample, 'u_SSRSettings', (2, 1024, 0, 0))
        v4(upsample, 'u_SSRSettings2', (.6, phase, 0, 0))
        result = draw(upsample, w, h)
        assert np.max(np.abs(result - (.2, .4, .8, .8))) < .002
        texture('u_SSRSceneMap', np.tile((1, .125, .25, 1), (h, w, 1)), linear=True)
        texture('u_SSRTraceMap', np.tile((.5, .5, math.log2(500)/12, .8), (4, 4, 1)))
        fresh = draw(radiance, 4, 4)
        assert np.max(np.abs(fresh - (.8, .1, .2, .8))) < .002
        texture('u_SSRSceneMap', np.tile((.25, .5, 1, 1), (h, w, 1)), linear=True)
        gl('glDeleteProgram', None, U)(radiance)
        gl('glDeleteProgram', None, U)(upsample)
    return {'direct and cached half-res radiance/confidence preserved for every phase': True,
            'radiance cache reads changed lighting from the current frame': True}


def resolve_edge_checks():
    w = h = 8
    normals = np.tile((.5, .5, .1, 1), (h, w, 1)).astype(np.float32)
    texture('u_SSRHiZMap', np.full((h, w), 100))
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    cached = np.tile((0, 1, 0, 1), (4, 4, 1)).astype(np.float32)
    cached[:, 2:] = (1, 0, 0, 1)
    texture('u_SSRTraceMap', cached)
    prog = program('ssr_resolve', '#define UPSAMPLE_RADIANCE\n')
    uniforms(prog, w, h)
    v4(prog, 'u_SSRSettings', (2, 1024, 0, 0))
    v4(prog, 'u_SSRSettings2', (.6, 0, 0, 0))
    outcomes = {}
    for label, mode in [('depth edge', 'depth'), ('normal edge', 'normal'),
                        ('roughness edge', 'roughness')]:
        ns = normals.copy()
        depth = np.full((h, w), 100, dtype=np.float32)
        if mode == 'depth':
            depth[:, 4:] = 1000
        elif mode == 'normal':
            ns[:, 4:, :2] = (.5, .99)  # nearly perpendicular world normal
        else:
            ns[:, 4:, 2] = .8
        texture('u_SSRHiZMap', depth)
        texture('u_SSRNormalMap', ns)
        texture('u_SSRCubemapMap', np.stack((np.zeros_like(depth), np.zeros_like(depth),
                                           np.zeros_like(depth), depth), axis=-1))
        result = draw(prog, w, h)
        # x=3 lies halfway between two rays on different surfaces/materials.
        outcomes[f'cached resolve rejects {label} leakage'] = result[3, 3, 0] < .001 and result[3, 3, 1] > .99
    gl('glDeleteProgram', None, U)(prog)
    return outcomes


def compile_checks():
    S = '#define SSR_STOCHASTIC\n'  # r_ssrStochastic
    FOG = FROXEL_DEFINES + '#define SSR_REFLECTED_FOG\n'  # r_ssrReflectedFog
    for name, defines in [('ssr_trace', ''), ('ssr_trace', '#define USE_HIZ\n'),
        ('ssr_trace', '#define CLASSIFY\n'), ('ssr_trace', '#define PRECLASSIFIED\n'),
        ('ssr_trace', '#define USE_HIZ\n#define PRECLASSIFIED\n'),
        ('ssr_downsample', ''), ('ssr_downsample', '#define MASK_LEVEL\n'),
        ('ssr_resolve', ''), ('ssr_resolve', '#define TRACE_RADIANCE\n'),
        ('ssr_resolve', '#define UPSAMPLE_RADIANCE\n'),
        ('ssr_temporal', ''), ('ssr_composite', ''),
        ('ssr_composite', '#define FUSED_RESOLVE\n'),
        ('ssr_composite', '#define FUSED_RESOLVE\n#define UPSAMPLE_RADIANCE\n'),
        ('ssr_debug', ''), ('ssr_hiz', '#define LINEARIZE\n'), ('ssr_hiz', ''),
        ('ssr_trace', S), ('ssr_trace', S + '#define USE_HIZ\n'), ('ssr_trace', S + '#define PRECLASSIFIED\n'),
        ('ssr_trace', S + '#define USE_HIZ\n#define PRECLASSIFIED\n'), ('ssr_resolve', S),
        ('ssr_resolve', S + '#define TRACE_RADIANCE\n'), ('ssr_resolve', S + '#define UPSAMPLE_RADIANCE\n'),
        ('ssr_temporal', S),
        ('ssr_resolve', FOG), ('ssr_resolve', FOG + '#define TRACE_RADIANCE\n'),
        ('ssr_resolve', FOG + S + '#define TRACE_RADIANCE\n'), ('ssr_resolve', FOG + '#define USE_FROXEL_RGB\n'),
        ('ssr_composite', FOG + '#define FUSED_RESOLVE\n')]:
        prog = program(name, defines)
        gl('glDeleteProgram', None, U)(prog)
    return {'30 SSR shader permutations compile/link': True}


def msaa_resolve_checks():
    # Reproduce RB_PostProcess's old default-MRT color resolve: OpenGL
    # broadcasts the read color to all enabled destination attachments.
    w = h = 8
    scene, material = (.25, .5, 1, 1), (.5, .5, .125, 100)
    src = U()
    gl('glGenFramebuffers', None, I, C.POINTER(U))(1, C.byref(src))
    gl('glBindFramebuffer', None, U, U)(0x8D40, src)
    buffers = (U * 2)()
    gl('glGenRenderbuffers', None, I, C.POINTER(U))(2, buffers)
    for i in range(2):
        gl('glBindRenderbuffer', None, U, U)(0x8D41, buffers[i])
        gl('glRenderbufferStorageMultisample', None, U, I, U, I, I)(0x8D41, 4, RGBA16F, w, h)
        gl('glFramebufferRenderbuffer', None, U, U, U, U)(0x8D40, 0x8CE0 + i, 0x8D41, buffers[i])
    attachments = (U * 2)(0x8CE0, 0x8CE1)
    gl('glDrawBuffers', None, I, P)(2, attachments)
    assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
    for i, value in enumerate((scene, material)):
        gl('glClearBufferfv', None, U, I, P)(0x1800, i, (F * 4)(*value))
    dst = fbo([tex2d(w, h, RGBA16F) for _ in range(2)])

    def blit():
        gl('glBindFramebuffer', None, U, U)(0x8CA8, src)
        gl('glReadBuffer', None, U)(0x8CE0)
        gl('glBindFramebuffer', None, U, U)(0x8CA9, dst)
        gl('glBlitFramebuffer', None, I, I, I, I, I, I, I, I, U, U)(
            0, 0, w, h, 0, 0, w, h, 0x4000, 0x2600)

    blit()
    assert np.max(np.abs(read_color(dst, w, h, 1) - scene)) < .001
    gl('glBindFramebuffer', None, U, U)(0x8D40, dst)
    gl('glClearBufferfv', None, U, I, P)(0x1800, 1, (F * 4)(*material))
    gl('glDrawBuffer', None, U)(0x8CE0)
    blit()
    assert np.max(np.abs(read_color(dst, w, h, 0) - scene)) < .001
    assert np.max(np.abs(read_color(dst, w, h, 1) - material)) < .001
    gl('glDeleteRenderbuffers', None, I, P)(2, buffers)
    gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(src))
    dst_name = U(dst)
    gl('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(dst_name))
    return {'MSAA selected color resolve preserves material attachments (old broadcast reproduced)': True}


def confidence_checks():
    prog = program('ssr_resolve', body='''out vec4 out_Color;
        void main() { out_Color = vec4(0, 0, 0,
            SSREncodeConfidence(floor(gl_FragCoord.x) / 1023.0, u_SSRSettings.x > 0.5)); }''')
    decode = program('ssr_resolve', body='''out vec4 out_Color;
        void main() {
            float w = texelFetch(u_SSRTraceMap, ivec2(gl_FragCoord.xy), 0).a;
            out_Color = vec4(abs(SSRHitConfidence(w) - floor(gl_FragCoord.x) / 1023.0),
                SSRHitReused(w) ? 1 : 0, 0, 1);
        }''')
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    stored = tex2d(1024, 1, 0x805B)  # actual trace format GL_RGBA16, not half float
    target = fbo([stored])
    for p in (prog, decode):
        uniforms(p, 1024, 1)
    for reused in (0, 1):
        gl('glBindFramebuffer', None, U, U)(0x8D40, target)
        gl('glViewport', None, I, I, I, I)(0, 0, 1024, 1)
        gl('glUseProgram', None, U)(prog)
        v4(prog, 'u_SSRSettings', (reused, 0, 0, 0))
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        values = read_color(target, 1024, 1)[0, :, 3]
        bits = np.rint(values * 65535).astype(int)
        assert np.all((bits & 1) == reused), (reused, np.count_nonzero((bits & 1) != reused))
        # zero confidence is still a hit: the geometry flag keeps w != 0
        assert np.all(bits & 2) and np.all(values > 0)
        bind(SAMPLERS.index('u_SSRTraceMap'), stored)
        result = draw(decode, 1024, 1)[0]
        assert np.max(result[:, 0]) < 1.5 / 16383, (reused, np.max(result[:, 0]))
        assert np.all(result[:, 1] == reused)
    for p in (prog, decode):
        gl('glDeleteProgram', None, U)(p)
    return {'14 bit confidence, geometry and reused bits survive actual RGBA16 storage': True}


def classified_miss_checks():
    w = h = 8
    classify = program('ssr_trace', '#define CLASSIFY\n')
    trace = program('ssr_trace', '#define PRECLASSIFIED\n')
    for prog in (classify, trace):
        uniforms(prog, w, h)
        v4(prog, 'u_SSRSettings2', (.6, .1, 1, 0))
        v4(prog, 'u_SSRSettings3', (0, .004, 4, 72))
        v4(prog, 'u_SSRSettings4', (0, 0, 0, 0))
    texture('u_SSRHiZMap', np.full((h, w), 100))
    # Valid receivers; the left half too rough to need any rays. The depth test
    # must leave the trace untouched there (the clear decides hit vs miss), and
    # run it on the right half, whose rays face away and write a miss (0).
    normals = np.tile((.5, .5, .9, 1), (h, w, 1)).astype(np.float32)
    normals[:, 4:, 2] = .1
    texture('u_SSRNormalMap', normals)
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    texture('u_SSRSpecularMap', np.tile((.5, .5, .5, 1), (h, w, 1)))
    gl('glActiveTexture', None, U)(0x84C0 + 15)  # not over a sampler's unit (unit 0 is the Hi-Z)
    target = fbo([tex2d(w, h, 0x805B)])
    depth = U()
    gl('glGenRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    gl('glBindRenderbuffer', None, U, U)(0x8D41, depth)
    gl('glRenderbufferStorage', None, U, U, I, I)(0x8D41, 0x81A5, w, h)
    gl('glFramebufferRenderbuffer', None, U, U, U, U)(0x8D40, 0x8D00, 0x8D41, depth)
    assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    # sentinel alpha 1 (the old colorBlack fake hit) shows which pixels ran
    gl('glEnable', None, U)(0x0B71)
    gl('glDepthMask', None, U)(1)
    gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
    gl('glClearBufferfv', None, U, I, P)(0x1800, 0, (F * 4)(0, 0, 0, 1))
    gl('glClearBufferfv', None, U, I, P)(0x1801, 0, (F * 1)(1))
    gl('glDepthFunc', None, U)(0x0201)  # LESS, as tr_ssr.cpp for both passes
    gl('glColorMask', None, U, U, U, U)(0, 0, 0, 0)
    gl('glUseProgram', None, U)(classify)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    gl('glDepthMask', None, U)(0)
    gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
    gl('glUseProgram', None, U)(trace)
    gl('glDrawArrays', None, U, I, I)(4, 0, 3)
    result = read_color(target, w, h)
    skipped = np.all(result[:, :4, 3] == 1)
    traced = np.all(result[:, 4:, 3] == 0)
    gl('glDisable', None, U)(0x0B71)
    gl('glDepthMask', None, U)(1)
    gl('glDeleteRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    for prog in (classify, trace):
        gl('glDeleteProgram', None, U)(prog)
    return {'LESS-only classification: no-ray pixels skip the trace (clear stays), ray pixels run it': skipped and traced}


def composite_inputs(w, h, depth=100.0):
    # receivers facing the camera (view = world normal (0, 0, -1), octahedral (1, 1)), W = 1, no cubemap
    texture('u_SSRHiZMap', np.full((h, w), depth), levels=True)
    texture('u_SSRNormalMap', np.tile((1, 1, .02, 1), (h, w, 1)))
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, depth), (h, w, 1)))
    texture('u_SSRSpecularMap', np.ones((h, w, 4)))


def emitter_checks(revision=None):
    # A glowing sphere 50 units in front of a receiver, on its reflection ray
    # (straight back to the camera). A wall hit at z = 70 hides it even when
    # the SSR confidence is zero (screen edge, grazing, ...); no hit shows it.
    prog = program('ssr_composite', revision=revision)
    uniforms(prog, 1, 1)
    composite_inputs(1, 1)
    texture('u_SSRTraceMap', np.zeros((1, 1, 4)))
    v4(prog, 'u_SSRSettings', (1, -1, 11, 0))
    v4(prog, 'u_SSRSettings2', (1, 1024, 0, 0))
    v4(prog, 'u_SSREmitterParams', (1, 0, .35, 0))
    loc = uloc(prog, 'u_SSREmitters')
    gl('glUniform4fv', None, I, I, P)(loc, 3, (F * 12)(0, 0, 50, 5, 0, 0, 50, 1, 1, 1, 1, 0))
    outcomes = {}
    wall = (.5, .5, math.log2(70) / 12)
    for label, w_value, hidden in [('no hit: emitter reflected', 0.0, False),
            ('zero confidence wall hit still hides the emitter', 2 / 65535, True),
            ('confident wall hit hides the emitter', 1.0, True)]:
        # fp32: the zero-confidence value 2 / 65535 is a half float denormal
        gl('glActiveTexture', None, U)(0x84C0 + 15)
        bind(SAMPLERS.index('u_SSRHistoryMap'), tex2d(1, 1, data=np.array([[[*wall, w_value]]])))

        value = draw(prog, 1, 1)[0, 0, 0]
        outcomes[label] = (value < 1e-4) if hidden else (value > .1)
    gl('glDeleteProgram', None, U)(prog)
    return outcomes


def emitter_neighbour_checks(revision=None):
    # Half resolution, 4x2 pixels, trace 2x1, phase 0. Pixel (1, 0) reads the
    # rays of pixels (0, 0) and (2, 0), same flat surface. Its own reflection
    # ray passes a glowing sphere at t = 50. Ray (0, 0) hit a wall 30 units
    # beside this ray (at t = 20 along it); ray (2, 0) missed. The wall is
    # nearer than the sphere by Euclidean distance (36 < 50), but this ray
    # never meets it: the emitter must stay visible.
    w, h = 4, 2
    prog = program('ssr_composite', revision=revision)
    uniforms(prog, w, h)
    v4(prog, 'u_SSRDepthParams', (0, 0, 4096, .001))
    composite_inputs(w, h)
    texture('u_SSRTraceMap', np.zeros((h, w, 4)))
    v4(prog, 'u_SSRSettings', (1, -1, 11, 0))
    v4(prog, 'u_SSRSettings2', (2, 1024, 0, 0))
    v4(prog, 'u_SSREmitterParams', (1, 0, .35, 0))
    uv = np.array([1.5 / w, .5 / h])
    Pv = np.array([*((uv * 2 - 1) * 100), 100.0])
    N = np.array([0, 0, -1.0])
    d = Pv / np.linalg.norm(Pv)
    R = d - 2 * np.dot(d, N) * N
    O = Pv + N * .2
    E = O + R * 50
    side = np.array([-R[1], R[0], 0.0])
    side /= np.linalg.norm(side)
    Q = O + R * 20 + side * 30

    def hit_texel(p, conf):
        return [*((p[:2] / p[2]) * .5 + .5), math.log2(p[2]) / 12, conf]

    loc = uloc(prog, 'u_SSREmitters')
    gl('glUniform4fv', None, I, I, P)(loc, 3, (F * 12)(*E, 5, *E, 1, 1, 1, 1, 0))
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    trace = np.array([[hit_texel(Q, 1.0), [0, 0, 0, 0]]], dtype=np.float32)
    bind(SAMPLERS.index('u_SSRHistoryMap'), tex2d(2, 1, data=trace))
    value = draw(prog, w, h)[0, 1, 0]
    gl('glDeleteProgram', None, U)(prog)
    return {'half-res emitter occlusion: a neighbour hit beside this ray does not hide the emitter': value > .1}


def trace_count_checks():
    # Both trace variants pass exactly the texels that need a ray (the query of
    # r_ssrReceiverCull -1): the classified one through the depth test, the
    # other by discarding. The discarded texels keep the cleared miss.
    w = h = 8
    classify = program('ssr_trace', '#define CLASSIFY\n')
    culled = program('ssr_trace', '#define PRECLASSIFIED\n')
    plain = program('ssr_trace')
    for prog in (classify, culled, plain):
        uniforms(prog, w, h)
        v4(prog, 'u_SSRSettings', (24, 5, 1024, 8))
        v4(prog, 'u_SSRSettings2', (.6, .1, 1, 0))
        v4(prog, 'u_SSRSettings3', (0, .004, 4, 72))
        v4(prog, 'u_SSRSettings4', (0, 0, 0, 0))
    texture('u_SSRHiZMap', np.full((h, w), 100))
    normals = np.tile((.5, .5, .9, 1), (h, w, 1)).astype(np.float32)
    normals[:, 5:, 2] = .1  # 3 of 8 columns need a ray: 24 texels
    texture('u_SSRNormalMap', normals)
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    texture('u_SSRSpecularMap', np.tile((.5, .5, .5, 1), (h, w, 1)))
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    target = fbo([tex2d(w, h, 0x805B)])
    depth = U()
    gl('glGenRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    gl('glBindRenderbuffer', None, U, U)(0x8D41, depth)
    gl('glRenderbufferStorage', None, U, U, I, I)(0x8D41, 0x81A5, w, h)
    gl('glFramebufferRenderbuffer', None, U, U, U, U)(0x8D40, 0x8D00, 0x8D41, depth)
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    query = U()
    gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(query))
    counts, kept = [], []
    for cull in (True, False):
        gl('glDepthMask', None, U)(1)
        gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
        gl('glClearBufferfv', None, U, I, P)(0x1800, 0, (F * 4)(0, 0, 0, 1))  # sentinel
        gl('glClearBufferfv', None, U, I, P)(0x1801, 0, (F * 1)(1))
        if cull:
            gl('glEnable', None, U)(0x0B71)
            gl('glDepthFunc', None, U)(0x0201)
            gl('glColorMask', None, U, U, U, U)(0, 0, 0, 0)
            gl('glUseProgram', None, U)(classify)
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
            gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
        else:
            gl('glDisable', None, U)(0x0B71)
        gl('glDepthMask', None, U)(0)
        gl('glUseProgram', None, U)(culled if cull else plain)
        gl('glBeginQuery', None, U, U)(0x8914, query)  # GL_SAMPLES_PASSED
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        gl('glEndQuery', None, U)(0x8914)
        samples = U()
        gl('glGetQueryObjectuiv', None, U, U, C.POINTER(U))(query, 0x8866, C.byref(samples))
        counts.append(samples.value)
        result = read_color(target, w, h)
        kept.append(np.all(result[:, :5, 3] == 1) and np.all(result[:, 5:, 3] == 0))
    gl('glDisable', None, U)(0x0B71)
    gl('glDepthMask', None, U)(1)
    gl('glDeleteQueries', None, I, C.POINTER(U))(1, C.byref(query))
    gl('glDeleteRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    for prog in (classify, culled, plain):
        gl('glDeleteProgram', None, U)(prog)
    return {f'both trace variants pass exactly the ray texels (24): {counts}': counts == [24, 24] and all(kept)}


def fused_checks():

    # The fused composite equals resolve + composite for direct and cached
    # half resolution rays, every phase.
    w = h = 8
    composite_inputs(w, h)
    scene = np.tile((.25, .5, 1, 1), (h, w, 1))
    trace = np.tile((.5, .5, math.log2(500) / 12, .8), (4, 4, 1))
    expected = (.2, .4, .8)
    outcomes = {}
    for label, defines in [('direct', '#define FUSED_RESOLVE\n'),
                           ('radiance cache', '#define FUSED_RESOLVE\n#define UPSAMPLE_RADIANCE\n')]:
        ok = True
        for phase in range(4):
            texture('u_SSRSceneMap', scene, linear=True)
            texture('u_SSRHistoryMap', trace)
            texture('u_SSRTraceMap', trace)
            if 'UPSAMPLE' in defines:
                radiance = program('ssr_resolve', '#define TRACE_RADIANCE\n')
                uniforms(radiance, w, h)
                v4(radiance, 'u_SSRSettings', (2, 1024, 0, 0))
                v4(radiance, 'u_SSRSettings2', (.6, phase, 0, 0))
                texture('u_SSRTraceMap', draw(radiance, 4, 4))
                gl('glDeleteProgram', None, U)(radiance)
            fused = program('ssr_composite', defines)
            uniforms(fused, w, h)
            v4(fused, 'u_SSRSettings', (1, -1, 0, 0))
            v4(fused, 'u_SSRSettings2', (2, 1024, phase, 0))
            v4(fused, 'u_SSRSettings3', (0, 0, 0, 0))
            v4(fused, 'u_SSREmitterParams', (0, 0, .35, 0))
            result = draw(fused, w, h)
            ok = ok and np.max(np.abs(result[..., :3] - expected)) < .002
            gl('glDeleteProgram', None, U)(fused)
        outcomes[f'fused composite resolves {label} half-res rays like the resolve pass'] = ok
    return outcomes


def footprint_checks(revision=None):
    # Rough hit at depth 500 on pixel corner (8, 8); mip 0 of the scene is
    # green, mips 1.. red. A surface at depth 100 near the hit (in front of it)
    # must keep the blur off it (green); without it, or with the test off, the
    # cone mip is red. The bilinear color taps reach the neighbouring Hi-Z
    # cells too: a foreground in the cell left of the hit's cell bleeds into
    # the sample, which a test of the hit's own cell alone misses.
    w = h = 16
    body = '''out vec4 out_Color;
    void main() {
        out_Color = SSRHitRadiance(vec4(.5, .5, log2(500.0)/12.0, 1.0),
            vec3(0, 0, 100), 1.0, 3.0, u_SSRSettings.x);
    }'''
    prog = program('ssr_resolve', body=body, revision=revision)
    uniforms(prog, w, h)
    v4(prog, 'u_SSRDepthParams', (0, 0, 4096, .001))  # fine pixels: the slope allowance stays small
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    scene = tex2d(w, h, RGBA16F, data=np.tile((0, 1, 0, 1), (h, w, 1)), linear=True)
    size = w
    for level in range(1, 4):
        size //= 2
        red = np.ascontiguousarray(np.tile((1, 0, 0, 1), (size, size, 1)), dtype=np.float32)
        gl('glTexImage2D', None, U, I, I, I, I, I, U, U, P)(
            TEX2D, level, RGBA16F, size, size, 0, RGBA, FLOAT, red.ctypes.data_as(P))
    gl('glTexParameteri', None, U, U, I)(TEX2D, 0x813D, 3)
    gl('glTexParameteri', None, U, U, I)(TEX2D, 0x2801, 0x2703)
    bind(SAMPLERS.index('u_SSRSceneMap'), scene)
    outcomes = {}
    for label, foreground, test, green in [('no foreground: cone mip', None, 6, 0),
            ('one foreground pixel next to the hit: footprint narrowed', 'pixel', 6, .4),
            ('foreground in the neighbouring Hi-Z cell: footprint narrowed', 'left', 6, .9),
            ('test off: cone mip', 'left', 0, 0)]:
        depth = np.full((h, w), 500, dtype=np.float32)
        if foreground == 'pixel':
            depth[8, 9] = 100  # the pixel next to the hit, the hit's own cell at every level
        elif foreground == 'left':
            depth[:, :8] = 100  # a pillar left of the hit: other cells than the hit's at levels 1-3
        texture('u_SSRHiZMap', depth, levels=True)
        v4(prog, 'u_SSRSettings', (test, 0, 0, 0))
        c = draw(prog, 1, 1)[0, 0]
        outcomes[f'footprint depth: {label}'] = (c[1] > green and c[0] < 1 - green) if green else (c[0] > .9 and c[1] < .1)
    gl('glDeleteProgram', None, U)(prog)
    return outcomes


def stochastic_scene(w):
    """Floor y = -50 under a wall at z = 600 with three lamps (projection (1, 1), row 0 = bottom)."""
    nd = (np.arange(w) + .5) / w * 2 - 1
    X, Y = np.meshgrid(nd, nd)
    floor_t = np.where(Y < 0, -50 / np.minimum(Y, -1e-6), 1e9)
    floor = floor_t < 600
    depth = np.where(floor, floor_t, 600).astype(np.float32)
    wx, wy = X * 600, Y * 600
    color = np.full((w, w, 4), .04, np.float32)
    color[..., 3] = 1
    for lx in (-250, 0, 250):
        color[(abs(wx - lx) < 14) & (abs(wy - 150) < 14), :3] = (12, 9, 5)
    color[floor, :3] = .02
    return X, Y, floor, depth, color


def ggx_reference(X, Y, floor, depth, color, roughness, samples=256):
    """Monte Carlo GGX lobe average (visible normals, weight G2 / G1) of the visible wall, normalized over the
    rays that reach it as the SSR confidence is; NaN where under 80% of the lobe does."""
    w = X.shape[0]
    alpha = max(roughness * roughness, .0025)
    a2 = alpha * alpha
    ys, xs = np.nonzero(floor)
    P = np.stack([X[ys, xs] * depth[ys, xs], Y[ys, xs] * depth[ys, xs], depth[ys, xs]], -1)
    V = -P / np.linalg.norm(P, axis=-1, keepdims=True)
    N, T = np.array([0., 1, 0]), np.array([-1., 0, 0])  # the frame of SSRSampleGGXReflection
    B = np.cross(N, T)
    Ve = np.stack([V @ T, V @ B, V @ N], -1)
    Vh = Ve * (alpha, alpha, 1)
    Vh /= np.linalg.norm(Vh, axis=-1, keepdims=True)
    T1 = np.stack([-Vh[:, 1], Vh[:, 0], 0 * Vh[:, 0]], -1)
    T1 /= np.linalg.norm(T1, axis=-1, keepdims=True)
    T2 = np.cross(Vh, T1)
    rng = np.random.default_rng(7)
    acc, hit, total = np.zeros((len(ys), 3)), np.zeros(len(ys)), np.zeros(len(ys))
    for _ in range(samples):
        r, phi = np.sqrt(rng.random(len(ys))), 2 * np.pi * rng.random(len(ys))
        t1, t2 = r * np.cos(phi), r * np.sin(phi)
        s = .5 * (1 + Vh[:, 2])
        t2 = (1 - s) * np.sqrt(np.maximum(1 - t1 * t1, 0)) + s * t2
        Nh = t1[:, None] * T1 + t2[:, None] * T2 + np.sqrt(np.maximum(1 - t1 * t1 - t2 * t2, 0))[:, None] * Vh
        He = Nh * (alpha, alpha, 1)
        He /= np.linalg.norm(He, axis=-1, keepdims=True)
        H = He[:, :1] * T + He[:, 1:2] * B + He[:, 2:] * N
        L = 2 * np.sum(V * H, -1, keepdims=True) * H - V
        NL = L[:, 1]
        weight = np.where(NL > 0, 2 * NL / (NL + np.sqrt(a2 + (1 - a2) * NL * NL)), 0)
        t = (600 - P[:, 2]) / np.maximum(L[:, 2], 1e-6)
        sx, sy = (P[:, 0] + L[:, 0] * t) / 600, (P[:, 1] + L[:, 1] * t) / 600
        ok = (L[:, 2] > 0) & (abs(sx) < .9) & (sy > -50 / 600 + .02) & (sy < .9) & (t < 1100)
        px = np.clip((sx + 1) / 2 * w - .5, 0, w - 1.001)
        py = np.clip((sy + 1) / 2 * w - .5, 0, w - 1.001)
        x0, y0 = px.astype(int), py.astype(int)
        fx, fy = (px - x0)[:, None], (py - y0)[:, None]
        c = (color[y0, x0, :3] * (1 - fx) * (1 - fy) + color[y0, x0 + 1, :3] * fx * (1 - fy) +
             color[y0 + 1, x0, :3] * (1 - fx) * fy + color[y0 + 1, x0 + 1, :3] * fx * fy)
        acc += np.where(ok, weight, 0)[:, None] * c
        hit += np.where(ok, weight, 0)
        total += weight
    out = np.full((w, w, 3), np.nan)
    valid = hit > .8 * total
    out[ys[valid], xs[valid]] = acc[valid] / hit[valid, None]
    return out


def stochastic_checks():
    """r_ssrStochastic against a Monte Carlo GGX reference: a rough floor reflecting three lamps, the frames of
    a static camera averaged (the temporal pass's ideal). The cone blur of the mirror ray turns a lamp into a
    round blob, the GGX lobe stretches it along the view."""
    w = 128
    X, Y, floor, depth, color = stochastic_scene(w)
    gl('glActiveTexture', None, U)(0x84C0 + 15)
    scene = tex2d(w, w, RGBA16F, RGBA, data=color, linear=True)
    mipmaps(scene)  # binds it on unit 0: the textures of unit 0 (Hi-Z) follow
    bind(SAMPLERS.index('u_SSRSceneMap'), scene)
    texture('u_SSRHiZMap', depth, levels=True)
    texture('u_SSRSpecularMap', np.ones((w, w, 4)))
    texture('u_SSRCubemapMap', np.dstack([np.zeros((w, w, 3)), depth]))
    texture('u_SSRPrevHitMap', np.zeros((4, 4, 4)))
    texture('u_VelocityMap', np.zeros((4, 4, 4)))
    lum = np.array([.2126, .7152, .0722])
    roughness = .3
    normal = np.zeros((w, w, 4), np.float32)
    normal[..., 0], normal[..., 1], normal[..., 2], normal[..., 3] = .5, 1, roughness, floor
    texture('u_SSRNormalMap', normal)

    def render(stochastic, cached, frames):
        S = '#define SSR_STOCHASTIC\n' if stochastic else ''
        trace = program('ssr_trace', S + '#define USE_HIZ\n')
        resolve = program('ssr_resolve', S + ('#define UPSAMPLE_RADIANCE\n' if cached else ''))
        radiance = program('ssr_resolve', S + '#define TRACE_RADIANCE\n')
        gl('glActiveTexture', None, U)(0x84C0 + 15)
        trace_tex = tex2d(w, w, 0x805B)
        trace_fbo = fbo([trace_tex])
        ray_tex = [tex2d(w, w, RGBA16F), tex2d(w, w, RGBA16F)]
        ray_fbo = fbo(ray_tex)
        out_fbo = fbo([tex2d(w, w, RGBA16F)])
        total = np.zeros((w, w, 4))
        for frame in range(frames):
            uniforms(trace, w, w)
            v4(trace, 'u_SSRSettings', (64, 8, 2048, 8))
            v4(trace, 'u_SSRSettings2', (1, 0, 1, frame))
            v4(trace, 'u_SSRSettings3', (6, .004, 4, 1024))
            v4(trace, 'u_SSRSettings4', (0, 0, 0, 0))
            v4(trace, 'u_SSRSettings5', (0, 24, 0, 0))
            gl('glBindFramebuffer', None, U, U)(0x8D40, trace_fbo)
            gl('glViewport', None, I, I, I, I)(0, 0, w, w)
            gl('glClearBufferfv', None, U, I, P)(0x1800, 0, (F * 4)(0, 0, 0, 0))
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
            bind(SAMPLERS.index('u_SSRTraceMap'), trace_tex)
            bind(SAMPLERS.index('u_SSRHitMap'), trace_tex)
            for prog in (radiance, resolve):
                uniforms(prog, w, w)
                v4(prog, 'u_SSRSettings', (1, 2048, 7, 6))
                v4(prog, 'u_SSRSettings2', (1, 0, frame, 0))
            if cached:
                gl('glBindFramebuffer', None, U, U)(0x8D40, ray_fbo)
                gl('glUseProgram', None, U)(radiance)
                gl('glDrawArrays', None, U, I, I)(4, 0, 3)
                bind(SAMPLERS.index('u_SSRTraceMap'), ray_tex[0])
                bind(SAMPLERS.index('u_SSRHistoryGeomMap'), ray_tex[1])
            gl('glBindFramebuffer', None, U, U)(0x8D40, out_fbo)
            gl('glUseProgram', None, U)(resolve)
            gl('glDrawArrays', None, U, I, I)(4, 0, 3)
            total += read_color(out_fbo, w, w)
        for prog in (trace, resolve, radiance):
            gl('glDeleteProgram', None, U)(prog)
        return total[..., :3] / np.maximum(total[..., 3:], 1e-6), total[..., 3] / frames

    def error(image, confidence, reference):
        m = ~np.isnan(reference[..., 0]) & (confidence > .5)
        e = image[m] @ lum - reference[m] @ lum
        return np.sqrt(np.mean(e ** 2) / np.mean((reference[m] @ lum) ** 2))

    def elongation(image):
        """vertical / horizontal extent above half the peak of the middle lamp's reflection"""
        l = np.where(floor, np.nan_to_num(image @ lum), 0)[:, w // 2 - w // 8: w // 2 + w // 8]
        y, x = np.unravel_index(np.argmax(l), l.shape)
        half = l > .5 * l[y, x]
        return half[:, x].sum() / max(half[y, :].sum(), 1)

    reference = ggx_reference(X, Y, floor, depth, color, roughness)
    cone, cone_c = render(False, False, 1)
    direct, direct_c = render(True, False, 32)
    cached, cached_c = render(True, True, 32)
    e_cone, e_sto, e_cached = (error(i, c, reference) for i, c in ((cone, cone_c), (direct, direct_c), (cached, cached_c)))
    both = floor & (direct_c > .5) & (cached_c > .5)
    cache_gap = np.mean(np.abs(direct[both] - cached[both]) @ lum) / np.mean(direct[both] @ lum)
    return {f'stochastic GGX nearer the GGX reference than the cone (r 0.3: {e_sto:.3f} vs {e_cone:.3f})':
                e_sto < .9 * e_cone,
            f'lamp reflection stretched along the view like the reference ({elongation(direct):.1f} vs '
            f'{elongation(reference):.1f}, cone {elongation(cone):.1f})':
                elongation(direct) > 1.4 and elongation(cone) < 1.2,
            f'cached rays (hit point + pdf) resolve like the direct path ({cache_gap:.3f}, error {e_cached:.3f})':
                cache_gap < .05 and e_cached < .9 * e_cone}


FROXEL_DEFINES = ('#define USE_FROXEL_FOG\n#define MAX_GPU_FOGS 24\n#define MAX_GPU_FOG_VOLUMES 64\n'
                  '#define FROXEL_MAX_SLICES 128\n#define FROXEL_LOCAL_POOL 8192\n#define FROXEL_EXTINCTION_PALETTE 16\n')


def froxel_block(prog, values):
    """Fill the VolumetricFog block of prog (binding 11) with values {member: floats}, the rest 0."""
    index = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(prog, b'VolumetricFog')
    assert index != 0xFFFFFFFF
    size = C.c_int()
    gl('glGetActiveUniformBlockiv', None, U, U, U, C.POINTER(C.c_int))(prog, index, 0x8A40, C.byref(size))
    data = np.zeros(size.value // 4, dtype=np.float32)
    names = list(values)
    raw = (C.c_char_p * len(names))(*[n.encode() for n in names])
    indices = (U * len(names))()
    gl('glGetUniformIndices', None, U, I, P, P)(prog, len(names), C.cast(raw, P), C.cast(indices, P))
    offsets = (C.c_int * len(names))()
    gl('glGetActiveUniformsiv', None, U, I, P, U, P)(prog, len(names), C.cast(indices, P), 0x8A3B, C.cast(offsets, P))
    for name, offset in zip(names, offsets):
        v = np.asarray(values[name], dtype=np.float32).ravel()
        data[offset // 4: offset // 4 + len(v)] = v
    ubo = U()
    gl('glGenBuffers', None, I, C.POINTER(U))(1, C.byref(ubo))
    gl('glBindBuffer', None, U, U)(0x8A11, ubo)
    gl('glBufferData', None, U, C.c_ssize_t, P, U)(0x8A11, data.nbytes, data.ctypes.data_as(P), 0x88E4)
    gl('glBindBufferBase', None, U, U, U)(0x8A11, 11, ubo)
    gl('glUniformBlockBinding', None, U, U, U)(prog, index, 11)


def volume(unit, data):
    """RGBA16F 3D texture (data: z, y, x, 4) on unit, linear, clamped"""
    gl('glActiveTexture', None, U)(0x84C0 + unit)
    tex = U()
    gl('glGenTextures', None, I, C.POINTER(U))(1, C.byref(tex))
    gl('glBindTexture', None, U, U)(0x806F, tex)
    d = np.ascontiguousarray(data, dtype=np.float32)
    gl('glTexImage3D', None, U, I, I, I, I, I, I, U, U, P)(0x806F, 0, RGBA16F, d.shape[2], d.shape[1], d.shape[0], 0,
                                                          RGBA, FLOAT, d.ctypes.data_as(P))
    for parameter, value in ((0x2800, 0x2601), (0x2801, 0x2601), (0x2802, 0x812F), (0x2803, 0x812F), (0x8072, 0x812F)):
        gl('glTexParameteri', None, U, U, I)(0x806F, parameter, value)
    gl('glActiveTexture', None, U)(0x84C0)


def reflected_fog_checks():
    """SSRReflectedFog (r_ssrReflectedFog) against analytic media: the transmittance and in-scattering of a
    view space segment marched through a froxel volume whose camera is the SSR view (world = view space)."""
    body = '''out vec4 out_Color;
    void main() {
        vec3 S, T;
        int k = int(gl_FragCoord.x);
        vec3 P = k == 0 ? vec3(0, 0, 100) : k == 1 ? vec3(-100, 0, 300) : vec3(0, 0, 100);
        vec3 Q = k == 0 ? vec3(0, 0, 500) : k == 1 ? vec3(100, 0, 300) : vec3(0, 0, 500);
        SSRReflectedFog(P, Q, S, T);
        out_Color = vec4(S.r, T.r, T.g, T.b);
    }'''
    prog = program('ssr_resolve', FROXEL_DEFINES + '#define SSR_REFLECTED_FOG\n', body=body)
    uniforms(prog, 4, 1)
    v4(prog, 'u_SSRSettings2', (0, 0, 0, 1))
    for name, unit in (('u_FroxelSource', 12), ('u_FroxelDynamic', 13)):
        gl('glUniform1i', None, I, I)(uloc(prog, name), unit)
    near, far, nx, ny, nz = 4.0, 1000.0, 16, 16, 64
    vp = np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 1, 0]], dtype=np.float32)
    froxel_block(prog, {
        'u_FroxelViewProjection': vp.T, 'u_FroxelViewOrigin': (0, 0, 0, 1), 'u_FroxelViewForward': (0, 0, 1, 0),
        'u_FroxelSliceParams': (near, far, math.log2(far / near), 4096), 'u_FroxelGridSize': (nx, ny, nz, 0)})
    sigma, emission = .002, .004
    # slice k center depth: near * (far / near)^((k + .5) / N) (slice 0 is linear, irrelevant here)
    centers = near * (far / near) ** ((np.arange(nz) + .5) / nz)
    uniform = np.zeros((nz, ny, nx, 4), np.float32)
    uniform[..., :3], uniform[..., 3] = emission, sigma
    volume(13, np.zeros((nz, ny, nx, 4)))
    # two slices of medium around depth 300: a march with fewer steps than froxels crossed misses them
    bounds = near * (far / near) ** (np.arange(nz + 1) / nz)
    band = (centers > 270) & (centers < 330)
    band_length = float(np.sum((bounds[1:] - bounds[:-1])[band]))
    results = {}
    for label, data in (('homogeneous', uniform), ('slab', uniform * (centers > 300)[:, None, None, None]),
                        ('band', uniform * band[:, None, None, None] * 20)):
        volume(12, data)
        out = draw(prog, 3, 1)[0]
        results[label] = out
    gl('glDeleteProgram', None, U)(prog)
    T400, T200 = math.exp(-sigma * 400), math.exp(-sigma * 200)
    hom, slab, thin = results['homogeneous'], results['slab'], results['band']
    Tband = math.exp(-sigma * 20 * band_length)
    S400 = emission * (1 - T400) / sigma
    return {
        f'reflected fog, homogeneous medium along the view: T {hom[0][1]:.4f} vs {T400:.4f}, '
        f'S {hom[0][0]:.4f} vs {S400:.4f}': abs(hom[0][1] - T400) < .01 and abs(hom[0][0] - S400) < .01 * S400 + 1e-4,
        f'reflected fog, the same medium crossed sideways (not a camera ray): T {hom[1][1]:.4f} vs {T200:.4f}':
            abs(hom[1][1] - T200) < .01,
        f'reflected fog, a slab from depth 300 half crossed: T {slab[2][1]:.4f} vs {T200:.4f}':
            abs(slab[2][1] - T200) < .03,
        f'reflected fog, two slices of dense medium on the way: T {thin[0][1]:.4f} vs {Tband:.4f}':
            abs(thin[0][1] - Tband) < .1 * (1 - Tband),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref')
    args = parser.parse_args()
    window, ctx = context(b'Opaque SSR regressions')
    vao = U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    try:
        if args.baseline_ref:
            for name, ok in {**cache_checks(args.baseline_ref), **temporal_checks(args.baseline_ref),
                             **emitter_checks(args.baseline_ref), **emitter_neighbour_checks(args.baseline_ref),
                             **footprint_checks(args.baseline_ref)}.items():
                print(f'BASELINE {"PASS" if ok else "REPRODUCED BUG"}: {name}', flush=True)
        outcomes = {**compile_checks(), **cache_checks(), **cache_gate_checks(), **temporal_checks(),
                    **coverage_checks(), **phase_checks(), **resolve_checks(), **resolve_edge_checks(),
                    **msaa_resolve_checks(), **confidence_checks(), **classified_miss_checks(),
                    **emitter_checks(), **emitter_neighbour_checks(), **fused_checks(), **footprint_checks(),
                    **trace_count_checks(), **stochastic_checks(), **reflected_fog_checks()}

        for name, ok in outcomes.items():
            print(f'{"PASS" if ok else "FAIL"}: {name}', flush=True)
        assert all(outcomes.values()), outcomes
        assert gl('glGetError', U)() == 0
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(window)
        SDL.SDL_Quit()


if __name__ == '__main__':
    main()
