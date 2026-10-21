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
    read_color, bind, gl, uloc, SDL, U, I, F, P, GL_VERTEX, GL_FRAGMENT,
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
            vec3(0, 0, 10), 0.0, 0.0);
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
    for name, defines in [('ssr_trace', ''), ('ssr_trace', '#define USE_HIZ\n'),
        ('ssr_trace', '#define CLASSIFY\n'), ('ssr_trace', '#define PRECLASSIFIED\n'),
        ('ssr_trace', '#define USE_HIZ\n#define PRECLASSIFIED\n'),
        ('ssr_downsample', ''), ('ssr_downsample', '#define MASK_LEVEL\n'),
        ('ssr_resolve', ''), ('ssr_resolve', '#define TRACE_RADIANCE\n'),
        ('ssr_resolve', '#define UPSAMPLE_RADIANCE\n'),
        ('ssr_temporal', ''), ('ssr_composite', ''),
        ('ssr_debug', ''), ('ssr_hiz', '#define LINEARIZE\n'), ('ssr_hiz', '')]:
        prog = program(name, defines)
        gl('glDeleteProgram', None, U)(prog)
    return {'15 SSR shader permutations compile/link': True}


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
            SSREncodeConfidence(gl_FragCoord.x / 1024.0, u_SSRSettings.x > 0.5)); }''')
    target = fbo([tex2d(1024, 1, 0x805B)])  # actual trace format GL_RGBA16, not half float
    uniforms(prog, 1024, 1)
    gl('glViewport', None, I, I, I, I)(0, 0, 1024, 1)
    for reused in (0, 1):
        v4(prog, 'u_SSRSettings', (reused, 0, 0, 0))
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        values = read_color(target, 1024, 1)[..., 3]
        parity = np.rint(values * 65535).astype(int) & 1
        assert np.all(parity == reused), (reused, np.count_nonzero(parity != reused))
    gl('glDeleteProgram', None, U)(prog)
    return {'reused debug bit survives actual RGBA16 storage': True}


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
    # Valid receivers, but too rough to need any rays. The depth test must
    # leave the trace untouched: its CLEAR alpha determines hit vs miss.
    texture('u_SSRNormalMap', np.tile((.5, .5, .9, 1), (h, w, 1)))
    texture('u_SSRCubemapMap', np.tile((0, 0, 0, 100), (h, w, 1)))
    texture('u_SSRSpecularMap', np.tile((.5, .5, .5, 1), (h, w, 1)))
    target = fbo([tex2d(w, h, 0x805B)])
    depth = U()
    gl('glGenRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    gl('glBindRenderbuffer', None, U, U)(0x8D41, depth)
    gl('glRenderbufferStorage', None, U, U, I, I)(0x8D41, 0x81A5, w, h)
    gl('glFramebufferRenderbuffer', None, U, U, U, U)(0x8D40, 0x8D00, 0x8D41, depth)
    assert gl('glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
    gl('glViewport', None, I, I, I, I)(0, 0, w, h)
    for alpha in (1, 0):  # old colorBlack, then the fixed clearMiss
        gl('glEnable', None, U)(0x0B71)
        gl('glDepthMask', None, U)(1)
        gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
        gl('glClearBufferfv', None, U, I, P)(0x1800, 0, (F * 4)(0, 0, 0, alpha))
        gl('glClearBufferfv', None, U, I, P)(0x1801, 0, (F * 1)(1))
        gl('glDepthFunc', None, U)(0x0201)  # LESS
        gl('glColorMask', None, U, U, U, U)(0, 0, 0, 0)
        gl('glUseProgram', None, U)(classify)
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        gl('glDepthFunc', None, U)(0x0204)  # GREATER
        gl('glDepthMask', None, U)(0)
        gl('glColorMask', None, U, U, U, U)(1, 1, 1, 1)
        gl('glUseProgram', None, U)(trace)
        gl('glDrawArrays', None, U, I, I)(4, 0, 3)
        result = read_color(target, w, h)
        assert np.all(result[..., 3] == alpha)
    gl('glDisable', None, U)(0x0B71)
    gl('glDepthMask', None, U)(1)
    gl('glDeleteRenderbuffers', None, I, C.POINTER(U))(1, C.byref(depth))
    for prog in (classify, trace):
        gl('glDeleteProgram', None, U)(prog)
    return {'classified non-receivers remain misses; old alpha-1 fake hit reproduced': True}


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
            for name, ok in {**cache_checks(args.baseline_ref), **temporal_checks(args.baseline_ref)}.items():
                print(f'BASELINE {"PASS" if ok else "REPRODUCED BUG"}: {name}', flush=True)
        outcomes = {**compile_checks(), **cache_checks(), **temporal_checks(),
                    **coverage_checks(), **phase_checks(), **resolve_checks(), **resolve_edge_checks(),
                    **msaa_resolve_checks(), **confidence_checks(), **classified_miss_checks()}
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
