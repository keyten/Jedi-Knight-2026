"""Production surface-sprite compute checks on a hidden GL 4.3 context.

Checks exact lists/indirect commands against a CPU reference, bounded output
(all candidates choose one bin), offset/dispatch guards, perspective and light
frusta, disabled depth-clamp planes, fade, per-plant LOD and camera crossings.
Raster/velocity parity checks use the real surface_sprites vertex shader.
Run: python tools/rend2/test_sprite_cull_gl.py [--permutations]
Set SHIM_MCCOMPAT=0x800000001 to select the Optimus NVIDIA GPU.
"""
import ctypes as C
import math
import struct
import sys
import numpy as np
import test_foliage_gl as f
import test_gl43_compute as k

U, I, F, P = f.U, f.I, f.F, f.P
gl = f.gl
SSBO, INDIRECT = 0x90D2, 0x8F3F


def context():
    assert f.SDL.SDL_Init(32) == 0, f.SDL.SDL_GetError()
    for attr, value in ((17, 4), (18, 3), (21, 1)):
        f.SDL.SDL_GL_SetAttribute(attr, value)
    window = f.SDL.SDL_CreateWindow(b'Sprite culling', 0, 0, 32, 32, 10)
    assert window, f.SDL.SDL_GetError()
    ctx = f.SDL.SDL_GL_CreateContext(window)
    assert ctx, f.SDL.SDL_GetError()
    print(gl('glGetString', C.c_char_p, U)(0x1F01).decode(), '/', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
    return window, ctx


def planes(matrix, clamp=False):
    matrix = np.asarray(matrix, np.float32).reshape(4, 4).T
    result = []
    for p in range(6):
        v = matrix[3] + (1 if p % 2 == 0 else -1) * matrix[p // 2]
        v = v / np.linalg.norm(v[:3])
        if clamp and p >= 4:
            v = np.zeros(4, np.float32)
        result.append(v)
    return np.asarray(result, np.float32)


def reference(data, frustum, camera, lod, shape):
    result = [[] for _ in range(6)]
    for i, sp in enumerate(data):
        distance = np.linalg.norm(sp[:3] - camera)
        radius = np.linalg.norm(sp[13:16]) + abs(sp[13]) * shape[1] + abs(sp[12]) * (1 + abs(shape[2])) * shape[0] + 1
        if distance >= shape[3] or np.any(frustum @ np.r_[sp[:3], 1] < -radius):
            continue
        cards = int(lod[2])
        if cards == 3 and lod[3] > 0 and distance >= lod[3]:
            cards = 2
        strip = lod[1] > 0.5 and max(distance - radius, 0) < lod[0]
        result[(3 if strip else 0) + cards - 1].append(i)
    return result


def dispatch(prog, data, frustum, camera, lod, shape):
    n, first, output, command_base = len(data), 7, 13, 2
    src = np.zeros((n + first + 5, 16), np.float32)
    src[first:first+n] = data
    source = k.ssbo(src.tobytes(), 0)
    index_data = np.full(output + 6*n + 29, 0xBADF00D, np.uint32)
    indices = k.ssbo(index_data.tobytes(), 1)
    commands_data = np.full((command_base + 6 + 3, 5), 0xA11CE, np.uint32)
    for b in range(6):
        commands_data[command_base+b] = (2*(int(lod[1])+1) if b >= 3 else 6, 0, 0, 0, 0)
    commands = k.ssbo(commands_data.tobytes(), 2)
    gl('glUseProgram', None, U)(prog)
    for name, value in (('u_SpriteCullPlanes', frustum), ('u_SpriteCullRange', [[first, n, output, command_base]]),
                        ('u_SpriteCullLod', [lod]), ('u_SpriteCullShape', [shape])):
        k.uniform_vec4s(prog, name, value)
    gl('glUniform3fv', None, I, I, P)(f.uloc(prog, 'u_SpriteViewOrigin'), 1, np.asarray(camera, np.float32).ctypes.data)
    gl('glDispatchCompute', None, U, U, U)(math.ceil(n/64), 1, 1)
    gl('glMemoryBarrier', None, U)(0x40 | 0x1 | 0x2000)
    actual_indices = np.frombuffer(k.read_buffer(indices, index_data.nbytes), np.uint32)
    actual_commands = np.frombuffer(k.read_buffer(commands, commands_data.nbytes), np.uint32).reshape(-1, 5)
    assert np.all(actual_indices[:output] == 0xBADF00D) and np.all(actual_indices[output+6*n:] == 0xBADF00D)
    assert np.array_equal(actual_commands[:command_base], commands_data[:command_base])
    assert np.array_equal(actual_commands[command_base+6:], commands_data[command_base+6:])
    expected = reference(data, frustum, camera, lod, shape)
    for b in range(6):
        command = actual_commands[command_base+b]
        assert command[0] == commands_data[command_base+b, 0] and np.all(command[2:] == 0)
        count = int(command[1]) // (b % 3 + 1)
        assert command[1] == count * (b % 3 + 1) and count <= n
        listed = actual_indices[output+b*n:output+b*n+count].astype(np.int64) - first
        assert sorted(listed) == expected[b], (b, sorted(listed)[:20], expected[b][:20])
        assert np.all(actual_indices[output+b*n+count:output+(b+1)*n] == 0xBADF00D)
    assert gl('glGetError', U)() == 0
    gl('glDeleteBuffers', None, I, P)(3, (U*3)(source, indices, commands))
    return sum(map(len, expected)), [len(x) for x in expected]


def check_lists():
    prog = k.compute_program('sprite_cull')
    rng = np.random.default_rng(93812)
    data = np.zeros((4099, 16), np.float32)
    data[:, :3] = rng.uniform([-2400, -1600, -3000], [2400, 1600, 400], (len(data), 3))
    data[:, 3] = rng.uniform(0, 1, len(data))
    data[:, 4:6] = (1, 0)
    data[:, 8:11] = rng.uniform(0, 1, (len(data), 3))
    data[:, 12:16] = rng.uniform([2, 4, -20, -20], [48, 120, 20, 20], (len(data), 4))
    camera = np.zeros(3, np.float32)
    projection = k.perspective(90, 4/3, 4, 2400)
    frustum = planes(projection)
    shape = [0.75, 0, 0.5, 2100]
    for lod in ([1e30, 4, 3, 0], [600, 4, 3, 1100], [600, 4, 2, 0], [600, 0, 1, 0]):
        print('perspective', lod, dispatch(prog, data, frustum, camera, lod, shape))
    # Near-camera and near-plane intersections must survive even with their
    # anchors behind a plane. Fully faded plants are culled at the endpoint.
    boundary = data[:129].copy()
    boundary[:, :3] = (0, 0, -8)
    boundary[:6, :3] = [(0,0,1), (0,0,-3), (0,0,-4), (2100,0,0), (2099,0,0), (0,0,-2100)]
    print('near-camera/all-one-bin', dispatch(prog, boundary, frustum, camera, [1e30,8,3,0], shape))
    # A light view may see offscreen camera plants. Its projection changes
    # culling only: LOD/fade remain tied to the original camera.
    ortho = np.diag([1/1000, 1/1000, -1/4000, 1]).astype(np.float32).flatten()
    for clamp in (False, True):
        light = planes(ortho, clamp)
        print('light/depth-clamp', clamp, dispatch(prog, data, light, camera, [600,4,3,1100], shape))
    # All n sprites land in each possible bin in turn. An exact n-slot
    # capacity plus guard sentinels checks the worst case without truncation.
    packed = boundary.copy()
    packed[:, :3] = (0, 0, -10)
    empty_planes = np.zeros((6,4), np.float32)
    for cards in (1,2,3):
        for segments in (0,8):
            total, bins = dispatch(prog, packed, empty_planes, camera, [1e30,segments,cards,0], shape)
            assert total == len(packed) and max(bins) == len(packed)
    gl('glDeleteProgram', None, U)(prog)
    print('PASS bounded lists, command counts, offsets, fade, LOD and near-camera cases')


def check_sprite_permutations():
    count = 0
    for bits in range(512):
        if bits & 1 and bits & 2:
            continue
        if bits & 256 and bits & (1 | 2 | 4 | 16 | 32 | 64):
            continue
        defines = []
        if bits & 1: defines.append('FACE_CAMERA')
        elif bits & 2: defines.append('FACE_UP')
        elif bits & 64: defines.append('FACE_FLATTENED')
        for bit, name in ((4,'FX_SPRITE'),(8,'USE_FOG'),(32,'ADDITIVE_BLEND'),(128,'VELOCITY_PASS'),(256,'AUTO_GRASS')):
            if bits & bit: defines.append(name)
        program = f.build('surface_sprites', defines, f.stage('foliage_interact', 'vertex'),
                          f.stage('volumetric_common', 'fragment'), froxel=bool(bits & 8))
        gl('glDeleteProgram', None, U)(program)
        count += 1
    return count


def set4(program, name, value):
    loc = f.uloc(program, name)
    if loc >= 0:
        gl('glUniform4fv', None, I, I, P)(loc, 1, np.asarray(value, np.float32).ctypes.data)


def blocks(program):
    identity = np.eye(4, dtype=np.float32).flatten()
    values = {
        'Camera': {'u_viewProjectionMatrix': identity},
        'Scene': {'u_frameTime': [1.3], 'u_deltaTime': [1/60]},
        'SurfaceSprite': {'u_FadeStartDistance': [1000], 'u_FadeEndDistance': [3000],
                          'u_FadeScale': [.5], 'u_WindIdle': [2], 'u_FxDuration': [1000]},
        'TemporalInfo': {'u_previousViewProjectionMatrix': identity, 'u_previousFrameTime': [1.2]},
        'FoliageInteraction': {'u_FIField': [0,0,1/2048,1], 'u_FIFieldPrevious': [0,0,1/2048,1]},
    }
    buffers = []
    for binding, (block, fields) in enumerate(values.items()):
        index = gl('glGetUniformBlockIndex', U, U, C.c_char_p)(program, block.encode())
        if index == 0xFFFFFFFF:
            continue
        size = I()
        gl('glGetActiveUniformBlockiv', None, U, U, U, P)(program, index, 0x8A40, C.byref(size))
        data = bytearray(size.value)
        for name, value in fields.items():
            ui, offset = U(), I()
            names = (C.c_char_p*1)(name.encode())
            gl('glGetUniformIndices', None, U, I, P, P)(program, 1, names, C.byref(ui))
            if ui.value == 0xFFFFFFFF:
                continue
            gl('glGetActiveUniformsiv', None, U, I, P, U, P)(program, 1, C.byref(ui), 0x8A3B, C.byref(offset))
            struct.pack_into('<'+'f'*len(value), data, offset.value, *value)
        buffer = f.buffer(f.UNIFORM_BUFFER, np.frombuffer(data, np.uint8))
        gl('glUniformBlockBinding', None, U, U, U)(program, index, binding)
        gl('glBindBufferBase', None, U, U, U)(f.UNIFORM_BUFFER, binding, buffer)
        buffers.append(buffer)
    return buffers


def probe_program(defines):
    header = f.header(defines)
    program = f.compile_program([
        (f.GL_VERTEX, header+f.stage('foliage_interact','vertex')+f.stage('surface_sprites','vertex')),
        (f.GL_FRAGMENT, '#version 150 core\nout vec4 out_Color; void main(){out_Color=vec4(1);}')], 'sprite parity')
    names = [b'gl_Position', b'var_TexCoords', b'var_Color', b'var_Alpha']
    if 'VELOCITY_PASS' in defines: names.append(b'var_prevPosition')
    array = (C.c_char_p * len(names))(*names)
    gl('glTransformFeedbackVaryings', None, U, I, P, U)(program, len(names), array, 0x8C8C)
    gl('glLinkProgram', None, U)(program)
    linked = I()
    gl('glGetProgramiv', None, U, U, P)(program, 0x8B82, C.byref(linked))
    assert linked.value
    return program


def capture(program, data, cards, segments, path, velocity=False):
    n = len(data)
    gl('glUseProgram', None, U)(program)
    ubos = blocks(program)
    for name, value in (('u_SpriteData',11),('u_FoliageFieldMap',23),('u_FoliageFieldPrevMap',24)):
        gl('glUniform1i', None, I, I)(f.uloc(program,name), value)
    for name, value in (('u_SpriteViewOrigin',[0,0,0]), ('u_SpriteViewLeft',[.8,.6,0]), ('u_SpriteViewUp',[-.3,.4,.8660254])):
        gl('glUniform3fv', None, I, I, P)(f.uloc(program,name),1,np.asarray(value,np.float32).ctypes.data)
    set4(program,'u_AutoGrass',[3,0,0,.8])
    set4(program,'u_FoliageWind',[.8,.6,8,1.2])
    set4(program,'u_FoliageWindParams',[1,0,0,0])
    set4(program,'u_FoliageInteract',[1,0,0,0])
    set4(program,'u_GrassStrip',[segments,cards,path == 'list',path != 'old'])
    vao = f.vao()
    buffers = list(ubos)
    source_data = np.repeat(data,4,axis=0) if path == 'old' else data
    source = f.buffer(f.ARRAY_BUFFER,source_data)
    buffers.append(source)
    tex = U()
    if path == 'list':
        gl('glGenTextures',None,I,P)(1,C.byref(tex))
        f.bind(11,tex.value,f.TEXBUFFER)
        gl('glTexBuffer',None,U,U,U)(f.TEXBUFFER,f.RGBA32F,source)
        indices = f.buffer(f.ARRAY_BUFFER,np.arange(n,dtype=np.uint32))
        buffers.append(indices)
        gl('glEnableVertexAttribArray',None,U)(0)
        gl('glVertexAttribPointer',None,U,I,U,U,I,P)(0,1,0x1405,0,4,None)
        gl('glVertexAttribDivisor',None,U,U)(0,cards)
    else:
        stride = 256 if path == 'old' and segments else 64
        divisor = cards if path != 'old' or segments else 0
        for attr, size, offset in ((0,4,0),(7,3,16),(8,3,32),(12,4,48)):
            gl('glEnableVertexAttribArray',None,U)(attr)
            gl('glVertexAttribPointer',None,U,I,U,U,I,P)(attr,size,f.FLOAT,0,stride,P(offset))
            gl('glVertexAttribDivisor',None,U,U)(attr,divisor)
    count = 2*(segments+1) if segments else 6
    floats = 14 if velocity else 10
    output = f.buffer(0x8C8E,np.full((n*cards*count,floats),np.nan,np.float32))
    buffers.append(output)
    gl('glBindBufferBase',None,U,U,U)(0x8C8E,0,output)
    gl('glEnable',None,U)(0x8C89)
    gl('glBeginTransformFeedback',None,U)(0)
    if path == 'old' and not segments:
        indices = (np.arange(n,dtype=np.uint32)[:,None]*4 + np.array([0,1,2,0,2,3],np.uint32)).flatten()
        ibo = f.buffer(0x8893,indices)
        buffers.append(ibo)
        gl('glDrawElementsInstanced',None,U,I,U,P,I)(0,len(indices),0x1405,None,cards)
    elif not segments:
        ibo = f.buffer(0x8893,np.asarray([0,1,2,0,2,3],np.uint16))
        buffers.append(ibo)
        if path == 'list':
            command = f.buffer(INDIRECT,np.asarray([6,n*cards,0,0,0],np.uint32))
            buffers.append(command)
            gl('glDrawElementsIndirect',None,U,U,P)(0,0x1403,None)
        else:
            gl('glDrawElementsInstanced',None,U,I,U,P,I)(0,6,0x1403,None,n*cards)
    elif path == 'list':
        command = f.buffer(INDIRECT,np.asarray([count,n*cards,0,0,0],np.uint32))
        buffers.append(command)
        gl('glDrawArraysIndirect',None,U,P)(0,None)
    else:
        gl('glDrawArraysInstanced',None,U,I,I,I)(0,0,count,n*cards)
    gl('glEndTransformFeedback',None)()
    gl('glDisable',None,U)(0x8C89)
    gl('glBindBuffer',None,U,U)(0x8C8E,output)
    result = np.empty((n*cards*count,floats),np.float32)
    gl('glGetBufferSubData',None,U,C.c_ssize_t,C.c_ssize_t,P)(0x8C8E,0,result.nbytes,result.ctypes.data)
    assert np.all(np.isfinite(result)), (path,segments)
    assert gl('glGetError',U)() == 0, path
    gl('glDeleteBuffers',None,I,P)(len(buffers),(U*len(buffers))(*buffers))
    if tex.value: gl('glDeleteTextures',None,I,P)(1,C.byref(tex))
    gl('glDeleteVertexArrays',None,I,P)(1,C.byref(U(vao)))
    return np.asarray(sorted(map(tuple,result)),np.float32)


def check_pass_parity():
    rng = np.random.default_rng(34)
    data = np.zeros((29,16),np.float32)
    data[:,:3] = rng.uniform([-100,-100,-10],[100,100,10],(len(data),3))
    data[:,3] = rng.uniform(0,1,len(data))
    angles = rng.uniform(0,2*math.pi,len(data))
    data[:,4:6] = np.c_[np.cos(angles),np.sin(angles)]
    data[:,8:11] = rng.uniform(0,1,(len(data),3))
    data[:,12:16] = rng.uniform([2,8,-10,-10],[20,80,10,10],(len(data),4))
    data[:,8] = np.arange(len(data),dtype=np.float32)/len(data)
    data[::3,13] *= -1
    data[1::5,14:16] *= 8
    # Opposite current/previous fields test the full deformation envelope.
    for unit, displacement in ((23,[12,-9,0,0]),(24,[-11,8,0,0])):
        texture = f.texture(2,2,f.RGBA32F,f.RGBA,np.tile(displacement,(2,2,1)).astype(np.float32))
        f.bind(unit,texture)
    cases = [[],['FACE_CAMERA'],['FACE_FLATTENED'],['FACE_UP'],['AUTO_GRASS']]
    for defines in cases:
        for segments in (0,4):
            if 'FACE_UP' in defines and segments: continue
            cards = 3 if 'AUTO_GRASS' in defines else 1
            for velocity in (False,True):
                program = probe_program(defines+(['VELOCITY_PASS'] if velocity else []))
                old = capture(program,data,cards,segments,'old',velocity)
                compact = capture(program,data,cards,segments,'compact',velocity)
                listed = capture(program,data,cards,segments,'list',velocity)
                np.testing.assert_array_equal(old,compact)
                np.testing.assert_array_equal(old,listed)
                if 'FACE_UP' not in defines:
                    ids = np.rint(listed[:,6]*len(data)).astype(int)
                    records = data[ids]
                    shape_x = 1.2 if 'FACE_FLATTENED' in defines else (0.6 if 'AUTO_GRASS' in defines else 0.7)
                    shape_y = 1 if 'FACE_FLATTENED' in defines else (2 if 'FACE_CAMERA' in defines else 0)
                    radius = np.linalg.norm(records[:,13:16],axis=1) + abs(records[:,13])*shape_y + abs(records[:,12])*1.5*shape_x + 1
                    for xyz in (listed[:,:3],listed[:,-4:-1]) if velocity else (listed[:,:3],):
                        assert np.all(np.linalg.norm(xyz-records[:,:3],axis=1) <= radius + 1e-3), defines
                gl('glDeleteProgram',None,U)(program)
    print('PASS exact old/compact/list vertex, UV, color, alpha and velocity parity; current/previous deformation bounds')


def main():
    window, ctx = context()
    try:
        check_lists()
        check_pass_parity()
        if '--permutations' in sys.argv:
            print('compiled shader variants:', check_sprite_permutations())
        print('PASS')
    finally:
        f.SDL.SDL_GL_DeleteContext(ctx)
        f.SDL.SDL_DestroyWindow(window)
        f.SDL.SDL_Quit()

if __name__ == '__main__':
    main()
