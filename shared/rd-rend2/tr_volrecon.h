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

Structured lights (docs/rend2-static-lighting.md): every point proxy is checked
against the geometry (in.trace), fitted with the runtime point / spot attenuation
and classified. With in.promote, the confident physical lights take their modelled
part P out of the baked light (B' = B - P, moments rebuilt from the rest), so that
B' + L1(M') + the exact lights is the original energy:

  B' + P + S == legacy, P <= Q, |M'_c| <= B'_c
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
	// structured lights (mode 1): visibility between two points, 1 = clear, 0 = blocked or
	// starting in solid. Without it no light is validated or promoted.
	float (*trace)( void *user, const float start[3], const float end[3] );
	void *traceUser;
	const uint8_t *animated;	// 1 per cell, 1 = lit by an animated light style; may be null
	bool promote;				// move the promoted lights out of B / M
	float physicalThreshold;	// physicalConfidence above which a light is physical (0: default)
	float spotThreshold;		// spotConfidence above which a physical light is a spot (0: default)
	int maxPromoted;			// promoted light budget (0: default)
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

enum vrLightKind
{
	VR_LIGHT_TRANSPORT,		// light arrives from here, no lamp is known to be here
	VR_LIGHT_POINT,
	VR_LIGHT_SPOT,
	VR_LIGHT_RECT
};

enum vrLightFlags
{
	VR_LIGHT_PHYSICAL	= 1 << 0,	// physicalConfidence above the threshold
	VR_LIGHT_PROMOTED	= 1 << 1,	// an exact light, its part is out of B / M
	VR_LIGHT_RELOCATED	= 1 << 2,	// moved within sigmaP to where it sees its probes
	VR_LIGHT_ANIMATED	= 1 << 3	// lit cells of an animated style: never promoted
};

// a structured light (one per proxy), in the linear units of the grid
struct vrStaticLight
{
	vrLightKind kind;
	int flags;
	int proxy;					// index into vrOutput::proxies
	float origin[3];
	// runtime light: color * clamp(0.5 R^2 / r^2 - 0.5, 0, 1) * smoothstep(cosOuter, cosInner, cos),
	// the mean over the view directions (the froxel phase averages to 1). Already scaled by
	// promotionWeight.
	float color[3];
	float radius;				// R
	float confidence;			// reconstruction (the proxy)
	float visibility;			// energy weighted direct visibility of its probes
	float physicalConfidence;
	float radiometricConfidence;
	float spotConfidence;
	float promotionWeight;		// 0 when not promoted
	float explainedEnergy;		// sum of the attributed luminance
	float sigmaP;
	float fitError;				// weighted relative rms of the runtime model
	float leakFraction;			// modelled light at blocked probes / all modelled light
	float excessFraction;		// modelled light above the budget / modelled light at visible probes
	float axis[3];				// spot
	float cosInner;
	float cosOuter;
	// VR_LIGHT_RECT
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
	float msecLights;
	float msecTotal;
	// structured lights
	int traces;
	int physicalLights;
	int promotedPoints;
	int promotedSpots;
	int transportLights;
	int relocatedLights;
	float promotedFraction;		// sum lum P / sum lum B (before)
	float excessFraction;		// modelled light above the budget / promoted light
	float maxPartitionError;	// max |B' + P - B|
};

struct vrOutput
{
	std::vector<float> baseline;	// B, 3 per cell
	std::vector<float> sun;			// S, 3 per cell
	std::vector<float> sunFraction;	// 1 per cell, 0 in wall cells
	std::vector<float> moment[3];	// M_R, M_G, M_B: 3 per cell each (empty with mode 0)
	std::vector<float> promoted;	// P, 3 per cell (empty when nothing is promoted)
	std::vector<vrProxy> proxies;
	std::vector<vrStaticLight> lights;
	vrStats stats;
};

void VR_Reconstruct( const vrInput& in, vrOutput& out );

// IEEE half, round to nearest even, finite values clamped to +-65504
uint16_t VR_FloatToHalf( float f );
float VR_HalfToFloat( uint16_t h );

// legacy = B + P + S (not stored)
// RGBA half texels: baseline B (alpha = alpha[i], or 1 without it) and, when the
// output has them, the three moments (alpha 0). Moments are shortened after the
// rounding where needed, so that |M_c| <= B_c also holds for the half values.
void VR_PackHalf( const vrOutput& out, const float *alpha,
	std::vector<uint16_t>& baseline, std::vector<uint16_t> moments[3] );
