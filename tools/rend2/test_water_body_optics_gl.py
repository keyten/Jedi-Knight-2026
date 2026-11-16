"""GL 4.3 regression checks for the opt-in body-local water medium.

Checks rendered transmission against Beer-Lambert transport, shallow/deep
separation, advected air, neutral bubble scattering, lighting and Fresnel.
Uses the production fragment shader and the existing synthetic water scene.
"""
import sys
import ctypes as C
import statistics

import numpy as np

import test_water_tessellation_gl as Q
import test_watersurface_gl as T


def main():
    window, context = Q.context(b'Water body optics checks')
    ok = True
    try:
        # These samplers are assigned by GLSL_InitUniforms in the renderer.
        T.UNITS.update({'u_WaterFoamMap': 18, 'u_WaterFoamPattern': 16})
        rig = T.Rig(T.Camera((1500., -300., 1000.), (1500., 300., 0.)))
        params = rig.params()
        params[T.WATER_SLOTS['WATER_U_FOAM'], 0] = 1
        pattern = T.tex2d(1, 1, T.RGBA16F, data=np.full((1, 1, 4), .5, dtype=np.float32))
        T.bind(16, pattern)

        def field(air):
            pixels = np.array([[[0., 1., air, 0.]]], dtype=np.float32)
            T.bind(18, T.tex2d(1, 1, T.RGBA16F, data=pixels))

        def draw(program, controls, data=params):
            T.gl('glUseProgram', None, T.U)(program)
            T.set_vec4s(program, 'u_WaterBodyOptics', [controls])
            return rig.draw_water(program, data)

        transmission = T.probe_program('vec4(transmittance, pathLength)')
        field(0)
        baseline = draw(transmission, [0, 0, 0, 0])
        disabled = draw(transmission, [0, .006, .04, 0])
        covered = (baseline[..., 3] > 1) & (baseline[..., 3] < 2048)
        error = float(np.max(np.abs(baseline[covered] - disabled[covered])))
        ok = T.check('disabled medium preserves original transmission', error < 1e-7, f'error {error:.3g}') and ok

        suspended = draw(transmission, [1, .006, .04, 0])
        path = baseline[..., 3]
        expected = baseline[..., :3] * np.exp(-.006 * path)[..., None]
        error = float(np.max(np.abs(suspended[covered, :3] - expected[covered])))
        ok = T.check('suspension follows Beer-Lambert along the refracted path',
                     error < .002, f'max error {error:.3g}') and ok
        shallow = covered & (path < 25)
        deep = covered & (path > 350)
        ratio = suspended[..., 1] / np.maximum(baseline[..., 1], 1e-6)
        near, far = float(np.median(ratio[shallow])), float(np.median(ratio[deep]))
        ok = T.check('shallow edges stay clear while deep water hides the bed',
                     shallow.sum() > 100 and deep.sum() > 100 and near > .8 and far < .3,
                     f'extra transmission shallow {near:.3f}, deep {far:.3f}') and ok

        field(.75)
        aerated = draw(transmission, [1, .006, .04, 0])
        expected = baseline[..., :3] * np.exp(-(.006 + .04 * .75) * path)[..., None]
        error = float(np.max(np.abs(aerated[covered, :3] - expected[covered])))
        ok = T.check('advected bulk air adds extinction independently of surface foam',
                     error < .002 and np.all(aerated[covered, :3] <= suspended[covered, :3] + 1e-6),
                     f'max error {error:.3g}') and ok

        # Black bed and no base scattering: only the new, lit medium remains.
        rig.color = T.tex2d(*rig.size, T.RGBA16F, data=np.zeros((rig.size[1], rig.size[0], 4), dtype=np.float32))
        neutral = params.copy()
        neutral[2, :3] = 0
        neutral[3, :3] = 0
        neutral[9] = [1, 1, 1, 0]
        radiance = T.probe_program('vec4(transmitted, pathLength)')
        milk = draw(radiance, [1, 0, .04, 0], neutral)
        expected = np.array([.88, .90, .91]) * .5 * (1 - np.exp(-.04 * .75 * path))[..., None]
        error = float(np.max(np.abs(milk[covered, :3] - expected[covered])))
        ok = T.check('bubble plume scatters neutral incident light rather than emitting white paint',
                     error < .002, f'max error {error:.3g}') and ok
        T.gl('glUseProgram', None, T.U)(radiance)
        T.set_vec4s(radiance, 'u_WaterSpillway', [[0, 0, 1, 0], [0, 0, .2, 1], [0, 0, 0, 0]])
        lit = draw(radiance, [1, 0, .04, 0], neutral)
        T.set_vec4s(radiance, 'u_WaterSpillway', [[0, 0, 1, 0], [0, 0, .025, 1], [0, 0, 0, 0]])
        shadow = draw(radiance, [1, 0, .04, 0], neutral)
        error = float(np.max(np.abs(shadow[covered, :3] - lit[covered, :3] * .125)))
        ok = T.check('receiving-pool shadow also dims the bubble plume', error < .002,
                     f'max error {error:.3g}') and ok

        fresnel = T.probe_program('vec4(vec3(W), pathLength)')
        off = draw(fresnel, [0, 0, 0, 0])
        on = draw(fresnel, [1, .006, .04, 0])
        error = float(np.max(np.abs(off[covered, :3] - on[covered, :3])))
        ok = T.check('body medium leaves physical Fresnel weight unchanged', error < 1e-7,
                     f'max error {error:.3g}') and ok
        error = T.gl('glGetError', T.U)()
        ok = T.check('no GL error', error == 0, hex(error)) and ok
        for program in [transmission, radiance, fresnel]:
            T.gl('glDeleteProgram', None, T.U)(program)
    finally:
        T.SDL.SDL_GL_DeleteContext(context)
        T.SDL.SDL_DestroyWindow(window)
        T.SDL.SDL_Quit()
    print('OK' if ok else 'FAILED')
    return 0 if ok else 1


