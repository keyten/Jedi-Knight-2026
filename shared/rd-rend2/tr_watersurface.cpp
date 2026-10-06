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

// tr_watersurface.cpp -- modern water surface (r_waterSurface)
//
// The stock water surfaces of the maps get a physically based surface over
// the liquid medium, without rebaking a map or changing an asset: the opaque
// scene is complete and lit, then the water reads its color and depth and
// composites refraction (depth aware), the liquid's absorption / scattering,
// the dielectric Fresnel reflection (SSR -> cubemap -> fallback) and GGX glints
// (glsl/watersurface.glsl). The generic refraction (refractive shaders,
// RF_DISTORTION, drawn after tone mapping) is not touched.
//
// Classification (per map, R_WaterClassifySurfaces): the BSP semantics first.
// A drawn surface whose BSP shader has CONTENTS_WATER (surfaceparm water) is
// water, unless it is lava; its optics are slime when it also has
// CONTENTS_SLIME. The surface is matched to the liquid brush it lies on (all
// vertices on a side plane, inside the brush) for diagnostics and the fog
// volume medium. A shader is drawn as water when at least half of its area on
// the map faces up (its bottom faces left out): waterfalls and streams (steep
// patches with the same contents) keep their legacy stages. An existing refractive shader lying on a
// water brush is water too. The env.json "Liquids" profile of a shader
// (tr_liquid.cpp) sets its optics, as it sets the medium of the liquid brushes
// it lies on. Then r_waterOverride (name or prefix*), last the
// experimental name rule (r_waterSurfaceExperimental, off by default).
//
// Pipeline: the stage iterator draws a classified surface once with the water
// program (RB_WaterSurfaceDraws). Its draw items are tagged and the main pass
// draws them in their own slot (RB_SubmitRenderPass): after the opaque sort,
// the screen-space passes (SSR, SSGI) and the layers up to SS_FOG, before the
// atmosphere / clouds / froxel fog composites and the blended layers. There
// RB_WaterSurfacePrepare copies the HDR scene and its depth once, the water
// writes depth, so the composites fog the camera -> surface segment and the
// water the segment behind it. Everything before tone mapping: bloom sees the
// glints.
//
// r_waterSurface 0 (latched): no targets, no programs, no extra pass: the
// legacy draws are unchanged.
//
// docs/rend2-water-surface.md

#include "tr_local.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include "json.h"

/*
============================================================

Classification records (r_waterInfo)

============================================================
*/

enum waterReason_t
{
	WREASON_NONE,
	WREASON_CONTENTS,			// CONTENTS_WATER (surfaceparm water) of the BSP shader
	WREASON_BRUSH_TOP,	// upward boundary of a water/slime brush
	WREASON_OVERRIDE_ON,		// r_waterOverride on
	WREASON_EXPERIMENTAL,		// r_waterSurfaceExperimental name rule
	// legacy
	WREASON_LAVA,				// lava contents: an emissive liquid, not a water surface
	WREASON_SLIME_ONLY,			// slime without water contents
	WREASON_NOT_HORIZONTAL,		// the shader's area mostly does not face up (waterfall, stream)
	WREASON_REFRACTIVE_ONLY,	// refractive without water semantics: stays generic refraction
	WREASON_NO_SEMANTICS,		// water-like name or material only
	WREASON_OVERRIDE_OFF,		// r_waterOverride off
	WREASON_UNSUPPORTED,		// sky, portal, nodraw, flare
	WREASON_COUNT
};

static const char *s_reasonNames[WREASON_COUNT] = {
	"-",
	"CONTENTS_WATER",
	"top of a water brush",
	"r_waterOverride on",
	"experimental name rule",
	"lava",
	"slime without water contents",
	"not an upward interface (side / bottom / waterfall)",
	"refractive, no water semantics (generic refraction)",
	"water-like name / material only, no water semantics",
	"r_waterOverride off",
	"sky / portal / nodraw / flare",
};

enum { WORIENT_UP, WORIENT_DOWN, WORIENT_VERTICAL, WORIENT_SLOPED };
static const char *s_orientNames[] = { "up", "down", "vertical", "sloped" };
static const char *s_liquidNames[LIQUID_CLASSES] = { "water", "slime", "lava" };

struct waterBrushRecord_t
{
	int			brushNum;
	int			model;
	int			liquidClass;
	int			contents;
	int			shaderNum;
	int			firstPlane;		// in s_water.planes
	int			numPlanes;
	vec3_t		bounds[2];
};

struct waterSurfaceRecord_t
{
	int			surfaceNum;
	int			model;
	int			bspShader;
	int			contents;
	int			surfaceFlags;
	int			type;			// MST_*
	shader_t	*shader;
	vec3_t		normal;
	int			orient;
	float		area;
	vec3_t		bounds[2];
	int			brush;			// index in s_water.brushes, -1: none
	vec3_t		brushSide;		// normal of the side it lies on
	qboolean	refractive;
	qboolean	nameLike;
	qboolean modern;
	int reason;
	int liquidClass;
	int flags;
};

// Metadata only. No field here is consumed by the existing water draw path.
struct legacyWaterMotion_t
{
	struct scroll_t { int stage; float x, y; };
	struct wave_t { int stage; float base, amplitude, phase, frequency; };
	std::vector<scroll_t> scrolls;
	std::vector<wave_t> turb, stretch;
	float weightedScroll[2];
	float disagreement;
	float turbAmplitude, turbFrequency;
	float deformAmplitude, deformFrequency;
};

struct waterDynamics_t
{
	const char *name;
	float amplitude, wavelength, choppiness, speed, microNormal;
	float flow, damping, wake, foam, shoreline, rain;
};

