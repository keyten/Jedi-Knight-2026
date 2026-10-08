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

// tr_liquid.cpp -- liquid media of the froxel fog (r_volumetricWater)
//
// The water / slime / lava brushes of the world model become participating
// media of the froxel volume (tr_volumetric.cpp) without any rebake: the BSP
// keeps every brush with its sides, and the contents of a brush are the
// contentFlags of its BSP shader (the same test as R_LoadWeatherZones). A
// brush is convex, the inside of all its side planes, so the GPU clips the
// ray of a froxel against it exactly (glsl/liquid_common.glsl): a froxel cut
// by the water surface gets the covered fraction of its depth, not a step.
//
// Data:
//   world_t::liquids / liquidPlanes  every liquid brush of model 0 (map load)
//   plane buffer (TB_LIQUIDPLANES)   RGBA32F texels (normal, dist), static per map
//   Liquids block (per scene)         the MAX_GPU_LIQUIDS nearest brushes in the
//                                     froxel frustum, per slice 32-bit masks,
//                                     the media of the three classes, caustics
//   caustic pattern (TB_LIQUIDCAUSTICS)  tiling R16F, mean 1, generated once
//
// The same block lights the underwater surfaces of lightall (USE_LIQUID_SUN):
// the sun reaching a point inside a liquid brush passes the liquid above it
// (exact path length to the surface, transmittance and caustics).
//
// Class and medium: the class of a brush (its contents) is what gameplay sees,
// it selects the brush with the r_volumetricWater mask and tells cgame which
// tint to drop. The medium slot is the optics it gets (one of the three
// profiles): its class, except a water brush whose upward side (the drawn
// surface) has CONTENTS_SLIME, the semantics r_waterSurface reads too
// (vjun1: caulk_water brushes under the green water2_water1_vjun1), and the
// optional env.json "Liquids" rules (brush or top side shader name).
//
// Brush models (func_door water, inline *N models) are not media: they move,
// and their brushes have no transform here. cgame keeps its legacy tint for
// them (it asks the world model contents only, see r_volumetricWaterActive).
// The stock maps have one: the 16 unit thick func_static water sheet of vjun2,
// drained by a script, negligible as a medium.
//
// docs/rend2-volumetric-fog.md, "Underwater medium".

#include "tr_local.h"
#include "json.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

static struct
{
	// static GPU copy of the planes of tr.world
	const world_t	*uploadedWorld;
	GLuint			planeBuffer;
	image_t			planeImage;
	qboolean		planeImageValid;

	// caustic pattern, created on first use
	image_t			causticImage;
	qboolean		causticImageValid;

	int				activeMask;		// last r_volumetricWaterActive value
	int				activeReason[LIQUID_CLASSES];	// why a class bit is (not) set, s_liquidActiveReasons
	int				liquidFrame;	// realFrameNumber of the last froxel view with liquids in its block
	int				blockBrush[MAX_GPU_LIQUIDS];	// world_t::liquids index of each brush of the last block
	int				lastVisible;	// diagnostics: brushes of the last build
	int				lastCandidates;
	float			lastBuildUsec;
} s_liq;

static const char *s_liquidClassNames[LIQUID_CLASSES] = { "water", "slime", "lava" };
static const char *s_liquidSlotSources[] = { "contents", "top side slime", "env.json" };

// env.json "Liquids" rules of the current map
struct liquidRule_t
{
	std::string	pattern;	// lower case, trailing '*' = prefix
	int			profile;	// LIQUID_*
};
static std::vector<liquidRule_t> s_liquidRules;

// liquid brushes of the map the medium leaves out (gameplay still sees them)
struct liquidSkip_t
{
	int		brushNum;
	int		reason;		// world_t::liquidSkipped index
	int		shaderNum;
	int		model;		// BSP model of the brush (0 world, N inline *N)
};
static std::vector<liquidSkip_t> s_liquidSkips;
static const char *s_liquidSkipNames[4] = { "fog contents (BSP fog medium)", "shape", "capacity", "brush model (moving)" };

/*
============================================================

Map load

============================================================
*/

// LIQUID_* of BSP contents, -1 = none. Lava over slime over water, as the
// cgame tints.
static int R_LiquidClassOfContents( int contents )
{
	if ( contents & CONTENTS_LAVA )
		return LIQUID_LAVA;
	if ( contents & CONTENTS_SLIME )
		return LIQUID_SLIME;
	if ( contents & CONTENTS_WATER )
		return LIQUID_WATER;
	return -1;
}

/*
=================
R_LoadLiquidRules

env.json "Liquids" of the map (cubemaps/<map>/env.json, optional):

	"Liquids": [ { "Shader": "textures/common/water_1", "Profile": "slime" },
	             { "Shader": "textures/h_evil/*", "Profile": "water" } ]

A rule matches the BSP shader of a liquid brush (its contents) or the shader
of its upward side (the drawn liquid surface); a trailing '*' is a prefix.
Profile water | slime | lava: the medium (optics) the brush gets, the
r_volumetricWater* / Slime* / Lava* cvars. The gameplay class (the mask, the
cgame tint) stays the contents. The first matching rule wins. r_waterSurface
takes the same profile for a matching surface shader.
=================
*/
static void R_LoadLiquidRules( const char *baseName )
{
	s_liquidRules.clear();

	char filename[MAX_QPATH];
	Com_sprintf(filename, sizeof(filename), "cubemaps/%s/env.json", baseName);
	union { char *c; void *v; } buffer;
	const int filelen = ri.FS_ReadFile(filename, &buffer.v);
	if ( !buffer.c )
		return;
	const char *jsonEnd = buffer.c + filelen;
	const char *array = (JSON_ValueGetType(buffer.c, jsonEnd) == JSONTYPE_OBJECT) ?
		JSON_ObjectGetNamedValue(buffer.c, jsonEnd, "Liquids") : NULL;
	if ( array && JSON_ValueGetType(array, jsonEnd) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: Liquids is not an array\n", filename);
		array = NULL;
	}

	const int count = array ? (int)JSON_ArrayGetIndex(array, jsonEnd, NULL, 0) : 0;
	for ( int i = 0; i < count; i++ )
	{
		const char *entry = JSON_ArrayGetValue(array, jsonEnd, i);
		const char *shaderValue = entry ? JSON_ObjectGetNamedValue(entry, jsonEnd, "Shader") : NULL;
		const char *profileValue = entry ? JSON_ObjectGetNamedValue(entry, jsonEnd, "Profile") : NULL;
		char shader[MAX_QPATH] = "", profile[16] = "";
		if ( shaderValue )
			JSON_ValueGetString(shaderValue, jsonEnd, shader, sizeof(shader));
		if ( profileValue )
			JSON_ValueGetString(profileValue, jsonEnd, profile, sizeof(profile));

		int liquidProfile = -1;
		for ( int c = 0; c < LIQUID_CLASSES; c++ )
		{
			if ( !Q_stricmp(profile, s_liquidClassNames[c]) )
				liquidProfile = c;
		}
		if ( !shader[0] || liquidProfile < 0 )
		{
			ri.Printf(PRINT_WARNING, "%s: Liquids[%d] needs a \"Shader\" and a \"Profile\" (water, slime, lava)\n",
				filename, i);
			continue;
		}
		Q_strlwr(shader);
		s_liquidRules.push_back({ shader, liquidProfile });
	}
	if ( !s_liquidRules.empty() )
		ri.Printf(PRINT_ALL, "%s: %d liquid profile rule%s\n", filename, (int)s_liquidRules.size(),
			(s_liquidRules.size() == 1) ? "" : "s");
	ri.FS_FreeFile(buffer.v);
}

int R_LiquidProfileForShader( const char *name )
{
	// the rules of the last loaded world map (R_LoadLiquidBrushes, before tr.world is set)
	if ( !name || !name[0] || s_liquidRules.empty() )
		return -1;
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	for ( const liquidRule_t& rule : s_liquidRules )
	{
		const size_t n = rule.pattern.size();
		if ( n && rule.pattern[n - 1] == '*' ? !strncmp(lower, rule.pattern.c_str(), n - 1) : rule.pattern == lower )
			return rule.profile;
	}
	return -1;
}

