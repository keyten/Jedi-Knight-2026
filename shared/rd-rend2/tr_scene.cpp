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

#include "tr_local.h"
#include "tr_pbr_contract.h"

#include <chrono>
#include <vector>
#include <algorithm>

// Complete wall-clock frame intervals: includes CPU, GPU waits, swap and the
// game. No per-frame logging or synchronous query reads in the measured loop.
void R_SunShadowBenchmarkFrame()
{
	static std::vector<double> samples;
	static int target = 0;
	static std::chrono::steady_clock::time_point previous;
	if (!r_sunShadowBenchmark->integer)
	{
		target = 0;
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (!target)
	{
		target = r_sunShadowBenchmark->integer;
		samples.clear();
		samples.reserve(target);
		previous = now;
		return;
	}
	samples.push_back(std::chrono::duration<double, std::milli>(now - previous).count());
	previous = now;
	if ((int)samples.size() < target)
		return;
	double total = 0.0;
	for (double ms : samples) total += ms;
	std::sort(samples.begin(), samples.end());
	const double mean = total / samples.size();
	ri.Printf(PRINT_ALL, "sunShadowBenchmark: %d frames, mean %.4f ms, median %.4f ms, p95 %.4f ms, %.2f FPS\n",
		target, mean, samples[samples.size() / 2], samples[(samples.size() - 1) * 95 / 100], 1000.0 / mean);
	ri.Cvar_Set("r_sunShadowBenchmark", "0");
	target = 0;
}

/*
r_shadowStats: per-frame CPU cost of every view class, printed once per second.
front = R_RenderView (BSP walk, entity/G2/MD3 surfaces, draw surf sort),
submit = backend execution of that view's commands (GL calls, driver),
gather = R_GatherFrameViews (view setup incl. CSM bounds walks).
Point shadow views are cube faces; spot = one projected view per spot light.
*/
enum {
	SSTAT_SKYPORTAL, SSTAT_SUN, SSTAT_PLAYER, SSTAT_POINT, SSTAT_LTC, SSTAT_PORTAL, SSTAT_MAIN,
	SSTAT_SPOT, SSTAT_POINTCUBE, SSTAT_SUN_STATIC, SSTAT_SUN_DYNAMIC, SSTAT_CLASSES
};
static struct {
	double frontUs[SSTAT_CLASSES];
	double submitUs[SSTAT_CLASSES];
	double frontMaxUs[SSTAT_CLASSES];
	int views[SSTAT_CLASSES];
	int drawSurfs[SSTAT_CLASSES];
	double gatherUs;
	int frames;
	int lastPrintTime;
} shadowStats;

static int R_ShadowStatsClass( const viewParms_t *parms )
{
	if (parms->flags & VPF_SUN_STATIC_SHADOW)
		return SSTAT_SUN_STATIC;
	if (parms->flags & VPF_SUN_DYNAMIC_SHADOW)
		return SSTAT_SUN_DYNAMIC;
	if ( parms->viewParmType == VPT_POINT_SHADOWS && parms->fovX != 90.0f )
		return SSTAT_SPOT;
	return (int)parms->viewParmType;
}

static double R_ShadowStatsUs( std::chrono::steady_clock::time_point start )
{
	return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

void R_ShadowStatsEndFrame( void )
{
	if ( !r_shadowStats->integer )
	{
		Com_Memset(&shadowStats, 0, sizeof(shadowStats));
		Com_Memset(&rb_shadowSubmitProfile, 0, sizeof(rb_shadowSubmitProfile));
		tr.sunShadowCacheHits = 0;
		tr.sunShadowCacheBypasses = 0;
		tr.sunShadowCacheRestoredPixels = 0;
		tr.sunShadowCacheMisses = 0;
		tr.sunShadowCacheScrolls = 0;
		tr.sunShadowCacheDiskHits = 0;
		tr.sunShadowCacheDiskLoads = 0;
		tr.sunShadowCacheDiskWrites = 0;
		tr.sunShadowCacheUpdatedPixels = 0;
		R_ShadowCacheGetStats(nullptr, nullptr, nullptr, qtrue);
		return;
	}

	shadowStats.frames++;
	const int now = ri.Milliseconds();
	if ( now - shadowStats.lastPrintTime < 1000 )
		return;

	static const char *names[SSTAT_CLASSES] = {
		"skyportal", "sun cascade", "player shadow", "point face", "ltc shadow", "portal", "main", "spot", "point cube",
		"sun static", "sun dynamic" };
	const double perFrame = 1.0 / (double)Q_max(1, shadowStats.frames);
	double shadowFront = 0.0, shadowSubmit = 0.0;
	ri.Printf(PRINT_ALL, "shadowStats (%d frames), per frame: views, front ms, submit ms, drawsurfs/view, front max/view us\n",
		shadowStats.frames);
	for ( int c = 0; c < SSTAT_CLASSES; c++ )
	{
		if ( !shadowStats.views[c] )
			continue;
		ri.Printf(PRINT_ALL, "  %-13s %5.1f  %7.3f  %7.3f  %7.1f  %7.1f\n", names[c],
			shadowStats.views[c] * perFrame,
			shadowStats.frontUs[c] * perFrame * 0.001,
			shadowStats.submitUs[c] * perFrame * 0.001,
			(double)shadowStats.drawSurfs[c] / (double)shadowStats.views[c],
			shadowStats.frontMaxUs[c]);
		if ( c != SSTAT_MAIN && c != SSTAT_PORTAL && c != SSTAT_SKYPORTAL )
		{
			shadowFront += shadowStats.frontUs[c];
			shadowSubmit += shadowStats.submitUs[c];
		}
	}
	ri.Printf(PRINT_ALL, "  shadow total front %.3f ms, submit %.3f ms; gather %.3f ms\n",
		shadowFront * perFrame * 0.001, shadowSubmit * perFrame * 0.001, shadowStats.gatherUs * perFrame * 0.001);
	if (tr.sunShadowCacheHits || tr.sunShadowCacheMisses || tr.sunShadowCacheScrolls ||
		tr.sunShadowCacheDiskHits ||
		tr.sunShadowCacheDiskLoads || tr.sunShadowCacheDiskWrites || tr.sunShadowCacheBypasses)
	{
		const int total = tr.sunShadowCacheHits + tr.sunShadowCacheMisses +
			tr.sunShadowCacheScrolls + tr.sunShadowCacheDiskHits;
		const double megapixelsPerFrame = (double)tr.sunShadowCacheUpdatedPixels * perFrame / 1000000.0;
		ri.Printf(PRINT_ALL, "  sun static cache: %.1f hits/frame, %.1f scrolls/frame, %.1f disk/frame, %.1f full/frame (%.1f%% reused), %.3f Mpix updated/frame\n",
			tr.sunShadowCacheHits * perFrame,
			tr.sunShadowCacheScrolls * perFrame,
			tr.sunShadowCacheDiskHits * perFrame,
			tr.sunShadowCacheMisses * perFrame,
			100.0 * (double)(tr.sunShadowCacheHits + tr.sunShadowCacheScrolls +
				tr.sunShadowCacheDiskHits) / (double)Q_max(1, total),
			megapixelsPerFrame);
		ri.Printf(PRINT_ALL, "  sun working restore: %.3f Mpix/frame, %.1f baseline bypasses/frame\n",
			(double)tr.sunShadowCacheRestoredPixels * perFrame / 1000000.0,
			tr.sunShadowCacheBypasses * perFrame);
		if (tr.sunShadowCacheDiskLoads || tr.sunShadowCacheDiskWrites)
			ri.Printf(PRINT_ALL, "  sun disk pages: %.1f loaded/frame, %.1f written/frame\n",
				tr.sunShadowCacheDiskLoads * perFrame,
				tr.sunShadowCacheDiskWrites * perFrame);
	}
	int cacheHits, cacheMisses, cacheUncacheable;
	R_ShadowCacheGetStats(&cacheHits, &cacheMisses, &cacheUncacheable, qtrue);
	ri.Printf(PRINT_ALL, "  point shadow cache %.1f hits/frame, %.1f misses/frame, %.1f conservatively dirty/frame\n",
		cacheHits * perFrame, cacheMisses * perFrame, cacheUncacheable * perFrame);

	// backend phases of the point light cube face draw commands
	shadowSubmitProfile_t &prof = rb_shadowSubmitProfile;
	if ( prof.commands )
	{
		static const char *phaseNames[SHADOWPROF_PHASES] = {
			"pre", "beginView", "clouds", "record", "execute", "depthRest", "mainPass",
			"bv.bind", "bv.clear", "bv.hooks" };
		const double perCmd = 1.0 / prof.commands;
		ri.Printf(PRINT_ALL, "  cube draw cmds %.1f/frame, RB_DrawSurfs %.3f ms/frame = %.1f us/cmd; per cmd: "
			"%.1f surfs, %.1f items, %.1f binds\n",
			prof.commands * perFrame, prof.totalUs * perFrame * 0.001, prof.totalUs * perCmd,
			prof.surfaces * perCmd, prof.drawItems * perCmd, prof.programBinds * perCmd);
		char line[512] = "   us/cmd:";
		for ( int p = 0; p < SHADOWPROF_PHASES; p++ )
			Q_strcat(line, sizeof(line), va(" %s %.1f", phaseNames[p], prof.us[p] * perCmd));
		ri.Printf(PRINT_ALL, "%s\n", line);
	}
	Com_Memset(&prof, 0, sizeof(prof));

	Com_Memset(&shadowStats, 0, sizeof(shadowStats));
	tr.sunShadowCacheHits = 0;
	tr.sunShadowCacheBypasses = 0;
	tr.sunShadowCacheRestoredPixels = 0;
	tr.sunShadowCacheMisses = 0;
	tr.sunShadowCacheScrolls = 0;
	tr.sunShadowCacheDiskHits = 0;
	tr.sunShadowCacheDiskLoads = 0;
	tr.sunShadowCacheDiskWrites = 0;
	tr.sunShadowCacheUpdatedPixels = 0;
	shadowStats.lastPrintTime = now;
}


int			r_firstSceneDrawSurf;

int			r_numdlights;
int			r_firstSceneDlight;

int			r_numfogvolumes;
int			r_firstSceneFogVolume;

int			r_numvolparticles;
int			r_firstSceneVolParticle;
int			r_volumetricParticlesRejected;		// invalid or over the list this frame (tr_volparticle.cpp)

int			r_numentities;
int			r_firstSceneEntity;

int			r_numpolys;
int			r_firstScenePoly;

int			r_numpolyverts;


/*
====================
R_InitNextFrame

====================
*/
void R_InitNextFrame( void ) {
	backEndData->commands.used = 0;

	tr.numTimedBlocks = 0;

	r_firstSceneDrawSurf = 0;

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numfogvolumes = 0;
	r_firstSceneFogVolume = 0;

	r_numvolparticles = 0;
	r_firstSceneVolParticle = 0;
	r_volumetricParticlesRejected = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	r_firstSceneDlight = r_numdlights;
	r_firstSceneFogVolume = r_numfogvolumes;
	r_firstSceneVolParticle = r_numvolparticles;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( const trRefdef_t *refdef ) {
	srfPoly_t *poly;
#ifndef REND2_SP
	int	fogMask = -((refdef->rdflags & RDF_NOFOG) == 0);
#endif
	int i;
	for ( i = 0, poly = refdef->polys; i < refdef->numPolys ; i++, poly++ ) {
		shader_t *sh = R_GetShaderByHandle( poly->hShader );
		R_AddDrawSurf(
			(surfaceType_t *)poly,
			REFENTITYNUM_WORLD,
			sh,
#ifndef REND2_SP
			poly->fogIndex & fogMask,
#else
			poly->fogIndex,
#endif
			qfalse,
			qfalse,
			0 /* cubemapIndex */ );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/

void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
	srfPoly_t	*poly;
	int			i, j;
	int			fogIndex;
	fog_t		*fog;
	vec3_t		bounds[2];

	if ( !tr.registered ) {
		return;
	}

	for ( j = 0; j < numPolys; j++ ) {
		if ( (r_numpolyverts + numVerts) > max_polyverts || r_numpolys >= max_polys ) {
			ri.Printf(
				PRINT_DEVELOPER,
				S_COLOR_YELLOW "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->hShader = hShader;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];

		Com_Memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );

		// done.
		r_numpolys++;
		r_numpolyverts += numVerts;

		// if no world is loaded
		if ( tr.world == NULL ) {
			fogIndex = 0;
		}
		// see if it is in a fog volume
		else if ( tr.world->numfogs == 1 ) {
			fogIndex = 0;
		} else {
			// find which fog volume the poly is in
			VectorCopy( poly->verts[0].xyz, bounds[0] );
			VectorCopy( poly->verts[0].xyz, bounds[1] );
			for ( i = 1 ; i < poly->numVerts ; i++ ) {
				AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
			}
			for ( fogIndex = 1 ; fogIndex < tr.world->numfogs ; fogIndex++ ) {
				fog = &tr.world->fogs[fogIndex];
				if ( bounds[1][0] >= fog->bounds[0][0]
					&& bounds[1][1] >= fog->bounds[0][1]
					&& bounds[1][2] >= fog->bounds[0][2]
					&& bounds[0][0] <= fog->bounds[1][0]
					&& bounds[0][1] <= fog->bounds[1][1]
					&& bounds[0][2] <= fog->bounds[1][2] ) {
					break;
				}
			}
			if ( fogIndex == tr.world->numfogs ) {
				fogIndex = 0;
			}
		}
		poly->fogIndex = fogIndex;
	}
}


//=================================================================================


/*
=====================
RE_AddRefEntityToScene

=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent ) {
	vec3_t cross;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n");
		return;
	}
	if ( Q_isnan(ent->origin[0]) || Q_isnan(ent->origin[1]) || Q_isnan(ent->origin[2]) ) {
		static qboolean firstTime = qtrue;
		if (firstTime) {
			firstTime = qfalse;
			ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n");
		}
		return;
	}

	if (ent->reType == RT_ENT_CHAIN)
	{ //minirefents must die.
		return;
	}

	if ( (int)ent->reType < 0 || ent->reType >= RT_MAX_SP_REF_ENTITY_TYPE || ent->reType == RT_MAX_MP_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].dlightCacheScene = -1;
	backEndData->entities[r_numentities].bmodelDlightCacheScene = -1;
	backEndData->entities[r_numentities].cubemapCacheScene = -1;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;
	backEndData->entities[r_numentities].maxModelScale =
		R_MaxEntityAxisScale(&backEndData->entities[r_numentities]);
	backEndData->entities[r_numentities].ghoulCameraLodCalculated = qfalse;
	backEndData->entities[r_numentities].ghoulCameraProjectedRadius = 0.0f;
	backEndData->entities[r_numentities].plantBoneOffset = -1;
	backEndData->entities[r_numentities].plantBoneTexels = 0;

	CrossProduct(ent->axis[0], ent->axis[1], cross);
	backEndData->entities[r_numentities].mirrored = (qboolean)(DotProduct(ent->axis[2], cross) < 0.f);

	r_numentities++;
}

#ifndef REND2_SP
/*
=====================
RE_AddMiniRefEntityToScene

1:1 with how vanilla does it --eez
=====================
*/
void RE_AddMiniRefEntityToScene( const miniRefEntity_t *miniRefEnt ) {
	refEntity_t entity;
	if(!tr.registered)
		return;
	if(!miniRefEnt)
		return;
	memset(&entity, 0, sizeof(entity));
	memcpy(&entity, miniRefEnt, sizeof(*miniRefEnt));
	RE_AddRefEntityToScene(&entity);
}
#endif

/*
=====================
R_AllocSceneDlight

A cleared point light, NULL when the frame is full: MAX_DLIGHTS, or
MAX_RENDER_DLIGHTS with r_forwardPlus 1 (area lights use it too)
=====================
*/
dlight_t *R_AllocSceneDlight( void ) {
	if ( r_numdlights >= R_DlightCapacity() ) {
		R_ForwardPlusNoteDroppedLight();
		return NULL;
	}
	dlight_t *dl = &backEndData->dlights[r_numdlights++];
	Com_Memset( dl, 0, sizeof( *dl ) );
	dl->areaType = DLIGHT_POINT;
	dl->areaId = -1;
	dl->spotCosInner = -1.0f;	// cone factor 1 (R_SpotConeAttenuation)
	dl->spotCosOuter = -2.0f;
	dl->spotShadowSlot = -1;
	dl->cookieLayer = -1;
	return dl;
}

/*
=====================
R_AddSceneDynamicLight

The point light of RE_AddDynamicLightToScene, NULL when it was not added.
Spot lights (tr_spotlight.cpp) start from it.
=====================
*/
dlight_t *R_AddSceneDynamicLight( const vec3_t org, float intensity, float r, float g, float b, int additive ) {
	dlight_t	*dl;

	if ( !tr.registered ) {
		return NULL;
	}
	if ( intensity <= 0 ) {
		return NULL;
	}
	const vec3_t inputColor = { r, g, b };
	float physicalChroma[3] = {};
	if (r_pbrPhysical->integer &&
		!Rend2PBR::NormalizeChromaticity(inputColor, physicalChroma)) {
		return NULL;
	}
	dl = R_AllocSceneDlight();
	if ( !dl ) {
		return NULL;
	}
	VectorCopy (org, dl->origin);
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->canonicalIntensity = 0.0f;
	dl->physicalIntensity = qfalse;
	dl->emitterRadius = Rend2PBR::kDefaultEmitterRadiusMeters *
		r_pbrWorldUnitsPerMeter->value;
	if (r_pbrPhysical->integer)
	{
		dl->canonicalIntensity = Rend2PBR::LegacyRadiusToSceneCandela(intensity,
			r_pbrWorldUnitsPerMeter->value, r_pbrLegacyReference->value) *
			Rend2PBR::Luminance(inputColor);
		VectorScale(physicalChroma, dl->canonicalIntensity, dl->color);
	}

	if (r_hdr->integer && !r_pbrPhysical->integer)
	{
		float maxValue = MAX(r, MAX(g, b));
		if (maxValue > 1.0f)
		{
			// Renderer expects normalized color, in sdr there was no problem
			VectorScale(dl->color, 1.0f / maxValue, dl->color);
			// Don't scale the radius
			//dl->radius *= maxValue;
		}
		dl->radius = MIN(dl->radius, 65535.0f);
	}

	dl->additive = additive;
	return dl;
}

/*
========================
RE_AddPhysicalLightToScene

Optional API boundary. Physical values never leak as unit tags into shaders:
the dlight stored below is already canonical scene candela and Y-normalized RGB.
========================
*/
void RE_AddPhysicalLightToScene(const refPhysicalLight_t *light)
{
	if (!light || !r_pbrPhysical->integer || !tr.registered ||
		light->intensity <= 0.0f || light->range <= 0.0f)
		return;
	if (light->type != PHYSICALLIGHT_POINT && light->type != PHYSICALLIGHT_SPOT)
		return;

	const float outer = Com_Clamp(0.1f, 89.0f, light->outerAngle);
	const float inner = Com_Clamp(0.0f, outer, light->innerAngle);
	const float cosOuter = cosf(outer * (float)M_PI / 180.0f);
	const float cosInner = Q_min(1.0f, Q_max(cosf(inner * (float)M_PI / 180.0f),
		cosOuter + 0.002f));
	float candela = light->intensity;
	if (light->unit == PHYSICALUNIT_LUMENS)
	{
		if (light->type == PHYSICALLIGHT_SPOT)
			candela = Rend2PBR::LumensToSpotCandela(candela, cosInner, cosOuter);
		else
		{
			candela = Rend2PBR::LumensToOmniCandela(candela);
		}
	}
	else if (light->unit != PHYSICALUNIT_CANDELA)
	{
		return;
	}

	float chroma[3];
	if (!Rend2PBR::NormalizeChromaticity(light->color, chroma))
		return;
	dlight_t *dl = R_AllocSceneDlight();
	if (!dl)
		return;
	VectorCopy(light->origin, dl->origin);
	dl->radius = light->range;
	dl->canonicalIntensity = Rend2PBR::PhysicalToScene(candela, R_PBRPhysicalToScene());
	dl->physicalIntensity = qtrue;
	dl->emitterRadius = Rend2PBR::kDefaultEmitterRadiusMeters *
		r_pbrWorldUnitsPerMeter->value;
	VectorScale(chroma, dl->canonicalIntensity, dl->color);
	dl->additive = (light->flags & SPOTLIGHT_ADDITIVE) ? qtrue : qfalse;
	if (light->type == PHYSICALLIGHT_SPOT)
	{
		VectorCopy(light->dir, dl->spotDir);
		if (VectorNormalize(dl->spotDir) <= 1.0e-6f)
		{
			dl->spotDir[2] = 1.0f;
		}
		dl->spot = qtrue;
		dl->spotCosInner = cosInner;
		dl->spotCosOuter = cosOuter;
		dl->spotNoShadow = (light->flags & SPOTLIGHT_NOSHADOW) ? qtrue : qfalse;
	}
}

/*
=====================
RE_AddDynamicLightToScene

=====================
*/
void RE_AddDynamicLightToScene( const vec3_t org, float intensity, float r, float g, float b, int additive ) {
	R_AddSceneDynamicLight( org, intensity, r, g, b, additive );
}

/*
=====================
RE_AddFogVolumeToScene

A local fog volume for this scene (tr_fogvolume.cpp), same lifetime as a
dynamic light. Validated and converted when the froxel volume is built.
=====================
*/
void RE_AddFogVolumeToScene( const refFogVolume_t *volume ) {
	if ( !tr.registered || !volume ) {
		return;
	}
	if ( r_numfogvolumes >= MAX_REF_FOG_VOLUMES ) {
		ri.Printf( PRINT_DEVELOPER, "RE_AddFogVolumeToScene: more than %d fog volumes this frame\n", MAX_REF_FOG_VOLUMES );
		return;
	}
	if ( !R_FogVolumeHasMedium(volume) ) {
		return;
	}
	backEndData->fogVolumes[r_numfogvolumes++] = *volume;
}

/*
=====================
RE_AddVolumetricParticleToScene

The participating medium of an FX particle for this scene
(tr_volparticle.cpp), same lifetime as a dynamic light. Culled and capped
when the froxel volume is built.
=====================
*/
void RE_AddVolumetricParticleToScene( const refVolParticle_t *particle ) {
	if ( !tr.registered || !particle || !r_volumetricParticles->integer ) {
		return;
	}
	if ( r_numvolparticles >= MAX_REF_VOL_PARTICLES ) {
		r_volumetricParticlesRejected++;
		return;
	}
	if ( Q_isnan(particle->origin[0]) || Q_isnan(particle->origin[1]) || Q_isnan(particle->origin[2]) ||
		!(particle->radius > 0.0f) || !R_VolParticleHasMedium(particle) ) {
		r_volumetricParticlesRejected++;
		return;
	}
	backEndData->volParticles[r_numvolparticles++] = *particle;
}

/*
=====================
RE_AddLightToScene

=====================
*/
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, intensity, r, g, b, qfalse );
}

/*
=====================
RE_AddAdditiveLightToScene

=====================
*/
void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, intensity, r, g, b, qtrue );
}

void RE_BeginScene(const refdef_t *fd)
{
	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;
	tr.refdef.frameTime = MIN(fd->time - tr.refdef.lastTime, 50.f);

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for (i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}

	tr.refdef.sunDir[3] = 0.0f;
	tr.refdef.sunCol[3] = 1.0f;
	tr.refdef.sunAmbCol[3] = 1.0f;

	VectorCopy(tr.sunDirection, tr.refdef.sunDir);
	if ( (fd->rdflags & RDF_NOWORLDMODEL) || !(r_depthPrepass->value) ){
		tr.refdef.colorScale = 1.0f;
		VectorSet(tr.refdef.sunCol, 0, 0, 0);
		VectorSet(tr.refdef.sunAmbCol, 0, 0, 0);
	}
	else
	{
		tr.refdef.colorScale = r_forceSun->integer ? r_forceSunMapLightScale->value : tr.mapLightScale;

		if (r_sunlightMode->integer == 1)
		{
			tr.refdef.sunCol[0] =
			tr.refdef.sunCol[1] =
			tr.refdef.sunCol[2] = 1.0f;

			tr.refdef.sunAmbCol[0] =
			tr.refdef.sunAmbCol[1] =
			tr.refdef.sunAmbCol[2] = r_forceSun->integer ? r_forceSunAmbientScale->value : tr.sunShadowScale;
		}
		else
		{
			float scale = pow(2.0f, r_mapOverBrightBits->integer - tr.overbrightBits - 8);
			if (r_forceSun->integer)
			{
				VectorScale(tr.sunLight, scale * r_forceSunLightScale->value,   tr.refdef.sunCol);
				VectorScale(tr.sunLight, scale * r_forceSunAmbientScale->value, tr.refdef.sunAmbCol);
			}
			else
			{
				VectorScale(tr.sunLight, scale,                     tr.refdef.sunCol);
				VectorScale(tr.sunLight, scale * tr.sunShadowScale, tr.refdef.sunAmbCol);
			}
		}

		if (r_forceSun->integer == 2)
		{
			vec4_t lightDir, lightCol;
			int scale = 32768;
			float angle = (fd->time % scale) / (float)scale * M_PI;
			lightDir[0] = cos(angle);
			lightDir[1] = sin(35.0f * M_PI / 180.0f);
			lightDir[2] = sin(angle) * cos(35.0f * M_PI / 180.0f);
			lightDir[3] = 0.0f;

			lightCol[0] =
				lightCol[1] =
				lightCol[2] = CLAMP(sin(angle) * 2.0f, 0.0f, 1.0f) * 2.0f;
			lightCol[3] = 1.0f;

			VectorCopy4(lightDir, tr.refdef.sunDir);
			VectorCopy4(lightCol, tr.refdef.sunCol);
			VectorScale4(lightCol, 0.2f, tr.refdef.sunAmbCol);
		}

		// r_linearLighting: these scale gamma space lighting, convert them
		// for linear light
		if (tr.forcedLinearLight)
		{
			tr.refdef.colorScale = powf(tr.refdef.colorScale, 2.2f);
			for (int i = 0; i < 3; i++)
			{
				tr.refdef.sunCol[i] = (float)sRGBtoRGB(tr.refdef.sunCol[i]);
				tr.refdef.sunAmbCol[i] = (float)sRGBtoRGB(tr.refdef.sunAmbCol[i]);
			}
		}
	}

	if (r_forceAutoExposure->integer)
	{
		tr.refdef.autoExposureMinMax[0] = r_forceAutoExposureMin->value;
		tr.refdef.autoExposureMinMax[1] = r_forceAutoExposureMax->value;
	}
	else
	{
		tr.refdef.autoExposureMinMax[0] = tr.autoExposureMinMax[0];
		tr.refdef.autoExposureMinMax[1] = tr.autoExposureMinMax[1];
	}

	if (r_forceToneMap->integer)
	{
		tr.refdef.toneMinAvgMaxLinear[0] = pow(2, r_forceToneMapMin->value);
		tr.refdef.toneMinAvgMaxLinear[1] = pow(2, r_forceToneMapAvg->value);
		tr.refdef.toneMinAvgMaxLinear[2] = pow(2, r_forceToneMapMax->value);
	}
	else
	{
		tr.refdef.toneMinAvgMaxLinear[0] = pow(2, tr.toneMinAvgMaxLevel[0]);
		tr.refdef.toneMinAvgMaxLinear[1] = pow(2, tr.toneMinAvgMaxLevel[1]);
		tr.refdef.toneMinAvgMaxLinear[2] = pow(2, tr.toneMinAvgMaxLevel[2]);
	}

	// r_linearLighting: the tone parameters were chosen for a display-encoded
	// buffer. Convert them to linear light, so exposure and the legacy curve
	// behave the same on the linear buffer (see also R_UpdateFixedExposureLevel)
	if (tr.forcedLinearLight)
	{
		tr.refdef.autoExposureMinMax[0] *= 2.2f;
		tr.refdef.autoExposureMinMax[1] *= 2.2f;
		tr.refdef.toneMinAvgMaxLinear[0] = powf(tr.refdef.toneMinAvgMaxLinear[0], 2.2f);
		tr.refdef.toneMinAvgMaxLinear[1] = powf(tr.refdef.toneMinAvgMaxLinear[1], 2.2f);
		tr.refdef.toneMinAvgMaxLinear[2] = powf(tr.refdef.toneMinAvgMaxLinear[2], 2.2f);
	}

	// Makro - copy exta info if present
	if (fd->rdflags & RDF_EXTRA) {
		const refdefex_t* extra = (const refdefex_t*) (fd+1);

		tr.refdef.blurFactor = extra->blurFactor;

		if (fd->rdflags & RDF_SUNLIGHT)
		{
			VectorCopy(extra->sunDir,    tr.refdef.sunDir);
			VectorCopy(extra->sunCol,    tr.refdef.sunCol);
			VectorCopy(extra->sunAmbCol, tr.refdef.sunAmbCol);
		}
	}
	else
	{
		tr.refdef.blurFactor = 0.0f;
	}

	// derived info

	tr.refdef.floatTime = tr.refdef.time * 0.001f;

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	// r_forwardPlusSpawnTestLights (developer only), before the scene takes its lights
	R_ForwardPlusAddTestLights(fd);

	// r_ltcAreaLights: the map's area lights (and r_ltcDebug 6 / 7 polygons)
	R_AddAreaLightsToScene(fd);
	R_PBRTestLightsBeginScene(fd);

	// r_spot test lights, r_spotLightDebug cones of the scene's spot lights
	R_SpotLightsBeginScene(fd, r_firstSceneDlight);

	// r_foliageInteraction: the character colliders of this scene (and the
	// r_foliageInteractionDebug 1 capsules)
	R_FoliageInteractionBeginScene(fd);
	R_WaterBodiesDebugDraw(fd);
	R_LuxelDebugDraw(fd);

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];

	// local fog volumes: the map's and r_fogvol's, then the scene's list
	R_FogVolumesBeginScene(fd);
	tr.refdef.num_fogVolumes = r_numfogvolumes - r_firstSceneFogVolume;
	tr.refdef.fogVolumes = &backEndData->fogVolumes[r_firstSceneFogVolume];
	tr.refdef.num_volParticles = r_numvolparticles - r_firstSceneVolParticle;
	tr.refdef.volParticles = &backEndData->volParticles[r_firstSceneVolParticle];

	// Add the decals here because decals add polys and we need to ensure
	// that the polys are added before the the renderer is prepared
	if ( !(fd->rdflags & RDF_NOWORLDMODEL) )
		R_AddDecals();

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	tr.refdef.num_pshadows = 0;
	tr.refdef.pshadows = &backEndData->pshadows[0];

	// turn off dynamic lighting globally by clearing all the
	// dlights if it needs to be disabled or if vertex lighting is enabled
	if ( r_dynamiclight->integer == 0 ||
		 r_vertexLight->integer == 1 ) {
		tr.refdef.num_dlights = 0;
	}

	if (fd->rdflags & RDF_SKYBOXPORTAL)
	{
		tr.world->skyboxportal = 1;

		// Don't update constants yet. Store everything and render everything next scene
		return;
	}
	else if (backEndData->currentFrame->currentScene == 0)
	{
		// cdr - only change last time for the real render, not the portal
		// nor other scenes to be rendered like ui scenes
		tr.refdef.lastTime = fd->time;
	}

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;
	tr.portalRenderedThisFrame = qfalse;

	//ri.Printf(PRINT_ALL, "RE_BeginScene Frame: %i, skyportal: %i, entities: %i\n", backEndData->realFrameNumber, int(tr.world->skyboxportal && (tr.refdef.rdflags & RDF_SKYBOXPORTAL)), tr.refdef.num_entities);
	const auto gatherStart = std::chrono::steady_clock::now();
	R_GatherFrameViews(&tr.refdef);
	if ( r_shadowStats->integer )
		shadowStats.gatherUs += R_ShadowStatsUs(gatherStart);
	RB_UpdateConstants(&tr.refdef);

	R_PushDebugGroup(AL_SCENE, va("Scene_%i", backEndData->currentFrame->currentScene));
}

