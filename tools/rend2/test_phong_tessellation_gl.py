"""Compile character Phong material/depth/fog/velocity stages on a real GL 4.3 driver.

Also measures post-skinning geometry with transform feedback: curvature, the
displacement cap, shared-edge continuity and a rotated skeletal pose.
Run: python tools/rend2/test_phong_tessellation_gl.py
"""
import ctypes as C

import numpy as np

from lightrecon_regression import common_header
import test_foliage_gl as F
from test_water_tessellation_gl import context

gl, U, I, P = F.gl, F.U, F.I, F.P
TC, TE = 0x8E88, 0x8E87


def phong_stage(which):
    return F.read('phong').split(f'/*[{which}]*/')[1].split('/*[')[0]


def sources(name, defines=(), vertex=None, fragment=None):
    family = {'lightall': 'LIGHTALL', 'generic': 'GENERIC', 'fogpass': 'FOG',
              'velocity': 'VELOCITY', 'refraction': 'REFRACTION'}[name]
    h = F.header(list(defines) + [f'PHONG_{family}']).replace('#version 150', '#version 430')
    h += '\n'.join(line for line in common_header([]).splitlines()
                   if line.startswith('#define RC_') or line.startswith('#define R_CompareOriginal')) + '\n'
    vh = h + '#define USE_PHONG_TESSELLATION\n'
    # The same output renaming as GLSL_LoadPhongGPUShader.
    for varying in ('var_TexCoords', 'var_Color', 'var_LeafFlutter', 'var_Normal',
                    'var_Tangent', 'var_ViewDir', 'var_LightDir', 'var_Position',
                    'var_prevPosition', 'var_DiffuseTex', 'var_WSPosition',
                    'var_TangentViewDir', 'var_RefractPosR', 'var_RefractPosG', 'var_RefractPosB'):
        vh += f'#define {varying} phongVertex_{varying}\n'
    lib = F.foliage_vertex_library() if name != 'refraction' and vertex is None else ''
    fs_lib = F.stage('pbr_common', 'fragment') if name == 'lightall' else ''
    if name == 'refraction':
        fs_lib = F.stage('output_transform', 'fragment')
    interface = F.stage('phong_interface', 'vertex')
    fh = h
    fragment_body = fragment if fragment is not None else F.stage(name, 'fragment')
    if name in ('lightall', 'generic'):
        fh += '#define main PhongOriginalFragment\n'
        fragment_body += phong_stage('Fragment')
    return [(F.GL_VERTEX, vh + lib + phong_stage('Vertex')
             + (vertex if vertex is not None else F.stage(name, 'vertex'))),
            (TC, h + '#define PHONG_CONTROL\n' + interface + phong_stage('TessControl')),
            (TE, h + interface + phong_stage('TessEvaluation')),
            (F.GL_FRAGMENT, fh + fs_lib + fragment_body)]


def check_materials():
    variants = {
        'lightall': [[], ['USE_LIGHT', 'USE_LIGHT_VECTOR'],
                     ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_FAST_LIGHT'],
                     ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_NORMALMAP', 'USE_SPECULARMAP'],
                     ['USE_LIGHT', 'USE_LIGHT_VECTOR', 'USE_NORMALMAP', 'USE_PARALLAXMAP',
                      'USE_TCGEN', 'USE_TCMOD', 'USE_PHYSICAL_PBR']],
        'generic': [[], ['USE_FOG'], ['USE_RGBAGEN', 'USE_TCGEN', 'USE_TCMOD']],
        'fogpass': [[], ['USE_FALLBACK_GLOBAL_FOG']],
        'velocity': [[], ['USE_DEPTH_ONLY'], ['USE_PARALLAXMAP', 'USE_TCGEN', 'USE_TCMOD']],
        'refraction': [[], ['USE_TCGEN', 'USE_TCMOD']],
    }
    count = 0
    for name, cases in variants.items():
        for case in cases:
            for animation in ([], ['USE_SKELETAL_ANIMATION']):
                defines = case + animation
                program = F.compile_program(sources(name, defines), f'{name} Phong {defines}')
                gl('glDeleteProgram', None, U)(program)
                count += 1
    print(f'PASS: {count} material/depth/velocity/fog/refraction permutations compile and link')


