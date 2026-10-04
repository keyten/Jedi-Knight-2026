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

/*
============================================================

Classification records (r_waterInfo)

============================================================
*/

enum waterReason_t
{
	WREASON_NONE,
	WREASON_CONTENTS,			// CONTENTS_WATER (surfaceparm water) of the BSP shader
	WREASON_REFRACTIVE_BRUSH,	// refractive shader on a water brush side
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
	"refractive on a water brush",
	"r_waterOverride on",
	"experimental name rule",
	"lava",
	"slime without water contents",
	"mostly not facing up (waterfall / stream)",
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
	int			brush;			// index in s_water.brushes, -1: none
	vec3_t		brushSide;		// normal of the side it lies on
	qboolean	refractive;
	qboolean	nameLike;
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
	float		classifyMsec;

	std::vector<waterOverride_t>		overrides;

	// per frame
	int			timer;
	int			lastDraws;
	int			drawsThisFrame;
	unsigned	statsFrame;
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

// the side plane of brush b that p lies on, -1: none
static int R_WaterOnSide( const waterBrushRecord_t& b, const vec3_t p, float eps )
{
	for ( int k = 0; k < b.numPlanes; k++ )
	{
		const waterPlane_t& plane = s_water.planes[b.firstPlane + k];
		if ( fabsf(DotProduct(plane.normal, p) - plane.dist) < eps )
			return k;
	}
	return -1;
}

// At least half of the shader's area on the map faces up, its downward faces
// left out: a liquid brush draws its bottom (the area of its top) and its
// sides too. Waterfalls and streams (steep sheets) face up nowhere.
static qboolean R_WaterMostlyUp( const waterShaderRecord_t& s )
{
	const float area = s.area - s.downArea;
	return (qboolean)(s.upArea > 0.0f && s.upArea >= 0.5f * area);
}

/*
=================
R_WaterDecideShaders

Per shader decision from the surface records, then r_waterOverride and the
experimental rule; writes shader_t::waterSurface / waterClass / waterFlags.
Runs at map load and again when r_waterOverride or r_waterSurfaceExperimental
change.
=================
*/
static void R_WaterDecideShaders( void )
{
	// reset every shader of the records (a previous decision may be undone)
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		s.shader->waterSurface = 0;
		s.shader->waterClass = LIQUID_WATER;
		s.shader->waterFlags = 0;
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
			s.reason = R_WaterMostlyUp(s) ? WREASON_CONTENTS : WREASON_NOT_HORIZONTAL;
		else if ( liquidClass == LIQUID_SLIME )
			s.reason = WREASON_SLIME_ONLY;
		else if ( refractive && s.linked > 0 )
			s.reason = WREASON_REFRACTIVE_BRUSH;
		else if ( refractive )
			s.reason = WREASON_REFRACTIVE_ONLY;
		else
			s.reason = WREASON_NO_SEMANTICS;

		s.modern = (qboolean)(s.reason == WREASON_CONTENTS || s.reason == WREASON_REFRACTIVE_BRUSH);

		// the optics and the medium of the brush the surfaces lie on
		for ( const waterSurfaceRecord_t& r : s_water.surfaces )
		{
			if ( r.shader != s.shader || r.brush < 0 )
				continue;
			const waterBrushRecord_t& b = s_water.brushes[r.brush];
			if ( b.contents & CONTENTS_FOG )
				s.flags |= WATERSURF_FOG_MEDIUM;
			if ( b.liquidClass == LIQUID_SLIME )
				s.liquidClass = LIQUID_SLIME;
		}
		if ( s.contents & CONTENTS_FOG )
			s.flags |= WATERSURF_FOG_MEDIUM;
		// the env.json "Liquids" profile of this shader, the medium the
		// liquid brushes under it get too (tr_liquid.cpp); lava has no water optics
		const int profile = R_LiquidProfileForShader(s.shader->name);
		if ( profile == LIQUID_WATER || profile == LIQUID_SLIME )
			s.liquidClass = profile;

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

	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		if ( !s.modern )
			continue;
		s.shader->waterSurface = 1;
		s.shader->waterClass = (int8_t)s.liquidClass;
		s.shader->waterFlags = (uint8_t)s.flags;
	}
}