/*
r_shadowVerify 1: on the next frame every point light cube is rendered by the
6 separate face views and by the single pass (r_shadowSinglePass), the depth
of the 6 faces is read back after each, and the texels that differ are
printed. Resets itself to 0.
*/
static void R_ShadowCubeReadFaces( const viewParms_t *faces, std::vector<float> &depth )
{
	const int texels = DSHADOW_MAP_SIZE * DSHADOW_MAP_SIZE;
	depth.resize(texels * 6);
	FBO_t *oldFbo = glState.currentFBO;
	qglPixelStorei(GL_PACK_ALIGNMENT, 4);
	for ( int f = 0; f < 6; f++ )
	{
		FBO_Bind(faces[f].targetFbo);
		qglReadPixels(0, 0, DSHADOW_MAP_SIZE, DSHADOW_MAP_SIZE, GL_DEPTH_COMPONENT, GL_FLOAT,
			depth.data() + f * texels);
	}
	FBO_Bind(oldFbo);
}

static void R_ShadowCubeVerify( const viewParms_t *faces, int cubeIndex )
{
	std::vector<float> separate, single;

	// r_shadowLayered: the reference is the single pass with per face
	// commands, else the 6 separate face views
	const bool layered = r_shadowLayered->integer != 0;
	int faceSurfs[6] = { 1, 1, 1, 1, 1, 1 };
	if ( layered )
	{
		tr.refdef.numDrawSurfs = 0;
		R_RenderCubeShadowView(faces, false, faceSurfs);
		R_IssuePendingRenderCommands();
	}
	else
	{
		for ( int f = 0; f < 6; f++ )
		{
			tr.refdef.numDrawSurfs = 0;
			R_RenderView(const_cast<viewParms_t *>(&faces[f]));
			R_IssuePendingRenderCommands();
		}
	}
	R_ShadowCubeReadFaces(faces, separate);

	tr.refdef.numDrawSurfs = 0;
	backEnd.cubeLayerUnsupported = 0;
	const bool layeredUsed = R_RenderCubeShadowView(faces, layered);
	R_IssuePendingRenderCommands();
	R_ShadowCubeReadFaces(faces, single);
	if ( layered )
		ri.Printf(PRINT_ALL, "shadowVerify cube %d: layered %s, %d items without layer output\n", cubeIndex,
			layeredUsed ? "used" : "NOT used (fallback)", backEnd.cubeLayerUnsupported);

	const int texels = DSHADOW_MAP_SIZE * DSHADOW_MAP_SIZE;
	int totalDiff = 0;
	for ( int f = 0; f < 6; f++ )
	{
		// a face without surfaces gets no command in the single pass (stale
		// content), the layered draw clears it
		if ( !faceSurfs[f] )
		{
			ri.Printf(PRINT_ALL, "shadowVerify cube %d face %d: no surfaces, not compared\n", cubeIndex, f);
			continue;
		}
		int diff = 0, covered = 0;
		float maxDiff = 0.0f;
		for ( int t = 0; t < texels; t++ )
		{
			const float a = separate[f * texels + t];
			const float b = single[f * texels + t];
			if ( a < 1.0f )
				covered++;
			if ( a != b )
			{
				diff++;
				maxDiff = Q_max(maxDiff, fabsf(a - b));
			}
		}
		totalDiff += diff;
		ri.Printf(PRINT_ALL, "shadowVerify cube %d face %d: %d texels covered, %d differ (max %g)\n",
			cubeIndex, f, covered, diff, maxDiff);
	}
	ri.Printf(PRINT_ALL, "shadowVerify cube %d (light at %.0f %.0f %.0f r %.0f): %s\n", cubeIndex,
		faces[0].ori.origin[0], faces[0].ori.origin[1], faces[0].ori.origin[2], faces[0].zFar,
		totalDiff ? "MISMATCH" : "identical");
}

