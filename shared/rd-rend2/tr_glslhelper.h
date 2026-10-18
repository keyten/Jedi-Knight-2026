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
// tr_glslhelper.h -- GLSL programs linked in rend2_glslc processes

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// one program for a helper: what ShaderProgramBuilder::Build would compile
struct glslHelperJob_t
{
	uint64_t key;			// program cache key
	std::vector<std::pair<uint32_t, std::string>> attribs;	// glBindAttribLocation
	std::vector<std::pair<uint32_t, std::string>> outputs;	// glBindFragDataLocation
	std::vector<std::string> varyings;						// interleaved transform feedback
	std::vector<std::pair<uint32_t, std::string>> stages;	// GL shader type, source
	size_t sourceSize;

	// result: a program binary for glProgramBinary
	bool done;
	uint32_t format;
	std::vector<uint8_t> binary;
};

// rend2_glslc.exe is next to the renderer and has not failed this session
bool GLSL_HelperAvailable( void );

/*
Links the jobs in up to `processes` rend2_glslc processes (r_shaderCompileProcesses)
and returns how many got a binary. The others, and every job when the helper
is missing or runs on another driver, are left for this process to compile.
idle runs on this thread while it waits (splash screen).
*/
int GLSL_HelperCompile( std::vector<glslHelperJob_t>& jobs, int processes, void (*idle)( void ) );
