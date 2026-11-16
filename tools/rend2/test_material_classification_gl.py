"""Compile/link the semantic debug transport in a hidden GL context.

No drawing, screenshots, game launch or visual checks. Reuses existing test
header/context helpers; checks the real generic/lightall/sky/sprite sources.
"""
import ctypes as C
import re

from test_liquids_gl import context, close_context, render_header, stage, gl, U, I, ROOT


def compile_program(sources, label):
    program = gl('glCreateProgram', U)()
    shaders = []
    try:
        for kind, text in sources:
            shader = gl('glCreateShader', U, U)(kind)
            shaders.append(shader)
            src = C.c_char_p(text.encode())
            gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), C.c_void_p)(shader, 1, C.byref(src), None)
            gl('glCompileShader', None, U)(shader)
            ok, log = I(), C.create_string_buffer(32768)
            gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(ok))
            gl('glGetShaderInfoLog', None, U, I, C.c_void_p, C.c_void_p)(shader, len(log), None, log)
            assert ok.value, (label, log.value.decode())
            gl('glAttachShader', None, U, U)(program, shader)
        gl('glLinkProgram', None, U)(program)
        ok, log = I(), C.create_string_buffer(32768)
        gl('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8B82, C.byref(ok))
        gl('glGetProgramInfoLog', None, U, I, C.c_void_p, C.c_void_p)(program, len(log), None, log)
        assert ok.value, (label, log.value.decode())
        return program
    finally:
        for shader in shaders:
            gl('glDeleteShader', None, U)(shader)


def main():
    window, ctx = context(b'Material classification compile check')
    count = 0
    try:
        vertex_library = stage('leaf_flutter', 'Vertex') + stage('plant_skin', 'Vertex')
        sprite_library = stage('foliage_interact', 'Vertex')
        pbr = stage('pbr_common', 'Fragment')
        cases = [
            ('generic', [], vertex_library, ''),
            ('generic', ['USE_RGBAGEN', 'USE_TCGEN', 'USE_TCMOD'], vertex_library, ''),
            ('lightall', ['USE_LIGHTALL_DEBUG'], vertex_library, pbr),  # sky/unlit
            ('lightall', ['USE_LIGHTALL_DEBUG', 'USE_LIGHT', 'USE_LIGHTMAP'], vertex_library, pbr),
            ('lightall', ['USE_LIGHTALL_DEBUG', 'USE_LIGHT', 'USE_LIGHTMAP', 'USE_NORMALMAP',
                          'USE_SPECULARMAP'], vertex_library, pbr),
            ('lightall', ['USE_LIGHTALL_DEBUG', 'USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_NORMALMAP',
                          'USE_SPECULARMAP', 'USE_PHYSICAL_PBR'], vertex_library, pbr),
            ('surface_sprites', [], sprite_library, ''),
            ('surface_sprites', ['FACE_CAMERA', 'FX_SPRITE'], sprite_library, ''),
            ('surface_sprites', ['VELOCITY_PASS'], sprite_library, ''),
        ]
        for name, defines, vertex_lib, fragment_lib in cases:
            h = render_header(defines)
            ids = re.findall(r'^COMPARE_FEATURE\(\s*(RC_\w+)',
                             (ROOT / 'shared/rd-rend2/tr_compare.inc').read_text(), re.M)
            h += ''.join('#define {} {}\n'.format(key, i) for i, key in enumerate(ids))
            h += ('#define R_CompareOriginal(feature) (gl_FragCoord.x < u_CompareSplit.x && '
                  '(u_CompareMask[(feature) >> 5] & (1 << ((feature) & 31))) != 0)\n')
            program = compile_program([
                (0x8B31, h + vertex_lib + stage(name, 'Vertex')),
                (0x8B30, h + fragment_lib + stage(name, 'Fragment')),
            ], name + ' ' + ' '.join(defines))
            if 'VELOCITY_PASS' not in defines:
                assert gl('glGetUniformLocation', I, U, C.c_char_p)(program, b'u_MaterialDebug') >= 0, (name, defines)
            gl('glDeleteProgram', None, U)(program)
            count += 1
        print('Compiled and linked', count, 'material debug programs; no draws performed')
    finally:
        close_context(window, ctx)


if __name__ == '__main__':
    main()
