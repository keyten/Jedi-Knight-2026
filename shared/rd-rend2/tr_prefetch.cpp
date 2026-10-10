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
// tr_prefetch.cpp -- decode predicted images on worker threads ahead of use

/*
r_loadPrefetch 1: as soon as the names of the shaders about to be registered
are known (the BSP shader lump, a .skin file, the surfaces of an MD3), the
images those shaders will load are predicted from their script text and
decoded on the worker threads of tr_jobs.cpp while the main thread keeps
registering. R_LoadImageProbe then takes the finished pixels instead of
reading and decoding the file itself.

Main thread: resolves the file exactly as R_LoadImage would (original
extension, then jpg, png, tga), reads it through the VFS, parses the header
and allocates the output with R_Malloc. Worker: the libjpeg / libpng decode
into that buffer, with the same library calls and transforms as LoadJPG and
LoadPNG, so the pixels are the same. TGA files are left to the normal path:
their cost is the VFS read, which has to stay on the main thread anyway.

A prediction that is never used is freed by R_PrefetchFlush (end of the world
load, end of registration, texture deletion). A decode that fails, or a
header LoadJPG / LoadPNG would reject, is dropped and the normal path runs,
printing what it always printed. The bytes outstanding are capped by
r_loadPrefetchMB. Without worker threads (r_loadThreads 0) nothing is
prefetched.
*/

#include "tr_local.h"

#include <csetjmp>
#include <cstdio>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <jpeglib.h>
#include <png.h>

namespace {
using PrefetchClock = std::chrono::steady_clock;

enum PrefetchFormat { PREFETCH_JPG, PREFETCH_PNG };

struct JpegError {
	jpeg_error_mgr pub;
	jmp_buf jump;
	char message[JMSG_LENGTH_MAX];
};

struct PrefetchEntry {
	std::string path;			// the file R_LoadImage would load
	PrefetchFormat format;
	byte *file = nullptr;		// VFS buffer, freed on the main thread
	long fileLength = 0;
	byte *pixels = nullptr;		// R_Malloc, handed to the caller or freed
	int width = 0, height = 0;
	std::size_t bytes = 0;
	R_Job *job = nullptr;
	bool ok = false;
	long long decodeUs = 0;

	jpeg_decompress_struct jpeg;
	JpegError jpegError;
	bool jpegCreated = false;

