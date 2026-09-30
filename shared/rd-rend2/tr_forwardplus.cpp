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

/*
Forward+ / clustered dynamic lighting (r_forwardPlus 1), see
docs/rend2-forward-plus.md.

Legacy (r_forwardPlus 0): at most MAX_DLIGHTS lights, every surface carries a
32 bit mask of the lights touching it, lightall loops over the Lights block.

Forward+: up to MAX_RENDER_DLIGHTS lights. For every colour view of a scene
(main, portal / mirror, sky portal) the CPU splits the viewport into
tiles x tiles x depth slices ("clusters") and bins each light sphere into the
clusters it may touch. Three buffer textures (GL 3.1 core, no compute) carry
the result to lightall, which loops only over the lights of its cluster:

  light data   RGBA32F  3 texels per light of the scene
                          0: origin.xyz, radius
                          1: color.rgb, type (0 = point; line / rect / spot reserved)
                          2: shadow slot (-1 = none), flags, 2 x reserved
  cluster grid RG32UI   1 texel per cluster: absolute offset into the index
                          list, light count
  index list   R16UI    light index within the scene's light data

Each gpuFrame_t slot owns its own set of buffers (write offsets reset once per
frame), so an upload never touches data the GPU may still read.

Lights are binned in importance order, so when a cluster is full
(r_forwardPlusMaxLightsPerCluster) the least important lights are dropped.

Point light shadows: only MAX_DLIGHT_SHADOWS cubes exist. Forward+ gives the
r_forwardPlusMaxShadowLights most important lights a slot (with hysteresis so
slots do not jump between lights of similar importance); the shader reads the
slot from the light data instead of assuming slot == light index.
*/

#include "tr_local.h"

#include <algorithm>
#include <chrono>
#include <vector>

#define FPLUS_LIGHT_TEXELS		5	// must match lightall.glsl
#define FPLUS_MAX_VIEWS			ARRAY_LEN(tr.cameraUboOffsets)
#define FPLUS_LIGHT_CAPACITY	(MAX_SCENES * MAX_RENDER_DLIGHTS * FPLUS_LIGHT_TEXELS)
#define FPLUS_GRID_CAPACITY		(1 << 19)	// clusters per frame (all scenes / views)
#define FPLUS_INDEX_CAPACITY	(1 << 21)	// light indexes per frame
#define FPLUS_STATS_FRAMES		64

enum
{
	FPLUS_BUFFER_LIGHTS,
	FPLUS_BUFFER_GRID,
	FPLUS_BUFFER_INDICES,
	FPLUS_NUM_BUFFERS
};

struct fplusView_t
{
	qboolean enabled;
	int grid[4];
	vec4_t params;
	vec4_t params2;
	vec4_t debug;
};

struct fplusFrameBuffers_t
{
	GLuint buffers[FPLUS_NUM_BUFFERS];
	image_t images[FPLUS_NUM_BUFFERS];	// buffer textures, bound like images
	int writeOffset[FPLUS_NUM_BUFFERS];	// texels
	unsigned frameStamp;
};

struct fplusLightRange_t
{
	int light;
	int x0, x1, y0, y1, z0, z1;
};

struct fplusShadowHistory_t
{
	vec3_t origin;
	vec3_t color;
	float radius;
	int slot;
};

struct fplusStats_t
{
	int lights;				// lights of the main scene
	int dropped;			// lights refused by the capacity this frame
	int clusters;
	int nonEmptyClusters;
	int lightRefs;			// sum of cluster light counts
	int maxPerCluster;
	int overflowClusters;
	int overflowRefs;		// light / cluster pairs dropped by the cluster limit
	int shadowed;
	int views;
	double buildMsec;		// CPU, all views of all scenes of the frame
};

static struct
{
	qboolean initialized;		// cleared by R_ShutdownForwardPlus
	qboolean active;			// latched per frame
	qboolean gpuFailed;
	qboolean gpuCreated;
	qboolean firstCheck;
	int capacity[FPLUS_NUM_BUFFERS];
	fplusFrameBuffers_t frames[MAX_FRAMES];

	// scene (tr.sceneCount, light array) the lists below were built for
	int preparedScene;
	const dlight_t *preparedLights;
	int numLights;
	int order[MAX_RENDER_DLIGHTS];			// most important first
	float importance[MAX_RENDER_DLIGHTS];
	int shadowSlot[MAX_RENDER_DLIGHTS];		// -1 = unshadowed
	int slotLight[MAX_DLIGHT_SHADOWS];
	int numSlots;
	int numShadowCubes;						// slots 0..numShadowCubes-1 are drawn

	fplusShadowHistory_t shadowHistory[MAX_DLIGHT_SHADOWS];
	int numShadowHistory;

	fplusView_t views[FPLUS_MAX_VIEWS];

	// statistics
	int droppedThisFrame;
	fplusStats_t current;		// being gathered this frame
	fplusStats_t last;			// previous frame
	double buildHistory[FPLUS_STATS_FRAMES];
	float gpuHistory[FPLUS_STATS_FRAMES];
	int numBuildSamples;
	int numGpuSamples;
	int pendingTimer[MAX_FRAMES];
	int readableTimer[MAX_FRAMES];
	qboolean warnedCapacity;

	// r_forwardPlusSpawnTestLights
	int testLights;
	float testRadius;

	// r_forwardPlusBenchmark
	int benchStep;				// -1 = off
	int benchFrame;
	int benchFrames;
	int benchSavedMode;
	int benchSavedTestLights;
	double benchBuild;
	double benchGpu;
	int benchGpuSamples;
	int benchFrameMsec;
	int benchStartTime;
	double benchResults[16][4];	// build ms, gpu ms, frame ms, gpu samples
} s_fp = {};

static std::vector<uint32_t> s_counts;
static std::vector<uint32_t> s_offsets;
static std::vector<uint32_t> s_cursor;
static std::vector<uint32_t> s_gridData;
static std::vector<uint16_t> s_indexData;
static std::vector<fplusLightRange_t> s_ranges;

static const struct
{
	int forwardPlus;
	int lights;
} s_benchSteps[] = {
	{ 0, 8 }, { 0, 32 },
	{ 1, 8 }, { 1, 32 }, { 1, 64 }, { 1, 128 }, { 1, 256 },
};

/*
============================================================

Mode, capacity, dependency messages

============================================================
*/

qboolean R_ForwardPlusActive( void )
{
	return s_fp.active;
}

int R_DlightCapacity( void )
{
	return s_fp.active ? MAX_RENDER_DLIGHTS : LEGACY_DLIGHT_LIMIT;
}

