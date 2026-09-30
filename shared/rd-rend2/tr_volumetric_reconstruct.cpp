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
Static baked light of the froxel fog (r_volumetricFog 2), built once per map
load: the light grid split by the sun, and the directional baked light moments
(r_volumetricFogStaticDirectional). The reconstruction itself is in
tr_volrecon.cpp (no renderer globals, tested by tools/volrecon_test); this file
reads the light grid, traces the sky visibility, collects the static area
emitters and uploads the textures. See docs/rend2-volumetric-fog.md.

The legacy volumetric light map merges the ambient and directed light of every
light grid cell (volumetricLightMaps[0], R_BuildLightGridTexture). Textures with
its layout, so that B + S == the legacy value:

  volumetricSunGrid     S = the directed light of cells lit from the sun direction
                        (alignment smoothstep 25..10 degrees) times the part of the
                        cell that sees the sky: an indoor lamp that happens to be
                        aligned with the sun stays static light
  volumetricStaticGrid  B = everything else (rgb), sun fraction f = alignment * sky
                        visibility (a): inside the cascades the realtime sun replaces
                        S with f * sun * shadow, so B + f * sun never counts the
                        (1 - f) D kept in B twice
  volumetricDirMoment*  (mode 1, 2) the first angular moments of the attributed part
                        of B per colour channel, |M_c| <= B_c: the injection applies
                        B + 3 g (M.v) with |g| <= 1/3 (the L1 Henyey-Greenstein
                        response), so the mean over all directions stays B

Cells in walls (styles[0] == LS_LSNONE) keep their light isotropic. The realtime
sun radiance is estimated from the sunlit cells, so the beams have the
brightness the map was compiled with.
*/

#include "tr_local.h"
#include "tr_volrecon.h"

#include <algorithm>
#include <vector>

// light grid cells whose light comes from within ~10 degrees of the sun
// direction are sun, beyond ~25 degrees not
#define FROXEL_SUN_COS_OUTER 0.9063f	// cos(25)
#define FROXEL_SUN_COS_INNER 0.9848f	// cos(10)
#define FROXEL_SUN_TRACE_DISTANCE 65536.0f

static struct
{
	qboolean valid;
	int mode;
	vrStats stats;
	std::vector<vrProxy> proxies;
	int numAreaSources;
	int numAreaCandidates;
	int numTraced;		// candidate cells
	int numRefined;		// cells that got the four corner rays
	int numRays;
	int numSeeSky;

	int msecTrace;
	int msecAreaSources;
	int msecUpload;
	int msecTotal;
	float maxError;
	float maxRelError;
} s_vr;

// samplers of the injection without the moments (raster and compute), as R_VolumetricComputeAvailable
#define FROXEL_INJECT_SAMPLERS(rgb) ((rgb) ? 15 : 14)
#define FROXEL_MOMENT_SAMPLERS 3

// the three moment samplers (units TB_SPECULARMAP, TB_SSAOMAP, TB_VOLUMETRICMOMENTB) on top of the
// injection's own: GL 3.2 guarantees only 16 fragment samplers. Checked once per context (the
// renderer DLL is reloaded with it).
static qboolean R_VolumetricStaticDirectionalSupported( void )
{
	static int supported = -1;
	if ( supported < 0 )
	{
		GLint fragment = 0, combined = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragment);
		qglGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combined);
		const int needed = FROXEL_INJECT_SAMPLERS(r_volumetricFogRGBExtinction->integer != 0) + FROXEL_MOMENT_SAMPLERS;
		supported = (fragment >= needed && combined > TB_VOLUMETRICMOMENTB) ? 1 : 0;
		if ( !supported && r_volumetricFogStaticDirectional->integer )
		{
			ri.Printf(PRINT_WARNING, "r_volumetricFogStaticDirectional: needs %d fragment samplers and more than %d "
				"texture units (have %d, %d), directional baked light disabled\n", needed, TB_VOLUMETRICMOMENTB,
				fragment, combined);
		}
	}
	return (qboolean)(supported != 0);
}

qboolean R_VolumetricStaticDirectional( void )
{
	return (qboolean)(R_VolumetricFroxelEnabled() && r_volumetricFogStaticDirectional->integer != 0 &&
		R_VolumetricStaticDirectionalSupported());
}


