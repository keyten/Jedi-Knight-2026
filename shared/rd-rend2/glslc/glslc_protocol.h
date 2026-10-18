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

// Pipe protocol between rend2 (tr_glslhelper.cpp) and rend2_glslc, the
// process that links GLSL programs for it (r_shaderCompileProcesses).
// Every message is a 32-bit magic, a 32-bit payload size and the payload;
// numbers are little endian, a string is a 32-bit length and its bytes.
//
// rend2_glslc --major M --minor m [--debug]
//   creates a hidden window with the renderer's context version (core
//   profile) and sends GLSLC_HELLO: vendor, renderer, version strings.
//
// GLSLC_JOB (renderer -> helper): u64 key, u32 flags (GLSLC_RETRIEVABLE),
//   u32 n + n * (u32 index, string name) attribute locations,
//   u32 n + n * (u32 index, string name) fragment data locations,
//   u32 n + n * string transform feedback varyings (interleaved),
//   u32 n + n * (u32 GL shader type, string source) stages.
// GLSLC_RESULT (helper -> renderer): u64 key, u32 status (GLSLC_OK or the
//   failed step), u32 binary format, string binary (or the info log).
//
// The helper exits when its stdin is closed.

#pragma once

#include <cstdint>

#define GLSLC_HELLO			0x49484332u	// "2CHI"
#define GLSLC_JOB			0x424a4332u	// "2CJB"
#define GLSLC_RESULT		0x53524332u	// "2CRS"

#define GLSLC_RETRIEVABLE	1u

enum glslcStatus_t
{
	GLSLC_OK,
	GLSLC_COMPILE_FAILED,
	GLSLC_LINK_FAILED,
	GLSLC_NO_BINARY,
};

#define GLSLC_MAX_MESSAGE	(64u * 1024u * 1024u)