void R_ForwardPlusNoteDroppedLight( void )
{
	s_fp.droppedThisFrame++;
}

// printed when the relevant cvars change (and once after renderer start),
// never every frame. Nothing gets enabled on the user's behalf.
static void R_ForwardPlusCheckDependencies( void )
{
	const qboolean shadowsChanged = (qboolean)r_forwardPlusMaxShadowLights->modified;
	if ( !s_fp.firstCheck && !r_forwardPlus->modified &&
		!r_forwardPlusDebug->modified && !shadowsChanged )
	{
		return;
	}

	const qboolean first = s_fp.firstCheck;
	s_fp.firstCheck = qfalse;
	r_forwardPlus->modified = qfalse;
	r_forwardPlusDebug->modified = qfalse;
	r_forwardPlusMaxShadowLights->modified = qfalse;

	if ( !r_forwardPlus->integer )
	{
		if ( r_forwardPlusDebug->integer )
			ri.Printf(PRINT_ALL, "r_forwardPlusDebug requires clustered lighting. Enable r_forwardPlus 1.\n");
		if ( shadowsChanged && !first )
			ri.Printf(PRINT_ALL, "r_forwardPlusMaxShadowLights applies to r_forwardPlus 1 only (the legacy path shadows every light, up to %d).\n", MAX_DLIGHTS);
		return;
	}

	if ( s_fp.gpuFailed )
		ri.Printf(PRINT_WARNING, "r_forwardPlus: buffer textures unavailable, legacy dynamic lights are used.\n");

	if ( r_forwardPlusMaxShadowLights->integer > 0 && r_dlightMode->integer < 2 )
		ri.Printf(PRINT_ALL, "Forward+ dynamic light shadows (r_forwardPlusMaxShadowLights) require r_dlightMode 2 (latched, vid_restart).\n");
}

static void R_ForwardPlusBenchmarkFrame( void );

void R_ForwardPlusBeginFrame( void )
{
	if ( !s_fp.initialized )
	{
		// first frame after renderer start
		s_fp.initialized = qtrue;
		s_fp.firstCheck = qtrue;
		s_fp.preparedScene = -1;
		s_fp.benchStep = -1;
		for ( int i = 0; i < MAX_FRAMES; i++ )
			s_fp.pendingTimer[i] = s_fp.readableTimer[i] = -1;
	}

	R_ForwardPlusBenchmarkFrame();
	R_ForwardPlusCheckDependencies();

	s_fp.last = s_fp.current;
	s_fp.last.dropped = s_fp.droppedThisFrame;
	Com_Memset(&s_fp.current, 0, sizeof(s_fp.current));
	s_fp.droppedThisFrame = 0;

	if ( s_fp.last.views > 0 )
	{
		s_fp.buildHistory[s_fp.numBuildSamples % FPLUS_STATS_FRAMES] = s_fp.last.buildMsec;
		s_fp.numBuildSamples++;
	}

	// test lights need cheats (they are not gameplay entities)
	if ( s_fp.testLights > 0 && !ri.Cvar_VariableIntegerValue("sv_cheats") && s_fp.benchStep < 0 )
		s_fp.testLights = 0;

	s_fp.active = (qboolean)(r_forwardPlus->integer != 0 && !s_fp.gpuFailed);

	// LTC area lights depend on this frame's Forward+ state
	R_AreaLightsBeginFrame();
}

/*
============================================================

GPU buffers

============================================================
*/

static const GLenum s_bufferFormats[FPLUS_NUM_BUFFERS] = { GL_RGBA32F, GL_RG32UI, GL_R16UI };
static const int s_texelSizes[FPLUS_NUM_BUFFERS] = { 16, 8, 2 };
static const int s_units[FPLUS_NUM_BUFFERS] = { TB_FPLUS_LIGHTS, TB_FPLUS_GRID, TB_FPLUS_INDICES };

static qboolean R_ForwardPlusCreateBuffers( void )
{
	if ( s_fp.gpuCreated )
		return qtrue;
	if ( s_fp.gpuFailed )
		return qfalse;

	const int maxTexels = glRefConfig.maxTextureBufferSize;
	if ( !qglTexBuffer || maxTexels < FPLUS_LIGHT_CAPACITY )
	{
		s_fp.gpuFailed = qtrue;
		ri.Printf(PRINT_WARNING, "r_forwardPlus: buffer textures unavailable (max %d texels), legacy dynamic lights are used.\n", maxTexels);
		return qfalse;
	}

	s_fp.capacity[FPLUS_BUFFER_LIGHTS] = FPLUS_LIGHT_CAPACITY;
	s_fp.capacity[FPLUS_BUFFER_GRID] = Q_min(maxTexels, FPLUS_GRID_CAPACITY);
	s_fp.capacity[FPLUS_BUFFER_INDICES] = Q_min(maxTexels, FPLUS_INDEX_CAPACITY);

	for ( int f = 0; f < MAX_FRAMES; f++ )
	{
		fplusFrameBuffers_t *fb = &s_fp.frames[f];
		qglGenBuffers(FPLUS_NUM_BUFFERS, fb->buffers);
		for ( int b = 0; b < FPLUS_NUM_BUFFERS; b++ )
		{
			qglBindBuffer(GL_TEXTURE_BUFFER, fb->buffers[b]);
			qglBufferData(GL_TEXTURE_BUFFER,
				(GLsizeiptr)s_fp.capacity[b] * s_texelSizes[b], NULL, GL_DYNAMIC_DRAW);

			image_t *image = &fb->images[b];
			Com_Memset(image, 0, sizeof(*image));
			Q_strncpyz(image->imgName, va("*fplus%d_%d", f, b), sizeof(image->imgName));
			image->flags = IMGFLAG_TEXBUFFER;
			qglGenTextures(1, &image->texnum);
			GL_BindToTMU(image, s_units[b]);
			qglTexBuffer(GL_TEXTURE_BUFFER, s_bufferFormats[b], fb->buffers[b]);
			fb->writeOffset[b] = 0;
		}
		fb->frameStamp = ~0u;
	}
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);

	s_fp.gpuCreated = qtrue;
	ri.Printf(PRINT_DEVELOPER, "Forward+: %d light / %d cluster / %d index texels per frame\n",
		s_fp.capacity[0], s_fp.capacity[1], s_fp.capacity[2]);
	return qtrue;
}