static void R_WaterCollectCandidates( void );

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
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();

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
	s_water.classifyMsec = (float)(ri.Milliseconds() - start);

	int modern = 0;
	for ( const waterShaderRecord_t& s : s_water.shaders )
		modern += s.modern ? 1 : 0;
	if ( !s_water.shaders.empty() )
		ri.Printf(PRINT_DEVELOPER, "Water surfaces: %d shaders drawn as water of %d candidates (%d surfaces, %d liquid brushes), %.1f ms\n",
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
		s.shader->waterFlags = 0;
	}
	s_water.surfaces.clear();
	s_water.shaders.clear();
	if ( !world )
		return;

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

		const qboolean liquid = (qboolean)(R_WaterLiquidClass(contents) >= 0);
		const qboolean refractive = shader->useDistortion;
		const qboolean nameLike = R_WaterNameLike(shader->name);
		const qboolean material = (qboolean)((surfaceFlags & MATERIAL_MASK) == MATERIAL_WATER);
		if ( !liquid && !refractive && !nameLike && !material && !R_WaterFindOverride(shader->name) )
			continue;

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
			for ( int v = 0; v < bsp->numVerts; v++ )
				VectorAdd(vnormal, bsp->verts[v].normal, vnormal);
			if ( DotProduct(sum, vnormal) < 0.0f )
				VectorNegate(sum, sum);
			if ( VectorNormalize(sum) < 1e-6f )
				VectorNormalize2(vnormal, sum);
			VectorCopy(sum, r.normal);
			r.area = area;

			// the liquid brush it lies on: every vertex inside the brush and on one of its sides
			for ( size_t bi = 0; bi < s_water.brushes.size() && bsp->numVerts > 0; bi++ )
			{
				const waterBrushRecord_t& b = s_water.brushes[bi];
				qboolean all = qtrue;
				for ( int v = 0; v < bsp->numVerts && all; v++ )
				{
					if ( !R_WaterInsideBrush(b, bsp->verts[v].xyz, 2.0f) || R_WaterOnSide(b, bsp->verts[v].xyz, 1.5f) < 0 )
						all = qfalse;
				}
				if ( all )
				{
					r.brush = (int)bi;
					const int side = R_WaterOnSide(b, bsp->verts[0].xyz, 1.5f);
					VectorCopy(s_water.planes[b.firstPlane + side].normal, r.brushSide);
					break;
				}
			}
		}
		r.orient = R_WaterOrientation(r.normal);
		s_water.surfaces.push_back(r);
	}

	// per shader
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		waterShaderRecord_t *s = NULL;
		for ( waterShaderRecord_t& e : s_water.shaders )
		{
			if ( e.shader == r.shader )
			{
				s = &e;
				break;
			}
		}
		if ( !s )
		{
			waterShaderRecord_t e = {};
			e.shader = r.shader;
			e.bspShader = r.bspShader;
			e.reason = WREASON_NONE;
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

/*
============================================================

Commands

============================================================
*/

static void R_WaterPrintShaders( void )
{
	ri.Printf(PRINT_ALL, "  shaders (decision per shader, surfaces up/down/vertical/sloped, up share of the area without the bottoms, linked to a liquid brush):\n");
	for ( const waterShaderRecord_t& s : s_water.shaders )
	{
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
		ri.Printf(PRINT_ALL, "  %d candidate surfaces (r_waterInfo surfaces lists them)\n", (int)s_water.surfaces.size());
		return;
	}
	ri.Printf(PRINT_ALL, "  surfaces:\n");
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		const qboolean modern = (qboolean)(r.shader->waterSurface != 0);
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
		// an overridden shader may not have been a candidate: collect again
		R_WaterCollectCandidates();
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
	if ( !s_water.resources )
		return;

	tr.waterSceneImage = R_CreateImage("*waterScene", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);
	tr.waterDepthImage = R_CreateImage("*waterDepth", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_DEPTH24_STENCIL8);

	const int size = 256;
	std::vector<float> slopes;
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
	if ( !s_water.resources || !tr.waterSceneImage || !tr.waterDepthImage )
		return;

	tr.waterCopyFbo = FBO_Create("_waterCopy", tr.waterSceneImage->width, tr.waterSceneImage->height);
	FBO_Bind(tr.waterCopyFbo);
	FBO_AttachTextureImage(tr.waterSceneImage, 0);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tr.waterDepthImage->texnum, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(tr.waterCopyFbo);
}

void R_WaterSurfaceShutdown( void )
{
	// images and FBOs belong to the renderer lists; the resource decision
	// follows the programs (R_CreateWaterSurfaceImages)
	s_water.world = NULL;
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();
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

	if ( !s_water.resources || !r_waterSurface->integer || !tr.waterCopyFbo )
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
	if ( !backEnd.waterSurfaceView || !shader || !shader->waterSurface )
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
void RB_WaterSurfaceSetupDraw( const shaderCommands_t *input, UniformDataWriter& uniforms,
	SamplerBindingsWriter& samplers )
{
	const shader_t *shader = input->shader;
	const viewParms_t& viewParms = backEnd.viewParms;
	const float *proj = viewParms.projectionMatrix;
	const int liquidClass = Com_Clampi(LIQUID_WATER, LIQUID_SLIME, shader->waterClass);

	vec4_t water[WATER_UNIFORM_VEC4S];
	Com_Memset(water, 0, sizeof(water));

	VectorSet4(water[0],
		Com_Clamp(1.0f, 2.0f, r_waterSurfaceIOR->value),
		Com_Clamp(0.0f, 1.0f, r_waterSurfaceRoughness->value),
		Com_Clamp(0.0f, 4.0f, r_waterSurfaceNormal->value),
		Com_Clamp(0.0f, 4.0f, r_waterSurfaceRefraction->value));
	VectorSet4(water[1],
		Com_Clamp(0.0f, 4.0f, r_waterSurfaceReflection->value),
		backEnd.waterSurfaceSSR ? Com_Clamp(0.0f, 1.0f, r_waterSurfaceSSR->value) : 0.0f,
		Com_Clamp(0.0f, 16.0f, r_waterSurfaceAbsorption->value),
		Com_Clamp(0.0f, 16.0f, r_waterSurfaceDepthScale->value));

	// the optics of the liquid: the r_volumetricWater / Slime parameters (the
	// same values as the froxel medium, R_LiquidsMaterial), used whether the
	// volumetric water is on or not
	vec4_t extinction, albedo;
	R_LiquidsMaterial(liquidClass, extinction, albedo);
	const qboolean froxel = RB_VolumetricLookupReady();
	// the froxel volume holds this liquid: a world brush of an enabled class
	// (r_volumetricWater mask) whose medium is these optics (vjun1: water
	// brushes with the slime medium under a slime surface)
	const qboolean froxelLiquid = (qboolean)(froxel && backEnd.currentEntity == &tr.worldEntity &&
		(R_LiquidMediumSlotMask() & (1 << liquidClass)) != 0);
	VectorSet4(water[2], extinction[0] * extinction[3], extinction[1] * extinction[3], extinction[2] * extinction[3],
		froxelLiquid ? 1.0f : 0.0f);
	VectorSet4(water[3], albedo[0], albedo[1], albedo[2], albedo[3]);

	VectorSet4(water[4], proj[0], proj[5], proj[8], proj[9]);
	VectorSet4(water[5], proj[14], proj[10], fmodf(backEnd.refdef.floatTime, 3600.0f), tr.linearLight ? 1.0f : 0.0f);

	// debug view and split
	int debugView = r_waterSurfaceDebug->integer;
	float splitX = -1.0f;
	if ( debugView == 9 )
	{
		splitX = (float)viewParms.viewportX + Com_Clamp(0.0f, 1.0f, r_waterSurfaceSplit->value) * (float)viewParms.viewportWidth;
		debugView = 0;
	}

	const qboolean cubemap = (qboolean)(r_cubeMapping->integer && !(viewParms.flags & VPF_NOCUBEMAPS) &&
		input->cubemapIndex > 0 && input->cubemapIndex <= tr.numCubemaps && tr.cubemaps[input->cubemapIndex - 1].image);
	const qboolean sun = (qboolean)(r_sunlightMode->integer && (viewParms.flags & VPF_USESUNLIGHT) && tr.sunShadowArrayImage);

	int flags = 0;
	if ( cubemap )
		flags |= 1;
	if ( sun )
		flags |= 2;
	if ( froxel )
		flags |= 4;
	if ( r_waterSurfaceDepthReject->integer )
		flags |= 8;
	if ( backEnd.waterSurfaceSSR )
		flags |= 16;
	// a fog volume brush: its froxel medium is the liquid (froxel fog), or the legacy
	// fog passes already fogged the scene under it
	if ( shader->waterFlags & WATERSURF_FOG_MEDIUM )
		flags |= 32;
	if ( liquidClass == LIQUID_SLIME )
		flags |= 64;
	if ( shader->waterFlags & WATERSURF_OVERRIDE )
		flags |= 128;
	if ( shader->waterFlags & WATERSURF_EXPERIMENTAL )
		flags |= 256;
	// the split sum of the specular IBL (only created with r_cubeMapping): without
	// it the reflection weight is the exact Fresnel of the smooth surface
	if ( tr.envBrdfImage )
		flags |= 512;

	VectorSet4(water[6], (float)debugView, splitX, (float)flags, 8192.0f);

	// view basis of the reconstruction (x right, y up)
	vec3_t right, up;
	VectorScale(viewParms.ori.axis[1], -1.0f, right);
	VectorCopy(viewParms.ori.axis[2], up);
	VectorNormalize(right);
	VectorNormalize(up);
	// the liquid of the froxel volume fades out over its last fifth (R_LiquidsBuild):
	// the analytic medium takes over there
	const float farZ = R_VolumetricFarZ();
	const float fadeStart = farZ > 0.0f ? farZ * 0.8f : 0.0f;
	const float fadeLength = farZ > 0.0f ? farZ * 0.2f : 1.0f;
	VectorSet4(water[7], right[0], right[1], right[2], fadeStart);
	VectorSet4(water[8], up[0], up[1], up[2], 1.0f / MAX(fadeLength, 1.0f));

	// without a cubemap: the ambient of the sky (or a dim grey)
	vec3_t env;
	VectorCopy(backEnd.refdef.sunAmbCol, env);
	if ( VectorLength(env) < 0.02f )
		VectorSet(env, 0.08f, 0.09f, 0.1f);
	VectorSet4(water[9], env[0], env[1], env[2], sun ? 1.0f : 0.0f);

	// waves: the flow layer repeats once per first stage texture tile, the world layers every 192 units
	const float waveSize = 192.0f;
	VectorSet4(water[10], 1.0f, 1.0f / waveSize, 0.0f, 0.0f);
	// drift of the world layers, wrapped at whole tiles (the pattern tiles)
	const float t = backEnd.refdef.floatTime;
	const float wind[2] = { 0.8f, 0.6f };
	const float wind2[2] = { -0.6f * 0.6f + 0.8f * 0.8f, 0.8f * 0.6f + 0.6f * 0.8f };
	auto wrap = []( double v ) -> float { return (float)(v - floor(v)); };
	VectorSet4(water[11], wrap(wind[0] * 0.020 * t), wrap(wind[1] * 0.020 * t),
		wrap(wind2[0] * 0.034 * t + 0.37), wrap(wind2[1] * 0.034 * t + 0.71));

	uniforms.SetUniformVec4(UNIFORM_WATER, water[0], WATER_UNIFORM_VEC4S);

	samplers.AddStaticImage(tr.waterSceneImage, 0);
	samplers.AddStaticImage(tr.waterDepthImage, 1);
	samplers.AddStaticImage(tr.waterNormalImage, 2);
	if ( tr.envBrdfImage )
		samplers.AddStaticImage(tr.envBrdfImage, 3);
	if ( cubemap )
	{
		const cubemap_t *cm = &tr.cubemaps[input->cubemapIndex - 1];
		samplers.AddStaticImage(cm->image, 4);
		vec4_t info;
		VectorSubtract(cm->origin, viewParms.ori.origin, info);
		info[3] = 1.0f;
		VectorScale4(info, 1.0f / cm->parallaxRadius, info);
		uniforms.SetUniformVec4(UNIFORM_CUBEMAPINFO, info);
	}
	if ( sun )
		samplers.AddStaticImage(tr.sunShadowArrayImage, TB_SHADOWMAP);

	if ( backEnd.waterSurfaceSSR )
	{
		screenViewInfo_t info;
		RB_ScreenGetViewInfo(info);
		uniforms.SetUniformVec4(UNIFORM_SSRPROJECTION, info.projection);
		uniforms.SetUniformVec4(UNIFORM_SSRDEPTHPARAMS, info.depthParams);
		uniforms.SetUniformVec4(UNIFORM_SSRVIEWPORT, info.viewport);
		vec4_t texelSize;
		RB_ScreenTexelSize(texelSize, tr.renderFbo->width, tr.renderFbo->height, tr.renderFbo->width, tr.renderFbo->height);
		uniforms.SetUniformVec4(UNIFORM_SSRTEXELSIZE, texelSize);
		// the trace settings of the SSR (r_ssrQuality, r_ssrSteps, ...)
		int steps, refine;
		qboolean hiZ;
		RB_SSRTraceParams(&steps, &refine, &hiZ);
		vec4_t settings, settings2, settings3;
		VectorSet4(settings, (float)Com_Clampi(1, 256, steps), (float)Com_Clampi(0, 16, refine),
			r_ssrMaxDistance->value, r_ssrThickness->value);
		VectorSet4(settings2, MAX(r_ssrMaxRoughness->value, 0.05f), r_ssrEdgeFade->value, (float)(SSR_COLOR_MIPS - 1), 0.0f);
		VectorSet4(settings3, (float)(SCREEN_HIZ_MIPS - 1), 0.0f, r_znear->value, (float)Com_Clampi(8, 1024, steps * 3));
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS, settings);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS2, settings2);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS3, settings3);
		samplers.AddStaticImage(tr.screenHiZImage, 10);
		samplers.AddStaticImage(tr.ssrColorImage, 11);
	}

	// the froxel volume between the surface and the scene behind it
	if ( froxel )
		RB_VolumetricSetupFogDraw(1, uniforms, samplers);
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
	FBO_FastBlit(tr.renderFbo, NULL, tr.waterCopyFbo, NULL, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	FBO_Bind(oldFbo);
	GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
		viewParms.viewportWidth, viewParms.viewportHeight);
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
qboolean RB_WaterSurfaceDistortion( const shader_t *shader )
{
	if ( !shader->useDistortion )
		return qfalse;
	if ( shader->waterSurface && backEnd.waterSurfaceView && tr.waterSurfaceShader[0].program )
		return qfalse;
	return qtrue;
}
