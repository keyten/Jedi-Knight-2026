/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_light.c

#include "tr_local.h"

#include <algorithm>
#include <vector>

#define	DLIGHT_AT_RADIUS		16
// at the edge of a dlight's influence, this amount of light will be added

#define	DLIGHT_MINIMUM_RADIUS	16
// never calculate a range less than this to prevent huge light numbers


/*
===============
R_TransformDlights

Transforms the origins of an array of dlights.
Used by both the front end (for DlightBmodel) and
the back end (before doing the lighting calculation)
===============
*/
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *ori) {
	int		i;
	vec3_t	temp;

	for ( i = 0 ; i < count ; i++, dl++ ) {
		VectorSubtract( dl->origin, ori->origin, temp );
		dl->transformed[0] = DotProduct( temp, ori->axis[0] );
		dl->transformed[1] = DotProduct( temp, ori->axis[1] );
		dl->transformed[2] = DotProduct( temp, ori->axis[2] );
	}
}

/*
=============
R_DlightBmodel

Determine which dynamic lights may effect this bmodel
=============
*/
void R_DlightBmodel( bmodel_t *bmodel, trRefEntity_t *ent ) {
	int			i, j;
	dlight_t	*dl;
	int			mask;
	msurface_t	*surf;

	if ( ent->bmodelDlightCacheScene == tr.sceneCount ) {
		mask = ent->bmodelDlightCacheBits;
	} else {
		// The entity transform and scene light list are the same in every colour
		// and shadow view.  Do the O(lights) local-bounds test only once.
		R_TransformDlights( tr.refdef.num_dlights, tr.refdef.dlights, &tr.ori );

		mask = 0;
		for ( i=0 ; i<tr.refdef.num_dlights ; i++ ) {
			dl = &tr.refdef.dlights[i];

			// see if the point is close enough to the bounds to matter
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( dl->transformed[j] - bmodel->bounds[1][j] > dl->radius ) {
					break;
				}
				if ( bmodel->bounds[0][j] - dl->transformed[j] > dl->radius ) {
					break;
				}
			}
			if ( j < 3 ) {
				continue;
			}

			// we need to check this light
			if ( R_ForwardPlusActive() ) {
				// Forward+: no per light bits, only "lit" (cluster lists pick the lights)
				mask = 1;
				break;
			}
			mask |= 1 << i;
		}
		ent->bmodelDlightCacheScene = tr.sceneCount;
		ent->bmodelDlightCacheBits = mask;
	}

	ent->needDlights = (qboolean)(mask != 0);

	world_t *world = R_GetWorld(bmodel->worldIndex);
	// set the dlight bits in all the surfaces
	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		surf = world->surfaces + bmodel->firstSurface + i;

		switch(*surf->data)
		{
			case SF_FACE:
			case SF_GRID:
			case SF_TRIANGLES:
			case SF_VBO_MESH:
				((srfBspSurface_t *)surf->data)->dlightBits = mask;
				break;

			default:
				break;
		}
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

extern	cvar_t	*r_ambientScale;
extern	cvar_t	*r_directedScale;
extern	cvar_t	*r_debugLight;