void R_ShutdownForwardPlus( void )
{
	if ( s_fp.gpuCreated )
	{
		for ( int f = 0; f < MAX_FRAMES; f++ )
		{
			fplusFrameBuffers_t *fb = &s_fp.frames[f];
			for ( int b = 0; b < FPLUS_NUM_BUFFERS; b++ )
			{
				for ( int u = 0; u < MAX_TEXTURE_UNITS; u++ )
				{
					if ( glState.currenttextures[u] == (int)fb->images[b].texnum )
						glState.currenttextures[u] = 0;
				}
				qglDeleteTextures(1, &fb->images[b].texnum);
			}
			qglDeleteBuffers(FPLUS_NUM_BUFFERS, fb->buffers);
		}
	}

	// the next renderer start begins from scratch (see R_ForwardPlusBeginFrame)
	const int testLights = s_fp.testLights;
	const float testRadius = s_fp.testRadius;
	Com_Memset(&s_fp, 0, sizeof(s_fp));
	s_fp.testLights = testLights;
	s_fp.testRadius = testRadius;
}

static fplusFrameBuffers_t *RB_ForwardPlusFrameBuffers( const gpuFrame_t *frame )
{
	fplusFrameBuffers_t *fb = &s_fp.frames[(frame - backEndData->frames) % MAX_FRAMES];
	if ( fb->frameStamp != backEndData->realFrameNumber )
	{
		fb->frameStamp = backEndData->realFrameNumber;
		for ( int b = 0; b < FPLUS_NUM_BUFFERS; b++ )
			fb->writeOffset[b] = 0;
	}
	return fb;
}

// appends texels to this frame's buffer, returns the first texel or -1 when full
static int RB_ForwardPlusUpload( fplusFrameBuffers_t *fb, int buffer, const void *data, int numTexels )
{
	if ( numTexels <= 0 )
		return fb->writeOffset[buffer];
	if ( fb->writeOffset[buffer] + numTexels > s_fp.capacity[buffer] )
	{
		if ( !s_fp.warnedCapacity )
		{
			s_fp.warnedCapacity = qtrue;
			ri.Printf(PRINT_WARNING, "Forward+: per frame buffer full (buffer %d), views without cluster lists get no dynamic light. Increase r_forwardPlusTileSize or lower r_forwardPlusSlices.\n", buffer);
		}
		return -1;
	}

	const int first = fb->writeOffset[buffer];
	qglBindBuffer(GL_TEXTURE_BUFFER, fb->buffers[buffer]);
	qglBufferSubData(GL_TEXTURE_BUFFER, (GLintptr)first * s_texelSizes[buffer],
		(GLsizeiptr)numTexels * s_texelSizes[buffer], data);
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);
	fb->writeOffset[buffer] += numTexels;
	return first;
}

/*
============================================================

Scene: importance order, shadow slots, legacy light list

============================================================
*/

static float R_DlightLuminance( const dlight_t *dl )
{
	return 0.2126f * dl->color[0] + 0.7152f * dl->color[1] + 0.0722f * dl->color[2];
}

// roughly the light's solid angle (screen size) times its brightness; lights
// the camera is inside of matter most
static float R_DlightImportance( const dlight_t *dl, const vec3_t viewOrigin )
{
	if ( dl->areaType != DLIGHT_POINT )
	{
		// Radiance times projected emitter area estimates its solid-angle
		// contribution. The influence radius is only a culling bound.
		vec3_t delta, closest;
		VectorSubtract(viewOrigin, dl->origin, delta);
		VectorCopy(dl->origin, closest);
		VectorMA(closest, Com_Clamp(-dl->halfWidth, dl->halfWidth,
			DotProduct(delta, dl->areaRight)), dl->areaRight, closest);
		if ( dl->areaType == DLIGHT_RECT )
			VectorMA(closest, Com_Clamp(-dl->halfHeight, dl->halfHeight,
				DotProduct(delta, dl->areaUp)), dl->areaUp, closest);
		const float area = 4.0f * dl->halfWidth * dl->halfHeight;
		const float distSq = DistanceSquared(viewOrigin, closest);
		const float halfDiagonal = sqrtf(dl->halfWidth * dl->halfWidth +
			dl->halfHeight * dl->halfHeight);
		const float range = Q_max(dl->radius - halfDiagonal, 1.0f);
		const float d2 = distSq / (range * range);
		const float window = Q_max(0.0f, 1.0f - d2 * d2);
		return Q_max(R_DlightLuminance(dl), 0.0f) * area /
			Q_max(distSq, area) * window * window;
	}
	const float radius = Q_max(dl->radius, 1.0f);
	const float distSq = DistanceSquared(dl->origin, viewOrigin);
	const float minDistSq = 0.0625f * radius * radius;
	float importance = Q_max(R_DlightLuminance(dl), 0.01f) * radius * radius / Q_max(distSq, minDistSq);
	if ( distSq < radius * radius )
		importance *= 4.0f;
	return importance;
}

static int R_MatchShadowHistory( const dlight_t *dl )
{
	for ( int h = 0; h < s_fp.numShadowHistory; h++ )
	{
		const fplusShadowHistory_t *hist = &s_fp.shadowHistory[h];
		const float tolerance = Q_max(16.0f, 0.25f * Q_max(dl->radius, hist->radius));
		if ( DistanceSquared(dl->origin, hist->origin) > tolerance * tolerance )
			continue;
		if ( fabsf(dl->color[0] - hist->color[0]) > 0.25f ||
			fabsf(dl->color[1] - hist->color[1]) > 0.25f ||
			fabsf(dl->color[2] - hist->color[2]) > 0.25f )
		{
			continue;
		}
		return h;
	}
	return -1;
}

