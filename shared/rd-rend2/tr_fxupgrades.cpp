/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/
// tr_fxupgrades.cpp -- opt-in upgrades of the legacy (efx) FX, see
// docs/rend2-fx-upgrades.md. Four features, each off by default with its own
// r_<feature>Compare:
//
//  r_fxLights        emissive FX sprites in the scene become short-lived,
//                    unshadowed point lights of the Forward+ list (merged,
//                    ranked, budgeted)
//  r_fxDistortion    heat haze / shock rings: a half resolution offset buffer
//                    from classified FX sprites, one refracting composite
//  r_fxGpuParticles  GL 4.3 compute pool of secondary sparks / embers / dust
//                    spawned by played efx events (GetRefFxEventAPI)
//  r_fxLayering      missing layers of stock impacts / explosions: sparks and
//                    dust (pool), a light, a shock ring, a scorch decal
//
// Front end: R_FxUpgradesBeginScene (world scenes, before the scene takes its
// lights). Back end: RB_FxUpgradesView, after the main pass of the main view.

#include "tr_local.h"
#include "fx/FxLayering.h"
#include "fx/FxPhysicalizationEmission.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

static cvar_t *r_fxLights;
static cvar_t *r_fxLightsMax;
static cvar_t *r_fxLightsScale;
static cvar_t *r_fxLightsMerge;
static cvar_t *r_fxDistortion;
static cvar_t *r_fxDistortionStrength;
static cvar_t *r_fxDistortionMax;
static cvar_t *r_fxGpuParticles;
static cvar_t *r_fxGpuParticlesDensity;
static cvar_t *r_fxLayering;

namespace {

const int FX_MAX_EVENTS = 256;
const int FX_MAX_CANDIDATES = 2048;
const int FX_MAX_LIGHT_CLUSTERS = 64;
const int FX_MAX_LAYER_LIGHTS = 64;
const int FX_MAX_BURSTS = 64;
const int FX_MAX_DISTORTION = 256;
const int FX_POOL = 8192;
const int FX_MAX_SPAWN = 1024;
const float FX_GRAVITY = 800.0f;

struct FxParticle { float p0[4], p1[4], p2[4], p3[4]; };
struct FxDistortionSource { vec3_t origin; float radius, strength, kind, age, seed; };
struct FxTimedLight { vec3_t origin; vec3_t color; float radius; int birth, life; };
struct FxBurst { vec3_t origin; float radius, strength; int birth, life; float seed; };
struct FxCluster { vec3_t origin; vec3_t color; float power; };

enum FxShaderKind { FXS_NONE = 0, FXS_EMISSIVE = 1, FXS_HEAT = 2, FXS_SHOCK = 4, FXS_FIRE_TINT = 8 };
struct FxShaderInfo { int kind; float radiance; float heat; };

// front end
refFxEvent_t s_events[FX_MAX_EVENTS];
int s_numEvents;
FxTimedLight s_layerLights[FX_MAX_LAYER_LIGHTS];
FxBurst s_bursts[FX_MAX_BURSTS];
FxCluster s_prevClusters[FX_MAX_LIGHT_CLUSTERS];
int s_numPrevClusters;
std::unordered_map<const shader_t *, FxShaderInfo> s_shaderInfo;
qhandle_t s_scorchShader = -1;
unsigned s_seed = 0x9e3779b9u;

// handed to the back end (single threaded: filled by the world scene, read
// by the main view of the same frame)
std::vector<FxParticle> s_spawn;
FxDistortionSource s_distortion[FX_MAX_DISTORTION];
int s_numDistortion;
int s_aliveUntil;		// scene time (ms) the last spawned particle dies

// back end resources (created on first use)
GLuint s_stateBuffer, s_spawnBuffer;
image_t *s_particleImage;
image_t *s_depthImage;
FBO_t *s_depthFbo;
image_t *s_offsetImage;
FBO_t *s_offsetFbo;
image_t *s_sceneCopyImage;
FBO_t *s_sceneCopyFbo;
image_t *s_instanceImage;
int s_ringHead;
bool s_poolFailed;

float Random01()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return (float)(s_seed >> 8) * (1.0f / 16777216.0f);
}

float RandomRange(float a, float b) { return a + (b - a) * Random01(); }

void RandomUnit(vec3_t out)
{
	for (;;)
	{
		VectorSet(out, RandomRange(-1, 1), RandomRange(-1, 1), RandomRange(-1, 1));
		const float l = VectorLength(out);
		if (l > 0.05f && l <= 1.0f)
		{
			VectorScale(out, 1.0f / l, out);
			return;
		}
	}
}

bool NameHas(const char *name, const char *key) { return FxLayering::Contains(name, key); }

