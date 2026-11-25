/*
 * Per-plant frustum/fade culling and LOD. GL 4.3 produces bounded index lists
 * and indexed/array indirect commands; GL 3.2 uses the same compact plant records
 * as ordinary instanced attributes. No vertex-stage SSBO support is required.
 */
#include "tr_local.h"
#include "tr_spritecull.h"
#include <unordered_map>
#include <vector>

namespace
{
const unsigned SPRITE_BINS = 6;
const unsigned SPRITE_CANDIDATES = 1u << 20;
const unsigned SPRITE_COMMANDS = 8192 * SPRITE_BINS;
struct indirect_t { GLuint count, instanceCount, first, baseVertex, baseInstance; };
static_assert(sizeof(indirect_t) == 20, "GL DrawElementsIndirect layout");
struct arena_t
{
	VBO_t lists;
	GLuint commands;
	unsigned listUsed, commandUsed;
};
struct key_t
{
	const srfSprites_t *surface;
	int view, scene;
	bool operator==(const key_t &other) const
	{ return surface == other.surface && view == other.view && scene == other.scene; }
};
struct keyHash_t
{
	size_t operator()(const key_t &key) const
	{ return ((uintptr_t)key.surface >> 4) ^ ((size_t)key.view << 20) ^ ((size_t)key.scene << 28); }
};
std::unordered_map<key_t, spriteCullResult_t, keyHash_t> cache;
std::vector<image_t *> dataImages;
arena_t arenas[MAX_FRAMES] = {};
arena_t *lastArena;
unsigned frameStamp = ~0u;
bool failed;
bool warnedCapacity;

void Report()
{
	if (!lastArena || cache.empty() || (r_speeds->integer != 1 && r_speeds->integer != 100)) return;
	// Diagnostic mode only. Normal frames never read GPU counters or wait for
	// their results. This reads the completed prior frame before orphaning.
	std::vector<indirect_t> commands(lastArena->commandUsed);
	qglMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, lastArena->commands);
	qglGetBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, commands.size() * sizeof(indirect_t), commands.data());
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	uint64_t candidates = 0, submitted = 0, cards = 0, vertices = 0;
	unsigned uses = 0;
	for (const auto &entry : cache)
	{
		const spriteCullResult_t &result = entry.second;
		candidates += (uint64_t)result.numSprites * result.uses;
		uses += result.uses;
		for (unsigned bin = 0; bin < SPRITE_BINS; ++bin)
		{
			const indirect_t &command = commands[result.commandOffset / sizeof(indirect_t) + bin];
			submitted += (uint64_t)(command.instanceCount / (bin % 3 + 1)) * result.uses;
			cards += (uint64_t)command.instanceCount * result.uses;
			vertices += (uint64_t)command.instanceCount * (bin >= 3 ? command.count : 4) * result.uses;
		}
	}
	ri.Printf(PRINT_ALL, "Surface sprites GPU: candidates %llu submitted %llu culled %llu cards %llu verts %llu dispatches %u reused %u (previous frame)\n",
		(unsigned long long)candidates, (unsigned long long)submitted,
		(unsigned long long)(candidates - submitted), (unsigned long long)cards,
		(unsigned long long)vertices, (unsigned)cache.size(), uses - (unsigned)cache.size());
}

bool BeginFrame()
{
	if (frameStamp == backEndData->realFrameNumber) return !failed;
	Report();
	cache.clear();
	frameStamp = backEndData->realFrameNumber;
	arena_t &arena = arenas[frameStamp % MAX_FRAMES];
	lastArena = &arena;
	arena.listUsed = arena.commandUsed = 0;
	if (failed) return false;
	// Earlier renderer work may have left an error (for example a rejected
	// cached program binary). Attribute only errors from our own allocation.
	while (qglGetError() != GL_NO_ERROR) {}
	if (!arena.lists.vertexesVBO) qglGenBuffers(1, &arena.lists.vertexesVBO);
	if (!arena.commands) qglGenBuffers(1, &arena.commands);
	// Orphaning also makes reuse safe across GPU latency and multiple scenes.
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, arena.lists.vertexesVBO);
	arena.lists.vertexesSize = SPRITE_CANDIDATES * SPRITE_BINS * sizeof(GLuint);
	qglBufferData(GL_SHADER_STORAGE_BUFFER, arena.lists.vertexesSize, nullptr, GL_STREAM_DRAW);
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, arena.commands);
	qglBufferData(GL_DRAW_INDIRECT_BUFFER, SPRITE_COMMANDS * sizeof(indirect_t), nullptr, GL_STREAM_DRAW);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
	const GLenum error = qglGetError();
	if (!arena.lists.vertexesVBO || !arena.commands || error != GL_NO_ERROR)
	{
		failed = true;
		ri.Printf(PRINT_WARNING, "Surface sprite compute buffers unavailable (GL 0x%x); using CPU submission.\n", error);
	}
	return !failed;
}
}