static void R_ForwardPlusSelectShadows( const trRefdef_t *refdef )
{
	const int n = s_fp.numLights;
	for ( int i = 0; i < n; i++ )
		s_fp.shadowSlot[i] = -1;
	s_fp.numSlots = 0;
	s_fp.numShadowCubes = 0;

	const qboolean worldScene = (qboolean)!(refdef->rdflags & RDF_NOWORLDMODEL);
	int budget = Com_Clampi(0, MAX_DLIGHT_SHADOWS, r_forwardPlusMaxShadowLights->integer);
	if ( r_dlightMode->integer < 2 || !tr.pointShadowArrayImage || !worldScene )
		budget = 0;

	// hysteresis: lights shadowed last frame score 1.3x and keep their slot,
	// so near-equal lights do not trade the shadow back and forth
	int history[MAX_RENDER_DLIGHTS];
	float score[MAX_RENDER_DLIGHTS];
	int candidates[MAX_RENDER_DLIGHTS];
	for ( int i = 0; i < n; i++ )
	{
		history[i] = budget ? R_MatchShadowHistory(refdef->dlights + i) : -1;
		score[i] = s_fp.importance[i] * (history[i] >= 0 ? 1.3f : 1.0f);
		// area lights are unshadowed (LTC is not an area shadow)
		if ( refdef->dlights[i].areaType != DLIGHT_POINT )
			score[i] = -1.0f;
		// spot lights without shadow (SPOTLIGHT_NOSHADOW, r_spotLightShadows 0)
		if ( !R_DlightCastsShadow(refdef->dlights + i) )
			score[i] = -1.0f;
		candidates[i] = i;
	}

	const int count = Q_min(budget, n);
	std::partial_sort(candidates, candidates + count, candidates + n,
		[&score](int a, int b) { return score[a] != score[b] ? score[a] > score[b] : a < b; });

	qboolean slotUsed[MAX_DLIGHT_SHADOWS] = {};
	for ( int k = 0; k < count; k++ )
	{
		const int light = candidates[k];
		if ( score[light] < 0.0f )
			continue;
		if ( history[light] >= 0 )
		{
			const int slot = s_fp.shadowHistory[history[light]].slot;
			if ( slot < budget && !slotUsed[slot] )
			{
				slotUsed[slot] = qtrue;
				s_fp.shadowSlot[light] = slot;
			}
		}
	}
	int nextFree = 0;
	for ( int k = 0; k < count; k++ )
	{
		const int light = candidates[k];
		if ( s_fp.shadowSlot[light] >= 0 || score[light] < 0.0f )
			continue;
		while ( slotUsed[nextFree] )
			nextFree++;
		slotUsed[nextFree] = qtrue;
		s_fp.shadowSlot[light] = nextFree;
	}

	for ( int s = 0; s < MAX_DLIGHT_SHADOWS; s++ )
		s_fp.slotLight[s] = -1;
	for ( int i = 0; i < n; i++ )
	{
		if ( s_fp.shadowSlot[i] >= 0 )
		{
			s_fp.slotLight[s_fp.shadowSlot[i]] = i;
			s_fp.numSlots++;
			s_fp.numShadowCubes = Q_max(s_fp.numShadowCubes, s_fp.shadowSlot[i] + 1);
		}
	}

	// the first world scene of the frame owns the history
	if ( worldScene && backEndData->currentFrame->currentScene == 0 )
	{
		s_fp.numShadowHistory = 0;
		for ( int i = 0; i < n; i++ )
		{
			if ( s_fp.shadowSlot[i] < 0 )
				continue;
			fplusShadowHistory_t *hist = &s_fp.shadowHistory[s_fp.numShadowHistory++];
			VectorCopy(refdef->dlights[i].origin, hist->origin);
			VectorCopy(refdef->dlights[i].color, hist->color);
			hist->radius = refdef->dlights[i].radius;
			hist->slot = s_fp.shadowSlot[i];
		}
	}
}

void R_ForwardPlusPrepareScene( const trRefdef_t *refdef )
{
	const int n = Q_min(refdef->num_dlights, MAX_RENDER_DLIGHTS);
	if ( !s_fp.active )
		return;
	if ( s_fp.preparedScene == tr.sceneCount && s_fp.preparedLights == refdef->dlights &&
		s_fp.numLights == n )
	{
		return;
	}
	s_fp.preparedScene = tr.sceneCount;
	s_fp.preparedLights = refdef->dlights;

	s_fp.numLights = n;
	for ( int i = 0; i < n; i++ )
	{
		s_fp.importance[i] = R_DlightImportance(refdef->dlights + i, refdef->vieworg);
		s_fp.order[i] = i;
	}
	std::sort(s_fp.order, s_fp.order + n, [](int a, int b) {
		return s_fp.importance[a] != s_fp.importance[b] ?
			s_fp.importance[a] > s_fp.importance[b] : a < b;
	});

	R_ForwardPlusSelectShadows(refdef);
}

// number of shadow cubes to draw; unused slots below it have no light (-1)
int R_ForwardPlusNumShadowSlots( void )
{
	return s_fp.active ? s_fp.numShadowCubes : 0;
}

int R_ForwardPlusShadowSlotLight( int slot )
{
	if ( slot < 0 || slot >= MAX_DLIGHT_SHADOWS )
		return -1;
	return s_fp.slotLight[slot];
}

int R_GetUboDlights( const trRefdef_t *refdef, int *lightIndexes, int *shadowLayers )
{
	return R_GetDlightList(refdef, lightIndexes, shadowLayers, MAX_DLIGHTS);
}

// the point lights of the scene, most important first (Forward+) or in scene
// order (legacy), at most maxLights; shadowLayers: cube layer or -1
int R_GetDlightList( const trRefdef_t *refdef, int *lightIndexes, int *shadowLayers, int maxLights )
{
	if ( !s_fp.active )
	{
		// legacy: unchanged, light i uses shadow cube i
		const int n = Q_min(Q_min(refdef->num_dlights, MAX_DLIGHTS), maxLights);
		for ( int i = 0; i < n; i++ )
		{
			lightIndexes[i] = i;
			shadowLayers[i] = i;
		}
		return n;
	}

	R_ForwardPlusPrepareScene(refdef);
	// point lights only: the legacy Lights block has no area light shape
	int n = 0;
	for ( int k = 0; k < s_fp.numLights && n < maxLights; k++ )
	{
		const int light = s_fp.order[k];
		if ( refdef->dlights[light].areaType != DLIGHT_POINT )
			continue;
		lightIndexes[n] = light;
		shadowLayers[n] = s_fp.shadowSlot[light];
		n++;
	}
	return n;
}

/*
============================================================

Cluster lists

============================================================
*/

struct fplusSlicing_t
{
	int slices;
	float nearSlice;
	float scale;
	float bias;
};

// must match FPlusSlice in lightall.glsl
static int R_ForwardPlusSlice( const fplusSlicing_t *s, float depth )
{
	if ( s->slices <= 1 || depth <= s->nearSlice )
		return 0;
	const int slice = 1 + (int)floorf(logf(depth) * s->scale + s->bias);
	return Com_Clampi(1, s->slices - 1, slice);
}

static void R_TransformPoint( const float *m, const vec3_t in, float *out )
{
	for ( int r = 0; r < 3; r++ )
		out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
}