/*
=================
R_SetupEntityLightingGrid

=================
*/
static void R_SetupEntityLightingGrid( trRefEntity_t *ent, world_t *world ) {
	vec3_t	lightOrigin;
	int		pos[3];
	int		i, j;
	float	frac[3];
	int		gridStep[3];
	vec3_t	direction;
	float	totalFactor;
	uint32_t startGridPos;
	if (r_fullbright->integer || tr.refdef.doLAGoggles)
	{
		ent->ambientLight[0] = ent->ambientLight[1] = ent->ambientLight[2] = 255.0f;
		ent->directedLight[0] = ent->directedLight[1] = ent->directedLight[2] = 255.0f;
		VectorCopy(tr.sunDirection, ent->lightDir);
		return;
	}
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// seperate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	VectorSubtract( lightOrigin, world->lightGridOrigin, lightOrigin );
	for ( i = 0 ; i < 3 ; i++ ) {
		float	v;

		v = lightOrigin[i]*world->lightGridInverseSize[i];
		pos[i] = floor( v );
		frac[i] = v - pos[i];
		if ( pos[i] < 0 ) {
			pos[i] = 0;
		} else if ( pos[i] >= world->lightGridBounds[i] - 1 ) {
			pos[i] = world->lightGridBounds[i] - 1;
		}
	}

	VectorClear( ent->ambientLight );
	VectorClear( ent->directedLight );
	VectorClear( direction );

	assert( world->lightGridData ); // NULL with -nolight maps

	// trilerp the light value
	gridStep[0] = 1;
	gridStep[1] = 1 * world->lightGridBounds[0];
	gridStep[2] = 1 * world->lightGridBounds[0] * world->lightGridBounds[1];
	startGridPos = pos[0] * gridStep[0] + pos[1] * gridStep[1] + pos[2] * gridStep[2];

	totalFactor = 0;
	for ( i = 0 ; i < 8 ; i++ ) {
		float	factor;
		mgrid_t	*data;
		uint32_t gridPos;
		int		lat, lng;
		vec3_t	normal;

		#if idppc
		float d0, d1, d2, d3, d4, d5;
		#endif

		factor = 1.0;
		gridPos = startGridPos;

		for ( j = 0 ; j < 3 ; j++ ) {
			if ( i & (1<<j) ) {
				factor *= frac[j];
				gridPos += gridStep[j];
			} else {
				factor *= (1.0 - frac[j]);
			}
		}

		if (gridPos >= (unsigned)world->numGridArrayElements)
		{//we've gone off the array somehow
			continue;
		}

		data = world->lightGridData + *(world->lightGridArray+gridPos);
		if ( data->styles[0] == LS_LSNONE )
		{
			continue;	// ignore samples in walls
		}

		totalFactor += factor;
		#if idppc
		d0 = data[0]; d1 = data[1]; d2 = data[2];
		d3 = data[3]; d4 = data[4]; d5 = data[5];

		ent->ambientLight[0] += factor * d0;
		ent->ambientLight[1] += factor * d1;
		ent->ambientLight[2] += factor * d2;

		ent->directedLight[0] += factor * d3;
		ent->directedLight[1] += factor * d4;
		ent->directedLight[2] += factor * d5;
		#else
		if (world->hdrLightGrid)
		{
			float *hdrData = world->hdrLightGrid + (gridPos * 6);
			ent->ambientLight[0] += factor * hdrData[0] * 255.0f;
			ent->ambientLight[1] += factor * hdrData[1] * 255.0f;
			ent->ambientLight[2] += factor * hdrData[2] * 255.0f;

			ent->directedLight[0] += factor * hdrData[3] * 255.0f;
			ent->directedLight[1] += factor * hdrData[4] * 255.0f;
			ent->directedLight[2] += factor * hdrData[5] * 255.0f;
		}
		else
		{
			for(j=0;j<MAXLIGHTMAPS;j++)
			{
				if (data->styles[j] != LS_LSNONE)
				{
					const byte	style= data->styles[j];

					ent->ambientLight[0] += factor * data->ambientLight[j][0] * styleColors[style][0] / 255.0f;
					ent->ambientLight[1] += factor * data->ambientLight[j][1] * styleColors[style][1] / 255.0f;
					ent->ambientLight[2] += factor * data->ambientLight[j][2] * styleColors[style][2] / 255.0f;

					ent->directedLight[0] += factor * data->directLight[j][0] * styleColors[style][0] / 255.0f;
					ent->directedLight[1] += factor * data->directLight[j][1] * styleColors[style][1] / 255.0f;
					ent->directedLight[2] += factor * data->directLight[j][2] * styleColors[style][2] / 255.0f;
				}
				else
				{
					break;
				}
			}
		}
		#endif
		lat = data->latLong[1];
		lng = data->latLong[0];
		lat *= (FUNCTABLE_SIZE/256);
		lng *= (FUNCTABLE_SIZE/256);

		// decode X as cos( lat ) * sin( long )
		// decode Y as sin( lat ) * sin( long )
		// decode Z as cos( long )

		normal[0] = tr.sinTable[(lat+(FUNCTABLE_SIZE/4))&FUNCTABLE_MASK] * tr.sinTable[lng];
		normal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		normal[2] = tr.sinTable[(lng+(FUNCTABLE_SIZE/4))&FUNCTABLE_MASK];

		VectorMA( direction, factor, normal, direction );
	}

	if ( totalFactor > 0 && totalFactor < 0.99 ) {
		totalFactor = 1.0f / totalFactor;
		VectorScale( ent->ambientLight, totalFactor, ent->ambientLight );
		VectorScale( ent->directedLight, totalFactor, ent->directedLight );
	}

	VectorScale( ent->ambientLight, r_ambientScale->value, ent->ambientLight );
	VectorScale( ent->directedLight, r_directedScale->value, ent->directedLight );

	VectorNormalize2( direction, ent->lightDir );
}