static const waterDynamics_t s_dynamics[] = {
	{ "generic_water", .15f, 96, .1f, 1, 1, 0, .8f, 1, .2f, .5f, 1 },
	{ "still_pool", .025f, 48, .02f, .4f, .5f, 0, 1.3f, .4f, .05f, .2f, .6f },
	{ "calm_water", .08f, 80, .05f, .7f, .8f, 0, 1, .7f, .1f, .4f, .8f },
	{ "lake", .3f, 192, .2f, 1, 1, 0, .8f, 1, .3f, .7f, 1 },
	{ "slow_stream", .12f, 64, .08f, 1.1f, 1, .4f, 1.2f, .8f, .3f, .7f, 1 },
	{ "fast_stream", .2f, 48, .2f, 1.8f, 1.2f, 1, 1.8f, 1, .7f, 1, 1.2f },
	{ "slime", .04f, 64, .02f, .3f, .4f, 0, 2, .2f, .1f, .3f, .3f },
	{ "waterfall", .2f, 40, .1f, 2, 1, 1, 1.5f, 1, .8f, 1, 1 },
	{ "heavy_waterfall", .4f, 64, .2f, 2.5f, 1.2f, 1, 2, 1.5f, 1, 1, 1.2f },
	{ "no_water_dynamics", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

struct waterBody_t
{
	int id, model, liquidClass, opticsClass, dynamics;
	const char *decision;
	vec3_t bounds[2], planeNormal;
	float planeDist, area, depthAverage, depthDeepest;
	std::vector<int> surfaces, brushes;
	std::set<std::string> shaders;
	legacyWaterMotion_t legacy;
	vec2_t flow;
	float waveMultiplier, foamMultiplier, interactionMultiplier;
};

struct waterBodyRule_t
{
	std::string shader;
	int id, dynamics;
	bool hasPoint, hasBounds, hasFlow;
	vec3_t point, bounds[2];
	vec2_t flow;
	float wave, foam, interaction;
};

struct waterShaderRecord_t
{
	shader_t	*shader;
	int			bspShader;
	int			contents;
	int			surfaces;
	int			orientCount[4];
	float		area;
	float		upArea;
	float		downArea;
	int			linked;
	int			reason;
	int			liquidClass;	// optics
	int			flags;			// WATERSURF_*
	qboolean	modern;
};

struct waterPlane_t
{
	vec3_t		normal;
	float		dist;
};

// what the classification needs of every surface of the map (the BSP lumps
// are gone after the load): kept so r_waterOverride can add any shader later
struct waterSurfaceSource_t
{
	int			bspShader;
	int			model;
	int			type;			// MST_*
};

struct waterOverride_t
{
	std::string	pattern;		// lower case, trailing '*' = prefix
	int			mode;			// 1 on, 0 off
	int			liquidClass;	// -1 auto
};

static struct
{
	qboolean	resources;		// targets and programs exist (latched r_waterSurface)

	const world_t	*world;
	std::vector<waterBrushRecord_t>		brushes;
	std::vector<waterPlane_t>			planes;
	std::vector<waterSurfaceSource_t>	sources;
	std::vector<waterSurfaceRecord_t>	surfaces;
	std::vector<waterShaderRecord_t>	shaders;
	std::vector<waterBody_t>			bodies;
	std::vector<waterBodyRule_t>		bodyRules;
	float		bodyMsec;
	qboolean	bodyDebug;
	qhandle_t	bodyDebugShader;
	float		classifyMsec;
	std::vector<int> mergedViewSurfaces;

	std::vector<waterOverride_t>		overrides;

	// per frame
	int			timer;
	int			lastDraws;
	int			drawsThisFrame;
	unsigned	statsFrame;
	screenHistory_t history;
	qboolean reflectionReady;
	qboolean reflectionHistoryValid;
	int reflectionCurrent;
	float reflectionScale;
	vec4_t viewWater[WATER_UNIFORM_VEC4S];
	qboolean viewWaterReady;
	vec4_t extinction[LIQUID_CLASSES], albedo[LIQUID_CLASSES];
	screenViewInfo_t viewInfo;
	vec4_t ssrSettings[3];
	int viewFlags;
	int fallbackCubemap;
} s_water;

qboolean R_WaterSurfaceResourcesEnabled( void )
{
	return s_water.resources;
}

static int R_WaterLiquidClass( int contents )
{
	if ( contents & CONTENTS_LAVA )
		return LIQUID_LAVA;
	if ( contents & CONTENTS_SLIME )
		return LIQUID_SLIME;
	if ( contents & CONTENTS_WATER )
		return LIQUID_WATER;
	return -1;
}

// the experimental name rule (and the r_waterInfo hint): whole words of the
// path, never a plain substring of an unrelated word
static qboolean R_WaterNameLike( const char *name )
{
	static const char *words[] = { "water", "pool", "lake", "river", "pond", "ocean", "swamp", "liquid", NULL };
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	for ( int i = 0; words[i]; i++ )
	{
		const size_t len = strlen(words[i]);
		for ( const char *p = strstr(lower, words[i]); p; p = strstr(p + 1, words[i]) )
		{
			// the word may be followed by digits or a separator, and must start a path element or word
			const qboolean start = (qboolean)(p == lower || p[-1] == '/' || p[-1] == '_' || p[-1] == '-');
			const char next = p[len];
			const qboolean end = (qboolean)(next == '\0' || next == '_' || next == '-' || next == '/' ||
				(next >= '0' && next <= '9'));
			if ( start && end )
				return qtrue;
		}
	}
	return qfalse;
}

static qboolean R_WaterPatternMatches( const std::string& pattern, const char *name )
{
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	if ( !pattern.empty() && pattern.back() == '*' )
		return (qboolean)(strncmp(lower, pattern.c_str(), pattern.size() - 1) == 0);
	return (qboolean)(pattern == lower);
}

static const waterOverride_t *R_WaterFindOverride( const char *name )
{
	// the last matching entry wins (a later, more specific line replaces a prefix)
	const waterOverride_t *found = NULL;
	for ( const waterOverride_t& o : s_water.overrides )
	{
		if ( R_WaterPatternMatches(o.pattern, name) )
			found = &o;
	}
	return found;
}

static int R_WaterOrientation( const vec3_t n )
{
	if ( n[2] > 0.7f )
		return WORIENT_UP;
	if ( n[2] < -0.7f )
		return WORIENT_DOWN;
	if ( fabsf(n[2]) < 0.3f )
		return WORIENT_VERTICAL;
	return WORIENT_SLOPED;
}

// p inside brush b (grown by eps)
static qboolean R_WaterInsideBrush( const waterBrushRecord_t& b, const vec3_t p, float eps )
{
	for ( int k = 0; k < b.numPlanes; k++ )
	{
		const waterPlane_t& plane = s_water.planes[b.firstPlane + k];
		if ( DotProduct(plane.normal, p) - plane.dist > eps )
			return qfalse;
	}
	return qtrue;
}

/*
=================
R_WaterDecideShaders

Per shader decision from the surface records, then r_waterOverride and the
experimental rule; writes per-surface waterKey and shader candidate flags.
Runs at map load and again when r_waterOverride or r_waterSurfaceExperimental
change.
=================
*/
static qboolean R_WaterMostlyUp( const waterShaderRecord_t& s )
{
	return (qboolean)(s.upArea > 0.0f && s.upArea >= 0.5f * (s.area - s.downArea));
}

static void R_WaterRefreshBodies( void );

static void R_WaterDecideShaders( void )
{
	// reset every shader of the records (a previous decision may be undone)
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		s.shader->waterSurface = 0;
		s.shader->waterFlowStage = nullptr;
		for (int stage = 0; stage < MAX_SHADER_STAGES && s.shader->stages[stage]; stage++)
		{
			shaderStage_t *candidate = s.shader->stages[stage];
			shaderStage_t *flow = s.shader->waterFlowStage;
			if (!candidate->ss && !candidate->bundle[0].isLightmap &&
				(!flow || (!flow->bundle[0].numTexMods && candidate->bundle[0].numTexMods)))
				s.shader->waterFlowStage = candidate;
		}
	}

	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		const int liquidClass = R_WaterLiquidClass(s.contents);
		const qboolean refractive = s.shader->useDistortion;
		s.modern = qfalse;
		s.flags = 0;
		s.liquidClass = (liquidClass == LIQUID_SLIME) ? LIQUID_SLIME : LIQUID_WATER;

		if ( s.shader->isSky || s.shader->isPortal || (s.shader->surfaceFlags & SURF_NODRAW) )
			s.reason = WREASON_UNSUPPORTED;
		else if ( liquidClass == LIQUID_LAVA )
			s.reason = WREASON_LAVA;
		else if ( s.contents & CONTENTS_WATER )
			s.reason = s.upArea > 0.0f ? WREASON_CONTENTS : WREASON_NOT_HORIZONTAL;
		else if ( liquidClass == LIQUID_SLIME )
			s.reason = WREASON_SLIME_ONLY;
		else if ( s.linked > 0 && s.upArea > 0.0f )
			s.reason = WREASON_BRUSH_TOP;
		else if ( refractive )
			s.reason = WREASON_REFRACTIVE_ONLY;
		else
			s.reason = WREASON_NO_SEMANTICS;

		s.modern = (qboolean)(s.reason == WREASON_CONTENTS || s.reason == WREASON_BRUSH_TOP);

		if ( s.reason == WREASON_UNSUPPORTED )
			continue;

		const waterOverride_t *o = R_WaterFindOverride(s.shader->name);
		if ( o )
		{
			s.modern = (qboolean)(o->mode != 0);
			s.reason = o->mode ? WREASON_OVERRIDE_ON : WREASON_OVERRIDE_OFF;
			s.flags |= WATERSURF_OVERRIDE;
			if ( o->liquidClass >= 0 )
				s.liquidClass = o->liquidClass;
		}
		else if ( !s.modern && r_waterSurfaceExperimental->integer && s.reason == WREASON_NO_SEMANTICS &&
			R_WaterNameLike(s.shader->name) && R_WaterMostlyUp(s) )
		{
			s.modern = qtrue;
			s.reason = WREASON_EXPERIMENTAL;
			s.flags |= WATERSURF_EXPERIMENTAL;
		}
	}

	std::unordered_map<const shader_t *, const waterShaderRecord_t *> decisions;
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		decisions[s.shader] = &s;
		if ( s.modern )
			s.shader->waterSurface = 1; // candidate only; draws use the surface key
	}
	for ( waterSurfaceRecord_t& r : s_water.surfaces )
	{
		const waterShaderRecord_t& d = *decisions[r.shader];
		r.reason = d.reason;
		r.flags = d.flags;
		r.liquidClass = R_WaterLiquidClass(r.contents) == LIQUID_SLIME ? LIQUID_SLIME : LIQUID_WATER;
		const waterBrushRecord_t *brush = r.brush >= 0 ? &s_water.brushes[r.brush] : nullptr;
		if ( brush && brush->liquidClass == LIQUID_SLIME )
			r.liquidClass = LIQUID_SLIME;
		if ( (r.contents & CONTENTS_FOG) || (brush && (brush->contents & CONTENTS_FOG)) )
			r.flags |= WATERSURF_FOG_MEDIUM;
		const int profile = R_LiquidProfileForShader(r.shader->name);
		if ( profile == LIQUID_WATER || profile == LIQUID_SLIME )
			r.liquidClass = profile;
		const waterOverride_t *override = R_WaterFindOverride(r.shader->name);
		if ( override && override->liquidClass >= 0 )
			r.liquidClass = override->liquidClass;
		// Only an upward boundary is the pool/lake interface. Sides and bottoms
		// retain their legacy material; a shared shader must not turn them into air.
		const bool boundary = r.orient == WORIENT_UP;
		const bool linkedTop = brush && brush->liquidClass != LIQUID_LAVA &&
			r.brushSide[2] > 0.7f && DotProduct(r.normal, r.brushSide) > 0.7f;
		const bool semantics = (r.contents & CONTENTS_WATER) || linkedTop;
		r.modern = (qboolean)(d.modern && boundary && (semantics || override ||
			(d.flags & WATERSURF_EXPERIMENTAL)) && !(r.contents & CONTENTS_LAVA));
		if ( d.modern && !boundary )
			r.reason = WREASON_NOT_HORIZONTAL;
		uint32_t key = 0;
		if ( r.modern )
		{
			key = WATERKEY_INTERFACE | ((uint32_t)r.liquidClass << 4) | ((uint32_t)r.flags << 8);
			if ( linkedTop && brush->model == 0 )
				key |= WATERKEY_WORLD_BRUSH | (1u << (16 + brush->liquidClass));
		}
		s_water.world->surfaces[r.surfaceNum].waterKey = key;
	}

	R_WaterUpdateMergedSurfaces(const_cast<world_t *>(s_water.world));
	R_WaterRefreshBodies();
}

void R_WaterUpdateMergedSurfaces(world_t *world)
{
	if (!world || s_water.world != world || !world->viewSurfaces || !world->numMergedSurfaces)
		return;
	if (s_water.mergedViewSurfaces.empty())
		s_water.mergedViewSurfaces.assign(world->viewSurfaces, world->viewSurfaces + world->nummarksurfaces);
	for (int i = 0; i < world->nummarksurfaces; i++)
	{
		const int original = world->marksurfaces[i];
		const shader_t *shader = world->surfaces[original].shader;
		// Keep original surfaces for water/overrides: their classification may
		// differ within a merged mesh. They still batch via the shared static VBO.
		world->viewSurfaces[i] = shader && (shader->waterSurface || R_WaterFindOverride(shader->name)) ?
			original : s_water.mergedViewSurfaces[i];
	}
}

static void R_WaterCollectCandidates( void );
static void R_WaterBuildBodies( void );

/*
=================
R_WaterClassifySurfaces

Called by R_LoadBSP after the surfaces (shader pointers, vertices) and the
planes are loaded. Keeps the records of the candidate surfaces for r_waterInfo.
=================
*/
void R_WaterClassifySurfaces( world_t *world, const byte *fileBase, const lump_t *surfacesLump,
	const lump_t *modelsLump, const lump_t *brushesLump, const lump_t *sidesLump )
{
	const int start = ri.Milliseconds();
	// the shaders of the previous map are gone with it
	s_water.world = world;
	s_water.mergedViewSurfaces.clear();
	s_water.history.valid = qfalse;
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();
	s_water.bodies.clear();
	s_water.bodyRules.clear();

	if ( surfacesLump->filelen % sizeof(dsurface_t) || modelsLump->filelen % sizeof(dmodel_t) ||
		brushesLump->filelen % sizeof(dbrush_t) || sidesLump->filelen % sizeof(dbrushside_t) )
	{
		return;
	}

	const dsurface_t *dsurfaces = (const dsurface_t *)(fileBase + surfacesLump->fileofs);
	const int numSurfaces = surfacesLump->filelen / sizeof(dsurface_t);
	const dmodel_t *models = (const dmodel_t *)(fileBase + modelsLump->fileofs);
	const int numModels = modelsLump->filelen / sizeof(dmodel_t);
	const dbrush_t *brushes = (const dbrush_t *)(fileBase + brushesLump->fileofs);
	const int numBrushes = brushesLump->filelen / sizeof(dbrush_t);
	const dbrushside_t *sides = (const dbrushside_t *)(fileBase + sidesLump->fileofs);
	const int numSides = sidesLump->filelen / sizeof(dbrushside_t);

	std::vector<int> surfaceModel(numSurfaces, -1);
	std::vector<int> brushModel(numBrushes, -1);
	for ( int m = 0; m < numModels; m++ )
	{
		const int fs = LittleLong(models[m].firstSurface), ns = LittleLong(models[m].numSurfaces);
		const int fb = LittleLong(models[m].firstBrush), nb = LittleLong(models[m].numBrushes);
		for ( int i = Q_max(fs, 0); i < fs + ns && i < numSurfaces; i++ )
			surfaceModel[i] = m;
		for ( int i = Q_max(fb, 0); i < fb + nb && i < numBrushes; i++ )
			brushModel[i] = m;
	}

	// every liquid brush, of every model, fog volumes included
	for ( int i = 0; i < numBrushes; i++ )
	{
		const int shaderNum = LittleLong(brushes[i].shaderNum);
		if ( shaderNum < 0 || shaderNum >= world->numShaders )
			continue;
		const int contents = world->shaders[shaderNum].contentFlags;
		const int liquidClass = R_WaterLiquidClass(contents);
		if ( liquidClass < 0 )
			continue;
		const int firstSide = LittleLong(brushes[i].firstSide);
		const int count = LittleLong(brushes[i].numSides);
		if ( firstSide < 0 || count <= 0 || firstSide + count > numSides )
			continue;

		waterBrushRecord_t b = {};
		b.brushNum = i;
		b.model = brushModel[i];
		b.liquidClass = liquidClass;
		b.contents = contents;
		b.shaderNum = shaderNum;
		b.firstPlane = (int)s_water.planes.size();
		ClearBounds(b.bounds[0], b.bounds[1]);
		for ( int k = 0; k < count; k++ )
		{
			const int planeNum = LittleLong(sides[firstSide + k].planeNum);
			if ( planeNum < 0 || planeNum >= world->numplanes )
				continue;
			const cplane_t *plane = &world->planes[planeNum];
			waterPlane_t p;
			VectorCopy(plane->normal, p.normal);
			p.dist = plane->dist;
			s_water.planes.push_back(p);
			b.numPlanes++;
			// axial sides bound the brush
			for ( int axis = 0; axis < 3; axis++ )
			{
				if ( plane->normal[axis] > 0.999f )
					b.bounds[1][axis] = plane->dist;
				else if ( plane->normal[axis] < -0.999f )
					b.bounds[0][axis] = -plane->dist;
			}
		}
		s_water.brushes.push_back(b);
	}

	s_water.sources.resize(world->numsurfaces);
	for ( int i = 0; i < world->numsurfaces; i++ )
	{
		waterSurfaceSource_t& src = s_water.sources[i];
		src.bspShader = i < numSurfaces ? LittleLong(dsurfaces[i].shaderNum) : -1;
		src.model = i < numSurfaces ? surfaceModel[i] : -1;
		src.type = i < numSurfaces ? LittleLong(dsurfaces[i].surfaceType) : MST_BAD;
	}

	R_WaterCollectCandidates();
	R_WaterBuildBodies();
	s_water.classifyMsec = (float)(ri.Milliseconds() - start);

	int modern = 0;
	for ( const waterShaderRecord_t& s : s_water.shaders )
		modern += s.modern ? 1 : 0;
	if ( !s_water.shaders.empty() )
		ri.Printf(PRINT_DEVELOPER, "Water surfaces: %d candidate shaders of %d cached (%d surfaces, %d liquid brushes), %.1f ms\n",
			modern, (int)s_water.shaders.size(), (int)s_water.surfaces.size(), (int)s_water.brushes.size(),
			s_water.classifyMsec);
}