// Conservative cluster range of a light sphere, using the view's own matrices
// (mirrors, off-centre and jittered projections included). XY: the projected
// eye space bounding box of the sphere, the whole viewport once the sphere
// reaches the near plane. False when the light cannot touch the view.
static qboolean R_ForwardPlusLightRange(
	const viewParms_t *view, const dlight_t *dl, const fplusSlicing_t *slicing,
	float tileSize, int tilesX, int tilesY, fplusLightRange_t *range )
{
	if ( dl->radius <= 0.0f )
		return qfalse;
	// spot lights: the sphere around the cone (tr_spotlight.cpp)
	vec3_t center;
	float radius;
	R_SpotBoundingSphere(dl, center, &radius);

	float eye[3];
	R_TransformPoint(view->world.modelViewMatrix, center, eye);
	const float depth = -eye[2];
	if ( depth + radius <= view->zNear || depth - radius >= view->zFar )
		return qfalse;

	range->z0 = R_ForwardPlusSlice(slicing, Q_max(depth - radius, 0.0f));
	range->z1 = R_ForwardPlusSlice(slicing, depth + radius);

	range->x0 = 0;
	range->x1 = tilesX - 1;
	range->y0 = 0;
	range->y1 = tilesY - 1;
	if ( depth - radius <= view->zNear )
		return qtrue;

	const float *p = view->projectionMatrix;
	float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
	for ( int c = 0; c < 8; c++ )
	{
		const float x = eye[0] + ((c & 1) ? radius : -radius);
		const float y = eye[1] + ((c & 2) ? radius : -radius);
		const float z = eye[2] + ((c & 4) ? radius : -radius);
		const float cx = p[0] * x + p[4] * y + p[8] * z + p[12];
		const float cy = p[1] * x + p[5] * y + p[9] * z + p[13];
		const float cw = p[3] * x + p[7] * y + p[11] * z + p[15];
		if ( cw <= 1e-4f )
			return qtrue;	// degenerate, keep the whole viewport
		minX = Q_min(minX, cx / cw);
		maxX = Q_max(maxX, cx / cw);
		minY = Q_min(minY, cy / cw);
		maxY = Q_max(maxY, cy / cw);
	}
	if ( maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f )
		return qfalse;

	const float width = (float)view->viewportWidth;
	const float height = (float)view->viewportHeight;
	range->x0 = Com_Clampi(0, tilesX - 1, (int)floorf((minX * 0.5f + 0.5f) * width / tileSize));
	range->x1 = Com_Clampi(0, tilesX - 1, (int)floorf((maxX * 0.5f + 0.5f) * width / tileSize));
	range->y0 = Com_Clampi(0, tilesY - 1, (int)floorf((minY * 0.5f + 0.5f) * height / tileSize));
	range->y1 = Com_Clampi(0, tilesY - 1, (int)floorf((maxY * 0.5f + 0.5f) * height / tileSize));
	return qtrue;
}

static void RB_ForwardPlusBuildView(
	fplusFrameBuffers_t *fb, const viewParms_t *view, const trRefdef_t *refdef,
	int lightBase, qboolean collectStats )
{
	fplusView_t *out = &s_fp.views[view->currentViewParm];
	Com_Memset(out, 0, sizeof(*out));
	if ( view->viewportWidth <= 0 || view->viewportHeight <= 0 )
		return;

	const int tileSize = Com_Clampi(16, 256, r_forwardPlusTileSize->integer);
	const int tilesX = (view->viewportWidth + tileSize - 1) / tileSize;
	const int tilesY = (view->viewportHeight + tileSize - 1) / tileSize;
	const int maxPerCluster = Com_Clampi(1, 255, r_forwardPlusMaxLightsPerCluster->integer);

	fplusSlicing_t slicing;
	slicing.slices = Com_Clampi(1, 64, r_forwardPlusSlices->integer);
	slicing.nearSlice = Q_max(1.0f, r_forwardPlusNearSlice->value);
	const float farZ = Q_max(view->zFar, slicing.nearSlice * 2.0f);
	slicing.scale = slicing.slices > 1 ?
		(float)(slicing.slices - 1) / logf(farZ / slicing.nearSlice) : 0.0f;
	slicing.bias = -logf(slicing.nearSlice) * slicing.scale;

	const int numClusters = tilesX * tilesY * slicing.slices;

	// ranges in importance order
	s_ranges.clear();
	for ( int k = 0; k < s_fp.numLights; k++ )
	{
		fplusLightRange_t range;
		range.light = s_fp.order[k];
		if ( R_ForwardPlusLightRange(view, refdef->dlights + range.light, &slicing,
				(float)tileSize, tilesX, tilesY, &range) )
		{
			s_ranges.push_back(range);
		}
	}

	// pass 1: counts, capped per cluster (less important lights drop out)
	s_counts.assign(numClusters, 0);
	int overflowRefs = 0;
	for ( const fplusLightRange_t& r : s_ranges )
	{
		for ( int z = r.z0; z <= r.z1; z++ )
			for ( int y = r.y0; y <= r.y1; y++ )
			{
				uint32_t *row = &s_counts[(z * tilesY + y) * tilesX];
				for ( int x = r.x0; x <= r.x1; x++ )
				{
					if ( row[x] < (uint32_t)maxPerCluster )
						row[x]++;
					else
						overflowRefs++;
				}
			}
	}

	// prefix sum
	s_offsets.resize(numClusters);
	uint32_t total = 0;
	for ( int c = 0; c < numClusters; c++ )
	{
		s_offsets[c] = total;
		total += s_counts[c];
	}

	// pass 2: fill, same order and cap
	s_indexData.resize(Q_max(total, 1u));
	s_cursor.assign(numClusters, 0);
	for ( const fplusLightRange_t& r : s_ranges )
	{
		for ( int z = r.z0; z <= r.z1; z++ )
			for ( int y = r.y0; y <= r.y1; y++ )
			{
				const int rowStart = (z * tilesY + y) * tilesX;
				for ( int x = r.x0; x <= r.x1; x++ )
				{
					const int c = rowStart + x;
					if ( s_cursor[c] < s_counts[c] )
						s_indexData[s_offsets[c] + s_cursor[c]++] = (uint16_t)r.light;
				}
			}
	}

	const int indexBase = RB_ForwardPlusUpload(fb, FPLUS_BUFFER_INDICES, s_indexData.data(), (int)total);
	if ( indexBase < 0 )
		return;

	s_gridData.resize(numClusters * 2);
	for ( int c = 0; c < numClusters; c++ )
	{
		s_gridData[c * 2 + 0] = (uint32_t)indexBase + s_offsets[c];
		s_gridData[c * 2 + 1] = s_counts[c];
	}
	const int gridBase = RB_ForwardPlusUpload(fb, FPLUS_BUFFER_GRID, s_gridData.data(), numClusters);
	if ( gridBase < 0 )
		return;

	out->enabled = qtrue;
	out->grid[0] = gridBase;
	out->grid[1] = lightBase;
	out->grid[2] = tilesX;
	out->grid[3] = tilesY;
	VectorSet4(out->params, (float)tileSize, (float)slicing.slices, slicing.scale, slicing.bias);
	VectorSet4(out->params2, (float)view->viewportX, (float)view->viewportY, 1.0f, slicing.nearSlice);
	VectorSet4(out->debug,
		(float)Com_Clampi(0, 9, r_forwardPlusDebug->integer),
		(float)r_forwardPlusDebugLight->integer,
		(float)maxPerCluster, R_AreaLightsDebugParam());

	s_fp.current.views++;
	if ( collectStats )
	{
		fplusStats_t *st = &s_fp.current;
		st->clusters = numClusters;
		st->lightRefs = (int)total;
		st->overflowRefs = overflowRefs;
		for ( int c = 0; c < numClusters; c++ )
		{
			if ( s_counts[c] )
				st->nonEmptyClusters++;
			if ( (int)s_counts[c] > st->maxPerCluster )
				st->maxPerCluster = (int)s_counts[c];
		}
		// clusters that hit the cap (some of them lost lights)
		if ( overflowRefs )
		{
			for ( int c = 0; c < numClusters; c++ )
				if ( (int)s_counts[c] >= maxPerCluster )
					st->overflowClusters++;
		}
	}
}