// Compare the final static+dynamic result with an uncached render using the
// identical stable projection. The reference overwrites only the working
// layer, leaving both persistent banks intact for the next scroll/hit.
static void R_ReadSunShadowDepth(FBO_t *target, std::vector<float>& depth)
{
	FBO_t *oldFbo = glState.currentFBO;
	GLint packBuffer, packAlignment;
	qglGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	qglPixelStorei(GL_PACK_ALIGNMENT, 4);
	FBO_Bind(target);
	qglReadPixels(0, 0, target->width, target->height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
	FBO_Bind(oldFbo);
	qglPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
}

static void R_VerifySunShadowView(const viewParms_t& cached)
{
	FBO_t *target = cached.targetFbo;
	const size_t pixels = (size_t)target->width * target->height;
	std::vector<float> actual(pixels), reference(pixels);
	R_ReadSunShadowDepth(target, actual);

	viewParms_t full = cached;
	full.flags &= ~(VPF_SUN_DYNAMIC_SHADOW | VPF_NOCLEAR);
	full.flags |= VPF_SUN_VERIFY_SHADOW;
	tr.refdef.numDrawSurfs = 0;
	R_RenderView(&full);
	R_IssuePendingRenderCommands();
	R_ReadSunShadowDepth(target, reference);
	// Preserve the actual cached working layer, so the next verified frame
	// exercises partial restoration as well (not a fresh full-copy fallback).
	GLint unpackBuffer, unpackAlignment;
	qglGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
	qglGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
	qglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	GL_SelectTexture(0);
	GL_BindToTMU(tr.sunShadowArrayImage, 0);
	qglTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, cached.targetFboLayer,
		target->width, target->height, 1, GL_DEPTH_COMPONENT, GL_FLOAT, actual.data());
	qglPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
	qglBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);

	size_t different = 0, significant = 0, outsideFootprint = 0;
	float maxError = 0.0f;
	const int level = cached.targetFboLayer;
	const float epsilon = Q_max(8.0f / 16777215.0f,
		0.05f * tr.refdef.sunShadowTexelSize[level] / Q_max(1.0f, tr.refdef.sunShadowDepthSpan[level]));
	for (size_t i = 0; i < pixels; ++i)
	{
		const float error = fabsf(actual[i] - reference[i]);
		different += actual[i] != reference[i];
		significant += error > 2.0f / 16777215.0f;
		maxError = Q_max(maxError, error);
		if (error > 2.0f / 16777215.0f)
		{
			// Scrolling reuses rasterized triangles at an integer translation.
			// GPU clipping/interpolation need not be bit-invariant under that
			// translation. Keep the strict D24 metric, and separately test that
			// both depths remain inside the same one-texel surface footprint.
			float actualMin = actual[i], actualMax = actual[i];
			float referenceMin = reference[i], referenceMax = reference[i];
			const int x = (int)(i % target->width), y = (int)(i / target->width);
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (x + dx < 0 || x + dx >= target->width || y + dy < 0 || y + dy >= target->height)
						continue;
					const size_t n = (size_t)(y + dy) * target->width + x + dx;
					actualMin = Q_min(actualMin, actual[n]);
					actualMax = Q_max(actualMax, actual[n]);
					referenceMin = Q_min(referenceMin, reference[n]);
					referenceMax = Q_max(referenceMax, reference[n]);
				}
			outsideFootprint += actual[i] < referenceMin - epsilon || actual[i] > referenceMax + epsilon ||
				reference[i] < actualMin - epsilon || reference[i] > actualMax + epsilon;
		}
	}
	ri.Printf(PRINT_ALL,
		"sunShadowVerify cascade %d: %s, %u/%u differ, %u above 2 D24 steps, max %g, %u outside one-texel footprint\n",
		cached.targetFboLayer, outsideFootprint ? "MISMATCH" : "PASS",
		(unsigned)different, (unsigned)pixels, (unsigned)significant, maxError, (unsigned)outsideFootprint);
}