/*
=================
R_WaterCollectCandidates

The candidate surfaces of the map (liquid contents, refractive, water-like
name or material, or a shader named by r_waterOverride), their geometry and
liquid brush, the per shader records, then the decisions.
=================
*/
static void R_WaterCollectCandidates( void )
{
	const world_t *world = s_water.world;
	// a shader that leaves the candidates (an override cleared) must not keep its decision
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		s.shader->waterSurface = 0;
	}
	s_water.surfaces.clear();
	s_water.shaders.clear();
	if ( !world )
		return;

	std::unordered_map<int, std::vector<size_t>> modelBrushes;
	for (size_t i = 0; i < s_water.brushes.size(); i++)
		modelBrushes[s_water.brushes[i].model].push_back(i);
	for ( int i = 0; i < world->numsurfaces && i < (int)s_water.sources.size(); i++ )
	{
		const int bspShader = s_water.sources[i].bspShader;
		if ( bspShader < 0 || bspShader >= world->numShaders )
			continue;
		const int contents = world->shaders[bspShader].contentFlags;
		const int surfaceFlags = world->shaders[bspShader].surfaceFlags;
		const msurface_t *surf = &world->surfaces[i];
		shader_t *shader = surf->shader;
		if ( !shader )
			continue;

		const qboolean refractive = shader->useDistortion;
		const qboolean nameLike = R_WaterNameLike(shader->name);

		waterSurfaceRecord_t r = {};
		r.surfaceNum = i;
		r.model = s_water.sources[i].model;
		r.bspShader = bspShader;
		r.contents = contents;
		r.surfaceFlags = surfaceFlags;
		r.type = s_water.sources[i].type;
		r.shader = shader;
		r.brush = -1;
		r.refractive = refractive;
		r.nameLike = nameLike;

		// geometry: the area weighted normal of the triangles
		const surfaceType_t type = surf->data ? *surf->data : SF_BAD;
		if ( type == SF_FACE || type == SF_GRID || type == SF_TRIANGLES )
		{
			const srfBspSurface_t *bsp = (const srfBspSurface_t *)surf->data;
			vec3_t sum = { 0.0f, 0.0f, 0.0f };
			float area = 0.0f;
			for ( int t = 0; t + 2 < bsp->numIndexes; t += 3 )
			{
				const int a = bsp->indexes[t], b = bsp->indexes[t + 1], c = bsp->indexes[t + 2];
				if ( a >= bsp->numVerts || b >= bsp->numVerts || c >= bsp->numVerts )
					continue;
				vec3_t e1, e2, n;
				VectorSubtract(bsp->verts[b].xyz, bsp->verts[a].xyz, e1);
				VectorSubtract(bsp->verts[c].xyz, bsp->verts[a].xyz, e2);
				CrossProduct(e2, e1, n);
				area += 0.5f * VectorLength(n);
				// the winding of the BSP triangles is clockwise seen from the front: compare with the
				// vertex normals and keep their side
				VectorAdd(sum, n, sum);
			}
			vec3_t vnormal = { 0.0f, 0.0f, 0.0f };
			vec3_t surfaceBounds[2];
			ClearBounds(surfaceBounds[0], surfaceBounds[1]);
			for ( int v = 0; v < bsp->numVerts; v++ )
			{
				VectorAdd(vnormal, bsp->verts[v].normal, vnormal);
				// Renderer cullBounds are built later by the VBO pass.
				AddPointToBounds(bsp->verts[v].xyz, surfaceBounds[0], surfaceBounds[1]);
			}
			if ( DotProduct(sum, vnormal) < 0.0f )
				VectorNegate(sum, sum);
			if ( VectorNormalize(sum) < 1e-6f )
				VectorNormalize2(vnormal, sum);
			VectorCopy(sum, r.normal);
			r.area = area;
			VectorCopy(surfaceBounds[0], r.bounds[0]);
			VectorCopy(surfaceBounds[1], r.bounds[1]);

			// the liquid brush it lies on: every vertex inside the brush and on one of its sides
			for ( size_t bi : modelBrushes[r.model] )
			{
				if (bsp->numVerts <= 0)
					break;
				const waterBrushRecord_t& b = s_water.brushes[bi];
				if ( b.model != r.model )
					continue;
				bool bounded = true;
				for ( int axis = 0; axis < 3; axis++ )
					if ( b.bounds[0][axis] <= b.bounds[1][axis] )
						bounded &= surfaceBounds[1][axis] >= b.bounds[0][axis] - 2.0f &&
							surfaceBounds[0][axis] <= b.bounds[1][axis] + 2.0f;
				if ( !bounded )
					continue;
				bool inside = true;
				for (int v = 0; v < bsp->numVerts && inside; v++)
					inside = R_WaterInsideBrush(b, bsp->verts[v].xyz, 2.0f) != qfalse;
				if (!inside)
					continue;
				int commonSide = -1;
				// A corner vertex lies on several planes. Try all of them rather
				// than choosing the first plane, which may be a vertical side.
				for (int side = 0; side < b.numPlanes && commonSide < 0; side++)
				{
					const waterPlane_t& plane = s_water.planes[b.firstPlane + side];
					bool coplanar = true;
					for (int v = 0; v < bsp->numVerts && coplanar; v++)
						coplanar = fabsf(DotProduct(plane.normal, bsp->verts[v].xyz) - plane.dist) < 1.5f;
					if (coplanar)
						commonSide = side;
				}
				if (commonSide >= 0)
				{
					r.brush = (int)bi;
					VectorCopy(s_water.planes[b.firstPlane + commonSide].normal, r.brushSide);
					break;
				}
			}
		}
		r.orient = R_WaterOrientation(r.normal);
		s_water.surfaces.push_back(r);
	}

	// Indexed aggregation: linear in the number of surfaces, including custom overrides.
	std::unordered_map<shader_t *, size_t> shaderIndexes;
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		waterShaderRecord_t *s = nullptr;
		const auto found = shaderIndexes.find(r.shader);
		if ( found != shaderIndexes.end() )
			s = &s_water.shaders[found->second];
		if ( !s )
		{
			waterShaderRecord_t e = {};
			e.shader = r.shader;
			e.bspShader = r.bspShader;
			e.reason = WREASON_NONE;
			shaderIndexes[r.shader] = s_water.shaders.size();
			s_water.shaders.push_back(e);
			s = &s_water.shaders.back();
		}
		s->contents |= r.contents;
		s->surfaces++;
		s->orientCount[r.orient]++;
		s->area += r.area;
		if ( r.orient == WORIENT_UP )
			s->upArea += r.area;
		else if ( r.orient == WORIENT_DOWN )
			s->downArea += r.area;
		if ( r.brush >= 0 )
			s->linked++;
	}

	R_WaterDecideShaders();
}

static bool R_WaterBoundsTouch( const vec3_t a[2], const vec3_t b[2], float epsilon )
{
	for ( int k = 0; k < 3; k++ )
		if ( a[1][k] + epsilon < b[0][k] || b[1][k] + epsilon < a[0][k] )
			return false;
	return true;
}

static void R_WaterBodyMotion( waterBody_t& body )
{
	for ( const std::string& name : body.shaders )
	{
		const shader_t *shader = nullptr;
		for ( const waterSurfaceRecord_t& s : s_water.surfaces )
			if ( name == s.shader->name &&
				(R_WaterLiquidClass(s.contents) >= 0 || (s.brush >= 0 && s.brushSide[2] > .7f)) )
			{ shader = s.shader; break; }
		if ( !shader ) continue;
		for ( int stage = 0; stage < MAX_SHADER_STAGES && shader->stages[stage]; stage++ )
		{
			const textureBundle_t& bundle = shader->stages[stage]->bundle[0];
			for ( int t = 0; t < bundle.numTexMods; t++ )
			{
				const texModInfo_t& mod = bundle.texMods[t];
				if ( mod.type == TMOD_SCROLL )
					body.legacy.scrolls.push_back({ stage, mod.scroll[0], mod.scroll[1] });
				else if ( mod.type == TMOD_TURBULENT )
				{
					body.legacy.turb.push_back({ stage, mod.wave.base, mod.wave.amplitude, mod.wave.phase, mod.wave.frequency });
					body.legacy.turbAmplitude = Q_max(body.legacy.turbAmplitude, fabsf(mod.wave.amplitude));
					body.legacy.turbFrequency = Q_max(body.legacy.turbFrequency, fabsf(mod.wave.frequency));
				}
				else if ( mod.type == TMOD_STRETCH )
					body.legacy.stretch.push_back({ stage, mod.wave.base, mod.wave.amplitude, mod.wave.phase, mod.wave.frequency });
			}
		}
		for ( int d = 0; d < shader->numDeforms; d++ )
			if ( shader->deforms[d].deformation == DEFORM_WAVE )
			{
				body.legacy.deformAmplitude = Q_max(body.legacy.deformAmplitude, fabsf(shader->deforms[d].deformationWave.amplitude));
				body.legacy.deformFrequency = Q_max(body.legacy.deformFrequency, fabsf(shader->deforms[d].deformationWave.frequency));
			}
	}
	float weight = 0, sx = 0, sy = 0;
	for ( const legacyWaterMotion_t::scroll_t& scroll : body.legacy.scrolls )
	{
		const float len = sqrtf(scroll.x * scroll.x + scroll.y * scroll.y);
		if ( len > 0 ) { sx += scroll.x; sy += scroll.y; weight += len; }
	}
	if ( weight > 0 )
	{
		body.legacy.weightedScroll[0] = sx / weight;
		body.legacy.weightedScroll[1] = sy / weight;
		body.legacy.disagreement = 1.0f - Q_min(1.0f, sqrtf(sx * sx + sy * sy) / weight);
	}
}

