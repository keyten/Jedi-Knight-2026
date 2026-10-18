/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
// rend2_glslc.cpp -- links GLSL programs for rend2 in a separate process

/*
Drivers that serialize GLSL compilation inside a process (the Intel Windows
driver: several threads with shared contexts are slower than one) still
compile in parallel in separate processes. rend2 (tr_glslhelper.cpp) starts
a few of these helpers for a batch of programs, sends each the stage sources
and interface bindings of a program, and loads the returned program binary
with glProgramBinary, as a disk cache hit. Protocol: glslc_protocol.h.
*/

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define GLSLC_APIENTRY __stdcall
#else
#define GLSLC_APIENTRY
#endif

#include "glslc_protocol.h"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
typedef unsigned char GLubyte;

#define GL_VENDOR							0x1F00
#define GL_RENDERER							0x1F01
#define GL_VERSION							0x1F02
#define GL_COMPILE_STATUS					0x8B81
#define GL_LINK_STATUS						0x8B82
#define GL_INFO_LOG_LENGTH					0x8B84
#define GL_PROGRAM_BINARY_LENGTH			0x8741
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT	0x8257
#define GL_INTERLEAVED_ATTRIBS				0x8C8C

static struct
{
	const GLubyte *(GLSLC_APIENTRY *GetString)( GLenum name );
	GLuint (GLSLC_APIENTRY *CreateShader)( GLenum type );
	void (GLSLC_APIENTRY *ShaderSource)( GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths );
	void (GLSLC_APIENTRY *CompileShader)( GLuint shader );
	void (GLSLC_APIENTRY *GetShaderiv)( GLuint shader, GLenum name, GLint *value );
	void (GLSLC_APIENTRY *GetShaderInfoLog)( GLuint shader, GLsizei size, GLsizei *length, GLchar *log );
	void (GLSLC_APIENTRY *DeleteShader)( GLuint shader );
	GLuint (GLSLC_APIENTRY *CreateProgram)( void );
	void (GLSLC_APIENTRY *AttachShader)( GLuint program, GLuint shader );
	void (GLSLC_APIENTRY *BindAttribLocation)( GLuint program, GLuint index, const GLchar *name );
	void (GLSLC_APIENTRY *BindFragDataLocation)( GLuint program, GLuint index, const GLchar *name );
	void (GLSLC_APIENTRY *TransformFeedbackVaryings)( GLuint program, GLsizei count, const GLchar *const *names, GLenum mode );
	void (GLSLC_APIENTRY *ProgramParameteri)( GLuint program, GLenum name, GLint value );
	void (GLSLC_APIENTRY *LinkProgram)( GLuint program );
	void (GLSLC_APIENTRY *GetProgramiv)( GLuint program, GLenum name, GLint *value );
	void (GLSLC_APIENTRY *GetProgramInfoLog)( GLuint program, GLsizei size, GLsizei *length, GLchar *log );
	void (GLSLC_APIENTRY *GetProgramBinary)( GLuint program, GLsizei size, GLsizei *length, GLenum *format, void *binary );
	void (GLSLC_APIENTRY *DeleteProgram)( GLuint program );
} gl;

static bool LoadFunctions( void )
{
	struct { void **pointer; const char *name; } functions[] = {
		{ (void **)&gl.GetString, "glGetString" },
		{ (void **)&gl.CreateShader, "glCreateShader" },
		{ (void **)&gl.ShaderSource, "glShaderSource" },
		{ (void **)&gl.CompileShader, "glCompileShader" },
		{ (void **)&gl.GetShaderiv, "glGetShaderiv" },
		{ (void **)&gl.GetShaderInfoLog, "glGetShaderInfoLog" },
		{ (void **)&gl.DeleteShader, "glDeleteShader" },
		{ (void **)&gl.CreateProgram, "glCreateProgram" },
		{ (void **)&gl.AttachShader, "glAttachShader" },
		{ (void **)&gl.BindAttribLocation, "glBindAttribLocation" },
		{ (void **)&gl.BindFragDataLocation, "glBindFragDataLocation" },
		{ (void **)&gl.TransformFeedbackVaryings, "glTransformFeedbackVaryings" },
		{ (void **)&gl.ProgramParameteri, "glProgramParameteri" },
		{ (void **)&gl.LinkProgram, "glLinkProgram" },
		{ (void **)&gl.GetProgramiv, "glGetProgramiv" },
		{ (void **)&gl.GetProgramInfoLog, "glGetProgramInfoLog" },
		{ (void **)&gl.GetProgramBinary, "glGetProgramBinary" },
		{ (void **)&gl.DeleteProgram, "glDeleteProgram" },
	};
	for ( const auto& function : functions )
	{
		*function.pointer = SDL_GL_GetProcAddress(function.name);
		if ( !*function.pointer )
		{
			fprintf(stderr, "rend2_glslc: no %s\n", function.name);
			return false;
		}
	}
	return true;
}