def bench():
    """Interleaved GPU draw batches isolate medium arithmetic from load/clock drift.

    This synthetic 1080p water scene excludes map capture, SSR and simulation.
    Use actual-map r_speeds reports to assess the whole water pass.
    """
    window, context = Q.context(b'Water body optics benchmark')
    try:
        T.UNITS.update({'u_WaterFoamMap': 18, 'u_WaterFoamPattern': 16})
        size = (1920, 1080)
        rig = T.Rig(T.Camera((1500., -300., 1000.), (1500., 300., 0.), size=size), size=size)
        params = rig.params()
        params[T.WATER_SLOTS['WATER_U_FOAM'], 0] = 1
        T.bind(16, T.tex2d(1, 1, T.RGBA16F, data=np.full((1, 1, 4), .5, np.float32)))
        T.bind(18, T.tex2d(1, 1, T.RGBA16F, data=np.array([[[0., 1., .75, 0.]]], np.float32)))
        program = T.water_program()
        rig.draw_water(program, params)
        # Preserve the scene setup and redraw the same visible surface without
        # readback/finish inside the measured batch. Equal depth must still pass.
        T.gl('glDepthFunc', None, T.U)(0x0203)  # GL_LEQUAL
        query = T.U()
        T.gl('glGenQueries', None, T.I, C.POINTER(T.U))(1, C.byref(query))
        times = {0: [], 1: []}
        for cycle in range(24):
            for enabled in (0, 1, 1, 0):
                T.set_vec4s(program, 'u_WaterBodyOptics', [[enabled, .004, .04, 0]])
                T.gl('glBeginQuery', None, T.U, T.U)(0x88BF, query)
                for _ in range(32):
                    T.gl('glDrawArrays', None, T.U, T.I, T.I)(4, 0, rig.water_count)
                T.gl('glEndQuery', None, T.U)(0x88BF)
                elapsed = C.c_uint64()
                T.gl('glGetQueryObjectui64v', None, T.U, T.U, C.POINTER(C.c_uint64))(
                    query, 0x8866, C.byref(elapsed))
                if cycle >= 4:
                    times[enabled].append(elapsed.value / 32e6)
        off, on = [statistics.median(times[k]) for k in (0, 1)]
        print(f'Synthetic 1920x1080 water draw, 40 batches of 32 draws per mode: '
              f'off {off:.4f} ms, on {on:.4f} ms, delta {on-off:+.4f} ms ({(on/off-1)*100:+.2f}%)')
        assert T.gl('glGetError', T.U)() == 0
        T.gl('glDeleteQueries', None, T.I, C.POINTER(T.U))(1, C.byref(query))
        T.gl('glDeleteProgram', None, T.U)(program)
    finally:
        T.SDL.SDL_GL_DeleteContext(context)
        T.SDL.SDL_DestroyWindow(window)
        T.SDL.SDL_Quit()
    return 0


if __name__ == '__main__':
    sys.exit(bench() if '--bench' in sys.argv else main())