static void R_WaterResolveBody( waterBody_t& body )
{
	body.dynamics = 0;
	body.decision = "fallback";
	if ( body.liquidClass == LIQUID_LAVA )
		body.dynamics = 9, body.decision = "auto: lava contents";
	else if ( body.liquidClass == LIQUID_SLIME || body.opticsClass == LIQUID_SLIME )
		body.dynamics = 6, body.decision = "auto: slime contents/optics";
	else if ( body.shaders.count("textures/common/water2_still") )
		body.dynamics = 1, body.decision = "stock: still shader";
	else if ( body.shaders.count("textures/h_evil/wfall") && body.planeNormal[2] < .7f )
		body.dynamics = 7, body.decision = "stock: waterfall shader and orientation";
	// Scroll lives in texture coordinates. It is evidence, not a world-space flow vector.
	for ( const waterBodyRule_t& rule : s_water.bodyRules )
	{
		if ( rule.id >= 0 && rule.id != body.id ) continue;
		if ( !rule.shader.empty() )
		{
			bool matched = false;
			for ( const std::string& shader : body.shaders )
				if ( R_WaterPatternMatches(rule.shader, shader.c_str()) ) matched = true;
			if ( !matched ) continue;
		}
		if ( rule.hasPoint )
		{
			bool inside = true;
			for ( int k = 0; k < 3; k++ )
				inside &= rule.point[k] >= body.bounds[0][k] && rule.point[k] <= body.bounds[1][k];
			if ( !inside ) continue;
		}
		if ( rule.hasBounds && !R_WaterBoundsTouch(body.bounds, rule.bounds, 0) ) continue;
		body.dynamics = rule.dynamics;
		body.decision = "explicit env.json";
		body.waveMultiplier = rule.wave;
		body.foamMultiplier = rule.foam;
		body.interactionMultiplier = rule.interaction;
		if ( rule.hasFlow ) { body.flow[0] = rule.flow[0]; body.flow[1] = rule.flow[1]; }
	}
}

static void R_WaterBuildBodies( void )
{
	const int start = ri.Milliseconds();
	s_water.bodies.clear();
	// Candidate records are in BSP surface order. Only physically touching, similarly
	// oriented surfaces of the same static model and liquid class can share a body.
	std::vector<int> candidates;
	for ( int i = 0; i < (int)s_water.surfaces.size(); i++ )
		if ( R_WaterLiquidClass(s_water.surfaces[i].contents) >= 0 ||
			(s_water.surfaces[i].brush >= 0 && s_water.surfaces[i].brushSide[2] > .7f &&
			 s_water.surfaces[i].orient == WORIENT_UP) )
			candidates.push_back(i);
	const int count = (int)candidates.size();
	std::vector<int> parent(count);
	for ( int i = 0; i < count; i++ ) parent[i] = i;
	auto root = [&]( int n ) { while ( parent[n] != n ) n = parent[n]; return n; };
	for ( int i = 0; i < count; i++ )
	{
		const waterSurfaceRecord_t& a = s_water.surfaces[candidates[i]];
		for ( int j = i + 1; j < count; j++ )
		{
			const waterSurfaceRecord_t& b = s_water.surfaces[candidates[j]];
			if ( a.model != b.model || a.model < 0 ||
				(a.brush >= 0 ? s_water.brushes[a.brush].liquidClass : R_WaterLiquidClass(a.contents)) !=
				(b.brush >= 0 ? s_water.brushes[b.brush].liquidClass : R_WaterLiquidClass(b.contents)) ) continue;
			if ( a.brush >= 0 && b.brush >= 0 &&
				(a.brush == b.brush || R_WaterBoundsTouch(s_water.brushes[a.brush].bounds, s_water.brushes[b.brush].bounds, 0.5f)) )
			{
				parent[root(j)] = root(i);
				continue;
			}
			if ( a.orient != b.orient ) continue;
			if ( !R_WaterBoundsTouch(a.bounds, b.bounds, 2.0f) ) continue;
			if ( DotProduct(a.normal, b.normal) < .94f ) continue;
			if ( fabsf(DotProduct(a.normal, b.bounds[0]) - DotProduct(a.normal, a.bounds[0])) > 4.0f ) continue;
			parent[root(j)] = root(i);
		}
	}
	std::unordered_map<int, int> roots;
	for ( int i = 0; i < count; i++ )
	{
		const waterSurfaceRecord_t& s = s_water.surfaces[candidates[i]];
		const int cls = s.brush >= 0 ? s_water.brushes[s.brush].liquidClass : R_WaterLiquidClass(s.contents);
		if ( cls < 0 ) continue;
		const int key = root(i);
		if ( !roots.count(key) )
		{
			waterBody_t body = {};
			body.id = (int)s_water.bodies.size() + 1;
			body.model = s.model;
			body.liquidClass = cls;
			body.opticsClass = cls == LIQUID_LAVA ? LIQUID_LAVA : s.liquidClass;
			body.waveMultiplier = body.foamMultiplier = body.interactionMultiplier = 1;
			ClearBounds(body.bounds[0], body.bounds[1]);
			VectorCopy(s.normal, body.planeNormal);
			body.planeDist = DotProduct(s.normal, s.bounds[0]);
			roots[key] = (int)s_water.bodies.size();
			s_water.bodies.push_back(body);
		}
		waterBody_t& body = s_water.bodies[roots[key]];
		body.surfaces.push_back(s.surfaceNum);
		body.shaders.insert(s.shader->name);
		body.area += s.area;
		AddPointToBounds(s.bounds[0], body.bounds[0], body.bounds[1]);
		AddPointToBounds(s.bounds[1], body.bounds[0], body.bounds[1]);
		if ( s.brush >= 0 && std::find(body.brushes.begin(), body.brushes.end(), s.brush) == body.brushes.end() )
			body.brushes.push_back(s.brush);
	}
	for ( waterBody_t& body : s_water.bodies )
	{
		float depthSum = 0;
		for ( int bi : body.brushes )
		{
			const waterBrushRecord_t& brush = s_water.brushes[bi];
			const float depth = Q_max(0.0f, brush.bounds[1][2] - brush.bounds[0][2]);
			depthSum += depth;
			body.depthDeepest = Q_max(body.depthDeepest, depth);
		}
		body.depthAverage = body.brushes.empty() ? 0 : depthSum / body.brushes.size();
		R_WaterBodyMotion(body);
		R_WaterResolveBody(body);
	}
	s_water.bodyMsec = (float)(ri.Milliseconds() - start);
}

static void R_WaterRefreshBodies( void )
{
	if ( s_water.bodies.empty() ) return;
	for ( waterBody_t& body : s_water.bodies )
	{
		if ( body.liquidClass != LIQUID_LAVA && !body.surfaces.empty() )
		{
			for ( const waterSurfaceRecord_t& surface : s_water.surfaces )
				if ( surface.surfaceNum == body.surfaces[0] )
				{
					body.opticsClass = surface.liquidClass;
					break;
				}
		}
		R_WaterResolveBody(body);
	}
}

static bool R_WaterJsonVec( const char *object, const char *end, const char *name, float *out, int count )
{
	const char *value = JSON_ObjectGetNamedValue(object, end, name);
	if ( !value || JSON_ValueGetType(value, end) != JSONTYPE_ARRAY ) return false;
	const char *items[3] = {};
	if ( JSON_ArrayGetIndex(value, end, items, count) < count ) return false;
	for ( int i = 0; i < count; i++ ) out[i] = JSON_ValueGetFloat(items[i], end);
	return true;
}

void R_WaterBodiesLoadJson( world_t *world, const char *json, const char *end, const char *filename )
{
	if ( world != s_water.world ) return;
	s_water.bodyRules.clear();
	const char *array = JSON_ObjectGetNamedValue(json, end, "WaterBodies");
	if ( !array ) return;
	if ( JSON_ValueGetType(array, end) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: WaterBodies is not an array\n", filename);
		return;
	}
	const int count = Q_min((int)JSON_ArrayGetIndex(array, end, NULL, 0), 256);
	for ( int i = 0; i < count; i++ )
	{
		const char *entry = JSON_ArrayGetValue(array, end, i);
		if ( !entry || JSON_ValueGetType(entry, end) != JSONTYPE_OBJECT ) continue;
		waterBodyRule_t rule = {};
		rule.id = -1;
		rule.wave = rule.foam = rule.interaction = 1;
		const char *selector = JSON_ObjectGetNamedValue(entry, end, "Selector");
		if ( !selector || JSON_ValueGetType(selector, end) != JSONTYPE_OBJECT ) selector = entry;
		const char *value = JSON_ObjectGetNamedValue(selector, end, "Shader");
		char shader[MAX_QPATH] = "";
		if ( value ) JSON_ValueGetString(value, end, shader, sizeof(shader));
		value = JSON_ObjectGetNamedValue(selector, end, "ShaderPrefix");
		if ( value && !shader[0] )
		{
			JSON_ValueGetString(value, end, shader, sizeof(shader) - 1);
			Q_strcat(shader, sizeof(shader), "*");
		}
		Q_strlwr(shader);
		rule.shader = shader;
		value = JSON_ObjectGetNamedValue(selector, end, "BodyId");
		if ( value ) rule.id = (int)JSON_ValueGetFloat(value, end);
		rule.hasPoint = R_WaterJsonVec(selector, end, "Point", rule.point, 3) ||
			R_WaterJsonVec(selector, end, "Origin", rule.point, 3);
		const char *bounds = JSON_ObjectGetNamedValue(selector, end, "Bounds");
		if ( bounds && JSON_ValueGetType(bounds, end) == JSONTYPE_OBJECT )
			rule.hasBounds = R_WaterJsonVec(bounds, end, "Mins", rule.bounds[0], 3) &&
				R_WaterJsonVec(bounds, end, "Maxs", rule.bounds[1], 3);
		char profile[32] = "";
		value = JSON_ObjectGetNamedValue(entry, end, "DynamicsProfile");
		if ( value ) JSON_ValueGetString(value, end, profile, sizeof(profile));
		rule.dynamics = -1;
		for ( int d = 0; d < (int)(sizeof(s_dynamics) / sizeof(s_dynamics[0])); d++ )
			if ( !Q_stricmp(profile, s_dynamics[d].name) ) rule.dynamics = d;
		if ( rule.dynamics < 0 || (rule.shader.empty() && rule.id < 0 && !rule.hasPoint && !rule.hasBounds) )
		{
			ri.Printf(PRINT_WARNING, "%s: WaterBodies[%d] needs a selector and valid DynamicsProfile\n", filename, i);
			continue;
		}
		rule.hasFlow = R_WaterJsonVec(entry, end, "Flow", rule.flow, 2);
		value = JSON_ObjectGetNamedValue(entry, end, "WaveMultiplier");
		if ( value ) rule.wave = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "FoamMultiplier");
		if ( value ) rule.foam = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "InteractionMultiplier");
		if ( value ) rule.interaction = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		s_water.bodyRules.push_back(rule);
	}
	for ( waterBody_t& body : s_water.bodies ) R_WaterResolveBody(body);
	ri.Printf(PRINT_DEVELOPER, "%s: %d water dynamics rule%s\n", filename,
		(int)s_water.bodyRules.size(), s_water.bodyRules.size() == 1 ? "" : "s");
}

/*
============================================================

Commands

============================================================
*/