	png_structp png = nullptr;
	png_infop pngInfo = nullptr;
	std::size_t pngOffset = 0;
	std::vector<byte *> rows;
};

struct PrefetchState {
	std::deque<std::string> pending;			// predicted names, in registration order
	std::unordered_set<std::string> known;		// predicted or resolved this session
	std::unordered_map<std::string, std::unique_ptr<PrefetchEntry>> entries;	// by requested name
	std::size_t bytes = 0;
	int submitted = 0, taken = 0, waited = 0, failed = 0, unused = 0, skippedTga = 0;
	long long waitUs = 0, mainUs = 0;
};

PrefetchState state;

cvar_t *PrefetchCvar() {
#ifdef REND2_SP
	static cvar_t *cvar = ri.Cvar_Get("r_loadPrefetch", "1", CVAR_ARCHIVE);
#else
	static cvar_t *cvar = ri.Cvar_Get("r_loadPrefetch", "1", CVAR_ARCHIVE,
		"Decode predicted textures on worker threads during map load");
#endif
	return cvar;
}

cvar_t *PrefetchBudgetCvar() {
#ifdef REND2_SP
	static cvar_t *cvar = ri.Cvar_Get("r_loadPrefetchMB", "512", CVAR_ARCHIVE);
#else
	static cvar_t *cvar = ri.Cvar_Get("r_loadPrefetchMB", "512", CVAR_ARCHIVE,
		"Memory for textures decoded ahead of use, in MB");
#endif
	return cvar;
}

std::string LowerKey(const char *name) {
	char key[MAX_QPATH];
	Q_strncpyz(key, name, sizeof(key));
	Q_strlwr(key);
	return key;
}

long long ElapsedUs(PrefetchClock::time_point start) {
	return std::chrono::duration_cast<std::chrono::microseconds>(PrefetchClock::now() - start).count();
}

// ---- libjpeg, the calls of LoadJPG ----

void JpegErrorExit(j_common_ptr cinfo) {
	JpegError *error = (JpegError *)cinfo->err;
	(*cinfo->err->format_message)(cinfo, error->message);
	longjmp(error->jump, 1);
}

void JpegOutputMessage(j_common_ptr) {
}

bool JpegHeader(PrefetchEntry &e) {
	e.jpeg.err = jpeg_std_error(&e.jpegError.pub);
	e.jpegError.pub.error_exit = JpegErrorExit;
	e.jpegError.pub.output_message = JpegOutputMessage;
	if (setjmp(e.jpegError.jump)) {
		if (e.jpegCreated)
			jpeg_destroy_decompress(&e.jpeg);
		e.jpegCreated = false;
		return false;
	}
	jpeg_create_decompress(&e.jpeg);
	e.jpegCreated = true;
	jpeg_mem_src(&e.jpeg, e.file, e.fileLength);
	(void)jpeg_read_header(&e.jpeg, TRUE);
	e.jpeg.out_color_space = JCS_RGB;
	jpeg_calc_output_dimensions(&e.jpeg);

	const unsigned pixelcount = e.jpeg.output_width * e.jpeg.output_height;
	if (!e.jpeg.output_width || !e.jpeg.output_height
		|| ((pixelcount * 4) / e.jpeg.output_width) / 4 != e.jpeg.output_height
		|| pixelcount > 0x1FFFFFFF || e.jpeg.output_components != 3) {
		jpeg_destroy_decompress(&e.jpeg);
		e.jpegCreated = false;
		return false;
	}
	e.width = (int)e.jpeg.output_width;
	e.height = (int)e.jpeg.output_height;
	return true;
}

void JpegDecode(PrefetchEntry &e) {
	if (setjmp(e.jpegError.jump)) {
		jpeg_destroy_decompress(&e.jpeg);
		e.jpegCreated = false;
		e.ok = false;
		return;
	}
	(void)jpeg_start_decompress(&e.jpeg);
	const unsigned pixelcount = e.jpeg.output_width * e.jpeg.output_height;
	const unsigned row_stride = e.jpeg.output_width * e.jpeg.output_components;
	if (e.jpeg.output_width != (unsigned)e.width || e.jpeg.output_height != (unsigned)e.height ||
		e.jpeg.output_components != 3) {
		jpeg_destroy_decompress(&e.jpeg);
		e.jpegCreated = false;
		e.ok = false;
		return;
	}
	byte *out = e.pixels;
	while (e.jpeg.output_scanline < e.jpeg.output_height) {
		byte *buf = out + row_stride * e.jpeg.output_scanline;
		JSAMPARRAY buffer = &buf;
		(void)jpeg_read_scanlines(&e.jpeg, buffer, 1);
	}

	// Expand from RGB to RGBA, as LoadJPG
	unsigned sindex = pixelcount * e.jpeg.output_components;
	unsigned dindex = pixelcount * 4;
	do {
		out[--dindex] = 255;
		out[--dindex] = out[--sindex];
		out[--dindex] = out[--sindex];
		out[--dindex] = out[--sindex];
	} while (sindex);

	(void)jpeg_finish_decompress(&e.jpeg);
	jpeg_destroy_decompress(&e.jpeg);
	e.jpegCreated = false;
	e.ok = true;
}

// ---- libpng, the calls of LoadPNG ----

void PngError(png_structp, png_const_charp) {
	// libpng longjmps to png_jmpbuf when this returns
}

void PngWarning(png_structp, png_const_charp) {
}

void PngRead(png_structp png, png_bytep data, png_size_t length) {
	PrefetchEntry *e = (PrefetchEntry *)png_get_io_ptr(png);
	if (e->pngOffset + length > (std::size_t)e->fileLength) {
		png_error(png, "read past the end of the file");
		return;
	}
	memcpy(data, e->file + e->pngOffset, length);
	e->pngOffset += length;
}

bool IsPowerOfTwo(png_uint_32 i) { return (i & (i - 1)) == 0; }

bool PngHeader(PrefetchEntry &e) {
	const int SIGNATURE_LEN = 8;
	if (e.fileLength < SIGNATURE_LEN || !png_check_sig(e.file, SIGNATURE_LEN))
		return false;
	e.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, PngError, PngWarning);
	if (!e.png)
		return false;
	e.pngInfo = png_create_info_struct(e.png);
	if (!e.pngInfo || setjmp(png_jmpbuf(e.png))) {
		png_destroy_read_struct(&e.png, e.pngInfo ? &e.pngInfo : NULL, NULL);
		e.png = nullptr;
		e.pngInfo = nullptr;
		return false;
	}
	e.pngOffset = SIGNATURE_LEN;
	png_set_read_fn(e.png, (png_voidp)&e, PngRead);
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
	png_set_keep_unknown_chunks(e.png, PNG_HANDLE_CHUNK_NEVER, NULL, -1);
#endif
	png_set_sig_bytes(e.png, SIGNATURE_LEN);
	png_read_info(e.png, e.pngInfo);