void RB_UpdateForwardPlus( gpuFrame_t *frame, const trRefdef_t *refdef )
{
	for ( int i = 0; i < tr.numCachedViewParms; i++ )
	{
		const int v = tr.cachedViewParms[i].currentViewParm;
		if ( v >= 0 && v < (int)FPLUS_MAX_VIEWS )
			s_fp.views[v].enabled = qfalse;
	}

	if ( !s_fp.active || !R_ForwardPlusCreateBuffers() )
		return;

	const auto start = std::chrono::steady_clock::now();

	R_ForwardPlusPrepareScene(refdef);
	fplusFrameBuffers_t *fb = RB_ForwardPlusFrameBuffers(frame);

	// light data of the scene, shared by its views
	vec4_t lightData[MAX_RENDER_DLIGHTS * FPLUS_LIGHT_TEXELS];
	const qboolean cookiesActive = R_LightCookiesActive();
	for ( int i = 0; i < s_fp.numLights; i++ )
	{
		const dlight_t *dl = refdef->dlights + i;
		float *t = lightData[i * FPLUS_LIGHT_TEXELS];
		// point:  origin, radius | color, 0 | shadow slot, cookie layer (-1
		//         none), cos inner, projected spot shadow | cone axis, cos
		//         outer | cookie roll, 0, 0, 0 (tr_lightcookie.cpp)
		//         (tr_spotlight.cpp; point lights: cos inner -1, cos outer -2)
		// area:   centre, cull radius | radiance, type | -1, flags, half width,
		//         half height | right | up (tr_arealights.cpp)
		VectorSet4(t + 0, dl->origin[0], dl->origin[1], dl->origin[2], dl->radius);
		VectorSet4(t + 4, dl->color[0], dl->color[1], dl->color[2], (float)dl->areaType);
		if ( dl->areaType == DLIGHT_POINT )
		{
			const float projected = (dl->spotShadowSlot >= 0 && dl->spotShadowSlot == s_fp.shadowSlot[i]) ? 1.0f : 0.0f;
			VectorSet4(t + 8, (float)s_fp.shadowSlot[i], cookiesActive ? (float)dl->cookieLayer : -1.0f,
				dl->spotCosInner, projected);
		}
		else
			VectorSet4(t + 8, (float)s_fp.shadowSlot[i], (float)dl->areaFlags, dl->halfWidth, dl->halfHeight);
		if ( dl->areaType != DLIGHT_POINT )
		{
			// the window reaches range = cull radius - half diagonal
			t[3] = dl->radius - sqrtf(dl->halfWidth * dl->halfWidth + dl->halfHeight * dl->halfHeight);
			VectorSet4(t + 12, dl->areaRight[0], dl->areaRight[1], dl->areaRight[2], 0.0f);
			VectorSet4(t + 16, dl->areaUp[0], dl->areaUp[1], dl->areaUp[2], 0.0f);
		}
		else
		{
			VectorSet4(t + 12, dl->spotDir[0], dl->spotDir[1], dl->spotDir[2], dl->spotCosOuter);
			VectorSet4(t + 16, dl->cookieRoll, 0.0f, 0.0f, 0.0f);
		}
	}
	const int lightBase = RB_ForwardPlusUpload(fb, FPLUS_BUFFER_LIGHTS, lightData,
		s_fp.numLights * FPLUS_LIGHT_TEXELS);
	if ( lightBase < 0 )
		return;

	const qboolean mainScene = (qboolean)(frame->currentScene == 0 && !(refdef->rdflags & RDF_NOWORLDMODEL));
	for ( int i = 0; i < tr.numCachedViewParms; i++ )
	{
		const viewParms_t *view = &tr.cachedViewParms[i];
		if ( view->flags & VPF_DEPTHSHADOW )
			continue;
		if ( view->viewParmType != VPT_MAIN &&
			view->viewParmType != VPT_PORTAL &&
			view->viewParmType != VPT_SKYPORTAL )
		{
			continue;
		}
		if ( view->currentViewParm < 0 || view->currentViewParm >= (int)FPLUS_MAX_VIEWS )
			continue;

		RB_ForwardPlusBuildView(fb, view, refdef, lightBase,
			(qboolean)(mainScene && view->viewParmType == VPT_MAIN));
	}

	if ( mainScene )
	{
		s_fp.current.lights = s_fp.numLights;
		s_fp.current.shadowed = s_fp.numSlots;
	}
	s_fp.current.buildMsec += std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

void RB_ForwardPlusCameraParams( int viewParm, CameraBlock *cameraBlock )
{
	if ( !s_fp.active || viewParm < 0 || viewParm >= (int)FPLUS_MAX_VIEWS || !s_fp.views[viewParm].enabled )
		return;	// zeroed block: enabled = 0

	const fplusView_t *v = &s_fp.views[viewParm];
	for ( int i = 0; i < 4; i++ )
		cameraBlock->fplusGrid[i] = v->grid[i];
	VectorCopy4(v->params, cameraBlock->fplusParams);
	VectorCopy4(v->params2, cameraBlock->fplusParams2);
	VectorCopy4(v->debug, cameraBlock->fplusDebug);
}

qboolean RB_ForwardPlusViewEnabled( int viewParm )
{
	if ( !s_fp.active || viewParm < 0 || viewParm >= (int)FPLUS_MAX_VIEWS )
		return qfalse;
	return s_fp.views[viewParm].enabled;
}

void RB_ForwardPlusBindTextures( SamplerBindingsWriter& samplers )
{
	fplusFrameBuffers_t *fb = &s_fp.frames[(backEndData->currentFrame - backEndData->frames) % MAX_FRAMES];
	for ( int b = 0; b < FPLUS_NUM_BUFFERS; b++ )
		samplers.AddStaticImage(&fb->images[b], s_units[b]);
}

qboolean RB_ForwardPlusDebugBypassesToneMap( void )
{
	if ( R_AreaLightsActive() && r_ltcDebug->integer >= 1 && r_ltcDebug->integer <= 8 &&
		r_ltcDebug->integer != 6 && r_ltcDebug->integer != 7 )
	{
		return qtrue;	// r_ltcDebug views written unlit
	}
	return (qboolean)(s_fp.active && r_forwardPlusDebug->integer >= 1 && r_forwardPlusDebug->integer <= 9);
}

/*
============================================================

GPU time of the main view (the forward lighting pass)

============================================================
*/

void R_ForwardPlusSetMainViewTimer( int timerHandle )
{
	if ( !backEndData->currentFrame || backEndData->currentFrame->currentScene != 0 )
		return;
	const int f = (int)(backEndData->currentFrame - backEndData->frames) % MAX_FRAMES;
	s_fp.pendingTimer[f] = timerHandle;
}

// called before the frame slot's timers are reused: they hold the frame that
// last ran in this slot (MAX_FRAMES ago)
void R_ForwardPlusCollectGpuTimes( gpuFrame_t *frame )
{
	const int f = (int)(frame - backEndData->frames) % MAX_FRAMES;
	const int handle = s_fp.readableTimer[f];
	s_fp.readableTimer[f] = s_fp.pendingTimer[f];
	s_fp.pendingTimer[f] = -1;

	if ( !glRefConfig.timerQuery || handle < 0 || handle >= frame->numTimedBlocks )
		return;

	const gpuTimedBlock_t *block = frame->timedBlocks + handle;
	if ( !block->beginTimer || !block->endTimer )
		return;

	GLuint available = 0;
	qglGetQueryObjectuiv(block->endTimer, GL_QUERY_RESULT_AVAILABLE, &available);
	if ( !available )
		return;

	GLuint64 startTime = 0, endTime = 0;
	qglGetQueryObjectui64v(block->beginTimer, GL_QUERY_RESULT, &startTime);
	qglGetQueryObjectui64v(block->endTimer, GL_QUERY_RESULT, &endTime);
	const float msec = (float)((double)(endTime - startTime) / 1e6);
	s_fp.gpuHistory[s_fp.numGpuSamples % FPLUS_STATS_FRAMES] = msec;
	s_fp.numGpuSamples++;

	if ( s_fp.benchStep >= 0 && s_fp.benchFrame >= 0 )
	{
		s_fp.benchGpu += msec;
		s_fp.benchGpuSamples++;
	}
}

static float R_ForwardPlusAverageGpu( void )
{
	const int n = Q_min(s_fp.numGpuSamples, FPLUS_STATS_FRAMES);
	float sum = 0.0f;
	for ( int i = 0; i < n; i++ )
		sum += s_fp.gpuHistory[i];
	return n ? sum / n : -1.0f;
}

static double R_ForwardPlusAverageBuild( void )
{
	const int n = Q_min(s_fp.numBuildSamples, FPLUS_STATS_FRAMES);
	double sum = 0.0;
	for ( int i = 0; i < n; i++ )
		sum += s_fp.buildHistory[i];
	return n ? sum / n : 0.0;
}

/*
============================================================

Commands

============================================================
*/

void R_ForwardPlusStats_f( void )
{
	const fplusStats_t *st = &s_fp.last;
	ri.Printf(PRINT_ALL, "Forward+ (r_forwardPlus %d, %s)\n", r_forwardPlus->integer,
		s_fp.active ? "active" : (s_fp.gpuFailed ? "unavailable" : "legacy path"));
	if ( !s_fp.active )
	{
		ri.Printf(PRINT_ALL, "  capacity %d lights per frame (legacy), dropped last frame: %d\n",
			LEGACY_DLIGHT_LIMIT, st->dropped);
		return;
	}

	ri.Printf(PRINT_ALL, "  lights: %d in main scene, %d dropped (capacity %d per frame)\n",
		st->lights, st->dropped, MAX_RENDER_DLIGHTS);
	ri.Printf(PRINT_ALL, "  clusters: %d (tile %d px, %d slices), %d non-empty, %d views built\n",
		st->clusters, Com_Clampi(16, 256, r_forwardPlusTileSize->integer),
		Com_Clampi(1, 64, r_forwardPlusSlices->integer), st->nonEmptyClusters, st->views);
	ri.Printf(PRINT_ALL, "  lights per cluster: avg %.2f (non-empty %.2f), max %d, cap %d\n",
		st->clusters ? (float)st->lightRefs / st->clusters : 0.0f,
		st->nonEmptyClusters ? (float)st->lightRefs / st->nonEmptyClusters : 0.0f,
		st->maxPerCluster, Com_Clampi(1, 255, r_forwardPlusMaxLightsPerCluster->integer));
	ri.Printf(PRINT_ALL, "  overflow: %d clusters full, %d light/cluster pairs dropped\n",
		st->overflowClusters, st->overflowRefs);
	ri.Printf(PRINT_ALL, "  shadowed lights: %d (r_forwardPlusMaxShadowLights %d, r_dlightMode %d)\n",
		st->shadowed, r_forwardPlusMaxShadowLights->integer, r_dlightMode->integer);
	ri.Printf(PRINT_ALL, "  CPU cluster build: %.3f ms last frame, %.3f ms avg\n",
		st->buildMsec, R_ForwardPlusAverageBuild());
	const float gpu = R_ForwardPlusAverageGpu();
	if ( gpu >= 0.0f )
		ri.Printf(PRINT_ALL, "  GPU main view (lighting pass): %.3f ms avg\n", gpu);
	else
		ri.Printf(PRINT_ALL, "  GPU main view: no timer queries\n");
}

static float R_TestLightRand( int i, int k )
{
	unsigned int h = (unsigned int)(i * 747796405u + k * 2891336453u + 12345u);
	h = (h ^ (h >> 16)) * 0x45d9f3bu;
	h = (h ^ (h >> 16)) * 0x45d9f3bu;
	h ^= h >> 16;
	return (float)(h & 0xffff) / 65535.0f;
}

void R_ForwardPlusAddTestLights( const refdef_t *fd )
{
	if ( s_fp.testLights <= 0 || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) )
		return;

	const float spread = s_fp.testRadius > 0.0f ? s_fp.testRadius : 768.0f;
	const float t = fd->time * 0.001f;
	for ( int i = 0; i < s_fp.testLights; i++ )
	{
		// deterministic points in a sphere around the camera, slowly orbiting
		const float u = R_TestLightRand(i, 0) * 2.0f - 1.0f;
		const float phi = R_TestLightRand(i, 1) * 2.0f * (float)M_PI + t * (0.2f + 0.3f * R_TestLightRand(i, 2));
		const float dist = spread * (0.15f + 0.85f * sqrtf(R_TestLightRand(i, 3)));
		const float s = sqrtf(Q_max(0.0f, 1.0f - u * u));
		vec3_t org;
		org[0] = fd->vieworg[0] + dist * s * cosf(phi);
		org[1] = fd->vieworg[1] + dist * s * sinf(phi);
		org[2] = fd->vieworg[2] + dist * u * 0.35f;

		const float hue = R_TestLightRand(i, 4) * 6.0f;
		vec3_t color = {
			Com_Clamp(0.0f, 1.0f, fabsf(hue - 3.0f) - 1.0f),
			Com_Clamp(0.0f, 1.0f, 2.0f - fabsf(hue - 2.0f)),
			Com_Clamp(0.0f, 1.0f, 2.0f - fabsf(hue - 4.0f)) };
		const float radius = 120.0f + 200.0f * R_TestLightRand(i, 5);
		RE_AddLightToScene(org, radius, color[0], color[1], color[2]);
	}
}