/*
===============
LogLight
===============
*/
static void LogLight( trRefEntity_t *ent ) {
	int	max1, max2;

	if ( !(ent->e.renderfx & RF_FIRST_PERSON ) ) {
		return;
	}

	max1 = ent->ambientLight[0];
	if ( ent->ambientLight[1] > max1 ) {
		max1 = ent->ambientLight[1];
	} else if ( ent->ambientLight[2] > max1 ) {
		max1 = ent->ambientLight[2];
	}

	max2 = ent->directedLight[0];
	if ( ent->directedLight[1] > max2 ) {
		max2 = ent->directedLight[1];
	} else if ( ent->directedLight[2] > max2 ) {
		max2 = ent->directedLight[2];
	}

	ri.Printf( PRINT_ALL, "amb:%i  dir:%i\n", max1, max2 );
}

/*
===============================================================================

r_entitySunSplit

The light grid stores one directed light per cell: the sum of every light the
cell sees (the sun, lamps, bounce) along their mean direction. The legacy model
darkens all of it with the sun shadow map while lighting it from the grid
direction, so the shadow and the lit side of a model disagree, the sun cosine is
applied twice and lamps go dark in the sun's shadow. With the split, the sun part
of the sample is taken out of the directed light and the sun is lit as an
analytic light from its own direction, shadowed per pixel (lightall
USE_ENTITY_SUN_SPLIT).

The sun of the grid is calibrated once per map (R_CalibrateEntitySun): the
median directed light of the cells lit from the sun direction. Per entity, a few
probe rays towards the sun tell whether the sun reaches the model at all (no sun
through the cascades indoors or beyond their range) and how much of the grid
sample is sunlit.

===============================================================================
*/

#define ENTITY_SUN_CALIBRATION_COS_STRICT	0.9848f	// cos(10)
#define ENTITY_SUN_CALIBRATION_COS_WIDE		0.9397f	// cos(20)
#define ENTITY_SUN_CALIBRATION_MIN_CELLS	16
#define ENTITY_SUN_PROBES					5
#define ENTITY_SUN_CACHE_SIZE				4096	// power of two
#define ENTITY_SUN_CACHE_QUANTUM			8.0f
#define ENTITY_SUN_CACHE_MSEC				1000	// SP traces see movers too

qboolean R_EntitySunSplitWanted( void )
{
	// the split lives in the per pixel legacy grid path only: the fast light
	// vertex path and the multi-point / GPU grids keep the legacy light
	return (qboolean)(r_entitySunSplit && r_entitySunSplit->integer &&
		r_sunlightMode->integer &&
		(r_normalMapping->integer || r_specularMapping->integer) &&
		r_entityLightGrid->integer == 0 && r_entityLightGridDebug->integer == 0);
}

static float R_EntitySunLuma( const vec3_t c )
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