/*
=================
R_LoadLiquidBrushes

The liquid brushes of the world model (dmodel_t 0: inline models move),
called by R_LoadBSP while the lumps are loaded. Their side planes are the
planes of the BSP plane lump (world_t::planes, already swapped). The first six
sides of a brush are its axial bounds (q3map sorts them first, as
R_LoadFogs and the weather zones rely on).
=================
*/
void R_LoadLiquidBrushes( world_t *world, const byte *fileBase, const lump_t *modelsLump,
	const lump_t *brushesLump, const lump_t *sidesLump )
{
	const int start = ri.Milliseconds();
	world->numLiquids = 0;
	world->liquids = NULL;
	world->numLiquidPlanes = 0;
	world->liquidPlanes = NULL;
	world->liquidClassMask = 0;
	Com_Memset(world->liquidSkipped, 0, sizeof(world->liquidSkipped));
	world->liquidLoadMsec = 0.0f;
	R_LoadLiquidRules(world->baseName);

	if ( modelsLump->filelen < (int)sizeof(dmodel_t) ||
		brushesLump->filelen % sizeof(dbrush_t) ||
		sidesLump->filelen % sizeof(dbrushside_t) )
	{
		return;
	}

	const dmodel_t *model = (const dmodel_t *)(fileBase + modelsLump->fileofs);
	const dbrush_t *brushes = (const dbrush_t *)(fileBase + brushesLump->fileofs);
	const dbrushside_t *sides = (const dbrushside_t *)(fileBase + sidesLump->fileofs);
	const int numBrushes = brushesLump->filelen / sizeof(dbrush_t);
	const int numModels = modelsLump->filelen / sizeof(dmodel_t);
	const int numSides = sidesLump->filelen / sizeof(dbrushside_t);
	const int firstBrush = LittleLong(model->firstBrush);
	const int modelBrushes = LittleLong(model->numBrushes);
	if ( firstBrush < 0 || modelBrushes < 0 || firstBrush + modelBrushes > numBrushes )
		return;

	std::vector<liquidBrush_t> liquids;
	std::vector<float> planes;
	s_liquidSkips.clear();
	auto skip = [&]( int reason, int brushNum, int shaderNum )
	{
		world->liquidSkipped[reason]++;
		liquidSkip_t record = { brushNum, reason, shaderNum, -1 };
		for ( int m = 0; m < numModels; m++ )
		{
			const int fb = LittleLong(model[m].firstBrush);
			if ( brushNum >= fb && brushNum < fb + LittleLong(model[m].numBrushes) )
				record.model = m;
		}
		s_liquidSkips.push_back(record);
	};

	for ( int i = 0; i < numBrushes; i++ )
	{
		const int shaderNum = LittleLong(brushes[i].shaderNum);
		if ( shaderNum < 0 || shaderNum >= world->numShaders )
			continue;
		const int contents = world->shaders[shaderNum].contentFlags;
		const int liquidClass = R_LiquidClassOfContents(contents);
		if ( liquidClass < 0 )
			continue;

		// liquids of the moving brush models: not supported (no transform)
		if ( i < firstBrush || i >= firstBrush + modelBrushes )
		{
			skip(3, i, shaderNum);
			continue;
		}
		// a fog volume brush already is a medium of the froxel fog (R_LoadFogs)
		if ( contents & CONTENTS_FOG )
		{
			skip(0, i, shaderNum);
			continue;
		}

		const int firstSide = LittleLong(brushes[i].firstSide);
		const int brushSides = LittleLong(brushes[i].numSides);
		if ( brushSides < 6 || brushSides > MAX_LIQUID_SIDES || firstSide < 0 || firstSide + brushSides > numSides )
		{
			skip(1, i, shaderNum);
			continue;
		}

		// axial bounds: sides 0..5 are -x, +x, -y, +y, -z, +z
		liquidBrush_t brush = {};
		qboolean axial = qtrue;
		for ( int k = 0; k < 6 && axial; k++ )
		{
			const int planeNum = LittleLong(sides[firstSide + k].planeNum);
			if ( planeNum < 0 || planeNum >= world->numplanes )
			{
				axial = qfalse;
				break;
			}
			const cplane_t *plane = &world->planes[planeNum];
			const int axis = k >> 1;
			const float sign = (k & 1) ? 1.0f : -1.0f;
			if ( plane->normal[axis] * sign < 0.999f )
				axial = qfalse;
			else
				brush.bounds[k & 1][axis] = sign * plane->dist;
		}
		if ( !axial || brush.bounds[0][0] >= brush.bounds[1][0] ||
			brush.bounds[0][1] >= brush.bounds[1][1] || brush.bounds[0][2] >= brush.bounds[1][2] )
		{
			skip(1, i, shaderNum);
			continue;
		}

		if ( (int)liquids.size() >= MAX_LIQUID_BRUSHES ||
			(int)(planes.size() / 4) + brushSides > MAX_LIQUID_PLANES )
		{
			skip(2, i, shaderNum);
			continue;
		}

		brush.firstPlane = (int)(planes.size() / 4);
		brush.liquidClass = liquidClass;
		brush.mediumSlot = liquidClass;
		brush.slotSource = LIQUIDSLOT_CONTENTS;
		brush.brushNum = i;
		brush.shaderNum = shaderNum;
		brush.topShaderNum = -1;
		for ( int k = 0; k < brushSides; k++ )
		{
			const int planeNum = LittleLong(sides[firstSide + k].planeNum);
			if ( planeNum < 0 || planeNum >= world->numplanes )
				continue;
			const cplane_t *plane = &world->planes[planeNum];
			// the upward side: the drawn surface of the liquid (a liquid shader
			// before caulk / nodraw when there are several)
			const int sideShader = LittleLong(sides[firstSide + k].shaderNum);
			if ( plane->normal[2] > 0.7f && sideShader >= 0 && sideShader < world->numShaders &&
				(brush.topShaderNum < 0 ||
				(R_LiquidClassOfContents(world->shaders[sideShader].contentFlags) >= 0 &&
				!(world->shaders[sideShader].surfaceFlags & SURF_NODRAW) &&
				(world->shaders[brush.topShaderNum].surfaceFlags & SURF_NODRAW))) )
			{
				brush.topShaderNum = sideShader;
			}
			planes.push_back(plane->normal[0]);
			planes.push_back(plane->normal[1]);
			planes.push_back(plane->normal[2]);
			planes.push_back(plane->dist);
			brush.numPlanes++;
		}

		// the medium: env.json rule (brush shader, then top side shader), else a
		// water brush under a slime-flagged surface is slime, else the class
		const int topContents = (brush.topShaderNum >= 0) ? world->shaders[brush.topShaderNum].contentFlags : 0;
		int profile = R_LiquidProfileForShader(world->shaders[shaderNum].shader);
		if ( profile < 0 && brush.topShaderNum >= 0 )
			profile = R_LiquidProfileForShader(world->shaders[brush.topShaderNum].shader);
		if ( profile >= 0 )
		{
			brush.mediumSlot = profile;
			brush.slotSource = LIQUIDSLOT_ENV_JSON;
		}
		else if ( liquidClass == LIQUID_WATER && (topContents & CONTENTS_SLIME) )
		{
			brush.mediumSlot = LIQUID_SLIME;
			brush.slotSource = LIQUIDSLOT_TOP_SLIME;
		}
		liquids.push_back(brush);
		world->liquidClassMask |= 1 << liquidClass;
	}

	if ( !liquids.empty() )
	{
		world->numLiquids = (int)liquids.size();
		world->liquids = (liquidBrush_t *)Hunk_Alloc(world->numLiquids * sizeof(liquidBrush_t), h_low);
		Com_Memcpy(world->liquids, liquids.data(), world->numLiquids * sizeof(liquidBrush_t));
		world->numLiquidPlanes = (int)(planes.size() / 4);
		world->liquidPlanes = (vec4_t *)Hunk_Alloc(world->numLiquidPlanes * sizeof(vec4_t), h_low);
		Com_Memcpy(world->liquidPlanes, planes.data(), planes.size() * sizeof(float));
	}

	world->liquidLoadMsec = (float)(ri.Milliseconds() - start);
	if ( world->numLiquids || world->liquidSkipped[0] || world->liquidSkipped[1] ||
		world->liquidSkipped[2] || world->liquidSkipped[3] )
	{
		ri.Printf(PRINT_DEVELOPER, "Liquid brushes: %d (%d planes), skipped: %d fog, %d shape, %d capacity, %d brush model\n",
			world->numLiquids, world->numLiquidPlanes, world->liquidSkipped[0], world->liquidSkipped[1],
			world->liquidSkipped[2], world->liquidSkipped[3]);
	}
}

/*
============================================================

Availability

============================================================
*/

// What the compiled GLSL programs were built with (-1: not decided yet). SP
// keeps its programs over map loads (R_Init with cached GPU shaders) while the
// latched cvars take their new values there: the liquid code must follow the
// programs, not the cvar, or the renderer would drop cgame's tint for a medium
// the shaders do not have (or feed liquid shaders no Liquids block).
static int s_liquidsProgramState = -1;
static int s_liquidSurfacesProgramState = -1;

static qboolean R_LiquidsAvailableNow( void )
{
	static int checkedUnits = -1;
	if ( !r_volumetricWater || !r_volumetricWater->integer || !R_VolumetricFroxelEnabled() )
		return qfalse;
	if ( checkedUnits < 0 )
	{
		GLint maxFragmentSamplers = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentSamplers);
		checkedUnits = maxFragmentSamplers;
		// the compute injection (r_gl43) samples the same units; GL 4.3 only
		// guarantees 16 there (0: no compute, the query is not supported)
		GLint maxComputeSamplers = 0;
		qglGetIntegerv(0x91BC /* GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS */, &maxComputeSamplers);
		while ( qglGetError() != GL_NO_ERROR )
			;
		if ( maxComputeSamplers > 0 && maxComputeSamplers < checkedUnits )
			checkedUnits = maxComputeSamplers;
		if ( checkedUnits <= TB_LIQUIDCAUSTICS )
		{
			ri.Printf(PRINT_WARNING, "r_volumetricWater: %d texture units (fragment %d, compute %d), %d needed, disabled\n",
				checkedUnits, maxFragmentSamplers, maxComputeSamplers, TB_LIQUIDCAUSTICS + 1);
		}
	}
	return (qboolean)(checkedUnits > TB_LIQUIDCAUSTICS);
}