void R_SpawnTestLights_f( void )
{
	if ( !ri.Cvar_VariableIntegerValue("sv_cheats") )
	{
		ri.Printf(PRINT_ALL, "r_forwardPlusSpawnTestLights is cheat protected (sv_cheats 1).\n");
		return;
	}
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: r_forwardPlusSpawnTestLights <count 0-%d> [spread radius]\n", MAX_RENDER_DLIGHTS);
		return;
	}
	s_fp.testLights = Com_Clampi(0, MAX_RENDER_DLIGHTS, atoi(ri.Cmd_Argv(1)));
	s_fp.testRadius = ri.Cmd_Argc() > 2 ? (float)atof(ri.Cmd_Argv(2)) : 0.0f;
	ri.Printf(PRINT_ALL, "%d renderer test lights around the camera%s\n", s_fp.testLights,
		(!r_forwardPlus->integer && s_fp.testLights > LEGACY_DLIGHT_LIMIT) ?
			va(" (legacy path keeps the first %d; r_forwardPlus 1 for more)", LEGACY_DLIGHT_LIMIT) : "");
}

/*
============================================================

r_forwardPlusBenchmark: legacy 8/32, Forward+ 8..256 test lights

============================================================
*/

#define BENCH_WARMUP_FRAMES 30

static void R_ForwardPlusBenchmarkStartStep( void )
{
	ri.Cvar_Set("r_forwardPlus", s_benchSteps[s_fp.benchStep].forwardPlus ? "1" : "0");
	s_fp.testLights = s_benchSteps[s_fp.benchStep].lights;
	s_fp.benchFrame = -BENCH_WARMUP_FRAMES;
	s_fp.benchBuild = 0.0;
	s_fp.benchGpu = 0.0;
	s_fp.benchGpuSamples = 0;
}

