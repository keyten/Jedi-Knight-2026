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
Static Lighting Reconstruction (r_staticLightReconstruction), built once per map
load and owned here, not by its consumers. It reads the light grid, traces the
sky visibility of the cells lit from the sun direction, collects the static area
emitters and runs the reconstruction of tr_volrecon.cpp (pure, tested by
tools/volrecon_test) with the BSP as its visibility oracle. It keeps:

  the low frequency baked field  B / S / M, handed to the froxel fog adapter
                                 (tr_volumetric_reconstruct.cpp) for upload
  structured lights              one per source proxy: transport, point, spot or
                                 rect, with transport and physical confidence, and
                                 the BSP cluster / area of the light

With r_recoveredVolumetricLights the confident physical point / spot lights are
promoted: their modelled light leaves B and M, and the froxel fog injects them as
exact lights instead (R_VolumetricBuildLightLists), so the baked energy is never
counted twice. The light grid is static, so nothing here runs per frame except
the r_staticLightDebug drawing. No disk cache yet: the cost is paid at every map
load. See docs/rend2-static-lighting.md.
*/

#include "tr_local.h"
#include "tr_volrecon.h"
#include "tr_staticlighting.h"

#include <algorithm>
#include <vector>

// light grid cells whose light comes from within ~10 degrees of the sun
// direction are sun, beyond ~25 degrees not
#define STATIC_SUN_COS_OUTER 0.9063f	// cos(25)
#define STATIC_SUN_COS_INNER 0.9848f	// cos(10)
#define STATIC_SUN_TRACE_DISTANCE 65536.0f

static struct
{
	qboolean valid;
	int mode;
	qboolean promote;
	vrStats stats;
	std::vector<vrProxy> proxies;
	std::vector<staticLight_t> lights;
	int numAreaSources;
	int numAreaCandidates;
	int numTraced;		// candidate cells
	int numRefined;		// cells that got the four corner rays
	int numRays;
	int numSeeSky;

	int msecTrace;
	int msecAreaSources;
	int msecReconstruct;
	int msecTotal;
	staticLightingUploadStats_t upload;
} s_sl;

void R_ClearStaticLighting( void )
{
	s_sl.valid = qfalse;
	s_sl.mode = 0;
	s_sl.promote = qfalse;
	s_sl.numAreaSources = s_sl.numAreaCandidates = 0;
	s_sl.numTraced = s_sl.numRefined = s_sl.numRays = s_sl.numSeeSky = 0;
	s_sl.msecTrace = s_sl.msecAreaSources = s_sl.msecReconstruct = s_sl.msecTotal = 0;
	Com_Memset(&s_sl.stats, 0, sizeof(s_sl.stats));
	Com_Memset(&s_sl.upload, 0, sizeof(s_sl.upload));
	std::vector<vrProxy>().swap(s_sl.proxies);
	std::vector<staticLight_t>().swap(s_sl.lights);
}

const std::vector<staticLight_t>& R_StaticLights( void )
{
	return s_sl.lights;
}