/*
=================
R_LiquidsLatchPrograms / R_LiquidsUnlatchPrograms

Called when the GLSL programs are compiled (GLSL_LoadGPUShaders) and deleted
(GLSL_ShutdownGPUShaders): the liquid state is decided once per program set.
=================
*/
void R_LiquidsLatchPrograms( void )
{
	s_liquidsProgramState = -1;
	s_liquidSurfacesProgramState = -1;
	const qboolean liquids = R_LiquidsAvailableNow();
	s_liquidsProgramState = liquids ? 1 : 0;
	s_liquidSurfacesProgramState =
		(liquids && r_volumetricWaterSurfaces->integer && r_sunlightMode->integer) ? 1 : 0;
}

void R_LiquidsUnlatchPrograms( void )
{
	s_liquidsProgramState = -1;
	s_liquidSurfacesProgramState = -1;
}

qboolean R_LiquidsAvailable( void )
{
	if ( s_liquidsProgramState < 0 )
		return R_LiquidsAvailableNow();
	return (qboolean)(s_liquidsProgramState && R_VolumetricFroxelEnabled());
}

qboolean R_LiquidSurfacesEnabled( void )
{
	if ( s_liquidSurfacesProgramState < 0 )
		return (qboolean)(R_LiquidsAvailableNow() && r_volumetricWaterSurfaces->integer && r_sunlightMode->integer);
	return (qboolean)(s_liquidSurfacesProgramState && R_LiquidsAvailable());
}

/*
=================
R_LiquidClassMask

The liquid classes drawn as media on this map. With programs built without
liquids the mask is 0 whatever r_volumetricWater says (vid_restart needed);
with liquid programs, r_volumetricWater 0 on a later map gives 0 too (the
programs then see an empty Liquids block).
=================
*/
int R_LiquidClassMask( void )
{
	if ( !R_LiquidsAvailable() || !tr.world )
		return 0;
	return r_volumetricWater->integer & tr.world->liquidClassMask & ((1 << LIQUID_CLASSES) - 1);
}

// the media (optics slots) of the brushes R_LiquidClassMask draws
int R_LiquidMediumSlotMask( void )
{
	const int mask = R_LiquidClassMask();
	int slots = 0;
	for ( int i = 0; mask && i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		if ( mask & (1 << brush->liquidClass) )
			slots |= 1 << brush->mediumSlot;
	}
	return slots;
}

int R_LiquidPointClass( const vec3_t p )
{
	return R_LiquidClassOfContents(ri.CM_PointContents(p, 0));
}

/*
=================
R_LiquidPointBrush

The first kept liquid brush of a class drawn this map (R_LiquidClassMask)
that holds p, -1 none: the planes the GPU clips with. A small tolerance
(half a unit) puts the surface itself inside, so the collision test and this
one never leave a camera on the surface without a medium.
=================
*/
int R_LiquidPointBrush( const vec3_t p, int liquidClass )
{
	const int mask = R_LiquidClassMask();
	if ( liquidClass < 0 || !(mask & (1 << liquidClass)) )
		return -1;
	for ( int i = 0; i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		if ( brush->liquidClass != liquidClass )
			continue;
		qboolean inside = qtrue;
		for ( int k = 0; k < brush->numPlanes && inside; k++ )
		{
			const float *plane = tr.world->liquidPlanes[brush->firstPlane + k];
			if ( DotProduct(plane, p) - plane[3] > 0.5f )
				inside = qfalse;
		}
		if ( inside )
			return i;
	}
	return -1;
}

/*
============================================================

GPU resources

============================================================
*/

static void R_LiquidsForgetUnit( GLuint texnum )
{
	for ( int u = 0; u < MAX_TEXTURE_UNITS; u++ )
	{
		if ( glState.currenttextures[u] == (int)texnum )
			glState.currenttextures[u] = 0;
	}
}

void R_LiquidsShutdown( void )
{
	if ( s_liq.planeImageValid )
	{
		R_LiquidsForgetUnit(s_liq.planeImage.texnum);
		qglDeleteTextures(1, &s_liq.planeImage.texnum);
		qglDeleteBuffers(1, &s_liq.planeBuffer);
	}
	if ( s_liq.causticImageValid )
	{
		R_LiquidsForgetUnit(s_liq.causticImage.texnum);
		qglDeleteTextures(1, &s_liq.causticImage.texnum);
	}
	Com_Memset(&s_liq, 0, sizeof(s_liq));
	if ( r_volumetricWaterActive )
		ri.Cvar_Set("r_volumetricWaterActive", "0");
}

// planes of tr.world into the static buffer texture (at least one texel)
static void R_LiquidsUploadPlanes( void )
{
	if ( s_liq.planeImageValid && s_liq.uploadedWorld == tr.world )
		return;

	if ( !s_liq.planeImageValid )
	{
		qglGenBuffers(1, &s_liq.planeBuffer);
		Com_Memset(&s_liq.planeImage, 0, sizeof(s_liq.planeImage));
		Q_strncpyz(s_liq.planeImage.imgName, "*liquidPlanes", sizeof(s_liq.planeImage.imgName));
		s_liq.planeImage.flags = IMGFLAG_TEXBUFFER;
		qglGenTextures(1, &s_liq.planeImage.texnum);
		s_liq.planeImageValid = qtrue;
	}

	static const vec4_t none = { 0.0f, 0.0f, 1.0f, 0.0f };
	const int count = (tr.world && tr.world->numLiquidPlanes) ? tr.world->numLiquidPlanes : 1;
	const void *data = (tr.world && tr.world->numLiquidPlanes) ? (const void *)tr.world->liquidPlanes : (const void *)none;
	qglBindBuffer(GL_TEXTURE_BUFFER, s_liq.planeBuffer);
	qglBufferData(GL_TEXTURE_BUFFER, count * sizeof(vec4_t), data, GL_STATIC_DRAW);
	GL_BindToTMU(&s_liq.planeImage, TB_LIQUIDPLANES);
	qglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, s_liq.planeBuffer);
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);
	s_liq.uploadedWorld = tr.world;
}

