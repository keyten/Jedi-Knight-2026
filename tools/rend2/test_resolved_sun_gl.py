"""C01 sunlight shader permutations and froxel missing-membership GPU controls."""
import ctypes as C
import json
from pathlib import Path
import sys
import lightrecon_regression as R
import test_volumetric_compute as V
from test_watersurface_gl import context, water_program


def wall_doorway():
    probe = r'''
void main() {
    if (gl_GlobalInvocationID.y != 0u || gl_GlobalInvocationID.z != 0u || gl_GlobalInvocationID.x >= 3u) return;
    int x = int(gl_GlobalInvocationID.x);
    var_Slice = 0;
    vec3 p = vec3(x == 0 ? -0.5 : x == 1 ? 0.0 : 3.0, 0.0, 0.0);
    FroxelStaticLight l = BakedAndSunLight(p, p, 1.0, vec4(0.0), 0);
    imageStore(u_InjectOutput, ivec3(x, 0, 0), vec4(l.sun.r, l.sunUnshadowed.r, l.sunPhase.x, l.baseline.r));
}
'''
    prog = V.program('volumetric_inject', True, False, True, physical=True, probe=probe)
    black = V.texture(1, 1, 1, [0]*4)
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    buffers = V.bind_program(prog, dict(black=black), False,
        dict(u_FroxelSunDirection=[0, 0, 1, 2], u_FroxelSunColor=[1, 1, 1, 1],
             u_FroxelShadowParams=[40, 16, 0, 0], u_FroxelGridScale=[0, 0, 0, 0]))
    buffers.append(V.uniform_buffer(prog, 'Lights',
        dict(u_ShadowMvp=identity, u_ShadowMvp2=identity, u_ShadowMvp3=identity,
             u_ShadowSplits=[10, 20, 40, 30], u_ShadowBlend=[0, 0, 0, 0]), 1))
    depth = [.2] * (16 * 16 * 3)
    # Opaque wall with a real open support region in every cascade.
    for layer in range(3):
        for y in range(4, 12):
            for x in range(7, 10):
                depth[(layer * 16 + y) * 16 + x] = 1
    array = V.U()
    V.gl('glGenTextures', None, V.I, C.POINTER(V.U))(1, C.byref(array))
    V.gl('glActiveTexture', None, V.U)(0x84C0 + 14)
    V.gl('glBindTexture', None, V.U, V.U)(0x8C1A, array)
    V.gl('glTexImage3D', None, V.U, V.I, V.I, V.I, V.I, V.I, V.I, V.U, V.U, V.P)(
        0x8C1A, 0, 0x822E, 16, 16, 3, 0, 0x1903, 0x1406, (V.F * len(depth))(*depth))
    for param in (0x2801, 0x2800):
        V.gl('glTexParameteri', None, V.U, V.U, V.I)(0x8C1A, param, 0x2600)
    loc = V.gl('glGetUniformLocation', V.I, V.U, C.c_char_p)(prog, b'u_ShadowMap')
    V.gl('glUniform1i', None, V.I, V.I)(loc, 14)
    out = V.texture(3, 1, 1)
    V.gl('glBindImageTexture', None, V.U, V.U, V.I, V.U, V.I, V.U, V.U)(0, out, 0, 1, 0, 0x88B9, 0x881A)
    V.gl('glDispatchCompute', None, V.U, V.U, V.U)(1, 1, 1)
    V.gl('glMemoryBarrier', None, V.U)(0x28)
    values = V.read(out, 12)
    assert values[0] == 0 and values[4] == 1 and values[8] == 0, values
    assert values[1] == 1 and values[5] == 1 and values[9] == 0, values
    assert values[3] == values[7] == values[11] == 0, values
    V.delete_buffers(buffers)
    print('PASS: actual CSM wall=0, doorway=1, unsupported projection=0; no baseline duplication')


def main():
    cpp = sys.argv[1] if len(sys.argv) > 1 else R.cpp_path()
    cases = json.loads((R.BASELINE / 'matrix.json').read_text())
    # Existing physical shaders, both sunlight modes, all supported geometry paths.
    for geometry in ['USE_LIGHTMAP', 'USE_LIGHT_VECTOR', 'USE_LIGHT_VERTEX']:
        for mode in ['SHADOWMAP_MODULATE', 'USE_PRIMARY_LIGHT']:
            for extra in [[], ['USE_ENTITY_SUN_SPLIT'], ['USE_SKIN_SSS']]:
                if extra and geometry != 'USE_LIGHT_VECTOR':
                    continue
                defines = ['USE_LIGHT', geometry, 'USE_NORMALMAP', 'USE_SPECULARMAP',
                           'USE_PHYSICAL_PBR', 'USE_SHADOWMAP', 'USE_SHADOWS2', mode] + extra
                cases.append(dict(name='C01/' + '/'.join([geometry, mode] + extra),
                                  program='lightall', defines=defines, settings={}))
    contracts = R.snapshot(cases, cpp)
    reflection = R.gpu_compile(cases, contracts)
    print('PASS: actual GLSL compile/link', len(cases), 'programs', reflection['driver'])
    window, ctx = context(b'C01 canonical sun fallback controls')
    try:
        for physical in [False, True]:
            water = water_program(physical=physical, shadows2=True, froxel=2, snell=True)
            V.gl('glDeleteProgram', None, V.U)(water)
        print('PASS: actual water shaders with legacy and physical sunlight')
        black = V.texture(1, 1, 1, [0]*4)
        baked = V.texture(1, 1, 1, [2, 3, 4, 0])
        # A very bright unsupported candidate must not turn an unknown field
        # into a black light, a bright light or an area-average visibility mask.
        for rgb in [False, True]:
            inject = V.program('volumetric_inject', True, rgb, False, physical=True)
            media = V.program('volumetric_inject', True, rgb, False, media=True, physical=True)
            textures = dict(black=black, u_VolumetricStaticGrid=baked)
            outputs = []
            for policy in [0, -1, 2]:
                overrides = dict(u_FroxelSunDirection=[0, 0, 1, policy],
                                 u_FroxelSunColor=[100000, 100000, 100000, 0])
                medium, out, tail = V.run_compute(inject, media, textures, False, overrides)
                data = V.read(out[0], V.W * V.H * V.D * 4)
                if outputs:
                    assert data == outputs[0], (rgb, policy, data[:4], outputs[0][:4])
                outputs.append(data)
            print('PASS: no P06 proof / no CSM coverage preserves full baked baseline, RGB=', rgb)
        wall_doorway()
        assert V.gl('glGetError', V.U)() == 0
    finally:
        V.SDL.SDL_GL_DeleteContext(ctx)
        V.SDL.SDL_DestroyWindow(window)
        V.SDL.SDL_Quit()
    print('C01 GPU checks passed')


if __name__ == '__main__':
    main()