static float R_StaticSRGBToLinear( float c )
{
	return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float R_StaticSmoothstep( float edge0, float edge1, float x )
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

static void R_StaticTrace( trace_t *trace, const vec3_t start, const vec3_t end )
{
	Com_Memset(trace, 0, sizeof(*trace));
#ifdef REND2_SP
	ri.SV_Trace(trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
}

static qboolean R_StaticSunVisible( const vec3_t start, const vec3_t sunDir )
{
	vec3_t end;
	VectorMA(start, STATIC_SUN_TRACE_DISTANCE, sunDir, end);
	trace_t trace;
	R_StaticTrace(&trace, start, end);
	if ( trace.startsolid || trace.allsolid )
		return qfalse;
	// the sky brushes are solid: reaching one is reaching the sky
	return (qboolean)(trace.fraction >= 1.0f || (trace.surfaceFlags & SURF_SKY));
}

// rays of the light grid cell towards the sun: its centre and four corners of a
// tetrahedron half a cell out; returns how many of rays [first, first + count) see the sky
static int R_StaticSunRays( const world_t *world, int cell, const vec3_t sunDir, int first, int count )
{
	static const float corners[5][3] = { { 0, 0, 0 }, { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } };
	const int *bounds = world->lightGridBounds;
	const int x = cell % bounds[0], y = (cell / bounds[0]) % bounds[1], z = cell / (bounds[0] * bounds[1]);
	int seen = 0;
	for ( int k = first; k < first + count; k++ )
	{
		vec3_t p;
		p[0] = world->lightGridOrigin[0] + (x + 0.5f * corners[k][0]) * world->lightGridSize[0];
		p[1] = world->lightGridOrigin[1] + (y + 0.5f * corners[k][1]) * world->lightGridSize[1];
		p[2] = world->lightGridOrigin[2] + (z + 0.5f * corners[k][2]) * world->lightGridSize[2];
		if ( R_StaticSunVisible(p, sunDir) )
			seen++;
	}
	return seen;
}

// the reconstruction's visibility oracle: world brushes only, 0 from inside a wall
static float R_StaticLightTrace( void *user, const float start[3], const float end[3] )
{
	(void)user;
	trace_t trace;
	R_StaticTrace(&trace, start, end);
	if ( trace.startsolid || trace.allsolid )
		return 0.0f;
	return trace.fraction >= 1.0f ? 1.0f : 0.0f;
}

// the leaf of a point (R_PointInLeaf works on tr.world, not yet set while loading)
static const mnode_t *R_StaticPointInLeaf( const world_t *world, const vec3_t p )
{
	const mnode_t *node = world->nodes;
	while ( node && node->contents == -1 )
	{
		const float d = DotProduct(p, node->plane->normal) - node->plane->dist;
		node = node->children[d >= 0.0f ? 0 : 1];
	}
	return node;
}

static int R_StaticCompareFloats( const void *a, const void *b )
{
	const float fa = *(const float *)a, fb = *(const float *)b;
	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

/*
=================
R_BuildStaticLighting
=================
*/
void R_BuildStaticLighting( world_t *world )
{
	R_ClearStaticLighting();
	R_ClearVolumetricStaticLighting(world);

	// v1: the froxel fog is the only consumer
	if ( r_volumetricFog->integer != 2 || !R_VolumetricFroxelEnabled() || !world->lightGridData ||
		world->numGridArrayElements <= 0 )
		return;

	const int numCells = world->numGridArrayElements;
	if ( numCells != world->lightGridBounds[0] * world->lightGridBounds[1] * world->lightGridBounds[2] )
	{
		ri.Printf(PRINT_WARNING, "R_BuildStaticLighting: light grid size mismatch, no reconstruction\n");
		return;
	}

	const int startTime = ri.Milliseconds();
	const int mode = R_VolumetricStaticDirectional() ? Com_Clampi(0, 2, r_volumetricFogStaticDirectional->integer) : 0;
	const qboolean structured = (qboolean)(mode == 1 && r_staticLightReconstruction->integer);
	const qboolean promote = (qboolean)(structured && r_recoveredVolumetricLights->integer);
	const qboolean splitSun = tr.sunParsed;
	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);

	// the light grid in the linear space of the legacy volumetric texture
	std::vector<float> ambient(numCells * 3), direct(numCells * 3), bspDir(numCells * 3);
	std::vector<float> sunAlign(splitSun ? numCells : 0), sunVis(splitSun ? numCells : 0);
	std::vector<uint8_t> valid(numCells), animated(numCells);
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
					a = R_StaticSRGBToLinear(a);
					d = R_StaticSRGBToLinear(d);
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
		// a cell that also gets the light of an animated style: such a lamp is never a static light
		animated[i] = 0;
		for ( int s = 1; s < MAXLIGHTMAPS; s++ )
			if ( data->styles[s] != LS_LSNONE )
				animated[i] = 1;
	}

	// sun alignment, and the sky visibility of the valid cells lit from the sun direction.
	// World geometry is static: traced once here instead of probing the cascades
	// per froxel every frame. Two passes: the centre ray of every candidate, then the
	// four corner rays only where the centre result changes between neighbours (window
	// edges, the rim of shadows); elsewhere the cell is all sky or all blocked.
	const int traceStart = ri.Milliseconds();
	if ( splitSun )
	{
		const int *bounds = world->lightGridBounds;
		// 0 = no candidate, 1 = centre blocked, 2 = centre sees the sky
		std::vector<uint8_t> centre(numCells, 0);
		for ( int i = 0; i < numCells; i++ )
		{
			const vec3_t cellDir = { bspDir[i * 3 + 0], bspDir[i * 3 + 1], bspDir[i * 3 + 2] };
			sunAlign[i] = valid[i] ? R_StaticSmoothstep(STATIC_SUN_COS_OUTER, STATIC_SUN_COS_INNER, DotProduct(cellDir, sunDir)) : 0.0f;
			sunVis[i] = 1.0f;
			const float *d = &direct[i * 3];
			if ( sunAlign[i] > 0.0f && (d[0] > 0.0f || d[1] > 0.0f || d[2] > 0.0f) )
			{
				centre[i] = R_StaticSunRays(world, i, sunDir, 0, 1) ? 2 : 1;
				s_sl.numRays++;
				s_sl.numTraced++;
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
				sunVis[i] = (seen + R_StaticSunRays(world, i, sunDir, 1, 4)) / 5.0f;
				s_sl.numRays += 4;
				s_sl.numRefined++;
			}
			else
				sunVis[i] = (float)seen;
			if ( sunVis[i] > 0.0f )
				s_sl.numSeeSky++;
		}
	}
	s_sl.msecTrace = ri.Milliseconds() - traceStart;

	// static emitters anchor the reconstruction (mode 1 only)
	const int areaStart = ri.Milliseconds();
	std::vector<vrAreaSource> areas;
	if ( mode == 1 )
		R_CollectStaticAreaSources(world, areas, &s_sl.numAreaCandidates);
	s_sl.numAreaSources = (int)areas.size();
	s_sl.msecAreaSources = ri.Milliseconds() - areaStart;

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
	if ( structured )
	{
		in.trace = R_StaticLightTrace;
		in.animated = animated.data();
		in.promote = promote ? true : false;
		in.physicalThreshold = r_recoveredPhysicalConfidence->value;
		in.spotThreshold = r_recoveredSpotConfidence->value;
		in.maxPromoted = Com_Clampi(1, STATIC_LIGHTS_MAX_PROMOTED, r_recoveredVolumetricMaxLights->integer);
	}

	const int reconstructStart = ri.Milliseconds();
	vrOutput out;
	VR_Reconstruct(in, out);
	s_sl.msecReconstruct = ri.Milliseconds() - reconstructStart;

	// the inputs are not needed any more: give the memory back before the packing
	// (a million cell grid holds a few hundred MB during the reconstruction)
	std::vector<float>().swap(ambient);
	std::vector<float>().swap(direct);
	std::vector<float>().swap(bspDir);
	std::vector<float>().swap(sunAlign);
	std::vector<float>().swap(sunVis);
	std::vector<uint8_t>().swap(animated);
	std::vector<vrAreaSource>().swap(areas);

	// the structured lights, with the BSP region of their origin (runtime PVS culling)
	s_sl.lights.reserve(out.lights.size());
	for ( const vrStaticLight& l : out.lights )
	{
		staticLight_t sl;
		sl.light = l;
		const mnode_t *leaf = world->nodes ? R_StaticPointInLeaf(world, l.origin) : NULL;
		sl.cluster = leaf ? leaf->cluster : -1;
		sl.area = leaf ? leaf->area : -1;
		s_sl.lights.push_back(sl);
	}

	R_UploadVolumetricStaticLighting(world, out, valid.data(), &s_sl.upload);

	s_sl.valid = qtrue;
	s_sl.mode = mode;
	s_sl.promote = promote;
	s_sl.stats = out.stats;
	s_sl.proxies = out.proxies;
	s_sl.msecTotal = ri.Milliseconds() - startTime;

	ri.Printf(PRINT_DEVELOPER, "Static lighting: %d cells, sun visibility %d traced (%d refined, %d rays), %d see the sky, %d msec\n",
		numCells, s_sl.numTraced, s_sl.numRefined, s_sl.numRays, s_sl.numSeeSky, s_sl.msecTrace);
	if ( mode != 0 )
	{
		ri.Printf(PRINT_DEVELOPER, "Static lighting reconstruction (mode %d): %d point proxies, %d area anchors "
			"(of %d), %d directional cells, %.1f%% of the baked light directional, %d msec\n",
			mode, out.stats.pointProxies, out.stats.areaAnchors, s_sl.numAreaSources, out.stats.directionalCells,
			out.stats.directionalFraction * 100.0f, s_sl.msecTotal);
	}
	if ( structured )
	{
		ri.Printf(PRINT_DEVELOPER, "Static lighting structured lights: %d physical, %d transport, %d promoted "
			"(%d points, %d spots), %.1f%% of the baked light promoted, %d traces\n",
			out.stats.physicalLights, out.stats.transportLights, out.stats.promotedPoints + out.stats.promotedSpots,
			out.stats.promotedPoints, out.stats.promotedSpots, out.stats.promotedFraction * 100.0f, out.stats.traces);
	}
}

/*
=================
R_StaticLightCulled

Runtime culling of a structured light against the view: the PVS of the view
cluster (a light in a cluster the view cannot see is skipped). Conservative:
the cluster of the light's origin only, the froxel range test comes after.
=================
*/
qboolean R_StaticLightCulled( const staticLight_t& light, int viewCluster )
{
	const world_t *w = tr.world;
	if ( !w || !w->vis || viewCluster < 0 || viewCluster >= w->numClusters ||
		light.cluster < 0 || light.cluster >= w->numClusters )
		return qfalse;
	const byte *vis = w->vis + viewCluster * w->clusterBytes;
	return (qboolean)!(vis[light.cluster >> 3] & (1 << (light.cluster & 7)));
}

int R_StaticViewCluster( const vec3_t origin )
{
	if ( !tr.world || !tr.world->nodes )
		return -1;
	const mnode_t *leaf = R_StaticPointInLeaf(tr.world, origin);
	return leaf ? leaf->cluster : -1;
}

/*
============================================================

Debug: r_staticLightDebug

============================================================
*/

qhandle_t RE_RegisterShaderFromImage(const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage);

// a camera facing line quad (as R_SpotDebugSegment)
static void R_StaticDebugSegment( qhandle_t shader, const refdef_t *fd, const vec3_t a, const vec3_t b, const byte *rgba )
{
	vec3_t dir, toEye, side;
	VectorSubtract(b, a, dir);
	VectorSubtract(a, fd->vieworg, toEye);
	CrossProduct(dir, toEye, side);
	if ( VectorNormalize(side) < 1e-6f )
		return;
	VectorScale(side, 0.35f + 0.0015f * VectorLength(toEye), side);

	polyVert_t verts[4];
	VectorSubtract(a, side, verts[0].xyz);
	VectorAdd(a, side, verts[1].xyz);
	VectorAdd(b, side, verts[2].xyz);
	VectorSubtract(b, side, verts[3].xyz);
	for ( int i = 0; i < 4; i++ )
	{
		verts[i].st[0] = verts[i].st[1] = 0.5f;
		Com_Memcpy(verts[i].modulate, rgba, 4);
	}
	RE_AddPolyToScene(shader, 4, verts, 1);
}

static void R_StaticDebugCircle( qhandle_t shader, const refdef_t *fd, const vec3_t center, const vec3_t axisA,
	const vec3_t axisB, float radius, const byte *rgba )
{
	const int segments = 24;
	vec3_t prev;
	for ( int s = 0; s <= segments; s++ )
	{
		const float a = (float)s / segments * 2.0f * (float)M_PI;
		vec3_t p;
		VectorMA(center, radius * cosf(a), axisA, p);
		VectorMA(p, radius * sinf(a), axisB, p);
		if ( s > 0 )
			R_StaticDebugSegment(shader, fd, prev, p, rgba);
		VectorCopy(p, prev);
	}
}

// green: physical (promoted: brighter), yellow: uncertain point, magenta: transport, cyan: area.
// 1: the lights, 2: also their range (R) and positional uncertainty (sigma), 3: promoted only
void R_StaticLightsBeginScene( const refdef_t *fd )
{
	if ( !r_staticLightDebug->integer || !s_sl.valid || !tr.world || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) )
		return;
	const qhandle_t shader = RE_RegisterShaderFromImage("*staticLightDebug", lightmaps2d, stylesDefault, tr.whiteImage, qfalse);
	static const byte promotedColor[4] = { 60, 255, 60, 255 };
	static const byte physicalColor[4] = { 30, 150, 30, 255 };
	static const byte uncertainColor[4] = { 255, 230, 40, 255 };
	static const byte transportColor[4] = { 255, 40, 255, 255 };
	static const byte areaColor[4] = { 40, 220, 255, 255 };
	static const byte rangeColor[4] = { 120, 120, 120, 255 };
	static const vec3_t axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	const int level = r_staticLightDebug->integer;

	for ( const staticLight_t& sl : s_sl.lights )
	{
		const vrStaticLight& l = sl.light;
		const qboolean promoted = (qboolean)((l.flags & VR_LIGHT_PROMOTED) != 0);
		if ( level == 3 && !promoted )
			continue;
		const byte *color = l.kind == VR_LIGHT_RECT ? areaColor : l.kind == VR_LIGHT_TRANSPORT ? transportColor :
			promoted ? promotedColor : (l.flags & VR_LIGHT_PHYSICAL) ? physicalColor : uncertainColor;
		const float size = 12.0f;

		if ( l.kind == VR_LIGHT_RECT )
		{
			vec3_t c[4];
			for ( int k = 0; k < 4; k++ )
			{
				VectorMA(l.origin, (k == 0 || k == 3) ? l.halfWidth : -l.halfWidth, l.right, c[k]);
				VectorMA(c[k], (k < 2) ? l.halfHeight : -l.halfHeight, l.up, c[k]);
			}
			for ( int k = 0; k < 4; k++ )
				R_StaticDebugSegment(shader, fd, c[k], c[(k + 1) & 3], color);
			continue;
		}

		// a cross: transport proxies, a sphere (three circles): point lights
		for ( int k = 0; k < 3; k++ )
		{
			vec3_t a, b;
			VectorMA(l.origin, -size, axes[k], a);
			VectorMA(l.origin, size, axes[k], b);
			R_StaticDebugSegment(shader, fd, a, b, color);
		}
		if ( l.kind != VR_LIGHT_TRANSPORT )
			for ( int k = 0; k < 3; k++ )
				R_StaticDebugCircle(shader, fd, l.origin, axes[k], axes[(k + 1) % 3], size, color);

		if ( l.kind == VR_LIGHT_SPOT )
		{
			// the outer cone at a quarter of the radius, its axis
			vec3_t axis, side, up, tip;
			VectorCopy(l.axis, axis);
			PerpendicularVector(side, axis);
			CrossProduct(axis, side, up);
			const float length = 0.25f * l.radius;
			VectorMA(l.origin, length, axis, tip);
			R_StaticDebugSegment(shader, fd, l.origin, tip, color);
			const float cosA = l.cosOuter, sinA = sqrtf(Q_max(0.0f, 1.0f - cosA * cosA));
			vec3_t center;
			VectorMA(l.origin, length * cosA, axis, center);
			R_StaticDebugCircle(shader, fd, center, side, up, length * sinA, color);
			for ( int k = 0; k < 4; k++ )
			{
				const float a = k * 0.5f * (float)M_PI;
				vec3_t p;
				VectorMA(center, length * sinA * cosf(a), side, p);
				VectorMA(p, length * sinA * sinf(a), up, p);
				R_StaticDebugSegment(shader, fd, l.origin, p, color);
			}
		}

		if ( level == 2 )
		{
			R_StaticDebugCircle(shader, fd, l.origin, axes[0], axes[1], l.radius, rangeColor);
			if ( l.sigmaP > 0.0f )
				R_StaticDebugCircle(shader, fd, l.origin, axes[0], axes[1], l.sigmaP, color);
		}
	}
}

