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
// tr_glslhelper.cpp -- GLSL programs linked in rend2_glslc processes

/*
The Intel Windows driver compiles GLSL one program at a time per process:
several threads with shared contexts are slower than one (measured: 6 threads
x0.83), but separate processes run in parallel (6 processes x3.3). For a batch
of programs that are not in the disk cache (a level's lightall permutations,
GLSL_BuildLazy) rend2 starts up to r_shaderCompileProcesses copies of
rend2_glslc (shared/rd-rend2/glslc), which sit next to the renderer DLL.
Each opens a hidden window with the same context version, checks that its
GL_VENDOR / GL_RENDERER / GL_VERSION are the renderer's (else the binaries
would not load), links the programs it is sent and returns their binaries.
They become program cache entries, so ShaderProgramBuilder::Build loads them
with glProgramBinary; anything that fails is compiled here as before, which
also reports its errors. The helpers exit at the end of the batch.
*/

#include "tr_local.h"
#include "tr_glslhelper.h"
#include "glslc/glslc_protocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32

static bool s_helperUnusable;	// missing or on another driver: not tried again

struct helperProcess_t
{
	HANDLE process;
	HANDLE toHelper;	// its stdin
	HANDLE fromHelper;	// its stdout
};

static bool GLSL_HelperWrite( HANDLE pipe, const void *data, size_t size )
{
	const char *bytes = (const char *)data;
	while ( size )
	{
		DWORD written = 0;
		if ( !WriteFile(pipe, bytes, (DWORD)std::min<size_t>(size, 1 << 20), &written, nullptr) || !written )
			return false;
		bytes += written;
		size -= written;
	}
	return true;
}

static bool GLSL_HelperRead( HANDLE pipe, void *data, size_t size )
{
	char *bytes = (char *)data;
	while ( size )
	{
		DWORD read = 0;
		if ( !ReadFile(pipe, bytes, (DWORD)std::min<size_t>(size, 1 << 20), &read, nullptr) || !read )
			return false;
		bytes += read;
		size -= read;
	}
	return true;
}

static bool GLSL_HelperReadMessage( HANDLE pipe, uint32_t magic, std::string& payload )
{
	uint32_t header[2];
	if ( !GLSL_HelperRead(pipe, header, sizeof(header)) || header[0] != magic || header[1] > GLSLC_MAX_MESSAGE )
		return false;
	payload.resize(header[1]);
	return !header[1] || GLSL_HelperRead(pipe, &payload[0], header[1]);
}

static void GLSL_PutU32( std::string& out, uint32_t value )
{
	out.append((const char *)&value, sizeof(value));
}

static void GLSL_PutString( std::string& out, const std::string& text )
{
	GLSL_PutU32(out, (uint32_t)text.size());
	out.append(text);
}

static bool GLSL_GetU32( const std::string& in, size_t& offset, uint32_t& value )
{
	if ( in.size() - offset < sizeof(value) )
		return false;
	memcpy(&value, in.data() + offset, sizeof(value));
	offset += sizeof(value);
	return true;
}

static bool GLSL_GetString( const std::string& in, size_t& offset, std::string& value )
{
	uint32_t length = 0;
	if ( !GLSL_GetU32(in, offset, length) || in.size() - offset < length )
		return false;
	value.assign(in.data() + offset, length);
	offset += length;
	return true;
}