// message writing / reading over stdout / stdin

static void PutU32( std::string& out, uint32_t value )
{
	out.append((const char *)&value, sizeof(value));
}

static void PutString( std::string& out, const char *text, size_t length )
{
	PutU32(out, (uint32_t)length);
	out.append(text, length);
}

static bool Send( uint32_t magic, const std::string& payload )
{
	const uint32_t header[2] = { magic, (uint32_t)payload.size() };
	return fwrite(header, sizeof(header), 1, stdout) == 1 &&
		(payload.empty() || fwrite(payload.data(), payload.size(), 1, stdout) == 1) &&
		fflush(stdout) == 0;
}

struct Reader
{
	const std::string& data;
	size_t offset;
	bool ok;

	uint32_t U32()
	{
		uint32_t value = 0;
		if ( data.size() - offset < sizeof(value) )
		{
			ok = false;
			return 0;
		}
		memcpy(&value, data.data() + offset, sizeof(value));
		offset += sizeof(value);
		return value;
	}

	uint64_t U64()
	{
		const uint64_t low = U32();
		return low | ((uint64_t)U32() << 32);
	}

	std::string String()
	{
		const uint32_t length = U32();
		if ( !ok || data.size() - offset < length )
		{
			ok = false;
			return std::string();
		}
		std::string value(data.data() + offset, length);
		offset += length;
		return value;
	}
};

static std::string ShaderLog( GLuint shader )
{
	GLint length = 0;
	gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
	std::string log((size_t)(length > 0 ? length : 1), '\0');
	gl.GetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, &log[0]);
	return log;
}

static std::string ProgramLog( GLuint program )
{
	GLint length = 0;
	gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
	std::string log((size_t)(length > 0 ? length : 1), '\0');
	gl.GetProgramInfoLog(program, (GLsizei)log.size(), nullptr, &log[0]);
	return log;
}