/*
=================
R_LiquidsCausticPattern

A tiling sun caustic pattern of size x size texels, mean 1: light from above
refracted by a tiling wave height field h (integer wave vectors) lands
displaced by -k grad h on the floor; photons splatted bilinearly (4 per
texel) give the bright focus lines and the dark gaps of real caustics. One
small blur, then normalized to mean 1, so 1 + s (pattern - 1) keeps the
average sunlight for any strength s. Deterministic (fixed waves).
=================
*/
void R_LiquidsCausticPattern( int size, std::vector<float>& out );
void R_LiquidsCausticPattern( int size, std::vector<float>& out )
{
	struct wave_t { int kx, ky; float amplitude, phase; };
	static const wave_t waves[] = {
		{ 3, 1, 1.00f, 0.3f }, { -1, 4, 0.85f, 1.7f }, { 2, -5, 0.60f, 4.1f }, { 5, 3, 0.50f, 2.6f },
		{ -4, -2, 0.70f, 5.3f }, { 1, 6, 0.40f, 0.9f }, { 7, -2, 0.30f, 3.4f }, { -6, 5, 0.25f, 1.2f },
		{ 4, 7, 0.22f, 5.9f }, { -8, -3, 0.18f, 2.2f }, { 9, 4, 0.14f, 0.5f }, { -3, -9, 0.14f, 4.7f },
	};
	const int numWaves = (int)ARRAY_LEN(waves);
	const float twoPi = 2.0f * (float)M_PI;

	// gradient of h at texel centers (units: height per texel)
	std::vector<float> gx(size * size), gy(size * size);
	float maxGradient = 0.0f;
	for ( int y = 0; y < size; y++ )
	{
		for ( int x = 0; x < size; x++ )
		{
			float dx = 0.0f, dy = 0.0f;
			for ( int w = 0; w < numWaves; w++ )
			{
				const float arg = twoPi * (waves[w].kx * (x + 0.5f) + waves[w].ky * (y + 0.5f)) / size + waves[w].phase;
				// amplitude / |k| keeps the slopes of the short waves small
				const float k = sqrtf((float)(waves[w].kx * waves[w].kx + waves[w].ky * waves[w].ky));
				const float c = cosf(arg) * waves[w].amplitude / k;
				dx += c * twoPi * waves[w].kx / size;
				dy += c * twoPi * waves[w].ky / size;
			}
			gx[y * size + x] = dx;
			gy[y * size + x] = dy;
			maxGradient = MAX(maxGradient, sqrtf(dx * dx + dy * dy));
		}
	}

	// the largest displacement: a fraction of the main wave length, where the
	// focus lines (caustics) have formed but have not crossed into noise
	// (0.12: standard deviation ~0.65 of the mean, tools in the harness)
	const float displacement = (size / 3.0f) * 0.12f / MAX(maxGradient, 1e-6f);
	std::vector<float> energy(size * size, 0.0f);
	for ( int y = 0; y < size * 2; y++ )
	{
		for ( int x = 0; x < size * 2; x++ )
		{
			const int sx = x >> 1, sy = y >> 1;
			const float px = (x + 0.5f) * 0.5f - gx[sy * size + sx] * displacement - 0.5f;
			const float py = (y + 0.5f) * 0.5f - gy[sy * size + sx] * displacement - 0.5f;
			const float fx = floorf(px), fy = floorf(py);
			const float ax = px - fx, ay = py - fy;
			const int ix = ((int)fx % size + size) % size;
			const int iy = ((int)fy % size + size) % size;
			const int ix1 = (ix + 1) % size, iy1 = (iy + 1) % size;
			energy[iy * size + ix] += (1.0f - ax) * (1.0f - ay);
			energy[iy * size + ix1] += ax * (1.0f - ay);
			energy[iy1 * size + ix] += (1.0f - ax) * ay;
			energy[iy1 * size + ix1] += ax * ay;
		}
	}

	// 1 2 1 blur (wrapping), mean 1, the rare focus peaks clamped to 6 (they
	// would only alias), mean 1 again
	out.assign(size * size, 0.0f);
	double sum = 0.0;
	for ( int y = 0; y < size; y++ )
	{
		for ( int x = 0; x < size; x++ )
		{
			float v = 0.0f;
			for ( int j = -1; j <= 1; j++ )
			{
				for ( int i = -1; i <= 1; i++ )
				{
					const float w = (float)((2 - abs(i)) * (2 - abs(j)));
					v += w * energy[((y + j + size) % size) * size + (x + i + size) % size];
				}
			}
			out[y * size + x] = v;
			sum += v;
		}
	}
	for ( int pass = 0; pass < 2; pass++ )
	{
		const float scale = (float)((double)size * size / MAX(sum, 1e-9));
		sum = 0.0;
		for ( float& v : out )
		{
			v *= scale;
			if ( pass == 0 )
				v = MIN(v, 6.0f);
			sum += v;
		}
	}
}

static void R_LiquidsCreateCausticImage( void )
{
	if ( s_liq.causticImageValid )
		return;

	const int size = 256;
	std::vector<float> pattern;
	R_LiquidsCausticPattern(size, pattern);

	Com_Memset(&s_liq.causticImage, 0, sizeof(s_liq.causticImage));
	Q_strncpyz(s_liq.causticImage.imgName, "*liquidCaustics", sizeof(s_liq.causticImage.imgName));
	s_liq.causticImage.width = size;
	s_liq.causticImage.height = size;
	qglGenTextures(1, &s_liq.causticImage.texnum);
	// This upload always initializes the procedural fallback.  The surface-driven
	// interaction atlas is selected only when a liquid consumer is bound.
	GL_BindToTMU(&s_liq.causticImage, TB_LIQUIDCAUSTICS);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, size, size, 0, GL_RED, GL_FLOAT, pattern.data());
	// the box mips keep the mean: far away and in big froxels the pattern fades to 1
	qglGenerateMipmap(GL_TEXTURE_2D);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	s_liq.causticImageValid = qtrue;
}

void R_LiquidsBindTextures( void )
{
	if ( !R_LiquidsAvailable() )
		return;
	R_LiquidsUploadPlanes();
	R_LiquidsCreateCausticImage();
	GL_BindToTMU(&s_liq.planeImage, TB_LIQUIDPLANES);
	image_t *surfaceAtlas = r_waterCausticsMode->integer == 2 ? R_WaterCausticsInteractionAtlas() : nullptr;
	GL_BindToTMU(surfaceAtlas ? surfaceAtlas : &s_liq.causticImage, TB_LIQUIDCAUSTICS);
}

/*
=================
R_LiquidsUpdateActive

r_volumetricWaterActive tells cgame which liquid classes the renderer draws as
a medium (it then skips their legacy full screen tint): the classes of this
map in r_volumetricWater, while the froxel volume can be built at all (the
runtime switches and scene conditions of its world view test), less the class
of a camera standing in a liquid the medium does not have (a brush skipped
for shape / capacity, a water + fog brush: BSP fog plus the legacy tint, as
before). cgame tests the same world model contents at the same origin.

Outside any liquid the value does not depend on the camera or the frustum,
so entering water from the air never changes it (cgame sees a change one
frame later). Set only when it changes.
=================
*/
static const char *s_liquidActiveReasons[] = {
	"drawn", "class off (r_volumetricWater / map)", "no froxel volume for this scene", "camera in a liquid the medium lacks"
};

void R_LiquidsUpdateActive( const trRefdef_t *refdef, const viewParms_t *view )
{
	// scenes without a world view (menus, portraits) say nothing about the camera
	if ( refdef && (refdef->rdflags & RDF_NOWORLDMODEL) )
		return;
	// the scene conditions under which the froxel volume is never built
	// (RB_UpdateVolumetricConstants): no medium, cgame keeps its tint
	const qboolean noVolume = (qboolean)(
		(refdef && (refdef->doLAGoggles || (refdef->rdflags & RDF_HYPERSPACE))) ||
		!r_drawfog->integer || !r_depthPrepass->integer || tr.renderFbo == NULL ||
		(refdef && view == NULL));
	int mask = noVolume ? 0 : R_LiquidClassMask();
	for ( int c = 0; c < LIQUID_CLASSES; c++ )
		s_liq.activeReason[c] = (mask & (1 << c)) ? 0 : (R_LiquidClassMask() & (1 << c)) ? 2 : 1;

	// the camera in a liquid the medium does not have: that class keeps the tint
	if ( mask && refdef )
	{
		const float *origin = view ? view->ori.origin : refdef->vieworg;
		const int cameraClass = R_LiquidPointClass(origin);
		if ( cameraClass >= 0 && (mask & (1 << cameraClass)) && R_LiquidPointBrush(origin, cameraClass) < 0 )
		{
			mask &= ~(1 << cameraClass);
			s_liq.activeReason[cameraClass] = 3;
		}
	}

	if ( mask != s_liq.activeMask || (r_volumetricWaterActive && r_volumetricWaterActive->integer != mask) )
	{
		ri.Cvar_Set("r_volumetricWaterActive", va("%i", mask));
		s_liq.activeMask = mask;
	}
}

/*
=================
R_LiquidsWorldLoaded

After a map load: the planes go to the GPU, the caustic pattern is made once,
and r_volumetricWaterActive follows the new map.
=================
*/
void R_LiquidsWorldLoaded( void )
{
	if ( s_liquidsProgramState == 0 && R_LiquidsAvailableNow() )
		ri.Printf(PRINT_ALL, "r_volumetricWater: the shaders were built without liquids, vid_restart to use them\n");
	if ( R_LiquidsAvailable() )
	{
		R_LiquidsUploadPlanes();
		R_LiquidsCreateCausticImage();
	}
	R_LiquidsUpdateActive(NULL, NULL);
}

/*
============================================================

Per frame

============================================================
*/

static void R_LiquidsParseVec3( const char *s, const vec3_t fallback, vec3_t out )
{
	VectorCopy(fallback, out);
	if ( s && sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]) != 3 )
		VectorCopy(fallback, out);
}

/*
=================
R_LiquidsMaterial

The medium of a liquid class: extinction per unit (scalar, the mean of the
channels), relative extinction color (mean 1, R_VolumetricExtinctionColor),
single scattering albedo and Henyey-Greenstein g. The in-scattering color is
albedo * extinction color in both modes (sigma_s = sigma_t.rgb * albedo).
=================
*/
void R_LiquidsMaterial( int liquidClass, vec4_t extinction, vec4_t albedo )
{
	cvar_t *const cvars[LIQUID_CLASSES][4] = {
		{ r_volumetricWaterExtinction, r_volumetricWaterColor, r_volumetricWaterAlbedo, r_volumetricWaterAnisotropy },
		{ r_volumetricSlimeExtinction, r_volumetricSlimeColor, r_volumetricSlimeAlbedo, r_volumetricSlimeAnisotropy },
		{ r_volumetricLavaExtinction, r_volumetricLavaColor, r_volumetricLavaAlbedo, r_volumetricLavaAnisotropy },
	};
	cvar_t *const *c = cvars[Com_Clampi(0, LIQUID_CLASSES - 1, liquidClass)];

	vec3_t color, relative, rgb;
	const vec3_t one = { 1.0f, 1.0f, 1.0f };
	R_LiquidsParseVec3(c[1]->string, one, color);
	R_VolumetricExtinctionColor(color, relative);
	VectorSet4(extinction, relative[0], relative[1], relative[2], Com_Clamp(0.0f, 1.0f, c[0]->value));
	if (liquidClass != LIQUID_LAVA && R_WaterSurfaceResourcesEnabled() && r_waterSurface->integer)
		extinction[3] *= Com_Clamp(0.0f, 16.0f, r_waterSurfaceAbsorption->value);

	const vec3_t grey = { 0.5f, 0.5f, 0.5f };
	R_LiquidsParseVec3(c[2]->string, grey, rgb);
	for ( int i = 0; i < 3; i++ )
		rgb[i] = Com_Clamp(0.0f, 1.0f, rgb[i]);
	VectorSet4(albedo, rgb[0], rgb[1], rgb[2], Com_Clamp(-0.9f, 0.9f, c[3]->value));
}