// Classification of one FX material, cached per shader
const FxShaderInfo &ShaderInfo(const shader_t *shader)
{
	auto found = s_shaderInfo.find(shader);
	if (found != s_shaderInfo.end())
		return found->second;
	FxShaderInfo info = { FXS_NONE, 0.0f, 0.0f };
	const char *name = shader->name;
	const shaderStage_t *stage = shader->stages[0];
	const uint32_t blend = stage ? stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS) : 0;
	const bool additive = (blend & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_ONE &&
		((blend & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_ONE || (blend & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_SRC_ALPHA);
	const bool excluded = NameHas(name, "smoke") || NameHas(name, "dust") || NameHas(name, "steam") ||
		NameHas(name, "water") || NameHas(name, "splash") || NameHas(name, "mist") || NameHas(name, "rain") ||
		NameHas(name, "bubble") || NameHas(name, "glass") || NameHas(name, "blood") || NameHas(name, "shadow");
	const bool fire = NameHas(name, "fire") || NameHas(name, "flame") || NameHas(name, "burn") ||
		NameHas(name, "explos") || NameHas(name, "ember");
	if (additive && !excluded)
	{
		info.kind |= FXS_EMISSIVE;
		// stock fire materials have measured radiance (fx_physicalization)
		for (const FxPhysical::EmissionRule &rule : FxPhysical::emissionRules)
			if (!Q_stricmp(rule.shader, name))
				info.radiance = MAX(info.radiance, rule.radiance);
		if (info.radiance <= 0.0f)
		{
			const bool hot = fire || NameHas(name, "flash") || NameHas(name, "muzzle") || NameHas(name, "spark") ||
				NameHas(name, "blaster") || NameHas(name, "bolt") || NameHas(name, "glow") || NameHas(name, "flare");
			info.radiance = hot ? 0.18f : 0.08f;
		}
		if (fire && !NameHas(name, "blue"))
			info.kind |= FXS_FIRE_TINT;
	}
	if (!excluded)
	{
		if (NameHas(name, "shock") || NameHas(name, "ring") || NameHas(name, "wave"))
		{
			info.kind |= FXS_SHOCK;
			info.heat = 1.0f;
		}
		else if (NameHas(name, "explos") || NameHas(name, "blast"))
		{
			info.kind |= FXS_HEAT;
			info.heat = 1.0f;
		}
		else if (fire || NameHas(name, "heat"))
		{
			info.kind |= FXS_HEAT;
			info.heat = 0.6f;
		}
		else if (NameHas(name, "muzzle") || NameHas(name, "flash"))
		{
			info.kind |= FXS_HEAT;
			info.heat = 0.45f;
		}
	}
	return s_shaderInfo.emplace(shader, info).first->second;
}

bool IsFxPrimitive(const refEntity_t &e)
{
	return e.reType == RT_SPRITE || e.reType == RT_ORIENTED_QUAD || e.reType == RT_LINE ||
		e.reType == RT_ELECTRICITY || e.reType == RT_CYLINDER;
}

float FxPrimitiveSize(const refEntity_t &e)
{
	float size = e.radius;
	if (e.reType == RT_LINE || e.reType == RT_ELECTRICITY)
	{
		// a streak: radius is its half width, length counts as area
		vec3_t d;
		VectorSubtract(e.oldorigin, e.origin, d);
		size = sqrtf(MAX(e.radius, 0.5f) * VectorLength(d));
	}
	return size;
}

void FxPrimitiveCentre(const refEntity_t &e, vec3_t out)
{
	if (e.reType == RT_LINE || e.reType == RT_ELECTRICITY)
	{
		VectorAdd(e.origin, e.oldorigin, out);
		VectorScale(out, 0.5f, out);
	}
	else
		VectorCopy(e.origin, out);
}

bool PoolProgramsReady()
{
	return tr.fxParticleSimShader.program && tr.fxParticleShader.program && !s_poolFailed &&
		R_HasModernFeatures(MODERN_COMPUTE | MODERN_SSBO | MODERN_IMAGE_LOAD_STORE);
}

bool LayeringActive() { return r_fxLayering->integer && !R_CompareOff(RC_FXLAYERING); }
bool GpuParticlesActive() { return r_fxGpuParticles->integer && !R_CompareOff(RC_FXGPUPARTICLES); }
bool DistortionActive() { return (r_fxDistortion->integer && !R_CompareOff(RC_FXDISTORTION)) || LayeringActive(); }

// ------------------------------------------------------------------------
// r_fxLights

void AddFxLights(const refdef_t *fd, int firstEntity, int numEntities, int firstDlight, int numDlights)
{
	static FxCluster candidates[FX_MAX_CANDIDATES];
	int numCandidates = 0;
	const float range = 3000.0f;
	for (int i = firstEntity; i < numEntities && numCandidates < FX_MAX_CANDIDATES; ++i)
	{
		const refEntity_t &e = backEndData->entities[i].e;
		if (!IsFxPrimitive(e) || (e.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)))
			continue;
		shader_t *shader = R_GetShaderByHandle(e.customShader);
		if (!shader || shader == tr.defaultShader || !shader->stages[0])
			continue;
		const FxShaderInfo &info = ShaderInfo(shader);
		if (!(info.kind & FXS_EMISSIVE))
			continue;
		vec3_t centre;
		FxPrimitiveCentre(e, centre);
		if (DistanceSquared(centre, fd->vieworg) > range * range)
			continue;
		vec3_t rgb;
		for (int c = 0; c < 3; ++c)
			rgb[c] = FxPhysical::LinearTint(e.shaderRGBA[c]);
		if (info.kind & FXS_FIRE_TINT)
		{
			// white tinted fire sprites: the texture's flame colour
			const float m = MAX(rgb[0], MAX(rgb[1], rgb[2]));
			if (rgb[2] > 0.8f * m && rgb[1] > 0.8f * m)
				VectorSet(rgb, m, m * 0.5f, m * 0.18f);
		}
		const float lum = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
		const float size = FxPrimitiveSize(e);
		if (lum < 0.02f || size <= 0.0f)
			continue;
		FxCluster &c = candidates[numCandidates++];
		VectorCopy(centre, c.origin);
		c.power = info.radiance * lum * size * size;
		VectorScale(rgb, c.power / lum, c.color);
	}
	if (!numCandidates)
	{
		s_numPrevClusters = 0;
		return;
	}

	// greedy merge, brightest first
	std::sort(candidates, candidates + numCandidates,
		[](const FxCluster &a, const FxCluster &b) { return a.power > b.power; });
	FxCluster clusters[FX_MAX_LIGHT_CLUSTERS];
	int numClusters = 0;
	const float merge = MAX(1.0f, r_fxLightsMerge->value);
	for (int i = 0; i < numCandidates; ++i)
	{
		const FxCluster &cand = candidates[i];
		int target = -1;
		for (int k = 0; k < numClusters; ++k)
			if (DistanceSquared(clusters[k].origin, cand.origin) < merge * merge)
			{
				target = k;
				break;
			}
		if (target < 0)
		{
			if (numClusters >= FX_MAX_LIGHT_CLUSTERS)
				continue;
			clusters[numClusters++] = cand;
			continue;
		}
		FxCluster &c = clusters[target];
		const float w = c.power + cand.power;
		for (int a = 0; a < 3; ++a)
			c.origin[a] = (c.origin[a] * c.power + cand.origin[a] * cand.power) / w;
		VectorAdd(c.color, cand.color, c.color);
		c.power = w;
	}

	// budget: clusters are already sorted by their first (brightest) member;
	// sort again by the merged power
	std::sort(clusters, clusters + numClusters,
		[](const FxCluster &a, const FxCluster &b) { return a.power > b.power; });
	const int budget = MIN(numClusters, MAX(0, r_fxLightsMax->integer));
	FxCluster kept[FX_MAX_LIGHT_CLUSTERS];
	int numKept = 0;
	for (int i = 0; i < budget; ++i)
	{
		const FxCluster &c = clusters[i];
		// effects with an authored light (efx Light primitive) already light the scene
		bool authored = false;
		for (int d = firstDlight; d < firstDlight + numDlights && !authored; ++d)
			authored = DistanceSquared(backEndData->dlights[d].origin, c.origin) < merge * merge * 0.25f;
		if (authored)
			continue;
		// inverse square: the radius where the irradiance falls to a fixed
		// threshold grows with the square root of the power
		float radius = Com_Clamp(24.0f, 512.0f, r_fxLightsScale->value * sqrtf(c.power));
		// temporal smoothing against sprite birth / death flicker
		for (int p = 0; p < s_numPrevClusters; ++p)
			if (DistanceSquared(s_prevClusters[p].origin, c.origin) < merge * merge)
			{
				radius = 0.5f * (radius + s_prevClusters[p].power);
				break;
			}
		vec3_t color;
		VectorScale(c.color, 1.0f / c.power, color);
		const float m = MAX(color[0], MAX(color[1], color[2]));
		if (m <= 1.0e-4f)
			continue;
		VectorScale(color, 1.0f / m, color);
		dlight_t *dl = R_AddSceneDynamicLight(c.origin, radius, color[0], color[1], color[2], qfalse);
		if (!dl)
			break;
		dl->noShadow = qtrue;
		FxCluster &k = kept[numKept++];
		VectorCopy(c.origin, k.origin);
		k.power = radius;	// previous radius for the smoothing
	}
	memcpy(s_prevClusters, kept, numKept * sizeof(kept[0]));
	s_numPrevClusters = numKept;
}

// ------------------------------------------------------------------------
// r_fxDistortion sources

void AddDistortion(const vec3_t origin, float radius, float strength, float kind, float age, float seed)
{
	if (s_numDistortion >= FX_MAX_DISTORTION || radius <= 0.0f || strength <= 0.0f)
		return;
	FxDistortionSource &s = s_distortion[s_numDistortion++];
	VectorCopy(origin, s.origin);
	s.radius = radius;
	s.strength = strength;
	s.kind = kind;
	s.age = age;
	s.seed = seed;
}

void CollectDistortion(const refdef_t *fd, int firstEntity, int numEntities)
{
	if (!(r_fxDistortion->integer && !R_CompareOff(RC_FXDISTORTION)))
		return;
	struct Ranked { float score; FxDistortionSource s; };
	static Ranked ranked[FX_MAX_CANDIDATES];
	int numRanked = 0;
	for (int i = firstEntity; i < numEntities && numRanked < FX_MAX_CANDIDATES; ++i)
	{
		const refEntity_t &e = backEndData->entities[i].e;
		if (!IsFxPrimitive(e) || (e.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)))
			continue;
		shader_t *shader = R_GetShaderByHandle(e.customShader);
		if (!shader || shader == tr.defaultShader || !shader->stages[0])
			continue;
		const FxShaderInfo &info = ShaderInfo(shader);
		if (!(info.kind & (FXS_HEAT | FXS_SHOCK)))
			continue;
		// faded sprites refract less (additive sprites carry the fade in RGB)
		const float vis = MAX(e.shaderRGBA[3], MAX(e.shaderRGBA[0], MAX(e.shaderRGBA[1], e.shaderRGBA[2]))) / 255.0f;
		const float size = FxPrimitiveSize(e);
		if (vis < 0.05f || size < 1.0f)
			continue;
		Ranked &r = ranked[numRanked++];
		FxPrimitiveCentre(e, r.s.origin);
		const bool shock = (info.kind & FXS_SHOCK) != 0;
		r.s.radius = size * (shock ? 1.1f : 1.6f);
		r.s.strength = info.heat * MIN(1.0f, vis * 1.5f);
		// a shock sprite is its own ring: the band sits at 70% of the radius
		r.s.kind = shock ? 1.0f : 0.0f;
		r.s.age = shock ? 0.7f : 0.0f;
		r.s.seed = (float)(i & 63) * 0.37f;
		r.score = r.s.strength * r.s.radius / MAX(16.0f, Distance(r.s.origin, fd->vieworg));
	}
	const int keep = MIN(numRanked, MIN(FX_MAX_DISTORTION, MAX(0, r_fxDistortionMax->integer)));
	if (numRanked > keep)
		std::partial_sort(ranked, ranked + keep, ranked + numRanked,
			[](const Ranked &a, const Ranked &b) { return a.score > b.score; });
	for (int i = 0; i < keep; ++i)
		AddDistortion(ranked[i].s.origin, ranked[i].s.radius, ranked[i].s.strength,
			ranked[i].s.kind, ranked[i].s.age, ranked[i].s.seed);
}