void R_WaterBodies_f( void )
{
	if ( !s_water.world || s_water.world != tr.world )
	{
		ri.Printf(PRINT_ALL, "r_waterBodies: no map\n");
		return;
	}
	const char *arg = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
	size_t memoryBytes = s_water.bodies.capacity() * sizeof(waterBody_t) +
		s_water.bodyRules.capacity() * sizeof(waterBodyRule_t);
	for ( const waterBody_t& b : s_water.bodies )
		memoryBytes += (b.surfaces.capacity() + b.brushes.capacity()) * sizeof(int) +
			b.legacy.scrolls.capacity() * sizeof(legacyWaterMotion_t::scroll_t) +
			(b.legacy.turb.capacity() + b.legacy.stretch.capacity()) * sizeof(legacyWaterMotion_t::wave_t) +
			b.shaders.size() * (sizeof(std::string) + 48);
	if ( !Q_stricmp(arg, "draw") )
	{
		s_water.bodyDebug = s_water.bodyDebug ? qfalse : qtrue;
		ri.Printf(PRINT_ALL, "r_waterBodies draw %s\n", s_water.bodyDebug ? "on" : "off");
	}
	else if ( !Q_stricmp(arg, "dump") )
	{
		// JSON Lines: one complete record per print, so log timestamps can be
		// stripped line by line and large custom maps never truncate one array.
		ri.Printf(PRINT_ALL, "{\"type\":\"waterBodies\",\"map\":\"%s\",\"count\":%d,\"bodyMsec\":%.1f,\"memoryBytesEstimate\":%u}\n",
			tr.world->baseName, (int)s_water.bodies.size(), s_water.bodyMsec, (unsigned)memoryBytes);
		for ( const waterBody_t& b : s_water.bodies )
		{
			ri.Printf(PRINT_ALL, "{\"type\":\"body\",\"id\":%d,\"model\":%d,\"bounds\":[[%g,%g,%g],[%g,%g,%g]],\"area\":%g,\"depthAverage\":%g,\"depthDeepest\":%g,\"class\":\"%s\",\"opticsProfile\":\"%s\",\"dynamicsProfile\":\"%s\",\"source\":\"%s\",\"plane\":[%g,%g,%g,%g],\"flow\":[%g,%g],\"multipliers\":[%g,%g,%g],\"weightedScroll\":[%g,%g],\"scrollDisagreement\":%g,\"deformAmplitude\":%g,\"deformFrequency\":%g}\n",
				b.id, b.model, b.bounds[0][0], b.bounds[0][1], b.bounds[0][2],
				b.bounds[1][0], b.bounds[1][1], b.bounds[1][2], b.area, b.depthAverage, b.depthDeepest,
				s_liquidNames[b.liquidClass], s_liquidNames[b.opticsClass], s_dynamics[b.dynamics].name, b.decision,
				b.planeNormal[0], b.planeNormal[1], b.planeNormal[2], b.planeDist,
				b.flow[0], b.flow[1], b.waveMultiplier, b.foamMultiplier, b.interactionMultiplier,
				b.legacy.weightedScroll[0], b.legacy.weightedScroll[1], b.legacy.disagreement,
				b.legacy.deformAmplitude, b.legacy.deformFrequency);
			for ( int id : b.surfaces ) ri.Printf(PRINT_ALL, "{\"type\":\"surface\",\"body\":%d,\"id\":%d}\n", b.id, id);
			for ( int id : b.brushes ) ri.Printf(PRINT_ALL, "{\"type\":\"brush\",\"body\":%d,\"id\":%d}\n", b.id, s_water.brushes[id].brushNum);
			for ( const std::string& shader : b.shaders ) ri.Printf(PRINT_ALL, "{\"type\":\"shader\",\"body\":%d,\"name\":\"%s\"}\n", b.id, shader.c_str());
			for ( const legacyWaterMotion_t::scroll_t& s : b.legacy.scrolls )
				ri.Printf(PRINT_ALL, "{\"type\":\"scroll\",\"body\":%d,\"stage\":%d,\"vector\":[%g,%g]}\n", b.id, s.stage, s.x, s.y);
			for ( const legacyWaterMotion_t::wave_t& w : b.legacy.turb )
				ri.Printf(PRINT_ALL, "{\"type\":\"turb\",\"body\":%d,\"stage\":%d,\"wave\":[%g,%g,%g,%g]}\n", b.id, w.stage, w.base, w.amplitude, w.phase, w.frequency);
			for ( const legacyWaterMotion_t::wave_t& w : b.legacy.stretch )
				ri.Printf(PRINT_ALL, "{\"type\":\"stretch\",\"body\":%d,\"stage\":%d,\"wave\":[%g,%g,%g,%g]}\n", b.id, w.stage, w.base, w.amplitude, w.phase, w.frequency);
		}
		return;
	}
	else if ( arg[0] && Q_stricmp(arg, "draw") )
	{
		ri.Printf(PRINT_ALL, "usage: r_waterBodies [dump | draw]\n");
		return;
	}
	ri.Printf(PRINT_ALL, "%s: %d water bodies, %.1f ms, approximately %u bytes, %d rules, draw %s\n", tr.world->baseName,
		(int)s_water.bodies.size(), s_water.bodyMsec, (unsigned)memoryBytes, (int)s_water.bodyRules.size(), s_water.bodyDebug ? "on" : "off");
	for ( const waterBody_t& b : s_water.bodies )
	{
		const unsigned h = (unsigned)b.id * 2654435761u;
		ri.Printf(PRINT_ALL, "  #%d color #%02x%02x%02x model %d (%g %g %g)-(%g %g %g) area %.0f depth %.0f/%.0f %s optics %s dynamics %d:%s [%s] surfaces %d brushes %d flow (%g %g)\n",
			b.id, 64 + (h & 127), 64 + ((h >> 8) & 127), 64 + ((h >> 16) & 127),
			b.model, b.bounds[0][0], b.bounds[0][1], b.bounds[0][2], b.bounds[1][0], b.bounds[1][1], b.bounds[1][2],
			b.area, b.depthAverage, b.depthDeepest, s_liquidNames[b.liquidClass], s_liquidNames[b.opticsClass],
			b.dynamics, s_dynamics[b.dynamics].name, b.decision, (int)b.surfaces.size(), (int)b.brushes.size(), b.flow[0], b.flow[1]);
		for ( const std::string& shader : b.shaders ) ri.Printf(PRINT_ALL, "    %s\n", shader.c_str());
		ri.Printf(PRINT_ALL, "    legacy scrolls %d weighted (%g %g) disagreement %.2f turb %d %.3f/%.3f stretch %d deform %.3f/%.3f\n",
			(int)b.legacy.scrolls.size(), b.legacy.weightedScroll[0], b.legacy.weightedScroll[1], b.legacy.disagreement,
			(int)b.legacy.turb.size(), b.legacy.turbAmplitude, b.legacy.turbFrequency, (int)b.legacy.stretch.size(),
			b.legacy.deformAmplitude, b.legacy.deformFrequency);
		for ( const legacyWaterMotion_t::scroll_t& s : b.legacy.scrolls )
			ri.Printf(PRINT_ALL, "      stage %d scroll (%g %g)\n", s.stage, s.x, s.y);
		for ( const legacyWaterMotion_t::wave_t& w : b.legacy.turb )
			ri.Printf(PRINT_ALL, "      stage %d turb (%g %g %g %g)\n", w.stage, w.base, w.amplitude, w.phase, w.frequency);
		for ( const legacyWaterMotion_t::wave_t& w : b.legacy.stretch )
			ri.Printf(PRINT_ALL, "      stage %d stretch (%g %g %g %g)\n", w.stage, w.base, w.amplitude, w.phase, w.frequency);
	}
}

qhandle_t RE_RegisterShaderFromImage( const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage );

static void R_WaterBodySegment( const refdef_t *fd, qhandle_t shader, const vec3_t a, const vec3_t b, const byte *color )
{
	vec3_t direction, eye, side;
	VectorSubtract(b, a, direction);
	VectorSubtract(a, fd->vieworg, eye);
	CrossProduct(direction, eye, side);
	if ( VectorNormalize(side) < 1e-6f ) return;
	VectorScale(side, 1.0f + .0015f * VectorLength(eye), side);
	polyVert_t verts[4] = {};
	VectorSubtract(a, side, verts[0].xyz);
	VectorAdd(a, side, verts[1].xyz);
	VectorAdd(b, side, verts[2].xyz);
	VectorSubtract(b, side, verts[3].xyz);
	for ( int i = 0; i < 4; i++ )
	{
		verts[i].st[0] = verts[i].st[1] = .5f;
		Com_Memcpy(verts[i].modulate, color, 4);
	}
	RE_AddPolyToScene(shader, 4, verts, 1);
}