	png_uint_32 width, height;
	int depth, colortype;
	png_get_IHDR(e.png, e.pngInfo, &width, &height, &depth, &colortype, NULL, NULL, NULL);
	if (!IsPowerOfTwo(width) || !IsPowerOfTwo(height) ||
		(colortype != PNG_COLOR_TYPE_RGB && colortype != PNG_COLOR_TYPE_RGBA)) {
		png_destroy_read_struct(&e.png, &e.pngInfo, NULL);
		e.png = nullptr;
		e.pngInfo = nullptr;
		return false;
	}
	if (colortype == PNG_COLOR_TYPE_RGB)
		png_set_add_alpha(e.png, 0xff, PNG_FILLER_AFTER);
	png_read_update_info(e.png, e.pngInfo);
	e.width = (int)width;
	e.height = (int)height;
	return true;
}

void PngDecode(PrefetchEntry &e) {
	if (setjmp(png_jmpbuf(e.png))) {
		png_destroy_read_struct(&e.png, &e.pngInfo, NULL);
		e.png = nullptr;
		e.pngInfo = nullptr;
		e.ok = false;
		return;
	}
	png_read_image(e.png, e.rows.data());
	png_read_end(e.png, NULL);
	png_destroy_read_struct(&e.png, &e.pngInfo, NULL);
	e.png = nullptr;
	e.pngInfo = nullptr;
	e.ok = true;
}

void DecodeJob(void *user) {
	PrefetchEntry *e = (PrefetchEntry *)user;
	const auto start = PrefetchClock::now();
	if (e->format == PREFETCH_JPG)
		JpegDecode(*e);
	else
		PngDecode(*e);
	e->decodeUs = ElapsedUs(start);
}

void FreeEntry(PrefetchEntry &e) {
	if (e.job) {
		R_JobWait(e.job);
		e.job = nullptr;
	}
	if (e.jpegCreated) {
		jpeg_destroy_decompress(&e.jpeg);
		e.jpegCreated = false;
	}
	if (e.png) {
		png_destroy_read_struct(&e.png, e.pngInfo ? &e.pngInfo : NULL, NULL);
		e.png = nullptr;
	}
	if (e.pixels) {
		Z_Free(e.pixels);
		e.pixels = nullptr;
	}
	if (e.file) {
		ri.FS_FreeFile(e.file);
		e.file = nullptr;
	}
	state.bytes -= e.bytes;
	e.bytes = 0;
}

int LoaderIndex(const char *extension) {
	if (!Q_stricmp(extension, "jpg")) return 0;
	if (!Q_stricmp(extension, "png")) return 1;
	if (!Q_stricmp(extension, "tga")) return 2;
	return -1;
}

// what R_LoadImage would load: the name as given, then the other extensions
// in loader order (jpg, png, tga). Empty when there is no such file.
std::string ResolveFile(const char *name) {
	static const char *extensions[3] = { "jpg", "png", "tga" };
	const char *extension = COM_GetExtension(name);
	const int given = LoaderIndex(extension);
	if (given >= 0 && ri.FS_ReadFile(name, NULL) > 0)
		return name;
	char base[MAX_QPATH];
	COM_StripExtension(name, base, sizeof(base));
	for (int i = 0; i < 3; ++i) {
		if (i == given)
			continue;
		const char *candidate = va("%s.%s", base, extensions[i]);
		if (ri.FS_ReadFile(candidate, NULL) > 0)
			return candidate;
	}
	return std::string();
}

// starts reading and decoding one predicted name
void Start(const std::string &key) {
	if (state.entries.count(key) || R_ImageKnownMissing(key.c_str()) || R_ImageLoadedByName(key.c_str()))
		return;
	const std::string path = ResolveFile(key.c_str());
	if (path.empty()) {
		R_ImageMarkMissing(key.c_str());
		return;
	}
	const int loader = LoaderIndex(COM_GetExtension(path.c_str()));
	if (loader != 0 && loader != 1) {
		++state.skippedTga;
		return;
	}

	std::unique_ptr<PrefetchEntry> e(new PrefetchEntry);
	e->path = path;
	e->format = loader == 0 ? PREFETCH_JPG : PREFETCH_PNG;
	e->fileLength = ri.FS_ReadFile(path.c_str(), (void **)&e->file);
	if (e->fileLength <= 0 || !e->file) {
		if (e->file) ri.FS_FreeFile(e->file);
		return;
	}
	const bool header = e->format == PREFETCH_JPG ? JpegHeader(*e) : PngHeader(*e);
	if (!header) {
		ri.FS_FreeFile(e->file);
		++state.failed;
		return;
	}
	e->bytes = (std::size_t)e->width * e->height * 4;
	e->pixels = (byte *)R_Malloc((int)e->bytes, TAG_TEMP_WORKSPACE, qfalse);
	if (e->format == PREFETCH_PNG) {
		e->rows.resize(e->height);
		for (int i = 0; i < e->height; ++i)
			e->rows[i] = e->pixels + (std::size_t)i * e->width * 4;
	}
	state.bytes += e->bytes;
	PrefetchEntry *raw = e.get();
	state.entries.emplace(key, std::move(e));
	raw->job = R_JobSubmit(DecodeJob, raw);
	++state.submitted;
}

