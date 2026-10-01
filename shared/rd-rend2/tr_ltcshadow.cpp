/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.
OpenJK is free software under the GNU General Public License version 2.
===========================================================================
*/

#include "tr_local.h"

#include <algorithm>
#include <cstdint>

struct ltcShadowCacheEntry_t
{
	int areaId;
	uint64_t lastUsedFrame;
	qboolean complete;
};

static ltcShadowCacheEntry_t s_cache[LTC_STATIC_CACHE_SLOTS];
static uint64_t s_generation;
static int s_buildFrame = -1;
static int s_hits, s_misses, s_evictions, s_builds, s_dynamicViews;
static int s_buildsThisFrame, s_dynamicLightsThisFrame, s_saberProxyCubes, s_saberReferenceViews;
static int s_statsFrame = -1;

int R_LtcShadowCacheSlots(void)
{
	if (!tr.ltcShadowArrayImage)
		return 0;
	return Q_min(LTC_STATIC_CACHE_SLOTS,
		(tr.ltcShadowArrayImage->layers - LTC_EXTRA_SHADOW_LAYERS) /
		LTC_SHADOW_LAYERS_PER_LIGHT);
}

void R_LtcShadowInvalidate(void)
{
	R_LtcSaberScreenInvalidate();
	for (int i = 0; i < LTC_STATIC_CACHE_SLOTS; ++i)
	{
		s_cache[i].areaId = -1;
		s_cache[i].lastUsedFrame = 0;
		s_cache[i].complete = qfalse;
	}
	++s_generation;
	s_buildFrame = -1;
	s_hits = s_misses = s_evictions = s_builds = s_dynamicViews = 0;
	s_buildsThisFrame = s_dynamicLightsThisFrame = 0;
	s_saberProxyCubes = s_saberReferenceViews = 0;
	s_statsFrame = -1;
}

void R_LtcShadowStats_f(void)
{
	int resident = 0;
	for (int i = 0; i < R_LtcShadowCacheSlots(); ++i)
		resident += s_cache[i].complete ? 1 : 0;
	ri.Printf(PRINT_ALL, "LTC shadow cache: %d/%d resident, %d hits, %d misses, %d builds (%d this frame), %d evictions (generation %llu)\n",
		resident, R_LtcShadowCacheSlots(), s_hits, s_misses, s_builds,
		s_buildsThisFrame, s_evictions,
		(unsigned long long)s_generation);
	ri.Printf(PRINT_ALL, "LTC dynamic: %d lights, %d views this frame (%d lifetime)\n",
		s_dynamicLightsThisFrame, s_dynamicLightsThisFrame * LTC_SHADOW_LAYERS_PER_LIGHT,
		s_dynamicViews);
	ri.Printf(PRINT_ALL, "LTC saber: %d proxy cubes, %d reference views this frame\n",
		s_saberProxyCubes, s_saberReferenceViews);
	R_LtcSaberScreenStats_f();
}

static qboolean R_LtcShadowSlotNeeded(const trRefdef_t *refdef, int areaId)
{
	for (int i = 0; i < refdef->num_dlights; ++i)
		if (refdef->dlights[i].areaType == DLIGHT_RECT &&
			refdef->dlights[i].areaId == areaId &&
			refdef->dlights[i].areaShadowSlot >= 0)
			return qtrue;
	return qfalse;
}

static int R_LtcShadowReserve(const trRefdef_t *refdef, const dlight_t *light,
	qboolean *build)
{
	*build = qfalse;
	const int slots = R_LtcShadowCacheSlots();
	for (int i = 0; i < slots; ++i)
	{
		if (s_cache[i].areaId != light->areaId)
			continue;
		s_cache[i].lastUsedFrame = (uint64_t)tr.frameCount;
		if (s_cache[i].complete)
			++s_hits;
		return s_cache[i].complete ? i : -1;
	}
	++s_misses;
	if (s_buildFrame == tr.frameCount)
		return -1;

	int candidate = -1;
	uint64_t oldest = UINT64_MAX;
	for (int i = 0; i < slots; ++i)
	{
		if (s_cache[i].areaId < 0)
		{
			candidate = i;
			break;
		}
		if (s_cache[i].lastUsedFrame < oldest &&
			!R_LtcShadowSlotNeeded(refdef, s_cache[i].areaId))
		{
			oldest = s_cache[i].lastUsedFrame;
			candidate = i;
		}
	}
	if (candidate < 0)
		return -1;
	if (s_cache[candidate].areaId >= 0)
		++s_evictions;
	s_cache[candidate].areaId = light->areaId;
	s_cache[candidate].lastUsedFrame = (uint64_t)tr.frameCount;
	s_cache[candidate].complete = qtrue;
	s_buildFrame = tr.frameCount;
	++s_builds;
	++s_buildsThisFrame;
	*build = qtrue;
	return candidate;
}