static void R_WaterBodyDigit( const refdef_t *fd, qhandle_t shader, const vec3_t origin, int digit, float offset, const byte *color )
{
	static const byte masks[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
	static const float lines[7][4] = {
		{ 0, 2, 1, 2 }, { 1, 2, 1, 1 }, { 1, 1, 1, 0 },
		{ 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { 0, 2, 0, 1 }, { 0, 1, 1, 1 }
	};
	if ( digit < 0 || digit > 9 ) return;
	for ( int segment = 0; segment < 7; segment++ )
	{
		if ( !(masks[digit] & (1 << segment)) ) continue;
		vec3_t a, b;
		VectorMA(origin, (offset + lines[segment][0]) * 12, fd->viewaxis[1], a);
		VectorMA(a, lines[segment][1] * 12, fd->viewaxis[2], a);
		VectorMA(origin, (offset + lines[segment][2]) * 12, fd->viewaxis[1], b);
		VectorMA(b, lines[segment][3] * 12, fd->viewaxis[2], b);
		R_WaterBodySegment(fd, shader, a, b, color);
	}
}

void R_WaterBodiesDebugDraw( const refdef_t *fd )
{
	if ( !s_water.bodyDebug || !tr.world || s_water.world != tr.world || (fd->rdflags & RDF_NOWORLDMODEL) ) return;
	if ( !s_water.bodyDebugShader )
		s_water.bodyDebugShader = RE_RegisterShaderFromImage("*waterBodyDebug", lightmaps2d, stylesDefault, tr.whiteImage, qfalse);
	for ( const waterBody_t& body : s_water.bodies )
	{
		const unsigned h = (unsigned)body.id * 2654435761u;
		const byte color[4] = { (byte)(64 + (h & 127)), (byte)(64 + ((h >> 8) & 127)),
			(byte)(64 + ((h >> 16) & 127)), 255 };
		vec3_t p[8];
		for ( int i = 0; i < 8; i++ )
			VectorSet(p[i], body.bounds[(i & 1) != 0][0], body.bounds[(i & 2) != 0][1], body.bounds[(i & 4) != 0][2]);
		for ( int i = 0; i < 8; i++ )
			for ( int bit = 0; bit < 3; bit++ )
				if ( !(i & (1 << bit)) ) R_WaterBodySegment(fd, s_water.bodyDebugShader, p[i], p[i | (1 << bit)], color);
		vec3_t center, end;
		VectorAdd(body.bounds[0], body.bounds[1], center);
		VectorScale(center, .5f, center);
		if ( fabsf(body.planeNormal[2]) > .5f )
			center[2] = (body.planeDist - body.planeNormal[0] * center[0] - body.planeNormal[1] * center[1]) / body.planeNormal[2];
		VectorMA(center, 32, body.planeNormal, end);
		R_WaterBodySegment(fd, s_water.bodyDebugShader, center, end, color);
		vec3_t label;
		VectorMA(center, 40, body.planeNormal, label);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, (body.id / 100) % 10, 0, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, (body.id / 10) % 10, 1.3f, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, body.id % 10, 2.6f, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, body.dynamics, 4.5f, color);
		vec3_t plane[4];
		for ( int corner = 0; corner < 4; corner++ )
		{
			VectorCopy(center, plane[corner]);
			const int axis = fabsf(body.planeNormal[2]) > .5f ? 2 :
				(fabsf(body.planeNormal[0]) > fabsf(body.planeNormal[1]) ? 0 : 1);
			const int u = (axis + 1) % 3, v = (axis + 2) % 3;
			plane[corner][u] = body.bounds[(corner & 1) != 0][u];
			plane[corner][v] = body.bounds[(corner & 2) != 0][v];
			if ( fabsf(body.planeNormal[axis]) > .01f )
				plane[corner][axis] = (body.planeDist - body.planeNormal[u] * plane[corner][u] -
					body.planeNormal[v] * plane[corner][v]) / body.planeNormal[axis];
		}
		const int outline[4] = { 0, 1, 3, 2 };
		for ( int edge = 0; edge < 4; edge++ )
			R_WaterBodySegment(fd, s_water.bodyDebugShader, plane[outline[edge]], plane[outline[(edge + 1) & 3]], color);
		if ( body.flow[0] || body.flow[1] )
		{
			VectorSet(end, center[0] + body.flow[0] * 64, center[1] + body.flow[1] * 64, center[2]);
			R_WaterBodySegment(fd, s_water.bodyDebugShader, center, end, color);
		}
	}
}

static void R_WaterPrintShaders( void )
{
	ri.Printf(PRINT_ALL, "  candidate shaders (final decisions are per surface; up/down/vertical/sloped, up share, brush links):\n");
	for ( const waterShaderRecord_t& s : s_water.shaders )
	{
		if (!s.modern && !R_WaterNameLike(s.shader->name) && !s.shader->useDistortion &&
			R_WaterLiquidClass(s.contents) < 0 && !s.linked && !R_WaterFindOverride(s.shader->name))
			continue;
		ri.Printf(PRINT_ALL, "  %-6s %-44s %-5s %3d (%d/%d/%d/%d) up %3.0f%% linked %3d contents 0x%08x%s%s  %s\n",
			s.modern ? "WATER" : "legacy", s.shader->name, s_liquidNames[s.liquidClass], s.surfaces,
			s.orientCount[0], s.orientCount[1], s.orientCount[2], s.orientCount[3],
			s.area - s.downArea > 0.0f ? 100.0f * s.upArea / (s.area - s.downArea) : 0.0f, s.linked, (unsigned)s.contents,
			s.shader->useDistortion ? " refractive" : "",
			(s.flags & WATERSURF_FOG_MEDIUM) ? " fog-medium" : "",
			s_reasonNames[s.reason]);
	}
}

/*
=================
R_WaterInfo_f

r_waterInfo [surfaces]: the liquid brushes, the candidate shaders with the
reason of their decision, optionally every candidate surface
=================
*/
void R_WaterInfo_f( void )
{
	if ( !tr.world || s_water.world != tr.world )
	{
		ri.Printf(PRINT_ALL, "r_waterInfo: no map\n");
		return;
	}

	const qboolean listSurfaces = (qboolean)(ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "surfaces"));
	ri.Printf(PRINT_ALL, "Water surfaces of %s (classified in %.1f ms)\n", tr.world->baseName, s_water.classifyMsec);
	ri.Printf(PRINT_ALL, "  r_waterSurface %d (latched %s, resources %s), r_waterSurfaceExperimental %d, overrides %d\n",
		r_waterSurface->integer, r_waterSurface->latchedString ? r_waterSurface->latchedString : "-",
		s_water.resources ? "yes" : "no", r_waterSurfaceExperimental->integer, (int)s_water.overrides.size());
	ri.Printf(PRINT_ALL, "  last frame: %d water draws, view %s, SSR %s, froxel medium %s\n", s_water.lastDraws,
		backEnd.waterSurfaceView ? "yes" : "no", backEnd.waterSurfaceSSR ? "yes" : "no",
		RB_VolumetricLookupReady() ? "yes" : "no");

	int worldBrushes = 0;
	for ( const waterBrushRecord_t& b : s_water.brushes )
		worldBrushes += b.model == 0 ? 1 : 0;
	ri.Printf(PRINT_ALL, "  liquid brushes: %d (world %d, brush models %d)\n", (int)s_water.brushes.size(),
		worldBrushes, (int)s_water.brushes.size() - worldBrushes);
	for ( size_t i = 0; i < s_water.brushes.size(); i++ )
	{
		const waterBrushRecord_t& b = s_water.brushes[i];
		const char *shader = (b.shaderNum >= 0 && b.shaderNum < tr.world->numShaders) ? tr.world->shaders[b.shaderNum].shader : "?";
		ri.Printf(PRINT_ALL, "  %3d brush %5d model %3d %-5s sides %2d (%6.0f %6.0f %6.0f)-(%6.0f %6.0f %6.0f)%s %s\n",
			(int)i, b.brushNum, b.model, s_liquidNames[b.liquidClass], b.numPlanes,
			b.bounds[0][0], b.bounds[0][1], b.bounds[0][2], b.bounds[1][0], b.bounds[1][1], b.bounds[1][2],
			(b.contents & CONTENTS_FOG) ? " fog" : "", shader);
	}

	R_WaterPrintShaders();

	if ( !listSurfaces )
	{
		ri.Printf(PRINT_ALL, "  %d cached surfaces (r_waterInfo surfaces lists water candidates)\n", (int)s_water.surfaces.size());
		return;
	}
	ri.Printf(PRINT_ALL, "  surfaces:\n");
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		if (!r.modern && !R_WaterNameLike(r.shader->name) && !r.refractive &&
			R_WaterLiquidClass(r.contents) < 0 && r.brush < 0 && !R_WaterFindOverride(r.shader->name))
			continue;
		const qboolean modern = r.modern;
		char link[64] = "-";
		if ( r.brush >= 0 )
			Com_sprintf(link, sizeof(link), "brush %d side (%.2f %.2f %.2f)", s_water.brushes[r.brush].brushNum,
				r.brushSide[0], r.brushSide[1], r.brushSide[2]);
		ri.Printf(PRINT_ALL, "  %5d model %3d %-8s %-8s n (%5.2f %5.2f %5.2f) area %8.0f %-6s %s%s%s | %s\n",
			r.surfaceNum, r.model, r.type == MST_PATCH ? "patch" : r.type == MST_TRIANGLE_SOUP ? "trisoup" : "planar",
			s_orientNames[r.orient], r.normal[0], r.normal[1], r.normal[2], r.area,
			modern ? "WATER" : "legacy", r.shader->name,
			(r.contents & CONTENTS_WATER) ? " [CONTENTS_WATER]" : "", r.refractive ? " [refractive]" : "", link);
	}
}

/*
=================
R_WaterOverride_f

r_waterOverride                                 list
r_waterOverride <shader | prefix*> on [water | slime]
r_waterOverride <shader | prefix*> off
r_waterOverride <shader | prefix*> clear
r_waterOverride clear                           remove all

For cfg files (map specific setups): applies to the current map at once and
to every map loaded later.
=================
*/
void R_WaterOverride_f( void )
{
	const int argc = ri.Cmd_Argc();
	if ( argc < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: r_waterOverride <shader | prefix*> on [water | slime] | off | clear;  r_waterOverride clear\n");
		for ( const waterOverride_t& o : s_water.overrides )
			ri.Printf(PRINT_ALL, "  %s %s%s%s\n", o.pattern.c_str(), o.mode ? "on" : "off",
				o.liquidClass >= 0 ? " " : "", o.liquidClass >= 0 ? s_liquidNames[o.liquidClass] : "");
		return;
	}

	char pattern[MAX_QPATH];
	Q_strncpyz(pattern, ri.Cmd_Argv(1), sizeof(pattern));
	Q_strlwr(pattern);
	for ( char *c = pattern; *c; c++ )
	{
		if ( *c == '\\' )
			*c = '/';
	}

	if ( argc == 2 && !Q_stricmp(pattern, "clear") )
	{
		s_water.overrides.clear();
	}
	else if ( argc >= 3 )
	{
		const char *mode = ri.Cmd_Argv(2);
		s_water.overrides.erase(std::remove_if(s_water.overrides.begin(), s_water.overrides.end(),
			[&]( const waterOverride_t& o ) { return o.pattern == pattern; }), s_water.overrides.end());
		if ( !Q_stricmp(mode, "on") || !Q_stricmp(mode, "off") )
		{
			waterOverride_t o;
			o.pattern = pattern;
			o.mode = !Q_stricmp(mode, "on") ? 1 : 0;
			o.liquidClass = -1;
			if ( argc >= 4 )
			{
				if ( !Q_stricmp(ri.Cmd_Argv(3), "slime") )
					o.liquidClass = LIQUID_SLIME;
				else if ( !Q_stricmp(ri.Cmd_Argv(3), "water") )
					o.liquidClass = LIQUID_WATER;
				else
					ri.Printf(PRINT_WARNING, "r_waterOverride: unknown profile \"%s\" (water, slime)\n", ri.Cmd_Argv(3));
			}
			s_water.overrides.push_back(o);
		}
		else if ( Q_stricmp(mode, "clear") )
		{
			ri.Printf(PRINT_WARNING, "r_waterOverride: unknown mode \"%s\" (on, off, clear)\n", mode);
			return;
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "usage: r_waterOverride <shader | prefix*> on [water | slime] | off | clear\n");
		return;
	}

	if ( tr.world && s_water.world == tr.world )
	{
		// Geometry and brush associations are immutable and were cached at load.
		R_WaterDecideShaders();
		ri.Printf(PRINT_ALL, "r_waterOverride: reapplied to %s\n", tr.world->baseName);
	}
}

/*
============================================================

Resources

============================================================
*/

/*
=================
R_WaterBuildWaveSlopes

Tiling wave slopes, size x size texels: a height field of integer wave
vectors (tiles exactly), a broad spectrum with a soft peak, random phases and
a mild directional spread. Stored (dh/dx, dh/dy, (dh/dx)^2, (dh/dy)^2) with
the RMS slope normalized to 1: the box mips average slopes and squared slopes,
so the shader recovers the unresolved slope variance (LEAN mapping).
=================
*/
static void R_WaterBuildWaveSlopes( int size, std::vector<float>& out )
{
	struct wave_t { float kx, ky, amp, phase; };
	std::vector<wave_t> waves;
	unsigned seed = 0x5eed1234u;
	auto rnd = [&]() -> float
	{
		seed = seed * 1664525u + 1013904223u;
		return (float)((seed >> 8) & 0xffffff) / 16777216.0f;
	};
	for ( int ky = -12; ky <= 12; ky++ )
	{
		for ( int kx = -12; kx <= 12; kx++ )
		{
			const float k = sqrtf((float)(kx * kx + ky * ky));
			if ( k < 1.0f || k > 12.5f )
				continue;
			// spectrum: rises to k ~ 3, falls as k^-3 (height); prefers +x, keeps some cross waves
			const float peak = 3.0f;
			float amp = (k < peak) ? k / peak : powf(peak / k, 3.0f);
			const float dir = (float)kx / k;
			amp *= 0.45f + 0.55f * (0.5f + 0.5f * dir) * (0.5f + 0.5f * dir);
			amp *= 0.6f + 0.8f * rnd();
			waves.push_back({ (float)kx, (float)ky, amp, rnd() * 2.0f * (float)M_PI });
		}
	}

	out.assign(size * size * 4, 0.0f);
	double sum2 = 0.0;
	const float twoPi = 2.0f * (float)M_PI;
	for ( int y = 0; y < size; y++ )
	{
		for ( int x = 0; x < size; x++ )
		{
			const float u = (float)x / size, v = (float)y / size;
			float sx = 0.0f, sy = 0.0f;
			for ( const wave_t& w : waves )
			{
				const float c = cosf(twoPi * (w.kx * u + w.ky * v) + w.phase) * w.amp * twoPi;
				sx += c * w.kx;
				sy += c * w.ky;
			}
			float *t = &out[(y * size + x) * 4];
			t[0] = sx;
			t[1] = sy;
			sum2 += sx * sx + sy * sy;
		}
	}
	const float scale = (float)(1.0 / sqrt(MAX(sum2 / (size * size), 1e-12)));
	for ( int i = 0; i < size * size; i++ )
	{
		float *t = &out[i * 4];
		t[0] *= scale;
		t[1] *= scale;
		t[2] = t[0] * t[0];
		t[3] = t[1] * t[1];
	}
}