void R_CalibrateEntitySun( world_t *world )
{
	world->entitySunValid = qfalse;
	world->entitySunCells = 0;
	VectorClear(world->entitySunGrid);
	if ( !R_EntitySunSplitWanted() || !tr.sunParsed || !world->lightGridData ||
		world->numGridArrayElements <= 0 )
		return;

	vec3_t sunDir;
	VectorCopy(tr.sunDirection, sunDir);
	VectorNormalize(sunDir);

	const int numCells = world->numGridArrayElements;
	const float windows[2] = { ENTITY_SUN_CALIBRATION_COS_STRICT, ENTITY_SUN_CALIBRATION_COS_WIDE };
	std::vector<std::pair<float, int>> cells;
	for ( int w = 0; w < 2 && (int)cells.size() < ENTITY_SUN_CALIBRATION_MIN_CELLS; w++ )
	{
		cells.clear();
		for ( int i = 0; i < numCells; i++ )
		{
			const mgrid_t *data = world->lightGridData + world->lightGridArray[i];
			if ( data->styles[0] == LS_LSNONE )
				continue;
			// as R_SetupEntityLightingGrid
			const int lat = data->latLong[1] * (FUNCTABLE_SIZE / 256);
			const int lng = data->latLong[0] * (FUNCTABLE_SIZE / 256);
			vec3_t dir;
			dir[0] = tr.sinTable[(lat + (FUNCTABLE_SIZE / 4)) & FUNCTABLE_MASK] * tr.sinTable[lng];
			dir[1] = tr.sinTable[lat] * tr.sinTable[lng];
			dir[2] = tr.sinTable[(lng + (FUNCTABLE_SIZE / 4)) & FUNCTABLE_MASK];
			if ( DotProduct(dir, sunDir) < windows[w] )
				continue;
			vec3_t d;
			if ( world->hdrLightGrid )
				VectorScale(world->hdrLightGrid + i * 6 + 3, 255.0f, d);
			else
				VectorSet(d, data->directLight[0][0], data->directLight[0][1], data->directLight[0][2]);
			const float luma = R_EntitySunLuma(d);
			if ( luma > 0.0f )
				cells.push_back(std::make_pair(luma, i));
		}
	}
	if ( (int)cells.size() < ENTITY_SUN_CALIBRATION_MIN_CELLS )
	{
		ri.Printf(PRINT_DEVELOPER, "r_entitySunSplit: %d grid cells lit from the sun direction, legacy entity light\n",
			(int)cells.size());
		return;
	}

	std::nth_element(cells.begin(), cells.begin() + cells.size() / 2, cells.end());
	const int median = cells[cells.size() / 2].second;
	if ( world->hdrLightGrid )
		VectorScale(world->hdrLightGrid + median * 6 + 3, 255.0f, world->entitySunGrid);
	else
	{
		const mgrid_t *data = world->lightGridData + world->lightGridArray[median];
		VectorSet(world->entitySunGrid, data->directLight[0][0], data->directLight[0][1], data->directLight[0][2]);
	}
	world->entitySunCells = (int)cells.size();
	world->entitySunValid = qtrue;
	ri.Printf(PRINT_DEVELOPER, "r_entitySunSplit: grid sun %.1f %.1f %.1f from %d cells\n",
		world->entitySunGrid[0], world->entitySunGrid[1], world->entitySunGrid[2], world->entitySunCells);
}

// sun visibility of a point, cached on a small grid: the world is static, and the
// probes of a model stay in the same cells for many frames
static qboolean R_EntitySunProbe( const vec3_t point, const vec3_t sunDir )
{
	static struct
	{
		int key[3];
		int time;
		const world_t *world;
		vec3_t sunDir;
		qboolean seen;
	} cache[ENTITY_SUN_CACHE_SIZE];

	int key[3];
	for ( int c = 0; c < 3; c++ )
		key[c] = (int)floorf(point[c] * (1.0f / ENTITY_SUN_CACHE_QUANTUM));
	const unsigned hash = ((unsigned)key[0] * 73856093u ^ (unsigned)key[1] * 19349663u ^
		(unsigned)key[2] * 83492791u) & (ENTITY_SUN_CACHE_SIZE - 1);
	const int now = ri.Milliseconds();
	auto &entry = cache[hash];
	if ( entry.world == tr.world && entry.key[0] == key[0] && entry.key[1] == key[1] &&
		entry.key[2] == key[2] && VectorCompare(entry.sunDir, sunDir) &&
		now - entry.time >= 0 && now - entry.time < ENTITY_SUN_CACHE_MSEC )
		return entry.seen;

	// the centre of the cache cell: every point of the cell gets the same answer
	vec3_t centre;
	for ( int c = 0; c < 3; c++ )
		centre[c] = (key[c] + 0.5f) * ENTITY_SUN_CACHE_QUANTUM;
	entry.seen = R_SunVisibleFromPoint(centre, sunDir);
	for ( int c = 0; c < 3; c++ )
		entry.key[c] = key[c];
	VectorCopy(sunDir, entry.sunDir);
	entry.time = now;
	entry.world = tr.world;
	return entry.seen;
}

