// Context negotiation belongs to the renderer so older engines can load it.
#include "tr_local.h"
#include <SDL.h>

void GLimp_ConfigureContext(windowDesc_t *desc)
{
	if (desc->gl.majorVersion != 4 || desc->gl.minorVersion != 3)
		return;

	// WIN_Init in older engines fails fatally, so probe before calling it.
	// No probe GL objects or function pointers survive into the real context.
	SDL_Window *previousWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext previousContext = SDL_GL_GetCurrentContext();
	const bool ownsVideo = !SDL_WasInit(SDL_INIT_VIDEO);
	bool supported = false;
	if (!ownsVideo || SDL_InitSubSystem(SDL_INIT_VIDEO) == 0)
	{
		SDL_GL_ResetAttributes();
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
			(desc->gl.contextFlags & GLCONTEXT_DEBUG) ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
		SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
		SDL_Window *probeWindow = SDL_CreateWindow("Rend2 OpenGL probe",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 32, 32,
			SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
		if (probeWindow)
		{
			SDL_GLContext probeContext = SDL_GL_CreateContext(probeWindow);
			if (probeContext)
			{
				// A successful request should already guarantee this. Verify the
				// actual context in case a driver silently supplies an older one.
				typedef void (APIENTRY *GetIntegerProc)(GLenum, GLint *);
				GetIntegerProc getInteger = (GetIntegerProc)SDL_GL_GetProcAddress("glGetIntegerv");
				GLint major = 0, minor = 0, profile = 0;
				if (getInteger)
				{
					getInteger(GL_MAJOR_VERSION, &major);
					getInteger(GL_MINOR_VERSION, &minor);
					getInteger(GL_CONTEXT_PROFILE_MASK, &profile);
					supported = (major > 4 || (major == 4 && minor >= 3)) &&
						(profile & GL_CONTEXT_CORE_PROFILE_BIT);
				}
				SDL_GL_DeleteContext(probeContext);
			}
			else
				ri.Printf(PRINT_ALL, "OpenGL 4.3 probe failed: %s\n", SDL_GetError());
			SDL_DestroyWindow(probeWindow);
		}
		else
			ri.Printf(PRINT_ALL, "OpenGL 4.3 probe window failed: %s\n", SDL_GetError());

		if (previousContext && SDL_GL_MakeCurrent(previousWindow, previousContext) != 0)
			ri.Printf(PRINT_WARNING, "Could not restore OpenGL context after probe: %s\n", SDL_GetError());
		SDL_GL_ResetAttributes();
		if (ownsVideo)
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
	else
		ri.Printf(PRINT_ALL, "OpenGL 4.3 probe video initialization failed: %s\n", SDL_GetError());

	if (supported)
		ri.Printf(PRINT_ALL, "Renderer probe: OpenGL 4.3 Core available.\n");
	else
	{
		desc->gl.majorVersion = 3;
		desc->gl.minorVersion = 2;
		ri.Printf(PRINT_ALL, "Renderer probe: falling back to OpenGL 3.2 Core.\n");
	}
}