unsigned int R_LiquidsMediumKey( unsigned int key )
{
	cvar_t *const cvars[] = {
		r_volumetricWaterExtinction, r_volumetricWaterColor, r_volumetricWaterAlbedo, r_volumetricWaterAnisotropy,
		r_volumetricSlimeExtinction, r_volumetricSlimeColor, r_volumetricSlimeAlbedo, r_volumetricSlimeAnisotropy,
		r_volumetricLavaExtinction, r_volumetricLavaColor, r_volumetricLavaAlbedo, r_volumetricLavaAnisotropy,
		r_volumetricWaterSunPath, r_volumetricWaterCaustics, r_volumetricWaterCausticScale, r_volumetricWaterCausticFocus,
		r_waterCausticsMode, r_waterCausticsQuality, r_waterCausticsMaxDepth, r_waterCausticsSlope,
		r_waterCausticsFilter, r_waterCausticsDebug,
		r_waterSurface, r_waterSurfaceAbsorption,
	};
	for ( cvar_t *cv : cvars )
	{
		for ( const char *s = cv->string; *s; s++ )
			key = (key ^ (unsigned char)*s) * 16777619u;
		key = (key ^ 0xffu) * 16777619u;
	}
	const int mask = R_LiquidClassMask();
	key = (key ^ (unsigned int)mask) * 16777619u;
	key = (key ^ (unsigned int)(tr.world ? tr.world->numLiquids : 0)) * 16777619u;
	// the media of the brushes (env.json rules are per map, but keep the key exact)
	for ( int i = 0; tr.world && i < tr.world->numLiquids; i++ )
		key = (key ^ (unsigned int)tr.world->liquids[i].mediumSlot) * 16777619u;
	return key;
}

// a liquid brush the froxel volume may see: bounds against the four frustum
// sides, then the view depth range (minDepth, maxDepth)
static qboolean R_LiquidVisible( const liquidBrush_t *brush, const viewParms_t *view, const vec3_t forward,
	float farZ, float *minDepth, float *maxDepth )
{
	for ( int p = 0; p < 4; p++ )
	{
		const cplane_t *plane = &view->frustum[p];
		vec3_t corner;
		for ( int c = 0; c < 3; c++ )
			corner[c] = brush->bounds[plane->normal[c] >= 0.0f ? 1 : 0][c];
		if ( DotProduct(corner, plane->normal) - plane->dist < 0.0f )
			return qfalse;
	}

	float lo = 1e30f, hi = -1e30f;
	for ( int c = 0; c < 8; c++ )
	{
		const vec3_t corner = {
			brush->bounds[c & 1][0], brush->bounds[(c >> 1) & 1][1], brush->bounds[(c >> 2) & 1][2] };
		vec3_t delta;
		VectorSubtract(corner, view->ori.origin, delta);
		const float depth = DotProduct(delta, forward);
		lo = MIN(lo, depth);
		hi = MAX(hi, depth);
	}
	*minDepth = lo;
	*maxDepth = hi;
	return (qboolean)(hi >= 0.0f && lo <= farZ);
}

qboolean R_LiquidsInFrustum( const viewParms_t *view, const vec3_t forward, float farZ )
{
	const int mask = R_LiquidClassMask();
	if ( !mask || !view )
		return qfalse;
	for ( int i = 0; i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		float lo, hi;
		if ( (mask & (1 << brush->liquidClass)) && R_LiquidVisible(brush, view, forward, farZ, &lo, &hi) )
			return qtrue;
	}
	return qfalse;
}

/*
=================
R_LiquidsBuild

The Liquids block of a froxel view: the visible liquid brushes, those around
the camera first, then the nearest (distance from the camera to the bounds),
at most MAX_GPU_LIQUIDS; per slice the brushes whose view depth range may
touch it, one slice wider on both sides (jittered samples). Liquids end with
the volume: they fade out over the last fifth before far (no tail term).
Returns the number of brushes in the block.
=================
*/
int R_LiquidsBuild( LiquidsBlock *block, const viewParms_t *view, const vec3_t forward, float farZ,
	int numSlices, int (*depthSlice)(float depth), int cameraClass, float time )
{
	Com_Memset(block, 0, sizeof(*block));
	const auto buildStart = std::chrono::steady_clock::now();
	const int mask = R_LiquidClassMask();
	block->params[1] = (float)cameraClass;
	block->params[3] = (float)mask;
	s_liq.lastVisible = 0;
	s_liq.lastCandidates = 0;
	if ( !mask || !view )
		return 0;

	struct candidate_t { int index; float distance; float minDepth, maxDepth; };
	candidate_t candidates[MAX_LIQUID_BRUSHES];
	int numCandidates = 0;
	for ( int i = 0; i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		if ( !(mask & (1 << brush->liquidClass)) )
			continue;
		float lo, hi;
		if ( !R_LiquidVisible(brush, view, forward, farZ, &lo, &hi) )
			continue;
		float d2 = 0.0f;
		for ( int c = 0; c < 3; c++ )
		{
			const float o = view->ori.origin[c];
			const float d = (o < brush->bounds[0][c]) ? brush->bounds[0][c] - o :
				(o > brush->bounds[1][c]) ? o - brush->bounds[1][c] : 0.0f;
			d2 += d * d;
		}
		candidates[numCandidates++] = { i, d2, lo, hi };
	}
	s_liq.lastCandidates = numCandidates;
	std::sort(candidates, candidates + numCandidates,
		[]( const candidate_t& a, const candidate_t& b ) { return a.distance < b.distance; });

	const int count = MIN(numCandidates, MAX_GPU_LIQUIDS);
	for ( int n = 0; n < count; n++ )
	{
		const candidate_t& candidate = candidates[n];
		const liquidBrush_t *brush = &tr.world->liquids[candidate.index];
		s_liq.blockBrush[n] = candidate.index;
		VectorSet4(block->mins[n], brush->bounds[0][0], brush->bounds[0][1], brush->bounds[0][2],
			(float)brush->firstPlane);
		VectorSet4(block->maxs[n], brush->bounds[1][0], brush->bounds[1][1], brush->bounds[1][2],
			(float)(brush->numPlanes + 64 * brush->mediumSlot + 256 * brush->liquidClass));
		R_WaterCausticsBrushParams(brush->brushNum, time, block->waveParams[n], block->waveTerms[n]);

		const int z0 = Q_max(0, depthSlice(MAX(candidate.minDepth, 0.0f)) - 1);
		const int z1 = Q_min(numSlices - 1, depthSlice(MIN(candidate.maxDepth, farZ)) + 1);
		for ( int k = z0; k <= z1; k++ )
			block->slices[k] |= 1 << n;
	}
	s_liq.lastVisible = count;
	if ( count )
		s_liq.liquidFrame = backEndData->realFrameNumber;

	block->params[0] = (float)count;
	block->params[2] = r_volumetricWaterSunPath->integer ? 1.0f : 0.0f;

	// the media in use: the profile of every slot a drawn brush has (a water
	// brush may take the slime profile), the others zero
	const int slots = R_LiquidMediumSlotMask();
	for ( int c = 0; c < LIQUID_CLASSES; c++ )
	{
		R_LiquidsMaterial(c, block->material[c * 2], block->material[c * 2 + 1]);
		if ( !(slots & (1 << c)) )
			block->material[c * 2][3] = 0.0f;
	}

	// caustics of the sun under water: period, animation phase (wrapped, so the
	// float keeps its precision), focus depth, strength
	const float period = MAX(r_volumetricWaterCausticScale->value, 8.0f);
	const float phase = fmodf(time * r_volumetricWaterCausticSpeed->value, 1024.0f);
	VectorSet4(block->caustics, 1.0f / period, phase, MAX(r_volumetricWaterCausticFocus->value, 1.0f),
		Com_Clamp(0.0f, 1.0f, r_volumetricWaterCaustics->value));
	const float spacing = r_waterCausticsQuality->integer <= 0 ? 32.0f :
		(r_waterCausticsQuality->integer == 1 ? 16.0f : 8.0f);
	VectorSet4(block->causticSurface, (float)r_waterCausticsMode->integer, spacing,
		r_waterCausticsMaxDepth->value, r_waterCausticsSlope->value);
	VectorSet4(block->causticDebug, r_waterCausticsFilter->value, (float)r_waterCausticsDebug->integer,
		r_waterSurfaceIOR->value, r_waterSurface->integer ? 1.0f : 0.0f);

	// fade before far; the world size of a froxel per unit of view depth (caustic
	// lod) is set by the caller, which knows the froxel grid
	const float fadeStart = farZ * 0.8f;
	VectorSet4(block->view, fadeStart, 1.0f / MAX(farZ - fadeStart, 1.0f), 0.0f, 0.0f);
	s_liq.lastBuildUsec = std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - buildStart).count();
	return count;
}