// ------------------------------------------------------------------------
// secondary particles and layers from efx events

void SpawnParticle(const vec3_t origin, const vec3_t velocity, const vec3_t color, int type,
	float life, float size, float opacity, float bounce)
{
	if ((int)s_spawn.size() >= FX_MAX_SPAWN)
		return;
	FxParticle p;
	VectorCopy(origin, p.p0);
	p.p0[3] = life;
	VectorCopy(velocity, p.p1);
	p.p1[3] = life;
	VectorCopy(color, p.p2);
	p.p2[3] = (float)type;
	p.p3[0] = size;
	p.p3[1] = opacity;
	p.p3[2] = bounce;
	p.p3[3] = Random01();
	s_spawn.push_back(p);
	s_aliveUntil = MAX(s_aliveUntil, tr.refdef.time + (int)(life * 1000.0f) + 50);
}

void EventDirection(const refFxEvent_t &ev, vec3_t dir)
{
	VectorCopy(ev.dir, dir);
	if (VectorNormalize(dir) < 0.5f)
		VectorSet(dir, 0.0f, 0.0f, 1.0f);
}

void SpawnSparks(const refFxEvent_t &ev, int count, float speed, float heat)
{
	vec3_t dir;
	EventDirection(ev, dir);
	const float scale = Com_Clamp(0.25f, 4.0f, ev.scale);
	for (int i = 0; i < count; ++i)
	{
		vec3_t v, r;
		RandomUnit(r);
		// a cone around the normal, wider for explosions
		VectorMA(r, 1.3f, dir, v);
		VectorNormalize(v);
		VectorScale(v, RandomRange(0.35f, 1.0f) * speed * sqrtf(scale), v);
		const float t = RandomRange(0.6f, 1.0f) * heat;
		const vec3_t color = { 6.0f * t, 2.8f * t * t, 0.9f * t * t * t };
		SpawnParticle(ev.origin, v, color, 0, RandomRange(0.35f, 1.1f), RandomRange(0.5f, 1.0f), 1.0f, 0.45f);
	}
}