static void R_SetupEntitySunSplit( const trRefdef_t *refdef, trRefEntity_t *ent, const vec3_t lightOrigin )
{
	ent->sunSplit = qfalse;
	if ( !R_EntitySunSplitWanted() || ent == &tr.worldEntity || ent->e.reType != RT_MODEL ||
		(refdef->rdflags & RDF_NOWORLDMODEL) || !tr.world || !tr.world->lightGridData ||
		!tr.world->entitySunValid || r_fullbright->integer || tr.refdef.doLAGoggles )
		return;

	vec3_t sunDir;
	VectorCopy(refdef->sunDir, sunDir);
	if ( VectorNormalize(sunDir) < 0.5f )
		return;

	// probes: the lighting origin, the head, the feet and both sides across the sun
	vec3_t side;
	const vec3_t up = { 0.0f, 0.0f, 1.0f };
	CrossProduct(sunDir, up, side);
	if ( VectorNormalize(side) < 0.01f )
		VectorSet(side, 1.0f, 0.0f, 0.0f);
	const float offsets[ENTITY_SUN_PROBES][2] = { { 0, 0 }, { 0, 40 }, { 0, -16 }, { 32, 8 }, { -32, 8 } };
	int seen = 0;
	for ( int p = 0; p < ENTITY_SUN_PROBES; p++ )
	{
		vec3_t point;
		VectorMA(lightOrigin, offsets[p][0], side, point);
		point[2] += offsets[p][1];
		if ( R_EntitySunProbe(point, sunDir) )
			seen++;
	}
	// No probe sees the sky: deep inside, or the probes start in solid (a model
	// whose lighting origin is inside world brushes, e.g. a parked ship). The
	// legacy light keeps the cascades' darkening there; the split would leave
	// the grid light unshadowed.
	if ( seen == 0 )
		return;
	const float sunlit = (float)seen / ENTITY_SUN_PROBES;

	// the grid sun in the units of ent->directedLight (R_SetupEntityLighting)
	vec3_t sun;
	VectorScale(tr.world->entitySunGrid, r_directedScale->value, sun);
	if ( tr.forcedLinearLight )
	{
		for ( int c = 0; c < 3; c++ )
			sun[c] = 255.0f * (float)sRGBtoRGB(sun[c] / 255.0f);
	}
	const float sunLuma = R_EntitySunLuma(sun);
	if ( sunLuma <= 0.0f )
		return;

	// The sun part of the sampled grid light: bounded by the projection of the
	// directed light on the sun direction (a light from elsewhere is never taken
	// out), by the calibrated sun, and scaled by the share of the sample that
	// sees the sun.
	const float directedLuma = R_EntitySunLuma(ent->directedLight);
	const float projection = MAX(DotProduct(ent->lightDir, sunDir), 0.0f);
	const float gridSunLuma = sunlit * MIN(sunLuma, directedLuma * projection);
	vec3_t gridSun;
	VectorScale(sun, gridSunLuma / sunLuma, gridSun);

	vec3_t residualVector;
	VectorScale(ent->lightDir, directedLuma, residualVector);
	VectorMA(residualVector, -gridSunLuma, sunDir, residualVector);
	const float residualLength = VectorLength(residualVector);
	if ( residualLength > 1e-3f * MAX(directedLuma, 1.0f) )
		VectorScale(residualVector, 1.0f / residualLength, ent->residualDir);
	else
		VectorCopy(ent->lightDir, ent->residualDir);
	for ( int c = 0; c < 3; c++ )
		ent->residualLight[c] = MAX(ent->directedLight[c] - gridSun[c], 0.0f);

	// the analytic sun: full strength wherever any probe sees the sky (the
	// cascades shape it per pixel)
	VectorCopy(sun, ent->sunSplitLight);
	ent->sunSplitSeen = sunlit;
	ent->sunSplit = qtrue;
}

