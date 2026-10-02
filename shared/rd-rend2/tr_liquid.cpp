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
// Brush models (func_door water, inline *N models) are not media: they move,
// and their brushes have no transform here. cgame keeps its legacy tint for
// them (it asks the world model contents only, see r_volumetricWaterActive).
//
// docs/rend2-volumetric-fog.md, "Underwater medium".

#include "tr_local.h"

#include <algorithm>
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
	int				lastVisible;	// diagnostics: brushes of the last build
	int				lastCandidates;
} s_liq;

static const char *s_liquidClassNames[LIQUID_CLASSES] = { "water", "slime", "lava" };

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
	const int numSides = sidesLump->filelen / sizeof(dbrushside_t);
	const int firstBrush = LittleLong(model->firstBrush);
	const int modelBrushes = LittleLong(model->numBrushes);
	if ( firstBrush < 0 || modelBrushes < 0 || firstBrush + modelBrushes > numBrushes )
		return;

	std::vector<liquidBrush_t> liquids;
	std::vector<float> planes;

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
			world->liquidSkipped[3]++;
			continue;
		}
		// a fog volume brush already is a medium of the froxel fog (R_LoadFogs)
		if ( contents & CONTENTS_FOG )
		{
			world->liquidSkipped[0]++;
			continue;
		}

		const int firstSide = LittleLong(brushes[i].firstSide);
		const int brushSides = LittleLong(brushes[i].numSides);
		if ( brushSides < 6 || brushSides > MAX_LIQUID_SIDES || firstSide < 0 || firstSide + brushSides > numSides )
		{
			world->liquidSkipped[1]++;
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
			world->liquidSkipped[1]++;
			continue;
		}

		if ( (int)liquids.size() >= MAX_LIQUID_BRUSHES ||
			(int)(planes.size() / 4) + brushSides > MAX_LIQUID_PLANES )
		{
			world->liquidSkipped[2]++;
			continue;
		}

		brush.firstPlane = (int)(planes.size() / 4);
		brush.liquidClass = liquidClass;
		brush.brushNum = i;
		brush.shaderNum = shaderNum;
		for ( int k = 0; k < brushSides; k++ )
		{
			const int planeNum = LittleLong(sides[firstSide + k].planeNum);
			if ( planeNum < 0 || planeNum >= world->numplanes )
				continue;
			const cplane_t *plane = &world->planes[planeNum];
			planes.push_back(plane->normal[0]);
			planes.push_back(plane->normal[1]);
			planes.push_back(plane->normal[2]);
			planes.push_back(plane->dist);
			brush.numPlanes++;
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

qboolean R_LiquidsAvailable( void )
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

qboolean R_LiquidSurfacesEnabled( void )
{
	return (qboolean)(R_LiquidsAvailable() && r_volumetricWaterSurfaces->integer && r_sunlightMode->integer);
}

int R_LiquidClassMask( void )
{
	if ( !R_LiquidsAvailable() || !tr.world )
		return 0;
	return r_volumetricWater->integer & tr.world->liquidClassMask & ((1 << LIQUID_CLASSES) - 1);
}

int R_LiquidPointClass( const vec3_t p )
{
	return R_LiquidClassOfContents(ri.CM_PointContents(p, 0));
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
	GL_BindToTMU(&s_liq.causticImage, TB_LIQUIDCAUSTICS);
}

/*
=================
R_LiquidsUpdateActive

r_volumetricWaterActive tells cgame which liquid classes the renderer draws as
a medium (it then skips their legacy full screen tint): the classes of this
map in r_volumetricWater, while the froxel volume can be built at all (the
runtime switches of its world view test). Camera independent; set only when
it changes, cgame sees it one frame later.
=================
*/
void R_LiquidsUpdateActive( void )
{
	const int mask = (r_drawfog->integer && r_depthPrepass->integer) ? R_LiquidClassMask() : 0;
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
	if ( R_LiquidsAvailable() )
	{
		R_LiquidsUploadPlanes();
		R_LiquidsCreateCausticImage();
	}
	R_LiquidsUpdateActive();
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
		VectorSet4(block->mins[n], brush->bounds[0][0], brush->bounds[0][1], brush->bounds[0][2],
			(float)brush->firstPlane);
		VectorSet4(block->maxs[n], brush->bounds[1][0], brush->bounds[1][1], brush->bounds[1][2],
			(float)(brush->numPlanes + 64 * brush->liquidClass));

		const int z0 = Q_max(0, depthSlice(MAX(candidate.minDepth, 0.0f)) - 1);
		const int z1 = Q_min(numSlices - 1, depthSlice(MIN(candidate.maxDepth, farZ)) + 1);
		for ( int k = z0; k <= z1; k++ )
			block->slices[k] |= 1 << n;
	}
	s_liq.lastVisible = count;

	block->params[0] = (float)count;
	block->params[2] = r_volumetricWaterSunPath->integer ? 1.0f : 0.0f;

	for ( int c = 0; c < LIQUID_CLASSES; c++ )
	{
		R_LiquidsMaterial(c, block->material[c * 2], block->material[c * 2 + 1]);
		if ( !(mask & (1 << c)) )
			block->material[c * 2][3] = 0.0f;
	}

	// caustics of the sun under water: period, animation phase (wrapped, so the
	// float keeps its precision), focus depth, strength
	const float period = MAX(r_volumetricWaterCausticScale->value, 8.0f);
	const float phase = fmodf(time * r_volumetricWaterCausticSpeed->value, 1024.0f);
	VectorSet4(block->caustics, 1.0f / period, phase, MAX(r_volumetricWaterCausticFocus->value, 1.0f),
		Com_Clamp(0.0f, 1.0f, r_volumetricWaterCaustics->value));

	// fade before far; the world size of a froxel per unit of view depth (caustic
	// lod) is set by the caller, which knows the froxel grid
	const float fadeStart = farZ * 0.8f;
	VectorSet4(block->view, fadeStart, 1.0f / MAX(farZ - fadeStart, 1.0f), 0.0f, 0.0f);
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
		samplers.AddStaticImage(&s_liq.causticImage, TB_LIQUIDCAUSTICS);
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

r_liquids: the liquid brushes of the map and of the last froxel frame
=================
*/
void R_Liquids_f( void )
{
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "r_liquids: no map\n");
		return;
	}
	const world_t *w = tr.world;
	ri.Printf(PRINT_ALL, "Liquid brushes of %s: %d (%d planes), load %.2f ms\n", w->baseName,
		w->numLiquids, w->numLiquidPlanes, w->liquidLoadMsec);
	ri.Printf(PRINT_ALL, "  skipped: %d fog contents, %d shape (sides / not axial), %d capacity, %d brush model (moving, unsupported)\n",
		w->liquidSkipped[0], w->liquidSkipped[1], w->liquidSkipped[2], w->liquidSkipped[3]);
	ri.Printf(PRINT_ALL, "  r_volumetricWater %d (latched), available %s, classes drawn %d (r_volumetricWaterActive %d), surfaces %s\n",
		r_volumetricWater->integer, R_LiquidsAvailable() ? "yes" : "no", R_LiquidClassMask(),
		r_volumetricWaterActive->integer, R_LiquidSurfacesEnabled() ? "yes" : "no");
	ri.Printf(PRINT_ALL, "  last froxel frame: %d visible of %d candidates (GPU max %d)\n",
		s_liq.lastVisible, s_liq.lastCandidates, MAX_GPU_LIQUIDS);

	for ( int i = 0; i < w->numLiquids; i++ )
	{
		const liquidBrush_t *b = &w->liquids[i];
		const char *shader = (b->shaderNum >= 0 && b->shaderNum < w->numShaders) ? w->shaders[b->shaderNum].shader : "?";
		ri.Printf(PRINT_ALL, "  %3d %-5s brush %5d sides %2d (%5.0f %5.0f %5.0f)-(%5.0f %5.0f %5.0f) %s\n",
			i, s_liquidClassNames[b->liquidClass], b->brushNum, b->numPlanes,
			b->bounds[0][0], b->bounds[0][1], b->bounds[0][2],
			b->bounds[1][0], b->bounds[1][1], b->bounds[1][2], shader);
	}

	const int cameraClass = w->numLiquids ? R_LiquidPointClass(tr.refdef.vieworg) : -1;
	ri.Printf(PRINT_ALL, "  camera contents: %s\n", cameraClass < 0 ? "none" : s_liquidClassNames[cameraClass]);
}