def geometry_program(skeletal):
    # Minimal base VS, using the production wrapper, interface, TCS and TES.
    vertex = """
in vec3 attr_Position;
in vec3 attr_Normal;
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
uniform mat4 u_ModelMatrix;
uniform mat3x4 u_BoneMatrices[72];
out vec3 var_WSPosition;
out vec2 var_TexCoords;
void main()
{
    vec3 position = attr_Position;
    vec3 normal = attr_Normal - vec3(0.5);
#if defined(USE_SKELETAL_ANIMATION)
    mat3x4 bone = u_BoneMatrices[0];
    mat4x3 skin = transpose(bone);
    position = skin * vec4(position, 1.0);
    normal = skin * vec4(normal, 0.0);
#endif
    PhongControlPoint(position, normal, vec2(0.0));
    var_TexCoords = vec2(0.0);
    var_WSPosition = attr_Position;
    gl_Position = vec4(attr_Position, 1.0);
}
"""
    program = gl('glCreateProgram', U)()
    for kind, source in sources('fogpass', ['USE_SKELETAL_ANIMATION'] if skeletal else [], vertex,
                                 'out vec4 out_Color; void main() { out_Color=vec4(1.0); }'):
        shader = gl('glCreateShader', U, U)(kind)
        data = C.c_char_p(source.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(data), None)
        gl('glCompileShader', None, U)(shader)
        status = I()
        gl('glGetShaderiv', None, U, U, C.POINTER(I))(shader, 0x8B81, C.byref(status))
        log = C.create_string_buffer(8192)
        gl('glGetShaderInfoLog', None, U, I, P, P)(shader, len(log), None, log)
        assert status.value, (kind, log.value)
        gl('glAttachShader', None, U, U)(program, shader)
        gl('glDeleteShader', None, U)(shader)
    for i, attr in enumerate(F.ATTRIBUTES):
        gl('glBindAttribLocation', None, U, U, C.c_char_p)(program, i, attr.encode())
    capture = (C.c_char_p * 2)(b'gl_Position', b'var_WSPosition')
    gl('glTransformFeedbackVaryings', None, U, I, C.POINTER(C.c_char_p), U)(program, 2, capture, 0x8C8C)
    gl('glLinkProgram', None, U)(program)
    status = I()
    gl('glGetProgramiv', None, U, U, C.POINTER(I))(program, 0x8B82, C.byref(status))
    log = C.create_string_buffer(8192)
    gl('glGetProgramInfoLog', None, U, I, P, P)(program, len(log), None, log)
    assert status.value, log.value
    return program