/*
=================
R_SetupEntityLighting

Calculates all the lighting values that will be used
by the Calc_* functions
=================
*/
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent ) {
	int				i;
	float			d;
	vec3_t			lightDir;
	vec3_t			lightOrigin;

	// lighting calculations
	if ( ent->lightingCalculated ) {
		return;
	}
	ent->lightingCalculated = qtrue;

	//
	// trace a sample point down to find ambient light
	//
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// seperate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	// if NOWORLDMODEL, only use dynamic lights (menu system, etc)
	if ( !(refdef->rdflags & RDF_NOWORLDMODEL )
		&& tr.world->lightGridData ) {
		R_SetupEntityLightingGrid( ent, tr.world );
	} else {
		ent->ambientLight[0] = ent->ambientLight[1] =
			ent->ambientLight[2] = tr.identityLight * 150;
		ent->directedLight[0] = ent->directedLight[1] =
			ent->directedLight[2] = tr.identityLight * 150;
		VectorCopy( tr.sunDirection, ent->lightDir );
	}

	// only do min lighting when there is no hdr light data
	if (tr.hdrLighting != qtrue)
	{
		if (ent->e.renderfx & RF_MINLIGHT)
		{ //the minlight flag is now for items rotating on their holo thing
			if (ent->e.shaderRGBA[0] == 255 &&
				ent->e.shaderRGBA[1] == 255 &&
				ent->e.shaderRGBA[2] == 0)
			{
				ent->ambientLight[0] += tr.identityLight * 255;
				ent->ambientLight[1] += tr.identityLight * 255;
				ent->ambientLight[2] += tr.identityLight * 0;
			}
			else
			{
				ent->ambientLight[0] += tr.identityLight * 16;
				ent->ambientLight[1] += tr.identityLight * 96;
				ent->ambientLight[2] += tr.identityLight * 150;
			}
		}
		// bonus items and view weapons have a fixed minimum add
		else if (ent->e.renderfx & RF_MORELIGHT) {
			ent->ambientLight[0] += tr.identityLight * 96;
			ent->ambientLight[1] += tr.identityLight * 96;
			ent->ambientLight[2] += tr.identityLight * 96;
		}
		else {
			// give everything a minimum light add
			ent->ambientLight[0] += tr.identityLight * 32;
			ent->ambientLight[1] += tr.identityLight * 32;
			ent->ambientLight[2] += tr.identityLight * 32;
		}
	}

	d = VectorLength( ent->directedLight );
	VectorScale( ent->lightDir, d, lightDir );

	// clamp ambient
	if (tr.hdrLighting != qtrue)
	{
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( ent->ambientLight[i] > tr.identityLightByte ) {
				ent->ambientLight[i] = tr.identityLightByte;
			}
		}
	}

	// r_linearLighting: the light grid is sRGB encoded like the lightmaps.
	// Decode after the gamma space minimum light and clamp above.
	if (tr.forcedLinearLight && !(refdef->rdflags & RDF_NOWORLDMODEL))
	{
		for (i = 0; i < 3; i++)
		{
			ent->ambientLight[i] = 255.0f * (float)sRGBtoRGB(ent->ambientLight[i] / 255.0f);
			ent->directedLight[i] = 255.0f * (float)sRGBtoRGB(ent->directedLight[i] / 255.0f);
		}
	}

	if ( r_debugLight->integer ) {
		LogLight( ent );
	}

	// save out the byte packet version
	((byte *)&ent->ambientLightInt)[0] = Q_ftol(ent->ambientLight[0]);
	((byte *)&ent->ambientLightInt)[1] = Q_ftol(ent->ambientLight[1]);
	((byte *)&ent->ambientLightInt)[2] = Q_ftol(ent->ambientLight[2]);
	((byte *)&ent->ambientLightInt)[3] = 0xff;

	// transform the direction to local space
	VectorNormalize( lightDir );
	VectorCopy(lightDir, ent->lightDir);

	ent->modelLightDir[0] = DotProduct( lightDir, ent->e.axis[0] );
	ent->modelLightDir[1] = DotProduct( lightDir, ent->e.axis[1] );
	ent->modelLightDir[2] = DotProduct( lightDir, ent->e.axis[2] );

	R_SetupEntitySunSplit( refdef, ent, lightOrigin );
}

/*
=================
R_LightForPoint
=================
*/
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	trRefEntity_t ent;

	if ( tr.world->lightGridData == NULL )
	  return qfalse;

	Com_Memset(&ent, 0, sizeof(ent));
	VectorCopy( point, ent.e.origin );
	R_SetupEntityLightingGrid( &ent, tr.world );
	VectorCopy(ent.ambientLight, ambientLight);
	VectorCopy(ent.directedLight, directedLight);
	VectorCopy(ent.lightDir, lightDir);

	return qtrue;
}