void R_ClearVolumetricStaticReconstruction( void )
{
	s_vr.valid = qfalse;
	s_vr.mode = 0;
	Com_Memset(&s_vr.stats, 0, sizeof(s_vr.stats));
	s_vr.proxies.clear();
	s_vr.proxies.shrink_to_fit();
}

static float R_VolumetricSRGBToLinear( float c )
{
	return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float R_VolumetricSmoothstep( float edge0, float edge1, float x )
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

// true when the world does not block the way from start towards the sun
static qboolean R_VolumetricSunVisible( const vec3_t start, const vec3_t sunDir )
{
	vec3_t end;
	VectorMA(start, FROXEL_SUN_TRACE_DISTANCE, sunDir, end);

	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	if ( trace.startsolid || trace.allsolid )
		return qfalse;
	// the sky brushes are solid: reaching one is reaching the sky
	return (qboolean)(trace.fraction >= 1.0f || (trace.surfaceFlags & SURF_SKY));
}

// rays of the light grid cell towards the sun: its centre and four corners of a
// tetrahedron half a cell out; returns how many of rays [first, first + count) see the sky
static int R_VolumetricSunRays( const world_t *world, int cell, const vec3_t sunDir, int first, int count )
{
	const int bx = world->lightGridBounds[0];
	const int by = world->lightGridBounds[1];
	const int gridPos[3] = { cell % bx, (cell / bx) % by, cell / (bx * by) };

	vec3_t center;
	for ( int c = 0; c < 3; c++ )
		center[c] = world->lightGridOrigin[c] + gridPos[c] * world->lightGridSize[c];

	static const float corners[5][3] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 1.0f, 1.0f, 1.0f }, { 1.0f, -1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f }, { -1.0f, -1.0f, 1.0f } };
	int visible = 0;
	for ( int k = first; k < first + count; k++ )
	{
		vec3_t start;
		for ( int c = 0; c < 3; c++ )
			start[c] = center[c] + 0.5f * corners[k][c] * world->lightGridSize[c];
		if ( R_VolumetricSunVisible(start, sunDir) )
			visible++;
	}
	return visible;
}