void SpawnEmbers(const refFxEvent_t &ev, int count)
{
	for (int i = 0; i < count; ++i)
	{
		vec3_t o, v, r;
		RandomUnit(r);
		VectorMA(ev.origin, 6.0f * Com_Clamp(0.25f, 4.0f, ev.scale), r, o);
		VectorSet(v, r[0] * 25.0f, r[1] * 25.0f, RandomRange(30.0f, 90.0f));
		const float t = RandomRange(0.5f, 1.0f);
		const vec3_t color = { 3.0f * t, 1.1f * t * t, 0.25f * t * t };
		SpawnParticle(o, v, color, 1, RandomRange(1.2f, 2.8f), RandomRange(0.5f, 0.9f), 1.0f, 0.3f);
	}
}

void SpawnDust(const refFxEvent_t &ev, int count)
{
	vec3_t dir;
	EventDirection(ev, dir);
	vec3_t ambient, directed, lightDir;
	vec3_t probe;
	VectorMA(ev.origin, 4.0f, dir, probe);
	vec3_t albedo = { 0.42f, 0.38f, 0.33f };
	if (tr.world && R_LightForPoint(probe, ambient, directed, lightDir))
	{
		// lit once at the event: ambient plus half the directed light, 0..255
		for (int c = 0; c < 3; ++c)
			albedo[c] *= Com_Clamp(0.05f, 2.0f, (ambient[c] + 0.5f * directed[c]) / 160.0f);
	}
	const float scale = Com_Clamp(0.25f, 4.0f, ev.scale);
	for (int i = 0; i < count; ++i)
	{
		vec3_t v, r;
		RandomUnit(r);
		VectorMA(r, 1.6f, dir, v);
		VectorNormalize(v);
		VectorScale(v, RandomRange(30.0f, 120.0f) * sqrtf(scale), v);
		SpawnParticle(ev.origin, v, albedo, 2, RandomRange(1.0f, 2.4f), RandomRange(4.0f, 9.0f) * scale,
			RandomRange(0.25f, 0.5f), 0.1f);
	}
}

void AddLayerLight(const refFxEvent_t &ev, const vec3_t color, float radius, int life)
{
	for (FxTimedLight &l : s_layerLights)
	{
		if (l.life > 0 && tr.refdef.time - l.birth < l.life)
			continue;
		vec3_t dir;
		EventDirection(ev, dir);
		VectorMA(ev.origin, 8.0f, dir, l.origin);
		VectorCopy(color, l.color);
		l.radius = radius * Com_Clamp(0.25f, 4.0f, ev.scale);
		l.birth = tr.refdef.time;
		l.life = life;
		return;
	}
}

void AddBurst(const refFxEvent_t &ev, float radius, float strength, int life)
{
	for (FxBurst &b : s_bursts)
	{
		if (b.life > 0 && tr.refdef.time - b.birth < b.life)
			continue;
		VectorCopy(ev.origin, b.origin);
		b.radius = radius * Com_Clamp(0.25f, 4.0f, ev.scale);
		b.strength = strength;
		b.birth = tr.refdef.time;
		b.life = life;
		b.seed = Random01() * 10.0f;
		return;
	}
}