void R_CreateWaterSurfaceImages( int width, int height, int hdrFormat )
{
	// the GPU programs are kept over a map change (only a vid_restart rebuilds
	// them): keep the decision they were built with
	if ( !tr.textureColorShader[0].program )
		s_water.resources = (qboolean)(r_waterSurface->integer != 0);

	tr.waterSceneImage = NULL;
	tr.waterDepthImage = NULL;
	tr.waterNormalImage = NULL;
	tr.waterGlowImage = NULL;
	tr.waterReflectionDepthImage = NULL;
	for (int i = 0; i < 2; i++)
	{
		tr.waterReflectionImage[i] = nullptr;
		tr.waterReflectionGeomImage[i] = nullptr;
		tr.waterReflectionHitImage[i] = nullptr;
	}
	s_water.history.valid = qfalse;
	if ( !s_water.resources )
		return;

	tr.waterSceneImage = R_CreateImage("*waterScene", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);
	tr.waterDepthImage = R_ScreenCreateImage("*waterDepth", width, height, GL_DEPTH24_STENCIL8, qfalse);

	tr.waterGlowImage = R_ScreenCreateImage("*waterGlow", width, height, hdrFormat, qtrue);
	if (R_SSRResourcesEnabled())
	{
		s_water.reflectionScale = R_SSRTraceScale();
		const int rw = Q_max(1, (int)ceilf(width / s_water.reflectionScale));
		const int rh = Q_max(1, (int)ceilf(height / s_water.reflectionScale));
		for (int i = 0; i < 2; i++)
		{
			tr.waterReflectionImage[i] = R_ScreenCreateImage(va("*waterReflection%d", i), rw, rh, GL_RGBA16F, qfalse);
			tr.waterReflectionGeomImage[i] = R_ScreenCreateImage(va("*waterReflectionGeom%d", i), rw, rh, GL_RGBA32F, qfalse);
			tr.waterReflectionHitImage[i] = R_ScreenCreateImage(va("*waterReflectionHit%d", i), rw, rh, GL_RGBA32F, qfalse);
		}
		tr.waterReflectionDepthImage = R_ScreenCreateImage("*waterReflectionDepth", rw, rh, GL_DEPTH24_STENCIL8, qfalse);
	}
	const int size = 256;
	// Fixed spectrum: retain the CPU result over map loads and vid_restart.
	static std::vector<float> slopes;
	if ( slopes.empty() )
		R_WaterBuildWaveSlopes(size, slopes);
	tr.waterNormalImage = R_CreateImage("*waterWaves", NULL, size, size, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_MUTABLE, GL_RGBA16F);
	GL_Bind(tr.waterNormalImage);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, slopes.data());
	qglGenerateMipmap(GL_TEXTURE_2D);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 8);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void R_CreateWaterSurfaceFBOs( void )
{
	tr.waterCopyFbo = NULL;
	tr.waterReflectionFbo[0] = tr.waterReflectionFbo[1] = nullptr;
	if ( !s_water.resources || !tr.waterSceneImage || !tr.waterDepthImage )
		return;

	tr.waterCopyFbo = FBO_Create("_waterCopy", tr.waterSceneImage->width, tr.waterSceneImage->height);
	FBO_Bind(tr.waterCopyFbo);
	FBO_AttachTextureImage(tr.waterSceneImage, 0);
	FBO_AttachTextureImage(tr.waterGlowImage, 1);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tr.waterDepthImage->texnum, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(tr.waterCopyFbo);
	for (int i = 0; i < 2 && tr.waterReflectionImage[i]; i++)
	{
		image_t *image = tr.waterReflectionImage[i];
		tr.waterReflectionFbo[i] = FBO_Create(va("_waterReflection%d", i), image->width, image->height);
		FBO_Bind(tr.waterReflectionFbo[i]);
		FBO_AttachTextureImage(image, 0);
		FBO_AttachTextureImage(tr.waterReflectionGeomImage[i], 1);
		FBO_AttachTextureImage(tr.waterReflectionHitImage[i], 2);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
			tr.waterReflectionDepthImage->texnum, 0);
		const GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
		qglDrawBuffers(3, buffers);
		R_CheckFBO(tr.waterReflectionFbo[i]);
	}
}

void R_WaterSurfaceShutdown( void )
{
	// images and FBOs belong to the renderer lists; the resource decision
	// follows the programs (R_CreateWaterSurfaceImages)
	s_water.world = NULL;
	s_water.mergedViewSurfaces.clear();
	s_water.history.valid = qfalse;
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();
	s_water.bodies.clear();
	s_water.bodyRules.clear();
	s_water.bodyDebugShader = 0;
}

/*
============================================================

View and draws

============================================================
*/

/*
=================
RB_WaterSurfaceBeginView

Called by RB_BeginDrawingView after the screen-space passes decided theirs:
the main view of the world, rendered into renderFbo.
=================
*/
void RB_WaterSurfaceBeginView( void )
{
	backEnd.waterSurfaceView = qfalse;
	backEnd.waterSurfaceSSR = qfalse;
	backEnd.waterItemTag = qfalse;
	backEnd.waterLegacyClipTag = qfalse;
	s_water.reflectionReady = qfalse;
	s_water.viewWaterReady = qfalse;

	if ( s_water.statsFrame != backEndData->realFrameNumber )
	{
		s_water.lastDraws = s_water.drawsThisFrame;
		s_water.drawsThisFrame = 0;
		s_water.statsFrame = backEndData->realFrameNumber;
	}

	// the experimental name rule may be toggled at run time
	if ( r_waterSurfaceExperimental->modified )
	{
		r_waterSurfaceExperimental->modified = qfalse;
		if ( tr.world && s_water.world == tr.world )
			R_WaterDecideShaders();
	}

	if ( !s_water.resources || !r_waterSurface->integer || !tr.waterCopyFbo || !backEnd.waterInterfacesVisible )
		return;
	const viewParms_t& viewParms = backEnd.viewParms;
	if ( viewParms.viewParmType != VPT_MAIN || viewParms.isPortal || viewParms.isSkyPortal )
		return;
	if ( viewParms.flags & VPF_DEPTHSHADOW )
		return;
	if ( viewParms.targetFbo != NULL && viewParms.targetFbo != tr.renderFbo )
		return;
	if ( glState.currentFBO != tr.renderFbo || backEnd.framePostProcessed )
		return;
	if ( !tr.world || (backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE)) )
		return;

	backEnd.waterSurfaceView = qtrue;
	// the SSR inputs of the view (depth pyramid, opaque color pyramid) are
	// built by RB_RenderSSR before the water slot
	backEnd.waterSurfaceSSR = (qboolean)(backEnd.ssrView && R_SSRResourcesEnabled() &&
		tr.screenHiZImage && tr.ssrColorImage && r_waterSurfaceSSR->value > 0.0f);
}

qboolean RB_WaterSurfaceDraws( const shader_t *shader )
{
	if ( !backEnd.waterSurfaceView || !shader || !(tess.waterKey & WATERKEY_INTERFACE) )
		return qfalse;
	if ( backEnd.depthFill || backEnd.refractionFill || backEnd.projection2D )
		return qfalse;
	if ( backEnd.currentEntity && (backEnd.currentEntity->e.renderfx & RF_DISTORTION) )
		return qfalse;
	if ( !tr.waterSurfaceShader[0].program )
		return qfalse;
	return qtrue;
}

shaderProgram_t *RB_WaterSurfaceProgram( const shader_t *shader )
{
	int index = 0;
	if ( shader->numDeforms && !ShaderRequiresCPUDeforms(shader) )
		index |= WATERDEF_USE_DEFORM_VERTEXES;
	if ( r_waterSnell->integer )
		index |= WATERDEF_USE_SNELL;
	// the SSR of the view walks the closest depth mips: so does the water
	if ( backEnd.waterSurfaceSSR && RB_SSRDepthLevels() > 1 && tr.waterSurfaceShader[index | WATERDEF_USE_HIZ].program )
		index |= WATERDEF_USE_HIZ;
	s_water.drawsThisFrame++;
	return &tr.waterSurfaceShader[index];
}

/*
=================
RB_WaterSurfaceSetupDraw

The water specific uniforms and textures of a draw (the uniform blocks, the
vertex data and the stage's tcMod are set by the stage iterator).
=================
*/
static void RB_WaterSurfaceCacheView( void )
{
	if (s_water.viewWaterReady)
		return;
	s_water.viewWaterReady = qtrue;
	const viewParms_t& v = backEnd.viewParms;
	vec4_t *water = s_water.viewWater;
	Com_Memset(water, 0, sizeof(s_water.viewWater));
	VectorSet4(water[0], Com_Clamp(1.0f, 2.0f, r_waterSurfaceIOR->value),
		Com_Clamp(0.0f, 1.0f, r_waterSurfaceRoughness->value),
		Com_Clamp(0.0f, 4.0f, r_waterSurfaceNormal->value), Com_Clamp(0.0f, 4.0f, r_waterSurfaceRefraction->value));
	VectorSet4(water[1], Com_Clamp(0.0f, 4.0f, r_waterSurfaceReflection->value),
		backEnd.waterSurfaceSSR ? Com_Clamp(0.0f, 1.0f, r_waterSurfaceSSR->value) : 0.0f,
		1.0f, Com_Clamp(0.0f, 16.0f, r_waterSurfaceDepthScale->value));
	for (int c = 0; c < LIQUID_CLASSES; c++)
		R_LiquidsMaterial(c, s_water.extinction[c], s_water.albedo[c]);
	const float *proj = v.projectionMatrix;
	VectorSet4(water[4], proj[0], proj[5], proj[8], proj[9]);
	VectorSet4(water[5], proj[14], proj[10], 0.0f, tr.linearLight ? 1.0f : 0.0f);
	const int debug = r_waterSurfaceDebug->integer;
	VectorSet4(water[6], debug == 9 ? 0.0f : (float)debug,
		debug == 9 ? v.viewportX + Com_Clamp(0.0f, 1.0f, r_waterSurfaceSplit->value) * v.viewportWidth : -1.0f,
		0.0f, 8192.0f);
	vec3_t right, up;
	VectorScale(v.ori.axis[1], -1.0f, right);
	VectorCopy(v.ori.axis[2], up);
	VectorNormalize(right);
	VectorNormalize(up);
	const float farZ = R_VolumetricFarZ();
	VectorSet4(water[7], right[0], right[1], right[2], farZ * 0.8f);
	VectorSet4(water[8], up[0], up[1], up[2], 1.0f / MAX(farZ * 0.2f, 1.0f));
	vec3_t env;
	VectorCopy(backEnd.refdef.sunAmbCol, env);
	if (VectorLength(env) < 0.02f)
		VectorSet(env, 0.08f, 0.09f, 0.1f);
	const bool sun = r_sunlightMode->integer && (v.flags & VPF_USESUNLIGHT) && tr.sunShadowArrayImage;
	VectorSet4(water[9], env[0], env[1], env[2], sun ? 1.0f : 0.0f);
	VectorSet4(water[10], 1.0f, 1.0f / 192.0f, 0.0f,
		r_waterSnell->integer ? (float)r_waterSnellDebug->integer : 0.0f);
	const double t = backEnd.refdef.floatTime;
	auto wrap = [](double value) -> float { return (float)(value - floor(value)); };
	VectorSet4(water[11], wrap(0.8 * 0.020 * t), wrap(0.6 * 0.020 * t),
		wrap(0.28 * 0.034 * t + 0.37), wrap(0.96 * 0.034 * t + 0.71));
	VectorSet4(water[12], (float)v.viewportX / tr.waterSceneImage->width,
		(float)v.viewportY / tr.waterSceneImage->height,
		(float)v.viewportWidth / tr.waterSceneImage->width,
		(float)v.viewportHeight / tr.waterSceneImage->height);
	s_water.viewFlags = (sun ? 2 : 0) | (RB_VolumetricLookupReady() ? 4 : 0) |
		(r_waterSurfaceDepthReject->integer ? 8 : 0) | (backEnd.waterSurfaceSSR ? 16 : 0) |
		(tr.envBrdfImage ? 512 : 0);
	if (r_waterSnell->integer && r_waterSnellDebug->integer == 1 && R_LiquidPointClass(v.ori.origin) >= 0)
		s_water.viewFlags |= 1024;
	s_water.fallbackCubemap = r_cubeMapping->integer && !(v.flags & VPF_NOCUBEMAPS) ? R_CubemapForPoint(v.ori.origin) : 0;
	if (backEnd.waterSurfaceSSR)
	{
		RB_ScreenGetViewInfo(s_water.viewInfo);
		int steps, refine;
		qboolean hiZ;
		RB_SSRTraceParams(&steps, &refine, &hiZ);
		VectorSet4(s_water.ssrSettings[0], (float)Com_Clampi(1, 256, steps), (float)Com_Clampi(0, 16, refine),
			r_ssrMaxDistance->value, r_ssrThickness->value);
		VectorSet4(s_water.ssrSettings[1], MAX(r_ssrMaxRoughness->value, 0.05f), r_ssrEdgeFade->value, SSR_COLOR_MIPS - 1, 0.0f);
		VectorSet4(s_water.ssrSettings[2], SCREEN_HIZ_MIPS - 1, 0.0f, r_znear->value, (float)Com_Clampi(8, 1024, steps * 3));
	}
}