bool Enabled() {
	const cvar_t *cvar = PrefetchCvar();
	return cvar && cvar->integer && R_JobWorkers() > 0;
}
}

// textures that go through the material DDC (r_materialDDCWorld for BSP
// surfaces, r_materialDDCObjects for the rest) are not decoded on a cache hit,
// so decoding them ahead would be wasted: no prefetch for that category
static bool MaterialDdcOn( bool world ) {
#ifdef REND2_SP
	static cvar_t *worldCvar = ri.Cvar_Get("r_materialDDCWorld", "0", CVAR_ARCHIVE);
	static cvar_t *objectsCvar = ri.Cvar_Get("r_materialDDCObjects", "1", CVAR_ARCHIVE);
#else
	static cvar_t *worldCvar = ri.Cvar_Get("r_materialDDCWorld", "0", CVAR_ARCHIVE, "Cache prepared BSP surface textures on disk");
	static cvar_t *objectsCvar = ri.Cvar_Get("r_materialDDCObjects", "1", CVAR_ARCHIVE, "Cache prepared NPC, object and other non-BSP textures on disk");
#endif
	const cvar_t *cvar = world ? worldCvar : objectsCvar;
	return cvar && cvar->integer != 0;
}

qboolean R_PrefetchWorldEnabled( void ) {
	return (qboolean)(Enabled() && !MaterialDdcOn(true));
}

qboolean R_PrefetchModelsEnabled( void ) {
	return (qboolean)(Enabled() && !MaterialDdcOn(false));
}

void R_PrefetchImage( const char *name ) {
	if (!name || !name[0] || name[0] == '$' || name[0] == '*' || !Enabled())
		return;
	std::string key = LowerKey(name);
	if (state.known.insert(key).second)
		state.pending.push_back(std::move(key));
}

void R_PrefetchPump( void ) {
	if (state.pending.empty())
		return;
	if (!Enabled()) {
		state.pending.clear();
		return;
	}
	const auto start = PrefetchClock::now();
	const std::size_t budget = (std::size_t)Com_Clampi(16, 4096, PrefetchBudgetCvar()->integer) * 1024u * 1024u;
	while (!state.pending.empty() && state.bytes < budget) {
		const std::string key = state.pending.front();
		state.pending.pop_front();
		Start(key);
	}
	state.mainUs += ElapsedUs(start);
}

qboolean R_PrefetchTake( const char *name, byte **pic, int *width, int *height ) {
	if (state.entries.empty())
		return qfalse;
	auto it = state.entries.find(LowerKey(name));
	if (it == state.entries.end())
		return qfalse;
	std::unique_ptr<PrefetchEntry> e = std::move(it->second);
	state.entries.erase(it);

	if (e->job) {
		if (!R_JobDone(e->job)) {
			const auto start = PrefetchClock::now();
			R_JobWait(e->job);
			state.waitUs += ElapsedUs(start);
			++state.waited;
		} else {
			R_JobWait(e->job);
		}
		e->job = nullptr;
	}
	if (!e->ok) {
		++state.failed;
		FreeEntry(*e);
		return qfalse;
	}
	*pic = e->pixels;
	*width = e->width;
	*height = e->height;
	e->pixels = nullptr;
#ifdef REND2_LOAD_PROFILE
	R_ImageProfileLoaderAttempt(e->path.c_str(), COM_GetExtension(e->path.c_str()), e->decodeUs, qtrue,
		e->width, e->height);
#endif
	FreeEntry(*e);
	++state.taken;
	return qtrue;
}

void R_PrefetchFlush( void ) {
	for (auto &it : state.entries) {
		FreeEntry(*it.second);
		++state.unused;
	}
	state.entries.clear();
	state.pending.clear();
	state.bytes = 0;
}

void R_PrefetchReset( void ) {
	R_PrefetchFlush();
	state.known.clear();
}

void R_PrefetchReport( void ) {
	if (!state.submitted && !state.skippedTga)
		return;
	ri.Printf(PRINT_ALL, "[map load] prefetch: %d decodes submitted, %d used (%d waited %lld ms), %d unused, %d failed, %d TGA left to the normal path; main-thread read/header %lld ms\n",
		state.submitted, state.taken, state.waited, state.waitUs / 1000, state.unused, state.failed,
		state.skippedTga, state.mainUs / 1000);
	state.submitted = state.taken = state.waited = state.failed = state.unused = state.skippedTga = 0;
	state.waitUs = state.mainUs = 0;
}