void AddScorch(const refFxEvent_t &ev, float radius)
{
	vec3_t dir;
	VectorCopy(ev.dir, dir);
	if (VectorNormalize(dir) < 0.5f)
		return;
	if (s_scorchShader < 0)
	{
		s_scorchShader = RE_RegisterShader("gfx/damage/burnmark4");
		if (!s_scorchShader || R_GetShaderByHandle(s_scorchShader) == tr.defaultShader)
			s_scorchShader = RE_RegisterShader("gfx/damage/burnmark1");
		if (s_scorchShader && R_GetShaderByHandle(s_scorchShader) == tr.defaultShader)
			s_scorchShader = 0;
	}
	if (!s_scorchShader)
		return;
	RE_AddDecalToScene(s_scorchShader, ev.origin, dir, Random01() * 360.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		qtrue, radius * Com_Clamp(0.25f, 4.0f, ev.scale), qfalse);
}

void ProcessEvents()
{
	const bool pool = PoolProgramsReady();
	const bool gpu = pool && GpuParticlesActive();
	const bool layering = LayeringActive();
	const float density = Com_Clamp(0.0f, 4.0f, r_fxGpuParticlesDensity->value);
	for (int i = 0; i < s_numEvents; ++i)
	{
		const refFxEvent_t &ev = s_events[i];
		bool sparksAdded = false;
		if (gpu)
		{
			// secondary detail on top of what the effect already draws
			int sparks = 0;
			switch (ev.fxClass)
			{
			case FXEV_CLASS_IMPACT_METAL: sparks = 12; break;
			case FXEV_CLASS_IMPACT_ENERGY: sparks = 6; break;
			case FXEV_CLASS_EXPLOSION: sparks = 40; break;
			case FXEV_CLASS_SPARKS: sparks = 16; break;
			default: break;
			}
			if (sparks)
			{
				SpawnSparks(ev, (int)(sparks * density + 0.5f), ev.fxClass == FXEV_CLASS_EXPLOSION ? 650.0f : 380.0f, 1.0f);
				sparksAdded = true;
			}
			if (ev.fxClass == FXEV_CLASS_FIRE || ev.fxClass == FXEV_CLASS_EXPLOSION)
				SpawnEmbers(ev, (int)((ev.fxClass == FXEV_CLASS_FIRE ? 1.0f : 12.0f) * density + Random01()));
		}
		if (!layering)
			continue;
		const int missing = FxLayering::MissingLayers(ev.fxClass, ev.has);
		if ((missing & FxLayering::LayerSparks) && pool && !sparksAdded)
			SpawnSparks(ev, ev.fxClass == FXEV_CLASS_EXPLOSION ? 32 : 8, ev.fxClass == FXEV_CLASS_EXPLOSION ? 600.0f : 320.0f, 0.9f);
		if ((missing & FxLayering::LayerDust) && pool)
			SpawnDust(ev, ev.fxClass == FXEV_CLASS_EXPLOSION ? 24 : 8);
		if (missing & FxLayering::LayerLight)
		{
			switch (ev.fxClass)
			{
			case FXEV_CLASS_EXPLOSION: { const vec3_t c = { 1.0f, 0.55f, 0.25f }; AddLayerLight(ev, c, 380.0f, 450); break; }
			case FXEV_CLASS_MUZZLE: { const vec3_t c = { 1.0f, 0.75f, 0.45f }; AddLayerLight(ev, c, 160.0f, 70); break; }
			default: { const vec3_t c = { 1.0f, 0.7f, 0.4f }; AddLayerLight(ev, c, 110.0f, 140); break; }
			}
		}
		if (missing & FxLayering::LayerDistortion)
		{
			if (ev.fxClass == FXEV_CLASS_EXPLOSION)
				AddBurst(ev, 260.0f, 1.2f, 550);
			else
				AddBurst(ev, 40.0f, 0.5f, 140);
		}
		if (missing & FxLayering::LayerScorch)
			AddScorch(ev, ev.fxClass == FXEV_CLASS_EXPLOSION ? 40.0f : 5.0f);
	}
	s_numEvents = 0;
}

void AddTimedLayers()
{
	const int now = tr.refdef.time;
	for (FxTimedLight &l : s_layerLights)
	{
		const int age = now - l.birth;
		if (l.life <= 0 || age < 0 || age >= l.life)
			continue;
		if (!R_ForwardPlusActive())
			continue;
		const float t = (float)age / (float)l.life;
		const float k = (1.0f - t) * (1.0f - t);
		dlight_t *dl = R_AddSceneDynamicLight(l.origin, MAX(8.0f, l.radius * sqrtf(k)), l.color[0], l.color[1], l.color[2], qfalse);
		if (dl)
			dl->noShadow = qtrue;
	}
	for (FxBurst &b : s_bursts)
	{
		const int age = now - b.birth;
		if (b.life <= 0 || age < 0 || age >= b.life)
			continue;
		const float t = (float)age / (float)b.life;
		// small bursts (muzzle) are a heat puff, large ones a travelling ring
		if (b.radius < 80.0f)
			AddDistortion(b.origin, b.radius * (0.6f + 0.4f * t), b.strength * (1.0f - t), 0.0f, 0.0f, b.seed);
		else
			AddDistortion(b.origin, b.radius, b.strength, 1.0f, MIN(0.98f, sqrtf(t)), b.seed);
	}
}

} // namespace

// ------------------------------------------------------------------------
// public: front end