static void R_LtcShadowSampleOrigin(const dlight_t *light, int sample, vec3_t origin)
{
	const float gauss = 0.5773502691896258f;
	const float u = (sample & 1) ? gauss : -gauss;
	const float v = (sample & 2) ? gauss : -gauss;
	VectorMA(light->origin, u * light->halfWidth, light->areaRight, origin);
	VectorMA(origin, v * light->halfHeight, light->areaUp, origin);
}

static void R_LtcShadowAppendCube(const dlight_t *light, const vec3_t origin,
	float farPlane, int layerBase, int flags)
{
	static const vec3_t axes[6][3] = {
		{{-1,0,0},{0,0,-1},{0,1,0}},
		{{ 1,0,0},{0,0, 1},{0,1,0}},
		{{0,-1,0},{1,0,0},{0,0,-1}},
		{{0, 1,0},{1,0,0},{0,0, 1}},
		{{0,0,-1},{1,0,0},{0,1,0}},
		{{0,0, 1},{-1,0,0},{0,1,0}}
	};
	(void)light;
	for (int face = 0; face < 6; ++face)
	{
		if (tr.numCachedViewParms >= (int)ARRAY_LEN(tr.cachedViewParms))
			return;
		viewParms_t parms;
		Com_Memset(&parms, 0, sizeof(parms));
		parms.viewportWidth = parms.viewportHeight = LTC_STATIC_SHADOW_SIZE;
		parms.fovX = parms.fovY = 90.0f;
		parms.flags = VPF_DEPTHSHADOW | VPF_NOVIEWMODEL | VPF_POINTSHADOW | flags;
		parms.zNear = 1.0f;
		parms.zFar = Q_max(farPlane, 2.0f);
		VectorCopy(origin, parms.ori.origin);
		for (int axis = 0; axis < 3; ++axis)
			VectorCopy(axes[face][axis], parms.ori.axis[axis]);
		parms.targetFbo = tr.ltcShadowScratchFbo;
		parms.targetFboLayer = layerBase + face;
		parms.currentViewParm = tr.numCachedViewParms;
		parms.viewParmType = VPT_LTC_SHADOWS;
		R_RotateForViewer(&parms.world, &parms);
		R_SetupProjection(&parms, parms.zNear, parms.zFar, qtrue);
		R_SetupProjectionZ(&parms);
		tr.cachedViewParms[tr.numCachedViewParms++] = parms;
	}
}

static qboolean R_LtcShadowHasCharacter(const trRefdef_t *refdef, const dlight_t *light,
	float *score)
{
	float nearest = 1e30f;
	float nearestCamera = 1e30f;
	for (int i = 0; i < refdef->num_entities; ++i)
	{
		const refEntity_t *ent = &refdef->entities[i].e;
		if (ent->reType != RT_MODEL || !ent->ghoul2)
			continue;
		const float distanceSq = DistanceSquared(ent->origin, light->origin);
		const float influence = light->radius + 64.0f;
		if (distanceSq < influence * influence)
		{
			nearest = Q_min(nearest, distanceSq);
			nearestCamera = Q_min(nearestCamera,
				DistanceSquared(ent->origin, refdef->vieworg));
		}
	}
	if (nearest == 1e30f)
		return qfalse;
	const float luminance = 0.2126f * light->color[0] +
		0.7152f * light->color[1] + 0.0722f * light->color[2];
	*score = luminance * 4.0f * light->halfWidth * light->halfHeight /
		Q_max(nearest, 64.0f) /
		(1.0f + nearestCamera / Q_max(light->radius * light->radius, 64.0f));
	return qtrue;
}

