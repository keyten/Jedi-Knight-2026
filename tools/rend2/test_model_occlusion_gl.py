"""Hidden OpenGL checks of current-frame depth-query / no-wait draw semantics.

Uses the renderer's GL_SAMPLES_PASSED + GL_QUERY_NO_WAIT protocol. Test-only
readback waits make the skipped-work assertion deterministic; production never
waits and uses no past-frame visibility. Run without a visible game window.
"""
import ctypes as C
import json
from test_volumetric_compute import SDL, U, I, F, P, gl


def compile_program():
    program = gl('glCreateProgram', U)()
    sources = [(0x8B31, """#version 150 core
uniform vec4 bounds;
uniform float depth, slope;
void main() {
 vec2 p[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
 vec2 xy = mix(bounds.xy, bounds.zw, p[gl_VertexID]);
 gl_Position = vec4(xy, depth + xy.x * slope, 1);
}"""), (0x8B30, """#version 150 core
uniform vec4 color;
out vec4 out_Color;
void main() { out_Color = color; }
""")]
    for kind, source in sources:
        shader = gl('glCreateShader', U, U)(kind)
        src = C.c_char_p(source.encode())
        gl('glShaderSource', None, U, I, C.POINTER(C.c_char_p), P)(shader, 1, C.byref(src), None)
        gl('glCompileShader', None, U)(shader)
        ok = I()
        gl('glGetShaderiv', None, U, U, P)(shader, 0x8B81, C.byref(ok))
        assert ok.value, 'compile failed'
        gl('glAttachShader', None, U, U)(program, shader)
        gl('glDeleteShader', None, U)(shader)
    gl('glLinkProgram', None, U)(program)
    ok = I()
    gl('glGetProgramiv', None, U, U, P)(program, 0x8B82, C.byref(ok))
    assert ok.value, 'link failed'
    gl('glUseProgram', None, U)(program)
    return program


def uniform(program, name, values):
    loc = gl('glGetUniformLocation', I, U, C.c_char_p)(program, name.encode())
    gl('glUniform%dfv' % len(values), None, I, I, P)(loc, 1, (F * len(values))(*values))


def run():
    assert SDL.SDL_Init(32) == 0, SDL.SDL_GetError()
    SDL.SDL_GL_SetAttribute(17, 3)
    SDL.SDL_GL_SetAttribute(18, 3)
    SDL.SDL_GL_SetAttribute(21, 1)
    win = SDL.SDL_CreateWindow(b'current frame model occlusion test', 0, 0, 32, 32, 10)
    assert win, SDL.SDL_GetError()
    ctx = SDL.SDL_GL_CreateContext(win)
    assert ctx, SDL.SDL_GetError()
    try:
        renderer = gl('glGetString', C.c_char_p, U)(0x1F01).decode()
        version = gl('glGetString', C.c_char_p, U)(0x1F02).decode()
        vao = U()
        gl('glGenVertexArrays', None, I, P)(1, C.byref(vao))
        gl('glBindVertexArray', None, U)(vao)
        program = compile_program()
        gl('glViewport', None, I, I, I, I)(0, 0, 32, 32)
        gl('glEnable', None, U)(0x0B71)
        gl('glDepthFunc', None, U)(0x0203)  # LEQUAL, including coplanar surfaces
        gl('glDisable', None, U)(0x0BE2)
        queries = (U * 2)()
        gl('glGenQueries', None, I, P)(2, queries)
        results = []
        cases = [
            ('fully_hidden', (-1,-1,1,1), -0.5, 0.5, 0, 0),
            ('partial', (-1,-1,0,1), -0.5, 0.5, 0, 1),
            ('visible', (-1,-1,1,1), 0.5, -0.5, 0, 1),
            ('coplanar', (-1,-1,1,1), 0.5, 0.5, 0, 1),
            ('near_intersection', (-1,-1,0,1), 0.5, -1, 1.0, 1),
            ('hidden_after_cut', (-1,-1,1,1), -0.5, 0.5, 0, 0),
            ('visible_after_cut', (-1,-1,1,1), 0.5, -0.5, 0, 1),
        ]
        for name, wall, wall_depth, model_depth, slope, expected in cases:
            images = []
            for conditional in (False, True):
                gl('glColorMask', None, U, U, U, U)(1,1,1,1)
                gl('glDepthMask', None, U)(1)
                gl('glClearColor', None, F,F,F,F)(0,0,0,1)
                gl('glClearDepth', None, C.c_double)(1)
                gl('glClear', None, U)(0x4100)
                uniform(program, 'bounds', wall)
                uniform(program, 'depth', [wall_depth])
                uniform(program, 'slope', [0])
                uniform(program, 'color', [0,1,0,1])
                gl('glDrawArrays', None, U,I,I)(4,0,6)
                uniform(program, 'bounds', [-0.75,-0.75,0.75,0.75])
                uniform(program, 'depth', [model_depth])
                uniform(program, 'slope', [slope])
                gl('glColorMask', None, U,U,U,U)(0,0,0,0)
                gl('glBeginQuery', None, U,U)(0x8914, queries[0])
                gl('glDrawArrays', None, U,I,I)(4,0,6)
                gl('glEndQuery', None, U)(0x8914)
                # Only this test waits; verifies deterministic suppression of VS work.
                gl('glFinish', None)()
                visible = U()
                gl('glGetQueryObjectuiv', None, U,U,P)(queries[0],0x8866,C.byref(visible))
                assert bool(visible.value) == bool(expected), (name, visible.value)
                gl('glColorMask', None, U,U,U,U)(1,1,1,1)
                uniform(program, 'color', [1,0,0,1])
                gl('glBeginQuery', None, U,U)(0x8C87, queries[1]) # PRIMITIVES_GENERATED
                if conditional:
                    gl('glBeginConditionalRender', None, U,U)(queries[0],0x8E14) # QUERY_NO_WAIT
                gl('glDrawArrays', None, U,I,I)(4,0,6)
                if conditional:
                    gl('glEndConditionalRender', None)()
                gl('glEndQuery', None, U)(0x8C87)
                primitives = U()
                gl('glGetQueryObjectuiv', None, U,U,P)(queries[1],0x8866,C.byref(primitives))
                assert primitives.value == (2 if not conditional or expected else 0), (name, primitives.value)
                pixels = (C.c_ubyte * (32*32*4))()
                gl('glReadPixels', None,I,I,I,I,U,U,P)(0,0,32,32,0x1908,0x1401,pixels)
                images.append(bytes(pixels))
            assert images[0] == images[1], (name, 'image changed')
            assert gl('glGetError', U)() == 0, name
            results.append(dict(case=name, visible=expected, matched=True, conditional_primitives=2 if expected else 0))
        print(json.dumps(dict(renderer=renderer, version=version, cases=results), indent=2))
    finally:
        SDL.SDL_GL_DeleteContext(ctx)
        SDL.SDL_DestroyWindow(win)
        SDL.SDL_Quit()


if __name__ == '__main__':
    run()