/*
============================================================

Draws

============================================================
*/

void RB_LiquidSurfaceSetupDraw( const shaderStage_t *pStage, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers )
{
	(void)pStage;
	vec4_t params = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( s_liq.planeImageValid && s_liq.causticImageValid && backEnd.volumetricView && !backEnd.depthFill &&
		!(backEnd.viewParms.flags & VPF_DEPTHSHADOW) )
	{
		params[0] = 1.0f;
	}
	uniforms.SetUniformVec4(UNIFORM_LIQUIDSURFACE, params);
	if ( s_liq.planeImageValid )
		samplers.AddStaticImage(&s_liq.planeImage, TB_LIQUIDPLANES);
	if ( s_liq.causticImageValid )
	{
		image_t *surfaceAtlas = r_waterCausticsMode->integer == 2 ? R_WaterCausticsInteractionAtlas() : nullptr;
		samplers.AddStaticImage(surfaceAtlas ? surfaceAtlas : &s_liq.causticImage, TB_LIQUIDCAUSTICS);
	}
}

/*
=================
RB_LiquidFogBlendMask

The froxel fog of a generic stage whose blend the legacy fog leaves out
(ACFF_NONE: GL_SRC_ALPHA GL_ONE, filters, ...): without it such a sprite is
drawn unfogged under water, where cgame no longer tints the view. Only in a
froxel view with liquids (the programs have USE_LIQUID_FOG_BLENDS then), so
the legacy and the plain froxel fog are unchanged. u_FogColorMask:
  (1 1 1 0)    the color scales with T (anything adding to the frame)
  (0 0 0 -n)   filter: color -> mix(color, n, 1 - T), alpha *= T (n = the
               neutral color of the blend: 1 modulate, 0.5 2x modulate)
=================
*/
qboolean RB_LiquidFogBlendMask( const shaderStage_t *stage, const shader_t *shader, vec4_t mask )
{
	if ( stage->adjustColorsForFog != ACFF_NONE || shader->isSky || backEnd.depthFill ||
		!R_LiquidClassMask() || RB_VolumetricFogMode(shader->sort) != 1 )
		return qfalse;
	const uint32_t src = stage->stateBits & GLS_SRCBLEND_BITS;
	const uint32_t dst = stage->stateBits & GLS_DSTBLEND_BITS;
	if ( !src && !dst )
		return qfalse;

	if ( dst == GLS_DSTBLEND_ONE )
		VectorSet4(mask, 1.0f, 1.0f, 1.0f, 0.0f);
	else if ( (src == GLS_SRCBLEND_DST_COLOR && dst == GLS_DSTBLEND_ZERO) ||
		(src == GLS_SRCBLEND_ZERO && dst == GLS_DSTBLEND_SRC_COLOR) ||
		(src == GLS_SRCBLEND_ZERO && dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA) )
		VectorSet4(mask, 0.0f, 0.0f, 0.0f, -1.0f);
	else if ( src == GLS_SRCBLEND_DST_COLOR && dst == GLS_DSTBLEND_SRC_COLOR )
		VectorSet4(mask, 0.0f, 0.0f, 0.0f, -0.5f);
	else
		VectorSet4(mask, 1.0f, 1.0f, 1.0f, 0.0f);
	return qtrue;
}

/*
=================
Liquid fog bypass (r_volumetricFogDebug 69, r_liquids bypass)

The draws of a froxel view with liquids that get no froxel fog: the water
between them and the camera is missing on them. Counted per frame with the
first shaders of each reason.
=================
*/
enum { LIQUIDBYPASS_NONE, LIQUIDBYPASS_LIGHTALL, LIQUIDBYPASS_BLEND, LIQUIDBYPASS_IMMEDIATE,
	LIQUIDBYPASS_REFRACTION, LIQUIDBYPASS_COUNT };
static const char *s_liquidBypassNames[LIQUIDBYPASS_COUNT] = {
	"none", "lightall blended (no fog pass)", "generic blend without fog", "immediate draw (beam)",
	"refraction / distortion pass" };
static const float s_liquidBypassColors[LIQUIDBYPASS_COUNT][3] = {
	{ 0, 0, 0 }, { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.9f, 0.0f }, { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.4f, 0.0f } };

#define MAX_LIQUID_BYPASS_SHADERS 16
static struct
{
	int				frame;
	int				counts[LIQUIDBYPASS_COUNT];
	int				numShaders;
	const shader_t	*shaders[MAX_LIQUID_BYPASS_SHADERS];
	int				shaderReason[MAX_LIQUID_BYPASS_SHADERS];
	int				shaderCount[MAX_LIQUID_BYPASS_SHADERS];
	int				weatherFrame;		// realFrameNumber of the last weather draw with the liquid cull
	int				weatherDraws;
} s_liqBypass;

static qboolean RB_LiquidFrameHasLiquids( void )
{
	return (qboolean)(backEndData && s_liq.liquidFrame == backEndData->realFrameNumber);
}

static void RB_LiquidBypassCount( int reason, const shader_t *shader )
{
	if ( s_liqBypass.frame != backEndData->realFrameNumber )
	{
		const int weatherFrame = s_liqBypass.weatherFrame, weatherDraws = s_liqBypass.weatherDraws;
		Com_Memset(&s_liqBypass, 0, sizeof(s_liqBypass));
		s_liqBypass.frame = backEndData->realFrameNumber;
		s_liqBypass.weatherFrame = weatherFrame;
		s_liqBypass.weatherDraws = weatherDraws;
	}
	s_liqBypass.counts[reason]++;
	for ( int i = 0; i < s_liqBypass.numShaders; i++ )
	{
		if ( s_liqBypass.shaders[i] == shader && s_liqBypass.shaderReason[i] == reason )
		{
			s_liqBypass.shaderCount[i]++;
			return;
		}
	}
	if ( s_liqBypass.numShaders < MAX_LIQUID_BYPASS_SHADERS )
	{
		const int n = s_liqBypass.numShaders++;
		s_liqBypass.shaders[n] = shader;
		s_liqBypass.shaderReason[n] = reason;
		s_liqBypass.shaderCount[n] = 1;
	}
}

int RB_LiquidBypassReason( const shader_t *shader, const shaderStage_t *stage, qboolean lightall, qboolean fogged )
{
	(void)stage;
	if ( !RB_LiquidFrameHasLiquids() || backEnd.depthFill || backEnd.projection2D || shader->isSky ||
		(backEnd.viewParms.flags & VPF_DEPTHSHADOW) )
		return LIQUIDBYPASS_NONE;

	int reason = LIQUIDBYPASS_NONE;
	if ( backEnd.refractionFill )
		reason = LIQUIDBYPASS_REFRACTION;
	else if ( RB_VolumetricFogMode(shader->sort) != 1 )
		return LIQUIDBYPASS_NONE;	// the composite fogs it, or not the froxel view
	else if ( lightall )
		reason = (shader->fogPass != FP_NONE) ? LIQUIDBYPASS_NONE : LIQUIDBYPASS_LIGHTALL;
	else if ( !fogged && shader->fogPass == FP_NONE )
		reason = LIQUIDBYPASS_BLEND;

	if ( reason != LIQUIDBYPASS_NONE )
		RB_LiquidBypassCount(reason, shader);
	return reason;
}

void RB_LiquidBypassImmediate( const shader_t *shader )
{
	if ( RB_LiquidFrameHasLiquids() && backEnd.volumetricView && !backEnd.depthFill )
		RB_LiquidBypassCount(LIQUIDBYPASS_IMMEDIATE, shader);
}

void RB_LiquidBypassDebugColor( int reason, vec4_t materialDebug )
{
	if ( reason <= LIQUIDBYPASS_NONE || reason >= LIQUIDBYPASS_COUNT || r_volumetricFogDebug->integer != 69 )
		return;
	VectorSet4(materialDebug, s_liquidBypassColors[reason][0], s_liquidBypassColors[reason][1],
		s_liquidBypassColors[reason][2], 1.0f);
}

void RB_LiquidWeatherCulled( void )
{
	if ( s_liqBypass.weatherFrame != backEndData->realFrameNumber )
	{
		s_liqBypass.weatherFrame = backEndData->realFrameNumber;
		s_liqBypass.weatherDraws = 0;
	}
	s_liqBypass.weatherDraws++;
}