image_t *R_CreateSpriteDataImage(VBO_t *vbo)
{
	if (!R_HasModernFeatures(MODERN_COMPUTE | MODERN_SSBO) || !qglTexBuffer || vbo->vertexesSize / 16 > (size_t)glRefConfig.maxTextureBufferSize) return nullptr;
	image_t *image = new image_t();
	Q_strncpyz(image->imgName, "*spriteData", sizeof(image->imgName));
	image->flags = IMGFLAG_TEXBUFFER;
	qglGenTextures(1, &image->texnum);
	GL_BindToTMU(image, TB_FPLUS_LIGHTS);
	qglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, vbo->vertexesVBO);
	dataImages.push_back(image);
	return image;
}

void R_ShutdownSpriteCull()
{
	for (image_t *image : dataImages)
	{
		for (int unit = 0; unit < MAX_TEXTURE_UNITS; ++unit)
			if (glState.currenttextures[unit] == (int)image->texnum) glState.currenttextures[unit] = 0;
		qglDeleteTextures(1, &image->texnum);
		delete image;
	}
	dataImages.clear();
	for (arena_t &arena : arenas)
	{
		if (arena.lists.vertexesVBO) qglDeleteBuffers(1, &arena.lists.vertexesVBO);
		if (arena.commands) qglDeleteBuffers(1, &arena.commands);
		arena = {};
	}
	cache.clear();
	lastArena = nullptr;
	frameStamp = ~0u;
	failed = warnedCapacity = false;
}

