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

// Legacy fog baseline and authored-sun separation only. No recovered sources.
#include "tr_local.h"
#include "tr_bakedlighting.h"
#include <algorithm>
#include <vector>

#define STATIC_SUN_COS_OUTER 0.9063f
#define STATIC_SUN_COS_INNER 0.9848f

static float R_BakedSRGBToLinear(float c)
{
	return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float R_BakedSmoothstep(float edge0, float edge1, float x)
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

// rays of the light grid cell towards the sun: its centre and four corners of a
// tetrahedron half a cell out; returns how many of rays [first, first + count) see the sky
static int R_BakedSunRays(const world_t *world, int cell, const vec3_t sunDir, int first, int count)
{
	static const float corners[5][3] = {
		{0, 0, 0}, {1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
	const int *bounds = world->lightGridBounds;
	const int x = cell % bounds[0], y = (cell / bounds[0]) % bounds[1],
			  z = cell / (bounds[0] * bounds[1]);
	int seen = 0;
	for (int k = first; k < first + count; k++)
	{
		vec3_t p;
		p[0] = world->lightGridOrigin[0] + (x + 0.5f * corners[k][0]) * world->lightGridSize[0];
		p[1] = world->lightGridOrigin[1] + (y + 0.5f * corners[k][1]) * world->lightGridSize[1];
		p[2] = world->lightGridOrigin[2] + (z + 0.5f * corners[k][2]) * world->lightGridSize[2];
		if (R_SunVisibleFromPoint(p, sunDir))
			seen++;
	}
	return seen;
}

static void R_BakedGridDirection(const world_t *world, int i, vec3_t dir)
{
	const mgrid_t *data = world->lightGridData + world->lightGridArray[i];
	const float lat = data->latLong[1] * (2.0f * M_PI / 256.0f);
	const float lng = data->latLong[0] * (2.0f * M_PI / 256.0f);
	VectorSet(dir, cosf(lat) * sinf(lng), sinf(lat) * sinf(lng), cosf(lng));
}
static void R_BakedGridColor(const world_t *world, int i, vec3_t ambient, vec3_t direct)
{
	const mgrid_t *data = world->lightGridData + world->lightGridArray[i];
	for (int c = 0; c < 3; ++c)
	{
		float a = world->hdrLightGrid ? world->hdrLightGrid[i * 6 + c]
									  : data->ambientLight[0][c] / 255.0f;
		float d = world->hdrLightGrid ? world->hdrLightGrid[i * 6 + c + 3]
									  : data->directLight[0][c] / 255.0f;
		if (!world->hdrLightGrid && tr.forcedLinearLight)
		{
			a = R_BakedSRGBToLinear(a);
			d = R_BakedSRGBToLinear(d);
		}
		ambient[c] = a;
		direct[c] = d;
	}
}

// Map-owned images follow the renderer image registry lifetime. CPU vectors are
// local and released on return; no observation buffers or canonical asset refs.
void R_BuildBakedFogLighting(world_t *world)
{
	world->volumetricStaticGrid = world->volumetricSunGrid = nullptr;
	world->volumetricHasSunCells = qfalse;
	VectorClear(world->volumetricSunRadiance);
	world->particleLightReference = 0;
	if (r_volumetricFog->integer != 2 || !R_VolumetricFroxelEnabled())
		return;
	const int numCells = world->numGridArrayElements;
	if (!world->lightGridData || !world->lightGridArray || numCells <= 0)
		return;
	size_t count = 1;
	for (int c = 0; c < 3; ++c)
	{
		if (world->lightGridBounds[c] <= 0 ||
			count > size_t(numCells) / size_t(world->lightGridBounds[c]))
			return;
		count *= size_t(world->lightGridBounds[c]);
	}
	if (count != size_t(numCells))
		return;
	const int start = ri.Milliseconds();
	const qboolean splitSun = tr.sunParsed;
	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);
	std::vector<uint8_t> valid(numCells);
	std::vector<float> sunAlign(splitSun ? numCells : 0), sunVis(splitSun ? numCells : 0);
	for (int i = 0; i < numCells; ++i)
		valid[i] = world->lightGridData[world->lightGridArray[i]].styles[0] != LS_LSNONE;
	// This is the legacy presentation mask, never evidence that a grid cell is free space.
	int numRays = 0, numTraced = 0, numRefined = 0, numSeeSky = 0;

	// sun alignment, and the sky visibility of the valid cells lit from the sun direction.
	// World geometry is static: traced once here instead of probing the cascades
	// per froxel every frame. Two passes: the centre ray of every candidate, then the
	// four corner rays only where the centre result changes between neighbours (window
	// edges, the rim of shadows); elsewhere the cell is all sky or all blocked.
	if (splitSun)
	{
		const int *bounds = world->lightGridBounds;
		// 0 = no candidate, 1 = centre blocked, 2 = centre sees the sky
		std::vector<uint8_t> centre(numCells, 0);
		for (int i = 0; i < numCells; i++)
		{
			vec3_t cellDir;
			R_BakedGridDirection(world, i, cellDir);
			sunAlign[i] = valid[i] ? R_BakedSmoothstep(STATIC_SUN_COS_OUTER, STATIC_SUN_COS_INNER,
													   DotProduct(cellDir, sunDir))
								   : 0.0f;
			sunVis[i] = 1.0f;
			vec3_t a, d;
			R_BakedGridColor(world, i, a, d);
			if (sunAlign[i] > 0.0f && (d[0] > 0.0f || d[1] > 0.0f || d[2] > 0.0f))
			{
				centre[i] = R_BakedSunRays(world, i, sunDir, 0, 1) ? 2 : 1;
				numRays++;
				numTraced++;
			}
		}
		for (int i = 0; i < numCells; i++)
		{
			if (!centre[i])
				continue;
			const int x = i % bounds[0], y = (i / bounds[0]) % bounds[1],
					  z = i / (bounds[0] * bounds[1]);
			const int nb[6][3] = {{x - 1, y, z}, {x + 1, y, z}, {x, y - 1, z},
								  {x, y + 1, z}, {x, y, z - 1}, {x, y, z + 1}};
			qboolean edge = qfalse;
			for (int k = 0; k < 6 && !edge; k++)
			{
				if (nb[k][0] < 0 || nb[k][1] < 0 || nb[k][2] < 0 || nb[k][0] >= bounds[0] ||
					nb[k][1] >= bounds[1] || nb[k][2] >= bounds[2])
					continue;
				const uint8_t other =
					centre[nb[k][0] + bounds[0] * (nb[k][1] + bounds[1] * nb[k][2])];
				if (other && other != centre[i])
					edge = qtrue;
			}
			const int seen = centre[i] == 2 ? 1 : 0;
			if (edge)
			{
				sunVis[i] = (seen + R_BakedSunRays(world, i, sunDir, 1, 4)) / 5.0f;
				numRays += 4;
				numRefined++;
			}
			else
				sunVis[i] = (float)seen;
			if (sunVis[i] > 0.0f)
				numSeeSky++;
		}
	}
	std::vector<uint16_t> baseline(size_t(numCells) * 4), sun(size_t(numCells) * 4);
	std::vector<float> sunLuma;
	vec3_t sunColorSum = {0, 0, 0};
	double referenceSum = 0;
	int referenceCount = 0;
	for (int i = 0; i < numCells; ++i)
	{
		vec3_t a, d, b, s;
		R_BakedGridColor(world, i, a, d);
		const float f = splitSun && valid[i] ? sunAlign[i] * sunVis[i] : 0;
		for (int c = 0; c < 3; ++c)
		{
			Rend2BakedLighting::Split(a[c], d[c], f, world->hdrLightGrid != nullptr, b[c], s[c]);
			baseline[size_t(i) * 4 + c] = Rend2BakedLighting::FloatToHalf(b[c]);
			sun[size_t(i) * 4 + c] = Rend2BakedLighting::FloatToHalf(s[c]);
		}
		baseline[size_t(i) * 4 + 3] = Rend2BakedLighting::FloatToHalf(f);
		sun[size_t(i) * 4 + 3] = Rend2BakedLighting::FloatToHalf(1);
		if (valid[i])
		{
			referenceSum +=
				.2126f * (b[0] + s[0]) + .7152f * (b[1] + s[1]) + .0722f * (b[2] + s[2]);
			++referenceCount;
		}
		const float luma = .2126f * s[0] + .7152f * s[1] + .0722f * s[2];
		if (f > .5f && luma > 0)
		{
			sunLuma.push_back(luma);
			VectorAdd(sunColorSum, s, sunColorSum);
		}
	}
	if (referenceCount)
		world->particleLightReference = float(referenceSum / referenceCount);
	const int *b = world->lightGridBounds;
	world->volumetricStaticGrid = R_CreateImage3D("*volumetricStaticGrid", (byte *)baseline.data(),
												  b[0], b[1], b[2], GL_RGBA16F);
	world->volumetricSunGrid = R_CreateImage3D("*volumetricSunGrid", (byte *)sun.data(), b[0], b[1],
											   b[2], GL_R11F_G11F_B10F);
	if (sunLuma.size() >= 16)
	{
		std::sort(sunLuma.begin(), sunLuma.end());
		const float sumLuma =
			.2126f * sunColorSum[0] + .7152f * sunColorSum[1] + .0722f * sunColorSum[2];
		if (sumLuma > 0)
		{
			VectorScale(sunColorSum, sunLuma[sunLuma.size() * 9 / 10] / sumLuma,
						world->volumetricSunRadiance);
			world->volumetricHasSunCells = qtrue;
		}
	}
	ri.Printf(
		PRINT_DEVELOPER,
		"Baked fog grid: %d cells, %d sun candidates, %d edge cells, %d rays, %d see sky, %d ms\n",
		numCells, numTraced, numRefined, numRays, numSeeSky, ri.Milliseconds() - start);
}