void R_FxUpgradesRegister(void)
{
	r_fxLights = R_Cvar("r_fxLights", "0", CVAR_ARCHIVE,
		"Emissive FX sprites (sparks, bolts, fire, flashes) light the scene as merged, unshadowed Forward+ point lights; needs r_forwardPlus 1. 0 off, 1 on");
	R_CvarRange(r_fxLights, 0, 1, qtrue);
	r_fxLightsMax = R_Cvar("r_fxLightsMax", "16", CVAR_ARCHIVE,
		"r_fxLights: most FX lights per frame (brightest merged clusters)");
	R_CvarRange(r_fxLightsMax, 0, 64, qtrue);
	r_fxLightsScale = R_Cvar("r_fxLightsScale", "7", CVAR_ARCHIVE,
		"r_fxLights: light radius per square root of the sprite power (radiance x luminance x size^2)");
	R_CvarRange(r_fxLightsScale, 0, 64, qfalse);
	r_fxLightsMerge = R_Cvar("r_fxLightsMerge", "64", CVAR_ARCHIVE,
		"r_fxLights: sprites closer than this (world units) share one light");
	R_CvarRange(r_fxLightsMerge, 1, 512, qfalse);
	r_fxDistortion = R_Cvar("r_fxDistortion", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"Heat haze and shock distortion behind fire, explosions and muzzle flashes (half resolution offset buffer). 0 off, 1 on; vid_restart");
	R_CvarRange(r_fxDistortion, 0, 1, qtrue);
	r_fxDistortionStrength = R_Cvar("r_fxDistortionStrength", "1", CVAR_ARCHIVE,
		"r_fxDistortion / r_fxLayering: refraction amount, 1 = default");
	R_CvarRange(r_fxDistortionStrength, 0, 4, qfalse);
	r_fxDistortionMax = R_Cvar("r_fxDistortionMax", "96", CVAR_ARCHIVE,
		"r_fxDistortion: most distorting FX sprites per frame (nearest / strongest)");
	R_CvarRange(r_fxDistortionMax, 0, FX_MAX_DISTORTION, qtrue);
	r_fxGpuParticles = R_Cvar("r_fxGpuParticles", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"GPU secondary sparks and embers on efx impacts, explosions and fires (compute, needs r_gl43 1 and GL 4.3). 0 off, 1 on; vid_restart");
	R_CvarRange(r_fxGpuParticles, 0, 1, qtrue);
	r_fxGpuParticlesDensity = R_Cvar("r_fxGpuParticlesDensity", "1", CVAR_ARCHIVE,
		"r_fxGpuParticles: secondary particle count multiplier");
	R_CvarRange(r_fxGpuParticlesDensity, 0, 4, qfalse);
	r_fxLayering = R_Cvar("r_fxLayering", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"Missing layers of stock impacts and explosions: dust and sparks (GPU pool, GL 4.3), a light (r_forwardPlus), a shock ring, a scorch decal. 0 off, 1 on; vid_restart");
	R_CvarRange(r_fxLayering, 0, 1, qtrue);
}

qboolean R_FxDistortionProgramsWanted(void) { return (qboolean)(r_fxDistortion->integer || r_fxLayering->integer); }
qboolean R_FxParticleProgramsWanted(void) { return (qboolean)(r_fxGpuParticles->integer || r_fxLayering->integer); }

qboolean RE_FxEventsWanted(void)
{
	return (qboolean)(tr.registered && (r_fxGpuParticles->integer || r_fxLayering->integer));
}

void RE_AddFxEvent(const refFxEvent_t *event)
{
	if (!event || !RE_FxEventsWanted() || s_numEvents >= FX_MAX_EVENTS)
		return;
	if (event->fxClass <= FXEV_CLASS_NONE || event->fxClass >= FxLayering::ClassCount)
		return;
	if (Q_isnan(event->origin[0]) || Q_isnan(event->origin[1]) || Q_isnan(event->origin[2]))
		return;
	s_events[s_numEvents++] = *event;
}

void R_FxUpgradesBeginScene(const refdef_t *fd, int firstEntity, int numEntities, int firstDlight, int numDlights)
{
	if ((fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) || !tr.world)
		return;
	const bool lights = r_fxLights->integer && R_ForwardPlusActive() && !R_CompareOff(RC_FXLIGHTS);
	const bool distortion = DistortionActive();
	const bool events = r_fxGpuParticles->integer || r_fxLayering->integer;
	if (!lights && !distortion && !events)
	{
		s_numEvents = 0;
		s_numDistortion = 0;
		s_spawn.clear();
		return;
	}
	s_spawn.clear();
	s_numDistortion = 0;
	if (lights)
		AddFxLights(fd, firstEntity, numEntities, firstDlight, numDlights);
	if (distortion)
		CollectDistortion(fd, firstEntity, numEntities);
	if (events)
	{
		ProcessEvents();
		AddTimedLayers();
	}
}

void R_FxUpgradesShutdown(void)
{
	if (s_stateBuffer)
		qglDeleteBuffers(1, &s_stateBuffer);
	if (s_spawnBuffer)
		qglDeleteBuffers(1, &s_spawnBuffer);
	s_stateBuffer = s_spawnBuffer = 0;
	// images and FBOs belong to the renderer's lists
	s_particleImage = s_depthImage = s_offsetImage = s_sceneCopyImage = s_instanceImage = NULL;
	s_depthFbo = s_offsetFbo = s_sceneCopyFbo = NULL;
	s_ringHead = 0;
	s_poolFailed = false;
	s_numEvents = 0;
	s_numDistortion = 0;
	s_numPrevClusters = 0;
	s_aliveUntil = 0;
	s_spawn.clear();
	s_shaderInfo.clear();
	s_scorchShader = -1;
	memset(s_layerLights, 0, sizeof(s_layerLights));
	memset(s_bursts, 0, sizeof(s_bursts));
}

// ------------------------------------------------------------------------
// back end