// rend2_glslc.exe in the directory of this DLL
static bool GLSL_HelperPath( char *path, size_t size )
{
	HMODULE module = nullptr;
	if ( !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&GLSL_HelperCompile, &module) )
		return false;
	const DWORD length = GetModuleFileNameA(module, path, (DWORD)size);
	if ( !length || length >= size )
		return false;
	char *slash = strrchr(path, '\\');
	if ( !slash )
		return false;
	slash[1] = '\0';
	Q_strcat(path, (int)size, "rend2_glslc.exe");
	const DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool GLSL_HelperStart( const char *path, helperProcess_t& helper )
{
	helper = {};
	SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), nullptr, TRUE };
	HANDLE childIn = nullptr, childOut = nullptr;
	if ( !CreatePipe(&childIn, &helper.toHelper, &inheritable, 0) )
		return false;
	if ( !CreatePipe(&helper.fromHelper, &childOut, &inheritable, 0) )
	{
		CloseHandle(childIn);
		CloseHandle(helper.toHelper);
		return false;
	}
	SetHandleInformation(helper.toHelper, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(helper.fromHelper, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);

	// the helper inherits its pipe ends and nothing else of the game
	HANDLE inherit[3] = { childIn, childOut, nul };
	const DWORD numInherit = nul != INVALID_HANDLE_VALUE ? 3 : 2;
	SIZE_T listSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
	std::vector<char> listMemory(listSize);
	LPPROC_THREAD_ATTRIBUTE_LIST list = (LPPROC_THREAD_ATTRIBUTE_LIST)listMemory.data();
	bool started = InitializeProcThreadAttributeList(list, 1, 0, &listSize) &&
		UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
			numInherit * sizeof(HANDLE), nullptr, nullptr);

	if ( started )
	{
		int major = 3, minor = 2;
		sscanf(glConfig.version_string, "%d.%d", &major, &minor);
		char commandLine[MAX_PATH + 64];
		Com_sprintf(commandLine, sizeof(commandLine), "\"%s\" --major %d --minor %d%s",
			path, major, minor, r_debugContext->integer ? " --debug" : "");

		STARTUPINFOEXA startup = {};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = childIn;
		startup.StartupInfo.hStdOutput = childOut;
		startup.StartupInfo.hStdError = nul != INVALID_HANDLE_VALUE ? nul : childOut;
		startup.lpAttributeList = list;
		PROCESS_INFORMATION info = {};
		started = CreateProcessA(path, commandLine, nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &info) != 0;
		if ( started )
		{
			CloseHandle(info.hThread);
			helper.process = info.hProcess;
		}
		DeleteProcThreadAttributeList(list);
	}

	CloseHandle(childIn);
	CloseHandle(childOut);
	if ( nul != INVALID_HANDLE_VALUE )
		CloseHandle(nul);
	if ( !started )
	{
		CloseHandle(helper.toHelper);
		CloseHandle(helper.fromHelper);
		helper = {};
	}
	return started;
}

static void GLSL_HelperStop( helperProcess_t& helper )
{
	if ( helper.toHelper )
		CloseHandle(helper.toHelper);	// EOF: the helper exits
	if ( helper.process && WaitForSingleObject(helper.process, 2000) != WAIT_OBJECT_0 )
		TerminateProcess(helper.process, 1);
	if ( helper.fromHelper )
		CloseHandle(helper.fromHelper);
	if ( helper.process )
		CloseHandle(helper.process);
	helper = {};
}

enum helperOutcome_t
{
	HELPER_OK,
	HELPER_FAILED,		// died, wrong reply
	HELPER_MISMATCH,	// another GPU / driver than the renderer
};

static std::string GLSL_HelperRequest( const glslHelperJob_t& job )
{
	std::string out;
	GLSL_PutU32(out, (uint32_t)job.key);
	GLSL_PutU32(out, (uint32_t)(job.key >> 32));
	GLSL_PutU32(out, GLSLC_RETRIEVABLE);
	GLSL_PutU32(out, (uint32_t)job.attribs.size());
	for ( const auto& attrib : job.attribs )
	{
		GLSL_PutU32(out, attrib.first);
		GLSL_PutString(out, attrib.second);
	}
	GLSL_PutU32(out, (uint32_t)job.outputs.size());
	for ( const auto& output : job.outputs )
	{
		GLSL_PutU32(out, output.first);
		GLSL_PutString(out, output.second);
	}
	GLSL_PutU32(out, (uint32_t)job.varyings.size());
	for ( const auto& varying : job.varyings )
		GLSL_PutString(out, varying);
	GLSL_PutU32(out, (uint32_t)job.stages.size());
	for ( const auto& stage : job.stages )
	{
		GLSL_PutU32(out, stage.first);
		GLSL_PutString(out, stage.second);
	}
	return out;
}

// one helper: checks its driver, then takes jobs until none are left
static helperOutcome_t GLSL_HelperRun( helperProcess_t& helper, std::vector<glslHelperJob_t>& jobs,
	std::atomic<size_t>& next, std::atomic<int>& done, std::atomic<int>& replies )
{
	std::string message;
	if ( !GLSL_HelperReadMessage(helper.fromHelper, GLSLC_HELLO, message) )
		return HELPER_FAILED;
	size_t offset = 0;
	std::string vendor, renderer, version;
	if ( !GLSL_GetString(message, offset, vendor) || !GLSL_GetString(message, offset, renderer) ||
		!GLSL_GetString(message, offset, version) )
		return HELPER_FAILED;
	if ( vendor != glConfig.vendor_string || renderer != glConfig.renderer_string ||
		version != glConfig.version_string )
		return HELPER_MISMATCH;

	for ( ;; )
	{
		const size_t index = next.fetch_add(1);
		if ( index >= jobs.size() )
			return HELPER_OK;
		glslHelperJob_t& job = jobs[index];
		const std::string request = GLSL_HelperRequest(job);
		const uint32_t header[2] = { GLSLC_JOB, (uint32_t)request.size() };
		if ( !GLSL_HelperWrite(helper.toHelper, header, sizeof(header)) ||
			!GLSL_HelperWrite(helper.toHelper, request.data(), request.size()) ||
			!GLSL_HelperReadMessage(helper.fromHelper, GLSLC_RESULT, message) )
			return HELPER_FAILED;
		replies++;

		offset = 0;
		uint32_t keyLow = 0, keyHigh = 0, status = 0, format = 0;
		std::string binary;
		if ( !GLSL_GetU32(message, offset, keyLow) || !GLSL_GetU32(message, offset, keyHigh) ||
			!GLSL_GetU32(message, offset, status) || !GLSL_GetU32(message, offset, format) ||
			!GLSL_GetString(message, offset, binary) ||
			(((uint64_t)keyHigh << 32) | keyLow) != job.key )
			return HELPER_FAILED;
		// a failed program is compiled again here, which reports the error
		if ( status == GLSLC_OK )
		{
			job.format = format;
			job.binary.assign(binary.begin(), binary.end());
			job.done = true;
			done++;
		}
	}
}