/*
=================
RB_LiquidWeatherSetupDraw

Rain, snow and splashes inside a liquid brush are dropped (weather.glsl,
weatherSplash.glsl, USE_LIQUIDS): the weather occlusion map holds the opaque
world only, so without this the rain falls through the water surface and
splashes on the bed. Uses the brushes of the froxel view (the Liquids block):
u_LiquidSurface.x 1 = test. Either writer may be NULL.
=================
*/
qboolean RB_LiquidWeatherSetupDraw( UniformDataWriter *uniforms, SamplerBindingsWriter *samplers )
{
	const qboolean cull = (qboolean)(R_LiquidsAvailable() && s_liq.planeImageValid && tr.liquidsUboOffset != -1 &&
		RB_LiquidFrameHasLiquids() && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW));
	if ( uniforms )
	{
		vec4_t params = { cull ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
		uniforms->SetUniformVec4(UNIFORM_LIQUIDSURFACE, params);
		if ( cull )
			RB_LiquidWeatherCulled();
	}
	if ( samplers && s_liq.planeImageValid )
		samplers->AddStaticImage(&s_liq.planeImage, TB_LIQUIDPLANES);
	return cull;
}

UniformBlockBinding RB_GetLiquidsBlockUniformBinding( void )
{
	const byte currentFrameScene = backEndData->currentFrame->currentScene;
	UniformBlockBinding binding = {};
	binding.ubo = backEndData->currentFrame->ubo[currentFrameScene];
	binding.block = UNIFORM_BLOCK_LIQUIDS;
	binding.offset = (tr.liquidsUboOffset == -1) ? 0 : tr.liquidsUboOffset;
	return binding;
}

/*
=================
R_Liquids_f

r_liquids: the liquid brushes of the map, their media, what gameplay sees that
the medium does not, the last froxel frame, memory and timings.
r_liquids dump: the same as one JSON object per map (tools/rend2/liquid_audit.py
prints the same fields for every BSP without the game).
=================
*/
// covered length per medium of the ray o + t dir, t in [0, maxT], inside the
// kept brushes of the drawn classes: the CPU copy of LiquidCoverage (exact
// clip, union of the intervals of a medium)
static void R_LiquidRayCoverage( const vec3_t o, const vec3_t dir, float maxT, vec3_t covered )
{
	VectorClear(covered);
	const int mask = R_LiquidClassMask();
	struct interval_t { float enter, exit; int medium; };
	std::vector<interval_t> hits;
	for ( int i = 0; mask && i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		if ( !(mask & (1 << brush->liquidClass)) )
			continue;
		float enter = 0.0f, exit = maxT;
		for ( int k = 0; k < brush->numPlanes && enter < exit; k++ )
		{
			const float *plane = tr.world->liquidPlanes[brush->firstPlane + k];
			const float denom = DotProduct(plane, dir);
			const float dist = DotProduct(plane, o) - plane[3];
			if ( fabsf(denom) < 1e-8f )
			{
				if ( dist > 0.0f )
					exit = -1.0f;
				continue;
			}
			const float t = -dist / denom;
			if ( denom < 0.0f )
				enter = MAX(enter, t);
			else
				exit = MIN(exit, t);
		}
		if ( enter < exit )
			hits.push_back({ enter, exit, brush->mediumSlot });
	}
	std::sort(hits.begin(), hits.end(), []( const interval_t& a, const interval_t& b ) { return a.enter < b.enter; });
	float end[LIQUID_CLASSES] = { -1e30f, -1e30f, -1e30f };
	for ( const interval_t& h : hits )
	{
		if ( h.exit > end[h.medium] )
		{
			covered[h.medium] += h.exit - MAX(h.enter, end[h.medium]);
			end[h.medium] = h.exit;
		}
	}
}

/*
=================
R_LiquidsCamera_f

r_liquids camera: the liquid at the camera as gameplay (collision), the
medium (kept brushes) and the GPU block see it, the tint decision for cgame,
the medium, and the liquid along the view forward / up: segment length and
its transmittance (the CPU copy of the clip the volume uses).
=================
*/
static void R_LiquidsCamera_f( void )
{
	const float *origin = tr.refdef.vieworg;
	const int cameraClass = R_LiquidPointClass(origin);
	const int brush = (cameraClass >= 0) ? R_LiquidPointBrush(origin, cameraClass) : -1;
	int gpuSlot = -1;
	for ( int n = 0; brush >= 0 && n < s_liq.lastVisible; n++ )
	{
		if ( s_liq.blockBrush[n] == brush )
			gpuSlot = n;
	}

	ri.Printf(PRINT_ALL, "camera (%.0f %.0f %.0f): collision class %s, medium brush %d", origin[0], origin[1], origin[2],
		cameraClass < 0 ? "none" : s_liquidClassNames[cameraClass], brush);
	if ( brush >= 0 )
	{
		const liquidBrush_t *b = &tr.world->liquids[brush];
		ri.Printf(PRINT_ALL, " (BSP brush %d, medium %s), GPU block slot %d of %d\n", b->brushNum,
			s_liquidClassNames[b->mediumSlot], gpuSlot, s_liq.lastVisible);
	}
	else
		ri.Printf(PRINT_ALL, "%s\n", cameraClass >= 0 ? " -- gameplay liquid the medium does not have" : "");

	for ( int c = 0; c < LIQUID_CLASSES; c++ )
	{
		ri.Printf(PRINT_ALL, "  r_volumetricWaterActive %s bit %d: %s -> cgame %s\n", s_liquidClassNames[c],
			(s_liq.activeMask >> c) & 1, s_liquidActiveReasons[s_liq.activeReason[c]],
			((s_liq.activeMask >> c) & 1) ? "skips its tint (the medium colors the view)" : "draws its legacy tint");
	}

	const int mediumSlot = (brush >= 0) ? tr.world->liquids[brush].mediumSlot : -1;
	vec3_t sigma[LIQUID_CLASSES];
	for ( int c = 0; c < LIQUID_CLASSES; c++ )
	{
		vec4_t extinction, albedo;
		R_LiquidsMaterial(c, extinction, albedo);
		VectorScale(extinction, extinction[3], sigma[c]);
		if ( c == mediumSlot )
		{
			ri.Printf(PRINT_ALL, "  medium %s: sigma_t %.5f (rgb %.5f %.5f %.5f, 1/e after %.0f units), albedo %.2f %.2f %.2f, "
				"sigma_s rgb %.5f %.5f %.5f, g %.2f\n", s_liquidClassNames[c], extinction[3], sigma[c][0], sigma[c][1],
				sigma[c][2], extinction[3] > 0.0f ? 1.0f / extinction[3] : 0.0f, albedo[0], albedo[1], albedo[2],
				sigma[c][0] * albedo[0], sigma[c][1] * albedo[1], sigma[c][2] * albedo[2], albedo[3]);
		}
	}

	const char *names[2] = { "forward", "up" };
	const vec3_t up = { 0.0f, 0.0f, 1.0f };
	const float *dirs[2] = { tr.refdef.viewaxis[0], up };
	const float farZ = R_VolumetricFarZ();
	for ( int d = 0; d < 2; d++ )
	{
		vec3_t covered, tau = { 0.0f, 0.0f, 0.0f };
		R_LiquidRayCoverage(origin, dirs[d], 65536.0f, covered);
		for ( int c = 0; c < LIQUID_CLASSES; c++ )
			VectorMA(tau, covered[c], sigma[c], tau);
		ri.Printf(PRINT_ALL, "  %-7s liquid segment %.1f units (water %.1f slime %.1f lava %.1f), T rgb %.3f %.3f %.3f%s\n",
			names[d], covered[0] + covered[1] + covered[2], covered[0], covered[1], covered[2],
			expf(-tau[0]), expf(-tau[1]), expf(-tau[2]),
			(d == 0 && farZ > 0.0f && covered[0] + covered[1] + covered[2] > farZ * 0.8f) ?
				" (longer than the medium: it fades out before the froxel far)" : "");
	}
	ri.Printf(PRINT_ALL, "  in-scattering and the integrated transmittance per pixel: r_volumetricFogDebug 66 / 67 (63 exact, 65 length)\n");
}

/*
=================
R_LiquidsBypass_f

r_liquids bypass: the draws of the last frame with liquids in its froxel view
that got no froxel fog (r_volumetricFogDebug 69 tints them), and the weather
draws that took the under-liquid cull.
=================
*/
static void R_LiquidsBypass_f( void )
{
	const qboolean current = (qboolean)(s_liqBypass.frame == s_liq.liquidFrame && s_liq.liquidFrame != 0);
	ri.Printf(PRINT_ALL, "liquid fog bypass, frame %d (last frame with liquids in the froxel view: %d)%s\n",
		s_liqBypass.frame, s_liq.liquidFrame, current ? "" : " -- no bypass recorded in that frame");
	for ( int r = LIQUIDBYPASS_NONE + 1; r < LIQUIDBYPASS_COUNT; r++ )
		ri.Printf(PRINT_ALL, "  %-34s %d draws\n", s_liquidBypassNames[r], current ? s_liqBypass.counts[r] : 0);
	for ( int i = 0; current && i < s_liqBypass.numShaders; i++ )
	{
		ri.Printf(PRINT_ALL, "    %4d x %-30s %s\n", s_liqBypass.shaderCount[i], s_liquidBypassNames[s_liqBypass.shaderReason[i]],
			s_liqBypass.shaders[i] ? s_liqBypass.shaders[i]->name : "-");
	}
	ri.Printf(PRINT_ALL, "  weather draws with the under-liquid cull: %d (frame %d)\n",
		s_liqBypass.weatherFrame == s_liq.liquidFrame ? s_liqBypass.weatherDraws : 0, s_liqBypass.weatherFrame);
	ri.Printf(PRINT_ALL, "  not counted here (see docs, \"Render classes under water\"): mirrors / portals, the sun disc and\n"
		"  sun rays through a legacy water surface, the r_waterSurface Snell window, flares through legacy water\n");
}