void R_LtcShadowGatherViews(trRefdef_t *refdef)
{
	if (s_statsFrame != tr.frameCount)
	{
		s_statsFrame = tr.frameCount;
		s_buildsThisFrame = s_dynamicLightsThisFrame = 0;
		s_saberProxyCubes = s_saberReferenceViews = 0;
	}
	for (int i = 0; i < refdef->num_dlights; ++i)
	{
		refdef->dlights[i].areaShadowSlot = -1;
		refdef->dlights[i].areaDynamicShadowSlot = -1;
		if (refdef->dlights[i].areaType == DLIGHT_LINE &&
			R_ForwardPlusLightShadowSlot(i) >= 0)
			++s_saberProxyCubes;
	}
	if (!tr.ltcShadowArrayImage || !tr.ltcShadowScratchFbo || !tr.world ||
		!R_AreaLightsActive() ||
		(refdef->rdflags & RDF_NOWORLDMODEL))
		return;

	if (r_ltcStaticShadows->integer > 0)
	{
		for (int i = 0; i < refdef->num_dlights; ++i)
		{
			dlight_t *light = &refdef->dlights[i];
			if (light->areaType != DLIGHT_RECT || light->areaId < 0 ||
				(light->areaFlags & AREALIGHT_DYNAMIC))
				continue;
			qboolean build;
			const int slot = R_LtcShadowReserve(refdef, light, &build);
			light->areaShadowSlot = slot;
			if (!build)
				continue;
			for (int sample = 0; sample < LTC_STATIC_SHADOW_SAMPLES; ++sample)
			{
				vec3_t origin;
				R_LtcShadowSampleOrigin(light, sample, origin);
				R_LtcShadowAppendCube(light, origin, light->radius + light->areaHalfDiagonal,
					(slot * LTC_STATIC_SHADOW_SAMPLES + sample) * 6,
					VPF_LTC_STATIC_SHADOW);
			}
		}
	}

	if (r_ltcStaticShadows->integer >= 2)
	{
	struct candidate_t { int light; float score; } candidates[MAX_RENDER_DLIGHTS];
	int count = 0;
	for (int i = 0; i < refdef->num_dlights && count < MAX_RENDER_DLIGHTS; ++i)
	{
		float score;
		if (refdef->dlights[i].areaShadowSlot < 0 ||
			!R_LtcShadowHasCharacter(refdef, &refdef->dlights[i], &score))
			continue;
		candidates[count++] = { i, score };
	}
	std::sort(candidates, candidates + count,
		[](const candidate_t& a, const candidate_t& b) { return a.score > b.score; });
	const int dynamicBase = R_LtcShadowCacheSlots() * LTC_SHADOW_LAYERS_PER_LIGHT;
	for (int slot = 0; slot < Q_min(count, LTC_DYNAMIC_SHADOW_LIGHTS); ++slot)
	{
		dlight_t *light = &refdef->dlights[candidates[slot].light];
		light->areaDynamicShadowSlot = slot;
		++s_dynamicLightsThisFrame;
		for (int sample = 0; sample < LTC_STATIC_SHADOW_SAMPLES; ++sample)
		{
			vec3_t origin;
			R_LtcShadowSampleOrigin(light, sample, origin);
			R_LtcShadowAppendCube(light, origin, light->radius + light->areaHalfDiagonal,
				dynamicBase + (slot * LTC_STATIC_SHADOW_SAMPLES + sample) * 6,
				VPF_LTC_DYNAMIC_SHADOW);
			s_dynamicViews += 6;
		}
	}
	}

	if (r_ltcSaberShadows->integer == 3 && r_dlightMode->integer >= 2)
	{
		const int saberBaseCube = R_LtcShadowCacheSlots() * LTC_STATIC_SHADOW_SAMPLES +
			LTC_DYNAMIC_SHADOW_LIGHTS * LTC_STATIC_SHADOW_SAMPLES;
		int saberSlot = 0;
		const float gauss = 0.7745966692414834f;
		for (int i = 0; i < refdef->num_dlights && saberSlot < LTC_SCREEN_SABERS; ++i)
		{
			dlight_t *light = &refdef->dlights[i];
			if (light->areaType != DLIGHT_LINE ||
				R_ForwardPlusLightShadowSlot(i) < 0)
				continue;
			light->areaDynamicShadowSlot = saberBaseCube + 2 * saberSlot;
			for (int sample = 0; sample < 2; ++sample)
			{
				vec3_t origin;
				VectorMA(light->origin, (sample ? gauss : -gauss) * light->halfWidth,
					light->areaRight, origin);
				R_LtcShadowAppendCube(light, origin, light->radius + light->halfWidth,
					(light->areaDynamicShadowSlot + sample) * 6,
					VPF_LTC_SABER_SHADOW);
				s_saberReferenceViews += 6;
			}
			++saberSlot;
		}
	}
	else if (r_ltcSaberShadows->integer == 2 && R_LtcSaberScreenResourcesEnabled())
	{
		struct saberCandidate_t { int light; float score; } candidates[MAX_RENDER_DLIGHTS];
		int count = 0;
		for (int i = 0; i < refdef->num_dlights && count < MAX_RENDER_DLIGHTS; ++i)
		{
			dlight_t *light = &refdef->dlights[i];
			if (light->areaType != DLIGHT_LINE ||
				R_ForwardPlusLightShadowSlot(i) < 0)
				continue;
			vec3_t toLight;
			VectorSubtract(light->origin, refdef->vieworg, toLight);
			const float distanceSq = Q_max(DotProduct(toLight, toLight), 64.0f);
			const float invDistance = 1.0f / sqrtf(distanceSq);
			const float axial = DotProduct(toLight, light->areaRight) * invDistance;
			const float projectedLength = light->halfWidth *
				sqrtf(Q_max(0.0f, 1.0f - axial * axial));
			const float luminance = 0.2126f * light->color[0] +
				0.7152f * light->color[1] + 0.0722f * light->color[2];
			const float influence = Q_min(1.0f,
				light->radius * light->radius / distanceSq);
			candidates[count++] = { i, luminance * projectedLength * influence * invDistance };
		}
		std::sort(candidates, candidates + count,
			[](const saberCandidate_t& a, const saberCandidate_t& b) {
				return a.score != b.score ? a.score > b.score : a.light < b.light;
			});
		for (int channel = 0; channel < Q_min(count, LTC_SCREEN_SABERS); ++channel)
			refdef->dlights[candidates[channel].light].areaDynamicShadowSlot = channel;
	}
}
