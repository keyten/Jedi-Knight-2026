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

// Static Lighting Reconstruction: the persistent result of one map load
// (tr_staticlighting.cpp), read by its consumers

#pragma once

#include "tr_volrecon.h"

#include <vector>

#define STATIC_LIGHTS_MAX_PROMOTED	32	// r_recoveredVolumetricMaxLights cap
#define STATIC_PORTALS_MAX			32	// light portals per map (tr_volrecon.cpp PORTAL_MAX)

struct staticLight_t
{
	vrStaticLight light;
	int cluster;		// BSP cluster of the origin, -1 in solid / no vis
	int area;
};

struct staticLightPortal_t
{
	vrLightPortal portal;
	int frontCluster, backCluster;	// receiver side / source side of the aperture
	int frontArea, backArea;
};

struct staticLightingUploadStats_t
{
	float maxError;		// |B + P + S - legacy| after the half packing
	float maxRelError;
	int msec;
};

// all structured lights of the loaded world (empty when not built)
const std::vector<staticLight_t>& R_StaticLights( void );
const std::vector<staticLightPortal_t>& R_StaticLightPortals( void );
qboolean R_StaticLightCulled( const staticLight_t& light, int viewCluster );
int R_StaticViewCluster( const vec3_t origin );

// tr_volumetric_reconstruct.cpp: the froxel fog adapter
void R_ClearVolumetricStaticLighting( world_t *world );
void R_UploadVolumetricStaticLighting( world_t *world, const vrOutput& out, const uint8_t *valid,
	qboolean moments, staticLightingUploadStats_t *stats );