static FBO_t *CreateColorFbo(const char *name, image_t *image)
{
	FBO_t *fbo = FBO_Create(name, image->width, image->height);
	FBO_Bind(fbo);
	FBO_AttachTextureImage(image, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(fbo);
	return fbo;
}

static bool EnsureDepthCopy()
{
	if (s_depthFbo && s_depthImage->width == tr.renderFbo->width && s_depthImage->height == tr.renderFbo->height)
		return true;
	FBO_t *old = glState.currentFBO;
	s_depthImage = R_ScreenCreateImage("*fxDepth", tr.renderFbo->width, tr.renderFbo->height, GL_DEPTH24_STENCIL8, qfalse);
	if (!s_depthImage)
		return false;
	s_depthFbo = FBO_Create("_fxDepth", s_depthImage->width, s_depthImage->height);
	FBO_Bind(s_depthFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_depthImage->texnum, 0);
	qglDrawBuffer(GL_NONE);
	qglReadBuffer(GL_NONE);
	R_CheckFBO(s_depthFbo);
	FBO_Bind(old);
	return true;
}

static void SetProjectionParams(vec4_t out)
{
	const float *p = backEnd.viewParms.projectionMatrix;
	out[2] = p[10];
	out[3] = p[14];
}

static void WorldToClip(matrix_t out)
{
	Matrix16Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelViewMatrix, out);
}

static bool EnsurePool()
{
	if (s_stateBuffer)
		return true;
	if (s_poolFailed)
		return false;
	// only errors of the pool creation count, not older ones
	for (int i = 0; i < 16 && qglGetError() != GL_NO_ERROR; ++i) {}
	std::vector<FxParticle> zero(FX_POOL);
	memset(zero.data(), 0, zero.size() * sizeof(FxParticle));
	qglGenBuffers(1, &s_stateBuffer);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_stateBuffer);
	qglBufferData(GL_SHADER_STORAGE_BUFFER, FX_POOL * sizeof(FxParticle), zero.data(), GL_DYNAMIC_COPY);
	qglGenBuffers(1, &s_spawnBuffer);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_spawnBuffer);
	qglBufferData(GL_SHADER_STORAGE_BUFFER, FX_MAX_SPAWN * sizeof(FxParticle), NULL, GL_STREAM_DRAW);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	s_particleImage = R_CreateImage("*fxParticles", NULL, FX_POOL, 4, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA32F);
	if (qglGetError() != GL_NO_ERROR || !s_particleImage)
	{
		ri.Printf(PRINT_WARNING, "r_fxGpuParticles: could not create the particle pool; disabled.\n");
		s_poolFailed = true;
		return false;
	}
	s_ringHead = 0;
	return true;
}