int R_LightDirForPoint( vec3_t point, vec3_t lightDir, vec3_t normal, world_t *world )
{
	trRefEntity_t ent;

	if ( world->lightGridData == NULL )
	{
		VectorCopy(normal, lightDir);
		return qfalse;
	}

	Com_Memset(&ent, 0, sizeof(ent));
	VectorCopy( point, ent.e.origin );
	R_SetupEntityLightingGrid( &ent, world );

	if (DotProduct(ent.lightDir, normal) > 0.2f)
		VectorCopy(ent.lightDir, lightDir);
	else
		VectorCopy(normal, lightDir);

	return qtrue;
}

int R_DLightsForPoint(const vec3_t point, const float radius)
{
	int dlightBits = 0;
	for (int i = 0; i < tr.refdef.num_dlights; i++)
	{
		const dlight_t *currentDlight = &tr.refdef.dlights[i];
		const float radiusSum = radius + currentDlight->radius;

		// spot lights (tr_spotlight.cpp): also outside the cone
		if (DistanceSquared(point, currentDlight->origin) < radiusSum * radiusSum &&
			R_SpotSphereInCone(currentDlight, point, radius))
		{
			// Forward+: only "lit", the cluster lists pick the lights
			if (R_ForwardPlusActive())
				return 1;
			dlightBits |= 1 << i;
		}
	}
	return dlightBits;
}

int R_DLightsForEntity(trRefEntity_t *ent, const vec3_t point, const float radius)
{
	if (ent->dlightCacheScene != tr.sceneCount || ent->dlightCacheRadius != radius ||
		!VectorCompare(ent->dlightCachePoint, point))
	{
		ent->dlightCacheBits = R_DLightsForPoint(point, radius);
		VectorCopy(point, ent->dlightCachePoint);
		ent->dlightCacheRadius = radius;
		ent->dlightCacheScene = tr.sceneCount;
	}
	return ent->dlightCacheBits;
}

int R_CubemapForPoint( const vec3_t point )
{
	int cubemapIndex = -1;

	if ((r_cubeMapping->integer || r_diffuseIBL->integer) && tr.numCubemaps)
	{
		int i;
		float shortest = (float)WORLD_SIZE * (float)WORLD_SIZE;

		for (i = 0; i < MIN(tr.numCubemaps, MAX_RUNTIME_CUBEMAPS); i++)
		{
			vec3_t diff;
			float length;

			VectorSubtract(point, tr.cubemaps[i].origin, diff);
			length = DotProduct(diff, diff);

			if (shortest > length)
			{
				shortest = length;
				cubemapIndex = i;
			}
		}
	}

	return cubemapIndex + 1;
}

int R_CubemapForEntity(trRefEntity_t *ent, const vec3_t point)
{
	if (ent->cubemapCacheScene != tr.sceneCount ||
		!VectorCompare(ent->cubemapCachePoint, point))
	{
		ent->cubemapCacheIndex = R_CubemapForPoint(point);
		VectorCopy(point, ent->cubemapCachePoint);
		ent->cubemapCacheScene = tr.sceneCount;
	}
	return ent->cubemapCacheIndex;
}

#ifdef REND2_SP
//pass in origin
qboolean RE_GetLighting(const vec3_t origin, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir) {
	trRefEntity_t tr_ent;

	if (!tr.world || !tr.world->lightGridData) {
		ambientLight[0] = ambientLight[1] = ambientLight[2] = 255.0;
		directedLight[0] = directedLight[1] = directedLight[2] = 255.0;
		VectorCopy(tr.sunDirection, lightDir);
		return qfalse;
	}
	memset(&tr_ent, 0, sizeof(tr_ent));

	if (ambientLight[0] == 666)
	{//HAX0R
		tr_ent.e.hModel = -1;
	}

	VectorCopy(origin, tr_ent.e.origin);
	R_SetupEntityLightingGrid(&tr_ent, tr.world);
	VectorCopy(tr_ent.ambientLight, ambientLight);
	VectorCopy(tr_ent.directedLight, directedLight);
	VectorCopy(tr_ent.lightDir, lightDir);
	return qtrue;
}
#endif
