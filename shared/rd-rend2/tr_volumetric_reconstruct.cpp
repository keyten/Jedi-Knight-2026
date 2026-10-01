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
Froxel fog adapter of the Static Lighting Reconstruction (tr_staticlighting.cpp,
r_volumetricFog 2): the reconstructed baked field uploaded as the textures of the
injection, once per map load. The reconstruction is not built here.

The legacy volumetric light map merges the ambient and directed light of every
light grid cell (volumetricLightMaps[0], R_BuildLightGridTexture). Textures with
its layout, so that B + P + S == the legacy value:

  volumetricSunGrid     S = the directed light of cells lit from the sun direction
                        (alignment smoothstep 25..10 degrees) times the part of the
                        cell that sees the sky: an indoor lamp that happens to be
                        aligned with the sun stays static light
  volumetricStaticGrid  B = everything else (rgb) but the promoted lights P, sun
                        fraction f = alignment * sky visibility (a): inside the
                        cascades the realtime sun replaces S with f * sun * shadow, so
                        B + f * sun never counts the (1 - f) D kept in B twice
  volumetricDirMoment*  (mode 1, 2) the first angular moments of the attributed part
                        of B per colour channel, |M_c| <= B_c: the injection applies
                        B + 3 g (M.v) with |g| <= 1/3 (the L1 Henyey-Greenstein
                        response), so the mean over all directions stays B

P (r_recoveredVolumetricLights) is not stored: the froxel light lists inject the
promoted lights exactly (R_VolumetricBuildLightLists). Cells in walls (styles[0] ==
LS_LSNONE) keep their light isotropic. The realtime sun radiance is estimated from
the sunlit cells, so the beams have the brightness the map was compiled with.
See docs/rend2-volumetric-fog.md and docs/rend2-static-lighting.md.
*/

#include "tr_local.h"
#include "tr_volrecon.h"
#include "tr_staticlighting.h"

#include <algorithm>
#include <vector>

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


void R_ClearVolumetricStaticLighting( world_t *world )
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
}

static int R_VolumetricCompareFloats( const void *a, const void *b )
{
	const float fa = *(const float *)a, fb = *(const float *)b;
	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

/*
=================
R_UploadVolumetricStaticLighting
=================
*/
void R_UploadVolumetricStaticLighting( world_t *world, const vrOutput& out, const uint8_t *valid,
	qboolean moments, staticLightingUploadStats_t *stats )
{
	const int numCells = world->numGridArrayElements;
	// the reconstruction may run for other consumers while the fog itself is isotropic
	const int mode = (moments && !out.moment[0].empty()) ? 1 : 0;

	// half float texels, alpha = the sun fraction; the moments are shortened after the
	// rounding where needed (|M| <= B)
	const int uploadStart = ri.Milliseconds();
	std::vector<uint16_t> staticData, momentData[3];
	VR_PackHalf(out, out.sunFraction.data(), staticData, momentData);
	std::vector<uint16_t> sunData(numCells * 4);
	stats->maxError = stats->maxRelError = 0.0f;
	double sumError = 0.0;
	for ( int i = 0; i < numCells; i++ )
	{
		for ( int c = 0; c < 3; c++ )
			sunData[i * 4 + c] = VR_FloatToHalf(out.sun[i * 3 + c]);
		sunData[i * 4 + 3] = VR_FloatToHalf(1.0f);

		// reconstruction error of the stored parts against the legacy value (the promoted part P
		// is injected as exact lights); the sun part is counted as half here, its R11G11B10F
		// texture adds its own rounding on the GPU (debug view 25)
		for ( int c = 0; c < 3; c++ )
		{
			const float promoted = out.promoted.empty() ? 0.0f : out.promoted[i * 3 + c];
			const float legacy = out.baseline[i * 3 + c] + promoted + out.sun[i * 3 + c];
			const float stored = VR_HalfToFloat(staticData[i * 4 + c]) + promoted + VR_HalfToFloat(sunData[i * 4 + c]);
			const float error = fabsf(stored - legacy);
			stats->maxError = MAX(stats->maxError, error);
			sumError += error;
			if ( legacy > 1e-3f )
				stats->maxRelError = MAX(stats->maxRelError, error / legacy);
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
	stats->msec = ri.Milliseconds() - uploadStart;
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
		// the whole baked light: B + P + S
		float l[3];
		for ( int c = 0; c < 3; c++ )
			l[c] = out.baseline[i * 3 + c] + out.sun[i * 3 + c] + (out.promoted.empty() ? 0.0f : out.promoted[i * 3 + c]);
		referenceSum += 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
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

	ri.Printf(PRINT_DEVELOPER, "Froxel fog light grid: %d sunlit cells, sun radiance %.3f %.3f %.3f, split error max %g "
		"(%.3f%%), mean %g\n", numSunCells, world->volumetricSunRadiance[0], world->volumetricSunRadiance[1],
		world->volumetricSunRadiance[2], stats->maxError, stats->maxRelError * 100.0f, sumError / (3.0 * numCells));
}