// one GLSLC_JOB: compile, bind the interface as ShaderProgramBuilder::Build
// does, link, return the binary
static std::string Link( const std::string& request )
{
	Reader in = { request, 0, true };
	const uint64_t key = in.U64();
	const uint32_t flags = in.U32();

	std::vector<std::pair<uint32_t, std::string>> attribs, outputs;
	for ( uint32_t i = 0, n = in.U32(); in.ok && i < n; ++i )
	{
		const uint32_t index = in.U32();
		attribs.emplace_back(index, in.String());
	}
	for ( uint32_t i = 0, n = in.U32(); in.ok && i < n; ++i )
	{
		const uint32_t index = in.U32();
		outputs.emplace_back(index, in.String());
	}
	std::vector<std::string> varyings;
	for ( uint32_t i = 0, n = in.U32(); in.ok && i < n; ++i )
		varyings.push_back(in.String());
	std::vector<std::pair<uint32_t, std::string>> stages;
	for ( uint32_t i = 0, n = in.U32(); in.ok && i < n; ++i )
	{
		const uint32_t type = in.U32();
		stages.emplace_back(type, in.String());
	}

	std::string out;
	PutU32(out, (uint32_t)key);
	PutU32(out, (uint32_t)(key >> 32));
	if ( !in.ok )
	{
		PutU32(out, GLSLC_COMPILE_FAILED);
		PutU32(out, 0);
		const char error[] = "rend2_glslc: malformed job";
		PutString(out, error, sizeof(error) - 1);
		return out;
	}

	const GLuint program = gl.CreateProgram();
	std::vector<GLuint> shaders;
	uint32_t status = GLSLC_OK;
	std::string log;
	for ( const auto& stage : stages )
	{
		const GLuint shader = gl.CreateShader(stage.first);
		const GLchar *source = stage.second.c_str();
		const GLint length = (GLint)stage.second.size();
		gl.ShaderSource(shader, 1, &source, &length);
		gl.CompileShader(shader);
		GLint compiled = 0;
		gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		shaders.push_back(shader);
		if ( !compiled )
		{
			status = GLSLC_COMPILE_FAILED;
			log = ShaderLog(shader);
			break;
		}
		gl.AttachShader(program, shader);
	}

	if ( status == GLSLC_OK )
	{
		for ( const auto& attrib : attribs )
			gl.BindAttribLocation(program, attrib.first, attrib.second.c_str());
		for ( const auto& output : outputs )
			gl.BindFragDataLocation(program, output.first, output.second.c_str());
		if ( !varyings.empty() )
		{
			std::vector<const GLchar *> names;
			for ( const auto& varying : varyings )
				names.push_back(varying.c_str());
			gl.TransformFeedbackVaryings(program, (GLsizei)names.size(), names.data(), GL_INTERLEAVED_ATTRIBS);
		}
		if ( flags & GLSLC_RETRIEVABLE )
			gl.ProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, 1);
		gl.LinkProgram(program);
		GLint linked = 0;
		gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
		if ( !linked )
		{
			status = GLSLC_LINK_FAILED;
			log = ProgramLog(program);
		}
	}

	std::string binary;
	GLenum format = 0;
	if ( status == GLSLC_OK )
	{
		GLint length = 0;
		gl.GetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
		if ( length > 0 )
		{
			binary.resize((size_t)length);
			GLsizei written = 0;
			gl.GetProgramBinary(program, length, &written, &format, &binary[0]);
			binary.resize(written > 0 ? (size_t)written : 0);
		}
		if ( binary.empty() )
			status = GLSLC_NO_BINARY;
	}

	for ( GLuint shader : shaders )
		gl.DeleteShader(shader);
	gl.DeleteProgram(program);

	PutU32(out, status);
	PutU32(out, format);
	if ( status == GLSLC_OK )
		PutString(out, binary.data(), binary.size());
	else
		PutString(out, log.data(), log.size());
	return out;
}

int main( int argc, char **argv )
{
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	int major = 3, minor = 2;
	bool debug = false;
	for ( int i = 1; i < argc; ++i )
	{
		if ( !strcmp(argv[i], "--major") && i + 1 < argc )
			major = atoi(argv[++i]);
		else if ( !strcmp(argv[i], "--minor") && i + 1 < argc )
			minor = atoi(argv[++i]);
		else if ( !strcmp(argv[i], "--debug") )
			debug = true;
	}

	SDL_SetMainReady();
	if ( SDL_Init(SDL_INIT_VIDEO) != 0 )
	{
		fprintf(stderr, "rend2_glslc: %s\n", SDL_GetError());
		return 1;
	}
	// the context the renderer asks for (tr_init.cpp, InitOpenGL)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_Window *window = SDL_CreateWindow("rend2_glslc", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
	if ( !context || !LoadFunctions() )
	{
		fprintf(stderr, "rend2_glslc: no OpenGL %d.%d context: %s\n", major, minor, SDL_GetError());
		return 1;
	}

	std::string hello;
	for ( GLenum name : { GL_VENDOR, GL_RENDERER, GL_VERSION } )
	{
		const char *text = (const char *)gl.GetString(name);
		if ( !text )
			text = "";
		PutString(hello, text, strlen(text));
	}
	if ( !Send(GLSLC_HELLO, hello) )
		return 1;

	std::string request;
	for ( ;; )
	{
		uint32_t header[2];
		if ( fread(header, sizeof(header), 1, stdin) != 1 )
			break;	// the renderer closed the pipe
		if ( header[0] != GLSLC_JOB || header[1] > GLSLC_MAX_MESSAGE )
			return 1;
		request.resize(header[1]);
		if ( header[1] && fread(&request[0], header[1], 1, stdin) != 1 )
			return 1;
		if ( !Send(GLSLC_RESULT, Link(request)) )
			return 1;
	}

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