static int R_VolumetricCompareFloats( const void *a, const void *b )
{
	const float fa = *(const float *)a;
	const float fb = *(const float *)b;
	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

/*
=================
R_BuildVolumetricStaticLighting
=================
*/
void R_BuildVolumetricStaticLighting( world_t *world )
{
	world->volumetricStaticGrid = NULL;
	world->volumetricSunGrid = NULL;
	world->volumetricDirMomentR = NULL;
	world->volumetricDirMomentG = NULL;
	world->volumetricDirMomentB = NULL;
	world->volumetricHasSunCells = qfalse;
	VectorClear(world->volumetricSunRadiance);
	world->particleLightReference = 0.0f;
	world->volumetricReconstructedSources = 0;
	world->volumetricReconstructedAreaSources = 0;
	world->volumetricDirectionalCells = 0;
	world->volumetricDirectionalEnergyFraction = 0.0f;
	R_ClearVolumetricStaticReconstruction();

	if ( r_volumetricFog->integer != 2 || !R_VolumetricFroxelEnabled() || !world->lightGridData ||
		world->numGridArrayElements <= 0 )
		return;

	const int numCells = world->numGridArrayElements;
	if ( numCells != world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] )
	{
		ri.Printf(PRINT_WARNING, "R_BuildVolumetricStaticLighting: light grid size mismatch, no sun split\n");
		return;
	}

	const int startTime = ri.Milliseconds();
	const int mode = R_VolumetricStaticDirectional() ? Com_Clampi(0, 2, r_volumetricFogStaticDirectional->integer) : 0;
	const qboolean splitSun = tr.sunParsed;
	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);

	// the light grid in the linear space of the legacy volumetric texture
	std::vector<float> ambient(numCells * 3), direct(numCells * 3), bspDir(numCells * 3);
	std::vector<float> sunAlign(splitSun ? numCells : 0), sunVis(splitSun ? numCells : 0);
	std::vector<uint8_t> valid(numCells);
	for ( int i = 0; i < numCells; i++ )
	{
		const mgrid_t *data = world->lightGridData + world->lightGridArray[i];
		if ( world->hdrLightGrid )
		{
			// dense per cell, not through lightGridArray
			const float *hdrData = world->hdrLightGrid + (i * 6);
			for ( int c = 0; c < 3; c++ )
			{
				ambient[i * 3 + c] = hdrData[c];
				direct[i * 3 + c] = hdrData[c + 3];
			}
		}
		else
		{
			for ( int c = 0; c < 3; c++ )
			{
				float a = data->ambientLight[0][c] / 255.0f;
				float d = data->directLight[0][c] / 255.0f;
				if ( tr.forcedLinearLight )
				{
					// the legacy texture is GL_SRGB8 then
					a = R_VolumetricSRGBToLinear(a);
					d = R_VolumetricSRGBToLinear(d);
				}
				ambient[i * 3 + c] = a;
				direct[i * 3 + c] = d;
			}
		}

		// direction towards the light, as R_SetupEntityLightingGrid (256 steps per turn)
		const float lat = data->latLong[1] * (2.0f * M_PI / 256.0f);
		const float lng = data->latLong[0] * (2.0f * M_PI / 256.0f);
		bspDir[i * 3 + 0] = cosf(lat) * sinf(lng);
		bspDir[i * 3 + 1] = sinf(lat) * sinf(lng);
		bspDir[i * 3 + 2] = cosf(lng);
		valid[i] = data->styles[0] != LS_LSNONE ? 1 : 0;
	}

	// sun alignment, and the sky visibility of the valid cells lit from the sun direction.
	// World geometry is static: traced once here instead of probing the cascades
	// per froxel every frame. Two passes: the centre ray of every candidate, then the
	// four corner rays only where the centre result changes between neighbours (window
	// edges, the rim of shadows); elsewhere the cell is all sky or all blocked.
	const int traceStart = ri.Milliseconds();
	s_vr.numTraced = s_vr.numSeeSky = s_vr.numRays = s_vr.numRefined = 0;
	if ( splitSun )
	{
		const int *bounds = world->lightGridBounds;
		// 0 = no candidate, 1 = centre blocked, 2 = centre sees the sky
		std::vector<uint8_t> centre(numCells, 0);
		for ( int i = 0; i < numCells; i++ )
		{
			const vec3_t cellDir = { bspDir[i * 3 + 0], bspDir[i * 3 + 1], bspDir[i * 3 + 2] };
			sunAlign[i] = valid[i] ? R_VolumetricSmoothstep(FROXEL_SUN_COS_OUTER, FROXEL_SUN_COS_INNER, DotProduct(cellDir, sunDir)) : 0.0f;
			sunVis[i] = 1.0f;
			const float *d = &direct[i * 3];
			if ( sunAlign[i] > 0.0f && (d[0] > 0.0f || d[1] > 0.0f || d[2] > 0.0f) )
			{
				centre[i] = R_VolumetricSunRays(world, i, sunDir, 0, 1) ? 2 : 1;
				s_vr.numRays++;
				s_vr.numTraced++;
			}
		}
		for ( int i = 0; i < numCells; i++ )
		{
			if ( !centre[i] )
				continue;
			const int x = i % bounds[0], y = (i / bounds[0]) % bounds[1], z = i / (bounds[0] * bounds[1]);
			const int nb[6][3] = { { x - 1, y, z }, { x + 1, y, z }, { x, y - 1, z }, { x, y + 1, z }, { x, y, z - 1 }, { x, y, z + 1 } };
			qboolean edge = qfalse;
			for ( int k = 0; k < 6 && !edge; k++ )
			{
				if ( nb[k][0] < 0 || nb[k][1] < 0 || nb[k][2] < 0 ||
					nb[k][0] >= bounds[0] || nb[k][1] >= bounds[1] || nb[k][2] >= bounds[2] )
					continue;
				const uint8_t other = centre[nb[k][0] + bounds[0] * (nb[k][1] + bounds[1] * nb[k][2])];
				if ( other && other != centre[i] )
					edge = qtrue;
			}
			const int seen = centre[i] == 2 ? 1 : 0;
			if ( edge )
			{
				sunVis[i] = (seen + R_VolumetricSunRays(world, i, sunDir, 1, 4)) / 5.0f;
				s_vr.numRays += 4;
				s_vr.numRefined++;
			}
			else
				sunVis[i] = (float)seen;
			if ( sunVis[i] > 0.0f )
				s_vr.numSeeSky++;
		}
	}
	s_vr.msecTrace = ri.Milliseconds() - traceStart;

	// static emitters anchor the reconstruction (mode 1 only)
	const int areaStart = ri.Milliseconds();
	std::vector<vrAreaSource> areas;
	s_vr.numAreaCandidates = 0;
	if ( mode == 1 )
		R_CollectStaticAreaSources(world, areas, &s_vr.numAreaCandidates);
	s_vr.numAreaSources = (int)areas.size();
	s_vr.msecAreaSources = ri.Milliseconds() - areaStart;

	vrInput in;
	Com_Memset(&in, 0, sizeof(in));
	for ( int c = 0; c < 3; c++ )
	{
		in.dims[c] = world->lightGridBounds[c];
		in.origin[c] = world->lightGridOrigin[c];
		in.cellSize[c] = world->lightGridSize[c];
	}
	in.hdr = world->hdrLightGrid != NULL;
	in.mode = mode;
	in.ambient = ambient.data();
	in.direct = direct.data();
	in.bspDir = bspDir.data();
	in.valid = valid.data();
	in.sunAlign = splitSun ? sunAlign.data() : NULL;
	in.sunVis = splitSun ? sunVis.data() : NULL;
	in.areas = areas.empty() ? NULL : areas.data();
	in.numAreas = (int)areas.size();

	vrOutput out;
	VR_Reconstruct(in, out);

	// the inputs are not needed any more: give the memory back before the packing
	// (a million cell grid holds a few hundred MB during the reconstruction)
	std::vector<float>().swap(ambient);
	std::vector<float>().swap(direct);
	std::vector<float>().swap(bspDir);
	std::vector<float>().swap(sunAlign);
	std::vector<float>().swap(sunVis);
	std::vector<vrAreaSource>().swap(areas);

	// half float texels, alpha = the sun fraction; the moments are shortened after the
	// rounding where needed (|M| <= B)
	const int uploadStart = ri.Milliseconds();
	std::vector<uint16_t> staticData, momentData[3];
	VR_PackHalf(out, out.sunFraction.data(), staticData, momentData);
	for ( int c = 0; c < 3; c++ )
		std::vector<float>().swap(out.moment[c]);
	std::vector<uint16_t> sunData(numCells * 4);
	s_vr.maxError = s_vr.maxRelError = 0.0f;
	double sumError = 0.0;
	for ( int i = 0; i < numCells; i++ )
	{
		for ( int c = 0; c < 3; c++ )
			sunData[i * 4 + c] = VR_FloatToHalf(out.sun[i * 3 + c]);
		sunData[i * 4 + 3] = VR_FloatToHalf(1.0f);

		// reconstruction error of the stored parts against the legacy value; the sun part
		// is counted as half here, its R11G11B10F texture adds its own rounding on the GPU
		// (debug view 25)
		for ( int c = 0; c < 3; c++ )
		{
			const float legacy = out.baseline[i * 3 + c] + out.sun[i * 3 + c];
			const float stored = VR_HalfToFloat(staticData[i * 4 + c]) + VR_HalfToFloat(sunData[i * 4 + c]);
			const float error = fabsf(stored - legacy);
			s_vr.maxError = MAX(s_vr.maxError, error);
			sumError += error;
			if ( legacy > 1e-3f )
				s_vr.maxRelError = MAX(s_vr.maxRelError, error / legacy);
		}
	}

	const int *b = world->lightGridBounds;
	world->volumetricStaticGrid = R_CreateImage3D("*volumetricStaticGrid", (byte *)staticData.data(),
		b[0], b[1], b[2], GL_RGBA16F);
	world->volumetricSunGrid = R_CreateImage3D("*volumetricSunGrid", (byte *)sunData.data(),
		b[0], b[1], b[2], GL_R11F_G11F_B10F);	// rgb only: half the size and bandwidth
	if ( mode != 0 )
	{
		// signed first moments, RGBA half data uploaded as RGB16F
		world->volumetricDirMomentR = R_CreateImage3D("*volumetricDirMomentR", (byte *)momentData[0].data(),
			b[0], b[1], b[2], GL_RGB16F);
		world->volumetricDirMomentG = R_CreateImage3D("*volumetricDirMomentG", (byte *)momentData[1].data(),
			b[0], b[1], b[2], GL_RGB16F);
		world->volumetricDirMomentB = R_CreateImage3D("*volumetricDirMomentB", (byte *)momentData[2].data(),
			b[0], b[1], b[2], GL_RGB16F);
	}
	s_vr.msecUpload = ri.Milliseconds() - uploadStart;
	std::vector<uint16_t>().swap(staticData);
	std::vector<uint16_t>().swap(sunData);
	for ( int c = 0; c < 3; c++ )
		std::vector<uint16_t>().swap(momentData[c]);

	// the light of an average place of the map (r_particleLighting reference)
	double referenceSum = 0.0;
	int numReferenceCells = 0;
	for ( int i = 0; i < numCells; i++ )
	{
		if ( !valid[i] )
			continue;
		const float *bl = &out.baseline[i * 3], *sl = &out.sun[i * 3];
		referenceSum += 0.2126f * (bl[0] + sl[0]) + 0.7152f * (bl[1] + sl[1]) + 0.0722f * (bl[2] + sl[2]);
		numReferenceCells++;
	}
	if ( numReferenceCells > 0 )
		world->particleLightReference = (float)(referenceSum / numReferenceCells);

	// realtime sun radiance: 90th percentile of the sunlit cells, with their
	// average color. A handful of cells is not a sun.
	std::vector<float> sunLuma;
	vec3_t sunColorSum = { 0.0f, 0.0f, 0.0f };
	for ( int i = 0; i < numCells; i++ )
	{
		const float *s = &out.sun[i * 3];
		const float luma = 0.2126f * s[0] + 0.7152f * s[1] + 0.0722f * s[2];
		if ( out.sunFraction[i] > 0.5f && luma > 0.0f )
		{
			sunLuma.push_back(luma);
			VectorAdd(sunColorSum, s, sunColorSum);
		}
	}
	const int numSunCells = (int)sunLuma.size();
	if ( numSunCells >= 16 )
	{
		qsort(sunLuma.data(), numSunCells, sizeof(float), R_VolumetricCompareFloats);
		const float percentile = sunLuma[(numSunCells * 9) / 10];
		const float sumLuma = 0.2126f * sunColorSum[0] + 0.7152f * sunColorSum[1] + 0.0722f * sunColorSum[2];
		if ( sumLuma > 0.0f )
		{
			VectorScale(sunColorSum, percentile / sumLuma, world->volumetricSunRadiance);
			world->volumetricHasSunCells = qtrue;
		}
	}

	world->volumetricReconstructedSources = out.stats.pointProxies;
	world->volumetricReconstructedAreaSources = out.stats.areaAnchors;
	world->volumetricDirectionalCells = out.stats.directionalCells;
	world->volumetricDirectionalEnergyFraction = out.stats.directionalFraction;

	s_vr.valid = qtrue;
	s_vr.mode = mode;
	s_vr.stats = out.stats;
	s_vr.proxies = out.proxies;
	s_vr.msecTotal = ri.Milliseconds() - startTime;

	ri.Printf(PRINT_DEVELOPER, "Froxel fog light grid: %d cells, %d sunlit, sun radiance %.3f %.3f %.3f\n",
		numCells, numSunCells, world->volumetricSunRadiance[0],
		world->volumetricSunRadiance[1], world->volumetricSunRadiance[2]);
	ri.Printf(PRINT_DEVELOPER, "Froxel fog sun visibility: %d cells traced (%d refined, %d rays), %d see the sky, %d msec\n",
		s_vr.numTraced, s_vr.numRefined, s_vr.numRays, s_vr.numSeeSky, s_vr.msecTrace);
	ri.Printf(PRINT_DEVELOPER, "Froxel fog baked split: error max %g (%.3f%%), mean %g\n",
		s_vr.maxError, s_vr.maxRelError * 100.0f, sumError / (3.0 * numCells));
	if ( mode != 0 )
	{
		ri.Printf(PRINT_DEVELOPER, "Froxel fog directional baked light (mode %d): %d point proxies, %d area anchors "
			"(of %d), %d directional cells, %.1f%% of the baked light directional, %d msec\n",
			mode, out.stats.pointProxies, out.stats.areaAnchors, s_vr.numAreaSources, out.stats.directionalCells,
			out.stats.directionalFraction * 100.0f, s_vr.msecTotal);
	}
}

