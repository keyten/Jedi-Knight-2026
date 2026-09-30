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
Directional baked volumetric lighting reconstruction (r_volumetricFogStaticDirectional),
the pure part: no renderer globals, so tools/volrecon_test links it alone.

Input: the light grid as linear ambient A / directed D / BSP direction per dense
cell, the sun alignment and sky visibility of each cell, known static area
emitters. Output per cell:

  S  = baked sun part (sunFraction * D, sunFraction = alignment * visibility)
  B  = legacy - S, the complete non-sun baseline (isotropic mean)
  M_c = first angular moment of the confidently attributed part of B, per
        colour channel c (xyz = sum of energy * direction towards the light)

with |M_c| <= B_c, so B + 3 g M.v >= 0 for |g| <= 1/3 (the L1 Henyey-Greenstein
response the froxel fog applies). The moments never add energy: their mean
over the sphere is zero. See docs/rend2-volumetric-fog.md.
*/

#pragma once

#include <cstdint>
#include <vector>

// a static rectangle emitter (tr_arealights.cpp), normal = cross(right, up)
struct vrAreaSource
{
	float center[3];
	float right[3];		// unit
	float up[3];		// unit
	float halfWidth;
	float halfHeight;
	float color[3];		// linear, only the chromaticity is used
	float confidence;	// 0..1
	bool twoSided;
};

struct vrInput
{
	int dims[3];
	float origin[3];		// world position of cell (0, 0, 0)
	float cellSize[3];
	bool hdr;				// legacy = A + D (HDR) or max(A, D) (LDR)
	int mode;				// 0 split only, 1 reconstructed moments, 2 raw BSP direction moments
	const float *ambient;	// 3 per cell, linear
	const float *direct;	// 3 per cell, linear
	const float *bspDir;	// 3 per cell, unit, towards the light
	const uint8_t *valid;	// 1 per cell, 0 = in a wall (styles[0] == LS_LSNONE)
	const float *sunAlign;	// 1 per cell 0..1, may be null (no sun)
	const float *sunVis;	// 1 per cell 0..1, may be null (= 1)
	const vrAreaSource *areas;
	int numAreas;
};

enum vrSourceType
{
	VR_SOURCE_POINT,
	VR_SOURCE_RECT
};

// an accepted source proxy (point) or area anchor; self-contained, it outlives vrInput
struct vrProxy
{
	vrSourceType type;
	float position[3];
	float color[3];			// chromaticity, max component 1
	float confidence;
	float sigmaP;			// positional uncertainty, world units
	float rayRms;			// weighted ray residual, world units (point proxies)
	float range;			// attribution range, world units
	int area;				// index into vrInput::areas, -1 for a point proxy
	int support;			// support probes
	// VR_SOURCE_RECT: the emitter (copied from the area source)
	float right[3];
	float up[3];
	float halfWidth;
	float halfHeight;
	bool twoSided;
};

struct vrStats
{
	int cells;
	int mergedFits;
	int droppedSources;		// over the global PROXY budget
	int validCells;
	int seeds;
	int fits;
	int pointProxies;
	int areaAnchors;
	int directionalCells;
	float meanRayRms;			// in cell diagonals
	float p95RayRms;			// in cell diagonals
	float meanSigmaP;			// in cell diagonals
	float directionalFraction;	// sum lum |moments| / sum lum B (after cancellation)
	float attributedFraction;	// sum lum E / sum lum B (before cancellation)
	float maxSplitError;		// max |B + S - legacy| (float)
	float msecSplit;
	float msecGradients;
	float msecSeeds;
	float msecFit;
	float msecAreas;
	float msecAttribution;
	float msecTotal;
};

struct vrOutput
{
	std::vector<float> baseline;	// B, 3 per cell
	std::vector<float> sun;			// S, 3 per cell
	std::vector<float> sunFraction;	// 1 per cell, 0 in wall cells
	std::vector<float> moment[3];	// M_R, M_G, M_B: 3 per cell each (empty with mode 0)
	std::vector<vrProxy> proxies;
	vrStats stats;
};

void VR_Reconstruct( const vrInput& in, vrOutput& out );

// IEEE half, round to nearest even, finite values clamped to +-65504
uint16_t VR_FloatToHalf( float f );
float VR_HalfToFloat( uint16_t h );

// legacy = B + S (not stored)
// RGBA half texels: baseline B (alpha = alpha[i], or 1 without it) and, when the
// output has them, the three moments (alpha 0). Moments are shortened after the
// rounding where needed, so that |M_c| <= B_c also holds for the half values.
void VR_PackHalf( const vrOutput& out, const float *alpha,
	std::vector<uint16_t>& baseline, std::vector<uint16_t> moments[3] );