void RB_WaterSurfaceSetupDraw( const shaderCommands_t *input, UniformDataWriter& uniforms,
	SamplerBindingsWriter& samplers )
{
	RB_WaterSurfaceCacheView();
	vec4_t water[WATER_UNIFORM_VEC4S];
	Com_Memcpy(water, s_water.viewWater, sizeof(water));
	const uint32_t key = input->waterKey;
	const int liquidClass = Com_Clampi(LIQUID_WATER, LIQUID_SLIME, (key >> 4) & 3);
	const float *extinction = s_water.extinction[liquidClass];
	const bool froxel = (s_water.viewFlags & 4) != 0;
	const bool froxelLiquid = froxel && backEnd.currentEntity == &tr.worldEntity &&
		(key & WATERKEY_WORLD_BRUSH) && (R_LiquidClassMask() & (key >> 16)) != 0;
	VectorSet4(water[2], extinction[0] * extinction[3], extinction[1] * extinction[3], extinction[2] * extinction[3],
		froxelLiquid ? 1.0f : 0.0f);
	VectorCopy4(s_water.albedo[liquidClass], water[3]);
	int flags = s_water.viewFlags;
	int cubemapIndex = input->cubemapIndex;
	if (cubemapIndex <= 0 || cubemapIndex > tr.numCubemaps || !tr.cubemaps[cubemapIndex - 1].image)
		cubemapIndex = s_water.fallbackCubemap;
	const bool cubemap = r_cubeMapping->integer && !(backEnd.viewParms.flags & VPF_NOCUBEMAPS) &&
		cubemapIndex > 0 && cubemapIndex <= tr.numCubemaps && tr.cubemaps[cubemapIndex - 1].image;
	if (cubemap) flags |= 1;
	if ((key >> 8) & WATERSURF_FOG_MEDIUM) flags |= 32;
	if (liquidClass == LIQUID_SLIME) flags |= 64;
	if ((key >> 8) & WATERSURF_OVERRIDE) flags |= 128;
	if ((key >> 8) & WATERSURF_EXPERIMENTAL) flags |= 256;
	water[6][2] = (float)flags;
	uniforms.SetUniformVec4(UNIFORM_WATER, water[0], WATER_UNIFORM_VEC4S);
	samplers.AddStaticImage(tr.waterSceneImage, 0);
	samplers.AddStaticImage(tr.waterDepthImage, 1);
	samplers.AddStaticImage(tr.waterNormalImage, 2);
	samplers.AddStaticImage(tr.waterGlowImage, 9);
	if (tr.envBrdfImage) samplers.AddStaticImage(tr.envBrdfImage, 3);
	if (cubemap)
	{
		const cubemap_t *cm = &tr.cubemaps[cubemapIndex - 1];
		samplers.AddStaticImage(cm->image, 4);
		vec4_t info;
		VectorSubtract(cm->origin, backEnd.viewParms.ori.origin, info);
		info[3] = 1.0f;
		VectorScale4(info, 1.0f / MAX(cm->parallaxRadius, 1.0f), info);
		uniforms.SetUniformVec4(UNIFORM_CUBEMAPINFO, info);
	}
	if (flags & 2) samplers.AddStaticImage(tr.sunShadowArrayImage, TB_SHADOWMAP);
	if (backEnd.waterSurfaceSSR)
	{
		uniforms.SetUniformVec4(UNIFORM_SSRPROJECTION, s_water.viewInfo.projection);
		uniforms.SetUniformVec4(UNIFORM_SSRDEPTHPARAMS, s_water.viewInfo.depthParams);
		uniforms.SetUniformVec4(UNIFORM_SSRVIEWPORT, s_water.viewInfo.viewport);
		vec4_t texelSize;
		RB_ScreenTexelSize(texelSize, tr.renderFbo->width, tr.renderFbo->height, tr.renderFbo->width, tr.renderFbo->height);
		uniforms.SetUniformVec4(UNIFORM_SSRTEXELSIZE, texelSize);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS, s_water.ssrSettings[0]);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS2, s_water.ssrSettings[1]);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS3, s_water.ssrSettings[2]);
		samplers.AddStaticImage(tr.screenHiZImage, 10);
		samplers.AddStaticImage(tr.ssrColorImage, 11);
	}
	if (froxel) RB_VolumetricSetupFogDraw(1, uniforms, samplers);
}

/*
=================
RB_WaterSurfacePrepare / RB_WaterSurfaceFinish

Around the water slot of the main pass (RB_SubmitRenderPass), renderFbo bound:
the scene under the water and its depth are copied (MSAA: resolved) once.
=================
*/
void RB_WaterSurfacePrepare( void )
{
	FBO_t *oldFbo = glState.currentFBO;
	const viewParms_t& viewParms = backEnd.viewParms;

	R_PushDebugGroup(AL_STAGE, "Water surface");
	s_water.timer = RB_ScreenBeginTimer("Water surface");

	// blits are clipped by the scissor rectangle
	GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
	FBO_FastBlitIndexed(tr.renderFbo, tr.waterCopyFbo, 0, 0, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	FBO_FastBlitIndexed(tr.renderFbo, tr.waterCopyFbo, 1, 1, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	FBO_Bind(oldFbo);
	GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
		viewParms.viewportWidth, viewParms.viewportHeight);
}

qboolean RB_WaterSurfaceReflectionBegin( void )
{
	if (!backEnd.waterSurfaceSSR || !tr.waterReflectionFbo[0])
		return qfalse;
	s_water.reflectionHistoryValid = (qboolean)(r_ssrTemporal->integer &&
		RB_ScreenHistoryValid(s_water.history, s_water.reflectionScale));
	s_water.reflectionCurrent = s_water.history.valid ? 1 - s_water.history.current : 0;
	FBO_t *fbo = tr.waterReflectionFbo[s_water.reflectionCurrent];
	FBO_Bind(fbo);
	GL_SetViewportAndScissor(0, 0, fbo->width, fbo->height);
	GL_State(GLS_DEPTHMASK_TRUE);
	GL_SetScreenAuxWrite(true);
	qglClearBufferfv(GL_COLOR, 0, colorBlack);
	qglClearBufferfv(GL_COLOR, 1, colorBlack);
	qglClearBufferfv(GL_COLOR, 2, colorBlack);
	qglClear(GL_DEPTH_BUFFER_BIT);
	const viewParms_t& v = backEnd.viewParms;
	const int x = (int)floorf(v.viewportX / s_water.reflectionScale);
	const int y = (int)floorf(v.viewportY / s_water.reflectionScale);
	const int x1 = (int)ceilf((v.viewportX + v.viewportWidth) / s_water.reflectionScale);
	const int y1 = (int)ceilf((v.viewportY + v.viewportHeight) / s_water.reflectionScale);
	GL_SetViewportAndScissor(x, y, x1 - x, y1 - y);
	return qtrue;
}

void RB_WaterSurfaceReflectionEnd( void )
{
	screenViewInfo_t info;
	RB_ScreenGetViewInfo(info);
	RB_ScreenStoreHistory(s_water.history, info, s_water.reflectionScale, s_water.reflectionCurrent);
	s_water.reflectionReady = qtrue;
	FBO_Bind(tr.renderFbo);
	const viewParms_t& v = backEnd.viewParms;
	GL_SetViewportAndScissor(v.viewportX, v.viewportY, v.viewportWidth, v.viewportHeight);
}

void RB_WaterSurfaceBindReflection( shaderProgram_t *program, qboolean trace )
{
	vec4_t pass = {trace ? 1.0f : 0.0f,
		trace ? (s_water.reflectionHistoryValid ? 1.0f : 0.0f) : (s_water.reflectionReady ? 1.0f : 0.0f),
		Com_Clamp(0.0f, 0.95f, r_ssrTemporalWeight->value), s_water.reflectionScale};
	GLSL_SetUniformVec4(program, UNIFORM_WATERPASS, pass);
	if (!backEnd.waterSurfaceSSR || !tr.waterReflectionImage[0])
		return;
	const int index = trace ? 1 - s_water.reflectionCurrent : s_water.reflectionCurrent;
	GL_BindToTMU(tr.waterReflectionImage[index], 12);
	GL_BindToTMU(tr.waterReflectionGeomImage[index], 14);
	GL_BindToTMU(tr.waterReflectionHitImage[index], 15);
	if (trace && s_water.reflectionHistoryValid)
		GLSL_SetUniformMatrix4x4(program, UNIFORM_SSRREPROJECT, s_water.history.viewProjection);
}

void RB_WaterSurfaceFinish( void )
{
	RB_ScreenEndTimer(s_water.timer);
	s_water.timer = -1;
	R_PushDebugGroup(AL_STAGE, "Mainpass");
}

/*
=================
RB_WaterSurfaceLegacyScissor

r_waterSurfaceDebug 9: the legacy stages of the classified surfaces are drawn
left of the split only (RB_DrawItems), the water program right of it.
=================
*/
void RB_WaterSurfaceLegacyScissor( qboolean enable )
{
	const viewParms_t& viewParms = backEnd.viewParms;
	if ( enable )
	{
		const int split = (int)(Com_Clamp(0.0f, 1.0f, r_waterSurfaceSplit->value) * (float)viewParms.viewportWidth);
		qglScissor(viewParms.viewportX, viewParms.viewportY, split, viewParms.viewportHeight);
	}
	else
	{
		qglScissor(viewParms.viewportX, viewParms.viewportY, viewParms.viewportWidth, viewParms.viewportHeight);
	}
}

/*
=================
RB_WaterSurfaceDistortion

The refraction pass of the view (refractionFill, after tone mapping) takes
the refractive shaders: not a refractive shader classified as water in a view
with the water program, which draws it in the main pass instead.
=================
*/
qboolean RB_WaterSurfaceDistortion( const shader_t *shader, uint32_t waterKey )
{
	if ( !shader->useDistortion )
		return qfalse;
	if ( (waterKey & WATERKEY_INTERFACE) && backEnd.waterSurfaceView && tr.waterSurfaceShader[0].program )
		return qfalse;
	return qtrue;
}