static void R_ForwardPlusBenchmarkFrame( void )
{
	if ( s_fp.benchStep < 0 )
		return;

	if ( s_fp.benchFrame == 0 )
		s_fp.benchStartTime = ri.Milliseconds();
	if ( s_fp.benchFrame > 0 )
		s_fp.benchBuild += s_fp.current.buildMsec;	// frame that just ended
	s_fp.benchFrame++;

	if ( s_fp.benchFrame <= s_fp.benchFrames )
		return;

	double *result = s_fp.benchResults[s_fp.benchStep];
	result[0] = s_fp.benchBuild / s_fp.benchFrames;
	result[1] = s_fp.benchGpuSamples ? s_fp.benchGpu / s_fp.benchGpuSamples : -1.0;
	result[2] = (double)(ri.Milliseconds() - s_fp.benchStartTime) / s_fp.benchFrames;
	result[3] = s_fp.benchGpuSamples;

	s_fp.benchStep++;
	if ( s_fp.benchStep < (int)ARRAY_LEN(s_benchSteps) )
	{
		R_ForwardPlusBenchmarkStartStep();
		return;
	}

	ri.Printf(PRINT_ALL, "\nForward+ benchmark (%d frames per step, %d px tiles, %d slices)\n",
		s_fp.benchFrames, Com_Clampi(16, 256, r_forwardPlusTileSize->integer),
		Com_Clampi(1, 64, r_forwardPlusSlices->integer));
	ri.Printf(PRINT_ALL, "  mode       lights  cluster build (CPU)  main view (GPU)  frame\n");
	for ( int i = 0; i < (int)ARRAY_LEN(s_benchSteps); i++ )
	{
		const double *r = s_fp.benchResults[i];
		ri.Printf(PRINT_ALL, "  %-9s  %6d  %15.3f ms  %12s  %6.2f ms\n",
			s_benchSteps[i].forwardPlus ? "Forward+" : "legacy", s_benchSteps[i].lights,
			r[0], r[1] >= 0.0 ? va("%.3f ms", r[1]) : "n/a", r[2]);
	}
	ri.Printf(PRINT_ALL, "(legacy shows at most %d lights; frame time includes vsync and the game)\n", LEGACY_DLIGHT_LIMIT);

	ri.Cvar_Set("r_forwardPlus", va("%d", s_fp.benchSavedMode));
	s_fp.testLights = s_fp.benchSavedTestLights;
	s_fp.benchStep = -1;
}

void R_ForwardPlusBenchmark_f( void )
{
	if ( !ri.Cvar_VariableIntegerValue("sv_cheats") )
	{
		ri.Printf(PRINT_ALL, "r_forwardPlusBenchmark is cheat protected (sv_cheats 1, it spawns test lights).\n");
		return;
	}
	if ( s_fp.benchStep >= 0 )
	{
		ri.Printf(PRINT_ALL, "r_forwardPlusBenchmark already running.\n");
		return;
	}

	s_fp.benchFrames = ri.Cmd_Argc() > 1 ? Com_Clampi(10, 10000, atoi(ri.Cmd_Argv(1))) : 120;
	s_fp.benchSavedMode = r_forwardPlus->integer;
	s_fp.benchSavedTestLights = s_fp.testLights;
	s_fp.benchStep = 0;
	R_ForwardPlusBenchmarkStartStep();
	ri.Printf(PRINT_ALL, "Forward+ benchmark: %d steps x %d frames, keep the camera still.\n",
		(int)ARRAY_LEN(s_benchSteps), s_fp.benchFrames + BENCH_WARMUP_FRAMES);
}