void R_Liquids_f( void )
{
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "r_liquids: no map\n");
		return;
	}
	if ( ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "camera") )
	{
		R_LiquidsCamera_f();
		return;
	}
	if ( ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "bypass") )
	{
		R_LiquidsBypass_f();
		return;
	}
	const world_t *w = tr.world;
	const qboolean dump = (qboolean)(ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "dump"));
	auto shaderName = [w]( int n ) { return (n >= 0 && n < w->numShaders) ? w->shaders[n].shader : "-"; };

	int classCount[LIQUID_CLASSES] = {}, slotCount[LIQUID_CLASSES] = {}, sourceCount[3] = {};
	for ( int i = 0; i < w->numLiquids; i++ )
	{
		classCount[w->liquids[i].liquidClass]++;
		slotCount[w->liquids[i].mediumSlot]++;
		sourceCount[w->liquids[i].slotSource]++;
	}

	// memory: the static plane buffer (at least one texel), the caustic pattern
	// with its mips, the block of every scene, the hunk of the map
	const int planeBytes = MAX(w->numLiquidPlanes, 1) * (int)sizeof(vec4_t);
	const int causticBytes = s_liq.causticImageValid ? 256 * 256 * 2 * 4 / 3 : 0;
	const int hunkBytes = w->numLiquids * (int)sizeof(liquidBrush_t) + w->numLiquidPlanes * (int)sizeof(vec4_t);

	if ( dump )
	{
		ri.Printf(PRINT_ALL, "{ \"map\": \"%s\", \"brushes\": %d, \"planes\": %d, \"water\": %d, \"slime\": %d, \"lava\": %d,\n",
			w->baseName, w->numLiquids, w->numLiquidPlanes, classCount[0], classCount[1], classCount[2]);
		ri.Printf(PRINT_ALL, "  \"media\": { \"water\": %d, \"slime\": %d, \"lava\": %d }, \"slotSources\": { \"contents\": %d, \"topSlime\": %d, \"envJson\": %d },\n",
			slotCount[0], slotCount[1], slotCount[2], sourceCount[0], sourceCount[1], sourceCount[2]);
		ri.Printf(PRINT_ALL, "  \"skipped\": { \"fog\": %d, \"shape\": %d, \"capacity\": %d, \"brushModel\": %d },\n",
			w->liquidSkipped[0], w->liquidSkipped[1], w->liquidSkipped[2], w->liquidSkipped[3]);
		ri.Printf(PRINT_ALL, "  \"lastFrame\": { \"visible\": %d, \"candidates\": %d, \"gpuMax\": %d, \"buildUsec\": %.1f },\n",
			s_liq.lastVisible, s_liq.lastCandidates, MAX_GPU_LIQUIDS, s_liq.lastBuildUsec);
		ri.Printf(PRINT_ALL, "  \"memory\": { \"planeBuffer\": %d, \"caustics\": %d, \"blockPerScene\": %d, \"hunk\": %d }, \"loadMsec\": %.2f,\n",
			planeBytes, causticBytes, (int)sizeof(LiquidsBlock), hunkBytes, w->liquidLoadMsec);
		ri.Printf(PRINT_ALL, "  \"liquids\": [\n");
		for ( int i = 0; i < w->numLiquids; i++ )
		{
			const liquidBrush_t *b = &w->liquids[i];
			ri.Printf(PRINT_ALL, "    { \"brush\": %d, \"class\": \"%s\", \"medium\": \"%s\", \"source\": \"%s\", \"sides\": %d, \"shader\": \"%s\", \"top\": \"%s\", \"mins\": [%g, %g, %g], \"maxs\": [%g, %g, %g] }%s\n",
				b->brushNum, s_liquidClassNames[b->liquidClass], s_liquidClassNames[b->mediumSlot],
				s_liquidSlotSources[b->slotSource], b->numPlanes, shaderName(b->shaderNum), shaderName(b->topShaderNum),
				b->bounds[0][0], b->bounds[0][1], b->bounds[0][2], b->bounds[1][0], b->bounds[1][1], b->bounds[1][2],
				(i + 1 < w->numLiquids) ? "," : "");
		}
		ri.Printf(PRINT_ALL, "  ],\n  \"gameplayOnly\": [\n");
		for ( size_t i = 0; i < s_liquidSkips.size(); i++ )
		{
			const liquidSkip_t& k = s_liquidSkips[i];
			ri.Printf(PRINT_ALL, "    { \"brush\": %d, \"model\": %d, \"reason\": \"%s\", \"shader\": \"%s\" }%s\n",
				k.brushNum, k.model, s_liquidSkipNames[k.reason], shaderName(k.shaderNum),
				(i + 1 < s_liquidSkips.size()) ? "," : "");
		}
		ri.Printf(PRINT_ALL, "  ] }\n");
		return;
	}

	ri.Printf(PRINT_ALL, "Liquid brushes of %s: %d (%d planes): %d water, %d slime, %d lava; load %.2f ms\n", w->baseName,
		w->numLiquids, w->numLiquidPlanes, classCount[0], classCount[1], classCount[2], w->liquidLoadMsec);
	ri.Printf(PRINT_ALL, "  media: %d water, %d slime, %d lava profile (%d by contents, %d slime top side, %d env.json; %d rule%s)\n",
		slotCount[0], slotCount[1], slotCount[2], sourceCount[0], sourceCount[1], sourceCount[2],
		(int)s_liquidRules.size(), (s_liquidRules.size() == 1) ? "" : "s");
	ri.Printf(PRINT_ALL, "  skipped: %d fog contents, %d shape (sides / not axial), %d capacity, %d brush model (moving, unsupported)\n",
		w->liquidSkipped[0], w->liquidSkipped[1], w->liquidSkipped[2], w->liquidSkipped[3]);
	for ( const liquidSkip_t& k : s_liquidSkips )
	{
		ri.Printf(PRINT_ALL, "    gameplay only: brush %5d model %3d %-30s %s\n", k.brushNum, k.model,
			s_liquidSkipNames[k.reason], shaderName(k.shaderNum));
	}
	ri.Printf(PRINT_ALL, "  r_volumetricWater %d (latched), available %s, classes drawn %d (r_volumetricWaterActive %d), media %d, surfaces %s\n",
		r_volumetricWater->integer, R_LiquidsAvailable() ? "yes" : "no", R_LiquidClassMask(),
		r_volumetricWaterActive->integer, R_LiquidMediumSlotMask(), R_LiquidSurfacesEnabled() ? "yes" : "no");
	ri.Printf(PRINT_ALL, "  last froxel frame: %d visible of %d candidates (GPU max %d, %d dropped), build %.1f us\n",
		s_liq.lastVisible, s_liq.lastCandidates, MAX_GPU_LIQUIDS, s_liq.lastCandidates - s_liq.lastVisible,
		s_liq.lastBuildUsec);
	ri.Printf(PRINT_ALL, "  memory: plane buffer %d B, caustics %d B, Liquids block %d B per scene, hunk %d B\n",
		planeBytes, causticBytes, (int)sizeof(LiquidsBlock), hunkBytes);

	for ( int i = 0; i < w->numLiquids; i++ )
	{
		const liquidBrush_t *b = &w->liquids[i];
		ri.Printf(PRINT_ALL, "  %3d %-5s medium %-5s (%s) brush %5d sides %2d (%5.0f %5.0f %5.0f)-(%5.0f %5.0f %5.0f) %s top %s\n",
			i, s_liquidClassNames[b->liquidClass], s_liquidClassNames[b->mediumSlot], s_liquidSlotSources[b->slotSource],
			b->brushNum, b->numPlanes,
			b->bounds[0][0], b->bounds[0][1], b->bounds[0][2],
			b->bounds[1][0], b->bounds[1][1], b->bounds[1][2], shaderName(b->shaderNum), shaderName(b->topShaderNum));
	}

	const int cameraClass = w->numLiquids ? R_LiquidPointClass(tr.refdef.vieworg) : -1;
	ri.Printf(PRINT_ALL, "  camera contents: %s\n", cameraClass < 0 ? "none" : s_liquidClassNames[cameraClass]);
}