/*
=================
R_StaticLightingStats_f

r_vfogStaticStats [proxies | lights]: the static lighting reconstruction of the
loaded map
=================
*/
void R_StaticLightingStats_f( void )
{
	if ( !s_sl.valid )
	{
		ri.Printf(PRINT_ALL, "r_vfogStaticStats: no static lighting reconstruction built (r_volumetricFog 2, a map with a light grid)\n");
		return;
	}
	const vrStats& st = s_sl.stats;
	static const char *modeNames[] = { "off", "reconstructed moments", "raw BSP direction moments" };
	ri.Printf(PRINT_ALL, "r_volumetricFogStaticDirectional %d (%s)\n", s_sl.mode, modeNames[Com_Clampi(0, 2, s_sl.mode)]);
	ri.Printf(PRINT_ALL, "  probes          %d cells, %d valid\n", st.cells, st.validCells);
	ri.Printf(PRINT_ALL, "  sun visibility  %d cells traced (%d refined at edges, %d rays), %d see the sky\n",
		s_sl.numTraced, s_sl.numRefined, s_sl.numRays, s_sl.numSeeSky);
	ri.Printf(PRINT_ALL, "  split error     max %g (%.3f%% relative), float %g\n", s_sl.upload.maxError,
		s_sl.upload.maxRelError * 100.0f, st.maxSplitError);
	if ( s_sl.mode == 1 )
	{
		ri.Printf(PRINT_ALL, "  seeds           %d, %d fitted, %d merged into a stronger fit\n", st.seeds, st.fits, st.mergedFits);
		ri.Printf(PRINT_ALL, "  point proxies   %d (%d sources over the budget dropped)\n", st.pointProxies, st.droppedSources);
		ri.Printf(PRINT_ALL, "  area anchors    %d accepted of %d static sources (%d emissive candidates)\n",
			st.areaAnchors, s_sl.numAreaSources, s_sl.numAreaCandidates);
		ri.Printf(PRINT_ALL, "  ray residual    mean %.3f, P95 %.3f cell diagonals\n", st.meanRayRms, st.p95RayRms);
		ri.Printf(PRINT_ALL, "  position sigma  mean %.3f cell diagonals\n", st.meanSigmaP);
	}
	if ( s_sl.mode != 0 )
	{
		ri.Printf(PRINT_ALL, "  directional     %d cells, %.1f%% of the baked light attributed, %.1f%% directional after cancellation\n",
			st.directionalCells, st.attributedFraction * 100.0f, st.directionalFraction * 100.0f);
	}
	if ( st.traces > 0 )
	{
		ri.Printf(PRINT_ALL, "  structured      %d physical, %d transport, %d relocated, %d traces\n",
			st.physicalLights, st.transportLights, st.relocatedLights, st.traces);
		ri.Printf(PRINT_ALL, "  promoted        %s: %d points, %d spots, %.2f%% of the baked light, excess %.2f%%, "
			"partition error %g\n", s_sl.promote ? "on" : "off (r_recoveredVolumetricLights 0)", st.promotedPoints,
			st.promotedSpots, st.promotedFraction * 100.0f, st.excessFraction * 100.0f, st.maxPartitionError);
	}
	ri.Printf(PRINT_ALL, "  msec            total %d: sun trace %d, area sources %d, reconstruction %d (split %.1f, "
		"gradients %.1f, seeds %.1f, fit %.1f, anchors %.1f, attribution %.1f, lights %.1f), upload %d\n",
		s_sl.msecTotal, s_sl.msecTrace, s_sl.msecAreaSources, s_sl.msecReconstruct, st.msecSplit, st.msecGradients,
		st.msecSeeds, st.msecFit, st.msecAreas, st.msecAttribution, st.msecLights, s_sl.upload.msec);

	const char *arg = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
	if ( !Q_stricmp(arg, "proxies") )
	{
		for ( size_t k = 0; k < s_sl.proxies.size(); k++ )
		{
			const vrProxy& p = s_sl.proxies[k];
			ri.Printf(PRINT_ALL, "  %3d %-5s (%.0f %.0f %.0f) color %.2f %.2f %.2f confidence %.2f sigma %.0f range %.0f support %d",
				(int)k, p.type == VR_SOURCE_RECT ? "rect" : "point", p.position[0], p.position[1], p.position[2],
				p.color[0], p.color[1], p.color[2], p.confidence, p.sigmaP, p.range, p.support);
			if ( p.type == VR_SOURCE_RECT )
				ri.Printf(PRINT_ALL, " size %.0f x %.0f%s", 2.0f * p.halfWidth, 2.0f * p.halfHeight, p.twoSided ? " two-sided" : "");
			ri.Printf(PRINT_ALL, "\n");
		}
	}
	else if ( !Q_stricmp(arg, "lights") )
	{
		static const char *kindNames[] = { "transport", "point", "spot", "rect" };
		for ( size_t k = 0; k < s_sl.lights.size(); k++ )
		{
			const staticLight_t& sl = s_sl.lights[k];
			const vrStaticLight& l = sl.light;
			ri.Printf(PRINT_ALL, "  %3d %-9s%s%s%s (%.0f %.0f %.0f) R %.0f color %.4f %.4f %.4f conf %.2f vis %.2f phys %.2f "
				"fit %.2f leak %.2f excess %.2f cluster %d",
				(int)k, kindNames[Com_Clampi(0, 3, (int)l.kind)], (l.flags & VR_LIGHT_PROMOTED) ? " promoted" : "",
				(l.flags & VR_LIGHT_RELOCATED) ? " relocated" : "", (l.flags & VR_LIGHT_ANIMATED) ? " animated" : "",
				l.origin[0], l.origin[1], l.origin[2], l.radius, l.color[0], l.color[1], l.color[2], l.confidence,
				l.visibility, l.physicalConfidence, l.radiometricConfidence, l.leakFraction, l.excessFraction, sl.cluster);
			if ( l.kind == VR_LIGHT_SPOT )
				ri.Printf(PRINT_ALL, " axis %.2f %.2f %.2f cone %.0f / %.0f deg spot %.2f", l.axis[0], l.axis[1], l.axis[2],
					RAD2DEG(acosf(l.cosInner)), RAD2DEG(acosf(l.cosOuter)), l.spotConfidence);
			ri.Printf(PRINT_ALL, "\n");
		}
	}
	else
		ri.Printf(PRINT_ALL, "  (r_vfogStaticStats proxies | lights: list the sources / the structured lights)\n");
}