const spriteCullResult_t *RB_CullSurfaceSprites(const srfSprites_t *surf,
	const vec3_t viewOrigin, const vec4_t autoGrass, uint32_t shaderFlags,
	int stripSegments, int cards)
{
	if (!r_surfaceSpritesCompute->integer || !qglDrawArraysIndirect || !qglDrawElementsIndirect || !surf->spriteDataImage ||
		!tr.spriteCullShader.program || !R_HasModernFeatures(MODERN_COMPUTE | MODERN_SSBO) ||
		(shaderFlags & (SSDEF_FX_SPRITE | SSDEF_FACE_UP | SSDEF_ADDITIVE | SSDEF_FOG_MODULATE)) ||
		(surf->shader->stages[0]->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) || backEnd.cubeLayerView ||
		glRefConfig.maxComputeShaderStorageBlocks < 3 || glRefConfig.maxShaderStorageBufferBindings < 3 ||
		glRefConfig.maxShaderStorageBlockSize < (GLint64)(SPRITE_CANDIDATES * SPRITE_BINS * sizeof(GLuint)) ||
		glRefConfig.maxShaderStorageBlockSize < (GLint64)surf->vbo->vertexesSize ||
		(surf->numSprites + 63) / 64 > glRefConfig.maxComputeWorkGroupCount[0])
		return nullptr;

	const bool ready = BeginFrame();
	const key_t key = { surf, backEnd.viewParms.currentViewParm, backEndData->currentFrame->currentScene };
	const auto found = cache.find(key);
	if (found != cache.end())
	{
		++found->second.uses;
		return &found->second;
	}
	// Successful earlier lists remain valid after a later surface fails.
	// Reuse them in color/velocity even when new dispatches have been disabled.
	if (!ready) return nullptr;
	arena_t &arena = *lastArena;
	SpriteCull::Cursor reservation = { arena.listUsed, arena.commandUsed };
	if (!SpriteCull::Reserve(reservation, surf->numSprites,
			SPRITE_CANDIDATES * SPRITE_BINS, SPRITE_COMMANDS))
	{
		if (!warnedCapacity)
			ri.Printf(PRINT_WARNING, "Surface sprite compute arena full; remaining whole surfaces use CPU submission.\n");
		warnedCapacity = true;
		return nullptr;
	}
	const bool perPlant = r_surfaceSpritesCompute->integer >= 2;
	const unsigned cardMask = (1u << (cards - 1)) |
		((perPlant && cards == 3 && autoGrass[1] > 0.0f) ? (1u << 1) : 0);
	unsigned binMask = stripSegments > 0 ? cardMask << 3 : cardMask;
	if (perPlant && stripSegments > 0) binMask |= cardMask;
	spriteCullResult_t result = { &arena.lists, arena.commands,
		arena.commandUsed * sizeof(indirect_t), (int)arena.listUsed,
		surf->numSprites, stripSegments, binMask, 1 };
	indirect_t commands[SPRITE_BINS] = {};
	for (unsigned bin = 0; bin < SPRITE_BINS; ++bin)
		commands[bin].count = bin >= 3 ? 2 * (stripSegments + 1) : 6;
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, arena.commands);
	qglBufferSubData(GL_DRAW_INDIRECT_BUFFER, result.commandOffset, sizeof(commands), commands);
	qglBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

	// Use the same world->clip transform as the Camera UBO. In a depth-clamped
	// light view only the four lateral planes clip; near/far must be disabled.
	matrix_t matrix;
	Matrix16Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelViewMatrix, matrix);
	vec4_t planes[6] = {};
	for (int p = 0; p < 6; ++p)
	{
		if (p >= 4 && (backEnd.viewParms.flags & VPF_DEPTHCLAMP)) continue;
		const int row = p / 2;
		const float sign = (p & 1) ? -1.0f : 1.0f;
		for (int col = 0; col < 4; ++col) planes[p][col] = matrix[col * 4 + 3] + sign * matrix[col * 4 + row];
		const float length = VectorLength(planes[p]);
		if (length > 1e-8f) for (int col = 0; col < 4; ++col) planes[p][col] /= length;
		else VectorClear4(planes[p]);
	}
	const surfaceSprite_t &ss = *surf->sprite;
	const vec4_t range = { (float)surf->baseVertex, (float)surf->numSprites,
		(float)arena.listUsed, (float)arena.commandUsed };
	const vec4_t lod = { perPlant ? MAX(0.0f, r_grassBendLodDist->value) : 1e30f,
		(float)stripSegments, (float)cards, perPlant ? autoGrass[1] * 1.33f : 0.0f };
	const vec4_t shape = {
		((shaderFlags & SSDEF_FLATTENED) ? 1.0f : 0.5f) *
			((shaderFlags & SSDEF_AUTO_GRASS) ? fabsf(autoGrass[3]) : 1.0f) + 0.2f,
		(shaderFlags & SSDEF_FACE_CAMERA) ? 2.0f : ((shaderFlags & SSDEF_FLATTENED) ? 1.0f : 0.0f),
		ss.fadeScale, MAX(ss.fadeDist + 250.0f, ss.fadeMax) };
	shaderProgram_t *program = &tr.spriteCullShader;
	const int timer = RB_ScreenBeginTimer("Surface sprite cull");
	GLSL_BindProgram(program);
	GLSL_SetUniformVec4N(program, UNIFORM_SPRITECULLPLANES, planes[0], 6);
	GLSL_SetUniformVec4(program, UNIFORM_SPRITECULLRANGE, range);
	GLSL_SetUniformVec4(program, UNIFORM_SPRITECULLLOD, lod);
	GLSL_SetUniformVec4(program, UNIFORM_SPRITECULLSHAPE, shape);
	GLSL_SetUniformVec3(program, UNIFORM_SPRITEVIEWORIGIN, viewOrigin);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, surf->vbo->vertexesVBO);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, arena.lists.vertexesVBO);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, arena.commands);
	qglDispatchCompute((surf->numSprites + 63) / 64, 1, 1);
	qglMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
	for (int slot = 0; slot < 3; ++slot) qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, 0);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	RB_ScreenEndTimer(timer);
	const GLenum error = qglGetError();
	if (error != GL_NO_ERROR)
	{
		failed = true;
		ri.Printf(PRINT_WARNING, "Surface sprite compute dispatch failed (GL 0x%x); using CPU submission.\n", error);
		return nullptr;
	}
	arena.listUsed = reservation.lists;
	arena.commandUsed = reservation.commands;
	return &cache.emplace(key, result).first->second;
}