def check_geometry():
    vao = U()
    gl('glGenVertexArrays', None, I, C.POINTER(U))(1, C.byref(vao))
    gl('glBindVertexArray', None, U)(vao)
    # Two adjacent triangles, curved normals on a flat base square.
    positions = np.array([[-.6, -.6, 0], [.6, -.6, 0], [.6, .6, 0],
                          [-.6, -.6, 0], [.6, .6, 0], [-.6, .6, 0]], np.float32)
    normals = np.column_stack((positions[:, :2], np.ones(6))).astype(np.float32)
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    mesh = np.column_stack((positions, normals * .5 + .5, np.ones(6))).astype(np.float32)
    mesh_buffer = F.buffer(0x8892, mesh)
    for index, offset in ((0, 0), (7, 12)):
        gl('glEnableVertexAttribArray', None, U)(index)
        gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(index, 3, 0x1406, 0, 28, P(offset))
    gl('glEnableVertexAttribArray', None, U)(12)
    gl('glVertexAttribPointer', None, U, I, U, C.c_ubyte, I, P)(12, 1, 0x1406, 0, 28, P(24))
    gl('glVertexAttribI4ui', None, U, U, U, U, U)(10, 0, 0, 0, 0)
    gl('glVertexAttrib4f', None, U, C.c_float, C.c_float, C.c_float, C.c_float)(11, 1, 0, 0, 0)
    result = np.zeros((4096, 7), np.float32)
    feedback = F.buffer(0x8C8E, result)
    gl('glBindBufferBase', None, U, U, U)(0x8C8E, 0, feedback)
    query = U()
    gl('glGenQueries', None, I, C.POINTER(U))(1, C.byref(query))

    def draw(program, strength, cap, rotate=False):
        gl('glUseProgram', None, U)(program)
        loc = lambda n: gl('glGetUniformLocation', I, U, C.c_char_p)(program, n.encode())
        identity = np.eye(4, dtype=np.float32)
        for name in ('u_ModelMatrix', 'u_PhongViewProjection'):
            gl('glUniformMatrix4fv', None, I, I, C.c_ubyte, P)(loc(name), 1, 0, identity.ctypes.data)
        bone = np.array([[1, 0, 0, .1], [0, 1, 0, -.2], [0, 0, 1, 0]], np.float32)
        if rotate:
            bone[:, :3] = [[1, 0, 0], [0, 0, -1], [0, 1, 0]]
        gl('glUniformMatrix3x4fv', None, I, I, C.c_ubyte, P)(loc('u_BoneMatrices'), 1, 0, bone.ctypes.data)
        gl('glUniform4f', None, I, C.c_float, C.c_float, C.c_float, C.c_float)(loc('u_PhongParams'), 4, strength, cap, 0)
        gl('glEnable', None, U)(0x8C89)  # rasterizer discard
        gl('glBeginQuery', None, U, U)(0x8C88, query)
        gl('glBeginTransformFeedback', None, U)(4)
        gl('glDrawArrays', None, U, I, I)(0x000E, 0, 6)
        gl('glEndTransformFeedback', None)()
        gl('glEndQuery', None, U)(0x8C88)
        count = U()
        gl('glGetQueryObjectuiv', None, U, U, C.POINTER(U))(query, 0x8866, C.byref(count))
        gl('glGetBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(0x8C8E, 0, result.nbytes, result.ctypes.data)
        gl('glDisable', None, U)(0x8C89)
        assert count.value > 2, count.value
        return result[:count.value * 3].copy()

    static = geometry_program(False)
    flat = draw(static, 0, 1)
    curved = draw(static, 1, 1)
    capped = draw(static, 1, .02)
    assert np.max(np.abs(flat[:, 2])) < 1e-7
    assert np.max(np.abs(curved[:, 2])) > .05
    assert np.max(np.linalg.norm(capped[:, :3] - flat[:, :3], axis=1)) <= .02001
    assert np.max(np.abs(curved[:, :3] - curved[:, 4:7])) < 1e-6
    # Both copies of the shared edge must have identical displaced points.
    for point in curved[np.abs(curved[:, 0] - curved[:, 1]) < 1e-6, :3]:
        assert np.count_nonzero(np.linalg.norm(curved[:, :3] - point, axis=1) < 1e-6) >= 2
    skinned = draw(geometry_program(True), 1, 1)
    # Translation of the skeleton must leave the tangent-plane displacement unchanged.
    assert np.max(np.abs(skinned[:, :3] - curved[:, :3])) < 1e-6
    rotated = draw(geometry_program(True), 1, 1, rotate=True)
    rotation = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]])
    assert np.max(np.abs((rotated[:, :3] - flat[:, :3]) -
                         (curved[:, :3] - flat[:, :3]) @ rotation.T)) < 1e-6
    # Reproduce a hard-normal / material seam. The old projection opens the
    # shared diagonal; crease weights must keep both copies exactly on it.
    mesh[3:, 3:6] = (np.array([0.0, 0.0, 1.0]) * .5 + .5)
    def upload():
        gl('glBindBuffer', None, U, U)(0x8892, mesh_buffer)
        gl('glBufferSubData', None, U, C.c_ssize_t, C.c_ssize_t, P)(0x8892, 0, mesh.nbytes, mesh.ctypes.data)
    upload()
    broken = draw(static, 1, 1)
    edge = np.abs(flat[:, 0] - flat[:, 1]) < 1e-6
    assert np.max(np.abs(broken[edge, 2])) > .05, 'negative control did not reproduce the seam'
    mesh[[0, 2, 3, 4], 6] = 0
    upload()
    watertight = draw(static, 1, 1)
    assert np.max(np.abs(watertight[edge, :3] - flat[edge, :3])) < 1e-6
    mesh[:, 6] = 0
    upload()
    planar = draw(static, 1, 1)
    assert len(planar) < len(flat), 'fully preserved patches did not reduce interior tessellation'
    assert np.max(np.abs(planar[:, 2])) < 1e-6
    assert gl('glGetError', U)() == 0
    print('PASS: subdivision, curvature, cap, shared edges, skinning, hard-normal seam regression')


def main():
    window, ctx = context(b'Character Phong tessellation')
    try:
        check_materials()
        check_geometry()
    finally:
        F.SDL.SDL_GL_DeleteContext(ctx)
        F.SDL.SDL_DestroyWindow(window)
        F.SDL.SDL_Quit()


if __name__ == '__main__':
    main()