/*
=================
R_VolumetricStaticStats_f

r_vfogStaticStats [proxies]: the directional baked light reconstruction of the
loaded map (r_volumetricFogStaticDirectional)
=================
*/
void R_VolumetricStaticStats_f( void )
{
	if ( !s_vr.valid )
	{
		ri.Printf(PRINT_ALL, "r_vfogStaticStats: no froxel fog static light built (r_volumetricFog 2, a map with a light grid)\n");
		return;
	}
	const vrStats& st = s_vr.stats;
	static const char *modeNames[] = { "off", "reconstructed moments", "raw BSP direction moments" };
	ri.Printf(PRINT_ALL, "r_volumetricFogStaticDirectional %d (%s)\n", s_vr.mode, modeNames[Com_Clampi(0, 2, s_vr.mode)]);
	ri.Printf(PRINT_ALL, "  probes          %d cells, %d valid\n", st.cells, st.validCells);
	ri.Printf(PRINT_ALL, "  sun visibility  %d cells traced (%d refined at edges, %d rays), %d see the sky\n",
		s_vr.numTraced, s_vr.numRefined, s_vr.numRays, s_vr.numSeeSky);
	ri.Printf(PRINT_ALL, "  split error     max %g (%.3f%% relative), float %g\n", s_vr.maxError, s_vr.maxRelError * 100.0f,
		st.maxSplitError);
	if ( s_vr.mode == 1 )
	{
		ri.Printf(PRINT_ALL, "  seeds           %d, %d fitted, %d merged into a stronger fit\n", st.seeds, st.fits, st.mergedFits);
		ri.Printf(PRINT_ALL, "  point proxies   %d (%d sources over the budget dropped)\n", st.pointProxies, st.droppedSources);
		ri.Printf(PRINT_ALL, "  area anchors    %d accepted of %d static sources (%d emissive candidates)\n",
			st.areaAnchors, s_vr.numAreaSources, s_vr.numAreaCandidates);
		ri.Printf(PRINT_ALL, "  ray residual    mean %.3f, P95 %.3f cell diagonals\n", st.meanRayRms, st.p95RayRms);
		ri.Printf(PRINT_ALL, "  position sigma  mean %.3f cell diagonals\n", st.meanSigmaP);
	}
	if ( s_vr.mode != 0 )
	{
		ri.Printf(PRINT_ALL, "  directional     %d cells, %.1f%% of the baked light attributed, %.1f%% directional after cancellation\n",
			st.directionalCells, st.attributedFraction * 100.0f, st.directionalFraction * 100.0f);
	}
	ri.Printf(PRINT_ALL, "  msec            total %d: sun trace %d, area sources %d, split %.1f, gradients %.1f, seeds %.1f, "
		"fit %.1f, anchors %.1f, attribution %.1f, upload %d\n",
		s_vr.msecTotal, s_vr.msecTrace, s_vr.msecAreaSources, st.msecSplit, st.msecGradients, st.msecSeeds, st.msecFit,
		st.msecAreas, st.msecAttribution, s_vr.msecUpload);

	if ( ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "proxies") )
	{
		for ( size_t k = 0; k < s_vr.proxies.size(); k++ )
		{
			const vrProxy& p = s_vr.proxies[k];
			ri.Printf(PRINT_ALL, "  %3d %-5s (%.0f %.0f %.0f) color %.2f %.2f %.2f confidence %.2f sigma %.0f range %.0f support %d",
				(int)k, p.type == VR_SOURCE_RECT ? "rect" : "point", p.position[0], p.position[1], p.position[2],
				p.color[0], p.color[1], p.color[2], p.confidence, p.sigmaP, p.range, p.support);
			if ( p.type == VR_SOURCE_RECT )
				ri.Printf(PRINT_ALL, " size %.0f x %.0f%s", 2.0f * p.halfWidth, 2.0f * p.halfHeight, p.twoSided ? " two-sided" : "");
			ri.Printf(PRINT_ALL, "\n");
		}
	}
	else
		ri.Printf(PRINT_ALL, "  (r_vfogStaticStats proxies: list the sources)\n");
}