static void RunParticles(bool depthValid)
{
	if (!EnsurePool())
		return;
	const int numSpawn = MIN((int)s_spawn.size(), FX_MAX_SPAWN);
	if (numSpawn)
	{
		qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_spawnBuffer);
		qglBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, numSpawn * sizeof(FxParticle), s_spawn.data());
		qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	matrix_t m[2];
	WorldToClip(m[0]);
	// general inverse, done once here rather than per thread
	{
		matrix_t &out = m[1];
		const float *a = m[0];
		float inv[16];
		inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
		inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
		inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
		inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
		inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
		inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
		inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
		inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
		inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
		inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
		inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
		inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
		inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
		inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
		inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
		inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
		float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
		det = fabsf(det) > 1.0e-20f ? 1.0f / det : 0.0f;
		for (int i = 0; i < 16; ++i)
			out[i] = inv[i] * det;
	}

	const float dt = Com_Clamp(0.0f, 0.05f, backEnd.refdef.frameTime * 0.001f);
	vec4_t params[4] = {};
	VectorSet4(params[0], (float)s_ringHead, (float)numSpawn, (float)FX_POOL, dt);
	VectorSet4(params[1], backEnd.refdef.floatTime, 60.0f, depthValid ? 1.0f : 0.0f, FX_GRAVITY);
	SetProjectionParams(params[2]);
	VectorCopy(backEnd.viewParms.ori.origin, params[3]);

	shaderProgram_t *sim = &tr.fxParticleSimShader;
	GLSL_BindProgram(sim);
	GLSL_SetUniformMatrix4x4(sim, UNIFORM_FXMATRIX, m[0], 2);
	GLSL_SetUniformVec4N(sim, UNIFORM_FXPARAMS, params[0], 4);
	GL_BindToTMU(depthValid ? s_depthImage : tr.whiteImage, TB_LIGHTMAP);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_stateBuffer);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_spawnBuffer);
	qglBindImageTexture(0, s_particleImage->texnum, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	qglDispatchCompute((FX_POOL + 63) / 64, 1, 1);
	qglMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	qglBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	s_ringHead = (s_ringHead + numSpawn) % FX_POOL;
	s_spawn.clear();

	// draw: emissive (added) then dust (premultiplied over)
	shaderProgram_t *draw = &tr.fxParticleShader;
	GLSL_BindProgram(draw);
	GLSL_SetUniformMatrix4x4(draw, UNIFORM_FXMATRIX, m[0], 2);
	GL_BindToTMU(s_particleImage, TB_COLORMAP);
	GL_BindToTMU(depthValid ? s_depthImage : tr.whiteImage, TB_LIGHTMAP);
	const float split = R_CompareSplitX(RC_FXGPUPARTICLES);
	qglColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	for (int group = 0; group < 2; ++group)
	{
		VectorCopy(backEnd.viewParms.ori.origin, params[0]);
		params[0][3] = (float)group;
		VectorSet4(params[1], 0.012f, 1.0f, 0.0f, 0.0f);
		VectorSet4(params[2], 1.0f / tr.renderFbo->width, 1.0f / tr.renderFbo->height, 0.0f, 0.0f);
		SetProjectionParams(params[2]);
		VectorSet4(params[3], split, 0.0f, 0.0f, 0.0f);
		GLSL_SetUniformVec4N(draw, UNIFORM_FXPARAMS, params[0], 4);
		GL_State(group == 0 ? (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
			: (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA));
		qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, FX_POOL);
	}
	qglColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void RunDistortion(bool depthValid)
{
	if (!tr.fxDistortionShader[0].program || !tr.fxDistortionShader[1].program || !s_numDistortion)
		return;
	FBO_t *target = glState.currentFBO;
	const int halfW = MAX(1, tr.renderFbo->width / 2), halfH = MAX(1, tr.renderFbo->height / 2);
	if (!s_offsetFbo || s_offsetImage->width != halfW || s_offsetImage->height != halfH)
	{
		s_offsetImage = R_CreateImage("*fxDistortionOffset", NULL, halfW, halfH, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		s_offsetFbo = CreateColorFbo("_fxDistortionOffset", s_offsetImage);
		s_sceneCopyImage = R_CreateImage("*fxDistortionScene", NULL, tr.renderFbo->width, tr.renderFbo->height,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		s_sceneCopyFbo = CreateColorFbo("_fxDistortionScene", s_sceneCopyImage);
		FBO_Bind(target);
	}
	if (!s_instanceImage)
		s_instanceImage = R_CreateImage("*fxDistortionSources", NULL, FX_MAX_DISTORTION, 2, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA32F);

	static float rows[2][FX_MAX_DISTORTION][4];
	for (int i = 0; i < s_numDistortion; ++i)
	{
		const FxDistortionSource &s = s_distortion[i];
		VectorSet4(rows[0][i], s.origin[0], s.origin[1], s.origin[2], s.radius);
		VectorSet4(rows[1][i], s.strength, s.kind, s.age, s.seed);
	}
	GL_BindToTMU(s_instanceImage, TB_COLORMAP);
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_numDistortion, 1, GL_RGBA, GL_FLOAT, rows[0]);
	qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, s_numDistortion, 1, GL_RGBA, GL_FLOAT, rows[1]);

	matrix_t m[2];
	WorldToClip(m[0]);
	Matrix16Identity(m[1]);
	vec4_t params[4] = {};

	// offsets
	const int vx = backEnd.viewParms.viewportX, vy = backEnd.viewParms.viewportY;
	const int vw = backEnd.viewParms.viewportWidth, vh = backEnd.viewParms.viewportHeight;
	FBO_Bind(s_offsetFbo);
	GL_SetViewportAndScissor(vx / 2, vy / 2, MAX(1, vw / 2), MAX(1, vh / 2));
	const vec4_t black = { 0.0f, 0.0f, 0.0f, 0.0f };
	qglClearBufferfv(GL_COLOR, 0, black);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	shaderProgram_t *offset = &tr.fxDistortionShader[0];
	GLSL_BindProgram(offset);
	GLSL_SetUniformMatrix4x4(offset, UNIFORM_FXMATRIX, m[0], 2);
	VectorScale(backEnd.viewParms.ori.axis[1], -1.0f, params[0]);	// right
	params[0][3] = backEnd.refdef.floatTime;
	VectorCopy(backEnd.viewParms.ori.axis[2], params[1]);
	VectorSet4(params[2], 1.0f / halfW, 1.0f / halfH, 0.0f, 0.0f);
	SetProjectionParams(params[2]);
	VectorSet4(params[3], 0.06f * r_fxDistortionStrength->value, 0.03f, 0.0f, 0.0f);
	GLSL_SetUniformVec4N(offset, UNIFORM_FXPARAMS, params[0], 4);
	GL_BindToTMU(s_instanceImage, TB_COLORMAP);
	GL_BindToTMU(depthValid ? s_depthImage : tr.whiteImage, TB_LIGHTMAP);
	qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, s_numDistortion);

	// composite: refract a resolved copy of the scene back into the target
	GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
	FBO_FastBlit(target, NULL, s_sceneCopyFbo, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	FBO_Bind(target);
	GL_SetViewportAndScissor(vx, vy, vw, vh);
	GL_State(GLS_DEPTHTEST_DISABLE);
	shaderProgram_t *composite = &tr.fxDistortionShader[1];
	GLSL_BindProgram(composite);
	VectorSet4(params[2], 1.0f / tr.renderFbo->width, 1.0f / tr.renderFbo->height,
		R_CompareSplitX(RC_FXDISTORTION), 0.03f);
	GLSL_SetUniformVec4N(composite, UNIFORM_FXPARAMS, params[0], 4);
	GL_BindToTMU(s_sceneCopyImage, TB_COLORMAP);
	GL_BindToTMU(s_offsetImage, TB_LIGHTMAP);
	qglColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	RB_InstantTriangle();
	qglColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	s_numDistortion = 0;
}

void RB_FxUpgradesView(void)
{
	if (backEnd.viewParms.viewParmType != VPT_MAIN || backEnd.viewParms.isSkyPortal ||
		(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) || !tr.renderFbo ||
		(backEnd.viewParms.targetFbo && backEnd.viewParms.targetFbo != tr.renderFbo))
		return;
	const bool particles = (r_fxGpuParticles->integer || r_fxLayering->integer) && PoolProgramsReady() &&
		(!s_spawn.empty() || backEnd.refdef.time < s_aliveUntil);
	const bool distortion = s_numDistortion > 0 && tr.fxDistortionShader[0].program;
	if (!particles && !distortion)
	{
		s_spawn.clear();
		return;
	}
	if (tess.numIndexes)
		RB_EndSurface();
	FBO_t *target = glState.currentFBO;
	if (!target)
		return;
	const int timer = RB_ScreenBeginTimer("FX upgrades");
	const bool depthValid = EnsureDepthCopy();
	if (depthValid)
	{
		FBO_FastBlit(target, NULL, s_depthFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		FBO_Bind(target);
	}
	GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	if (particles)
		RunParticles(depthValid);
	else
		s_spawn.clear();
	if (distortion)
		RunDistortion(depthValid);
	FBO_Bind(target);
	GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	GL_State(GLS_DEFAULT);
	RB_ScreenEndTimer(timer);
}