bool GLSL_HelperAvailable( void )
{
	char path[MAX_PATH];
	if ( !s_helperUnusable && !GLSL_HelperPath(path, sizeof(path)) )
	{
		ri.Printf(PRINT_DEVELOPER, "GLSL: no rend2_glslc.exe next to the renderer, compiling here\n");
		s_helperUnusable = true;
	}
	return !s_helperUnusable;
}

int GLSL_HelperCompile( std::vector<glslHelperJob_t>& jobs, int processes, void (*idle)( void ) )
{
	if ( s_helperUnusable || jobs.size() < 2 || processes < 1 )
		return 0;

	char path[MAX_PATH];
	if ( !GLSL_HelperPath(path, sizeof(path)) )
	{
		ri.Printf(PRINT_DEVELOPER, "GLSL: no rend2_glslc.exe next to the renderer, compiling here\n");
		s_helperUnusable = true;
		return 0;
	}

	const int start = ri.Milliseconds();
	// longest sources first, the end of the batch is then not one long program
	std::stable_sort(jobs.begin(), jobs.end(), []( const glslHelperJob_t& a, const glslHelperJob_t& b ) {
		return a.sourceSize > b.sourceSize;
	});

	const int numHelpers = std::min<int>(processes, (int)jobs.size());
	std::vector<helperProcess_t> helpers(numHelpers);
	std::vector<helperOutcome_t> outcomes(numHelpers, HELPER_FAILED);
	std::vector<std::thread> threads;
	std::atomic<size_t> next(0);
	std::atomic<int> done(0);
	std::atomic<int> replies(0);
	std::atomic<int> running(0);
	for ( int i = 0; i < numHelpers; ++i )
	{
		if ( !GLSL_HelperStart(path, helpers[i]) )
			continue;
		running++;
		threads.emplace_back([&, i]() {
			outcomes[i] = GLSL_HelperRun(helpers[i], jobs, next, done, replies);
			running--;
		});
	}
	if ( threads.empty() )
	{
		ri.Printf(PRINT_WARNING, "GLSL: could not start %s, compiling here\n", path);
		s_helperUnusable = true;
		return 0;
	}

	// a helper that hangs in the driver: after two minutes without any reply
	// the helpers are stopped and the rest is compiled here
	int lastReplies = 0, lastProgress = ri.Milliseconds();
	bool stopped = false;
	while ( running > 0 )
	{
		if ( idle )
			idle();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		const int now = ri.Milliseconds();
		if ( replies != lastReplies )
		{
			lastReplies = replies;
			lastProgress = now;
		}
		else if ( !stopped && now - lastProgress > 120000 )
		{
			ri.Printf(PRINT_WARNING, "GLSL: rend2_glslc does not answer, compiling here\n");
			for ( helperProcess_t& helper : helpers )
			{
				if ( helper.process )
					TerminateProcess(helper.process, 1);
			}
			stopped = true;
		}
	}
	for ( std::thread& thread : threads )
		thread.join();
	for ( helperProcess_t& helper : helpers )
		GLSL_HelperStop(helper);

	int failed = 0;
	for ( helperOutcome_t outcome : outcomes )
	{
		if ( outcome == HELPER_MISMATCH && !s_helperUnusable )
		{
			ri.Printf(PRINT_WARNING, "GLSL: rend2_glslc runs on another GPU or driver than the game, compiling here\n");
			s_helperUnusable = true;
		}
		failed += outcome == HELPER_FAILED;
	}
	ri.Printf(PRINT_ALL, "GLSL: %d of %d programs linked in %d helper processes in %d ms%s\n",
		done.load(), (int)jobs.size(), (int)threads.size(), ri.Milliseconds() - start,
		failed ? " (a helper failed)" : "");
	return done;
}

#else

bool GLSL_HelperAvailable( void )
{
	return false;
}

int GLSL_HelperCompile( std::vector<glslHelperJob_t>& jobs, int processes, void (*idle)( void ) )
{
	return 0;
}

#endif