void RE_EndScene()
{
	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstSceneFogVolume = r_numfogvolumes;
	r_firstSceneVolParticle = r_numvolparticles;
	r_firstScenePoly = r_numpolys;
	tr.skyPortalEntities = 0;
	tr.numCachedViewParms = 0;
	tr.refdef.doLAGoggles = false;
	tr.refdef.doFullbright = false;
	backEndData->currentFrame->currentScene++;
}

/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *fd )
{
	int				startTime;

	if ( !tr.registered ) {
		return;
	}
	GLimp_LogComment( "====== RE_RenderScene =====\n" );

	if ( r_norefresh->integer ) {
		return;
	}

	// Picks up changes of r_colorGrading* and of the map LUT
	R_UpdateColorGrading();

	startTime = ri.Milliseconds();

	if (backEndData->currentFrame->currentScene >= MAX_SCENES) {
		ri.Error(ERR_DROP, "Tried to render to many subscenes per frame. Developer: Increase MAX_SCENES");
	}

	if (!tr.world && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

	RE_BeginScene(fd);

	// Store skyportal info and don't render yet
	if (tr.refdef.rdflags & RDF_SKYBOXPORTAL)
	{
		viewParms_t		parms;
		Com_Memset(&parms, 0, sizeof(parms));
		parms.viewportX = fd->x;
		parms.viewportY = glConfig.vidHeight - (fd->y + fd->height);
		parms.viewportWidth = fd->width;
		parms.viewportHeight = fd->height;
		parms.isPortal = qfalse;
		parms.isSkyPortal = qfalse;
		parms.zNear = r_znear->value;

		parms.fovX = fd->fov_x;
		parms.fovY = fd->fov_y;

		parms.stereoFrame = tr.refdef.stereoFrame;

		VectorCopy(fd->vieworg, parms.ori.origin);
		VectorCopy(fd->viewaxis[0], parms.ori.axis[0]);
		VectorCopy(fd->viewaxis[1], parms.ori.axis[1]);
		VectorCopy(fd->viewaxis[2], parms.ori.axis[2]);

		VectorCopy(fd->vieworg, parms.pvsOrigin);

		parms.isSkyPortal = qtrue;
		Com_Memcpy(&tr.skyPortalParms, &parms, sizeof(viewParms_t));
		Com_Memcpy(tr.skyPortalAreaMask, tr.refdef.areamask, sizeof(tr.refdef.areamask));
		tr.skyPortalEntities = tr.refdef.num_entities;
		return;
	}

	// Render all the passes
	int verifiedCubes = 0;
	for (int i = 0; i < tr.numCachedViewParms; i++)
	{
		const char *viewParmTypeNames[VPT_ALL] = {
			"Skyportal View",
			"Sunshadow Cascade",
			"Player Shadowmap",
			"Pointlight Shadow",
			"LTC Shadow",
			"Portal View",
			"Main View"};
		const char *viewName = viewParmTypeNames[tr.cachedViewParms[i].viewParmType];
		if (tr.cachedViewParms[i].flags & VPF_LTC_STATIC_SHADOW)
			viewName = "LTC static shadow build";
		else if (tr.cachedViewParms[i].flags & VPF_LTC_DYNAMIC_SHADOW)
			viewName = "LTC dynamic caster shadow";
		else if (tr.cachedViewParms[i].flags & VPF_LTC_SABER_SHADOW)
			viewName = "LTC saber reference shadow";
		else if (tr.cachedViewParms[i].flags & VPF_SUN_STATIC_SHADOW)
			viewName = "Sun static shadow build";
		else if (tr.cachedViewParms[i].flags & VPF_SUN_DYNAMIC_SHADOW)
			viewName = "Sun dynamic shadow overlay";
		R_PushDebugGroup(AL_VIEW, va(
			"%s_%i",
			viewName,
			i));
		qhandle_t timer = R_BeginTimedBlockCmd(va(
			"%s %i",
			viewName,
			i));
		if (tr.cachedViewParms[i].viewParmType == VPT_MAIN)
			R_ForwardPlusSetMainViewTimer(timer);
		tr.refdef.numDrawSurfs = 0;
		// r_shadowSinglePass: the 6 faces of a point light cube in one walk
		const bool cubeGroup = r_shadowSinglePass->integer &&
			(tr.cachedViewParms[i].flags & VPF_POINTCUBE) &&
			i + 5 < tr.numCachedViewParms &&
			(tr.cachedViewParms[i + 5].flags & VPF_POINTCUBE);
		if ( cubeGroup && r_shadowVerify->integer )
		{
			R_ShadowCubeVerify(&tr.cachedViewParms[i], i);
			R_EndTimedBlockCmd(timer);
			R_IssuePendingRenderCommands();
			verifiedCubes++;
			i += 5;
		}
		else if ( cubeGroup )
		{
			std::chrono::steady_clock::time_point frontStart;
			if ( r_shadowStats->integer )
				frontStart = std::chrono::steady_clock::now();
			R_RenderCubeShadowView(&tr.cachedViewParms[i], true);
			if ( r_shadowStats->integer )
			{
				const double frontUs = R_ShadowStatsUs(frontStart);
				shadowStats.drawSurfs[SSTAT_POINTCUBE] += tr.refdef.numDrawSurfs;
				const auto submitStart = std::chrono::steady_clock::now();
				R_EndTimedBlockCmd(timer);
				R_IssuePendingRenderCommands();
				shadowStats.submitUs[SSTAT_POINTCUBE] += R_ShadowStatsUs(submitStart);
				shadowStats.frontUs[SSTAT_POINTCUBE] += frontUs;
				shadowStats.frontMaxUs[SSTAT_POINTCUBE] = Q_max(shadowStats.frontMaxUs[SSTAT_POINTCUBE], frontUs);
				shadowStats.views[SSTAT_POINTCUBE]++;
			}
			else
			{
				R_EndTimedBlockCmd(timer);
				R_IssuePendingRenderCommands();
			}
			i += 5;
		}
		else if ( r_shadowStats->integer )
		{
			const int statClass = R_ShadowStatsClass(&tr.cachedViewParms[i]);
			const auto frontStart = std::chrono::steady_clock::now();
			R_RenderView(&tr.cachedViewParms[i]);
			const double frontUs = R_ShadowStatsUs(frontStart);
			shadowStats.drawSurfs[statClass] += tr.refdef.numDrawSurfs;
			const auto submitStart = std::chrono::steady_clock::now();
			R_EndTimedBlockCmd(timer);
			R_IssuePendingRenderCommands();
			shadowStats.submitUs[statClass] += R_ShadowStatsUs(submitStart);
			shadowStats.frontUs[statClass] += frontUs;
			shadowStats.frontMaxUs[statClass] = Q_max(shadowStats.frontMaxUs[statClass], frontUs);
			shadowStats.views[statClass]++;
		}
		else
		{
			R_RenderView(&tr.cachedViewParms[i]);
			R_EndTimedBlockCmd(timer);
			R_IssuePendingRenderCommands();
		}
		if (r_sunShadowVerify->integer &&
			(tr.cachedViewParms[i].flags & VPF_SUN_DYNAMIC_SHADOW))
		{
			R_VerifySunShadowView(tr.cachedViewParms[i]);
		}
	}
	if (r_sunShadowVerify->integer == 1 && !(fd->rdflags & RDF_NOWORLDMODEL))
		ri.Cvar_Set("r_sunShadowVerify", "0");

	if ( r_shadowVerify->integer && !(fd->rdflags & RDF_NOWORLDMODEL) )
	{
		ri.Printf(PRINT_ALL, "shadowVerify: %d point light cubes compared (%s)%s\n", verifiedCubes,
			r_shadowLayered->integer ? "single pass vs layered" : "separate faces vs single pass",
			r_shadowSinglePass->integer ? "" : " (needs r_shadowSinglePass 1)");
		ri.Cvar_Set("r_shadowVerify", "0");
	}

	if(!( fd->rdflags & RDF_NOWORLDMODEL ))
	{
		R_PushDebugGroup(AL_VIEW, "Post processing");
		qhandle_t timer = R_BeginTimedBlockCmd( "Post processing" );
		R_AddPostProcessCmd();
		R_EndTimedBlockCmd( timer );
	}

	R_IssuePendingRenderCommands();

	RE_EndScene();
	R_PushDebugGroup(AL_VIEW, "2D Pass");

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}
