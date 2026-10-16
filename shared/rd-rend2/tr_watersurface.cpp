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

// tr_watersurface.cpp -- modern water surface (r_waterSurface)
//
// The stock water surfaces of the maps get a physically based surface over
// the liquid medium, without rebaking a map or changing an asset: the opaque
// scene is complete and lit, then the water reads its color and depth and
// composites refraction (depth aware), the liquid's absorption / scattering,
// the dielectric Fresnel reflection (SSR -> cubemap -> fallback) and GGX glints
// (glsl/watersurface.glsl). The generic refraction (refractive shaders,
// RF_DISTORTION, drawn after tone mapping) is not touched.
//
// Classification (per map, R_WaterClassifySurfaces): the BSP semantics first.
// A drawn surface whose BSP shader has CONTENTS_WATER (surfaceparm water) is
// water, unless it is lava; its optics are slime when it also has
// CONTENTS_SLIME. The surface is matched to the liquid brush it lies on (all
// vertices on a side plane, inside the brush) for diagnostics and the fog
// volume medium. A shader is drawn as water when at least half of its area on
// the map faces up (its bottom faces left out): waterfalls and streams (steep
// patches with the same contents) keep their legacy stages. An existing refractive shader lying on a
// water brush is water too. The env.json "Liquids" profile of a shader
// (tr_liquid.cpp) sets its optics, as it sets the medium of the liquid brushes
// it lies on. Then r_waterOverride (name or prefix*), last the
// experimental name rule (r_waterSurfaceExperimental, off by default).
//
// Pipeline: the stage iterator draws a classified surface once with the water
// program (RB_WaterSurfaceDraws). Its draw items are tagged and the main pass
// draws them in their own slot (RB_SubmitRenderPass): after the opaque sort,
// the screen-space passes (SSR, SSGI) and the layers up to SS_FOG, before the
// atmosphere / clouds / froxel fog composites and the blended layers. There
// RB_WaterSurfacePrepare copies the HDR scene and its depth once, the water
// writes depth, so the composites fog the camera -> surface segment and the
// water the segment behind it. Everything before tone mapping: bloom sees the
// glints.
//
// r_waterSurface 0 (latched): no targets, no programs, no extra pass: the
// legacy draws are unchanged.
//
// docs/rend2-water-surface.md

#include "tr_local.h"
#include "tr_weather.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include "json.h"

// GL 4.3 depth contact readback: frames in flight, bodies (r_waterInteractionMaxBodies)
#define WATER_INTERSECTION_HIT_RING	4
#define WATER_INTERSECTION_MAX_SLOTS	32

/*
============================================================

Classification records (r_waterInfo)

============================================================
*/

enum waterReason_t
{
	WREASON_NONE,
	WREASON_CONTENTS,			// CONTENTS_WATER (surfaceparm water) of the BSP shader
	WREASON_BRUSH_TOP,	// upward boundary of a water/slime brush
	WREASON_OVERRIDE_ON,		// r_waterOverride on
	WREASON_EXPERIMENTAL,		// r_waterSurfaceExperimental name rule
	// legacy
	WREASON_LAVA,				// lava contents: an emissive liquid, not a water surface
	WREASON_SLIME_ONLY,			// slime without water contents
	WREASON_NOT_HORIZONTAL,		// the shader's area mostly does not face up (waterfall, stream)
	WREASON_REFRACTIVE_ONLY,	// refractive without water semantics: stays generic refraction
	WREASON_NO_SEMANTICS,		// water-like name or material only
	WREASON_OVERRIDE_OFF,		// r_waterOverride off
	WREASON_UNSUPPORTED,		// sky, portal, nodraw, flare
	WREASON_COUNT
};

static const char *s_reasonNames[WREASON_COUNT] = {
	"-",
	"CONTENTS_WATER",
	"top of a water brush",
	"r_waterOverride on",
	"experimental name rule",
	"lava",
	"slime without water contents",
	"not an upward interface (side / bottom / waterfall)",
	"refractive, no water semantics (generic refraction)",
	"water-like name / material only, no water semantics",
	"r_waterOverride off",
	"sky / portal / nodraw / flare",
};

enum { WORIENT_UP, WORIENT_DOWN, WORIENT_VERTICAL, WORIENT_SLOPED };
static const char *s_orientNames[] = { "up", "down", "vertical", "sloped" };
static const char *s_liquidNames[LIQUID_CLASSES] = { "water", "slime", "lava" };

struct waterBrushRecord_t
{
	int			brushNum;
	int			model;
	int			liquidClass;
	int			contents;
	int			shaderNum;
	int			firstPlane;		// in s_water.planes
	int			numPlanes;
	vec3_t		bounds[2];
};

struct waterSurfaceRecord_t
{
	int			surfaceNum;
	int			model;
	int			bspShader;
	int			contents;
	int			surfaceFlags;
	int			type;			// MST_*
	shader_t	*shader;
	vec3_t		normal;
	int			orient;
	float		area;
	vec3_t		bounds[2];
	int			brush;			// index in s_water.brushes, -1: none
	vec3_t		brushSide;		// normal of the side it lies on
	vec3_t		uvTangentU, uvTangentV; // world displacement for +1 base texture U/V
	float		uvArea, uvTotalArea;
	int			uvMirrorSign; // -1 mirrored, +1 ordinary, 0 mixed/unknown
	qboolean	refractive;
	qboolean	nameLike;
	qboolean modern;
	int reason;
	int liquidClass;
	int flags;
};

// Authored legacy motion retained for diagnostics and conservative body-flow resolution.
struct legacyWaterMotion_t
{
	struct scroll_t {
		std::string shader;
		int stage;
		float x, y;
		vec3_t worldVelocity;
		float surfaceArea, basisAgreement;
		qboolean reliable;
	};
	struct wave_t { int stage; float base, amplitude, phase, frequency; };
	std::vector<scroll_t> scrolls;
	std::vector<wave_t> turb, stretch;
	float weightedScroll[2];
	float disagreement;
	vec3_t worldVelocity;
	float worldSpeed, worldAgreement, uvCoverage;
	int confidence;
	float turbAmplitude, turbFrequency;
	float deformAmplitude, deformFrequency;
};

enum waterFlowSource_t { WFLOW_NONE, WFLOW_PROFILE, WFLOW_LEGACY, WFLOW_EXPLICIT };
enum waterFlowConfidence_t { WFLOW_CONFIDENCE_NONE, WFLOW_CONFIDENCE_AMBIGUOUS, WFLOW_CONFIDENCE_HIGH };
static const char *s_flowSources[] = { "none", "profile default", "legacy-derived", "explicit" };
static const char *s_flowConfidence[] = { "none", "ambiguous", "high" };

struct waterDynamics_t
{
	const char *name;
	float amplitude, wavelength, choppiness, speed, microNormal;
	float flow, damping, wake, foam, shoreline, rain;
	float windCoupling, defaultWind, dispersion;
};

enum waterWindSource_t { WWIND_NONE, WWIND_MAP, WWIND_WEATHER, WWIND_RAIN, WWIND_PROFILE };
static const char *s_waterWindSources[] = { "none", "map/environment", "global weather", "rain velocity", "profile default" };

struct waterWaveSet_t
{
	vec4_t terms[8];
	float omega[8] = {};
	vec2_t direction = { 0.8f, 0.6f };
	float strength = 0.0f;
	float choppiness = 0.0f;
	float micro = 0.0f;
};

struct waterDrawUniforms_t
{
	vec4_t slots[WATER_UNIFORM_VEC4S];
	explicit waterDrawUniforms_t(const vec4_t *source) { Com_Memcpy(slots, source, sizeof(slots)); }
	float *operator[](int slot) { return slots[slot]; }
	const float *operator[](int slot) const { return slots[slot]; }
	vec4_t *Data() { return slots; }
};

struct waterMotionSnapshot_t
{
	vec4_t slots[WATER_MOTION_VEC4S] = {};
	const shader_t *shader = nullptr;
	unsigned renderedFrame = ~0u;
	unsigned generation = 0;
	bool legacySupported = false;
	bool modelTransformSupported = false;
	bool interactionSupported = false;
	bool valid = false;
};

enum waterMotionInvalidation_t
{
	WM_INVALID_NONE,
	WM_INVALID_RESOURCE_RECREATE,
	WM_INVALID_MAP_CHANGE,
	WM_INVALID_BODY_REBUILD,
	WM_INVALID_INTERACTION_REMAP,
	WM_INVALID_TIME_DISCONTINUITY,
	WM_INVALID_SCREEN_HISTORY,
	WM_INVALID_BODY_IDENTITY,
	WM_INVALID_MODEL_IDENTITY,
	WM_INVALID_LEGACY_DEFORM,
	WM_INVALID_MOVING_INTERACTION,
	WM_INVALID_TOPOLOGY,
	WM_INVALID_COUNT
};

static const char *s_waterMotionInvalidationNames[WM_INVALID_COUNT] = {
	"none", "resource recreation", "map change", "water body rebuild",
	"interaction atlas remap", "wave/render time discontinuity", "screen history",
	"body identity", "previous model identity", "legacy deform unsupported",
	"moving-model interaction unsupported", "topology changed"
};

struct waterWindState_t
{
	waterWaveSet_t oldSet, targetSet;
	vec3_t resolved = { 0, 0, 0 };
	float strength = 0.0f;
	float blendStart = 0.0f;
	float blend = 1.0f;
	float updateTime = -1.0e30f;
	float rainNoise = 0.0f;
	int source = WWIND_NONE;
	bool valid = false;
	bool outdoor = false;
	bool initialized = false;
};

// One body-local rectangular grid.  The byte mask is rasterised from the
// body's actual BSP triangles; cells outside it never participate, so nearby
// disconnected water cannot communicate through the rectangle.
struct waterInteractionState_t
{
	int width = 0, height = 0;
	float texelX = 0.0f, texelY = 0.0f;
	std::vector<byte> mask;
	// Dense topology for the actual triangle union.  Each entry names a wet
	// cell and its L/R/D/U neighbours; a solid boundary is pre-resolved to the
	// cell itself.  Large irregular bodies therefore never run the stencil or
	// repeat mask/bounds tests for empty rectangle cells.
	std::vector<int> wetCells;
	std::vector<std::array<int, 4>> wetNeighbours;
	std::vector<float> state; // interleaved height, velocity, transient foam
	std::vector<float> next;
	// Depth-derived object contact is kept separate from event splash foam so
	// its persistence can be tuned without changing weapon/entry splashes.
	std::vector<float> intersectionFoam;
	std::vector<float> intersectionNext;
	// Render field: height, world slope X/Y, 1 + foam. Alpha zero remains the
	// dry-cell mask, so the visible-water shader needs one filtered fetch.
	std::vector<float> upload;
	image_t *image = nullptr;
	image_t *gpuState[2] = { nullptr, nullptr };
	image_t *gpuSource = nullptr;
	std::vector<float> gpuInitial;
	std::vector<float> gpuSourceData;
	// Static body-local bathymetry: R=depth below this surface, G=confidence.
	// It is derived from exact convex liquid intervals plus a solid-world trace;
	// consumers fall back to body.depthAverage whenever confidence is low.
	std::vector<float> bathymetry;
	image_t *bathymetryImage = nullptr;
	float bathymetryMaxDepth = 0.0f;
	int gpuCurrent = 0;
	bool gpuSourceDirty = false;
	int gpuSourceMinX = 0, gpuSourceMinY = 0, gpuSourceMaxX = 0, gpuSourceMaxY = 0;
	int atlasX = 0, atlasY = 0;
	bool active = false, dirty = false;
	bool resolvedForMotionHistory = false;
	unsigned lastMotionCopyFrame = ~0u;
	float energy = 0.0f, quietTime = 0.0f;
	float lastSource[4] = { 0, 0, 0, 0 }; // body UV, radius in UV, strength
	bool intersectionPending = false; // depth contacts injected since the last foam update
	// GL 4.3 depth contacts (RB_WaterIntersectionCompute): R16F source of this
	// body, rewritten by every dispatch; consumed once by the step and the foam
	image_t *gpuIntersection = nullptr;
	int intersectionSlot = -1;			// entry in the hit readback buffer
	bool intersectionFresh = false;		// written since the last fixed step
	bool intersectionFoamFresh = false;	// written since the last foam update
};

// One independently allocated low-resolution concentration field per active
// local body. R/G of state are concentration/body mask. RGBA source channels
// keep waterfall, intersection, impact and shoreline/steepness independently
// inspectable and disableable.
struct waterFoamState_t
{
	int width = 0, height = 0, current = 0;
	float texelX = 0.0f, texelY = 0.0f;
	std::vector<byte> mask;
	std::vector<byte> source;
	std::vector<float> initial;
	image_t *image[2] = { nullptr, nullptr };
	image_t *sourceImage = nullptr;
	bool active = false;
	// localized sources are uploaded only on frames that have any (and once
	// more to clear them); constant and breaking-wave terms are uniforms/GPU
	bool sourceDirty = false, sourceInTexture = false;
};

// r_waterFlowField: see R_WaterFlowFieldBuildBody.
struct waterFlowSlap_t
{
	vec3_t position;
	float strength;
	int next;
	unsigned seed;
};

struct waterFlowFieldState_t
{
	int width = 0, height = 0;
	float texelX = 0.0f, texelY = 0.0f;
	// RGBA per texel: velocity / body speed (xy), shore distance / delta, impingement
	std::vector<float> data;
	image_t *image = nullptr;
	float delta = 0.0f, maxDistance = 0.0f, meanScale = 1.0f;
	int wallTexels = 0, openTexels = 0;
	waterFlowSlap_t slaps[4] = {};
	int numSlaps = 0;
	bool valid = false;
};

struct waterWakeTrack_t
{
	vec3_t position = { 0, 0, 0 };
	float carry = 0.0f;
	int lastTime = 0;
	bool initialized = false;
};

static const waterDynamics_t s_dynamics[] = {
	{ "generic_water", .15f, 96, .1f, 1, 1, 0, .8f, 1, .2f, .5f, 1, .45f, .08f, 1.0f },
	{ "still_pool", .025f, 48, .02f, .4f, .5f, 0, 1.3f, .4f, .05f, .2f, .6f, .02f, .01f, 0.0f },
	{ "calm_water", .08f, 80, .05f, .7f, .8f, 0, 1, .7f, .1f, .4f, .8f, .20f, .04f, 0.65f },
	{ "lake", .3f, 192, .2f, 1, 1, 0, .8f, 1, .3f, .7f, 1, .90f, .16f, 1.0f },
	{ "slow_stream", .12f, 64, .08f, 1.1f, 1, .4f, 1.2f, .8f, .3f, .7f, 1, .35f, .06f, 0.0f },
	{ "fast_stream", .2f, 48, .2f, 1.8f, 1.2f, 1, 1.8f, 1, .7f, 1, 1.2f, .12f, .04f, 0.0f },
	{ "slime", .04f, 64, .02f, .3f, .4f, 0, 2, .2f, .1f, .3f, .3f, .02f, 0, 0.0f },
	{ "waterfall", .2f, 40, .1f, 2, 1, 1, 1.5f, 1, .8f, 1, 1, 0, 0, 0.0f },
	{ "heavy_waterfall", .4f, 64, .2f, 2.5f, 1.2f, 1, 2, 1.5f, 1, 1, 1.2f, 0, 0, 0.0f },
	{ "no_water_dynamics", 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f },
};

// cos/sin(i * golden angle), shared by every body and draw.  Keeping this
// literal table avoids rebuilding an invariant basis for each wave set.
static constexpr float s_waterWaveDirectionBasis[8][2] = {
	{ 1.000000000f, 0.000000000f }, { -0.737368858f, 0.675490316f },
	{ 0.087425665f, -0.996171046f }, { 0.608438932f, 0.793600697f },
	{ -0.984713506f, -0.174181833f }, { 0.843755215f, -0.536728178f },
	{ -0.259604133f, 0.965715121f }, { -0.460907209f, -0.887448333f }
};

enum waterfallProfileId_t { WFPROFILE_SMALL, WFPROFILE_MEDIUM, WFPROFILE_HEAVY, WFPROFILE_COUNT };
struct waterfallProfile_t
{
	const char *name;
	float flowSpeed, thickness, breakup, normalScale, aeration;
	float opacity, spray, impact;
};
static const waterfallProfile_t s_waterfallProfiles[WFPROFILE_COUNT] = {
	{ "small", 72.0f, 2.5f, 0.45f, 0.75f, 0.45f, 0.75f, 0.35f, 0.45f },
	{ "medium", 120.0f, 5.0f, 0.75f, 1.0f, 0.75f, 1.0f, 0.75f, 0.85f },
	{ "heavy", 168.0f, 9.0f, 1.0f, 1.3f, 1.0f, 1.25f, 1.25f, 1.35f }
};

struct waterBody_t
{
	int id, model, liquidClass, opticsClass, dynamics;
	const char *decision;
	vec3_t bounds[2], planeNormal;
	float planeDist, area, depthAverage, depthDeepest;
	std::vector<int> surfaces, brushes;
	std::set<std::string> shaders;
	legacyWaterMotion_t legacy;
	vec3_t flowDirection, flowVelocity;
	float flowSpeed;
	int flowSource, flowConfidence;
	float waveMultiplier, foamMultiplier, interactionMultiplier;
	bool hasExplicitWind = false;
	vec3_t explicitWindDirection = { 0, 0, 0 };
	float explicitWindStrength = 0.0f;
	waterWindState_t wind;
	int geometryVerts, geometryTriangles, geometryLevel;
	float geometryMaxEdge = 0.0f;
	waterInteractionState_t interaction;
	waterFoamState_t foam;
	waterFlowFieldState_t flowField;
	bool waterfall = false;
	int waterfallProfile = WFPROFILE_MEDIUM;
	const char *waterfallDecision = "legacy fallback";
	waterfallProfile_t waterfallValues = s_waterfallProfiles[WFPROFILE_MEDIUM];
	float fallMin = 0.0f, fallMax = 1.0f;
	vec3_t fallDirection = { 0, 0, -1 }, impactCenter = {};
	float impactWidth = 0.0f;
	float viewCoverage = 1.0f, viewDistance = 0.0f;
};

// Map-load boundary segments are also the persistent wet-contact carriers.
// Heights are filtered macro/interaction heights; micro normals never move a
// contact line.  Only the nearest small set is sent to opaque material draws.
struct waterContactSegment_t
{
	vec3_t a, b;
	float wetA, wetB;
	float currentA, currentB;
	// splash / wake / wall-impact run-up above the current waterline, decays
	// in about a second; the wet hold then keeps it for the drying time
	float runUpA, runUpB;
	int bodyId;
	bool rigid;
};

static float R_WaterPointSegmentDistance2D(const vec3_t p, const vec3_t a, const vec3_t b);

struct waterBodyRule_t
{
	std::string shader;
	int id, dynamics;
	bool hasPoint, hasBounds, hasFlow, hasDynamics, flowUsesProfileSpeed, runtime;
	vec3_t point, bounds[2];
	vec3_t flowDirection;
	float flowSpeed;
	float wave, foam, interaction;
	bool hasWind;
	vec3_t windDirection;
	float windStrength;
};

struct waterfallRule_t
{
	std::string shader;
	int id = -1, profile = WFPROFILE_MEDIUM;
	bool hasPoint = false, hasBounds = false, hasProfile = false, enabled = true;
	vec3_t point = {}, bounds[2] = {};
	unsigned authored = 0;
	waterfallProfile_t values = s_waterfallProfiles[WFPROFILE_MEDIUM];
};

// A waterfall base source is either an acknowledgement that the BSP already
// owns an FX runner (the preferred source), or an authored/inferred froxel
// medium plus LensWater SPRAY source. It never owns arbitrary renderer-world
// coordinates: inferred origins come only from a classified sheet impact edge.
struct waterfallEmitterRule_t
{
	waterfallRule_t selector;
	bool existingFx = false;
	bool hasOrigin = false;
	vec3_t origin = {};
	float radius = 0.0f;
	float density = 1.0f;
	float lens = 1.0f;
};

enum waterfallImpactOrigin_t
{
	WFIMPACT_CLASSIFIED_BOTTOM,
	WFIMPACT_AUTHORED_EMITTER,
	WFIMPACT_EXISTING_FX
};
static const char *s_waterfallImpactOrigins[] = { "classified bottom", "authored emitter", "existing FX origin" };

// A resolved waterfall-to-horizontal-water connection.  It deliberately
// references an existing body-local field: no map-wide liquid disturbance and
// no second fluid solver are introduced.
struct waterfallImpactSource_t
{
	int waterfallBodyId = 0;
	int poolBodyId = 0;
	vec3_t origin = {};
	float radius = 0.0f;
	float strength = 1.0f;
	int originType = WFIMPACT_CLASSIFIED_BOTTOM;
	unsigned randomState = 1;
	int nextSecondaryTime = 0;
};

struct waterShaderRecord_t
{
	shader_t	*shader;
	int			bspShader;
	int			contents;
	int			surfaces;
	int			orientCount[4];
	float		area;
	float		upArea;
	float		downArea;
	int			linked;
	int			reason;
	int			liquidClass;	// optics
	int			flags;			// WATERSURF_*
	qboolean	modern;
};

struct waterPlane_t
{
	vec3_t		normal;
	float		dist;
};

// what the classification needs of every surface of the map (the BSP lumps
// are gone after the load): kept so r_waterOverride can add any shader later
struct waterSurfaceSource_t
{
	int			bspShader;
	int			model;
	int			type;			// MST_*
};

struct waterOverride_t
{
	std::string	pattern;		// lower case, trailing '*' = prefix
	int			mode;			// 1 on, 0 off
	int			liquidClass;	// -1 auto
};

static struct
{
	qboolean	resources;		// targets and programs exist (latched r_waterSurface)

	const world_t	*world;
	std::vector<waterBrushRecord_t>		brushes;
	std::vector<waterPlane_t>			planes;
	std::vector<waterSurfaceSource_t>	sources;
	std::vector<waterSurfaceRecord_t>	surfaces;
	std::vector<waterShaderRecord_t>	shaders;
	std::vector<waterBody_t>			bodies;
	std::vector<waterContactSegment_t>	contacts;
	int contactTime;
	int contactUniformTime;
	vec3_t contactUniformEye;
	vec4_t contactUniform[WATER_CONTACT_VEC4S];
	std::vector<waterBodyRule_t>		bodyRules;
	std::vector<waterfallRule_t>		waterfallRules;
	std::vector<waterfallEmitterRule_t>	waterfallEmitterRules;
	std::vector<waterfallImpactSource_t>	waterfallImpacts;
	bool mapWindValid;
	vec3_t mapWindDirection;
	float mapWindStrength;
	float		bodyMsec;
	float		geometryMsec;
	size_t		geometryBytes;
	qboolean	bodyDebug;
	qhandle_t	bodyDebugShader;
	float		classifyMsec;
	std::vector<int> mergedViewSurfaces;

	std::vector<waterOverride_t>		overrides;

	// per frame
	int			timer;
	int			lastDraws;
	int			drawsThisFrame;
	unsigned	statsFrame;
	screenHistory_t history;
	qboolean reflectionReady;
	qboolean reflectionHistoryValid;
	int reflectionCurrent;
	float reflectionScale;
	vec4_t viewWater[WATER_UNIFORM_VEC4S];
	qboolean viewWaterReady;
	vec4_t extinction[LIQUID_CLASSES], albedo[LIQUID_CLASSES];
	screenViewInfo_t viewInfo;
	vec4_t ssrSettings[3];
	int viewFlags;
	int fallbackCubemap;
	int copyRect[4];		// conservative visible-water rect, render-target pixels
	float waterfallProfileCoverage[WFPROFILE_COUNT]; // aggregate disconnected sheets sharing one profile
	std::vector<refWaterImpulse_t> pendingImpulses;
	std::vector<refWaterSource_t> continuousSources;
	std::unordered_map<int, waterWakeTrack_t> wakeTracks;
	unsigned interactionFrame;
	int interactionTime;
	float interactionAccumulator;
	int interactionSteps;
	float interactionSourceMsec;
	float interactionStepMsec;
	float interactionUploadMsec;
	double interactionSubmitUsec;
	unsigned interactionSubmissions;
	size_t interactionGpuBytes;
	image_t *interactionAtlas;
	image_t *previousInteractionAtlas;
	int interactionAtlasWidth, interactionAtlasHeight;
	unsigned interactionHistoryFrame;
	std::vector<waterMotionSnapshot_t> motionSnapshots;
	std::vector<waterMotionSnapshot_t> pendingMotionSnapshots;
	unsigned motionGeneration;
	unsigned motionCommitFrame;
	waterMotionInvalidation_t motionInvalidation;
	float motionLastWaveTime;
	bool motionWaveTimeValid;
	bool motionWaveTimeFrozen;
	float motionCopyMsec;
	double motionSnapshotUsec;
	unsigned motionSnapshotCount;
	unsigned foamFrame;
	int foamTime;
	size_t foamGpuBytes;
	size_t flowFieldGpuBytes;
	// GL 4.3 depth contacts: per body maximum source, read back without
	// waiting (a fence per entry) to wake sleeping bodies
	GLuint intersectionHitBuffers[WATER_INTERSECTION_HIT_RING];
	GLsync intersectionHitSync[WATER_INTERSECTION_HIT_RING];
	unsigned intersectionHitGeneration[WATER_INTERSECTION_HIT_RING];
	unsigned intersectionGeneration;
	int intersectionHitNext;
	int intersectionDispatches;
	int intersectionWakes;
} s_water;

static void R_WaterMotionInvalidate(waterMotionInvalidation_t reason)
{
	if (!r_waterSSRWaveReprojection || !r_waterSSRWaveReprojection->integer)
		return;
	s_water.motionGeneration++;
	s_water.motionCommitFrame = ~0u;
	s_water.interactionHistoryFrame = ~0u;
	s_water.motionInvalidation = reason;
	for (waterMotionSnapshot_t& snapshot : s_water.motionSnapshots) snapshot.valid = false;
	for (waterMotionSnapshot_t& snapshot : s_water.pendingMotionSnapshots) snapshot.valid = false;
}

qboolean R_WaterSurfaceResourcesEnabled( void )
{
	return s_water.resources;
}

qboolean R_WaterFoamResourcesEnabled( void )
{
	return (r_waterSurface && r_waterSurface->integer &&
		((r_waterFoamField && r_waterFoamField->integer) ||
		 (r_waterWhitewater && r_waterWhitewater->integer))) ? qtrue : qfalse;
}

static bool R_WaterInteractionGpuEnabled(void)
{
	return tr.waterInteractionFbo && tr.waterInteractionFieldShader.program;
}

// GL 4.3 depth contacts (glsl/water_intersection.glsl) instead of the
// readback of RB_WaterIntersectionInject: needs the GPU solver, whose step
// and the foam field consume the kernel's image
static void RB_WaterIntersectionPollHits(void);
static void RB_WaterIntersectionReleaseHits(void);

static bool R_WaterIntersectionComputeEnabled(void)
{
	return R_WaterInteractionGpuEnabled() && tr.waterIntersectionComputeShader.program &&
		R_HasModernFeatures(MODERN_COMPUTE | MODERN_IMAGE_LOAD_STORE | MODERN_SSBO);
}

/*
============================================================

Body-local interactive disturbance field (r_waterInteraction)

The GL 3.2 path integrates per-body RGBA16F state with fragment/FBO ping-pong
passes and resolves height/slope/foam directly into the shared render atlas.
The CPU integrator remains a no-FBO fallback. One update per real frame is
explicit; portal/reflection views only sample the latest resolved atlas.

============================================================
*/

static bool R_WaterInteractionPointInTriangle(float px, float py,
	const vec3_t a, const vec3_t b, const vec3_t c)
{
	const float e0 = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
	const float e1 = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
	const float e2 = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
	return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

static void R_WaterRasterBodyMask(const waterBody_t& body, int w, int h,
	float texelX, float texelY, std::vector<byte>& mask)
{
	mask.assign((size_t)w * h, 0);
	for (int surfaceNum : body.surfaces)
	{
		const msurface_t& ms = s_water.world->surfaces[surfaceNum];
		if (!(ms.waterKey & WATERKEY_INTERFACE) || !ms.data ||
			!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES)) continue;
		const srfBspSurface_t *surface = (const srfBspSurface_t *)ms.data;
		for (int ti = 0; ti + 2 < surface->numIndexes; ti += 3)
		{
			const int ia = surface->indexes[ti], ib = surface->indexes[ti + 1], ic = surface->indexes[ti + 2];
			if (ia < 0 || ib < 0 || ic < 0 || ia >= surface->numVerts || ib >= surface->numVerts || ic >= surface->numVerts) continue;
			const vec3_t& a = surface->verts[ia].xyz; const vec3_t& b = surface->verts[ib].xyz; const vec3_t& c = surface->verts[ic].xyz;
			const int x0 = Com_Clampi(0, w - 1, (int)floorf((MIN(a[0], MIN(b[0], c[0])) - body.bounds[0][0]) / texelX));
			const int x1 = Com_Clampi(0, w - 1, (int)floorf((MAX(a[0], MAX(b[0], c[0])) - body.bounds[0][0]) / texelX));
			const int y0 = Com_Clampi(0, h - 1, (int)floorf((MIN(a[1], MIN(b[1], c[1])) - body.bounds[0][1]) / texelY));
			const int y1 = Com_Clampi(0, h - 1, (int)floorf((MAX(a[1], MAX(b[1], c[1])) - body.bounds[0][1]) / texelY));
			for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
			{
				static const float offsets[5][2] = {{.5f,.5f},{.08f,.08f},{.92f,.08f},{.08f,.92f},{.92f,.92f}};
				for (int sample = 0; sample < 5; ++sample)
				{
					const float px = body.bounds[0][0] + (x + offsets[sample][0]) * texelX;
					const float py = body.bounds[0][1] + (y + offsets[sample][1]) * texelY;
					if (R_WaterInteractionPointInTriangle(px, py, a, b, c)) { mask[(size_t)y * w + x] = 255; break; }
				}
			}
		}
	}
}

static bool R_WaterBrushVerticalInterval(const waterBrushRecord_t& brush,
	float x, float y, float& bottom, float& top)
{
	bottom = -1.0e30f; top = 1.0e30f;
	for (int k = 0; k < brush.numPlanes; ++k)
	{
		const waterPlane_t& plane = s_water.planes[brush.firstPlane + k];
		const float rhs = plane.dist - plane.normal[0] * x - plane.normal[1] * y;
		if (fabsf(plane.normal[2]) < 1.0e-6f)
		{
			if (rhs < -0.5f) return false;
			continue;
		}
		const float z = rhs / plane.normal[2];
		if (plane.normal[2] > 0.0f) top = MIN(top, z);
		else bottom = MAX(bottom, z);
		if (bottom > top + 0.5f) return false;
	}
	return bottom > -1.0e29f && top < 1.0e29f;
}

static void R_WaterBuildBathymetry(waterBody_t& body)
{
	waterInteractionState_t& sim = body.interaction;
	const size_t cells = (size_t)sim.width * sim.height;
	sim.bathymetry.assign(cells * 4, 0.0f);
	sim.bathymetryMaxDepth = 0.0f;
	if (!r_waterBathymetry->integer || fabsf(body.planeNormal[2]) < 0.9f)
		return;

	int exact = 0, intervalOnly = 0, invalid = 0;
	for (int y = 0; y < sim.height; ++y) for (int x = 0; x < sim.width; ++x)
	{
		const size_t i = (size_t)y * sim.width + x;
		if (!sim.mask[i]) continue;
		const float wx = body.bounds[0][0] + (x + 0.5f) * sim.texelX;
		const float wy = body.bounds[0][1] + (y + 0.5f) * sim.texelY;
		const float surface = (body.planeDist - body.planeNormal[0] * wx -
			body.planeNormal[1] * wy) / body.planeNormal[2];
		float liquidBottom = 1.0e30f;
		bool interval = false;
		for (int brushIndex : body.brushes)
		{
			if (brushIndex < 0 || brushIndex >= (int)s_water.brushes.size()) continue;
			float bottom, top;
			if (!R_WaterBrushVerticalInterval(s_water.brushes[brushIndex], wx, wy, bottom, top)) continue;
			// Only the contiguous liquid volume touching this body's surface is
			// eligible. A lower floor/brush on another storey cannot participate.
			if (surface < bottom - 0.5f || surface > top + 1.0f) continue;
			liquidBottom = MIN(liquidBottom, bottom);
			interval = true;
		}
		float depth = body.depthAverage;
		float confidence = 0.0f;
		if (interval && liquidBottom < surface - 0.25f)
		{
			depth = surface - liquidBottom;
			confidence = 0.5f; // exact liquid interval, no confirmed solid floor
			vec3_t start = { wx, wy, surface - 0.25f };
			vec3_t end = { wx, wy, liquidBottom - 1.0f };
			trace_t trace;
			Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
			ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end,
				ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
			ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
			if (!trace.startsolid && !trace.allsolid && trace.fraction < 1.0f &&
				trace.endpos[2] >= liquidBottom - 0.75f && trace.endpos[2] <= surface)
			{
				depth = surface - trace.endpos[2];
				confidence = 1.0f;
				++exact;
			}
			else ++intervalOnly;
		}
		else ++invalid;
		depth = MAX(depth, 0.5f);
		sim.bathymetry[i * 4] = depth;
		sim.bathymetry[i * 4 + 1] = confidence;
		sim.bathymetry[i * 4 + 3] = 1.0f;
		if (confidence >= 0.75f) sim.bathymetryMaxDepth = MAX(sim.bathymetryMaxDepth, depth);
	}
	if (r_waterBathymetryDebug->integer)
		ri.Printf(PRINT_ALL, "water bathymetry body %d: exact %d, interval-only %d, invalid %d, max %.1f\n",
			body.id, exact, intervalOnly, invalid, sim.bathymetryMaxDepth);
}

static bool R_WaterFoamAllocate(waterBody_t& body)
{
	waterFoamState_t& foam = body.foam;
	if (foam.active) return true;
	// The current persistent field is an XY projection for horizontal bodies.
	// Waterfalls expose sheet-local sources in their shader/profile data until
	// that field gains an across/fall coordinate domain.
	if (!R_WaterFoamResourcesEnabled() || body.liquidClass == LIQUID_LAVA || body.waterfall) return false;
	bool modern = false;
	for (int surfaceNum : body.surfaces)
		if (s_water.world->surfaces[surfaceNum].waterKey & WATERKEY_INTERFACE) { modern = true; break; }
	if (!modern) return false;
	const int longest = Com_Clampi(32, 512, r_waterFoamResolution->integer);
	const float extentX = MAX(body.bounds[1][0] - body.bounds[0][0], 1.0f);
	const float extentY = MAX(body.bounds[1][1] - body.bounds[0][1], 1.0f);
	if (extentX >= extentY) { foam.width = longest; foam.height = MAX(16, (int)ceilf(longest * extentY / extentX)); }
	else { foam.height = longest; foam.width = MAX(16, (int)ceilf(longest * extentX / extentY)); }
	foam.texelX = extentX / foam.width; foam.texelY = extentY / foam.height;
	R_WaterRasterBodyMask(body, foam.width, foam.height, foam.texelX, foam.texelY, foam.mask);
	foam.source.assign((size_t)foam.width * foam.height * 4, 0);
	foam.initial.assign((size_t)foam.width * foam.height * 4, 0.0f);
	for (size_t i = 0; i < foam.mask.size(); ++i) foam.initial[i * 4 + 1] = foam.mask[i] ? 1.0f : 0.0f;
	for (int i = 0; i < 2; ++i)
	{
		foam.image[i] = R_CreateImage(va("*waterFoam%d_%d", body.id, i), NULL,
			foam.width, foam.height, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE, GL_RGBA16F);
		GL_Bind(foam.image[i]);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, foam.width, foam.height, GL_RGBA, GL_FLOAT, foam.initial.data());
	}
	foam.sourceImage = R_CreateImage(va("*waterFoamSource%d", body.id), NULL,
		foam.width, foam.height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE, GL_RGBA8);
	foam.active = true;
	s_water.foamGpuBytes += (size_t)foam.width * foam.height * 20;
	return true;
}

static void R_WaterFoamBuild(void)
{
	s_water.foamGpuBytes = 0;
	s_water.foamFrame = ~0u;
	s_water.foamTime = -1;
	if (!R_WaterFoamResourcesEnabled() || !s_water.world)
		return;
	int allocated = 0;
	for (waterBody_t& body : s_water.bodies)
	{
		bool modern = false;
		for (int surfaceNum : body.surfaces)
			if (s_water.world->surfaces[surfaceNum].waterKey & WATERKEY_INTERFACE) { modern = true; break; }
		const bool turbulent = body.dynamics == 5 || body.dynamics == 7 || body.dynamics == 8;
		const bool interactionSource = body.interaction.image &&
			(r_waterFoamImpact->value > 0.0f || r_waterFoamIntersection->value > 0.0f);
		const bool needed = (turbulent && (r_waterFoamWaterfall->value > 0.0f || r_waterWhitewater->integer)) || interactionSource ||
			(r_waterShoreline->integer && r_waterFoamShoreline->value > 0.0f) ||
			r_waterFoamSteepness->value > 0.0f || (r_waterWhitewater->integer && body.interaction.image);
		if (!modern || !needed || body.liquidClass == LIQUID_LAVA)
			continue;
		if (R_WaterFoamAllocate(body)) ++allocated;
	}
	ri.Printf(PRINT_DEVELOPER, "water foam: %d local body fields, %.2f MiB GPU; GL3.2 FBO path\n",
		allocated, s_water.foamGpuBytes / (1024.0f * 1024.0f));
}

static void R_WaterInteractionBuild(void)
{
	R_WaterMotionInvalidate(WM_INVALID_INTERACTION_REMAP);
	s_water.interactionGpuBytes = 0;
	s_water.interactionAccumulator = 0.0f;
	s_water.interactionTime = -1;
	s_water.interactionFrame = ~0u;
	s_water.interactionAtlas = nullptr;
	s_water.previousInteractionAtlas = nullptr;
	s_water.interactionAtlasWidth = s_water.interactionAtlasHeight = 0;
	// contact readbacks in flight refer to the old body slots
	s_water.intersectionGeneration++;
	int intersectionSlots = 0;
	if (!r_waterInteraction || !r_waterInteraction->integer || !r_waterSurface->integer || !s_water.world)
		return;

	const float worldPerTexel = r_waterInteractionQuality->integer <= 0 ? 32.0f :
		(r_waterInteractionQuality->integer == 1 ? 16.0f : 8.0f);
	const int maxBodies = Com_Clampi(1, 32, r_waterInteractionMaxBodies->integer);
	// Atlas + two RGBA16F state images + one RGBA16F source image = 32 B/cell.
	const size_t requestedTexels = (size_t)Q_min(r_waterInteractionMaxTexels->integer,
		(r_waterInteractionMemoryMB->integer * 1024 * 1024) / 32);
	const size_t atlasLimit = (size_t)glConfig.maxTextureSize * glConfig.maxTextureSize / 2;
	const size_t texelBudget = MIN(requestedTexels, atlasLimit);
	size_t used = 0;
	int allocated = 0;
	for (waterBody_t& body : s_water.bodies)
	{
		if (body.waterfall) continue;
		bool eligible = false;
		for (int surfaceNum : body.surfaces)
			if (s_water.world->surfaces[surfaceNum].waterKey & WATERKEY_INTERFACE) { eligible = true; break; }
		if (!eligible || allocated >= maxBodies || body.liquidClass == LIQUID_LAVA || body.surfaces.empty())
			continue;
		waterInteractionState_t& sim = body.interaction;
		const float extentX = MAX(body.bounds[1][0] - body.bounds[0][0], worldPerTexel);
		const float extentY = MAX(body.bounds[1][1] - body.bounds[0][1], worldPerTexel);
		int w = Com_Clampi(16, 512, (int)ceilf(extentX / worldPerTexel));
		int h = Com_Clampi(16, 512, (int)ceilf(extentY / worldPerTexel));
		w = (w + 7) & ~7; h = (h + 7) & ~7;
		while (used + (size_t)w * h > texelBudget && (w > 16 || h > 16))
		{
			if (w >= h && w > 16) w = MAX(16, w / 2);
			else if (h > 16) h = MAX(16, h / 2);
		}
		if (used + (size_t)w * h > texelBudget) continue;
		sim.width = w; sim.height = h;
		sim.texelX = extentX / w; sim.texelY = extentY / h;
		const size_t cells = (size_t)w * h;
		sim.mask.assign(cells, 0);
		sim.state.assign(cells * 3, 0.0f);
		sim.next.assign(cells * 3, 0.0f);
		sim.intersectionFoam.assign(cells, 0.0f);
		sim.intersectionNext.assign(cells, 0.0f);
		sim.upload.assign(cells * 4, 0.0f);
		sim.gpuInitial.assign(cells * 4, 0.0f);
		sim.gpuSourceData.assign(cells * 4, 0.0f);
		for (size_t i = 0; i < cells; ++i) sim.gpuInitial[i * 4 + 3] = -1.0f;

		// Rasterise actual usable top triangles. Five sub-cell samples make thin
		// boundary triangles conservative without filling rectangular voids.
		for (int surfaceNum : body.surfaces)
		{
			const msurface_t& ms = s_water.world->surfaces[surfaceNum];
			if (!(ms.waterKey & WATERKEY_INTERFACE) || !ms.data ||
				!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES)) continue;
			const srfBspSurface_t *surface = (const srfBspSurface_t *)ms.data;
			for (int ti = 0; ti + 2 < surface->numIndexes; ti += 3)
			{
				const int ia = surface->indexes[ti], ib = surface->indexes[ti + 1], ic = surface->indexes[ti + 2];
				if (ia < 0 || ib < 0 || ic < 0 || ia >= surface->numVerts || ib >= surface->numVerts || ic >= surface->numVerts) continue;
				const vec3_t& a = surface->verts[ia].xyz; const vec3_t& b = surface->verts[ib].xyz; const vec3_t& c = surface->verts[ic].xyz;
				const int x0 = Com_Clampi(0, w - 1, (int)floorf((MIN(a[0], MIN(b[0], c[0])) - body.bounds[0][0]) / sim.texelX));
				const int x1 = Com_Clampi(0, w - 1, (int)floorf((MAX(a[0], MAX(b[0], c[0])) - body.bounds[0][0]) / sim.texelX));
				const int y0 = Com_Clampi(0, h - 1, (int)floorf((MIN(a[1], MIN(b[1], c[1])) - body.bounds[0][1]) / sim.texelY));
				const int y1 = Com_Clampi(0, h - 1, (int)floorf((MAX(a[1], MAX(b[1], c[1])) - body.bounds[0][1]) / sim.texelY));
				for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
				{
					static const float offsets[5][2] = {{.5f,.5f},{.08f,.08f},{.92f,.08f},{.08f,.92f},{.92f,.92f}};
					for (int sample = 0; sample < 5; ++sample)
					{
						const float px = body.bounds[0][0] + (x + offsets[sample][0]) * sim.texelX;
						const float py = body.bounds[0][1] + (y + offsets[sample][1]) * sim.texelY;
						if (R_WaterInteractionPointInTriangle(px, py, a, b, c)) { sim.mask[(size_t)y * w + x] = 255; break; }
					}
				}
			}
		}
		R_WaterBuildBathymetry(body);
		const size_t wetCount = (size_t)std::count_if(sim.mask.begin(), sim.mask.end(), [](byte value) { return value != 0; });
		sim.wetCells.reserve(wetCount);
		sim.wetNeighbours.reserve(wetCount);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			const int i = y * w + x;
			if (!sim.mask[i]) continue;
			auto neighbour = [&](int nx, int ny) -> int {
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) return i;
				const int n = ny * w + nx;
				return sim.mask[n] ? n : i;
			};
			sim.wetCells.push_back(i);
			sim.wetNeighbours.push_back({{ neighbour(x - 1, y), neighbour(x + 1, y),
				neighbour(x, y - 1), neighbour(x, y + 1) }});
			// Alpha encodes both occupancy and foam: dry is zero, wet is
			// [1,2].  This survives bilinear filtering while leaving RGB for
			// height and the pre-resolved world-space slope.
			sim.upload[(size_t)i * 4 + 3] = 1.0f;
			sim.gpuInitial[(size_t)i * 4 + 3] = 0.0f;
		}
		used += cells; ++allocated;
	}
	if (allocated)
	{
		int minimumWidth = 16;
		for (const waterBody_t& body : s_water.bodies)
			minimumWidth = MAX(minimumWidth, body.interaction.width);
		int firstWidth = 16; while (firstWidth < minimumWidth) firstWidth <<= 1;
		int atlasWidth = firstWidth, atlasHeight = glConfig.maxTextureSize;
		size_t bestArea = ~(size_t)0;
		// Try every power-of-two shelf width and keep the smallest real texture.
		// This avoids paying for a fixed wide atlas when a map has one tiny pool.
		for (int candidateWidth = firstWidth; candidateWidth <= glConfig.maxTextureSize; candidateWidth <<= 1)
		{
			int x = 0, y = 0, rowHeight = 0;
			for (const waterBody_t& body : s_water.bodies)
			{
				const waterInteractionState_t& sim = body.interaction;
				if (!sim.width) continue;
				if (x + sim.width > candidateWidth) { x = 0; y += rowHeight; rowHeight = 0; }
				x += sim.width; rowHeight = MAX(rowHeight, sim.height);
			}
			int candidateHeight = 16; while (candidateHeight < y + rowHeight) candidateHeight <<= 1;
			const size_t area = (size_t)candidateWidth * candidateHeight;
			if (candidateHeight <= glConfig.maxTextureSize && area < bestArea)
			{ bestArea = area; atlasWidth = candidateWidth; atlasHeight = candidateHeight; }
			if (candidateWidth > glConfig.maxTextureSize / 2) break;
		}
		int x = 0, y = 0, rowHeight = 0;
		for (waterBody_t& body : s_water.bodies)
		{
			waterInteractionState_t& sim = body.interaction;
			if (!sim.width) continue;
			if (x + sim.width > atlasWidth) { x = 0; y += rowHeight; rowHeight = 0; }
			sim.atlasX = x; sim.atlasY = y; x += sim.width; rowHeight = MAX(rowHeight, sim.height);
		}
		s_water.interactionAtlasWidth = atlasWidth; s_water.interactionAtlasHeight = atlasHeight;
		s_water.interactionAtlas = R_CreateImage("*waterInteractionAtlas", NULL, atlasWidth, atlasHeight,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		if (r_waterSSRWaveReprojection && r_waterSSRWaveReprojection->integer)
			s_water.previousInteractionAtlas = R_CreateImage("*waterPreviousInteractionAtlas", NULL, atlasWidth, atlasHeight,
				IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		GL_BindToTMU(s_water.interactionAtlas, 19); GL_SelectTexture(19);
		for (waterBody_t& body : s_water.bodies)
		{
			waterInteractionState_t& sim = body.interaction;
			if (!sim.width) continue;
			sim.image = s_water.interactionAtlas;
			qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.atlasX, sim.atlasY, sim.width, sim.height,
				GL_RGBA, GL_FLOAT, sim.upload.data());
			if (s_water.previousInteractionAtlas)
			{
				GL_BindToTMU(s_water.previousInteractionAtlas, 24); GL_SelectTexture(24);
				qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.atlasX, sim.atlasY, sim.width, sim.height,
					GL_RGBA, GL_FLOAT, sim.upload.data());
				GL_BindToTMU(s_water.interactionAtlas, 19); GL_SelectTexture(19);
			}
			for (int state = 0; state < 2; ++state)
			{
				sim.gpuState[state] = R_CreateImage(va("*waterInteractionState%d_%d", body.id, state), NULL,
					sim.width, sim.height, IMGTYPE_COLORALPHA,
					IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE, GL_RGBA16F);
				GL_Bind(sim.gpuState[state]);
				qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sim.width, sim.height,
					GL_RGBA, GL_FLOAT, sim.gpuInitial.data());
			}
			sim.gpuSource = R_CreateImage(va("*waterInteractionSource%d", body.id), NULL,
				sim.width, sim.height, IMGTYPE_COLORALPHA,
				IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE, GL_RGBA16F);
			GL_Bind(sim.gpuSource);
			qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sim.width, sim.height,
				GL_RGBA, GL_FLOAT, sim.gpuSourceData.data());
			if (R_WaterIntersectionComputeEnabled() && intersectionSlots < WATER_INTERSECTION_MAX_SLOTS)
			{
				std::vector<float> zero((size_t)sim.width * sim.height, 0.0f);
				sim.gpuIntersection = R_CreateImage(va("*waterIntersection%d", body.id), NULL,
					sim.width, sim.height, IMGTYPE_COLORALPHA,
					IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_R16F);
				GL_Bind(sim.gpuIntersection);
				qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sim.width, sim.height,
					GL_RED, GL_FLOAT, zero.data());
				sim.intersectionSlot = intersectionSlots++;
			}
			if (r_waterBathymetry->integer && !sim.bathymetry.empty())
			{
				sim.bathymetryImage = R_CreateImage(va("*waterBathymetry%d", body.id), NULL,
					sim.width, sim.height, IMGTYPE_COLORALPHA,
					IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
				GL_Bind(sim.bathymetryImage);
				qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sim.width, sim.height,
					GL_RGBA, GL_FLOAT, sim.bathymetry.data());
			}
		}
	}
	s_water.interactionGpuBytes = s_water.interactionAtlas ?
		(size_t)s_water.interactionAtlasWidth * s_water.interactionAtlasHeight * 8 +
		used * (r_waterBathymetry->integer ? 32 : 24) : 0;
	if (s_water.previousInteractionAtlas)
		s_water.interactionGpuBytes += (size_t)s_water.interactionAtlasWidth * s_water.interactionAtlasHeight * 8;
	ri.Printf(PRINT_DEVELOPER, "water interaction: %d body fields, %u active texels, %dx%d atlas, %.2f MiB GPU\n",
		allocated, (unsigned)used, s_water.interactionAtlasWidth, s_water.interactionAtlasHeight,
		s_water.interactionGpuBytes / (1024.0f * 1024.0f));
}

// Gameplay entries happen right at the waterline, where the discrete mask
// often misses the exact texel.  Accept the nearest wet texel within two
// texels (the same tolerance the crosshair impulse command uses).
static bool R_WaterInteractionMaskNear(const waterInteractionState_t& sim, int x, int y)
{
	for (int radius = 0; radius <= 2; ++radius)
		for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx)
		{
			if (MAX(abs(dx), abs(dy)) != radius) continue;
			const int sx = x + dx, sy = y + dy;
			if (sx < 0 || sy < 0 || sx >= sim.width || sy >= sim.height) continue;
			if (sim.mask[(size_t)sy * sim.width + sx]) return true;
		}
	return false;
}

static waterBody_t *R_WaterInteractionBodyAt(const vec3_t position)
{
	waterBody_t *best = nullptr;
	float bestPlane = 1.0e30f;
	for (waterBody_t& body : s_water.bodies)
	{
		waterInteractionState_t& sim = body.interaction;
		const float marginX = 2.0f * sim.texelX, marginY = 2.0f * sim.texelY;
		if (!sim.image || position[0] < body.bounds[0][0] - marginX || position[0] > body.bounds[1][0] + marginX ||
			position[1] < body.bounds[0][1] - marginY || position[1] > body.bounds[1][1] + marginY) continue;
		const int x = (int)floorf((position[0] - body.bounds[0][0]) / sim.texelX);
		const int y = (int)floorf((position[1] - body.bounds[0][1]) / sim.texelY);
		if (!R_WaterInteractionMaskNear(sim, x, y)) continue;
		const float plane = fabsf(DotProduct(body.planeNormal, position) - body.planeDist);
		if (plane < bestPlane) { best = &body; bestPlane = plane; }
	}
	return best;
}

static float R_WaterInteractionSurfaceHeight(const waterBody_t& body, const vec3_t position)
{
	const float nz = fabsf(body.planeNormal[2]) > 1.0e-4f ? body.planeNormal[2] : 1.0f;
	float height = (body.planeDist - body.planeNormal[0] * position[0] -
		body.planeNormal[1] * position[1]) / nz;
	const waterInteractionState_t& sim = body.interaction;
	if (!sim.image || sim.width < 1 || sim.height < 1)
		return height;
	const float fx = (position[0] - body.bounds[0][0]) / sim.texelX - 0.5f;
	const float fy = (position[1] - body.bounds[0][1]) / sim.texelY - 0.5f;
	const int x0 = Com_Clampi(0, sim.width - 1, (int)floorf(fx));
	const int y0 = Com_Clampi(0, sim.height - 1, (int)floorf(fy));
	const int x1 = MIN(x0 + 1, sim.width - 1), y1 = MIN(y0 + 1, sim.height - 1);
	const float tx = Com_Clamp(0.0f, 1.0f, fx - floorf(fx));
	const float ty = Com_Clamp(0.0f, 1.0f, fy - floorf(fy));
	const float h00 = sim.state[((size_t)y0 * sim.width + x0) * 3];
	const float h10 = sim.state[((size_t)y0 * sim.width + x1) * 3];
	const float h01 = sim.state[((size_t)y1 * sim.width + x0) * 3];
	const float h11 = sim.state[((size_t)y1 * sim.width + x1) * 3];
	return height + (h00 + (h10 - h00) * tx) +
		((h01 + (h11 - h01) * tx) - (h00 + (h10 - h00) * tx)) * ty;
}

static void R_WaterUpdateWindWaves(waterBody_t& body, float amplitude, float wavelength,
	float now, const precipitationState_t& precipitation, vec4_t *water);
static float R_WaterlineWaveHeight(const waterBody_t& body, const vec3_t position,
	float time, float shoreAttenuation, vec4_t *water);

static int R_WaterGeometryComponentCount(const waterBody_t& body, const vec4_t *terms)
{
	if (!r_waterWaves->integer || !r_waterGeometry->integer || body.waterfall ||
		body.geometryVerts <= 0 || body.geometryMaxEdge <= 0.0f)
		return 0;
	const int requested = r_waterWaveQuality->integer <= 0 ? 1 : 2;
	const float minimumWavelength = body.geometryMaxEdge * 4.0f;
	int resolved = 0;
	for (; resolved < requested; ++resolved)
	{
		const float k = terms[resolved][3];
		const float wavelength = k > 1.0e-6f ? 6.2831853f / k : 0.0f;
		if (wavelength + 1.0e-3f < minimumWavelength) break;
	}
	return resolved;
}

static bool R_WaterFlowFieldSample(const waterBody_t& body, float worldX, float worldY, float out[4]);
static float R_WaterFlowImpactHead(const waterBody_t& body);

static void RB_WaterContactUpdate(void)
{
	if (!r_waterShoreline->integer || s_water.contacts.empty()) return;
	const int now = backEnd.refdef.time;
	const float dt = s_water.contactTime > 0 ? Com_Clamp(0.0f, 0.25f, (now - s_water.contactTime) * 0.001f) : 0.0f;
	s_water.contactTime = now;
	s_water.contactUniformTime = -1;
	const float time = r_waterWaveTime->value >= 0.0f ? r_waterWaveTime->value : backEnd.refdef.floatTime;
	const float persistence = MAX(r_waterShoreWetPersistence->value, 0.0f);
	int preparedBodyId = -1;
	vec4_t contactWater[WATER_UNIFORM_VEC4S] = {};
	for (waterContactSegment_t& contact : s_water.contacts)
	{
		if (contact.bodyId <= 0 || contact.bodyId > (int)s_water.bodies.size()) continue;
		waterBody_t& body = s_water.bodies[contact.bodyId - 1];
		if (preparedBodyId != body.id)
		{
			Com_Memset(contactWater, 0, sizeof(contactWater));
			const waterDynamics_t& dynamics = s_dynamics[body.dynamics];
			const float legacyScale = body.legacy.deformAmplitude > 0.5f ? 0.2f : 1.0f;
			const float amplitude = dynamics.amplitude * 8.0f * legacyScale * body.waveMultiplier *
				Com_Clamp(0.0f, 4.0f, r_waterWaveAmplitude->value);
			const float wavelength = MAX(dynamics.wavelength *
				Com_Clamp(0.25f, 4.0f, r_waterWaveLength->value), 8.0f);
			R_WaterUpdateWindWaves(body, amplitude, wavelength, backEnd.refdef.floatTime,
				backEnd.refdef.precipitation, contactWater);
			preparedBodyId = body.id;
		}
		const float profileShore = Com_Clamp(0.0f, 1.5f, s_dynamics[body.dynamics].shoreline);
		auto height = [&](const vec3_t p) -> float {
			const float shore = contact.rigid ?
				Com_Clamp(0.03f, 0.08f, 0.03f + profileShore * 0.033333f) :
				Com_Clamp(0.15f, 0.40f, 0.15f + profileShore * 0.25f);
			const float interactionShore = contact.rigid ?
				Com_Clamp(0.45f, 0.55f, 0.45f + profileShore * 0.05f) :
				Com_Clamp(0.60f, 0.75f, 0.60f + profileShore * 0.10f);
			const float base = (body.planeDist - body.planeNormal[0] * p[0] - body.planeNormal[1] * p[1]) /
				MAX(fabsf(body.planeNormal[2]), 1.0e-4f);
			const float interaction = R_WaterInteractionSurfaceHeight(body, p) - base;
			return base + R_WaterlineWaveHeight(body, p, time, shore, contactWater) + interaction * interactionShore;
		};
		// Flow running into a wall stands up against it (stagnation head).
		float pileA = 0.0f, pileB = 0.0f;
		if (r_waterFlowImpact && r_waterFlowImpact->integer && body.flowField.valid)
		{
			const float head = R_WaterFlowImpactHead(body);
			float sample[4];
			if (R_WaterFlowFieldSample(body, contact.a[0], contact.a[1], sample)) pileA = head * sample[3];
			if (R_WaterFlowFieldSample(body, contact.b[0], contact.b[1], sample)) pileB = head * sample[3];
		}
		const float runUpDecay = expf(-dt * 1.5f);
		contact.runUpA *= runUpDecay; contact.runUpB *= runUpDecay;
		contact.currentA = height(contact.a) + pileA + contact.runUpA;
		contact.currentB = height(contact.b) + pileB + contact.runUpB;
		auto dry = [dt, persistence](float wet, float current) -> float {
			if (current >= wet || persistence <= 0.0f) return current;
			return wet + (current - wet) * MIN(dt / persistence, 1.0f);
		};
		contact.wetA = dry(contact.wetA, contact.currentA);
		contact.wetB = dry(contact.wetB, contact.currentB);
	}
}

static waterBody_t *R_WaterFoamBodyAt(const vec3_t position)
{
	waterBody_t *best = nullptr;
	float bestPlane = 1.0e30f;
	for (waterBody_t& body : s_water.bodies)
	{
		if (body.waterfall) continue;
		if (position[0] < body.bounds[0][0] || position[0] > body.bounds[1][0] ||
			position[1] < body.bounds[0][1] || position[1] > body.bounds[1][1]) continue;
		const float plane = fabsf(DotProduct(body.planeNormal, position) - body.planeDist);
		if (plane < bestPlane) { best = &body; bestPlane = plane; }
	}
	if (!best || !R_WaterFoamAllocate(*best)) return nullptr;
	waterFoamState_t& foam = best->foam;
	const int x = Com_Clampi(0, foam.width - 1, (int)((position[0] - best->bounds[0][0]) / foam.texelX));
	const int y = Com_Clampi(0, foam.height - 1, (int)((position[1] - best->bounds[0][1]) / foam.texelY));
	if (!foam.mask[(size_t)y * foam.width + x]) return nullptr;
	return best;
}

// Channels: R waterfall (rate), G intersection (rate), B continuous impact /
// shoreline / steepness API sources (rate), A one-shot splash deposit.
static void R_WaterFoamInject(waterBody_t& body, const vec3_t position,
	float radius, float strength, int type, bool oneShot = false)
{
	waterFoamState_t& foam = body.foam;
	if (!foam.active || strength <= 0.0f) return;
	int channel = 2;
	if (oneShot) channel = 3;
	else if (type == WATERFOAMSOURCE_WATERFALL) channel = 0;
	else if (type == WATERFOAMSOURCE_INTERSECTION) channel = 1;
	foam.sourceDirty = true;
	radius = MAX(radius, 1.5f * MAX(foam.texelX, foam.texelY));
	const int x0 = Com_Clampi(0, foam.width - 1, (int)floorf((position[0] - radius - body.bounds[0][0]) / foam.texelX));
	const int x1 = Com_Clampi(0, foam.width - 1, (int)floorf((position[0] + radius - body.bounds[0][0]) / foam.texelX));
	const int y0 = Com_Clampi(0, foam.height - 1, (int)floorf((position[1] - radius - body.bounds[0][1]) / foam.texelY));
	const int y1 = Com_Clampi(0, foam.height - 1, (int)floorf((position[1] + radius - body.bounds[0][1]) / foam.texelY));
	for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
	{
		const size_t i = (size_t)y * foam.width + x;
		if (!foam.mask[i]) continue;
		const float px = body.bounds[0][0] + (x + 0.5f) * foam.texelX;
		const float py = body.bounds[0][1] + (y + 0.5f) * foam.texelY;
		const float d2 = ((px - position[0]) * (px - position[0]) + (py - position[1]) * (py - position[1])) / (radius * radius);
		if (d2 >= 1.0f) continue;
		const int value = (int)(255.0f * Com_Clamp(0.0f, 1.0f, strength * (1.0f - d2) * (1.0f - d2)));
		foam.source[i * 4 + channel] = (byte)MAX((int)foam.source[i * 4 + channel], value);
	}
}

void RB_WaterContactUniforms(vec4_t *contact, int vec4Count)
{
	if (!contact || vec4Count <= 0) return;
	Com_Memset(contact, 0, sizeof(vec4_t) * vec4Count);
	if (!r_waterShoreline->integer || s_water.contacts.empty() || vec4Count < 3) return;
	const int copyCount = MIN(vec4Count, WATER_CONTACT_VEC4S);
	const vec3_t& eye = backEnd.viewParms.ori.origin;
	if (s_water.contactUniformTime == backEnd.refdef.time &&
		VectorCompare(s_water.contactUniformEye, eye))
	{
		Com_Memcpy(contact, s_water.contactUniform, sizeof(vec4_t) * copyCount);
		return;
	}
	struct candidate_t { float distance; const waterContactSegment_t *segment; };
	std::array<candidate_t, WATER_CONTACT_SEGMENTS> nearest;
	int nearestCount = 0;
	for (const waterContactSegment_t& segment : s_water.contacts)
	{
		const float planar = R_WaterPointSegmentDistance2D(eye, segment.a, segment.b);
		const float z = 0.5f * (segment.currentA + segment.currentB);
		const float dz = eye[2] - z;
		candidate_t candidate = { planar * planar + 0.25f * dz * dz, &segment };
		int position = nearestCount;
		while (position > 0 && candidate.distance < nearest[position - 1].distance) --position;
		if (position >= WATER_CONTACT_SEGMENTS) continue;
		const int moveEnd = MIN(nearestCount, WATER_CONTACT_SEGMENTS - 1);
		for (int i = moveEnd; i > position; --i) nearest[i] = nearest[i - 1];
		nearest[position] = candidate;
		nearestCount = MIN(nearestCount + 1, WATER_CONTACT_SEGMENTS);
	}
	const int count = MIN(nearestCount, (copyCount - 1) / 2);
	Com_Memset(s_water.contactUniform, 0, sizeof(s_water.contactUniform));
	VectorSet4(s_water.contactUniform[0], Com_Clamp(0.0f, 2.0f, r_waterShoreWetStrength->value),
		MAX(r_waterShoreContactSoftness->value, 0.25f), (float)r_waterShoreDebug->integer, (float)count);
	for (int i = 0; i < count; ++i)
	{
		const waterContactSegment_t& segment = *nearest[i].segment;
		VectorSet4(s_water.contactUniform[1 + i * 2], segment.a[0], segment.a[1], segment.currentA, segment.wetA);
		VectorSet4(s_water.contactUniform[2 + i * 2], segment.b[0], segment.b[1], segment.currentB, segment.wetB);
	}
	s_water.contactUniformTime = backEnd.refdef.time;
	VectorCopy(eye, s_water.contactUniformEye);
	Com_Memcpy(contact, s_water.contactUniform, sizeof(vec4_t) * copyCount);
}

// Raise the recent contact height of the body's shoreline segments near a
// disturbance.  The GPU disturbance field is never read back, so splashes,
// wakes and wall impacts report their estimated run-up here directly.
static void R_WaterShoreRunUp(const waterBody_t& body, const vec3_t position, float reach, float height)
{
	if (!r_waterShoreline->integer || height <= 0.01f || reach <= 0.0f) return;
	height = MIN(height, 24.0f);
	for (waterContactSegment_t& contact : s_water.contacts)
	{
		if (contact.bodyId != body.id) continue;
		const float da = sqrtf((contact.a[0] - position[0]) * (contact.a[0] - position[0]) +
			(contact.a[1] - position[1]) * (contact.a[1] - position[1]));
		const float db = sqrtf((contact.b[0] - position[0]) * (contact.b[0] - position[0]) +
			(contact.b[1] - position[1]) * (contact.b[1] - position[1]));
		if (da < reach) contact.runUpA = MAX(contact.runUpA, height * (1.0f - da / reach));
		if (db < reach) contact.runUpB = MAX(contact.runUpB, height * (1.0f - db / reach));
	}
}

static void R_WaterInteractionStamp(waterBody_t& body, const vec3_t position, float radius,
	float strength, float foam, const vec3_t direction, bool wake)
{
	waterInteractionState_t& sim = body.interaction;
	if (!sim.image) return;
	const float minRadius = 1.5f * MAX(sim.texelX, sim.texelY);
	radius = Com_Clamp(minRadius, 1024.0f, radius);
	strength = Com_Clamp(-8.0f, 8.0f, strength) * r_waterInteractionStrength->value * body.interactionMultiplier;
	const float cx = (position[0] - body.bounds[0][0]) / sim.texelX - 0.5f;
	const float cy = (position[1] - body.bounds[0][1]) / sim.texelY - 0.5f;
	const float stampExtent = radius * (wake ? 1.5f : 1.0f);
	const int rx = (int)ceilf(stampExtent / sim.texelX), ry = (int)ceilf(stampExtent / sim.texelY);
	vec2_t dir = { direction ? direction[0] : 0.0f, direction ? direction[1] : 0.0f };
	const float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
	if (dl > 1.0e-5f) { dir[0] /= dl; dir[1] /= dl; }
	std::vector<std::pair<size_t, float>> stamp;
	float stampSum = 0.0f;
	for (int y = MAX(0, (int)cy - ry); y <= MIN(sim.height - 1, (int)cy + ry); ++y)
		for (int x = MAX(0, (int)cx - rx); x <= MIN(sim.width - 1, (int)cx + rx); ++x)
		{
			const size_t i = (size_t)y * sim.width + x;
			if (!sim.mask[i]) continue;
			const float wx = (x - cx) * sim.texelX, wy = (y - cy) * sim.texelY;
			float d2 = (wx * wx + wy * wy) / (radius * radius);
			if (wake && dl > 1.0e-5f)
			{
				const float along = wx * dir[0] + wy * dir[1];
				const float across = -wx * dir[1] + wy * dir[0];
				d2 = across * across / (radius * radius) +
					along * along / (radius * radius * 2.25f);
			}
			if (d2 >= 1.0f) continue;
			// Compact Mexican-hat kernel: a positive impact core and a small
			// compensating trough. The discrete mean below removes the residual
			// introduced by mask clipping, so closed bodies cannot retain a DC
			// height offset after all motion has damped.
			float kernel = (1.0f - d2);
			if (wake && dl > 1.0e-5f)
			{
				// A moving hull pushes water up ahead of it.  An odd bow/stern
				// dipole cancels between the overlapping distance samples (the
				// integral of an odd kernel along the path is zero), so use a
				// forward-biased raised core; the mean correction below makes it
				// zero-mean and the wave equation radiates the trailing V.
				const float along = (wx * dir[0] + wy * dir[1]) / radius;
				kernel = kernel * kernel * Com_Clamp(0.0f, 1.4f, 0.55f + 0.45f * along);
			}
			else
			{
				kernel = kernel * kernel * (1.0f - 4.0f * d2);
			}
			if (dl > 1.0e-5f)
			{
				const float wl = sqrtf(wx * wx + wy * wy);
				if (wl > 1.0e-4f) kernel *= Com_Clamp(0.35f, 1.65f, 1.0f + 0.5f * (wx * dir[0] + wy * dir[1]) / wl);
			}
			// One-shot strength is a velocity kick. Continuous strength is an
			// acceleration integrated by the fixed step by the caller.
			const float delta = strength * 48.0f * kernel;
			stamp.push_back(std::make_pair(i, delta)); stampSum += delta;
		}
	if (!stamp.empty())
	{
		const float mean = stampSum / stamp.size();
		for (const auto& sample : stamp)
		{
			if (R_WaterInteractionGpuEnabled() && sim.gpuState[0])
			{
				const size_t base = sample.first * 4;
				const int sx = (int)(sample.first % sim.width), sy = (int)(sample.first / sim.width);
				if (!sim.gpuSourceDirty)
				{
					sim.gpuSourceMinX = sim.gpuSourceMaxX = sx;
					sim.gpuSourceMinY = sim.gpuSourceMaxY = sy;
				}
				else
				{
					sim.gpuSourceMinX = MIN(sim.gpuSourceMinX, sx);
					sim.gpuSourceMinY = MIN(sim.gpuSourceMinY, sy);
					sim.gpuSourceMaxX = MAX(sim.gpuSourceMaxX, sx);
					sim.gpuSourceMaxY = MAX(sim.gpuSourceMaxY, sy);
				}
				sim.gpuSourceData[base + 1] += sample.second - mean;
				if (foam > 0.0f)
					sim.gpuSourceData[base + 2] = Com_Clamp(0.0f, 1.0f,
						sim.gpuSourceData[base + 2] + foam * fabsf(sample.second) /
						MAX(fabsf(strength) * 48.0f, 0.001f));
				sim.gpuSourceDirty = true;
			}
			else
			{
				sim.state[sample.first * 3 + 1] += sample.second - mean;
				if (foam > 0.0f)
					sim.state[sample.first * 3 + 2] = Com_Clamp(0.0f, 1.0f,
						sim.state[sample.first * 3 + 2] + foam * fabsf(sample.second) /
						MAX(fabsf(strength) * 48.0f, 0.001f));
			}
		}
	}
	sim.active = sim.dirty = true; sim.quietTime = 0.0f;
	if (R_WaterInteractionGpuEnabled() && sim.gpuState[0])
		sim.energy = MAX(sim.energy, strength * strength);
	R_WaterShoreRunUp(body, position, radius * 3.0f + 32.0f, 0.35f * fabsf(strength) * radius);
	sim.lastSource[0] = (position[0] - body.bounds[0][0]) / MAX(body.bounds[1][0] - body.bounds[0][0], 1.0f);
	sim.lastSource[1] = (position[1] - body.bounds[0][1]) / MAX(body.bounds[1][1] - body.bounds[0][1], 1.0f);
	sim.lastSource[2] = radius / MAX(MAX(body.bounds[1][0] - body.bounds[0][0], body.bounds[1][1] - body.bounds[0][1]), 1.0f);
	sim.lastSource[3] = strength * (wake ? 0.5f : 1.0f);
	if (foam > 0.0f && r_waterFoamImpact && r_waterFoamImpact->value > 0.0f)
		R_WaterFoamInject(body, position, radius, foam * r_waterFoamImpact->value,
			WATERFOAMSOURCE_IMPACT_POOL, true);
}

/*
============================================================

Body-local spatial flow field (r_waterFlowField)

The resolved per-body flow is one uniform vector.  A real stream is slow
next to its banks and fast in the middle, turns along a wall it runs into
and stagnates where it meets one head on.  No fluid is simulated: at map
load each flowing body gets a small field built from

  - the rasterised body mask, with boundary texels classified by a short
    horizontal world trace into walls/banks (solid) and open ends (the
    liquid continues, e.g. into another brush or under a culvert);
  - an exact Euclidean distance transform to the solid boundary;
  - an open-channel lateral speed profile pow(d / delta, 0.35), rescaled
    so the mean speed over the wet area keeps the resolved body speed;
  - the approach component removed near walls (the flow turns along them)
    and a few masked smoothing passes;
  - a wall impingement term: how directly and how close the stream meets a
    solid boundary (r_waterFlowImpact).

RGBA16F texel: velocity / body speed (xy), shore distance / delta,
impingement 0..1.  A CPU copy answers R_WaterFlowForBody /
R_WaterFlowAtWorldPosition.  Uniform bodies without the field are unchanged.

============================================================
*/

static bool R_WaterFlowFieldEligible(const waterBody_t& body)
{
	if (!r_waterFlowField || !r_waterFlowField->integer || !r_waterFlow || !r_waterFlow->integer ||
		!r_waterSurface || !r_waterSurface->integer || !s_water.world)
		return false;
	if (body.waterfall || body.liquidClass == LIQUID_LAVA || body.flowSpeed <= 0.0f ||
		fabsf(body.planeNormal[2]) < 0.9f)
		return false;
	for (int surfaceNum : body.surfaces)
		if (s_water.world->surfaces[surfaceNum].waterKey & WATERKEY_INTERFACE)
			return true;
	return false;
}

// 1-D squared distance transform of a sampled function (Felzenszwalb and
// Huttenlocher), with a physical sample spacing.  Doubles: the "no seed"
// value must not swamp the squared positions.
static void R_WaterDistance1D(const double *f, double *d, int n, double spacing,
	std::vector<int>& v, std::vector<double>& z)
{
	v.resize(n); z.resize(n + 1);
	int k = 0;
	v[0] = 0; z[0] = -1.0e300; z[1] = 1.0e300;
	for (int q = 1; q < n; ++q)
	{
		const double pq = q * spacing;
		double s;
		for (;;)
		{
			const double pv = v[k] * spacing;
			s = ((f[q] + pq * pq) - (f[v[k]] + pv * pv)) / (2.0 * (pq - pv));
			if (s > z[k]) break;
			--k;
		}
		++k; v[k] = q; z[k] = s; z[k + 1] = 1.0e300;
	}
	k = 0;
	for (int q = 0; q < n; ++q)
	{
		const double pq = q * spacing;
		while (z[k + 1] < pq) ++k;
		const double delta = pq - v[k] * spacing;
		d[q] = delta * delta + f[v[k]];
	}
}

static bool R_WaterFlowFieldSolid(const vec3_t start, const vec3_t end)
{
	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	return trace.startsolid || trace.allsolid || trace.fraction < 1.0f;
}

static void R_WaterFlowFieldBuildBody(waterBody_t& body)
{
	waterFlowFieldState_t& field = body.flowField;
	field.valid = false;
	field.numSlaps = 0;
	if (!R_WaterFlowFieldEligible(body))
		return;

	const float extentX = MAX(body.bounds[1][0] - body.bounds[0][0], 1.0f);
	const float extentY = MAX(body.bounds[1][1] - body.bounds[0][1], 1.0f);
	const int w = Com_Clampi(16, 256, (int)ceilf(extentX / 8.0f));
	const int h = Com_Clampi(16, 256, (int)ceilf(extentY / 8.0f));
	if (field.image && (field.width != w || field.height != h))
		return; // a runtime rebuild keeps its allocation; bounds never change after load
	field.width = w; field.height = h;
	field.texelX = extentX / w; field.texelY = extentY / h;
	std::vector<byte> mask;
	R_WaterRasterBodyMask(body, w, h, field.texelX, field.texelY, mask);

	auto wet = [&](int x, int y) -> bool {
		return x >= 0 && y >= 0 && x < w && y < h && mask[(size_t)y * w + x];
	};
	auto surfaceZ = [&](float x, float y) -> float {
		return (body.planeDist - body.planeNormal[0] * x - body.planeNormal[1] * y) / body.planeNormal[2];
	};

	// Padded grid (one texel each side) so the outside of the bounds is a
	// boundary too.  Seeds of the distance transform are dry texels that are
	// not beyond an open end.
	const int pw = w + 2, ph = h + 2;
	std::vector<byte> open((size_t)pw * ph, 0);
	field.wallTexels = field.openTexels = 0;
	for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
	{
		if (!wet(x, y)) continue;
		vec2_t outward = { 0.0f, 0.0f };
		for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			if ((dx || dy) && !wet(x + dx, y + dy)) { outward[0] += dx; outward[1] += dy; }
		const float length = sqrtf(outward[0] * outward[0] + outward[1] * outward[1]);
		if (length < 1.0e-3f) continue;
		outward[0] /= length; outward[1] /= length;
		const float wx = body.bounds[0][0] + (x + 0.5f) * field.texelX;
		const float wy = body.bounds[0][1] + (y + 0.5f) * field.texelY;
		const float reach = MAX(field.texelX, field.texelY) + 16.0f;
		vec3_t start = { wx, wy, surfaceZ(wx, wy) - 4.0f };
		vec3_t end = { wx + outward[0] * reach, wy + outward[1] * reach, start[2] };
		if (R_WaterFlowFieldSolid(start, end)) { ++field.wallTexels; continue; }
		++field.openTexels;
		for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			if (!wet(x + dx, y + dy)) open[(size_t)(y + dy + 1) * pw + (x + dx + 1)] = 1;
	}

	// Exact Euclidean distance (world units) to the nearest solid boundary.
	const double inf = 1.0e12;
	std::vector<double> grid((size_t)pw * ph), column(MAX(pw, ph)), out(MAX(pw, ph));
	std::vector<int> v; std::vector<double> z;
	for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x)
	{
		const bool isWet = wet(x - 1, y - 1);
		grid[(size_t)y * pw + x] = (!isWet && !open[(size_t)y * pw + x]) ? 0.0f : inf;
	}
	for (int y = 0; y < ph; ++y)
	{
		R_WaterDistance1D(&grid[(size_t)y * pw], out.data(), pw, (double)field.texelX, v, z);
		for (int x = 0; x < pw; ++x) grid[(size_t)y * pw + x] = out[x];
	}
	for (int x = 0; x < pw; ++x)
	{
		for (int y = 0; y < ph; ++y) column[y] = grid[(size_t)y * pw + x];
		R_WaterDistance1D(column.data(), out.data(), ph, (double)field.texelY, v, z);
		for (int y = 0; y < ph; ++y) grid[(size_t)y * pw + x] = out[y];
	}
	std::vector<float> distance((size_t)w * h, 0.0f);
	float maxDistance = 0.0f;
	for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
	{
		if (!wet(x, y)) continue;
		// a body with no solid boundary at all (open on every side) keeps uniform flow
		const double squared = grid[(size_t)(y + 1) * pw + (x + 1)];
		const float d = squared >= inf * 0.5 ? 4096.0f : (float)sqrt(squared);
		distance[(size_t)y * w + x] = d;
		maxDistance = MAX(maxDistance, MIN(d, 4096.0f));
	}
	const float delta = Com_Clamp(16.0f, 96.0f, 0.6f * maxDistance);
	const float turn = MAX(2.0f * MAX(field.texelX, field.texelY), 0.5f * delta);
	field.delta = delta; field.maxDistance = maxDistance;

	vec2_t flowDir = { body.flowDirection[0], body.flowDirection[1] };
	const float flowLength = sqrtf(flowDir[0] * flowDir[0] + flowDir[1] * flowDir[1]);
	if (flowLength < 1.0e-4f) return;
	flowDir[0] /= flowLength; flowDir[1] /= flowLength;

	auto distanceAt = [&](int x, int y) -> float {
		return wet(x, y) ? distance[(size_t)y * w + x] : 0.0f;
	};
	std::vector<float> velocity((size_t)w * h * 2, 0.0f), impingement((size_t)w * h, 0.0f);
	for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
	{
		if (!wet(x, y)) continue;
		const size_t i = (size_t)y * w + x;
		const float d = distance[i];
		// distance gradient: points away from the nearest solid boundary
		vec2_t n = { (distanceAt(x + 1, y) - distanceAt(x - 1, y)) / (2.0f * field.texelX),
			(distanceAt(x, y + 1) - distanceAt(x, y - 1)) / (2.0f * field.texelY) };
		const float nl = sqrtf(n[0] * n[0] + n[1] * n[1]);
		if (nl > 1.0e-4f) { n[0] /= nl; n[1] /= nl; } else { n[0] = n[1] = 0.0f; }
		const float approach = MIN(flowDir[0] * n[0] + flowDir[1] * n[1], 0.0f); // < 0 runs into the boundary
		const float x01 = Com_Clamp(0.0f, 1.0f, d / turn);
		const float nearWall = 1.0f - x01 * x01 * (3.0f - 2.0f * x01);
		vec2_t dir = { flowDir[0] - nearWall * approach * n[0], flowDir[1] - nearWall * approach * n[1] };
		const float profile = MAX(powf(Com_Clamp(0.0f, 1.0f, d / delta), 0.35f), 0.05f);
		velocity[i * 2] = dir[0] * profile;
		velocity[i * 2 + 1] = dir[1] * profile;
		const float i01 = Com_Clamp(0.0f, 1.0f, d / 48.0f);
		impingement[i] = Com_Clamp(0.0f, 1.0f, -approach) * (1.0f - i01 * i01 * (3.0f - 2.0f * i01));
	}
	// masked 3x3 smoothing so the turn along a wall is gradual
	std::vector<float> smooth(velocity.size());
	for (int pass = 0; pass < 4; ++pass)
	{
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			const size_t i = (size_t)y * w + x;
			if (!wet(x, y)) { smooth[i * 2] = smooth[i * 2 + 1] = 0.0f; continue; }
			float sx = 0.0f, sy = 0.0f, weight = 0.0f;
			for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
			{
				if (!wet(x + dx, y + dy)) continue;
				const size_t j = (size_t)(y + dy) * w + (x + dx);
				const float k = (dx || dy) ? 1.0f : 2.0f;
				sx += k * velocity[j * 2]; sy += k * velocity[j * 2 + 1]; weight += k;
			}
			smooth[i * 2] = sx / weight; smooth[i * 2 + 1] = sy / weight;
		}
		velocity.swap(smooth);
	}
	// keep the mean speed (the resolved body speed) over the wet area
	double sum = 0.0; int count = 0;
	for (int i = 0; i < w * h; ++i)
		if (mask[i]) { sum += sqrtf(velocity[i * 2] * velocity[i * 2] + velocity[i * 2 + 1] * velocity[i * 2 + 1]); ++count; }
	const float mean = count ? (float)(sum / count) : 1.0f;
	field.meanScale = Com_Clamp(1.0f, 1.4f, mean > 1.0e-3f ? 1.0f / mean : 1.0f);

	field.data.assign((size_t)w * h * 4, 0.0f);
	for (int i = 0; i < w * h; ++i)
	{
		if (!mask[i]) continue;
		field.data[i * 4] = velocity[i * 2] * field.meanScale;
		field.data[i * 4 + 1] = velocity[i * 2 + 1] * field.meanScale;
		field.data[i * 4 + 2] = Com_Clamp(0.0f, 1.0f, distance[i] / delta);
		field.data[i * 4 + 3] = impingement[i];
	}

	// Up to four slap points along the strongest impingement: the wall
	// impact emits small sloshing impulses there (r_waterFlowImpact).
	std::vector<float> remaining = impingement;
	for (int s = 0; s < 4; ++s)
	{
		int best = -1; float bestValue = 0.5f;
		for (int i = 0; i < w * h; ++i) if (remaining[i] > bestValue) { bestValue = remaining[i]; best = i; }
		if (best < 0) break;
		waterFlowSlap_t& slap = field.slaps[field.numSlaps++];
		const int bx = best % w, by = best / w;
		slap.position[0] = body.bounds[0][0] + (bx + 0.5f) * field.texelX;
		slap.position[1] = body.bounds[0][1] + (by + 0.5f) * field.texelY;
		slap.position[2] = surfaceZ(slap.position[0], slap.position[1]);
		slap.strength = bestValue;
		slap.next = 0;
		slap.seed = 0x9e3779b9u * (unsigned)(body.id * 4 + s + 1);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			const float dx = (x - bx) * field.texelX, dy = (y - by) * field.texelY;
			if (dx * dx + dy * dy < 96.0f * 96.0f) remaining[(size_t)y * w + x] = 0.0f;
		}
	}

	if (!field.image)
	{
		field.image = R_CreateImage(va("*waterFlowField%d", body.id), NULL, w, h, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE, GL_RGBA16F);
		s_water.flowFieldGpuBytes += (size_t)w * h * 8;
	}
	GL_Bind(field.image);
	qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, field.data.data());
	field.valid = true;
}

static void R_WaterFlowFieldBuild(void)
{
	s_water.flowFieldGpuBytes = 0;
	int built = 0;
	const int start = ri.Milliseconds();
	for (waterBody_t& body : s_water.bodies)
	{
		body.flowField.image = nullptr;
		R_WaterFlowFieldBuildBody(body);
		if (body.flowField.valid) ++built;
	}
	if (r_waterFlowField && r_waterFlowField->integer)
		ri.Printf(PRINT_DEVELOPER, "water flow field: %d bodies, %.2f MiB GPU, %d ms\n",
			built, s_water.flowFieldGpuBytes / (1024.0f * 1024.0f), ri.Milliseconds() - start);
}

// Bilinear CPU lookup in body space; false when the body has no field.
static bool R_WaterFlowFieldSample(const waterBody_t& body, float worldX, float worldY, float out[4])
{
	const waterFlowFieldState_t& field = body.flowField;
	if (!field.valid) return false;
	const float fx = (worldX - body.bounds[0][0]) / field.texelX - 0.5f;
	const float fy = (worldY - body.bounds[0][1]) / field.texelY - 0.5f;
	const int x0 = Com_Clampi(0, field.width - 1, (int)floorf(fx));
	const int y0 = Com_Clampi(0, field.height - 1, (int)floorf(fy));
	const int x1 = MIN(x0 + 1, field.width - 1), y1 = MIN(y0 + 1, field.height - 1);
	const float tx = Com_Clamp(0.0f, 1.0f, fx - floorf(fx)), ty = Com_Clamp(0.0f, 1.0f, fy - floorf(fy));
	for (int c = 0; c < 4; ++c)
	{
		const float a = field.data[((size_t)y0 * field.width + x0) * 4 + c];
		const float b = field.data[((size_t)y0 * field.width + x1) * 4 + c];
		const float d = field.data[((size_t)y1 * field.width + x0) * 4 + c];
		const float e = field.data[((size_t)y1 * field.width + x1) * 4 + c];
		out[c] = (a + (b - a) * tx) + ((d + (e - d) * tx) - (a + (b - a) * tx)) * ty;
	}
	return true;
}

// r_waterFlowImpact: the uniforms of the field consumers and the sloshing
// impulses at the strongest wall impact points.
static float R_WaterFlowImpactHead(const waterBody_t& body)
{
	// stagnation head U^2 / 2g, capped by depth and a visual maximum
	const float speed = body.flowSpeed * Com_Clamp(0.0f, 4.0f, r_waterFlowSpeed->value);
	const float g = MAX(r_waterWaveGravity->value, 1.0f);
	const float depthCap = body.depthAverage > 0.5f ? 0.5f * body.depthAverage : 12.0f;
	return MIN(MIN(speed * speed / (2.0f * g), 12.0f), depthCap);
}

static void R_WaterFlowImpactSlaps(int now)
{
	if (!r_waterFlowImpact || !r_waterFlowImpact->integer) return;
	for (waterBody_t& body : s_water.bodies)
	{
		waterFlowFieldState_t& field = body.flowField;
		if (!field.valid) continue;
		const float head = R_WaterFlowImpactHead(body);
		for (int s = 0; s < field.numSlaps; ++s)
		{
			waterFlowSlap_t& slap = field.slaps[s];
			if (now < slap.next) continue;
			// irregular 0.25-0.9 s rhythm, strength jittered: sloshing, not a metronome
			slap.seed = slap.seed * 1664525u + 1013904223u;
			const float r0 = (float)((slap.seed >> 8) & 0xffff) / 65535.0f;
			slap.seed = slap.seed * 1664525u + 1013904223u;
			const float r1 = (float)((slap.seed >> 8) & 0xffff) / 65535.0f;
			slap.next = now + 250 + (int)(650.0f * r0);
			const float strength = Com_Clamp(0.0f, 0.6f, slap.strength * head * 0.06f * (0.6f + 0.8f * r1));
			if (strength <= 0.01f) continue;
			if (body.interaction.image)
				R_WaterInteractionStamp(body, slap.position, 20.0f + 16.0f * r1, strength,
					0.25f * slap.strength, NULL, false);
			R_WaterShoreRunUp(body, slap.position, 48.0f, head * slap.strength * (0.8f + 0.6f * r1));
		}
	}
}

static float R_WaterfallImpactRandom(waterfallImpactSource_t& source)
{
	source.randomState = source.randomState * 1664525u + 1013904223u;
	return (float)(source.randomState & 0x00ffffffu) / 16777216.0f;
}

static bool R_WaterfallImpactTargetsBody(const waterBody_t *body)
{
	if (!body || !r_waterfallImpact || !r_waterfallImpact->integer) return false;
	for (const waterfallImpactSource_t& source : s_water.waterfallImpacts)
		if (source.poolBodyId == body->id) return true;
	return false;
}

// Feed the existing shallow-water field.  The steady negative kick is the
// falling column; sparse offset kicks break its symmetry without turning the
// entire receiving brush turbulent.
static void R_WaterfallImpactInteraction(int now, float dt)
{
	if (!r_waterfallImpact || !r_waterfallImpact->integer ||
		!r_waterInteraction || !r_waterInteraction->integer) return;
	for (waterfallImpactSource_t& source : s_water.waterfallImpacts)
	{
		if (source.poolBodyId <= 0 || source.poolBodyId > (int)s_water.bodies.size()) continue;
		waterBody_t& pool = s_water.bodies[source.poolBodyId - 1];
		if (!pool.interaction.image) continue;
		const float radius = source.radius * r_waterfallImpactRadius->value;
		const float impulse = source.strength * r_waterfallImpactImpulse->value;
		if (impulse > 0.0f)
			R_WaterInteractionStamp(pool, source.origin, radius, -impulse * dt,
				0.0f, NULL, false);

		const float turbulence = source.strength * r_waterfallImpactTurbulence->value;
		if (turbulence <= 0.0f) continue;
		if (!source.nextSecondaryTime)
			source.nextSecondaryTime = now + 50 + (int)(R_WaterfallImpactRandom(source) * 120.0f);
		if (now < source.nextSecondaryTime) continue;
		const float angle = R_WaterfallImpactRandom(source) * 6.2831853f;
		const float reach = sqrtf(R_WaterfallImpactRandom(source)) * radius * 0.78f;
		vec3_t position, direction = { cosf(angle), sinf(angle), 0.0f };
		VectorMA(source.origin, reach, direction, position);
		position[2] = source.origin[2];
		const float secondaryRadius = radius * (0.16f + 0.24f * R_WaterfallImpactRandom(source));
		const float secondary = turbulence * (0.06f + 0.16f * R_WaterfallImpactRandom(source));
		R_WaterInteractionStamp(pool, position, secondaryRadius, -secondary,
			0.12f * r_waterfallImpactFoam->value, direction, false);
		source.nextSecondaryTime = now + 70 + (int)(R_WaterfallImpactRandom(source) * 180.0f);
	}
}

void RE_AddWaterImpulse(const refWaterImpulse_t *impulse)
{
	const auto start = std::chrono::high_resolution_clock::now();
	if (!impulse || ((!r_waterInteraction || !r_waterInteraction->integer) && !R_WaterFoamResourcesEnabled())) return;
	if (s_water.pendingImpulses.size() < 128) s_water.pendingImpulses.push_back(*impulse);
	s_water.interactionSubmitUsec += std::chrono::duration<double, std::micro>(
		std::chrono::high_resolution_clock::now() - start).count();
	++s_water.interactionSubmissions;
}

void RE_SetWaterSources(const refWaterSource_t *sources, int count)
{
	const auto start = std::chrono::high_resolution_clock::now();
	s_water.continuousSources.clear();
	if (!sources || count <= 0 ||
		((!r_waterInteraction || !r_waterInteraction->integer) && !R_WaterFoamResourcesEnabled())) return;
	count = Com_Clampi(0, 64, count);
	s_water.continuousSources.assign(sources, sources + count);
	s_water.interactionSubmitUsec += std::chrono::duration<double, std::micro>(
		std::chrono::high_resolution_clock::now() - start).count();
	++s_water.interactionSubmissions;
}

static void R_WaterInteractionWakes(int now)
{
	for (auto it = s_water.wakeTracks.begin(); it != s_water.wakeTracks.end(); )
	{
		if (now - it->second.lastTime > 1000) it = s_water.wakeTracks.erase(it);
		else ++it;
	}

	for (const refWaterSource_t& source : s_water.continuousSources)
	{
		waterWakeTrack_t& track = s_water.wakeTracks[source.id];
		vec3_t delta;
		VectorSubtract(source.position, track.position, delta);
		const float distance = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
		const bool discontinuity = !track.initialized || now - track.lastTime > 250 || distance > 512.0f;
		track.lastTime = now;
		if (discontinuity)
		{
			VectorCopy(source.position, track.position);
			track.carry = 0.0f;
			track.initialized = true;
			continue;
		}

		const float speed = sqrtf(source.velocity[0] * source.velocity[0] +
			source.velocity[1] * source.velocity[1]);
		const float baseRadius = MAX(source.radius, source.width * 0.5f);
		const float narrow = Com_Clamp(0.55f, 1.15f, 1.15f - speed / 900.0f);
		const float radius = baseRadius * narrow;
		const float spacing = Com_Clamp(6.0f, 64.0f, radius * 0.65f);
		if (source.strength > 0.0f && distance > 1.0e-4f)
		{
			const float first = spacing - track.carry;
			int samples = first <= distance ? 1 + (int)floorf((distance - first) / spacing) : 0;
			samples = MIN(samples, 24);
			for (int n = 0; n < samples; ++n)
			{
				const float travelled = first + n * spacing;
				const float f = Com_Clamp(0.0f, 1.0f, travelled / distance);
				vec3_t position;
				VectorMA(track.position, f, delta, position);
				waterBody_t *body = R_WaterInteractionBodyAt(position);
				if (!body) continue;
				const float surface = R_WaterInteractionSurfaceHeight(*body, position);
				const float bottom = position[2] + source.boundsMinZ;
				const float top = position[2] + source.boundsMaxZ;
				// Only a body crossing the displaced waterline makes a surface wake:
				// fully submerged and airborne objects are deliberately excluded.
				if (surface < bottom - 2.0f || surface > top + 2.0f) continue;
				const float immersion = Com_Clamp(0.0f, 1.0f,
					(surface - bottom) / MAX(top - bottom, 1.0f));
				const float speedScale = Com_Clamp(0.45f, 2.5f, speed / 160.0f);
				// Stamps now superpose along the path: normalise by the number of
				// overlapping samples (2 radius / spacing) and apply the profile's
				// wake response (still pools damp, fast streams carry more).
				const float overlap = Com_Clamp(0.15f, 1.0f, spacing / (2.0f * radius));
				position[2] = surface;
				R_WaterInteractionStamp(*body, position, radius,
					source.strength * (0.35f + 0.65f * sqrtf(immersion)) * speedScale *
					overlap * 1.4f * s_dynamics[body->dynamics].wake,
					source.foam * immersion, source.direction, true);
			}
			const int uncappedSamples = first <= distance ? 1 + (int)floorf((distance - first) / spacing) : 0;
			track.carry = track.carry + distance - uncappedSamples * spacing;
			if (track.carry < 0.0f) track.carry = 0.0f;
			if (track.carry >= spacing) track.carry = fmodf(track.carry, spacing);
		}
		else
		{
			track.carry = 0.0f;
		}
		VectorCopy(source.position, track.position);
	}
}

// CPU mirror of AdvectState in water_interaction_field.glsl: Catmull-Rom over
// 4x4 wet cells at the departure point, clamped to the inner 2x2 range.
// Channels: height, velocity, transient foam, intersection foam.
static bool R_WaterInteractionAdvect(const waterInteractionState_t& sim, float px, float py, float out[4])
{
	const float bx = floorf(px), by = floorf(py);
	const float tx = px - bx, ty = py - by;
	auto weights = [](float t, float w[4]) {
		w[0] = t * (-0.5f + t * (1.0f - 0.5f * t));
		w[1] = 1.0f + t * t * (-2.5f + 1.5f * t);
		w[2] = t * (0.5f + t * (2.0f - 1.5f * t));
		w[3] = t * t * (-0.5f + 0.5f * t);
	};
	float wx[4], wy[4]; weights(tx, wx); weights(ty, wy);
	float sum[4] = {}, lo[4], hi[4];
	for (int c = 0; c < 4; ++c) { lo[c] = 1.0e30f; hi[c] = -1.0e30f; }
	for (int j = 0; j < 4; ++j)
	{
		float row[4] = {};
		for (int i = 0; i < 4; ++i)
		{
			const int x = (int)bx + i - 1, y = (int)by + j - 1;
			if (x < 0 || y < 0 || x >= sim.width || y >= sim.height) return false;
			const size_t cell = (size_t)y * sim.width + x;
			if (!sim.mask[cell]) return false;
			const float value[4] = { sim.state[cell * 3], sim.state[cell * 3 + 1], sim.state[cell * 3 + 2], sim.intersectionFoam[cell] };
			for (int c = 0; c < 4; ++c)
			{
				row[c] += wx[i] * value[c];
				if ((i == 1 || i == 2) && (j == 1 || j == 2)) { lo[c] = MIN(lo[c], value[c]); hi[c] = MAX(hi[c], value[c]); }
			}
		}
		for (int c = 0; c < 4; ++c) sum[c] += wy[j] * row[c];
	}
	for (int c = 0; c < 4; ++c) out[c] = Com_Clamp(lo[c], hi[c], sum[c]);
	return true;
}

static void R_WaterInteractionStepBody(waterBody_t& body, float dt)
{
	waterInteractionState_t& sim = body.interaction;
	if (!sim.active) return;
	const float profileSpeed = 32.0f + s_dynamics[body.dynamics].speed * 32.0f;
	// CFL guard is part of the physical contract: extreme cvar/profile values
	// cannot destabilise the explicit five-point stencil.
	const float cLimit = 0.68f / (dt * sqrtf(1.0f / (sim.texelX * sim.texelX) + 1.0f / (sim.texelY * sim.texelY)));
	const float c = MIN(profileSpeed * r_waterInteractionSpeed->value, cLimit);
	const bool variableDepth = r_waterInteractionVariableDepth->integer &&
		!sim.bathymetry.empty() && sim.bathymetryMaxDepth > 0.0f;
	const float fallbackDepth = body.depthAverage > 0.5f ? body.depthAverage :
		MAX(c * c / MAX(r_waterWaveGravity->value, 1.0f), 0.5f);
	const float effectiveGravity = variableDepth ? MIN(r_waterWaveGravity->value,
		(cLimit * cLimit) / MAX(MAX(sim.bathymetryMaxDepth, fallbackDepth), 0.5f)) : 0.0f;
	const float damping = 0.32f * s_dynamics[body.dynamics].damping * r_waterInteractionDamping->value;
	float bodyFlowX = r_waterFlow->integer ? body.flowVelocity[0] * r_waterFlowSpeed->value : 0.0f;
	float bodyFlowY = r_waterFlow->integer ? body.flowVelocity[1] * r_waterFlowSpeed->value : 0.0f;
	// The wave-equation CFL limit above does not protect the following upwind
	// advection.  Preserve direction while keeping its 2-D Courant number below
	// one; extreme flow CVars then slow down instead of saturating the height
	// clamp and poisoning the field.
	const float flowCourant = dt * (fabsf(bodyFlowX) / sim.texelX + fabsf(bodyFlowY) / sim.texelY);
	if (flowCourant > 0.95f)
	{
		const float scale = 0.95f / flowCourant;
		bodyFlowX *= scale; bodyFlowY *= scale;
	}
	const bool flowField = r_waterFlow->integer && body.flowField.valid;
	const float fieldSpeed = body.flowSpeed * r_waterFlowSpeed->value;
	double energy = 0.0; int wet = 0;
	for (size_t wetIndex = 0; wetIndex < sim.wetCells.size(); ++wetIndex)
	{
		const size_t i = (size_t)sim.wetCells[wetIndex];
		// spatial flow (r_waterFlowField): the same per-cell rule as the GPU pass
		float flowX = bodyFlowX, flowY = bodyFlowY;
		float fieldSample[4];
		if (flowField && R_WaterFlowFieldSample(body,
			body.bounds[0][0] + ((int)(i % sim.width) + 0.5f) * sim.texelX,
			body.bounds[0][1] + ((int)(i / sim.width) + 0.5f) * sim.texelY, fieldSample))
		{
			flowX = fieldSample[0] * fieldSpeed; flowY = fieldSample[1] * fieldSpeed;
			const float courant = dt * (fabsf(flowX) / sim.texelX + fabsf(flowY) / sim.texelY);
			if (courant > 0.95f) { flowX *= 0.95f / courant; flowY *= 0.95f / courant; }
		}
		const std::array<int, 4>& neighbours = sim.wetNeighbours[wetIndex];
		const float hc = sim.state[i * 3], vc = sim.state[i * 3 + 1];
		const float hl = sim.state[(size_t)neighbours[0] * 3];
		const float hr = sim.state[(size_t)neighbours[1] * 3];
		const float hd = sim.state[(size_t)neighbours[2] * 3];
		const float hu = sim.state[(size_t)neighbours[3] * 3];
		float acceleration;
		if (variableDepth)
		{
			auto depth = [&](size_t cell) -> float {
				const float *sample = &sim.bathymetry[cell * 4];
				return sample[1] >= 0.75f ? MAX(sample[0], 0.5f) : fallbackDepth;
			};
			const float hcDepth = depth(i);
			const float hLeft = 0.5f * (hcDepth + depth((size_t)neighbours[0]));
			const float hRight = 0.5f * (hcDepth + depth((size_t)neighbours[1]));
			const float hDown = 0.5f * (hcDepth + depth((size_t)neighbours[2]));
			const float hUp = 0.5f * (hcDepth + depth((size_t)neighbours[3]));
			acceleration = effectiveGravity *
				((hRight * (hr - hc) - hLeft * (hc - hl)) / (sim.texelX * sim.texelX) +
				 (hUp * (hu - hc) - hDown * (hc - hd)) / (sim.texelY * sim.texelY));
		}
		else
		{
			const float lap = (hl - 2.0f * hc + hr) / (sim.texelX * sim.texelX) +
				(hd - 2.0f * hc + hu) / (sim.texelY * sim.texelY);
			acceleration = c * c * lap;
		}
		// Transport by the flow first (semi-Lagrangian Catmull-Rom, the same
		// rule as water_interaction_field.glsl), then integrate the local wave
		// equation on the transported cell.  Upwind is the bank fallback.
		const float fc0 = sim.state[i * 3 + 2], ic0 = sim.intersectionFoam[i];
		float transported[4] = { hc, vc, fc0, ic0 };
		const bool flowing = fabsf(flowX) + fabsf(flowY) > 1.0e-4f;
		if (flowing && !R_WaterInteractionAdvect(sim, (int)(i % sim.width) - dt * flowX / sim.texelX,
			(int)(i / sim.width) - dt * flowY / sim.texelY, transported))
		{
			for (int channel = 0; channel < 4; ++channel)
			{
				auto value = [&](int cell) -> float {
					return channel < 3 ? sim.state[(size_t)cell * 3 + channel] : sim.intersectionFoam[cell];
				};
				const float cc = value((int)i);
				const float gx = flowX >= 0.0f ? (cc - value(neighbours[0])) / sim.texelX : (value(neighbours[1]) - cc) / sim.texelX;
				const float gy = flowY >= 0.0f ? (cc - value(neighbours[2])) / sim.texelY : (value(neighbours[3]) - cc) / sim.texelY;
				transported[channel] = cc - dt * (flowX * gx + flowY * gy);
			}
		}
		float v = transported[1] + dt * (acceleration - 2.0f * damping * transported[1]);
		float nh = transported[0] + dt * v;
		v = Com_Clamp(-512.0f, 512.0f, v); nh = Com_Clamp(-64.0f, 64.0f, nh);
		// Foam is a short-lived surface tracer: transported, softened and decayed.
		const float fl = sim.state[(size_t)neighbours[0] * 3 + 2];
		const float fr = sim.state[(size_t)neighbours[1] * 3 + 2];
		const float fd = sim.state[(size_t)neighbours[2] * 3 + 2];
		const float fu = sim.state[(size_t)neighbours[3] * 3 + 2];
		const float diffusion = 0.3f * ((fl - 2.0f * fc0 + fr) + (fd - 2.0f * fc0 + fu));
		float nf = Com_Clamp(0.0f, 1.0f, (transported[2] + dt * diffusion) * expf(-dt * 0.85f));
		const float il = sim.intersectionFoam[neighbours[0]], ir = sim.intersectionFoam[neighbours[1]];
		const float id = sim.intersectionFoam[neighbours[2]], iu = sim.intersectionFoam[neighbours[3]];
		const float iDiffusion = 0.3f * ((il - 2.0f * ic0 + ir) + (id - 2.0f * ic0 + iu));
		const float persistence = MAX(r_waterIntersectionFoamPersistence->value, 0.001f);
		sim.intersectionNext[i] = Com_Clamp(0.0f, 1.0f,
			(transported[3] + dt * iDiffusion) * expf(-dt / persistence));
		if (r_waterShoreline->integer && r_waterShoreFoam->value > 0.0f)
		{
			const bool boundary = neighbours[0] == (int)i || neighbours[1] == (int)i ||
				neighbours[2] == (int)i || neighbours[3] == (int)i;
			if (boundary)
			{
				// Only a disturbance breaking on the edge foams it; steady flow
				// along a bank is not agitation.  Saturating rate, not a floor.
				const float agitation = Com_Clamp(0.0f, 1.0f, (fabsf(v) - 40.0f) / 120.0f);
				const float source = s_dynamics[body.dynamics].foam * body.foamMultiplier *
					r_waterShoreFoam->value * agitation;
				nf = Com_Clamp(0.0f, 1.0f, nf + source * 4.0f * dt * (1.0f - nf));
			}
		}
		sim.next[i * 3] = nh; sim.next[i * 3 + 1] = v; sim.next[i * 3 + 2] = nf;
		energy += nh * nh + (v * v) / MAX(c * c, 1.0f); ++wet;
	}
	sim.state.swap(sim.next);
	sim.intersectionFoam.swap(sim.intersectionNext);
	sim.energy = wet ? (float)(energy / wet) : 0.0f;
	float foamPeak = 0.0f;
	for (int cell : sim.wetCells)
	{
		const size_t i = (size_t)cell;
		foamPeak = MAX(foamPeak, MAX(sim.state[i * 3 + 2], sim.intersectionFoam[i]));
	}
	if (sim.energy < 1.0e-5f && foamPeak < 1.0e-3f) sim.quietTime += dt; else sim.quietTime = 0.0f;
	if (sim.quietTime > 1.0f)
	{
		std::fill(sim.state.begin(), sim.state.end(), 0.0f);
		std::fill(sim.intersectionFoam.begin(), sim.intersectionFoam.end(), 0.0f);
		sim.active = false; sim.energy = 0.0f;
	}
	sim.dirty = true;
}

static void RB_WaterInteractionGpuStepBody(waterBody_t& body, float dt)
{
	waterInteractionState_t& sim = body.interaction;
	if (!sim.active || !sim.gpuState[0] || !sim.gpuSource) return;
	// the GL 4.3 depth contacts are a source of their own (sim.gpuIntersection)
	const bool applyContacts = sim.intersectionFresh && sim.gpuIntersection;
	const bool applySource = sim.gpuSourceDirty || applyContacts;
	if (sim.gpuSourceDirty)
	{
		GL_BindToTMU(sim.gpuSource, TB_NORMALMAP);
		const int sourceWidth = sim.gpuSourceMaxX - sim.gpuSourceMinX + 1;
		const int sourceHeight = sim.gpuSourceMaxY - sim.gpuSourceMinY + 1;
		qglPixelStorei(GL_UNPACK_ROW_LENGTH, sim.width);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.gpuSourceMinX, sim.gpuSourceMinY,
			sourceWidth, sourceHeight, GL_RGBA, GL_FLOAT,
			&sim.gpuSourceData[((size_t)sim.gpuSourceMinY * sim.width + sim.gpuSourceMinX) * 4]);
		qglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	const float profileSpeed = 32.0f + s_dynamics[body.dynamics].speed * 32.0f;
	const float cLimit = 0.68f / (dt * sqrtf(1.0f / (sim.texelX * sim.texelX) +
		1.0f / (sim.texelY * sim.texelY)));
	const float c = MIN(profileSpeed * r_waterInteractionSpeed->value, cLimit);
	const float fallbackDepth = body.depthAverage > 0.5f ? body.depthAverage :
		MAX(c * c / MAX(r_waterWaveGravity->value, 1.0f), 0.5f);
	const float effectiveGravity = MIN(r_waterWaveGravity->value,
		(cLimit * cLimit) / MAX(MAX(sim.bathymetryMaxDepth, fallbackDepth), 0.5f));
	const float damping = 0.32f * s_dynamics[body.dynamics].damping * r_waterInteractionDamping->value;
	float flowX = r_waterFlow->integer ? body.flowVelocity[0] * r_waterFlowSpeed->value : 0.0f;
	float flowY = r_waterFlow->integer ? body.flowVelocity[1] * r_waterFlowSpeed->value : 0.0f;
	const float flowCourant = dt * (fabsf(flowX) / sim.texelX + fabsf(flowY) / sim.texelY);
	if (flowCourant > 0.95f) { const float scale = 0.95f / flowCourant; flowX *= scale; flowY *= scale; }
	const float shore = r_waterShoreline->integer && r_waterShoreFoam->value > 0.0f ?
		s_dynamics[body.dynamics].foam * body.foamMultiplier * r_waterShoreFoam->value : 0.0f;
	const int target = sim.gpuCurrent ^ 1;
	FBO_Bind(tr.waterInteractionFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		sim.gpuState[target]->texnum, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	GL_SetViewportAndScissor(0, 0, sim.width, sim.height);
	GL_BindToTMU(sim.gpuState[sim.gpuCurrent], TB_COLORMAP);
	GL_BindToTMU(sim.gpuSource, TB_NORMALMAP);
	GL_BindToTMU(sim.bathymetryImage ? sim.bathymetryImage : tr.whiteImage, TB_DELUXEMAP);
	const bool flowField = r_waterFlow->integer && body.flowField.valid;
	GL_BindToTMU(flowField ? body.flowField.image : tr.whiteImage, TB_SPECULARMAP);
	vec4_t fieldParams = { flowField ? 1.0f : 0.0f, body.flowSpeed * r_waterFlowSpeed->value, 0.0f, 0.0f };
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERFLOWFIELDPARAMS, fieldParams);
	vec4_t sim0 = { dt, c, damping, -shore };
	vec4_t sim1 = { sim.texelX, sim.texelY, flowX, flowY };
	vec4_t domain = { 0.0f, 0.0f, MAX(r_waterIntersectionFoamPersistence->value, 0.001f), applySource ? 1.0f : 0.0f };
	vec4_t bathymetry = { r_waterInteractionVariableDepth->integer && sim.bathymetryImage ? 1.0f : 0.0f,
		fallbackDepth, effectiveGravity, applyContacts ? 1.0f : 0.0f };
	// not sampled without the contact flag (u_WaterBathymetryParams.w)
	GL_BindToTMU(applyContacts ? sim.gpuIntersection : tr.whiteImage, TB_SHADOWMAP);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONSIM0, sim0);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONSIM1, sim1);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONDOMAIN, domain);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERBATHYMETRYPARAMS, bathymetry);
	RB_InstantTriangle();
	sim.gpuCurrent = target;
	sim.intersectionFresh = false;
	if (sim.gpuSourceDirty)
	{
		for (int y = sim.gpuSourceMinY; y <= sim.gpuSourceMaxY; ++y)
			std::fill(sim.gpuSourceData.begin() + ((size_t)y * sim.width + sim.gpuSourceMinX) * 4,
				sim.gpuSourceData.begin() + ((size_t)y * sim.width + sim.gpuSourceMaxX + 1) * 4, 0.0f);
		// The draw above has consumed the source. Clear exactly the same small
		// texture rectangle so a later, unrelated source cannot reapply it.
		GL_BindToTMU(sim.gpuSource, TB_NORMALMAP);
		qglPixelStorei(GL_UNPACK_ROW_LENGTH, sim.width);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.gpuSourceMinX, sim.gpuSourceMinY,
			sim.gpuSourceMaxX - sim.gpuSourceMinX + 1,
			sim.gpuSourceMaxY - sim.gpuSourceMinY + 1, GL_RGBA, GL_FLOAT,
			&sim.gpuSourceData[((size_t)sim.gpuSourceMinY * sim.width + sim.gpuSourceMinX) * 4]);
		qglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		sim.gpuSourceDirty = false;
		sim.quietTime = 0.0f;
	}
	else sim.quietTime += dt;
	sim.energy *= expf(-2.0f * damping * dt);
	sim.dirty = true;
}

static void RB_WaterInteractionGpuResolveBody(waterBody_t& body)
{
	waterInteractionState_t& sim = body.interaction;
	if (!sim.dirty || !sim.gpuState[0]) return;
	FBO_Bind(tr.waterInteractionFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		s_water.interactionAtlas->texnum, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	GL_SetViewportAndScissor(sim.atlasX, sim.atlasY, sim.width, sim.height);
	GL_BindToTMU(sim.gpuState[sim.gpuCurrent], TB_COLORMAP);
	GL_BindToTMU(sim.gpuSource, TB_NORMALMAP);
	GL_BindToTMU(sim.bathymetryImage ? sim.bathymetryImage : tr.whiteImage, TB_DELUXEMAP);
	vec4_t sim0 = { 0.0f, 0.0f, 0.0f, 1.0f };
	vec4_t sim1 = { sim.texelX, sim.texelY, 0.0f, 0.0f };
	vec4_t noField = { 0.0f, 0.0f, 0.0f, 0.0f };
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERFLOWFIELDPARAMS, noField);
	vec4_t domain = { (float)sim.atlasX, (float)sim.atlasY, 1.0f, 0.0f };
	vec4_t bathymetry = { 0.0f, MAX(body.depthAverage, 0.5f), r_waterWaveGravity->value, 0.0f };
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONSIM0, sim0);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONSIM1, sim1);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERINTERACTIONDOMAIN, domain);
	GLSL_SetUniformVec4(&tr.waterInteractionFieldShader, UNIFORM_WATERBATHYMETRYPARAMS, bathymetry);
	RB_InstantTriangle();
	sim.dirty = false;
	sim.resolvedForMotionHistory = true;
}

static void RB_WaterInteractionUpdate(void)
{
	if (!r_waterInteraction->integer || s_water.interactionFrame == backEndData->realFrameNumber) return;
	s_water.interactionFrame = backEndData->realFrameNumber;
	const auto sourceStart = std::chrono::high_resolution_clock::now();
	const int now = backEnd.refdef.time;
	float elapsed = s_water.interactionTime < 0 ? 0.0f : (now - s_water.interactionTime) * 0.001f;
	s_water.interactionTime = now;
	elapsed = Com_Clamp(0.0f, 0.25f, elapsed);
	s_water.interactionAccumulator = MIN(s_water.interactionAccumulator + elapsed, 4.0f / 60.0f);
	// GL 4.3 depth contacts of earlier frames wake their bodies (never waits)
	RB_WaterIntersectionPollHits();

	for (const refWaterImpulse_t& impulse : s_water.pendingImpulses)
	{
		waterBody_t *body = nullptr;
		if (impulse.bodyId > 0 && impulse.bodyId <= (int)s_water.bodies.size())
		{
			waterBody_t& hinted = s_water.bodies[impulse.bodyId - 1];
			if (hinted.interaction.image) body = &hinted;
		}
		if (!body) body = R_WaterInteractionBodyAt(impulse.position);
		const float splashStrength = impulse.energy > 0.0f ? r_waterSplashStrength->value : 1.0f;
		const float splashFoam = impulse.energy > 0.0f ? r_waterSplashFoam->value : 1.0f;
		if (body) R_WaterInteractionStamp(*body, impulse.position, impulse.radius, impulse.strength * splashStrength,
			impulse.foam * splashFoam,
			(impulse.flags & WATERINTERACT_DIRECTIONAL) ? impulse.direction : NULL, false);
		if (r_waterSplashDebug && r_waterSplashDebug->integer && impulse.energy > 0.0f)
			ri.Printf(PRINT_ALL, "water splash: type %d pos %.1f %.1f %.1f radius %.1f velocity %.1f %.1f %.1f body %d energy %.3f impulse %.3f spray %d foam %.3f\n",
				impulse.type, impulse.position[0], impulse.position[1], impulse.position[2], impulse.radius,
				impulse.velocity[0], impulse.velocity[1], impulse.velocity[2], body ? body->id : 0,
				impulse.energy, impulse.strength, impulse.sprayCount, impulse.foam);
	}
	s_water.pendingImpulses.clear();
	R_WaterInteractionWakes(now);
	const auto stepStart = std::chrono::high_resolution_clock::now();

	const float fixedDt = 1.0f / 60.0f;
	const bool gpu = R_WaterInteractionGpuEnabled();
	FBO_t *oldFbo = glState.currentFBO;
	int gpuTimer = -1;
	if (gpu)
	{
		gpuTimer = RB_ScreenBeginTimer("Water interaction fields");
		GL_State(GLS_DEPTHTEST_DISABLE);
		GL_Cull(CT_TWO_SIDED);
		GLSL_BindProgram(&tr.waterInteractionFieldShader);
	}
	int steps = 0;
	while (s_water.interactionAccumulator + 1.0e-6f >= fixedDt && steps < 4)
	{
		R_WaterfallImpactInteraction(now, fixedDt);
		for (waterBody_t& body : s_water.bodies)
			if (gpu && body.interaction.gpuState[0]) RB_WaterInteractionGpuStepBody(body, fixedDt);
			else R_WaterInteractionStepBody(body, fixedDt);
		s_water.interactionAccumulator -= fixedDt; ++steps;
	}
	s_water.interactionSteps = steps;
	const auto uploadStart = std::chrono::high_resolution_clock::now();
	for (waterBody_t& body : s_water.bodies)
	{
		waterInteractionState_t& sim = body.interaction;
		if (!sim.image || !sim.dirty) continue;
		if (gpu && sim.gpuState[0])
		{
			// Without a GPU readback, use a conservative finite lifetime. A rare
			// full clear on sleep is far cheaper than uploading every active frame.
			if (sim.quietTime > 12.0f / MAX(r_waterInteractionDamping->value, 0.25f))
			{
				for (int state = 0; state < 2; ++state)
				{
					GL_BindToTMU(sim.gpuState[state], TB_COLORMAP);
					qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sim.width, sim.height,
						GL_RGBA, GL_FLOAT, sim.gpuInitial.data());
				}
				GL_BindToTMU(s_water.interactionAtlas, TB_COLORMAP);
				qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.atlasX, sim.atlasY, sim.width, sim.height,
					GL_RGBA, GL_FLOAT, sim.upload.data());
				sim.active = sim.dirty = false; sim.energy = sim.quietTime = 0.0f;
				sim.resolvedForMotionHistory = true;
			}
			else RB_WaterInteractionGpuResolveBody(body);
			continue;
		}
		for (size_t wetIndex = 0; wetIndex < sim.wetCells.size(); ++wetIndex)
		{
			const size_t i = (size_t)sim.wetCells[wetIndex];
			const std::array<int, 4>& neighbours = sim.wetNeighbours[wetIndex];
			const float hl = sim.state[(size_t)neighbours[0] * 3];
			const float hr = sim.state[(size_t)neighbours[1] * 3];
			const float hd = sim.state[(size_t)neighbours[2] * 3];
			const float hu = sim.state[(size_t)neighbours[3] * 3];
			sim.upload[i * 4] = sim.state[i * 3];
			sim.upload[i * 4 + 1] = (hr - hl) / (2.0f * sim.texelX);
			sim.upload[i * 4 + 2] = (hu - hd) / (2.0f * sim.texelY);
			sim.upload[i * 4 + 3] = 1.0f + Com_Clamp(0.0f, 1.0f,
				sim.state[i * 3 + 2] + sim.intersectionFoam[i]);
		}
		GL_BindToTMU(s_water.interactionAtlas, 19);
		GL_SelectTexture(19); // See allocation upload above.
		qglTexSubImage2D(GL_TEXTURE_2D, 0, sim.atlasX, sim.atlasY, sim.width, sim.height,
			GL_RGBA, GL_FLOAT, sim.upload.data());
		sim.dirty = false;
		sim.resolvedForMotionHistory = true;
	}
	if (gpu)
	{
		RB_ScreenEndTimer(gpuTimer);
		FBO_Bind(oldFbo);
		GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
			backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
		GLSL_BindNullProgram();
	}
	const auto end = std::chrono::high_resolution_clock::now();
	s_water.interactionSourceMsec = (float)std::chrono::duration<double, std::milli>(stepStart - sourceStart).count();
	s_water.interactionStepMsec = (float)std::chrono::duration<double, std::milli>(uploadStart - stepStart).count();
	s_water.interactionUploadMsec = (float)std::chrono::duration<double, std::milli>(end - uploadStart).count();
}

static void RB_WaterFoamUpdate(void)
{
	if (!R_WaterFoamResourcesEnabled() || !tr.waterFoamFbo ||
		!tr.waterFoamFieldShader.program || s_water.foamFrame == backEndData->realFrameNumber)
		return;
	s_water.foamFrame = backEndData->realFrameNumber;
	const int now = backEnd.refdef.time;
	float dt = s_water.foamTime < 0 ? 0.0f : (now - s_water.foamTime) * 0.001f;
	s_water.foamTime = now;
	dt = Com_Clamp(0.0f, 0.1f, dt);
	if (dt <= 0.0f) return;
	if (!r_waterInteraction || !r_waterInteraction->integer)
	{
		for (const refWaterImpulse_t& impulse : s_water.pendingImpulses)
		{
			waterBody_t *body = R_WaterFoamBodyAt(impulse.position);
			if (body) R_WaterFoamInject(*body, impulse.position, impulse.radius,
				impulse.foam * r_waterFoamImpact->value, WATERFOAMSOURCE_IMPACT_POOL, true);
		}
		s_water.pendingImpulses.clear();
	}
	if (r_waterfallImpact && r_waterfallImpact->integer && r_waterfallImpactFoam->value > 0.0f)
	{
		for (const waterfallImpactSource_t& source : s_water.waterfallImpacts)
		{
			if (source.poolBodyId <= 0 || source.poolBodyId > (int)s_water.bodies.size()) continue;
			waterBody_t& pool = s_water.bodies[source.poolBodyId - 1];
			if (!R_WaterFoamAllocate(pool)) continue;
			R_WaterFoamInject(pool, source.origin,
				source.radius * r_waterfallImpactRadius->value,
				0.9f * source.strength * r_waterfallImpactFoam->value,
				WATERFOAMSOURCE_WATERFALL);
		}
	}

	// Authored/API local sources are continuous. Ordinary type-zero sources
	// remain distance-sampled wakes and enter through R_WaterInteractionStamp.
	for (const refWaterSource_t& source : s_water.continuousSources)
	{
		if (source.type <= WATERFOAMSOURCE_WAKE || source.type > WATERFOAMSOURCE_WAVE_STEEPNESS)
			continue;
		waterBody_t *body = R_WaterFoamBodyAt(source.position);
		if (!body) continue;
		float scale = 1.0f;
		if (source.type == WATERFOAMSOURCE_WATERFALL) scale = r_waterFoamWaterfall->value;
		else if (source.type == WATERFOAMSOURCE_INTERSECTION) scale = r_waterFoamIntersection->value;
		else if (source.type == WATERFOAMSOURCE_IMPACT_POOL) scale = r_waterFoamImpact->value;
		else if (source.type == WATERFOAMSOURCE_SHORELINE) scale = r_waterFoamShoreline->value;
		else if (source.type == WATERFOAMSOURCE_WAVE_STEEPNESS) scale = r_waterFoamSteepness->value;
		R_WaterFoamInject(*body, source.position, source.radius, source.strength * scale, source.type);
	}

	FBO_t *oldFbo = glState.currentFBO;
	const int timer = RB_ScreenBeginTimer("Water foam fields");
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(&tr.waterFoamFieldShader);
	for (waterBody_t& body : s_water.bodies)
	{
		waterFoamState_t& foam = body.foam;
		if (!foam.active)
		{
			const bool turbulentCandidate = body.dynamics == 5 || body.dynamics == 7 || body.dynamics == 8;
			const bool interactionCandidate = body.interaction.image &&
				(r_waterFoamImpact->value > 0.0f || r_waterFoamIntersection->value > 0.0f);
			const bool enabledSource = (turbulentCandidate && (r_waterFoamWaterfall->value > 0.0f || r_waterWhitewater->integer)) ||
				interactionCandidate || (r_waterShoreline->integer && r_waterFoamShoreline->value > 0.0f) ||
				r_waterFoamSteepness->value > 0.0f || (r_waterWhitewater->integer && body.interaction.image) ||
				(r_waterFlowImpact->integer && body.flowField.valid);
			if (enabledSource) R_WaterFoamAllocate(body);
		}
		if (!foam.active) continue;
		const int w = foam.width, h = foam.height;
		waterInteractionState_t& interaction = body.interaction;
		// Depth-projected object intersections are resampled only on frames that
		// produced any (the CPU fallback keeps its decaying tracer every frame).
		const bool cpuInteraction = interaction.image && !(R_WaterInteractionGpuEnabled() && interaction.gpuState[0]);
		if (interaction.image && r_waterFoamIntersection->value > 0.0f &&
			(interaction.intersectionPending || cpuInteraction))
		{
			interaction.intersectionPending = false;
			for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
			{
				const size_t i = (size_t)y * w + x;
				if (!foam.mask[i]) continue;
				const int ix = Com_Clampi(0, interaction.width - 1, x * interaction.width / w);
				const int iy = Com_Clampi(0, interaction.height - 1, y * interaction.height / h);
				const float v = interaction.intersectionFoam[(size_t)iy * interaction.width + ix] * r_waterFoamIntersection->value;
				if (v <= 0.004f) continue;
				byte *source = &foam.source[i * 4];
				source[1] = (byte)MAX((int)source[1], (int)(255.0f * Com_Clamp(0.0f, 1.0f, v)));
				foam.sourceDirty = true;
			}
		}
		if (foam.sourceDirty || foam.sourceInTexture)
		{
			GL_BindToTMU(foam.sourceImage, TB_WATERFOAMSOURCE);
			GL_SelectTexture(TB_WATERFOAMSOURCE);
			qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, foam.source.data());
			foam.sourceInTexture = foam.sourceDirty;
		}
		else
			GL_BindToTMU(foam.sourceImage, TB_WATERFOAMSOURCE);
		// Constant and GPU-evaluated sources.  Only authored waterfall profiles
		// seed surface foam everywhere; banks foam where interaction waves break
		// on them, crests where they steepen, walls where the flow hits them.
		const bool turbulent = body.dynamics == 7 || body.dynamics == 8;
		const bool atlas = interaction.image && s_water.interactionAtlas && r_waterInteraction->integer;
		const bool impact = r_waterFlowImpact && r_waterFlowImpact->integer && body.flowField.valid && r_waterFlow->integer;
		vec4_t sources = {
			turbulent && r_waterFoamWaterfall->value > 0.0f ? Com_Clamp(0.0f, 1.0f,
				0.22f * s_dynamics[body.dynamics].foam * body.foamMultiplier * r_waterFoamWaterfall->value) : 0.0f,
			atlas && r_waterShoreline->integer ? MAX(r_waterFoamShoreline->value, 0.0f) *
				Com_Clamp(0.0f, 1.5f, s_dynamics[body.dynamics].shoreline) * body.foamMultiplier : 0.0f,
			atlas ? MAX(r_waterFoamSteepness->value, 0.0f) * body.foamMultiplier : 0.0f,
			impact ? Com_Clamp(0.0f, 2.0f, R_WaterFlowImpactHead(body) / 6.0f) * body.foamMultiplier : 0.0f };
		vec4_t rect = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (atlas)
			VectorSet4(rect, (interaction.atlasX + 0.5f) / s_water.interactionAtlasWidth,
				(interaction.atlasY + 0.5f) / s_water.interactionAtlasHeight,
				(interaction.width - 1.0f) / s_water.interactionAtlasWidth,
				(interaction.height - 1.0f) / s_water.interactionAtlasHeight);
		GL_BindToTMU(atlas ? s_water.interactionAtlas : tr.whiteImage, TB_WATERRAIN);
		const bool flowField = r_waterFlow->integer && body.flowField.valid;
		GL_BindToTMU(flowField ? body.flowField.image : tr.whiteImage, 21);
		const float bodySpeed = body.flowSpeed * (r_waterFlow->integer ? r_waterFlowSpeed->value : 0.0f);
		vec4_t fieldParams = { flowField ? 1.0f : 0.0f, bodySpeed,
			1.0f / MAX(body.bounds[1][0] - body.bounds[0][0], 1.0f), 1.0f / MAX(body.bounds[1][1] - body.bounds[0][1], 1.0f) };
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFOAMSOURCES, sources);
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFOAMINTERACTIONRECT, rect);
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFLOWFIELDPARAMS, fieldParams);
		// GL 4.3 depth contacts: resampled by the program, not into foam.source
		const bool contacts = interaction.gpuIntersection && interaction.intersectionFoamFresh &&
			r_waterFoamIntersection->value > 0.0f;
		GL_BindToTMU(contacts ? interaction.gpuIntersection : tr.whiteImage, 22);	// unread at scale 0
		const vec4_t contactParams = { contacts ? r_waterFoamIntersection->value : 0.0f, 0.0f, 0.0f, 0.0f };
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFOAMINTERSECTIONPARAMS, contactParams);
		interaction.intersectionFoamFresh = false;
		const int target = foam.current ^ 1;
		FBO_Bind(tr.waterFoamFbo);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, foam.image[target]->texnum, 0);
		qglDrawBuffer(GL_COLOR_ATTACHMENT0);
		GL_SetViewportAndScissor(0, 0, w, h);
		GL_BindToTMU(foam.image[foam.current], TB_WATERFOAM);
		vec4_t params = { dt, MAX(r_waterFoamDecay->value, 0.0f),
			Com_Clamp(0.0f, 1.0f, r_waterFoamDiffusion->value),
			Com_Clamp(0.0f, 4.0f, r_waterFoamAdvection->value) };
		const float flowScale = r_waterFlow->integer ? r_waterFlowSpeed->value : 0.0f;
		vec4_t flow = { body.flowVelocity[0] * flowScale / MAX(body.bounds[1][0] - body.bounds[0][0], 1.0f),
			body.flowVelocity[1] * flowScale / MAX(body.bounds[1][1] - body.bounds[0][1], 1.0f),
			backEnd.refdef.floatTime, (float)r_waterFoamDebug->integer };
		float profileSource = 0.0f;
		if (r_waterWhitewater->integer)
		{
			const float profile = body.dynamics == 5 ? 0.58f :
				(body.dynamics == 7 ? 0.82f : (body.dynamics == 8 ? 1.0f :
				(body.dynamics == 4 ? 0.025f : 0.0f)));
			const float speed = Com_Clamp(0.0f, 1.0f, body.flowSpeed * flowScale / 32.0f);
			const float shallow = body.depthAverage > 0.0f ?
				1.0f - Com_Clamp(0.0f, 1.0f, body.depthAverage / 64.0f) : 0.0f;
			const float slope = 1.0f - Com_Clamp(0.0f, 1.0f, fabsf(body.planeNormal[2]));
			profileSource = profile * Com_Clamp(0.25f, 1.5f,
				0.35f + 0.45f * speed + 0.35f * shallow + 0.65f * slope);
			// The CPU disturbance solver exposes a body energy scalar. Local
			// impacts/intersections also arrive in the source texture; this small
			// term keeps energetic unresolved disturbance eligible without making
			// calm bodies permanently aerated.
			if (body.interaction.image)
				profileSource = MAX(profileSource, Com_Clamp(0.0f, 0.8f,
					sqrtf(MAX(body.interaction.energy, 0.0f)) * 0.12f));
		}
		vec4_t whitewater = { Com_Clamp(0.0f, 1.0f, r_waterWhitewaterThreshold->value),
			MAX(r_waterWhitewaterDecay->value, 0.0f),
			Com_Clamp(0.0f, 4.0f, r_waterWhitewaterFoam->value), profileSource };
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFOAMPARAMS, params);
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERFOAMFLOW, flow);
		GLSL_SetUniformVec4(&tr.waterFoamFieldShader, UNIFORM_WATERWHITEWATERPARAMS, whitewater);
		RB_InstantTriangle();
		foam.current = target;
		// Keep the uploaded texture for source debug; CPU channels are rebuilt.
		if (foam.sourceDirty)
			std::fill(foam.source.begin(), foam.source.end(), 0);
		foam.sourceDirty = false;
		if (R_WaterInteractionGpuEnabled() && interaction.gpuState[0])
			std::fill(body.interaction.intersectionFoam.begin(),
				body.interaction.intersectionFoam.end(), 0.0f);
	}
	FBO_Bind(oldFbo);
	GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	RB_ScreenEndTimer(timer);
	GL_SelectTexture(0);
}

void R_WaterInteractionInfo_f(void)
{
	if (!r_waterSurface || !r_waterSurface->integer)
	{
		ri.Printf(PRINT_ALL, "water interaction: unavailable because r_waterSurface is disabled (set r_waterSurface 1; vid_restart)\n");
		return;
	}
	if (!r_waterInteraction || !r_waterInteraction->integer)
	{
		ri.Printf(PRINT_ALL, "water interaction: disabled (set r_waterInteraction 1; vid_restart)\n");
		return;
	}
	if (!s_water.interactionGpuBytes)
	{
		ri.Printf(PRINT_ALL, "water interaction: enabled, but this map has no allocated modern-water domains; check r_waterInteractionMaxBodies/MaxTexels/MemoryMB\n");
		return;
	}
	const double submitAverage = s_water.interactionSubmissions ?
		s_water.interactionSubmitUsec / s_water.interactionSubmissions : 0.0;
	const bool gpu = R_WaterInteractionGpuEnabled();
	ri.Printf(PRINT_ALL, "water interaction: enabled (%s), %u GPU bytes, geometry %s, last %d fixed steps, source %.3f ms, step dispatch %.3f ms, resolve submit %.3f ms, API submit %.3f us avg (%u)\n",
		gpu ? (R_WaterIntersectionComputeEnabled() ? "GL3.2 ping-pong, GL 4.3 depth contacts" : "GL3.2 ping-pong") : "CPU fallback",
		(unsigned)s_water.interactionGpuBytes, r_waterGeometry && r_waterGeometry->integer ? "displacement+optics" : "optics-only",
		s_water.interactionSteps, s_water.interactionSourceMsec,
		s_water.interactionStepMsec, s_water.interactionUploadMsec, submitAverage,
		s_water.interactionSubmissions);
	if (R_WaterIntersectionComputeEnabled())
		ri.Printf(PRINT_ALL, "  depth contacts: %d body dispatches last frame, %d wakes from readbacks\n",
			s_water.intersectionDispatches, s_water.intersectionWakes);
	for (const waterBody_t& body : s_water.bodies)
	{
		const waterInteractionState_t& sim = body.interaction;
		if (!sim.image) continue;
		int maskCells = 0; for (byte m : sim.mask) maskCells += m != 0;
		ri.Printf(PRINT_ALL, "  body %d: %dx%d, %.1fx%.1f wu/texel, mask %d/%d, %s, energy estimate %.7g, GPU %u bytes\n",
			body.id, sim.width, sim.height, sim.texelX, sim.texelY, maskCells, sim.width * sim.height,
			sim.active ? "active" : "sleeping", sim.energy, (unsigned)((size_t)sim.width * sim.height * 32));
	}
}

void R_WaterImpulse_f(void)
{
	if (!r_waterInteraction->integer) { ri.Printf(PRINT_ALL, "r_waterInteraction is disabled (latched; set 1 and vid_restart)\n"); return; }
	const float strength = ri.Cmd_Argc() > 1 ? (float)atof(ri.Cmd_Argv(1)) : 3.0f;
	const float radius = ri.Cmd_Argc() > 2 ? (float)atof(ri.Cmd_Argv(2)) : 32.0f;
	waterBody_t *best = nullptr; vec3_t hit = {}; float bestT = 1.0e30f;
	for (waterBody_t& body : s_water.bodies)
	{
		const float denom = DotProduct(body.planeNormal, tr.refdef.viewaxis[0]);
		if (fabsf(denom) < 1.0e-5f) continue;
		const float t = (body.planeDist - DotProduct(body.planeNormal, tr.refdef.vieworg)) / denom;
		if (t <= 0.0f || t >= bestT) continue;
		vec3_t p; VectorMA(tr.refdef.vieworg, t, tr.refdef.viewaxis[0], p);
		if (R_WaterInteractionBodyAt(p) == &body) { best = &body; bestT = t; VectorCopy(p, hit); }
	}
	// The simulation mask is discrete and a console command can run between
	// scene submissions. Fall back to the closest masked texel centre along the
	// last rendered crosshair ray; this also makes thin/triangular shores usable.
	if (!best)
	{
		float bestAngle = 1.0e30f;
		for (waterBody_t& body : s_water.bodies)
		{
			waterInteractionState_t& sim = body.interaction;
			if (!sim.image || fabsf(body.planeNormal[2]) < 1.0e-4f) continue;
			for (int y = 0; y < sim.height; ++y) for (int x = 0; x < sim.width; ++x)
			{
				if (!sim.mask[(size_t)y * sim.width + x]) continue;
				vec3_t p = { body.bounds[0][0] + (x + 0.5f) * sim.texelX,
					body.bounds[0][1] + (y + 0.5f) * sim.texelY, 0.0f };
				p[2] = (body.planeDist - body.planeNormal[0] * p[0] - body.planeNormal[1] * p[1]) / body.planeNormal[2];
				vec3_t delta, lateral; VectorSubtract(p, tr.refdef.vieworg, delta);
				const float t = DotProduct(delta, tr.refdef.viewaxis[0]);
				if (t <= 0.0f) continue;
				VectorMA(delta, -t, tr.refdef.viewaxis[0], lateral);
				const float angle = DotProduct(lateral, lateral) / (t * t);
				if (angle < bestAngle) { bestAngle = angle; best = &body; bestT = t; VectorCopy(p, hit); }
			}
		}
		if (bestAngle > 0.0311f) best = nullptr; // farther than ten degrees from the crosshair
	}
	if (!best) { ri.Printf(PRINT_ALL, "r_waterImpulse: crosshair does not hit an allocated water-body domain\n"); return; }
	refWaterImpulse_t impulse = {}; VectorCopy(hit, impulse.position); impulse.radius = radius; impulse.strength = strength;
	RE_AddWaterImpulse(&impulse);
	ri.Printf(PRINT_ALL, "water impulse: queued for body %d at (%.1f %.1f %.1f), radius %.1f, strength %.2f; use r_waterInteractionDebug 3 to inspect height\n",
		best->id, hit[0], hit[1], hit[2], radius, strength);
}

/*
=================
RB_WaterSurfaceSetCoverage

Conservative screen rectangle of submitted world-water bodies. Refraction can
sample up to 12% of the view away from a fragment, so the rectangle includes
that halo. Unreliable bounds or an AABB crossing the eye plane fall back to the
complete viewport.
=================
*/
void RB_WaterSurfaceSetCoverage( const drawSurf_t *drawSurfs, int numDrawSurfs )
{
	backEnd.waterInterfacesVisible = qfalse;
	const viewParms_t& v = backEnd.viewParms;
	for (waterBody_t& body : s_water.bodies)
	{
		body.viewCoverage = 0.0f;
		body.viewDistance = 0.0f;
	}
	for (int profile = 0; profile < WFPROFILE_COUNT; ++profile)
		s_water.waterfallProfileCoverage[profile] = 0.0f;
	s_water.copyRect[0] = v.viewportX;
	s_water.copyRect[1] = v.viewportY;
	s_water.copyRect[2] = v.viewportWidth;
	s_water.copyRect[3] = v.viewportHeight;
	if ((!r_waterSurface->integer && (!r_waterfall || !r_waterfall->integer)) || !drawSurfs || numDrawSurfs <= 0)
		return;

	int minX = v.viewportX + v.viewportWidth;
	int minY = v.viewportY + v.viewportHeight;
	int maxX = v.viewportX;
	int maxY = v.viewportY;
	bool full = false;
	std::set<int> projectedBodies;
	const float *m = v.world.modelViewMatrix;
	const float *p = v.projectionMatrix;
	for (int i = 0; i < numDrawSurfs && !full; ++i)
	{
		const uint32_t key = drawSurfs[i].waterKey;
		if (!(key & WATERKEY_INTERFACE))
			continue;
		if ((key & WATERKEY_WATERFALL) ? (!r_waterfall || !r_waterfall->integer) : !r_waterSurface->integer)
			continue;
		backEnd.waterInterfacesVisible = qtrue;
		const int bodyId = (int)(key >> 20);
		if (!(key & WATERKEY_WORLD_BRUSH) || bodyId <= 0 || bodyId > (int)s_water.bodies.size())
		{
			full = true;
			break;
		}
		if (!projectedBodies.insert(bodyId).second)
			continue;
		waterBody_t& body = s_water.bodies[bodyId - 1];
		int bodyMinX = v.viewportX + v.viewportWidth;
		int bodyMinY = v.viewportY + v.viewportHeight;
		int bodyMaxX = v.viewportX;
		int bodyMaxY = v.viewportY;
		vec3_t bodyCenter, bodyDelta;
		VectorAdd(body.bounds[0], body.bounds[1], bodyCenter);
		VectorScale(bodyCenter, 0.5f, bodyCenter);
		VectorSubtract(bodyCenter, v.ori.origin, bodyDelta);
		body.viewDistance = VectorLength(bodyDelta);
		// Match the deliberately conservative render-mesh cull expansion: live
		// amplitude controls and horizontal crest drift can move the interface
		// outside the static BSP/body bounds.
		const float waveExtent = 4.0f + s_dynamics[body.dynamics].amplitude * 8.0f * 4.0f *
			MAX(body.waveMultiplier, 1.0f) * 2.0f;
		const float interactionExtent = body.interaction.image && r_waterInteraction->integer ? 64.0f : 0.0f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const float x = body.bounds[(corner >> 0) & 1][0] + (((corner >> 0) & 1) ? waveExtent : -waveExtent);
			const float y = body.bounds[(corner >> 1) & 1][1] + (((corner >> 1) & 1) ? waveExtent : -waveExtent);
			const float zExtent = waveExtent + interactionExtent;
			const float z = body.bounds[(corner >> 2) & 1][2] + (((corner >> 2) & 1) ? zExtent : -zExtent);
			const float ex = m[0] * x + m[4] * y + m[8] * z + m[12];
			const float ey = m[1] * x + m[5] * y + m[9] * z + m[13];
			const float ez = m[2] * x + m[6] * y + m[10] * z + m[14];
			const float ew = m[3] * x + m[7] * y + m[11] * z + m[15];
			const float cx = p[0] * ex + p[4] * ey + p[8] * ez + p[12] * ew;
			const float cy = p[1] * ex + p[5] * ey + p[9] * ez + p[13] * ew;
			const float cw = p[3] * ex + p[7] * ey + p[11] * ez + p[15] * ew;
			if (cw <= 1.0e-4f)
			{
				body.viewCoverage = 1.0f;
				if (body.waterfall)
					s_water.waterfallProfileCoverage[body.waterfallProfile] = 1.0f;
				full = true;
				break;
			}
			const float sx = v.viewportX + (cx / cw * 0.5f + 0.5f) * v.viewportWidth;
			const float sy = v.viewportY + (cy / cw * 0.5f + 0.5f) * v.viewportHeight;
			minX = MIN(minX, (int)floorf(sx));
			minY = MIN(minY, (int)floorf(sy));
			maxX = MAX(maxX, (int)ceilf(sx));
			maxY = MAX(maxY, (int)ceilf(sy));
			bodyMinX = MIN(bodyMinX, (int)floorf(sx));
			bodyMinY = MIN(bodyMinY, (int)floorf(sy));
			bodyMaxX = MAX(bodyMaxX, (int)ceilf(sx));
			bodyMaxY = MAX(bodyMaxY, (int)ceilf(sy));
		}
		if (!full && bodyMaxX > bodyMinX && bodyMaxY > bodyMinY)
		{
			const int clippedWidth = MAX(0, MIN(bodyMaxX, v.viewportX + v.viewportWidth) - MAX(bodyMinX, v.viewportX));
			const int clippedHeight = MAX(0, MIN(bodyMaxY, v.viewportY + v.viewportHeight) - MAX(bodyMinY, v.viewportY));
			body.viewCoverage = (float)(clippedWidth * clippedHeight) /
				MAX((float)(v.viewportWidth * v.viewportHeight), 1.0f);
			if (body.waterfall)
			{
				float& profileCoverage = s_water.waterfallProfileCoverage[body.waterfallProfile];
				profileCoverage = MIN(profileCoverage + body.viewCoverage, 1.0f);
			}
		}
	}
	if (!backEnd.waterInterfacesVisible || full)
		return;

	const int haloX = (int)ceilf(0.12f * v.viewportWidth) + 2;
	const int haloY = (int)ceilf(0.12f * v.viewportHeight) + 2;
	minX = Com_Clampi(v.viewportX, v.viewportX + v.viewportWidth, minX - haloX);
	minY = Com_Clampi(v.viewportY, v.viewportY + v.viewportHeight, minY - haloY);
	maxX = Com_Clampi(v.viewportX, v.viewportX + v.viewportWidth, maxX + haloX);
	maxY = Com_Clampi(v.viewportY, v.viewportY + v.viewportHeight, maxY + haloY);
	if (maxX > minX && maxY > minY)
	{
		s_water.copyRect[0] = minX;
		s_water.copyRect[1] = minY;
		s_water.copyRect[2] = maxX - minX;
		s_water.copyRect[3] = maxY - minY;
	}
}

static int R_WaterLiquidClass( int contents )
{
	if ( contents & CONTENTS_LAVA )
		return LIQUID_LAVA;
	if ( contents & CONTENTS_SLIME )
		return LIQUID_SLIME;
	if ( contents & CONTENTS_WATER )
		return LIQUID_WATER;
	return -1;
}

// the experimental name rule (and the r_waterInfo hint): whole words of the
// path, never a plain substring of an unrelated word
static qboolean R_WaterNameLike( const char *name )
{
	static const char *words[] = { "water", "pool", "lake", "river", "pond", "ocean", "swamp", "liquid", NULL };
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	for ( int i = 0; words[i]; i++ )
	{
		const size_t len = strlen(words[i]);
		for ( const char *p = strstr(lower, words[i]); p; p = strstr(p + 1, words[i]) )
		{
			// the word may be followed by digits or a separator, and must start a path element or word
			const qboolean start = (qboolean)(p == lower || p[-1] == '/' || p[-1] == '_' || p[-1] == '-');
			const char next = p[len];
			const qboolean end = (qboolean)(next == '\0' || next == '_' || next == '-' || next == '/' ||
				(next >= '0' && next <= '9'));
			if ( start && end )
				return qtrue;
		}
	}
	return qfalse;
}

static qboolean R_WaterPatternMatches( const std::string& pattern, const char *name )
{
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	if ( !pattern.empty() && pattern.back() == '*' )
		return (qboolean)(strncmp(lower, pattern.c_str(), pattern.size() - 1) == 0);
	return (qboolean)(pattern == lower);
}

static const waterOverride_t *R_WaterFindOverride( const char *name )
{
	// the last matching entry wins (a later, more specific line replaces a prefix)
	const waterOverride_t *found = NULL;
	for ( const waterOverride_t& o : s_water.overrides )
	{
		if ( R_WaterPatternMatches(o.pattern, name) )
			found = &o;
	}
	return found;
}

static int R_WaterOrientation( const vec3_t n )
{
	if ( n[2] > 0.7f )
		return WORIENT_UP;
	if ( n[2] < -0.7f )
		return WORIENT_DOWN;
	if ( fabsf(n[2]) < 0.3f )
		return WORIENT_VERTICAL;
	return WORIENT_SLOPED;
}

// p inside brush b (grown by eps)
static qboolean R_WaterInsideBrush( const waterBrushRecord_t& b, const vec3_t p, float eps )
{
	for ( int k = 0; k < b.numPlanes; k++ )
	{
		const waterPlane_t& plane = s_water.planes[b.firstPlane + k];
		if ( DotProduct(plane.normal, p) - plane.dist > eps )
			return qfalse;
	}
	return qtrue;
}

/*
=================
R_WaterDecideShaders

Per shader decision from the surface records, then r_waterOverride and the
experimental rule; writes per-surface waterKey and shader candidate flags.
Runs at map load and again when r_waterOverride or r_waterSurfaceExperimental
change.
=================
*/
static qboolean R_WaterMostlyUp( const waterShaderRecord_t& s )
{
	return (qboolean)(s.upArea > 0.0f && s.upArea >= 0.5f * (s.area - s.downArea));
}

static void R_WaterRefreshBodies( void );

static void R_WaterDecideShaders( void )
{
	// Overrides and the experimental classifier can change body/profile identity
	// without recreating the renderer resources.  Never carry a geometry
	// snapshot across that reinterpretation.
	R_WaterMotionInvalidate(WM_INVALID_BODY_REBUILD);
	// reset every shader of the records (a previous decision may be undone)
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		s.shader->waterSurface = 0;
		s.shader->waterFlowStage = nullptr;
		for (int stage = 0; stage < MAX_SHADER_STAGES && s.shader->stages[stage]; stage++)
		{
			shaderStage_t *candidate = s.shader->stages[stage];
			shaderStage_t *flow = s.shader->waterFlowStage;
			if (!candidate->ss && !candidate->bundle[0].isLightmap &&
				(!flow || (!flow->bundle[0].numTexMods && candidate->bundle[0].numTexMods)))
				s.shader->waterFlowStage = candidate;
		}
	}

	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		const int liquidClass = R_WaterLiquidClass(s.contents);
		const qboolean refractive = s.shader->useDistortion;
		s.modern = qfalse;
		s.flags = 0;
		s.liquidClass = (liquidClass == LIQUID_SLIME) ? LIQUID_SLIME : LIQUID_WATER;

		if ( s.shader->isSky || s.shader->isPortal || (s.shader->surfaceFlags & SURF_NODRAW) )
			s.reason = WREASON_UNSUPPORTED;
		else if ( liquidClass == LIQUID_LAVA )
			s.reason = WREASON_LAVA;
		else if ( s.contents & CONTENTS_WATER )
			s.reason = s.upArea > 0.0f ? WREASON_CONTENTS : WREASON_NOT_HORIZONTAL;
		else if ( liquidClass == LIQUID_SLIME )
			s.reason = WREASON_SLIME_ONLY;
		else if ( s.linked > 0 && s.upArea > 0.0f )
			s.reason = WREASON_BRUSH_TOP;
		else if ( refractive )
			s.reason = WREASON_REFRACTIVE_ONLY;
		else
			s.reason = WREASON_NO_SEMANTICS;

		s.modern = (qboolean)(s.reason == WREASON_CONTENTS || s.reason == WREASON_BRUSH_TOP);

		if ( s.reason == WREASON_UNSUPPORTED )
			continue;

		const waterOverride_t *o = R_WaterFindOverride(s.shader->name);
		if ( o )
		{
			s.modern = (qboolean)(o->mode != 0);
			s.reason = o->mode ? WREASON_OVERRIDE_ON : WREASON_OVERRIDE_OFF;
			s.flags |= WATERSURF_OVERRIDE;
			if ( o->liquidClass >= 0 )
				s.liquidClass = o->liquidClass;
		}
		else if ( !s.modern && r_waterSurfaceExperimental->integer && s.reason == WREASON_NO_SEMANTICS &&
			R_WaterNameLike(s.shader->name) && R_WaterMostlyUp(s) )
		{
			s.modern = qtrue;
			s.reason = WREASON_EXPERIMENTAL;
			s.flags |= WATERSURF_EXPERIMENTAL;
		}
	}

	std::unordered_map<const shader_t *, const waterShaderRecord_t *> decisions;
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		decisions[s.shader] = &s;
		if ( s.modern )
			s.shader->waterSurface = 1; // candidate only; draws use the surface key
	}
	for ( waterSurfaceRecord_t& r : s_water.surfaces )
	{
		const waterShaderRecord_t& d = *decisions[r.shader];
		r.reason = d.reason;
		r.flags = d.flags;
		r.liquidClass = R_WaterLiquidClass(r.contents) == LIQUID_SLIME ? LIQUID_SLIME : LIQUID_WATER;
		const waterBrushRecord_t *brush = r.brush >= 0 ? &s_water.brushes[r.brush] : nullptr;
		if ( brush && brush->liquidClass == LIQUID_SLIME )
			r.liquidClass = LIQUID_SLIME;
		if ( (r.contents & CONTENTS_FOG) || (brush && (brush->contents & CONTENTS_FOG)) )
			r.flags |= WATERSURF_FOG_MEDIUM;
		const int profile = R_LiquidProfileForShader(r.shader->name);
		if ( profile == LIQUID_WATER || profile == LIQUID_SLIME )
			r.liquidClass = profile;
		const waterOverride_t *override = R_WaterFindOverride(r.shader->name);
		if ( override && override->liquidClass >= 0 )
			r.liquidClass = override->liquidClass;
		// Only an upward boundary is the pool/lake interface. Sides and bottoms
		// retain their legacy material; a shared shader must not turn them into air.
		const bool boundary = r.orient == WORIENT_UP;
		const bool linkedTop = brush && brush->liquidClass != LIQUID_LAVA &&
			r.brushSide[2] > 0.7f && DotProduct(r.normal, r.brushSide) > 0.7f;
		const bool semantics = (r.contents & CONTENTS_WATER) || linkedTop;
		r.modern = (qboolean)(d.modern && boundary && (semantics || override ||
			(d.flags & WATERSURF_EXPERIMENTAL)) && !(r.contents & CONTENTS_LAVA));
		if ( d.modern && !boundary )
			r.reason = WREASON_NOT_HORIZONTAL;
		uint32_t key = 0;
		if ( r.modern )
		{
			key = WATERKEY_INTERFACE | ((uint32_t)r.liquidClass << 4) | ((uint32_t)r.flags << 8);
			if ( linkedTop && brush->model == 0 )
				key |= WATERKEY_WORLD_BRUSH | (1u << (16 + brush->liquidClass));
		}
		s_water.world->surfaces[r.surfaceNum].waterKey = key;
	}

	R_WaterUpdateMergedSurfaces(const_cast<world_t *>(s_water.world));
	R_WaterRefreshBodies();
}

void R_WaterUpdateMergedSurfaces(world_t *world)
{
	if (!world || s_water.world != world || !world->viewSurfaces || !world->numMergedSurfaces)
		return;
	if (s_water.mergedViewSurfaces.empty())
		s_water.mergedViewSurfaces.assign(world->viewSurfaces, world->viewSurfaces + world->nummarksurfaces);
	for (int i = 0; i < world->nummarksurfaces; i++)
	{
		const int original = world->marksurfaces[i];
		const shader_t *shader = world->surfaces[original].shader;
		// Keep original surfaces for water/overrides: their classification may
		// differ within a merged mesh. They still batch via the shared static VBO.
		world->viewSurfaces[i] = shader && (shader->waterSurface || R_WaterFindOverride(shader->name)) ?
			original : s_water.mergedViewSurfaces[i];
	}
}

static void R_WaterCollectCandidates( void );
static void R_WaterBuildBodies( void );

/*
=================
R_WaterClassifySurfaces

Called by R_LoadBSP after the surfaces (shader pointers, vertices) and the
planes are loaded. Keeps the records of the candidate surfaces for r_waterInfo.
=================
*/
void R_WaterClassifySurfaces( world_t *world, const byte *fileBase, const lump_t *surfacesLump,
	const lump_t *modelsLump, const lump_t *brushesLump, const lump_t *sidesLump )
{
	R_WaterMotionInvalidate(WM_INVALID_BODY_REBUILD);
	const int start = ri.Milliseconds();
	// the shaders of the previous map are gone with it
	s_water.world = world;
	s_water.mergedViewSurfaces.clear();
	s_water.history.valid = qfalse;
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();
	s_water.bodies.clear();
	s_water.contacts.clear();
	s_water.contactTime = 0;
	s_water.contactUniformTime = -1;
	s_water.bodyRules.clear();
	s_water.waterfallRules.clear();
	s_water.waterfallEmitterRules.clear();
	s_water.waterfallImpacts.clear();
	s_water.mapWindValid = false;
	VectorClear(s_water.mapWindDirection);
	s_water.mapWindStrength = 0.0f;
	s_water.pendingImpulses.clear();
	s_water.continuousSources.clear();
	s_water.wakeTracks.clear();
	s_water.interactionGpuBytes = 0;
	s_water.interactionTime = -1;
	s_water.interactionSourceMsec = s_water.interactionStepMsec = s_water.interactionUploadMsec = 0.0f;
	s_water.interactionSubmitUsec = 0.0;
	s_water.interactionSubmissions = 0;

	if ( surfacesLump->filelen % sizeof(dsurface_t) || modelsLump->filelen % sizeof(dmodel_t) ||
		brushesLump->filelen % sizeof(dbrush_t) || sidesLump->filelen % sizeof(dbrushside_t) )
	{
		return;
	}

	const dsurface_t *dsurfaces = (const dsurface_t *)(fileBase + surfacesLump->fileofs);
	const int numSurfaces = surfacesLump->filelen / sizeof(dsurface_t);
	const dmodel_t *models = (const dmodel_t *)(fileBase + modelsLump->fileofs);
	const int numModels = modelsLump->filelen / sizeof(dmodel_t);
	const dbrush_t *brushes = (const dbrush_t *)(fileBase + brushesLump->fileofs);
	const int numBrushes = brushesLump->filelen / sizeof(dbrush_t);
	const dbrushside_t *sides = (const dbrushside_t *)(fileBase + sidesLump->fileofs);
	const int numSides = sidesLump->filelen / sizeof(dbrushside_t);

	std::vector<int> surfaceModel(numSurfaces, -1);
	std::vector<int> brushModel(numBrushes, -1);
	for ( int m = 0; m < numModels; m++ )
	{
		const int fs = LittleLong(models[m].firstSurface), ns = LittleLong(models[m].numSurfaces);
		const int fb = LittleLong(models[m].firstBrush), nb = LittleLong(models[m].numBrushes);
		for ( int i = Q_max(fs, 0); i < fs + ns && i < numSurfaces; i++ )
			surfaceModel[i] = m;
		for ( int i = Q_max(fb, 0); i < fb + nb && i < numBrushes; i++ )
			brushModel[i] = m;
	}

	// every liquid brush, of every model, fog volumes included
	for ( int i = 0; i < numBrushes; i++ )
	{
		const int shaderNum = LittleLong(brushes[i].shaderNum);
		if ( shaderNum < 0 || shaderNum >= world->numShaders )
			continue;
		const int contents = world->shaders[shaderNum].contentFlags;
		const int liquidClass = R_WaterLiquidClass(contents);
		if ( liquidClass < 0 )
			continue;
		const int firstSide = LittleLong(brushes[i].firstSide);
		const int count = LittleLong(brushes[i].numSides);
		if ( firstSide < 0 || count <= 0 || firstSide + count > numSides )
			continue;

		waterBrushRecord_t b = {};
		b.brushNum = i;
		b.model = brushModel[i];
		b.liquidClass = liquidClass;
		b.contents = contents;
		b.shaderNum = shaderNum;
		b.firstPlane = (int)s_water.planes.size();
		ClearBounds(b.bounds[0], b.bounds[1]);
		for ( int k = 0; k < count; k++ )
		{
			const int planeNum = LittleLong(sides[firstSide + k].planeNum);
			if ( planeNum < 0 || planeNum >= world->numplanes )
				continue;
			const cplane_t *plane = &world->planes[planeNum];
			waterPlane_t p;
			VectorCopy(plane->normal, p.normal);
			p.dist = plane->dist;
			s_water.planes.push_back(p);
			b.numPlanes++;
			// axial sides bound the brush
			for ( int axis = 0; axis < 3; axis++ )
			{
				if ( plane->normal[axis] > 0.999f )
					b.bounds[1][axis] = plane->dist;
				else if ( plane->normal[axis] < -0.999f )
					b.bounds[0][axis] = -plane->dist;
			}
		}
		s_water.brushes.push_back(b);
	}

	s_water.sources.resize(world->numsurfaces);
	for ( int i = 0; i < world->numsurfaces; i++ )
	{
		waterSurfaceSource_t& src = s_water.sources[i];
		src.bspShader = i < numSurfaces ? LittleLong(dsurfaces[i].shaderNum) : -1;
		src.model = i < numSurfaces ? surfaceModel[i] : -1;
		src.type = i < numSurfaces ? LittleLong(dsurfaces[i].surfaceType) : MST_BAD;
	}

	R_WaterCollectCandidates();
	R_WaterBuildBodies();
	R_WaterInteractionBuild();
	R_WaterFoamBuild();
	R_WaterFlowFieldBuild();
	s_water.classifyMsec = (float)(ri.Milliseconds() - start);

	int modern = 0;
	for ( const waterShaderRecord_t& s : s_water.shaders )
		modern += s.modern ? 1 : 0;
	if ( !s_water.shaders.empty() )
		ri.Printf(PRINT_DEVELOPER, "Water surfaces: %d candidate shaders of %d cached (%d surfaces, %d liquid brushes), %.1f ms\n",
			modern, (int)s_water.shaders.size(), (int)s_water.surfaces.size(), (int)s_water.brushes.size(),
			s_water.classifyMsec);
}

/*
=================
R_WaterCollectCandidates

The candidate surfaces of the map (liquid contents, refractive, water-like
name or material, or a shader named by r_waterOverride), their geometry and
liquid brush, the per shader records, then the decisions.
=================
*/
static void R_WaterCollectCandidates( void )
{
	const world_t *world = s_water.world;
	// a shader that leaves the candidates (an override cleared) must not keep its decision
	for ( waterShaderRecord_t& s : s_water.shaders )
	{
		s.shader->waterSurface = 0;
	}
	s_water.surfaces.clear();
	s_water.shaders.clear();
	if ( !world )
		return;

	std::unordered_map<int, std::vector<size_t>> modelBrushes;
	for (size_t i = 0; i < s_water.brushes.size(); i++)
		modelBrushes[s_water.brushes[i].model].push_back(i);
	for ( int i = 0; i < world->numsurfaces && i < (int)s_water.sources.size(); i++ )
	{
		const int bspShader = s_water.sources[i].bspShader;
		if ( bspShader < 0 || bspShader >= world->numShaders )
			continue;
		const int contents = world->shaders[bspShader].contentFlags;
		const int surfaceFlags = world->shaders[bspShader].surfaceFlags;
		const msurface_t *surf = &world->surfaces[i];
		shader_t *shader = surf->shader;
		if ( !shader )
			continue;

		const qboolean refractive = shader->useDistortion;
		const qboolean nameLike = R_WaterNameLike(shader->name);

		waterSurfaceRecord_t r = {};
		r.surfaceNum = i;
		r.model = s_water.sources[i].model;
		r.bspShader = bspShader;
		r.contents = contents;
		r.surfaceFlags = surfaceFlags;
		r.type = s_water.sources[i].type;
		r.shader = shader;
		r.brush = -1;
		r.refractive = refractive;
		r.nameLike = nameLike;

		// geometry: the area weighted normal of the triangles
		const surfaceType_t type = surf->data ? *surf->data : SF_BAD;
		if ( type == SF_FACE || type == SF_GRID || type == SF_TRIANGLES )
		{
			const srfBspSurface_t *bsp = (const srfBspSurface_t *)surf->data;
			vec3_t sum = { 0.0f, 0.0f, 0.0f };
			vec3_t tangentUSum = { 0.0f, 0.0f, 0.0f }, tangentVSum = { 0.0f, 0.0f, 0.0f };
			float area = 0.0f;
			float uvArea = 0.0f, mirrorArea = 0.0f;
			for ( int t = 0; t + 2 < bsp->numIndexes; t += 3 )
			{
				const int a = bsp->indexes[t], b = bsp->indexes[t + 1], c = bsp->indexes[t + 2];
				if ( a >= bsp->numVerts || b >= bsp->numVerts || c >= bsp->numVerts )
					continue;
				vec3_t e1, e2, n;
				VectorSubtract(bsp->verts[b].xyz, bsp->verts[a].xyz, e1);
				VectorSubtract(bsp->verts[c].xyz, bsp->verts[a].xyz, e2);
				CrossProduct(e2, e1, n);
				const float triangleArea = 0.5f * VectorLength(n);
				area += triangleArea;
				const float du1 = bsp->verts[b].st[0] - bsp->verts[a].st[0];
				const float dv1 = bsp->verts[b].st[1] - bsp->verts[a].st[1];
				const float du2 = bsp->verts[c].st[0] - bsp->verts[a].st[0];
				const float dv2 = bsp->verts[c].st[1] - bsp->verts[a].st[1];
				const float determinant = du1 * dv2 - du2 * dv1;
				if ( triangleArea > 1.0e-6f && fabsf(determinant) > 1.0e-8f )
				{
					vec3_t tu, tv;
					for ( int axis = 0; axis < 3; axis++ )
					{
						tu[axis] = (e1[axis] * dv2 - e2[axis] * dv1) / determinant;
						tv[axis] = (e2[axis] * du1 - e1[axis] * du2) / determinant;
						tangentUSum[axis] += tu[axis] * triangleArea;
						tangentVSum[axis] += tv[axis] * triangleArea;
					}
					uvArea += triangleArea;
					mirrorArea += (determinant < 0.0f ? -1.0f : 1.0f) * triangleArea;
				}
				// the winding of the BSP triangles is clockwise seen from the front: compare with the
				// vertex normals and keep their side
				VectorAdd(sum, n, sum);
			}
			vec3_t vnormal = { 0.0f, 0.0f, 0.0f };
			vec3_t surfaceBounds[2];
			ClearBounds(surfaceBounds[0], surfaceBounds[1]);
			for ( int v = 0; v < bsp->numVerts; v++ )
			{
				VectorAdd(vnormal, bsp->verts[v].normal, vnormal);
				// Renderer cullBounds are built later by the VBO pass.
				AddPointToBounds(bsp->verts[v].xyz, surfaceBounds[0], surfaceBounds[1]);
			}
			if ( DotProduct(sum, vnormal) < 0.0f )
				VectorNegate(sum, sum);
			if ( VectorNormalize(sum) < 1e-6f )
				VectorNormalize2(vnormal, sum);
			VectorCopy(sum, r.normal);
			r.area = area;
			r.uvArea = uvArea;
			r.uvTotalArea = area;
			if ( uvArea > 0.0f )
			{
				VectorScale(tangentUSum, 1.0f / uvArea, r.uvTangentU);
				VectorScale(tangentVSum, 1.0f / uvArea, r.uvTangentV);
				r.uvMirrorSign = fabsf(mirrorArea) >= 0.8f * uvArea ? (mirrorArea < 0.0f ? -1 : 1) : 0;
			}
			VectorCopy(surfaceBounds[0], r.bounds[0]);
			VectorCopy(surfaceBounds[1], r.bounds[1]);

			// the liquid brush it lies on: every vertex inside the brush and on one of its sides
			for ( size_t bi : modelBrushes[r.model] )
			{
				if (bsp->numVerts <= 0)
					break;
				const waterBrushRecord_t& b = s_water.brushes[bi];
				if ( b.model != r.model )
					continue;
				bool bounded = true;
				for ( int axis = 0; axis < 3; axis++ )
					if ( b.bounds[0][axis] <= b.bounds[1][axis] )
						bounded &= surfaceBounds[1][axis] >= b.bounds[0][axis] - 2.0f &&
							surfaceBounds[0][axis] <= b.bounds[1][axis] + 2.0f;
				if ( !bounded )
					continue;
				bool inside = true;
				for (int v = 0; v < bsp->numVerts && inside; v++)
					inside = R_WaterInsideBrush(b, bsp->verts[v].xyz, 2.0f) != qfalse;
				if (!inside)
					continue;
				int commonSide = -1;
				// A corner vertex lies on several planes. Try all of them rather
				// than choosing the first plane, which may be a vertical side.
				for (int side = 0; side < b.numPlanes && commonSide < 0; side++)
				{
					const waterPlane_t& plane = s_water.planes[b.firstPlane + side];
					bool coplanar = true;
					for (int v = 0; v < bsp->numVerts && coplanar; v++)
						coplanar = fabsf(DotProduct(plane.normal, bsp->verts[v].xyz) - plane.dist) < 1.5f;
					if (coplanar)
						commonSide = side;
				}
				if (commonSide >= 0)
				{
					r.brush = (int)bi;
					VectorCopy(s_water.planes[b.firstPlane + commonSide].normal, r.brushSide);
					break;
				}
			}
		}
		r.orient = R_WaterOrientation(r.normal);
		s_water.surfaces.push_back(r);
	}

	// Indexed aggregation: linear in the number of surfaces, including custom overrides.
	std::unordered_map<shader_t *, size_t> shaderIndexes;
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		waterShaderRecord_t *s = nullptr;
		const auto found = shaderIndexes.find(r.shader);
		if ( found != shaderIndexes.end() )
			s = &s_water.shaders[found->second];
		if ( !s )
		{
			waterShaderRecord_t e = {};
			e.shader = r.shader;
			e.bspShader = r.bspShader;
			e.reason = WREASON_NONE;
			shaderIndexes[r.shader] = s_water.shaders.size();
			s_water.shaders.push_back(e);
			s = &s_water.shaders.back();
		}
		s->contents |= r.contents;
		s->surfaces++;
		s->orientCount[r.orient]++;
		s->area += r.area;
		if ( r.orient == WORIENT_UP )
			s->upArea += r.area;
		else if ( r.orient == WORIENT_DOWN )
			s->downArea += r.area;
		if ( r.brush >= 0 )
			s->linked++;
	}

	R_WaterDecideShaders();
}

static bool R_WaterBoundsTouch( const vec3_t a[2], const vec3_t b[2], float epsilon )
{
	for ( int k = 0; k < 3; k++ )
		if ( a[1][k] + epsilon < b[0][k] || b[1][k] + epsilon < a[0][k] )
			return false;
	return true;
}

static void R_WaterBodyMotion( waterBody_t& body )
{
	for ( const std::string& name : body.shaders )
	{
		const shader_t *shader = nullptr;
		for ( const waterSurfaceRecord_t& s : s_water.surfaces )
			if ( name == s.shader->name &&
				(R_WaterLiquidClass(s.contents) >= 0 || (s.brush >= 0 && s.brushSide[2] > .7f)) )
			{ shader = s.shader; break; }
		if ( !shader ) continue;
		for ( int stage = 0; stage < MAX_SHADER_STAGES && shader->stages[stage]; stage++ )
		{
			const textureBundle_t& bundle = shader->stages[stage]->bundle[0];
			bool stableStageBasis = true;
			for ( int t = 0; t < bundle.numTexMods; t++ )
				if ( bundle.texMods[t].type == TMOD_TRANSFORM || bundle.texMods[t].type == TMOD_SCALE ||
					bundle.texMods[t].type == TMOD_ROTATE || bundle.texMods[t].type == TMOD_ENTITY_TRANSLATE )
					stableStageBasis = false;
			for ( int t = 0; t < bundle.numTexMods; t++ )
			{
				const texModInfo_t& mod = bundle.texMods[t];
				if ( mod.type == TMOD_SCROLL )
				{
					legacyWaterMotion_t::scroll_t scroll = {};
					scroll.shader = name;
					scroll.stage = stage;
					scroll.x = mod.scroll[0];
					scroll.y = mod.scroll[1];
					float candidateArea = 0.0f, reliableArea = 0.0f, velocityMagnitudeArea = 0.0f;
					vec3_t velocitySum = { 0.0f, 0.0f, 0.0f };
					for ( const waterSurfaceRecord_t& surface : s_water.surfaces )
					{
						if ( name != surface.shader->name ||
							std::find(body.surfaces.begin(), body.surfaces.end(), surface.surfaceNum) == body.surfaces.end() )
							continue;
						candidateArea += surface.area;
						const float coverage = surface.uvTotalArea > 0.0f ? surface.uvArea / surface.uvTotalArea : 0.0f;
						vec3_t cross;
						CrossProduct(surface.uvTangentU, surface.uvTangentV, cross);
						if ( coverage < 0.8f || surface.uvMirrorSign == 0 || VectorLength(cross) < 1.0e-4f )
							continue;
						vec3_t velocity;
						// A positive tcMod offset makes a fixed texture feature move in the
						// negative base-UV direction. dp/du and dp/dv preserve rotation,
						// anisotropic scale, mirroring and surface slope.
						for ( int axis = 0; axis < 3; axis++ )
							velocity[axis] = -mod.scroll[0] * surface.uvTangentU[axis] -
								mod.scroll[1] * surface.uvTangentV[axis];
						VectorMA(velocitySum, surface.area, velocity, velocitySum);
						velocityMagnitudeArea += VectorLength(velocity) * surface.area;
						reliableArea += surface.area;
					}
					if ( reliableArea > 0.0f )
						VectorScale(velocitySum, 1.0f / reliableArea, scroll.worldVelocity);
					scroll.surfaceArea = reliableArea;
					scroll.basisAgreement = velocityMagnitudeArea > 0.0f ?
						Q_min(1.0f, VectorLength(velocitySum) / velocityMagnitudeArea) : 0.0f;
					scroll.reliable = (qboolean)(stableStageBasis && candidateArea > 0.0f && reliableArea >= 0.8f * candidateArea &&
						scroll.basisAgreement >= 0.8f && VectorLength(scroll.worldVelocity) > 1.0e-4f);
					body.legacy.scrolls.push_back(scroll);
				}
				else if ( mod.type == TMOD_TURBULENT )
				{
					body.legacy.turb.push_back({ stage, mod.wave.base, mod.wave.amplitude, mod.wave.phase, mod.wave.frequency });
					body.legacy.turbAmplitude = Q_max(body.legacy.turbAmplitude, fabsf(mod.wave.amplitude));
					body.legacy.turbFrequency = Q_max(body.legacy.turbFrequency, fabsf(mod.wave.frequency));
				}
				else if ( mod.type == TMOD_STRETCH )
					body.legacy.stretch.push_back({ stage, mod.wave.base, mod.wave.amplitude, mod.wave.phase, mod.wave.frequency });
			}
		}
		for ( int d = 0; d < shader->numDeforms; d++ )
			if ( shader->deforms[d].deformation == DEFORM_WAVE )
			{
				body.legacy.deformAmplitude = Q_max(body.legacy.deformAmplitude, fabsf(shader->deforms[d].deformationWave.amplitude));
				body.legacy.deformFrequency = Q_max(body.legacy.deformFrequency, fabsf(shader->deforms[d].deformationWave.frequency));
			}
	}
	float weight = 0, sx = 0, sy = 0;
	for ( const legacyWaterMotion_t::scroll_t& scroll : body.legacy.scrolls )
	{
		const float len = sqrtf(scroll.x * scroll.x + scroll.y * scroll.y);
		if ( len > 0 ) { sx += scroll.x; sy += scroll.y; weight += len; }
	}
	if ( weight > 0 )
	{
		body.legacy.weightedScroll[0] = sx / weight;
		body.legacy.weightedScroll[1] = sy / weight;
		body.legacy.disagreement = 1.0f - Q_min(1.0f, sqrtf(sx * sx + sy * sy) / weight);
	}
	vec3_t worldSum = { 0.0f, 0.0f, 0.0f };
	float worldMagnitudeSum = 0.0f;
	int reliableScrolls = 0;
	for ( const legacyWaterMotion_t::scroll_t& scroll : body.legacy.scrolls )
	{
		if ( !scroll.reliable ) continue;
		VectorAdd(worldSum, scroll.worldVelocity, worldSum);
		worldMagnitudeSum += VectorLength(scroll.worldVelocity);
		reliableScrolls++;
	}
	float bodyArea = 0.0f, uvArea = 0.0f;
	for ( const waterSurfaceRecord_t& surface : s_water.surfaces )
		if ( std::find(body.surfaces.begin(), body.surfaces.end(), surface.surfaceNum) != body.surfaces.end() )
		{
			bodyArea += surface.area;
			uvArea += surface.uvArea;
		}
	body.legacy.uvCoverage = bodyArea > 0.0f ? Q_min(1.0f, uvArea / bodyArea) : 0.0f;
	if ( reliableScrolls > 0 && worldMagnitudeSum > 1.0e-4f )
	{
		body.legacy.worldAgreement = Q_min(1.0f, VectorLength(worldSum) / worldMagnitudeSum);
		VectorScale(worldSum, 1.0f / reliableScrolls, body.legacy.worldVelocity);
		body.legacy.worldSpeed = VectorLength(body.legacy.worldVelocity);
		body.legacy.confidence = body.legacy.uvCoverage >= 0.8f &&
			body.legacy.worldAgreement >= 0.72f && reliableScrolls == (int)body.legacy.scrolls.size() ?
			WFLOW_CONFIDENCE_HIGH : WFLOW_CONFIDENCE_AMBIGUOUS;
	}
	else if ( !body.legacy.scrolls.empty() )
		body.legacy.confidence = WFLOW_CONFIDENCE_AMBIGUOUS;
}

static bool R_WaterRuleMatches( const waterBodyRule_t& rule, const waterBody_t& body )
{
	if ( rule.id >= 0 && rule.id != body.id ) return false;
	if ( !rule.shader.empty() )
	{
		bool matched = false;
		for ( const std::string& shader : body.shaders )
			if ( R_WaterPatternMatches(rule.shader, shader.c_str()) ) matched = true;
		if ( !matched ) return false;
	}
	if ( rule.hasPoint )
		for ( int k = 0; k < 3; k++ )
			if ( rule.point[k] < body.bounds[0][k] || rule.point[k] > body.bounds[1][k] ) return false;
	if ( rule.hasBounds && !R_WaterBoundsTouch(body.bounds, rule.bounds, 0) ) return false;
	return true;
}

static void R_WaterResolveBody( waterBody_t& body )
{
	body.dynamics = 0;
	body.decision = "fallback";
	body.waveMultiplier = body.foamMultiplier = body.interactionMultiplier = 1.0f;
	VectorClear(body.flowDirection);
	VectorClear(body.flowVelocity);
	body.flowSpeed = 0.0f;
	body.flowSource = WFLOW_NONE;
	body.flowConfidence = body.legacy.confidence;
	body.hasExplicitWind = false;
	VectorClear(body.explicitWindDirection);
	body.explicitWindStrength = 0.0f;
	body.wind.initialized = false;
	if ( body.liquidClass == LIQUID_LAVA )
		body.dynamics = 9, body.decision = "auto: lava contents";
	else if ( body.liquidClass == LIQUID_SLIME || body.opticsClass == LIQUID_SLIME )
		body.dynamics = 6, body.decision = "auto: slime contents/optics";
	else if ( body.shaders.count("textures/common/water2_still") )
		body.dynamics = 1, body.decision = "stock: still shader";
	else if ( body.shaders.count("textures/h_evil/wfall") && body.planeNormal[2] < .7f )
		body.dynamics = 7, body.decision = "stock: waterfall shader and orientation";
	// Resolve the body profile and its non-flow multipliers first. A rule may
	// intentionally turn an otherwise moving stock material into a still pool.
	for ( const waterBodyRule_t& rule : s_water.bodyRules )
	{
		if ( !R_WaterRuleMatches(rule, body) ) continue;
		if ( rule.hasDynamics )
		{
			body.dynamics = rule.dynamics;
			body.decision = rule.runtime ? "runtime override" : "explicit env.json";
		}
		if ( !rule.runtime )
		{
			body.waveMultiplier = rule.wave;
			body.foamMultiplier = rule.foam;
			body.interactionMultiplier = rule.interaction;
		}
		if ( rule.hasWind )
		{
			body.hasExplicitWind = true;
			VectorCopy(rule.windDirection, body.explicitWindDirection);
			body.explicitWindStrength = rule.windStrength;
		}
	}

	const bool suppressAutomatic = body.dynamics == 1 || body.dynamics == 2 || body.dynamics == 3 ||
		body.dynamics == 6 || body.dynamics == 9;
	if ( !suppressAutomatic && body.legacy.confidence == WFLOW_CONFIDENCE_HIGH && body.legacy.worldSpeed > 0.0f )
	{
		VectorCopy(body.legacy.worldVelocity, body.flowDirection);
		body.flowSpeed = VectorNormalize(body.flowDirection);
		body.flowSpeed = Q_min(body.flowSpeed * (s_dynamics[body.dynamics].flow > 0.0f ?
			s_dynamics[body.dynamics].flow : 1.0f), 256.0f);
		VectorScale(body.flowDirection, body.flowSpeed, body.flowVelocity);
		body.flowSource = WFLOW_LEGACY;
		body.flowConfidence = WFLOW_CONFIDENCE_HIGH;
	}

	// Explicit rules always win, including an explicit speed of zero.
	for ( const waterBodyRule_t& rule : s_water.bodyRules )
	{
		if ( !rule.hasFlow || !R_WaterRuleMatches(rule, body) ) continue;
		VectorCopy(rule.flowDirection, body.flowDirection);
		const bool hasDirection = VectorNormalize(body.flowDirection) >= 1.0e-6f;
		if ( !hasDirection ) VectorClear(body.flowDirection);
		body.flowSpeed = rule.flowUsesProfileSpeed ? Q_max(0.0f, s_dynamics[body.dynamics].flow * 32.0f) :
			Q_max(0.0f, rule.flowSpeed);
		if ( !hasDirection ) body.flowSpeed = 0.0f;
		VectorScale(body.flowDirection, body.flowSpeed, body.flowVelocity);
		body.flowSource = rule.flowUsesProfileSpeed ? WFLOW_PROFILE : WFLOW_EXPLICIT;
		body.flowConfidence = WFLOW_CONFIDENCE_HIGH;
	}
}

static bool R_WaterfallRuleMatches(const waterfallRule_t& rule, const waterBody_t& body)
{
	if (rule.id >= 0 && rule.id != body.id) return false;
	if (!rule.shader.empty())
	{
		bool matched = false;
		for (const std::string& shader : body.shaders)
			if (R_WaterPatternMatches(rule.shader, shader.c_str())) matched = true;
		if (!matched) return false;
	}
	if (rule.hasPoint)
		for (int axis = 0; axis < 3; ++axis)
			if (rule.point[axis] < body.bounds[0][axis] || rule.point[axis] > body.bounds[1][axis]) return false;
	if (rule.hasBounds && !R_WaterBoundsTouch(body.bounds, rule.bounds, 0.0f)) return false;
	return true;
}

static void R_WaterResolveWaterfall(waterBody_t& body)
{
	body.waterfall = false;
	body.waterfallProfile = WFPROFILE_MEDIUM;
	body.waterfallDecision = "legacy fallback";
	const bool steep = fabsf(body.planeNormal[2]) < 0.7f;
	// Exact mapping is intentionally narrow and comes from the installed stock
	// asset audit. It is not a filename-substring classifier.
	if (steep && body.shaders.count("textures/h_evil/wfall"))
	{
		body.waterfall = true;
		body.waterfallProfile = (body.bounds[1][2] - body.bounds[0][2] > 1024.0f) ? WFPROFILE_HEAVY : WFPROFILE_MEDIUM;
		body.waterfallDecision = "audited stock mapping";
	}
	for (const waterfallRule_t& rule : s_water.waterfallRules)
	{
		if (!R_WaterfallRuleMatches(rule, body)) continue;
		body.waterfall = rule.enabled && steep;
		if (rule.hasProfile) body.waterfallProfile = rule.profile;
		body.waterfallValues = rule.values;
		body.waterfallDecision = steep ? "explicit env.json" : "rejected: surface is not steep";
	}
	if (!body.waterfall) return;
	body.waterfallValues = s_waterfallProfiles[body.waterfallProfile];
	for (const waterfallRule_t& rule : s_water.waterfallRules)
	{
		if (!R_WaterfallRuleMatches(rule, body) || !rule.enabled) continue;
		if (rule.authored & (1u << 0)) body.waterfallValues.flowSpeed = rule.values.flowSpeed;
		if (rule.authored & (1u << 1)) body.waterfallValues.thickness = rule.values.thickness;
		if (rule.authored & (1u << 2)) body.waterfallValues.breakup = rule.values.breakup;
		if (rule.authored & (1u << 3)) body.waterfallValues.normalScale = rule.values.normalScale;
		if (rule.authored & (1u << 4)) body.waterfallValues.aeration = rule.values.aeration;
		if (rule.authored & (1u << 5)) body.waterfallValues.opacity = rule.values.opacity;
		if (rule.authored & (1u << 6)) body.waterfallValues.spray = rule.values.spray;
		if (rule.authored & (1u << 7)) body.waterfallValues.impact = rule.values.impact;
	}
	vec3_t gravity = { 0, 0, -1 };
	VectorMA(gravity, -DotProduct(gravity, body.planeNormal), body.planeNormal, gravity);
	VectorNormalize(gravity);
	if (body.flowSpeed > 0.0f && DotProduct(body.flowDirection, gravity) > 0.35f)
	{
		VectorScale(body.flowDirection, 0.7f, body.fallDirection);
		VectorMA(body.fallDirection, 0.3f, gravity, body.fallDirection);
		VectorNormalize(body.fallDirection);
	}
	else VectorCopy(gravity, body.fallDirection);
	body.flowSpeed = body.waterfallValues.flowSpeed;
	VectorCopy(body.fallDirection, body.flowDirection);
	VectorScale(body.flowDirection, body.flowSpeed, body.flowVelocity);
	body.flowSource = body.flowSource == WFLOW_EXPLICIT ? WFLOW_EXPLICIT : WFLOW_PROFILE;
	body.flowConfidence = WFLOW_CONFIDENCE_HIGH;
	vec3_t across; CrossProduct(body.planeNormal, body.fallDirection, across); VectorNormalize(across);
	body.fallMin = 1.0e30f; body.fallMax = -1.0e30f;
	float acrossMin = 1.0e30f, acrossMax = -1.0e30f;
	for (int corner = 0; corner < 8; ++corner)
	{
		vec3_t p = { body.bounds[(corner >> 0) & 1][0], body.bounds[(corner >> 1) & 1][1], body.bounds[(corner >> 2) & 1][2] };
		const float along = DotProduct(p, body.fallDirection), side = DotProduct(p, across);
		body.fallMin = MIN(body.fallMin, along); body.fallMax = MAX(body.fallMax, along);
		acrossMin = MIN(acrossMin, side); acrossMax = MAX(acrossMax, side);
	}
	VectorAdd(body.bounds[0], body.bounds[1], body.impactCenter); VectorScale(body.impactCenter, 0.5f, body.impactCenter);
	VectorMA(body.impactCenter, body.fallMax - DotProduct(body.impactCenter, body.fallDirection), body.fallDirection, body.impactCenter);
	body.impactWidth = acrossMax - acrossMin;
}

static void R_WaterApplyWaterfallBodyKeys(waterBody_t& body)
{
	if (!s_water.world || body.id >= 4096) return;
	for (int surfaceNum : body.surfaces)
	{
		msurface_t& surface = const_cast<world_t *>(s_water.world)->surfaces[surfaceNum];
		const bool wasWaterfall = (surface.waterKey & WATERKEY_WATERFALL) != 0;
		surface.waterKey &= ~WATERKEY_WATERFALL;
		if (wasWaterfall)
		{
			bool horizontal = false;
			for (const waterSurfaceRecord_t& record : s_water.surfaces)
				if (record.surfaceNum == surfaceNum) { horizontal = record.modern != qfalse; break; }
			if (!horizontal) surface.waterKey &= ~WATERKEY_INTERFACE;
		}
		if (body.waterfall && r_waterfall && r_waterfall->integer)
		{
			surface.waterKey |= WATERKEY_INTERFACE | WATERKEY_WATERFALL | ((uint32_t)body.opticsClass << 4);
			if (surface.shader) surface.shader->waterSurface = 1;
		}
	}
}

static void R_WaterBuildBodies( void )
{
	const int start = ri.Milliseconds();
	s_water.bodies.clear();
	// Candidate records are in BSP surface order. Only physically touching, similarly
	// oriented surfaces of the same static model and liquid class can share a body.
	std::vector<int> candidates;
	for ( int i = 0; i < (int)s_water.surfaces.size(); i++ )
		if ( R_WaterLiquidClass(s_water.surfaces[i].contents) >= 0 ||
			(s_water.surfaces[i].brush >= 0 && s_water.surfaces[i].brushSide[2] > .7f &&
			 s_water.surfaces[i].orient == WORIENT_UP) )
			candidates.push_back(i);
	const int count = (int)candidates.size();
	std::vector<int> parent(count);
	for ( int i = 0; i < count; i++ ) parent[i] = i;
	auto root = [&]( int n ) { while ( parent[n] != n ) n = parent[n]; return n; };
	for ( int i = 0; i < count; i++ )
	{
		const waterSurfaceRecord_t& a = s_water.surfaces[candidates[i]];
		for ( int j = i + 1; j < count; j++ )
		{
			const waterSurfaceRecord_t& b = s_water.surfaces[candidates[j]];
			if ( a.model != b.model || a.model < 0 ||
				(a.brush >= 0 ? s_water.brushes[a.brush].liquidClass : R_WaterLiquidClass(a.contents)) !=
				(b.brush >= 0 ? s_water.brushes[b.brush].liquidClass : R_WaterLiquidClass(b.contents)) ) continue;
			if ( a.brush >= 0 && b.brush >= 0 &&
				(a.brush == b.brush || R_WaterBoundsTouch(s_water.brushes[a.brush].bounds, s_water.brushes[b.brush].bounds, 0.5f)) )
			{
				parent[root(j)] = root(i);
				continue;
			}
			if ( a.orient != b.orient ) continue;
			if ( !R_WaterBoundsTouch(a.bounds, b.bounds, 2.0f) ) continue;
			if ( DotProduct(a.normal, b.normal) < .94f ) continue;
			if ( fabsf(DotProduct(a.normal, b.bounds[0]) - DotProduct(a.normal, a.bounds[0])) > 4.0f ) continue;
			parent[root(j)] = root(i);
		}
	}
	std::unordered_map<int, int> roots;
	for ( int i = 0; i < count; i++ )
	{
		const waterSurfaceRecord_t& s = s_water.surfaces[candidates[i]];
		const int cls = s.brush >= 0 ? s_water.brushes[s.brush].liquidClass : R_WaterLiquidClass(s.contents);
		if ( cls < 0 ) continue;
		const int key = root(i);
		if ( !roots.count(key) )
		{
			waterBody_t body = {};
			body.id = (int)s_water.bodies.size() + 1;
			body.model = s.model;
			body.liquidClass = cls;
			body.opticsClass = cls == LIQUID_LAVA ? LIQUID_LAVA : s.liquidClass;
			body.waveMultiplier = body.foamMultiplier = body.interactionMultiplier = 1;
			ClearBounds(body.bounds[0], body.bounds[1]);
			VectorCopy(s.normal, body.planeNormal);
			body.planeDist = DotProduct(s.normal, s.bounds[0]);
			roots[key] = (int)s_water.bodies.size();
			s_water.bodies.push_back(body);
		}
		waterBody_t& body = s_water.bodies[roots[key]];
		body.surfaces.push_back(s.surfaceNum);
		body.shaders.insert(s.shader->name);
		body.area += s.area;
		AddPointToBounds(s.bounds[0], body.bounds[0], body.bounds[1]);
		AddPointToBounds(s.bounds[1], body.bounds[0], body.bounds[1]);
		if ( s.brush >= 0 && std::find(body.brushes.begin(), body.brushes.end(), s.brush) == body.brushes.end() )
			body.brushes.push_back(s.brush);
	}
	for ( waterBody_t& body : s_water.bodies )
	{
		float depthSum = 0;
		for ( int bi : body.brushes )
		{
			const waterBrushRecord_t& brush = s_water.brushes[bi];
			const float depth = Q_max(0.0f, brush.bounds[1][2] - brush.bounds[0][2]);
			depthSum += depth;
			body.depthDeepest = Q_max(body.depthDeepest, depth);
		}
		body.depthAverage = body.brushes.empty() ? 0 : depthSum / body.brushes.size();
		R_WaterBodyMotion(body);
		R_WaterResolveBody(body);
		R_WaterResolveWaterfall(body);
		// Reserve bits 20..31 for a map-local body ID. Zero means no matched body.
		if ( body.id < 4096 )
			for ( int surfaceNum : body.surfaces )
			{
				msurface_t& surface = const_cast<world_t *>(s_water.world)->surfaces[surfaceNum];
				surface.waterKey |= (uint32_t)body.id << 20;
				if (body.waterfall && r_waterfall && r_waterfall->integer)
				{
					surface.waterKey |= WATERKEY_INTERFACE | WATERKEY_WATERFALL | ((uint32_t)body.opticsClass << 4);
					if (surface.shader) surface.shader->waterSurface = 1;
				}
			}
		R_WaterApplyWaterfallBodyKeys(body);
	}
	s_water.bodyMsec = (float)(ri.Milliseconds() - start);
}

static void R_WaterRefreshBodies( void )
{
	if ( s_water.bodies.empty() ) return;
	for ( waterBody_t& body : s_water.bodies )
	{
		if ( body.id < 4096 )
			for ( int surfaceNum : body.surfaces )
				const_cast<world_t *>(s_water.world)->surfaces[surfaceNum].waterKey |= (uint32_t)body.id << 20;
		if ( body.liquidClass != LIQUID_LAVA && !body.surfaces.empty() )
		{
			for ( const waterSurfaceRecord_t& surface : s_water.surfaces )
				if ( surface.surfaceNum == body.surfaces[0] )
				{
					body.opticsClass = surface.liquidClass;
					break;
				}
		}
		R_WaterResolveBody(body);
		R_WaterResolveWaterfall(body);
		if (body.id < 4096 && body.waterfall && r_waterfall && r_waterfall->integer)
			for (int surfaceNum : body.surfaces)
			{
				msurface_t& surface = const_cast<world_t *>(s_water.world)->surfaces[surfaceNum];
				surface.waterKey |= WATERKEY_INTERFACE | WATERKEY_WATERFALL | ((uint32_t)body.opticsClass << 4);
				if (surface.shader) surface.shader->waterSurface = 1;
			}
		R_WaterApplyWaterfallBodyKeys(body);
	}
}

static bool R_WaterJsonVec( const char *object, const char *end, const char *name, float *out, int count )
{
	const char *value = JSON_ObjectGetNamedValue(object, end, name);
	if ( !value || JSON_ValueGetType(value, end) != JSONTYPE_ARRAY ) return false;
	const char *items[3] = {};
	if ( JSON_ArrayGetIndex(value, end, items, count) < count ) return false;
	for ( int i = 0; i < count; i++ ) out[i] = JSON_ValueGetFloat(items[i], end);
	return true;
}

static bool R_WaterParseWindJson(const char *object, const char *end, vec3_t direction, float& strength)
{
	if (!object || JSON_ValueGetType(object, end) != JSONTYPE_OBJECT) return false;
	vec3_t value = {};
	bool velocity = R_WaterJsonVec(object, end, "Velocity", value, 3);
	if (!velocity && !R_WaterJsonVec(object, end, "Direction", value, 3)) return false;
	const float length = VectorNormalize(value);
	if (length < 1.0e-5f) return false;
	VectorCopy(value, direction);
	const char *strengthValue = JSON_ObjectGetNamedValue(object, end, "Strength");
	strength = strengthValue ? Com_Clamp(0.0f, 1.0f, JSON_ValueGetFloat(strengthValue, end)) :
		Com_Clamp(0.0f, 1.0f, velocity ? length / 3.0f : length);
	return strength > 0.0f;
}

static void R_WaterfallEmittersParseJson( const char *json, const char *end, const char *filename )
{
	const char *array = JSON_ObjectGetNamedValue(json, end, "WaterfallEmitters");
	if (!array) return;
	if (JSON_ValueGetType(array, end) != JSONTYPE_ARRAY)
	{
		ri.Printf(PRINT_WARNING, "%s: WaterfallEmitters is not an array\n", filename);
		return;
	}
	const int count = Q_min((int)JSON_ArrayGetIndex(array, end, NULL, 0), 256);
	for (int i = 0; i < count; ++i)
	{
		const char *entry = JSON_ArrayGetValue(array, end, i);
		if (!entry || JSON_ValueGetType(entry, end) != JSONTYPE_OBJECT) continue;
		waterfallEmitterRule_t rule;
		const char *selector = JSON_ObjectGetNamedValue(entry, end, "Selector");
		if (!selector || JSON_ValueGetType(selector, end) != JSONTYPE_OBJECT) selector = entry;
		const char *value = JSON_ObjectGetNamedValue(selector, end, "Shader");
		char shader[MAX_QPATH] = "";
		if (value) JSON_ValueGetString(value, end, shader, sizeof(shader));
		value = JSON_ObjectGetNamedValue(selector, end, "ShaderPrefix");
		if (value && !shader[0]) { JSON_ValueGetString(value, end, shader, sizeof(shader) - 1); Q_strcat(shader, sizeof(shader), "*"); }
		Q_strlwr(shader); rule.selector.shader = shader;
		value = JSON_ObjectGetNamedValue(selector, end, "BodyId");
		if (value) rule.selector.id = (int)JSON_ValueGetFloat(value, end);
		rule.selector.hasPoint = R_WaterJsonVec(selector, end, "Point", rule.selector.point, 3);
		const char *bounds = JSON_ObjectGetNamedValue(selector, end, "Bounds");
		if (bounds && JSON_ValueGetType(bounds, end) == JSONTYPE_OBJECT)
			rule.selector.hasBounds = R_WaterJsonVec(bounds, end, "Mins", rule.selector.bounds[0], 3) &&
				R_WaterJsonVec(bounds, end, "Maxs", rule.selector.bounds[1], 3);
		value = JSON_ObjectGetNamedValue(entry, end, "ExistingFx");
		rule.existingFx = value && JSON_ValueGetFloat(value, end) != 0.0f;
		rule.hasOrigin = R_WaterJsonVec(entry, end, "Origin", rule.origin, 3);
		value = JSON_ObjectGetNamedValue(entry, end, "Radius"); if (value) rule.radius = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "Density"); if (value) rule.density = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "LensStrength"); if (value) rule.lens = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		if (rule.selector.shader.empty() && rule.selector.id < 0 && !rule.selector.hasPoint && !rule.selector.hasBounds)
		{
			ri.Printf(PRINT_WARNING, "%s: WaterfallEmitters[%d] needs a classified-waterfall selector\n", filename, i);
			continue;
		}
		s_water.waterfallEmitterRules.push_back(rule);
	}
}

void R_WaterBodiesLoadJson( world_t *world, const char *json, const char *end, const char *filename )
{
	if ( world != s_water.world ) return;
	s_water.bodyRules.clear();
	s_water.waterfallRules.clear();
	s_water.waterfallEmitterRules.clear();
	s_water.waterfallImpacts.clear();
	s_water.mapWindValid = false;
	VectorClear(s_water.mapWindDirection);
	s_water.mapWindStrength = 0.0f;
	const char *mapWind = JSON_ObjectGetNamedValue(json, end, "Wind");
	if (!mapWind) mapWind = JSON_ObjectGetNamedValue(json, end, "WaterWind");
	s_water.mapWindValid = R_WaterParseWindJson(mapWind, end,
		s_water.mapWindDirection, s_water.mapWindStrength);
	const char *array = JSON_ObjectGetNamedValue(json, end, "WaterBodies");
	if ( array && JSON_ValueGetType(array, end) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: WaterBodies is not an array\n", filename);
	}
	if ( array && JSON_ValueGetType(array, end) == JSONTYPE_ARRAY )
	{
	 const int count = Q_min((int)JSON_ArrayGetIndex(array, end, NULL, 0), 256);
	 for ( int i = 0; i < count; i++ )
	 {
		const char *entry = JSON_ArrayGetValue(array, end, i);
		if ( !entry || JSON_ValueGetType(entry, end) != JSONTYPE_OBJECT ) continue;
		waterBodyRule_t rule = {};
		rule.id = -1;
		rule.wave = rule.foam = rule.interaction = 1;
		const char *selector = JSON_ObjectGetNamedValue(entry, end, "Selector");
		if ( !selector || JSON_ValueGetType(selector, end) != JSONTYPE_OBJECT ) selector = entry;
		const char *value = JSON_ObjectGetNamedValue(selector, end, "Shader");
		char shader[MAX_QPATH] = "";
		if ( value ) JSON_ValueGetString(value, end, shader, sizeof(shader));
		value = JSON_ObjectGetNamedValue(selector, end, "ShaderPrefix");
		if ( value && !shader[0] )
		{
			JSON_ValueGetString(value, end, shader, sizeof(shader) - 1);
			Q_strcat(shader, sizeof(shader), "*");
		}
		Q_strlwr(shader);
		rule.shader = shader;
		value = JSON_ObjectGetNamedValue(selector, end, "BodyId");
		if ( value ) rule.id = (int)JSON_ValueGetFloat(value, end);
		rule.hasPoint = R_WaterJsonVec(selector, end, "Point", rule.point, 3) ||
			R_WaterJsonVec(selector, end, "Origin", rule.point, 3);
		const char *bounds = JSON_ObjectGetNamedValue(selector, end, "Bounds");
		if ( bounds && JSON_ValueGetType(bounds, end) == JSONTYPE_OBJECT )
			rule.hasBounds = R_WaterJsonVec(bounds, end, "Mins", rule.bounds[0], 3) &&
				R_WaterJsonVec(bounds, end, "Maxs", rule.bounds[1], 3);
		char profile[32] = "";
		value = JSON_ObjectGetNamedValue(entry, end, "DynamicsProfile");
		if ( value ) JSON_ValueGetString(value, end, profile, sizeof(profile));
		rule.dynamics = -1;
		for ( int d = 0; d < (int)(sizeof(s_dynamics) / sizeof(s_dynamics[0])); d++ )
			if ( !Q_stricmp(profile, s_dynamics[d].name) ) rule.dynamics = d;
		rule.hasDynamics = rule.dynamics >= 0;
		const char *windValue = JSON_ObjectGetNamedValue(entry, end, "Wind");
		rule.hasWind = R_WaterParseWindJson(windValue, end, rule.windDirection, rule.windStrength);
		const char *flowValue = JSON_ObjectGetNamedValue(entry, end, "Flow");
		if ( flowValue && JSON_ValueGetType(flowValue, end) == JSONTYPE_OBJECT )
		{
			rule.hasFlow = R_WaterJsonVec(flowValue, end, "Direction", rule.flowDirection, 3);
			value = JSON_ObjectGetNamedValue(flowValue, end, "Speed");
			rule.flowUsesProfileSpeed = value == nullptr;
			rule.flowSpeed = value ? Q_max(0.0f, JSON_ValueGetFloat(value, end)) : 0.0f;
		}
		else
		{
			float oldFlow[2] = {};
			rule.hasFlow = R_WaterJsonVec(entry, end, "Flow", oldFlow, 2);
			if ( rule.hasFlow )
			{
				VectorSet(rule.flowDirection, oldFlow[0], oldFlow[1], 0.0f);
				rule.flowSpeed = VectorLength(rule.flowDirection);
			}
		}
		value = JSON_ObjectGetNamedValue(entry, end, "FlowSpeed");
		if ( value && rule.hasFlow )
		{
			rule.flowSpeed = Q_max(0.0f, JSON_ValueGetFloat(value, end));
			rule.flowUsesProfileSpeed = false;
		}
		if ( rule.hasFlow && VectorNormalize(rule.flowDirection) < 1.0e-6f ) VectorClear(rule.flowDirection);
		if ( (!rule.hasDynamics && !rule.hasFlow && !rule.hasWind) ||
			(rule.shader.empty() && rule.id < 0 && !rule.hasPoint && !rule.hasBounds) )
		{
			ri.Printf(PRINT_WARNING, "%s: WaterBodies[%d] needs a selector and DynamicsProfile, Flow and/or Wind\n", filename, i);
			continue;
		}
		value = JSON_ObjectGetNamedValue(entry, end, "WaveMultiplier");
		if ( value ) rule.wave = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "FoamMultiplier");
		if ( value ) rule.foam = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		value = JSON_ObjectGetNamedValue(entry, end, "InteractionMultiplier");
		if ( value ) rule.interaction = Q_max(0.0f, JSON_ValueGetFloat(value, end));
		s_water.bodyRules.push_back(rule);
	 }
	}

	array = JSON_ObjectGetNamedValue(json, end, "Waterfalls");
	if (array && JSON_ValueGetType(array, end) != JSONTYPE_ARRAY)
		ri.Printf(PRINT_WARNING, "%s: Waterfalls is not an array\n", filename);
	if (array && JSON_ValueGetType(array, end) == JSONTYPE_ARRAY)
	{
		const int count = Q_min((int)JSON_ArrayGetIndex(array, end, NULL, 0), 256);
		for (int i = 0; i < count; ++i)
		{
			const char *entry = JSON_ArrayGetValue(array, end, i);
			if (!entry || JSON_ValueGetType(entry, end) != JSONTYPE_OBJECT) continue;
			waterfallRule_t rule;
			const char *selector = JSON_ObjectGetNamedValue(entry, end, "Selector");
			if (!selector || JSON_ValueGetType(selector, end) != JSONTYPE_OBJECT) selector = entry;
			const char *value = JSON_ObjectGetNamedValue(selector, end, "Shader");
			char shader[MAX_QPATH] = "";
			if (value) JSON_ValueGetString(value, end, shader, sizeof(shader));
			value = JSON_ObjectGetNamedValue(selector, end, "ShaderPrefix");
			if (value && !shader[0]) { JSON_ValueGetString(value, end, shader, sizeof(shader) - 1); Q_strcat(shader, sizeof(shader), "*"); }
			Q_strlwr(shader); rule.shader = shader;
			value = JSON_ObjectGetNamedValue(selector, end, "BodyId");
			if (value) rule.id = (int)JSON_ValueGetFloat(value, end);
			rule.hasPoint = R_WaterJsonVec(selector, end, "Point", rule.point, 3) || R_WaterJsonVec(selector, end, "Origin", rule.point, 3);
			const char *bounds = JSON_ObjectGetNamedValue(selector, end, "Bounds");
			if (bounds && JSON_ValueGetType(bounds, end) == JSONTYPE_OBJECT)
				rule.hasBounds = R_WaterJsonVec(bounds, end, "Mins", rule.bounds[0], 3) && R_WaterJsonVec(bounds, end, "Maxs", rule.bounds[1], 3);
			char profile[32] = "";
			value = JSON_ObjectGetNamedValue(entry, end, "Profile");
			if (value) JSON_ValueGetString(value, end, profile, sizeof(profile));
			for (int p = 0; p < WFPROFILE_COUNT; ++p) if (!Q_stricmp(profile, s_waterfallProfiles[p].name)) { rule.profile = p; rule.hasProfile = true; }
			rule.values = s_waterfallProfiles[rule.profile];
			value = JSON_ObjectGetNamedValue(entry, end, "Enabled"); if (value) rule.enabled = JSON_ValueGetFloat(value, end) != 0.0f;
			const char *names[] = { "FlowSpeed", "SheetThickness", "Breakup", "NormalScale", "Aeration", "Opacity", "SprayStrength", "ImpactStrength" };
			float *fields[] = { &rule.values.flowSpeed, &rule.values.thickness, &rule.values.breakup, &rule.values.normalScale, &rule.values.aeration, &rule.values.opacity, &rule.values.spray, &rule.values.impact };
			for (int f = 0; f < 8; ++f) if ((value = JSON_ObjectGetNamedValue(entry, end, names[f])) != NULL) { *fields[f] = Q_max(0.0f, JSON_ValueGetFloat(value, end)); rule.authored |= 1u << f; }
			if (rule.shader.empty() && rule.id < 0 && !rule.hasPoint && !rule.hasBounds)
			{
				ri.Printf(PRINT_WARNING, "%s: Waterfalls[%d] needs a selector\n", filename, i);
				continue;
			}
			s_water.waterfallRules.push_back(rule);
		}
	}
	R_WaterfallEmittersParseJson(json, end, filename);
	for ( waterBody_t& body : s_water.bodies ) { R_WaterResolveBody(body); R_WaterResolveWaterfall(body); R_WaterApplyWaterfallBodyKeys(body); }
	ri.Printf(PRINT_DEVELOPER, "%s: %d water dynamics rule%s\n", filename,
		(int)s_water.bodyRules.size(), s_water.bodyRules.size() == 1 ? "" : "s");
	ri.Printf(PRINT_DEVELOPER, "%s: %d waterfall rule%s\n", filename,
		(int)s_water.waterfallRules.size(), s_water.waterfallRules.size() == 1 ? "" : "s");
}

void R_WaterfallEmittersFinalize(world_t *world)
{
	if (!world || world != s_water.world) return;
	world->numWaterfallFogVolumes = 0;
	world->waterfallFogVolumes = NULL;
	world->numWaterfallLensEmitters = 0;
	world->waterfallLensEmitters = NULL;

	// A dedicated sidecar is additive. It is deliberately loaded after env.json
	// so a project PK3 need not copy and replace Cubemaps/FogVolumes.
	char filename[MAX_QPATH];
	Com_sprintf(filename, sizeof(filename), "cubemaps/%s/waterfalls.json", world->baseName);
	void *file = NULL;
	const int length = ri.FS_ReadFile(filename, &file);
	const bool sidecarLoaded = file != NULL;
	if (file)
	{
		const char *json = (const char *)file;
		const char *end = json + length;
		if (JSON_ValueGetType(json, end) == JSONTYPE_OBJECT)
			R_WaterfallEmittersParseJson(json, end, filename);
		else
			ri.Printf(PRINT_WARNING, "%s: root must be an object\n", filename);
		ri.FS_FreeFile(file);
	}

	// Resolve each sheet base onto one horizontal modern-water body.  A source
	// outside every local surface is rejected instead of perturbing the whole
	// liquid brush; maps with an ambiguous base can provide an explicit Origin.
	s_water.waterfallImpacts.clear();
	std::vector<std::array<float, 3>> existingFxOrigins;
	if (world->entityString)
	{
#ifdef REND2_SP
		// COM_ParseExt uses the shared parse-session stack even for an in-memory
		// entity lump.  Keep it balanced on normal, missing-sidecar and early map
		// shutdown paths.
		COM_ParseSession parseSession;
#endif
		const char *cursor = world->entityString;
		while (cursor)
		{
			const char *token = COM_ParseExt(&cursor, qtrue);
			if (!token[0]) break;
			if (token[0] != '{') continue;
			char classname[MAX_QPATH] = "", effect[MAX_QPATH] = "", origin[128] = "";
			while (cursor)
			{
				token = COM_ParseExt(&cursor, qtrue);
				if (!token[0] || token[0] == '}') break;
				char key[MAX_TOKEN_CHARS]; Q_strncpyz(key, token, sizeof(key));
				token = COM_ParseExt(&cursor, qtrue);
				if (!token[0]) break;
				if (!Q_stricmp(key, "classname")) Q_strncpyz(classname, token, sizeof(classname));
				else if (!Q_stricmp(key, "fxFile")) Q_strncpyz(effect, token, sizeof(effect));
				else if (!Q_stricmp(key, "origin")) Q_strncpyz(origin, token, sizeof(origin));
			}
			const bool waterfallFx = !Q_stricmp(effect, "effects/env/waterfall_mist.efx") ||
				!Q_stricmp(effect, "effects/env/waterfall_mist") ||
				!Q_stricmp(effect, "env/waterfall_mist.efx") || !Q_stricmp(effect, "env/waterfall_mist");
			std::array<float, 3> parsed = {};
			if (!Q_stricmp(classname, "fx_runner") && waterfallFx &&
				sscanf(origin, "%f %f %f", &parsed[0], &parsed[1], &parsed[2]) == 3)
				existingFxOrigins.push_back(parsed);
		}
	}
	auto closestOnBody = [&](const waterBody_t& pool, const vec3_t point, float radius,
		vec3_t resolved) -> bool
	{
		float bestDistance2 = 1.0e30f;
		vec2_t best = { point[0], point[1] };
		for (int surfaceNum : pool.surfaces)
		{
			const msurface_t& ms = world->surfaces[surfaceNum];
			if (!(ms.waterKey & WATERKEY_INTERFACE) || !ms.data ||
				!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES)) continue;
			const srfBspSurface_t *surface = (const srfBspSurface_t *)ms.data;
			for (int ti = 0; ti + 2 < surface->numIndexes; ti += 3)
			{
				const int indices[3] = { (int)surface->indexes[ti], (int)surface->indexes[ti + 1], (int)surface->indexes[ti + 2] };
				if (indices[0] < 0 || indices[1] < 0 || indices[2] < 0 ||
					indices[0] >= surface->numVerts || indices[1] >= surface->numVerts || indices[2] >= surface->numVerts) continue;
				const vec3_t& a = surface->verts[indices[0]].xyz;
				const vec3_t& b = surface->verts[indices[1]].xyz;
				const vec3_t& c = surface->verts[indices[2]].xyz;
				if (R_WaterInteractionPointInTriangle(point[0], point[1], a, b, c))
				{
					best[0] = point[0]; best[1] = point[1]; bestDistance2 = 0.0f;
				}
				if (bestDistance2 == 0.0f) break;
				const vec3_t *edge[3][2] = { { &a, &b }, { &b, &c }, { &c, &a } };
				for (int e = 0; e < 3; ++e)
				{
					const float ex = (*edge[e][1])[0] - (*edge[e][0])[0];
					const float ey = (*edge[e][1])[1] - (*edge[e][0])[1];
					const float length2 = ex * ex + ey * ey;
					const float t = length2 > 1.0e-6f ? Com_Clamp(0.0f, 1.0f,
						((point[0] - (*edge[e][0])[0]) * ex + (point[1] - (*edge[e][0])[1]) * ey) / length2) : 0.0f;
					const float x = (*edge[e][0])[0] + ex * t, y = (*edge[e][0])[1] + ey * t;
					const float dx = x - point[0], dy = y - point[1], distance2 = dx * dx + dy * dy;
					if (distance2 < bestDistance2) { bestDistance2 = distance2; best[0] = x; best[1] = y; }
				}
			}
			if (bestDistance2 == 0.0f) break;
		}
		if (bestDistance2 > radius * radius) return false;
		resolved[0] = best[0]; resolved[1] = best[1];
		resolved[2] = (pool.planeDist - pool.planeNormal[0] * best[0] -
			pool.planeNormal[1] * best[1]) / pool.planeNormal[2];
		return fabsf(resolved[2] - point[2]) <= MAX(radius * 1.5f, 128.0f);
	};
	auto addImpact = [&](const waterBody_t& fall, const waterfallEmitterRule_t *rule)
	{
		waterfallImpactSource_t source;
		source.waterfallBodyId = fall.id;
		source.radius = rule && rule->radius > 0.0f ? rule->radius :
			Com_Clamp(72.0f, 384.0f, fall.impactWidth * 0.45f);
		source.strength = fall.waterfallValues.impact;
		if (rule && rule->hasOrigin)
		{
			VectorCopy(rule->origin, source.origin);
			source.originType = rule->existingFx ? WFIMPACT_EXISTING_FX : WFIMPACT_AUTHORED_EMITTER;
		}
		else
		{
			VectorCopy(fall.impactCenter, source.origin);
			if (rule && rule->existingFx && !existingFxOrigins.empty())
			{
				float bestFxDistance2 = 1.0e30f;
				for (const std::array<float, 3>& fxOrigin : existingFxOrigins)
				{
					vec3_t delta; VectorSubtract(fxOrigin.data(), fall.impactCenter, delta);
					const float distance2 = DotProduct(delta, delta);
					if (distance2 < bestFxDistance2)
					{
						bestFxDistance2 = distance2;
						VectorCopy(fxOrigin.data(), source.origin);
					}
				}
				const float maximumFxDistance = MAX(512.0f, source.radius * 4.0f);
				if (bestFxDistance2 <= maximumFxDistance * maximumFxDistance)
					source.originType = WFIMPACT_EXISTING_FX;
				else VectorCopy(fall.impactCenter, source.origin);
			}
		}

		waterBody_t *bestPool = nullptr;
		vec3_t bestOrigin = {};
		float bestDistance = 1.0e30f;
		for (waterBody_t& pool : s_water.bodies)
		{
			if (pool.waterfall || pool.liquidClass == LIQUID_LAVA || fabsf(pool.planeNormal[2]) < 0.7f) continue;
			vec3_t candidate;
			if (!closestOnBody(pool, source.origin, source.radius, candidate)) continue;
			const float dz = fabsf(candidate[2] - source.origin[2]);
			const float dx = candidate[0] - source.origin[0], dy = candidate[1] - source.origin[1];
			const float score = dz + sqrtf(dx * dx + dy * dy) * 2.0f;
			if (score < bestDistance) { bestDistance = score; bestPool = &pool; VectorCopy(candidate, bestOrigin); }
		}
		if (!bestPool)
		{
			source.poolBodyId = 0;
			source.randomState = (unsigned)fall.id * 747796405u + 1u;
			s_water.waterfallImpacts.push_back(source);
			if (r_waterfallImpactDebug->integer)
				ri.Printf(PRINT_ALL, "waterfall impact: sheet body %d at (%.1f %.1f %.1f) has no reliable pool; add WaterfallEmitters Origin\n",
					fall.id, source.origin[0], source.origin[1], source.origin[2]);
			return;
		}
		source.poolBodyId = bestPool->id;
		VectorCopy(bestOrigin, source.origin);
		source.randomState = (unsigned)fall.id * 747796405u + (unsigned)bestPool->id * 2891336453u + 1u;
		s_water.waterfallImpacts.push_back(source);
	};
	for (const waterBody_t& body : s_water.bodies)
	{
		if (!body.waterfall) continue;
		bool explicitOrigins = false;
		for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules)
			if (rule.hasOrigin && R_WaterfallRuleMatches(rule.selector, body))
			{
				addImpact(body, &rule);
				explicitOrigins = true;
			}
		if (!explicitOrigins)
		{
			const waterfallEmitterRule_t *matched = nullptr;
			for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules)
				if (R_WaterfallRuleMatches(rule.selector, body)) { matched = &rule; break; }
			addImpact(body, matched);
		}
	}

	std::vector<refFogVolume_t> fog;
	std::vector<lensWaterEmitter_t> lens;
	for (const waterBody_t& body : s_water.bodies)
	{
		if (!body.waterfall) continue;
		bool matched = false, existingFx = false;
		for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules)
			if (R_WaterfallRuleMatches(rule.selector, body)) { matched = true; existingFx |= rule.existingFx; }
		if (existingFx)
		{
			// The FX runner owns spray particles and its physicalized medium. Only
			// add the optional camera coupling; this is still the existing SPRAY input.
			const waterfallEmitterRule_t *owner = NULL;
			for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules)
				if (rule.existingFx && R_WaterfallRuleMatches(rule.selector, body)) { owner = &rule; break; }
			const float radius = owner && owner->radius > 0.0f ? owner->radius :
				Com_Clamp(72.0f, 384.0f, body.impactWidth * 0.45f);
			lensWaterEmitter_t emitter = {};
			if (owner && owner->hasOrigin) VectorCopy(owner->origin, emitter.origin); else VectorCopy(body.impactCenter, emitter.origin);
			emitter.radius = radius * 2.0f;
			emitter.strength = body.waterfallValues.spray * (owner ? owner->lens : 1.0f);
			emitter.type = LENSWATER_SPRAY;
			lens.push_back(emitter);
			continue;
		}

		auto add = [&](const waterfallEmitterRule_t *rule)
		{
			const float radius = rule && rule->radius > 0.0f ? rule->radius : Com_Clamp(72.0f, 384.0f, body.impactWidth * 0.45f);
			const float density = rule ? rule->density : 1.0f;
			const float lensStrength = rule ? rule->lens : 1.0f;
			refFogVolume_t volume = {};
			volume.id = 0x50000000 | (int)fog.size();
			volume.shape = FOGVOLUME_ELLIPSOID;
			if (rule && rule->hasOrigin) VectorCopy(rule->origin, volume.origin); else VectorCopy(body.impactCenter, volume.origin);
			VectorSet(volume.extents, radius, radius, radius * 0.55f);
			AxisClear(volume.axis);
			volume.depthForOpaque = density > 0.0f ? 720.0f / density : 1.0e9f;
			VectorSet(volume.color, 0.94f, 0.97f, 1.0f);
			volume.softness = 0.78f;
			volume.anisotropy = 0.62f;
			volume.flags = FOGVOLUME_ANISOTROPY;
			fog.push_back(volume);

			lensWaterEmitter_t emitter = {};
			VectorCopy(volume.origin, emitter.origin);
			emitter.radius = radius * 2.0f;
			emitter.strength = body.waterfallValues.spray * lensStrength;
			emitter.type = LENSWATER_SPRAY;
			lens.push_back(emitter);
		};

		if (matched)
		{
			for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules)
				if (!rule.existingFx && R_WaterfallRuleMatches(rule.selector, body)) add(&rule);
		}
		else add(NULL); // classified bottom-edge fallback, never an arbitrary point
	}

	if (!fog.empty())
	{
		world->waterfallFogVolumes = (refFogVolume_t *)Hunk_Alloc(fog.size() * sizeof(refFogVolume_t), h_low);
		memcpy(world->waterfallFogVolumes, fog.data(), fog.size() * sizeof(refFogVolume_t));
		world->numWaterfallFogVolumes = (int)fog.size();
	}
	if (!lens.empty())
	{
		world->waterfallLensEmitters = (lensWaterEmitter_t *)Hunk_Alloc(lens.size() * sizeof(lensWaterEmitter_t), h_low);
		memcpy(world->waterfallLensEmitters, lens.data(), lens.size() * sizeof(lensWaterEmitter_t));
		world->numWaterfallLensEmitters = (int)lens.size();
	}
	int existingFxOwners = 0;
	for (const waterfallEmitterRule_t& rule : s_water.waterfallEmitterRules) if (rule.existingFx) ++existingFxOwners;
	if (r_waterfallMistDebug->integer || sidecarLoaded)
		ri.Printf(PRINT_ALL, "%s: %d waterfall mist emitter%s (%d existing FX owner%s)\n", filename,
			world->numWaterfallFogVolumes, world->numWaterfallFogVolumes == 1 ? "" : "s",
			existingFxOwners, existingFxOwners == 1 ? "" : "s");
	if (r_waterfallImpactDebug->integer)
	{
		int resolved = 0;
		for (const waterfallImpactSource_t& source : s_water.waterfallImpacts) resolved += source.poolBodyId > 0;
		ri.Printf(PRINT_ALL, "%s: %d/%d resolved waterfall impact pool%s\n", filename,
			resolved, (int)s_water.waterfallImpacts.size(), s_water.waterfallImpacts.size() == 1 ? "" : "s");
	}
}

/*
============================================================

Commands

============================================================
*/

void R_WaterBodies_f( void )
{
	if ( !s_water.world || s_water.world != tr.world )
	{
		ri.Printf(PRINT_ALL, "r_waterBodies: no map\n");
		return;
	}
	const char *arg = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
	size_t memoryBytes = s_water.bodies.capacity() * sizeof(waterBody_t) +
		s_water.bodyRules.capacity() * sizeof(waterBodyRule_t);
	for ( const waterBody_t& b : s_water.bodies )
		memoryBytes += (b.surfaces.capacity() + b.brushes.capacity()) * sizeof(int) +
			b.legacy.scrolls.capacity() * sizeof(legacyWaterMotion_t::scroll_t) +
			(b.legacy.turb.capacity() + b.legacy.stretch.capacity()) * sizeof(legacyWaterMotion_t::wave_t) +
			b.shaders.size() * (sizeof(std::string) + 48);
	if ( !Q_stricmp(arg, "draw") )
	{
		s_water.bodyDebug = s_water.bodyDebug ? qfalse : qtrue;
		ri.Printf(PRINT_ALL, "r_waterBodies draw %s\n", s_water.bodyDebug ? "on" : "off");
	}
	else if ( !Q_stricmp(arg, "dump") )
	{
		// JSON Lines: one complete record per print, so log timestamps can be
		// stripped line by line and large custom maps never truncate one array.
		ri.Printf(PRINT_ALL, "{\"type\":\"waterBodies\",\"map\":\"%s\",\"count\":%d,\"bodyMsec\":%.1f,\"memoryBytesEstimate\":%u}\n",
			tr.world->baseName, (int)s_water.bodies.size(), s_water.bodyMsec, (unsigned)memoryBytes);
		for ( const waterBody_t& b : s_water.bodies )
		{
			ri.Printf(PRINT_ALL, "{\"type\":\"body\",\"id\":%d,\"model\":%d,\"bounds\":[[%g,%g,%g],[%g,%g,%g]],\"area\":%g,\"depthAverage\":%g,\"depthDeepest\":%g,\"class\":\"%s\",\"opticsProfile\":\"%s\",\"dynamicsProfile\":\"%s\",\"source\":\"%s\",\"plane\":[%g,%g,%g,%g],\"flowDirection\":[%g,%g,%g],\"flowVelocity\":[%g,%g,%g],\"flowSpeed\":%g,\"flowSource\":\"%s\",\"flowConfidence\":\"%s\",\"multipliers\":[%g,%g,%g],\"weightedScroll\":[%g,%g],\"scrollDisagreement\":%g,\"legacyWorldVelocity\":[%g,%g,%g],\"legacyWorldAgreement\":%g,\"uvCoverage\":%g,\"deformAmplitude\":%g,\"deformFrequency\":%g}\n",
				b.id, b.model, b.bounds[0][0], b.bounds[0][1], b.bounds[0][2],
				b.bounds[1][0], b.bounds[1][1], b.bounds[1][2], b.area, b.depthAverage, b.depthDeepest,
				s_liquidNames[b.liquidClass], s_liquidNames[b.opticsClass], s_dynamics[b.dynamics].name, b.decision,
				b.planeNormal[0], b.planeNormal[1], b.planeNormal[2], b.planeDist,
				b.flowDirection[0], b.flowDirection[1], b.flowDirection[2],
				b.flowVelocity[0], b.flowVelocity[1], b.flowVelocity[2], b.flowSpeed,
				s_flowSources[b.flowSource], s_flowConfidence[b.flowConfidence],
				b.waveMultiplier, b.foamMultiplier, b.interactionMultiplier,
				b.legacy.weightedScroll[0], b.legacy.weightedScroll[1], b.legacy.disagreement,
				b.legacy.worldVelocity[0], b.legacy.worldVelocity[1], b.legacy.worldVelocity[2],
				b.legacy.worldAgreement, b.legacy.uvCoverage,
				b.legacy.deformAmplitude, b.legacy.deformFrequency);
			ri.Printf(PRINT_ALL, "{\"type\":\"bodyWind\",\"body\":%d,\"valid\":%s,\"outdoor\":%s,\"source\":\"%s\",\"resolved\":[%g,%g,%g],\"strength\":%g,\"currentDirection\":[%g,%g],\"targetDirection\":[%g,%g],\"blend\":%g}\n",
				b.id, b.wind.valid ? "true" : "false", b.wind.outdoor ? "true" : "false",
				s_waterWindSources[b.wind.source], b.wind.resolved[0], b.wind.resolved[1], b.wind.resolved[2],
				b.wind.strength, b.wind.oldSet.direction[0] + (b.wind.targetSet.direction[0] - b.wind.oldSet.direction[0]) * b.wind.blend,
				b.wind.oldSet.direction[1] + (b.wind.targetSet.direction[1] - b.wind.oldSet.direction[1]) * b.wind.blend,
				b.wind.targetSet.direction[0], b.wind.targetSet.direction[1], b.wind.blend);
			for ( int id : b.surfaces )
				for ( const waterSurfaceRecord_t& surface : s_water.surfaces ) if ( surface.surfaceNum == id )
					ri.Printf(PRINT_ALL, "{\"type\":\"surface\",\"body\":%d,\"id\":%d,\"normal\":[%g,%g,%g],\"uvTangentU\":[%g,%g,%g],\"uvTangentV\":[%g,%g,%g],\"uvCoverage\":%g,\"uvMirrorSign\":%d}\n",
						b.id, id, surface.normal[0], surface.normal[1], surface.normal[2],
						surface.uvTangentU[0], surface.uvTangentU[1], surface.uvTangentU[2],
						surface.uvTangentV[0], surface.uvTangentV[1], surface.uvTangentV[2],
						surface.uvTotalArea > 0.0f ? surface.uvArea / surface.uvTotalArea : 0.0f, surface.uvMirrorSign);
			for ( int id : b.brushes ) ri.Printf(PRINT_ALL, "{\"type\":\"brush\",\"body\":%d,\"id\":%d}\n", b.id, s_water.brushes[id].brushNum);
			for ( const std::string& shader : b.shaders ) ri.Printf(PRINT_ALL, "{\"type\":\"shader\",\"body\":%d,\"name\":\"%s\"}\n", b.id, shader.c_str());
			for ( const legacyWaterMotion_t::scroll_t& s : b.legacy.scrolls )
				ri.Printf(PRINT_ALL, "{\"type\":\"scroll\",\"body\":%d,\"shader\":\"%s\",\"stage\":%d,\"vector\":[%g,%g],\"worldVelocity\":[%g,%g,%g],\"basisAgreement\":%g,\"basisReliable\":%s}\n",
					b.id, s.shader.c_str(), s.stage, s.x, s.y, s.worldVelocity[0], s.worldVelocity[1], s.worldVelocity[2], s.basisAgreement, s.reliable ? "true" : "false");
			for ( const legacyWaterMotion_t::wave_t& w : b.legacy.turb )
				ri.Printf(PRINT_ALL, "{\"type\":\"turb\",\"body\":%d,\"stage\":%d,\"wave\":[%g,%g,%g,%g]}\n", b.id, w.stage, w.base, w.amplitude, w.phase, w.frequency);
			for ( const legacyWaterMotion_t::wave_t& w : b.legacy.stretch )
				ri.Printf(PRINT_ALL, "{\"type\":\"stretch\",\"body\":%d,\"stage\":%d,\"wave\":[%g,%g,%g,%g]}\n", b.id, w.stage, w.base, w.amplitude, w.phase, w.frequency);
		}
		return;
	}
	else if ( arg[0] && Q_stricmp(arg, "draw") )
	{
		ri.Printf(PRINT_ALL, "usage: r_waterBodies [dump | draw]\n");
		return;
	}
	ri.Printf(PRINT_ALL, "%s: %d water bodies, %.1f ms, approximately %u bytes, %d rules, draw %s\n", tr.world->baseName,
		(int)s_water.bodies.size(), s_water.bodyMsec, (unsigned)memoryBytes, (int)s_water.bodyRules.size(), s_water.bodyDebug ? "on" : "off");
	for ( const waterBody_t& b : s_water.bodies )
	{
		const unsigned h = (unsigned)b.id * 2654435761u;
		ri.Printf(PRINT_ALL, "  #%d color #%02x%02x%02x model %d (%g %g %g)-(%g %g %g) area %.0f depth %.0f/%.0f %s optics %s dynamics %d:%s [%s] surfaces %d brushes %d flow dir (%g %g %g) speed %.2f velocity (%g %g %g) %s/%s\n",
			b.id, 64 + (h & 127), 64 + ((h >> 8) & 127), 64 + ((h >> 16) & 127),
			b.model, b.bounds[0][0], b.bounds[0][1], b.bounds[0][2], b.bounds[1][0], b.bounds[1][1], b.bounds[1][2],
			b.area, b.depthAverage, b.depthDeepest, s_liquidNames[b.liquidClass], s_liquidNames[b.opticsClass],
			b.dynamics, s_dynamics[b.dynamics].name, b.decision, (int)b.surfaces.size(), (int)b.brushes.size(),
			b.flowDirection[0], b.flowDirection[1], b.flowDirection[2], b.flowSpeed,
			b.flowVelocity[0], b.flowVelocity[1], b.flowVelocity[2],
			s_flowSources[b.flowSource], s_flowConfidence[b.flowConfidence]);
		for ( const std::string& shader : b.shaders ) ri.Printf(PRINT_ALL, "    %s\n", shader.c_str());
		ri.Printf(PRINT_ALL, "    legacy scrolls %d weighted (%g %g) disagreement %.2f world (%g %g %g) agreement %.2f UV coverage %.2f confidence %s; stage importance unknown/equal; turb %d %.3f/%.3f stretch %d deform %.3f/%.3f\n",
			(int)b.legacy.scrolls.size(), b.legacy.weightedScroll[0], b.legacy.weightedScroll[1], b.legacy.disagreement,
			b.legacy.worldVelocity[0], b.legacy.worldVelocity[1], b.legacy.worldVelocity[2],
			b.legacy.worldAgreement, b.legacy.uvCoverage, s_flowConfidence[b.legacy.confidence],
			(int)b.legacy.turb.size(), b.legacy.turbAmplitude, b.legacy.turbFrequency, (int)b.legacy.stretch.size(),
			b.legacy.deformAmplitude, b.legacy.deformFrequency);
		ri.Printf(PRINT_ALL, "    wind %s %s source %s vector (%g %g %g) strength %.2f current (%g %g) target (%g %g) blend %.2f\n",
			b.wind.valid ? "valid" : "invalid", b.wind.outdoor ? "outdoor" : "indoor",
			s_waterWindSources[b.wind.source], b.wind.resolved[0], b.wind.resolved[1], b.wind.resolved[2], b.wind.strength,
			b.wind.oldSet.direction[0] + (b.wind.targetSet.direction[0] - b.wind.oldSet.direction[0]) * b.wind.blend,
			b.wind.oldSet.direction[1] + (b.wind.targetSet.direction[1] - b.wind.oldSet.direction[1]) * b.wind.blend,
			b.wind.targetSet.direction[0], b.wind.targetSet.direction[1], b.wind.blend);
		for ( const legacyWaterMotion_t::scroll_t& s : b.legacy.scrolls )
			ri.Printf(PRINT_ALL, "      %s stage %d scroll (%g %g) -> world velocity (%g %g %g), basis agreement %.2f, UV basis %s\n",
				s.shader.c_str(), s.stage, s.x, s.y, s.worldVelocity[0], s.worldVelocity[1], s.worldVelocity[2],
				s.basisAgreement, s.reliable ? "reliable" : "unreliable");
		for ( const legacyWaterMotion_t::wave_t& w : b.legacy.turb )
			ri.Printf(PRINT_ALL, "      stage %d turb (%g %g %g %g)\n", w.stage, w.base, w.amplitude, w.phase, w.frequency);
		for ( const legacyWaterMotion_t::wave_t& w : b.legacy.stretch )
			ri.Printf(PRINT_ALL, "      stage %d stretch (%g %g %g %g)\n", w.stage, w.base, w.amplitude, w.phase, w.frequency);
	}
}

void R_Waterfalls_f(void)
{
	if (!s_water.world || s_water.world != tr.world)
	{
		ri.Printf(PRINT_ALL, "r_waterfalls: no map\n");
		return;
	}
	int count = 0;
	for (const waterBody_t& body : s_water.bodies) if (body.waterfall) ++count;
	ri.Printf(PRINT_ALL, "%s: %d waterfall sheet%s; renderer %s; quality %d; geometry %s; rules %d\n",
		tr.world->baseName, count, count == 1 ? "" : "s", r_waterfall->integer ? "on" : "legacy",
		r_waterfallQuality->integer, r_waterfallGeometry->integer ? "on" : "BSP mesh", (int)s_water.waterfallRules.size());
	for (const waterBody_t& body : s_water.bodies)
	{
		if (!body.waterfall) continue;
		const waterfallProfile_t& p = body.waterfallValues;
		ri.Printf(PRINT_ALL, "  body %d profile %s [%s] surfaces", body.id, p.name, body.waterfallDecision);
		for (int surface : body.surfaces) ri.Printf(PRINT_ALL, " %d", surface);
		ri.Printf(PRINT_ALL, "; flow (%g %g %g) %.1f; thickness %.2f; breakup %.2f; normal %.2f; aeration %.2f; spray %.2f; impact %.2f\n",
			body.fallDirection[0], body.fallDirection[1], body.fallDirection[2], p.flowSpeed, p.thickness,
			p.breakup, p.normalScale, p.aeration, p.spray, p.impact);
		ri.Printf(PRINT_ALL, "    top %.2f bottom %.2f impact center (%g %g %g), width %.1f; foam/spray/impact source metadata available\n",
			body.fallMin, body.fallMax, body.impactCenter[0], body.impactCenter[1], body.impactCenter[2], body.impactWidth);
	}
	for (const waterfallImpactSource_t& source : s_water.waterfallImpacts)
		ri.Printf(PRINT_ALL, "  impact sheet %d -> pool %d at (%g %g %g), radius %.1f, strength %.2f [%s]%s\n",
			source.waterfallBodyId, source.poolBodyId, source.origin[0], source.origin[1], source.origin[2],
			source.radius, source.strength, s_waterfallImpactOrigins[source.originType],
			source.poolBodyId ? "" : " UNRESOLVED: explicit WaterfallEmitter required");
}

void R_WaterFlowOverride_f( void )
{
	if ( !tr.world || s_water.world != tr.world )
	{
		ri.Printf(PRINT_ALL, "r_waterFlowOverride: no map\n");
		return;
	}
	const char *action = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "list";
	if ( !Q_stricmp(action, "clear") )
	{
		s_water.bodyRules.erase(std::remove_if(s_water.bodyRules.begin(), s_water.bodyRules.end(),
			[](const waterBodyRule_t& rule) { return rule.runtime; }), s_water.bodyRules.end());
		for ( waterBody_t& body : s_water.bodies ) R_WaterResolveBody(body);
		ri.Printf(PRINT_ALL, "r_waterFlowOverride: runtime overrides cleared (env.json rules retained)\n");
		return;
	}
	if ( !Q_stricmp(action, "list") || !Q_stricmp(action, "dump") )
	{
		for ( const waterBody_t& body : s_water.bodies )
			ri.Printf(PRINT_ALL, "body %d flow direction (%g %g %g) speed %g velocity (%g %g %g) source %s confidence %s legacy agreement %.2f UV coverage %.2f\n",
				body.id, body.flowDirection[0], body.flowDirection[1], body.flowDirection[2], body.flowSpeed,
				body.flowVelocity[0], body.flowVelocity[1], body.flowVelocity[2],
				s_flowSources[body.flowSource], s_flowConfidence[body.flowConfidence],
				body.legacy.worldAgreement, body.legacy.uvCoverage);
		return;
	}
	if ( ri.Cmd_Argc() != 7 || (Q_stricmp(action, "body") && Q_stricmp(action, "shader") && Q_stricmp(action, "prefix")) )
	{
		ri.Printf(PRINT_ALL, "usage: r_waterFlowOverride body <id> <x> <y> <z> <speed>\n");
		ri.Printf(PRINT_ALL, "       r_waterFlowOverride shader|prefix <name> <x> <y> <z> <speed>\n");
		ri.Printf(PRINT_ALL, "       r_waterFlowOverride list|dump|clear\n");
		return;
	}
	waterBodyRule_t rule = {};
	rule.id = -1;
	rule.wave = rule.foam = rule.interaction = 1.0f;
	rule.runtime = true;
	rule.hasFlow = true;
	if ( !Q_stricmp(action, "body") ) rule.id = atoi(ri.Cmd_Argv(2));
	else
	{
		char selector[MAX_QPATH];
		Q_strncpyz(selector, ri.Cmd_Argv(2), sizeof(selector));
		Q_strlwr(selector);
		rule.shader = selector;
		if ( !Q_stricmp(action, "prefix") ) rule.shader += "*";
	}
	VectorSet(rule.flowDirection, (float)atof(ri.Cmd_Argv(3)), (float)atof(ri.Cmd_Argv(4)),
		(float)atof(ri.Cmd_Argv(5)));
	if ( VectorNormalize(rule.flowDirection) < 1.0e-6f ) VectorClear(rule.flowDirection);
	rule.flowSpeed = Q_max(0.0f, (float)atof(ri.Cmd_Argv(6)));
	s_water.bodyRules.push_back(rule);
	for ( waterBody_t& body : s_water.bodies ) R_WaterResolveBody(body);
	// the spatial field encodes the direction: rebuild it for the new rule
	for ( waterBody_t& body : s_water.bodies ) if ( body.flowField.image ) R_WaterFlowFieldBuildBody(body);
	ri.Printf(PRINT_ALL, "r_waterFlowOverride: applied runtime uniform-flow rule; use list to inspect\n");
}

// simulationCoordinate is body UV (0..1 over the body bounds)
qboolean R_WaterFlowForBody( int bodyId, const vec2_t simulationCoordinate, vec3_t velocity )
{
	VectorClear(velocity);
	if ( bodyId <= 0 || bodyId > (int)s_water.bodies.size() ) return qfalse;
	const waterBody_t& body = s_water.bodies[bodyId - 1];
	float sample[4];
	if ( simulationCoordinate && R_WaterFlowFieldSample(body,
		body.bounds[0][0] + simulationCoordinate[0] * (body.bounds[1][0] - body.bounds[0][0]),
		body.bounds[0][1] + simulationCoordinate[1] * (body.bounds[1][1] - body.bounds[0][1]), sample) )
		VectorSet(velocity, sample[0] * body.flowSpeed, sample[1] * body.flowSpeed, 0.0f);
	else
		VectorCopy(body.flowVelocity, velocity);
	return (qboolean)(body.flowSource != WFLOW_NONE);
}

image_t *R_WaterCausticsInteractionAtlas(void)
{
	return s_water.interactionAtlas;
}

qboolean R_WaterCausticsBrushParams(int bspBrushNum, float time, vec4_t params[8],
	vec4_t terms[8], vec4_t omega[2])
{
	time = r_waterWaveTime->value >= 0.0f ? r_waterWaveTime->value : time;
	Com_Memset(params, 0, sizeof(vec4_t) * 8);
	Com_Memset(terms, 0, sizeof(vec4_t) * 8);
	Com_Memset(omega, 0, sizeof(vec4_t) * 2);
	const waterBody_t *body = nullptr;
	for (const waterBody_t& candidate : s_water.bodies)
	{
		for (int brushIndex : candidate.brushes)
			if (brushIndex >= 0 && brushIndex < (int)s_water.brushes.size() &&
				s_water.brushes[brushIndex].brushNum == bspBrushNum) { body = &candidate; break; }
		if (body) break;
	}
	if (!body || !r_waterSurface->integer) return qfalse;
	const waterDynamics_t& dynamics = s_dynamics[body->dynamics];
	const float legacyWaveScale = body->legacy.deformAmplitude > 0.5f ? 0.2f : 1.0f;
	const float multiplier = body->waveMultiplier;
	VectorSet4(params[0], r_waterWaves->integer ? 1.0f : 0.0f,
		Com_Clamp(0.0f, 4.0f, r_waterWaveAmplitude->value), Com_Clamp(0.25f, 4.0f, r_waterWaveLength->value),
		Com_Clamp(0.0f, 4.0f, r_waterWaveSpeed->value));
	VectorSet4(params[1], dynamics.amplitude * 8.0f * legacyWaveScale * multiplier,
		dynamics.wavelength, dynamics.speed, Com_Clamp(0.0f, 2.0f, dynamics.microNormal * multiplier));
	VectorSet4(params[2], Com_Clamp(0.0f, 2.0f, r_waterWaveChoppiness->value),
		(float)Com_Clampi(0, 2, r_waterWaveQuality->integer), body->depthAverage, dynamics.choppiness);
	const float flowScale = Com_Clamp(0.0f, 4.0f, r_waterFlowSpeed->value);
	VectorSet4(params[3], body->flowVelocity[0] * flowScale, body->flowVelocity[1] * flowScale,
		body->flowVelocity[2] * flowScale, r_waterFlow->integer && body->flowSpeed > 0.0f ? 1.0f : 0.0f);
	VectorSet4(params[4], body->flowSpeed * flowScale, Com_Clamp(0.0f, 4.0f, r_waterFlowDetail->value),
		r_waterWaveShallow->integer ? 1.0f : 0.0f, multiplier);
	const waterInteractionState_t& interaction = body->interaction;
	const float extentX = MAX(body->bounds[1][0] - body->bounds[0][0], 1.0f);
	const float extentY = MAX(body->bounds[1][1] - body->bounds[0][1], 1.0f);
	VectorSet4(params[5], body->bounds[0][0], body->bounds[0][1], 1.0f / extentX, 1.0f / extentY);
	if (interaction.image && s_water.interactionAtlas)
	{
		VectorSet4(params[6], (interaction.atlasX + 0.5f) / s_water.interactionAtlasWidth,
			(interaction.atlasY + 0.5f) / s_water.interactionAtlasHeight,
			(interaction.width - 1.0f) / s_water.interactionAtlasWidth,
			(interaction.height - 1.0f) / s_water.interactionAtlasHeight);
		VectorSet4(params[7], interaction.texelX, interaction.texelY, r_waterInteraction->integer ? 1.0f : 0.0f, time);
	}
	else params[7][3] = time;
	const float wavelength = MAX(params[1][1] * params[0][2], 8.0f);
	const float amplitude = params[1][0] * params[0][1];
	const float flowLength = sqrtf(body->flowDirection[0] * body->flowDirection[0] + body->flowDirection[1] * body->flowDirection[1]);
	const float mainX = flowLength > 0.001f ? body->flowDirection[0] / flowLength : 0.8f;
	const float mainY = flowLength > 0.001f ? body->flowDirection[1] / flowLength : 0.6f;
	const float directionMix = flowLength > 0.001f ? 0.72f : 0.22f;
	const float baseK = 6.2831853f / MAX(wavelength, 4.0f);
	const float waveSpeed = dynamics.speed * params[0][3] * 16.0f;
	const float depth = body->depthAverage;
	const float baseDepthTerm = depth > 0.0f ? tanhf(baseK * depth) : 1.0f;
	const int dispersionMode = Com_Clampi(0, 2, r_waterWaveDispersion->integer);
	const bool physicalDispersion = dispersionMode == 2 && body->flowSpeed <= 0.001f;
	for (int i = 0; i < 8; ++i)
	{
		const float fi = (float)i;
		float dx = s_waterWaveDirectionBasis[i][0] * (1.0f - directionMix) + mainX * directionMix;
		float dy = s_waterWaveDirectionBasis[i][1] * (1.0f - directionMix) + mainY * directionMix;
		const float inv = 1.0f / sqrtf(MAX(dx * dx + dy * dy, 1.0e-8f)); dx *= inv; dy *= inv;
		const float scale = i < 4 ? (1.0f - 0.15f * fi) : (0.34f - 0.035f * (fi - 4.0f));
		const float lambdaScale = i < 4 ? (1.0f - 0.13f * fi) : (0.28f - 0.025f * (fi - 4.0f));
		const float k = 6.2831853f / MAX(wavelength * lambdaScale, 4.0f);
		VectorSet4(terms[i], dx, dy, amplitude * scale * (i < 4 ? 0.28f : 0.10f), k);
		const float depthTerm = depth > 0.0f ? tanhf(k * depth) : 1.0f;
		const float normalizedOmega = waveSpeed * baseK *
			sqrtf(MAX(k * depthTerm / MAX(baseK * baseDepthTerm, 1.0e-8f), 0.0f));
		const float linearOmega = waveSpeed * k;
		if (dispersionMode == 0)
			omega[i / 4][i & 3] = linearOmega;
		else if (physicalDispersion)
			omega[i / 4][i & 3] = sqrtf(MAX(r_waterWaveGravity->value * k * depthTerm, 0.0f));
		else
			omega[i / 4][i & 3] = linearOmega +
				(normalizedOmega - linearOmega) * dynamics.dispersion;
	}
	return qtrue;
}

qboolean R_WaterFlowAtWorldPosition( const vec3_t position, vec3_t velocity, int *bodyId )
{
	VectorClear(velocity);
	if ( bodyId ) *bodyId = 0;
	const waterBody_t *best = nullptr;
	float bestDistance = 1.0e30f;
	for ( const waterBody_t& body : s_water.bodies )
	{
		if ( position[0] < body.bounds[0][0] || position[0] > body.bounds[1][0] ||
			position[1] < body.bounds[0][1] || position[1] > body.bounds[1][1] ) continue;
		const float distance = fabsf(DotProduct(body.planeNormal, position) - body.planeDist);
		if ( distance < bestDistance ) { bestDistance = distance; best = &body; }
	}
	if ( !best ) return qfalse;
	float sample[4];
	if ( R_WaterFlowFieldSample(*best, position[0], position[1], sample) )
		VectorSet(velocity, sample[0] * best->flowSpeed, sample[1] * best->flowSpeed, 0.0f);
	else
		VectorCopy(best->flowVelocity, velocity);
	if ( bodyId ) *bodyId = best->id;
	return (qboolean)(best->flowSource != WFLOW_NONE);
}

qhandle_t RE_RegisterShaderFromImage( const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage );

static void R_WaterBodySegment( const refdef_t *fd, qhandle_t shader, const vec3_t a, const vec3_t b, const byte *color )
{
	vec3_t direction, eye, side;
	VectorSubtract(b, a, direction);
	VectorSubtract(a, fd->vieworg, eye);
	CrossProduct(direction, eye, side);
	if ( VectorNormalize(side) < 1e-6f ) return;
	VectorScale(side, 1.0f + .0015f * VectorLength(eye), side);
	polyVert_t verts[4] = {};
	VectorSubtract(a, side, verts[0].xyz);
	VectorAdd(a, side, verts[1].xyz);
	VectorAdd(b, side, verts[2].xyz);
	VectorSubtract(b, side, verts[3].xyz);
	for ( int i = 0; i < 4; i++ )
	{
		verts[i].st[0] = verts[i].st[1] = .5f;
		Com_Memcpy(verts[i].modulate, color, 4);
	}
	RE_AddPolyToScene(shader, 4, verts, 1);
}

static void R_WaterBodyDigit( const refdef_t *fd, qhandle_t shader, const vec3_t origin, int digit, float offset, const byte *color )
{
	static const byte masks[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
	static const float lines[7][4] = {
		{ 0, 2, 1, 2 }, { 1, 2, 1, 1 }, { 1, 1, 1, 0 },
		{ 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { 0, 2, 0, 1 }, { 0, 1, 1, 1 }
	};
	if ( digit < 0 || digit > 9 ) return;
	for ( int segment = 0; segment < 7; segment++ )
	{
		if ( !(masks[digit] & (1 << segment)) ) continue;
		vec3_t a, b;
		VectorMA(origin, (offset + lines[segment][0]) * 12, fd->viewaxis[1], a);
		VectorMA(a, lines[segment][1] * 12, fd->viewaxis[2], a);
		VectorMA(origin, (offset + lines[segment][2]) * 12, fd->viewaxis[1], b);
		VectorMA(b, lines[segment][3] * 12, fd->viewaxis[2], b);
		R_WaterBodySegment(fd, shader, a, b, color);
	}
}

void R_WaterBodiesDebugDraw( const refdef_t *fd )
{
	const qboolean bodyDebug = (qboolean)(s_water.bodyDebug || r_waterFlowDebug->integer ||
		(r_waterGeometry->integer && r_waterGeometryDebug->integer == 5));
	if ( !(bodyDebug || r_waterfallMistDebug->integer || r_waterfallImpactDebug->integer) ||
		!tr.world || s_water.world != tr.world || (fd->rdflags & RDF_NOWORLDMODEL) ) return;
	if ( !s_water.bodyDebugShader )
		s_water.bodyDebugShader = RE_RegisterShaderFromImage("*waterBodyDebug", lightmaps2d, stylesDefault, tr.whiteImage, qfalse);
	if (r_waterfallMistDebug->integer)
	{
		const byte mistColor[4] = { 80, 220, 255, 255 };
		for (int v = 0; v < tr.world->numWaterfallFogVolumes; ++v)
		{
			const refFogVolume_t& volume = tr.world->waterfallFogVolumes[v];
			vec3_t p[8];
			for (int i = 0; i < 8; ++i)
				for (int axis = 0; axis < 3; ++axis)
					p[i][axis] = volume.origin[axis] + ((i & (1 << axis)) ? 1.0f : -1.0f) *
						volume.extents[axis] * r_waterfallMistRadius->value;
			for (int i = 0; i < 8; ++i)
				for (int bit = 0; bit < 3; ++bit)
					if (!(i & (1 << bit))) R_WaterBodySegment(fd, s_water.bodyDebugShader,
						p[i], p[i | (1 << bit)], mistColor);
		}
		const byte lensColor[4] = { 80, 255, 140, 255 };
		for (int v = 0; v < tr.world->numWaterfallLensEmitters; ++v)
		{
			const lensWaterEmitter_t& emitter = tr.world->waterfallLensEmitters[v];
			vec3_t a, b;
			for (int axis = 0; axis < 3; ++axis)
			{
				VectorCopy(emitter.origin, a); VectorCopy(emitter.origin, b);
				a[axis] -= emitter.radius * 0.5f * r_waterfallMistRadius->value;
				b[axis] += emitter.radius * 0.5f * r_waterfallMistRadius->value;
				R_WaterBodySegment(fd, s_water.bodyDebugShader, a, b, lensColor);
			}
		}
	}
	if (r_waterfallImpactDebug->integer)
	{
		for (const waterfallImpactSource_t& source : s_water.waterfallImpacts)
		{
			const byte resolvedColor[4] = { 40, 235, 210, 255 };
			const byte authoredColor[4] = { 255, 190, 50, 255 };
			const byte unresolvedColor[4] = { 255, 45, 70, 255 };
			const byte *color = !source.poolBodyId ? unresolvedColor :
				(source.originType == WFIMPACT_CLASSIFIED_BOTTOM ? resolvedColor : authoredColor);
			const float radius = source.radius * r_waterfallImpactRadius->value;
			vec3_t previous = { source.origin[0] + radius, source.origin[1], source.origin[2] + 2.0f };
			for (int segment = 1; segment <= 24; ++segment)
			{
				const float angle = segment * (6.2831853f / 24.0f);
				vec3_t next = { source.origin[0] + cosf(angle) * radius,
					source.origin[1] + sinf(angle) * radius, source.origin[2] + 2.0f };
				R_WaterBodySegment(fd, s_water.bodyDebugShader, previous, next, color);
				VectorCopy(next, previous);
			}
			vec3_t a, b;
			VectorCopy(source.origin, a); VectorCopy(source.origin, b);
			a[2] -= 24.0f; b[2] += 48.0f;
			R_WaterBodySegment(fd, s_water.bodyDebugShader, a, b, color);
		}
	}
	if (!bodyDebug) return;
	for ( const waterBody_t& body : s_water.bodies )
	{
		const unsigned h = (unsigned)body.id * 2654435761u;
		const byte color[4] = { (byte)(64 + (h & 127)), (byte)(64 + ((h >> 8) & 127)),
			(byte)(64 + ((h >> 16) & 127)), 255 };
		if (r_waterGeometry->integer && r_waterGeometryDebug->integer == 5)
		{
			for (int surfaceNum : body.surfaces)
			{
				const msurface_t& ms = tr.world->surfaces[surfaceNum];
				if (!(ms.cullinfo.type & CULLINFO_BOX)) continue;
				vec3_t box[8];
				for (int i = 0; i < 8; ++i)
					VectorSet(box[i], ms.cullinfo.bounds[(i & 1) != 0][0],
						ms.cullinfo.bounds[(i & 2) != 0][1], ms.cullinfo.bounds[(i & 4) != 0][2]);
				for (int i = 0; i < 8; ++i)
					for (int bit = 0; bit < 3; ++bit)
						if (!(i & (1 << bit))) R_WaterBodySegment(fd, s_water.bodyDebugShader,
							box[i], box[i | (1 << bit)], color);
			}
			continue;
		}
		vec3_t p[8];
		for ( int i = 0; i < 8; i++ )
			VectorSet(p[i], body.bounds[(i & 1) != 0][0], body.bounds[(i & 2) != 0][1], body.bounds[(i & 4) != 0][2]);
		for ( int i = 0; i < 8; i++ )
			for ( int bit = 0; bit < 3; bit++ )
				if ( !(i & (1 << bit)) ) R_WaterBodySegment(fd, s_water.bodyDebugShader, p[i], p[i | (1 << bit)], color);
		vec3_t center, end;
		VectorAdd(body.bounds[0], body.bounds[1], center);
		VectorScale(center, .5f, center);
		if ( fabsf(body.planeNormal[2]) > .5f )
			center[2] = (body.planeDist - body.planeNormal[0] * center[0] - body.planeNormal[1] * center[1]) / body.planeNormal[2];
		VectorMA(center, 32, body.planeNormal, end);
		R_WaterBodySegment(fd, s_water.bodyDebugShader, center, end, color);
		vec3_t label;
		VectorMA(center, 40, body.planeNormal, label);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, (body.id / 100) % 10, 0, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, (body.id / 10) % 10, 1.3f, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, body.id % 10, 2.6f, color);
		R_WaterBodyDigit(fd, s_water.bodyDebugShader, label, body.dynamics, 4.5f, color);
		vec3_t plane[4];
		for ( int corner = 0; corner < 4; corner++ )
		{
			VectorCopy(center, plane[corner]);
			const int axis = fabsf(body.planeNormal[2]) > .5f ? 2 :
				(fabsf(body.planeNormal[0]) > fabsf(body.planeNormal[1]) ? 0 : 1);
			const int u = (axis + 1) % 3, v = (axis + 2) % 3;
			plane[corner][u] = body.bounds[(corner & 1) != 0][u];
			plane[corner][v] = body.bounds[(corner & 2) != 0][v];
			if ( fabsf(body.planeNormal[axis]) > .01f )
				plane[corner][axis] = (body.planeDist - body.planeNormal[u] * plane[corner][u] -
					body.planeNormal[v] * plane[corner][v]) / body.planeNormal[axis];
		}
		const int outline[4] = { 0, 1, 3, 2 };
		for ( int edge = 0; edge < 4; edge++ )
			R_WaterBodySegment(fd, s_water.bodyDebugShader, plane[outline[edge]], plane[outline[(edge + 1) & 3]], color);
		if ( r_waterFlowDebug->integer == 3 )
		{
			const byte uColor[4] = { 255, 64, 64, 255 }, vColor[4] = { 64, 255, 64, 255 };
			for ( const waterSurfaceRecord_t& surface : s_water.surfaces )
			{
				if ( std::find(body.surfaces.begin(), body.surfaces.end(), surface.surfaceNum) == body.surfaces.end() ) continue;
				vec3_t sc, direction;
				VectorAdd(surface.bounds[0], surface.bounds[1], sc);
				VectorScale(sc, 0.5f, sc);
				VectorCopy(surface.uvTangentU, direction);
				if ( VectorNormalize(direction) > 0.0f ) { VectorMA(sc, 48.0f, direction, end); R_WaterBodySegment(fd, s_water.bodyDebugShader, sc, end, uColor); }
				VectorCopy(surface.uvTangentV, direction);
				if ( VectorNormalize(direction) > 0.0f ) { VectorMA(sc, 48.0f, direction, end); R_WaterBodySegment(fd, s_water.bodyDebugShader, sc, end, vColor); }
			}
		}
		const vec3_t *arrow = nullptr;
		if ( r_waterFlowDebug->integer == 2 ) arrow = &body.legacy.worldVelocity;
		else if ( r_waterFlowDebug->integer == 1 || r_waterFlowDebug->integer == 4 || s_water.bodyDebug ) arrow = &body.flowVelocity;
		if ( arrow && VectorLength(*arrow) > 1.0e-4f )
		{
			vec3_t direction;
			VectorCopy(*arrow, direction);
			const float magnitude = VectorNormalize(direction);
			VectorMA(center, Q_min(192.0f, 32.0f + magnitude * 4.0f), direction, end);
			const byte high[4] = { 32, 255, 96, 255 }, ambiguous[4] = { 255, 160, 32, 255 };
			const byte *arrowColor = body.flowConfidence == WFLOW_CONFIDENCE_HIGH ? high : ambiguous;
			R_WaterBodySegment(fd, s_water.bodyDebugShader, center, end, arrowColor);
		}
	}
}

static void R_WaterPrintShaders( void )
{
	ri.Printf(PRINT_ALL, "  candidate shaders (final decisions are per surface; up/down/vertical/sloped, up share, brush links):\n");
	for ( const waterShaderRecord_t& s : s_water.shaders )
	{
		if (!s.modern && !R_WaterNameLike(s.shader->name) && !s.shader->useDistortion &&
			R_WaterLiquidClass(s.contents) < 0 && !s.linked && !R_WaterFindOverride(s.shader->name))
			continue;
		ri.Printf(PRINT_ALL, "  %-6s %-44s %-5s %3d (%d/%d/%d/%d) up %3.0f%% linked %3d contents 0x%08x%s%s  %s\n",
			s.modern ? "WATER" : "legacy", s.shader->name, s_liquidNames[s.liquidClass], s.surfaces,
			s.orientCount[0], s.orientCount[1], s.orientCount[2], s.orientCount[3],
			s.area - s.downArea > 0.0f ? 100.0f * s.upArea / (s.area - s.downArea) : 0.0f, s.linked, (unsigned)s.contents,
			s.shader->useDistortion ? " refractive" : "",
			(s.flags & WATERSURF_FOG_MEDIUM) ? " fog-medium" : "",
			s_reasonNames[s.reason]);
	}
}

/*
=================
R_WaterInfo_f

r_waterInfo [surfaces]: the liquid brushes, the candidate shaders with the
reason of their decision, optionally every candidate surface
=================
*/
void R_WaterInfo_f( void )
{
	if ( !tr.world || s_water.world != tr.world )
	{
		ri.Printf(PRINT_ALL, "r_waterInfo: no map\n");
		return;
	}

	const qboolean listSurfaces = (qboolean)(ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "surfaces"));
	ri.Printf(PRINT_ALL, "Water surfaces of %s (classified in %.1f ms)\n", tr.world->baseName, s_water.classifyMsec);
	ri.Printf(PRINT_ALL, "  r_waterSurface %d (latched %s, resources %s), r_waterSurfaceExperimental %d, overrides %d\n",
		r_waterSurface->integer, r_waterSurface->latchedString ? r_waterSurface->latchedString : "-",
		s_water.resources ? "yes" : "no", r_waterSurfaceExperimental->integer, (int)s_water.overrides.size());
	ri.Printf(PRINT_ALL, "  last frame: %d water draws, view %s, SSR %s, froxel medium %s\n", s_water.lastDraws,
		backEnd.waterSurfaceView ? "yes" : "no", backEnd.waterSurfaceSSR ? "yes" : "no",
		RB_VolumetricLookupReady() ? "yes" : "no");
	ri.Printf(PRINT_ALL, "  wave-aware SSR %d, motion generation %u, committed frame %u, interaction frame %u, invalidation %s, previous atlas %.2f MiB\n",
		r_waterSSRWaveReprojection ? r_waterSSRWaveReprojection->integer : 0,
		s_water.motionGeneration, s_water.motionCommitFrame, s_water.interactionHistoryFrame,
		s_waterMotionInvalidationNames[s_water.motionInvalidation],
		s_water.previousInteractionAtlas ?
			(s_water.interactionAtlasWidth * s_water.interactionAtlasHeight * 8.0f) / (1024.0f * 1024.0f) : 0.0f);
	ri.Printf(PRINT_ALL, "  motion bookkeeping %.3f us/draw, last dirty-rect copy CPU submission %.3f ms\n",
		s_water.motionSnapshotCount ? s_water.motionSnapshotUsec / s_water.motionSnapshotCount : 0.0,
		s_water.motionCopyMsec);
	if (r_waterSSRWaveReprojection && r_waterSSRWaveReprojection->integer)
		for (const waterBody_t& body : s_water.bodies)
		{
			const waterMotionSnapshot_t *snapshot = body.id < (int)s_water.motionSnapshots.size() ?
				&s_water.motionSnapshots[body.id] : nullptr;
			ri.Printf(PRINT_ALL, "    body %d snapshot %u generation %u previous %s interaction-history %s last-copy %u legacy-deform %s model-transform %s interaction-motion %s\n",
				body.id, snapshot ? snapshot->renderedFrame : ~0u, snapshot ? snapshot->generation : 0,
				snapshot && snapshot->valid ? "valid" : "invalid",
				body.interaction.image ? (s_water.previousInteractionAtlas &&
					s_water.interactionHistoryFrame == s_water.history.frameNumber ? "valid" : "invalid") : "n/a",
				body.interaction.lastMotionCopyFrame,
				snapshot && snapshot->legacySupported ? "yes" : "no",
				snapshot && snapshot->modelTransformSupported ? "yes" : "no",
				snapshot && snapshot->interactionSupported ? "yes" : "no");
		}

	int worldBrushes = 0;
	for ( const waterBrushRecord_t& b : s_water.brushes )
		worldBrushes += b.model == 0 ? 1 : 0;
	ri.Printf(PRINT_ALL, "  liquid brushes: %d (world %d, brush models %d)\n", (int)s_water.brushes.size(),
		worldBrushes, (int)s_water.brushes.size() - worldBrushes);
	for ( size_t i = 0; i < s_water.brushes.size(); i++ )
	{
		const waterBrushRecord_t& b = s_water.brushes[i];
		const char *shader = (b.shaderNum >= 0 && b.shaderNum < tr.world->numShaders) ? tr.world->shaders[b.shaderNum].shader : "?";
		ri.Printf(PRINT_ALL, "  %3d brush %5d model %3d %-5s sides %2d (%6.0f %6.0f %6.0f)-(%6.0f %6.0f %6.0f)%s %s\n",
			(int)i, b.brushNum, b.model, s_liquidNames[b.liquidClass], b.numPlanes,
			b.bounds[0][0], b.bounds[0][1], b.bounds[0][2], b.bounds[1][0], b.bounds[1][1], b.bounds[1][2],
			(b.contents & CONTENTS_FOG) ? " fog" : "", shader);
	}

	R_WaterPrintShaders();

	if ( !listSurfaces )
	{
		ri.Printf(PRINT_ALL, "  %d cached surfaces (r_waterInfo surfaces lists water candidates)\n", (int)s_water.surfaces.size());
		return;
	}
	ri.Printf(PRINT_ALL, "  surfaces:\n");
	for ( const waterSurfaceRecord_t& r : s_water.surfaces )
	{
		if (!r.modern && !R_WaterNameLike(r.shader->name) && !r.refractive &&
			R_WaterLiquidClass(r.contents) < 0 && r.brush < 0 && !R_WaterFindOverride(r.shader->name))
			continue;
		const qboolean modern = r.modern;
		char link[64] = "-";
		if ( r.brush >= 0 )
			Com_sprintf(link, sizeof(link), "brush %d side (%.2f %.2f %.2f)", s_water.brushes[r.brush].brushNum,
				r.brushSide[0], r.brushSide[1], r.brushSide[2]);
		ri.Printf(PRINT_ALL, "  %5d model %3d %-8s %-8s n (%5.2f %5.2f %5.2f) area %8.0f %-6s %s%s%s | %s\n",
			r.surfaceNum, r.model, r.type == MST_PATCH ? "patch" : r.type == MST_TRIANGLE_SOUP ? "trisoup" : "planar",
			s_orientNames[r.orient], r.normal[0], r.normal[1], r.normal[2], r.area,
			modern ? "WATER" : "legacy", r.shader->name,
			(r.contents & CONTENTS_WATER) ? " [CONTENTS_WATER]" : "", r.refractive ? " [refractive]" : "", link);
	}
}

/*
=================
R_WaterOverride_f

r_waterOverride                                 list
r_waterOverride <shader | prefix*> on [water | slime]
r_waterOverride <shader | prefix*> off
r_waterOverride <shader | prefix*> clear
r_waterOverride clear                           remove all

For cfg files (map specific setups): applies to the current map at once and
to every map loaded later.
=================
*/
void R_WaterOverride_f( void )
{
	const int argc = ri.Cmd_Argc();
	if ( argc < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: r_waterOverride <shader | prefix*> on [water | slime] | off | clear;  r_waterOverride clear\n");
		for ( const waterOverride_t& o : s_water.overrides )
			ri.Printf(PRINT_ALL, "  %s %s%s%s\n", o.pattern.c_str(), o.mode ? "on" : "off",
				o.liquidClass >= 0 ? " " : "", o.liquidClass >= 0 ? s_liquidNames[o.liquidClass] : "");
		return;
	}

	char pattern[MAX_QPATH];
	Q_strncpyz(pattern, ri.Cmd_Argv(1), sizeof(pattern));
	Q_strlwr(pattern);
	for ( char *c = pattern; *c; c++ )
	{
		if ( *c == '\\' )
			*c = '/';
	}

	if ( argc == 2 && !Q_stricmp(pattern, "clear") )
	{
		s_water.overrides.clear();
	}
	else if ( argc >= 3 )
	{
		const char *mode = ri.Cmd_Argv(2);
		s_water.overrides.erase(std::remove_if(s_water.overrides.begin(), s_water.overrides.end(),
			[&]( const waterOverride_t& o ) { return o.pattern == pattern; }), s_water.overrides.end());
		if ( !Q_stricmp(mode, "on") || !Q_stricmp(mode, "off") )
		{
			waterOverride_t o;
			o.pattern = pattern;
			o.mode = !Q_stricmp(mode, "on") ? 1 : 0;
			o.liquidClass = -1;
			if ( argc >= 4 )
			{
				if ( !Q_stricmp(ri.Cmd_Argv(3), "slime") )
					o.liquidClass = LIQUID_SLIME;
				else if ( !Q_stricmp(ri.Cmd_Argv(3), "water") )
					o.liquidClass = LIQUID_WATER;
				else
					ri.Printf(PRINT_WARNING, "r_waterOverride: unknown profile \"%s\" (water, slime)\n", ri.Cmd_Argv(3));
			}
			s_water.overrides.push_back(o);
		}
		else if ( Q_stricmp(mode, "clear") )
		{
			ri.Printf(PRINT_WARNING, "r_waterOverride: unknown mode \"%s\" (on, off, clear)\n", mode);
			return;
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "usage: r_waterOverride <shader | prefix*> on [water | slime] | off | clear\n");
		return;
	}

	if ( tr.world && s_water.world == tr.world )
	{
		// Geometry and brush associations are immutable and were cached at load.
		R_WaterDecideShaders();
		ri.Printf(PRINT_ALL, "r_waterOverride: reapplied to %s\n", tr.world->baseName);
	}
}

/*
============================================================

Resources

============================================================
*/

/*
=================
R_WaterBuildWaveSlopes

Tiling wave slopes, size x size texels: a height field of integer wave
vectors (tiles exactly), a broad spectrum with a soft peak, random phases and
a mild directional spread. Stored (dh/dx, dh/dy, (dh/dx)^2, (dh/dy)^2) with
the RMS slope normalized to 1: the box mips average slopes and squared slopes,
so the shader recovers the unresolved slope variance (LEAN mapping).
=================
*/
// Tiling procedural foam structure (no asset), RGBA8 with mips:
//   R  bubble membranes: Worley F2 - F1, bright on the walls between cells
//   G  fine bubbles: Worley F1, dark centres, bright rims
//   B  large patches: periodic fBm value noise
//   A  coarse membranes for distance/minification
// The surface shader thresholds a weighted sum against the local foam
// concentration, so foam opens into lace and specks as it decays.
static void R_WaterBuildFoamPattern( int size, std::vector<byte>& out )
{
	auto hash = [](int x, int y, int seed) -> float {
		unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= h >> 16;
		return (float)(h & 0xffffff) / 16777216.0f;
	};
	auto worley = [&](float u, float v, int cells, int seed, float& f1, float& f2) {
		const float x = u * cells, y = v * cells;
		const int cx = (int)floorf(x), cy = (int)floorf(y);
		f1 = f2 = 1.0e9f;
		for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
		{
			const int gx = cx + dx, gy = cy + dy;
			const int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;
			const float px = gx + 0.1f + 0.8f * hash(wx, wy, seed);
			const float py = gy + 0.1f + 0.8f * hash(wx, wy, seed + 17);
			const float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
			if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
		}
	};
	auto value = [&](float u, float v, int cells, int seed) -> float {
		const float x = u * cells, y = v * cells;
		const int ix = (int)floorf(x), iy = (int)floorf(y);
		float fx = x - ix, fy = y - iy;
		fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
		auto at = [&](int gx, int gy) { return hash(((gx % cells) + cells) % cells, ((gy % cells) + cells) % cells, seed); };
		const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
		return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
	};
	out.resize((size_t)size * size * 4);
	for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
	{
		const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
		float f1, f2, g1, g2, c1, c2;
		worley(u, v, 11, 3, f1, f2);
		worley(u, v, 29, 7, g1, g2);
		worley(u, v, 5, 11, c1, c2);
		const float lace = 1.0f - Com_Clamp(0.0f, 1.0f, (f2 - f1) * 2.2f);
		const float bubbles = Com_Clamp(0.0f, 1.0f, g1 * 1.35f);
		const float patches = 0.5f * value(u, v, 4, 23) + 0.3f * value(u, v, 8, 29) + 0.2f * value(u, v, 16, 31);
		const float coarse = 1.0f - Com_Clamp(0.0f, 1.0f, (c2 - c1) * 1.8f);
		byte *p = &out[((size_t)y * size + x) * 4];
		p[0] = (byte)(255.0f * lace); p[1] = (byte)(255.0f * bubbles);
		p[2] = (byte)(255.0f * Com_Clamp(0.0f, 1.0f, patches)); p[3] = (byte)(255.0f * coarse);
	}
}

static void R_WaterBuildWaveSlopes( int size, std::vector<float>& out )
{
	struct wave_t { float kx, ky, amp, phase; };
	std::vector<wave_t> waves;
	unsigned seed = 0x5eed1234u;
	auto rnd = [&]() -> float
	{
		seed = seed * 1664525u + 1013904223u;
		return (float)((seed >> 8) & 0xffffff) / 16777216.0f;
	};
	for ( int ky = -12; ky <= 12; ky++ )
	{
		for ( int kx = -12; kx <= 12; kx++ )
		{
			const float k = sqrtf((float)(kx * kx + ky * ky));
			if ( k < 1.0f || k > 12.5f )
				continue;
			// spectrum: rises to k ~ 3, falls as k^-3 (height); prefers +x, keeps some cross waves
			const float peak = 3.0f;
			float amp = (k < peak) ? k / peak : powf(peak / k, 3.0f);
			const float dir = (float)kx / k;
			amp *= 0.45f + 0.55f * (0.5f + 0.5f * dir) * (0.5f + 0.5f * dir);
			amp *= 0.6f + 0.8f * rnd();
			waves.push_back({ (float)kx, (float)ky, amp, rnd() * 2.0f * (float)M_PI });
		}
	}

	out.assign(size * size * 4, 0.0f);
	double sum2 = 0.0;
	const float twoPi = 2.0f * (float)M_PI;
	for ( int y = 0; y < size; y++ )
	{
		for ( int x = 0; x < size; x++ )
		{
			const float u = (float)x / size, v = (float)y / size;
			float sx = 0.0f, sy = 0.0f;
			for ( const wave_t& w : waves )
			{
				const float c = cosf(twoPi * (w.kx * u + w.ky * v) + w.phase) * w.amp * twoPi;
				sx += c * w.kx;
				sy += c * w.ky;
			}
			float *t = &out[(y * size + x) * 4];
			t[0] = sx;
			t[1] = sy;
			sum2 += sx * sx + sy * sy;
		}
	}
	const float scale = (float)(1.0 / sqrt(MAX(sum2 / (size * size), 1e-12)));
	for ( int i = 0; i < size * size; i++ )
	{
		float *t = &out[i * 4];
		t[0] *= scale;
		t[1] *= scale;
		t[2] = t[0] * t[0];
		t[3] = t[1] * t[1];
	}
}

void R_CreateWaterSurfaceImages( int width, int height, int hdrFormat )
{
	R_WaterMotionInvalidate(WM_INVALID_RESOURCE_RECREATE);
	// the GPU programs are kept over a map change (only a vid_restart rebuilds
	// them): keep the decision they were built with
	if ( !tr.textureColorShader[0].program )
		s_water.resources = (qboolean)(r_waterSurface->integer != 0 || (r_waterfall && r_waterfall->integer != 0));

	tr.waterSceneImage = NULL;
	tr.waterDepthImage = NULL;
	tr.waterNormalImage = NULL;
	tr.waterFoamPatternImage = NULL;
	tr.waterGlowImage = NULL;
	tr.waterReflectionDepthImage = NULL;
	for (int i = 0; i < 2; i++)
	{
		tr.waterReflectionImage[i] = nullptr;
		tr.waterReflectionGeomImage[i] = nullptr;
		tr.waterReflectionHitImage[i] = nullptr;
	}
	s_water.history.valid = qfalse;
	if ( !s_water.resources )
		return;

	tr.waterSceneImage = R_CreateImage("*waterScene", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);
	tr.waterDepthImage = R_ScreenCreateImage("*waterDepth", width, height, GL_DEPTH24_STENCIL8, qfalse);

	tr.waterGlowImage = R_ScreenCreateImage("*waterGlow", width, height, hdrFormat, qtrue);
	if (R_SSRResourcesEnabled())
	{
		s_water.reflectionScale = R_SSRTraceScale();
		const int rw = Q_max(1, (int)ceilf(width / s_water.reflectionScale));
		const int rh = Q_max(1, (int)ceilf(height / s_water.reflectionScale));
		for (int i = 0; i < 2; i++)
		{
			tr.waterReflectionImage[i] = R_ScreenCreateImage(va("*waterReflection%d", i), rw, rh, GL_RGBA16F, qfalse);
			// View depth, octahedral normals and roughness all have ample precision
			// in half floats.  The hit target stores a receiver-relative vector,
			// rather than an absolute world position, so it is half-float safe even
			// on maps far from the world origin.
			tr.waterReflectionGeomImage[i] = R_ScreenCreateImage(va("*waterReflectionGeom%d", i), rw, rh, GL_RGBA16F, qfalse);
			tr.waterReflectionHitImage[i] = R_ScreenCreateImage(va("*waterReflectionHit%d", i), rw, rh, GL_RGBA16F, qfalse);
		}
		tr.waterReflectionDepthImage = R_ScreenCreateImage("*waterReflectionDepth", rw, rh, GL_DEPTH24_STENCIL8, qfalse);
	}
	const int size = 256;
	// Fixed spectrum: retain the CPU result over map loads and vid_restart.
	static std::vector<float> slopes;
	if ( slopes.empty() )
		R_WaterBuildWaveSlopes(size, slopes);
	tr.waterNormalImage = R_CreateImage("*waterWaves", NULL, size, size, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_MUTABLE, GL_RGBA16F);
	GL_Bind(tr.waterNormalImage);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, slopes.data());
	qglGenerateMipmap(GL_TEXTURE_2D);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 8);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	static std::vector<byte> foamPattern;
	if ( foamPattern.empty() )
		R_WaterBuildFoamPattern(256, foamPattern);
	tr.waterFoamPatternImage = R_CreateImage("*waterFoamPattern", NULL, 256, 256, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_MUTABLE, GL_RGBA8);
	GL_Bind(tr.waterFoamPatternImage);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, foamPattern.data());
	qglGenerateMipmap(GL_TEXTURE_2D);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void R_CreateWaterSurfaceFBOs( void )
{
	tr.waterCopyFbo = NULL;
	tr.waterReflectionFbo[0] = tr.waterReflectionFbo[1] = nullptr;
	tr.waterFoamFbo = nullptr;
	tr.waterInteractionFbo = nullptr;
	if ( !s_water.resources || !tr.waterSceneImage || !tr.waterDepthImage )
		return;

	tr.waterCopyFbo = FBO_Create("_waterCopy", tr.waterSceneImage->width, tr.waterSceneImage->height);
	FBO_Bind(tr.waterCopyFbo);
	FBO_AttachTextureImage(tr.waterSceneImage, 0);
	FBO_AttachTextureImage(tr.waterGlowImage, 1);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tr.waterDepthImage->texnum, 0);
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(tr.waterCopyFbo);
	for (int i = 0; i < 2 && tr.waterReflectionImage[i]; i++)
	{
		image_t *image = tr.waterReflectionImage[i];
		tr.waterReflectionFbo[i] = FBO_Create(va("_waterReflection%d", i), image->width, image->height);
		FBO_Bind(tr.waterReflectionFbo[i]);
		FBO_AttachTextureImage(image, 0);
		FBO_AttachTextureImage(tr.waterReflectionGeomImage[i], 1);
		FBO_AttachTextureImage(tr.waterReflectionHitImage[i], 2);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
			tr.waterReflectionDepthImage->texnum, 0);
		const GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
		qglDrawBuffers(3, buffers);
		R_CheckFBO(tr.waterReflectionFbo[i]);
	}
	if (R_WaterFoamResourcesEnabled())
		tr.waterFoamFbo = FBO_Create("_waterFoamScratch", r_waterFoamResolution->integer,
			r_waterFoamResolution->integer);
	if (r_waterInteraction && r_waterInteraction->integer)
		tr.waterInteractionFbo = FBO_Create("_waterInteractionScratch", 16, 16);
}

// Subdivide the BSP triangles once. A single dyadic level for every surface in
// a body gives identical sample points on matching shared edges. Midpoints are
// cached per surface so the two triangles of a BSP face cannot form a T-junction.
static glIndex_t R_WaterMidpoint(std::vector<packedVertex_t>& verts,
	std::unordered_map<uint64_t, glIndex_t>& edges, glIndex_t a, glIndex_t b)
{
	const glIndex_t lo = MIN(a, b), hi = MAX(a, b);
	const uint64_t key = ((uint64_t)lo << 32) | hi;
	auto found = edges.find(key);
	if (found != edges.end()) return found->second;
	packedVertex_t v = {};
	for (int i = 0; i < 3; ++i)
		v.position[i] = 0.5f * (verts[a].position[i] + verts[b].position[i]);
	// Packed normals interpolate poorly; use the original float normals at the
	// first level where possible and otherwise decode the packed endpoints.
	vec3_t n;
	R_VboUnpackNormal(n, verts[a].normal);
	vec3_t nb;
	R_VboUnpackNormal(nb, verts[b].normal);
	VectorAdd(n, nb, n);
	VectorNormalize(n);
	v.normal = R_VboPackNormal(n);
	for (int i = 0; i < 2; ++i)
	{
		v.texcoords[0][i] = 0.5f * (verts[a].texcoords[0][i] + verts[b].texcoords[0][i]);
		v.texcoords[1][i] = 0.5f * (verts[a].texcoords[1][i] + verts[b].texcoords[1][i]);
	}
	const glIndex_t result = (glIndex_t)verts.size();
	verts.push_back(v);
	edges[key] = result;
	return result;
}

typedef std::array<int, 6> waterEdgeKey_t;
static waterEdgeKey_t R_WaterEdgeKey(const vec3_t a, const vec3_t b)
{
	std::array<int, 3> qa = { (int)floorf(a[0] * 16.0f + 0.5f), (int)floorf(a[1] * 16.0f + 0.5f), (int)floorf(a[2] * 16.0f + 0.5f) };
	std::array<int, 3> qb = { (int)floorf(b[0] * 16.0f + 0.5f), (int)floorf(b[1] * 16.0f + 0.5f), (int)floorf(b[2] * 16.0f + 0.5f) };
	if (qb < qa) std::swap(qa, qb);
	return { qa[0], qa[1], qa[2], qb[0], qb[1], qb[2] };
}

static float R_WaterPointSegmentDistance2D(const vec3_t p, const vec3_t a, const vec3_t b)
{
	const float dx = b[0] - a[0], dy = b[1] - a[1];
	const float d2 = dx * dx + dy * dy;
	const float t = d2 > 1.0e-6f ? Com_Clamp(0.0f, 1.0f,
		((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / d2) : 0.0f;
	const float x = p[0] - (a[0] + dx * t), y = p[1] - (a[1] + dy * t);
	return sqrtf(x * x + y * y);
}

static bool R_WaterBoundaryRigid(const waterBody_t& body, const vec3_t a, const vec3_t b)
{
	vec3_t mid; VectorAdd(a, b, mid); VectorScale(mid, 0.5f, mid);
	// A nearby opaque, near-vertical BSP surface crossing the water plane is a
	// real wall.  The conservative still-pool fallback covers brushes whose
	// structural wall is split or absent from the render surfaces.
	for (const waterSurfaceRecord_t& surface : s_water.surfaces)
	{
		if (surface.model != body.model || surface.orient != WORIENT_VERTICAL ||
			(surface.contents & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_FOG))) continue;
		if (mid[2] < surface.bounds[0][2] - 4.0f || mid[2] > surface.bounds[1][2] + 4.0f) continue;
		const float x = Com_Clamp(surface.bounds[0][0], surface.bounds[1][0], mid[0]);
		const float y = Com_Clamp(surface.bounds[0][1], surface.bounds[1][1], mid[1]);
		const float dx = mid[0] - x, dy = mid[1] - y;
		if (dx * dx + dy * dy <= 64.0f) return true;
	}
	return !Q_stricmp(s_dynamics[body.dynamics].name, "still_pool");
}

/*
=================
R_WaterBuildContacts

Shoreline contact segments (r_waterShoreline), independent of the render
mesh (r_waterGeometry only adds the skirts).  Body boundary edges come from
the interface triangles; on natural banks the brush outline is usually
buried under the terrain, so each endpoint is moved along the edge normal to
where the solid ground actually crosses the water plane.
=================
*/
static bool R_WaterTraceDown(float x, float y, float top, float bottom, float& hitZ)
{
	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
	vec3_t start = { x, y, top }, end = { x, y, bottom };
#ifdef REND2_SP
	ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	if (trace.startsolid || trace.allsolid) { hitZ = top; return true; }
	if (trace.fraction >= 1.0f) return false;
	hitZ = trace.endpos[2];
	return true;
}

// Ground above the water plane at (x, y)?  (true when buried under a bank)
static bool R_WaterGroundAbove(float x, float y, float waterZ)
{
	float hitZ;
	return R_WaterTraceDown(x, y, waterZ + 64.0f, waterZ - 2.0f, hitZ) && hitZ >= waterZ - 1.0f;
}

static void R_WaterSnapContactPoint(vec3_t point, const vec2_t outward)
{
	const float waterZ = point[2];
	const bool buried = R_WaterGroundAbove(point[0], point[1], waterZ);
	// buried: march toward the water until the ground drops below the plane;
	// clear: march outward until the ground rises to it
	const float sign = buried ? -1.0f : 1.0f;
	for (float step = 4.0f; step <= 64.0f; step += 4.0f)
	{
		const float x = point[0] + outward[0] * sign * step, y = point[1] + outward[1] * sign * step;
		if (R_WaterGroundAbove(x, y, waterZ) != buried)
		{
			const float back = buried ? step : step - 2.0f;
			point[0] += outward[0] * sign * back; point[1] += outward[1] * sign * back;
			return;
		}
	}
}

static void R_WaterBuildContacts(world_t *world)
{
	if (!r_waterShoreline->integer || !r_waterSurface->integer) return;
	int snapped = 0;
	for (waterBody_t& body : s_water.bodies)
	{
		if (body.waterfall) continue;
		std::vector<srfBspSurface_t *> surfaces;
		for (int surfaceNum : body.surfaces)
		{
			msurface_t& ms = world->surfaces[surfaceNum];
			if (!(ms.waterKey & WATERKEY_INTERFACE) ||
				!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES)) continue;
			srfBspSurface_t *surface = (srfBspSurface_t *)ms.data;
			if (surface->verts && surface->numIndexes >= 3 && surface->numVerts) surfaces.push_back(surface);
		}
		struct sourceEdge_t { int count = 0; vec3_t a = {}, b = {}, inside = {}; };
		std::map<waterEdgeKey_t, sourceEdge_t> edges;
		for (srfBspSurface_t *surface : surfaces)
			for (int i = 0; i + 2 < surface->numIndexes; i += 3)
				for (int e = 0; e < 3; ++e)
				{
					const int ia = surface->indexes[i + e], ib = surface->indexes[i + (e + 1) % 3];
					const int ic = surface->indexes[i + (e + 2) % 3];
					if (ia < 0 || ib < 0 || ic < 0 || ia >= surface->numVerts || ib >= surface->numVerts ||
						ic >= surface->numVerts) continue;
					sourceEdge_t& edge = edges[R_WaterEdgeKey(surface->verts[ia].xyz, surface->verts[ib].xyz)];
					if (!edge.count)
					{
						VectorCopy(surface->verts[ia].xyz, edge.a); VectorCopy(surface->verts[ib].xyz, edge.b);
						VectorCopy(surface->verts[ic].xyz, edge.inside);
					}
					edge.count++;
				}
		for (const auto& item : edges)
		{
			const sourceEdge_t& edge = item.second;
			if (edge.count != 1) continue;
			const bool rigid = R_WaterBoundaryRigid(body, edge.a, edge.b);
			// outward normal in XY: away from the triangle's third vertex
			vec2_t outward = { edge.b[1] - edge.a[1], edge.a[0] - edge.b[0] };
			const float length = sqrtf(outward[0] * outward[0] + outward[1] * outward[1]);
			if (length < 1.0e-3f) continue;
			outward[0] /= length; outward[1] /= length;
			if ((edge.inside[0] - edge.a[0]) * outward[0] + (edge.inside[1] - edge.a[1]) * outward[1] > 0.0f)
			{
				outward[0] = -outward[0]; outward[1] = -outward[1];
			}
			vec3_t delta; VectorSubtract(edge.b, edge.a, delta);
			const int pieces = MAX(1, (int)ceilf(VectorLength(delta) / 64.0f));
			for (int piece = 0; piece < pieces; ++piece)
			{
				waterContactSegment_t contact = {};
				const float t0 = (float)piece / pieces, t1 = (float)(piece + 1) / pieces;
				VectorMA(edge.a, t0, delta, contact.a); VectorMA(edge.a, t1, delta, contact.b);
				if (!rigid)
				{
					R_WaterSnapContactPoint(contact.a, outward);
					R_WaterSnapContactPoint(contact.b, outward);
					++snapped;
				}
				contact.wetA = contact.currentA = contact.a[2];
				contact.wetB = contact.currentB = contact.b[2];
				contact.bodyId = body.id; contact.rigid = rigid;
				s_water.contacts.push_back(contact);
			}
		}
	}
	ri.Printf(PRINT_DEVELOPER, "water shoreline: %d contact segments (%d bank segments snapped to the ground line), geometry skirts %s\n",
		(int)s_water.contacts.size(), snapped, r_waterGeometry->integer ? "on" : "off");
}

void R_WaterBuildGeometry(world_t *world)
{
	R_WaterMotionInvalidate(WM_INVALID_TOPOLOGY);
	s_water.geometryMsec = 0;
	s_water.geometryBytes = 0;
	s_water.contacts.clear();
	s_water.contactTime = 0;
	s_water.contactUniformTime = -1;
	if (s_water.world == world) R_WaterBuildContacts(world);
	if (s_water.world != world || (!r_waterGeometry->integer && (!r_waterfallGeometry || !r_waterfallGeometry->integer))) return;
	const int start = ri.Milliseconds();
	const int bodyCap = Com_Clampi(1024, 1000000, r_waterGeometryBodyVerts->integer);
	const int mapCap = Com_Clampi(1024, 4000000, r_waterGeometryMapVerts->integer);
	int mapVerts = 0;
	for (waterBody_t& body : s_water.bodies)
	{
		if ((body.waterfall && (!r_waterfallGeometry || !r_waterfallGeometry->integer)) ||
			(!body.waterfall && !r_waterGeometry->integer)) continue;
		const float target = body.waterfall ? (r_waterfallQuality->integer <= 0 ? 128.0f :
			(r_waterfallQuality->integer >= 2 ? 32.0f : 64.0f)) :
			Com_Clamp(16.0f, 512.0f, r_waterGeometryEdge->value);
		std::vector<srfBspSurface_t *> surfaces;
		int sourceVerts = 0, sourceTriangles = 0;
		float longest = 0;
		for (int surfaceNum : body.surfaces)
		{
			msurface_t& ms = world->surfaces[surfaceNum];
			if (!(ms.waterKey & WATERKEY_INTERFACE) ||
				!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES)) continue;
			srfBspSurface_t *s = (srfBspSurface_t *)ms.data;
			if (!s->verts || s->numIndexes < 3 || !s->numVerts) continue;
			surfaces.push_back(s);
			sourceVerts += s->numVerts;
			sourceTriangles += s->numIndexes / 3;
			for (int i = 0; i + 2 < s->numIndexes; i += 3)
				for (int e = 0; e < 3; ++e)
				{
					const int a = s->indexes[i + e], b = s->indexes[i + (e + 1) % 3];
					if (a < 0 || b < 0 || a >= s->numVerts || b >= s->numVerts) continue;
					vec3_t d; VectorSubtract(s->verts[a].xyz, s->verts[b].xyz, d);
					longest = MAX(longest, VectorLength(d));
				}
		}
		if (surfaces.empty()) continue;
		struct sourceEdge_t { int count = 0; vec3_t a = {}, b = {}; };
		std::map<waterEdgeKey_t, sourceEdge_t> sourceEdges;
		for (srfBspSurface_t *s : surfaces)
			for (int i = 0; i + 2 < s->numIndexes; i += 3)
				for (int e = 0; e < 3; ++e)
				{
					const int ia = s->indexes[i + e], ib = s->indexes[i + (e + 1) % 3];
					if (ia < 0 || ib < 0 || ia >= s->numVerts || ib >= s->numVerts) continue;
					sourceEdge_t& edge = sourceEdges[R_WaterEdgeKey(s->verts[ia].xyz, s->verts[ib].xyz)];
					if (!edge.count) { VectorCopy(s->verts[ia].xyz, edge.a); VectorCopy(s->verts[ib].xyz, edge.b); }
					edge.count++;
				}
		struct boundary_t { vec3_t a, b; bool rigid; };
		std::vector<boundary_t> boundaries;
		for (const auto& item : sourceEdges)
		{
			if (item.second.count != 1) continue;
			boundary_t boundary = {};
			VectorCopy(item.second.a, boundary.a); VectorCopy(item.second.b, boundary.b);
			boundary.rigid = R_WaterBoundaryRigid(body, boundary.a, boundary.b);
			if (body.waterfall) continue;
			boundaries.push_back(boundary);
		}
		int level = 0;
		while (level < 8 && longest > target * (1 << level)) ++level;
		const int requested = level;
		// A triangle subdivided n times along each edge has at most
		// (n+1)(n+2)/2 unique top vertices, plus 3n boundary feet.
		auto estimatedVerts = [sourceVerts, sourceTriangles](int l) -> int64_t {
			const int64_t n = 1LL << l;
			return sourceVerts + (int64_t)sourceTriangles * ((n + 1) * (n + 2) / 2 + 3 * n);
		};
		while (level > 0 && estimatedVerts(level) > MIN(bodyCap, mapCap - mapVerts)) --level;
		if (estimatedVerts(0) > MIN(bodyCap, mapCap - mapVerts))
		{
			ri.Printf(PRINT_WARNING, "water geometry body %d: vertex budget exhausted, using BSP mesh\n", body.id);
			continue;
		}
		if (level != requested)
			ri.Printf(PRINT_WARNING, "water geometry body %d: subdivision %d -> %d (budget)\n", body.id, requested, level);
		std::vector<packedVertex_t> verts;
		std::vector<glIndex_t> indices;
		float maxTopEdge = 0.0f;
		struct range_t { srfBspSurface_t *s; int first, count, verts; glIndex_t min, max; };
		std::vector<range_t> ranges;
		for (srfBspSurface_t *s : surfaces)
		{
			const int firstVert = (int)verts.size(), firstIndex = (int)indices.size();
			for (int i = 0; i < s->numVerts; ++i)
			{
				packedVertex_t v = {};
				VectorCopy(s->verts[i].xyz, v.position);
				v.normal = R_VboPackNormal(s->verts[i].normal);
				VectorCopy2(s->verts[i].st, v.texcoords[0]);
				if (body.waterfall)
				{
					// Falling sheets do not use shoreline/skirt metadata. Preserve the
					// stock lightmap UV so r_waterSurfaceDebug 9 can render the true
					// authored stages on the refined geometry.
					VectorCopy2(s->verts[i].lightmap[0], v.texcoords[1]);
					verts.push_back(v);
					continue;
				}
				v.texcoords[1][0] = 1.0f;
				float boundaryDistance = 65504.0f;
				bool rigidBoundary = false;
				for (const boundary_t& boundary : boundaries)
				{
					const float distance = R_WaterPointSegmentDistance2D(s->verts[i].xyz, boundary.a, boundary.b);
					if (distance < boundaryDistance) { boundaryDistance = distance; rigidBoundary = boundary.rigid; }
				}
				// Half-float safe signed encoding. The 0.25 bias retains class at an
				// exact boundary (negative rigid, positive natural).
				v.texcoords[1][1] = (rigidBoundary ? -1.0f : 1.0f) * (MIN(boundaryDistance, 65000.0f) + 0.25f);
				verts.push_back(v);
			}
			std::vector<glIndex_t> tris;
			for (int i = 0; i < s->numIndexes; ++i)
				tris.push_back((glIndex_t)(firstVert + s->indexes[i]));
			std::unordered_map<uint64_t, glIndex_t> edges;
			const int64_t subdivisionSegments = 1LL << level;
			const int64_t estimatedSurfaceVerts = s->numVerts + (int64_t)(s->numIndexes / 3) *
				((subdivisionSegments + 1) * (subdivisionSegments + 2) / 2);
			edges.reserve((size_t)MIN(estimatedSurfaceVerts, (int64_t)bodyCap));
			for (int pass = 0; pass < level; ++pass)
			{
				std::vector<glIndex_t> next;
				next.reserve(tris.size() * 4);
				for (size_t i = 0; i < tris.size(); i += 3)
				{
					const glIndex_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
					const glIndex_t ab = R_WaterMidpoint(verts, edges, a, b);
					const glIndex_t bc = R_WaterMidpoint(verts, edges, b, c);
					const glIndex_t ca = R_WaterMidpoint(verts, edges, c, a);
					const glIndex_t child[] = { a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca };
					next.insert(next.end(), child, child + 12);
				}
				tris.swap(next);
			}
			for (size_t i = 0; i < tris.size(); i += 3)
				for (int edge = 0; edge < 3; ++edge)
				{
					const glIndex_t a = tris[i + edge], b = tris[i + (edge + 1) % 3];
					const float dx = verts[a].position[0] - verts[b].position[0];
					const float dy = verts[a].position[1] - verts[b].position[1];
					maxTopEdge = MAX(maxTopEdge, sqrtf(dx * dx + dy * dy));
				}
			indices.insert(indices.end(), tris.begin(), tris.end());
			// A short downward skirt closes the wall gap when an edge rises and
			// covers mismatched BSP boundary segmentation between touching faces.
			std::unordered_map<uint64_t, int> edgeCount;
			if (!body.waterfall)
			{
			edgeCount.reserve(tris.size());
			std::unordered_map<uint64_t, std::pair<glIndex_t, glIndex_t>> oriented;
			oriented.reserve(tris.size());
			for (size_t i = 0; i < tris.size(); i += 3)
				for (int e = 0; e < 3; ++e)
				{
					glIndex_t a = tris[i + e], b = tris[i + (e + 1) % 3];
					uint64_t key = ((uint64_t)MIN(a, b) << 32) | MAX(a, b);
					++edgeCount[key]; oriented[key] = std::make_pair(a, b);
				}
			std::unordered_map<glIndex_t, glIndex_t> feet;
			feet.reserve(edgeCount.size());
			for (const auto& edge : edgeCount)
			{
				if (edge.second != 1) continue;
				glIndex_t top[2] = { oriented[edge.first].first, oriented[edge.first].second };
				if (r_waterShoreline->integer)
				{
					bool bodyBoundary = false;
					for (const boundary_t& boundary : boundaries)
					{
						if (R_WaterPointSegmentDistance2D(verts[top[0]].position, boundary.a, boundary.b) <= 0.125f &&
							R_WaterPointSegmentDistance2D(verts[top[1]].position, boundary.a, boundary.b) <= 0.125f)
						{
							bodyBoundary = true;
							break;
						}
					}
					// This is only a local surface boundary.  A matching edge on another
					// BSP surface makes it internal to the body and it needs no skirt.
					if (!bodyBoundary) continue;
				}
				glIndex_t foot[2];
				const float edgeX = verts[top[1]].position[0] - verts[top[0]].position[0];
				const float edgeY = verts[top[1]].position[1] - verts[top[0]].position[1];
				const float edgeLength = sqrtf(edgeX * edgeX + edgeY * edgeY);
				const float outwardX = edgeLength > 1.0e-5f ? -edgeY / edgeLength : 0.0f;
				const float outwardY = edgeLength > 1.0e-5f ? edgeX / edgeLength : 0.0f;
				for (int j = 0; j < 2; ++j)
				{
					const bool rigid = verts[top[j]].texcoords[1][1] < 0.0f;
					auto found = feet.find(top[j]);
					if ((!r_waterShoreline->integer || rigid) && found != feet.end()) foot[j] = found->second;
					else
					{
						packedVertex_t v = verts[top[j]];
						const float waveEnvelope = s_dynamics[body.dynamics].amplitude * 8.0f *
							Com_Clamp(0.0f, 4.0f, r_waterWaveAmplitude->value) * MAX(body.waveMultiplier, 1.0f);
						const float overlap = !r_waterShoreline->integer ? 64.0f :
							(rigid ? MAX(r_waterShoreContactSoftness->value, waveEnvelope + 2.0f) :
							MAX(1.0f, r_waterShoreContactSoftness->value * 0.5f));
						v.position[2] -= r_waterShoreline->integer ? MIN(overlap, 32.0f) : overlap;
						if (r_waterShoreline->integer && !rigid)
						{
							v.position[0] += outwardX * r_waterShoreContactSoftness->value;
							v.position[1] += outwardY * r_waterShoreContactSoftness->value;
						}
						v.texcoords[1][0] = 0.0f;
						foot[j] = (glIndex_t)verts.size();
						verts.push_back(v);
						if (!r_waterShoreline->integer || rigid) feet[top[j]] = foot[j];
					}
				}
				const glIndex_t skirt[] = { top[0], foot[0], top[1], top[1], foot[0], foot[1] };
				indices.insert(indices.end(), skirt, skirt + 6);
			}
			}
			if (!tris.empty())
			{
				glIndex_t lo = indices[firstIndex], hi = indices[firstIndex];
				for (size_t i = firstIndex; i < indices.size(); ++i) { lo = MIN(lo, indices[i]); hi = MAX(hi, indices[i]); }
				ranges.push_back({ s, firstIndex, (int)indices.size() - firstIndex, (int)verts.size() - firstVert, lo, hi });
			}
		}
		if (verts.size() > (size_t)MIN(bodyCap, mapCap - mapVerts) || indices.empty())
		{
			ri.Printf(PRINT_WARNING, "water geometry body %d: actual vertex budget exceeded, using BSP mesh\n", body.id);
			continue;
		}
		VBO_t *vbo = R_CreateVBO((byte *)verts.data(), verts.size() * sizeof(packedVertex_t),
			VBO_USAGE_STATIC, va("WaterBody_%d", body.id));
		IBO_t *ibo = R_CreateIBO((byte *)indices.data(), indices.size() * sizeof(glIndex_t),
			VBO_USAGE_STATIC, va("WaterBody_%d", body.id));
		vbo->offsets[ATTR_INDEX_POSITION] = offsetof(packedVertex_t, position);
		vbo->offsets[ATTR_INDEX_NORMAL] = offsetof(packedVertex_t, normal);
		vbo->offsets[ATTR_INDEX_TEXCOORD0] = offsetof(packedVertex_t, texcoords[0]);
		vbo->offsets[ATTR_INDEX_TEXCOORD1] = offsetof(packedVertex_t, texcoords[1]);
		vbo->strides[ATTR_INDEX_POSITION] = sizeof(packedVertex_t);
		vbo->strides[ATTR_INDEX_NORMAL] = sizeof(packedVertex_t);
		vbo->strides[ATTR_INDEX_TEXCOORD0] = sizeof(packedVertex_t);
		vbo->strides[ATTR_INDEX_TEXCOORD1] = sizeof(packedVertex_t);
		vbo->sizes[ATTR_INDEX_POSITION] = sizeof(verts[0].position);
		vbo->sizes[ATTR_INDEX_NORMAL] = sizeof(verts[0].normal);
		vbo->sizes[ATTR_INDEX_TEXCOORD0] = sizeof(verts[0].texcoords[0]);
		vbo->sizes[ATTR_INDEX_TEXCOORD1] = sizeof(verts[0].texcoords[1]);
		for (const range_t& r : ranges)
		{
			r.s->waterVbo = vbo; r.s->waterIbo = ibo;
			r.s->waterFirstIndex = r.first; r.s->waterNumIndexes = r.count;
			r.s->waterNumVerts = r.verts; r.s->waterMinIndex = r.min; r.s->waterMaxIndex = r.max;
		}
		// The render mesh may move outside the BSP's flat bounds. Preserve its
		// original plane for grouping/reflection; use expanded bounds for culling.
		const float extent = body.waterfall ? 4.0f + 10.0f * body.waterfallValues.breakup :
			4.0f + s_dynamics[body.dynamics].amplitude * 8.0f * 4.0f * MAX(body.waveMultiplier, 1.0f) * 2.0f;
		const float interactionExtent = !body.waterfall && body.interaction.image && r_waterInteraction->integer ? 64.0f : 0.0f;
		for (int surfaceNum : body.surfaces)
		{
			msurface_t& ms = world->surfaces[surfaceNum];
			if (!(ms.cullinfo.type & CULLINFO_BOX)) continue;
			for (int axis = 0; axis < 3; ++axis)
			{
				ms.cullinfo.bounds[0][axis] -= extent + (!body.waterfall && axis == 2 ? 64.0f : 0.0f);
				ms.cullinfo.bounds[1][axis] += extent + (axis == 2 ? interactionExtent : 0.0f);
			}
			ms.cullinfo.type &= ~CULLINFO_PLANE;
			ms.cullinfo.type &= ~CULLINFO_SPHERE;
		}
		body.geometryVerts = (int)verts.size();
		body.geometryTriangles = (int)indices.size() / 3;
		body.geometryLevel = level;
		body.geometryMaxEdge = maxTopEdge;
		mapVerts += body.geometryVerts;
		s_water.geometryBytes += verts.size() * sizeof(packedVertex_t) + indices.size() * sizeof(glIndex_t);
	}
	s_water.geometryMsec = (float)(ri.Milliseconds() - start);
}

void R_WaterGeometryInfo_f(void)
{
	ri.Printf(PRINT_ALL, "water geometry: %d bodies, %.1f ms, %u bytes GPU mesh, %d shoreline contact segments (%u CPU bytes)\n",
		(int)s_water.bodies.size(), s_water.geometryMsec, (unsigned)s_water.geometryBytes,
		(int)s_water.contacts.size(), (unsigned)(s_water.contacts.capacity() * sizeof(waterContactSegment_t)));
	for (const waterBody_t& body : s_water.bodies)
		ri.Printf(PRINT_ALL, "  body %d: %d vertices, %d triangles, level %d, max top edge %.1f\n",
			body.id, body.geometryVerts, body.geometryTriangles, body.geometryLevel, body.geometryMaxEdge);
}

void R_WaterSurfaceShutdown( void )
{
	R_WaterMotionInvalidate(WM_INVALID_MAP_CHANGE);
	// images and FBOs belong to the renderer lists; the resource decision
	// follows the programs (R_CreateWaterSurfaceImages)
	s_water.world = NULL;
	s_water.mergedViewSurfaces.clear();
	s_water.history.valid = qfalse;
	s_water.previousInteractionAtlas = nullptr;
	s_water.motionSnapshots.clear();
	s_water.pendingMotionSnapshots.clear();
	s_water.motionWaveTimeValid = false;
	s_water.motionCopyMsec = 0.0f;
	s_water.motionSnapshotUsec = 0.0;
	s_water.motionSnapshotCount = 0;
	s_water.brushes.clear();
	s_water.planes.clear();
	s_water.sources.clear();
	s_water.surfaces.clear();
	s_water.shaders.clear();
	s_water.bodies.clear();
	s_water.contacts.clear();
	s_water.contactTime = 0;
	s_water.contactUniformTime = -1;
	s_water.bodyRules.clear();
	s_water.waterfallRules.clear();
	s_water.waterfallEmitterRules.clear();
	s_water.waterfallImpacts.clear();
	s_water.mapWindValid = false;
	VectorClear(s_water.mapWindDirection);
	s_water.mapWindStrength = 0.0f;
	s_water.pendingImpulses.clear();
	s_water.continuousSources.clear();
	s_water.wakeTracks.clear();
	s_water.interactionGpuBytes = 0;
	s_water.interactionTime = -1;
	s_water.interactionSourceMsec = s_water.interactionStepMsec = s_water.interactionUploadMsec = 0.0f;
	s_water.interactionSubmitUsec = 0.0;
	s_water.interactionSubmissions = 0;
	s_water.bodyDebugShader = 0;
	s_water.geometryBytes = 0;
	s_water.geometryMsec = 0;
	RB_WaterIntersectionReleaseHits();
	s_water.intersectionGeneration++;
}

/*
============================================================

View and draws

============================================================
*/

/*
=================
RB_WaterSurfaceBeginView

Called by RB_BeginDrawingView after the screen-space passes decided theirs:
the main view of the world, rendered into renderFbo.
=================
*/
static bool RB_WaterPrimaryFrame(void)
{
	return backEnd.viewParms.viewParmType == VPT_MAIN && !backEnd.viewParms.isPortal &&
		!backEnd.viewParms.isSkyPortal && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW) &&
		tr.world && !(backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE));
}

void RB_WaterSurfaceBeginView( void )
{
	backEnd.waterSurfaceView = qfalse;
	backEnd.waterSurfaceSSR = qfalse;
	backEnd.waterItemTag = qfalse;
	backEnd.waterLegacyClipTag = qfalse;
	s_water.reflectionReady = qfalse;
	s_water.viewWaterReady = qfalse;

	if ( s_water.statsFrame != backEndData->realFrameNumber )
	{
		s_water.lastDraws = s_water.drawsThisFrame;
		s_water.drawsThisFrame = 0;
		s_water.statsFrame = backEndData->realFrameNumber;
	}

	// Advance once for the real main frame. Portal, mirror and reflection views
	// sample the same uploaded state and can never consume another time step.
	const bool primaryWaterFrame = RB_WaterPrimaryFrame();
	if (primaryWaterFrame)
	{
		// Classification mutates body keys and therefore motion identity.  Defer
		// the live toggle until this same canonical primary-frame boundary.
		if ( r_waterSurfaceExperimental->modified )
		{
			r_waterSurfaceExperimental->modified = qfalse;
			if ( s_water.world == tr.world )
				R_WaterDecideShaders();
		}
		if (r_waterSSRWaveReprojection && r_waterSSRWaveReprojection->integer)
		{
			const bool frozen = r_waterWaveTime->value >= 0.0f;
			const float waveTime = frozen ? r_waterWaveTime->value : backEnd.refdef.floatTime;
			if (s_water.motionWaveTimeValid && (frozen != s_water.motionWaveTimeFrozen ||
				waveTime < s_water.motionLastWaveTime - 1.0e-4f || waveTime - s_water.motionLastWaveTime > 1.0f))
				R_WaterMotionInvalidate(WM_INVALID_TIME_DISCONTINUITY);
			s_water.motionLastWaveTime = waveTime;
			s_water.motionWaveTimeFrozen = frozen;
			s_water.motionWaveTimeValid = true;
		}
		RB_UpdateWeatherWind();
		R_WaterFlowImpactSlaps(backEnd.refdef.time);
		RB_WaterInteractionUpdate();
		RB_WaterFoamUpdate();
		RB_WaterContactUpdate();
	}

	if ( !s_water.resources || (!r_waterSurface->integer && (!r_waterfall || !r_waterfall->integer)) || !tr.waterCopyFbo || !backEnd.waterInterfacesVisible )
		return;
	const viewParms_t& viewParms = backEnd.viewParms;
	if ( viewParms.viewParmType != VPT_MAIN || viewParms.isPortal || viewParms.isSkyPortal )
		return;
	if ( viewParms.flags & VPF_DEPTHSHADOW )
		return;
	if ( viewParms.targetFbo != NULL && viewParms.targetFbo != tr.renderFbo )
		return;
	if ( glState.currentFBO != tr.renderFbo || backEnd.framePostProcessed )
		return;
	if ( !tr.world || (backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE)) )
		return;

	backEnd.waterSurfaceView = qtrue;
	// the SSR inputs of the view (depth pyramid, opaque color pyramid) are
	// built by RB_RenderSSR before the water slot
	backEnd.waterSurfaceSSR = (qboolean)(backEnd.ssrView && R_SSRResourcesEnabled() &&
		tr.screenHiZImage && tr.ssrColorImage && r_waterSurfaceSSR->value > 0.0f);
}

qboolean RB_WaterSurfaceDraws( const shader_t *shader )
{
	if ( !backEnd.waterSurfaceView || !shader || !(tess.waterKey & WATERKEY_INTERFACE) )
		return qfalse;
	if ((tess.waterKey & WATERKEY_WATERFALL) ? (!r_waterfall || !r_waterfall->integer) : !r_waterSurface->integer)
		return qfalse;
	if ( backEnd.depthFill || backEnd.refractionFill || backEnd.projection2D )
		return qfalse;
	if ( backEnd.currentEntity && (backEnd.currentEntity->e.renderfx & RF_DISTORTION) )
		return qfalse;
	if ( !tr.waterSurfaceShader[0].program )
		return qfalse;
	return qtrue;
}

shaderProgram_t *RB_WaterSurfaceProgram( const shader_t *shader )
{
	int index = 0;
	if ( shader->numDeforms && !ShaderRequiresCPUDeforms(shader) )
		index |= WATERDEF_USE_DEFORM_VERTEXES;
	if ( r_waterSnell->integer )
		index |= WATERDEF_USE_SNELL;
	// the SSR of the view walks the closest depth mips: so does the water
	if ( backEnd.waterSurfaceSSR && RB_SSRDepthLevels() > 1 && tr.waterSurfaceShader[index | WATERDEF_USE_HIZ].program )
		index |= WATERDEF_USE_HIZ;
	s_water.drawsThisFrame++;
	return &tr.waterSurfaceShader[index];
}

/*
=================
RB_WaterSurfaceSetupDraw

The water specific uniforms and textures of a draw (the uniform blocks, the
vertex data and the stage's tcMod are set by the stage iterator).
=================
*/
static void RB_WaterSurfaceCacheView( void )
{
	if (s_water.viewWaterReady)
		return;
	s_water.viewWaterReady = qtrue;
	const viewParms_t& v = backEnd.viewParms;
	vec4_t *water = s_water.viewWater;
	Com_Memset(water, 0, sizeof(s_water.viewWater));
	VectorSet4(water[0], Com_Clamp(1.0f, 2.0f, r_waterSurfaceIOR->value),
		Com_Clamp(0.0f, 1.0f, r_waterSurfaceRoughness->value),
		Com_Clamp(0.0f, 4.0f, r_waterSurfaceNormal->value), Com_Clamp(0.0f, 4.0f, r_waterSurfaceRefraction->value));
	VectorSet4(water[1], Com_Clamp(0.0f, 4.0f, r_waterSurfaceReflection->value),
		backEnd.waterSurfaceSSR ? Com_Clamp(0.0f, 1.0f, r_waterSurfaceSSR->value) : 0.0f,
		1.0f, Com_Clamp(0.0f, 16.0f, r_waterSurfaceDepthScale->value));
	for (int c = 0; c < LIQUID_CLASSES; c++)
		R_LiquidsMaterial(c, s_water.extinction[c], s_water.albedo[c]);
	const float *proj = v.projectionMatrix;
	VectorSet4(water[4], proj[0], proj[5], proj[8], proj[9]);
	VectorSet4(water[5], proj[14], proj[10],
		r_waterWaveTime->value >= 0.0f ? r_waterWaveTime->value : backEnd.refdef.floatTime,
		tr.linearLight ? 1.0f : 0.0f);
	const int debug = r_waterSurfaceDebug->integer;
	VectorSet4(water[6], debug == 9 ? 0.0f : (float)debug,
		debug == 9 ? v.viewportX + Com_Clamp(0.0f, 1.0f, r_waterSurfaceSplit->value) * v.viewportWidth : -1.0f,
		0.0f, 8192.0f);
	vec3_t right, up;
	VectorScale(v.ori.axis[1], -1.0f, right);
	VectorCopy(v.ori.axis[2], up);
	VectorNormalize(right);
	VectorNormalize(up);
	const float farZ = R_VolumetricFarZ();
	VectorSet4(water[7], right[0], right[1], right[2], farZ * 0.8f);
	VectorSet4(water[8], up[0], up[1], up[2], 1.0f / MAX(farZ * 0.2f, 1.0f));
	vec3_t env;
	VectorCopy(backEnd.refdef.sunAmbCol, env);
	if (VectorLength(env) < 0.02f)
		VectorSet(env, 0.08f, 0.09f, 0.1f);
	const bool sun = r_sunlightMode->integer && (v.flags & VPF_USESUNLIGHT) && tr.sunShadowArrayImage;
	VectorSet4(water[9], env[0], env[1], env[2], sun ? 1.0f : 0.0f);
	VectorSet4(water[10], 1.0f, 1.0f / 192.0f, 0.0f,
		r_waterSnell->integer ? (float)r_waterSnellDebug->integer : 0.0f);
	const double t = r_waterWaveTime->value >= 0.0f ? r_waterWaveTime->value : backEnd.refdef.floatTime;
	auto wrap = [](double value) -> float { return (float)(value - floor(value)); };
	VectorSet4(water[11], wrap(0.8 * 0.020 * t), wrap(0.6 * 0.020 * t),
		wrap(0.28 * 0.034 * t + 0.37), wrap(0.96 * 0.034 * t + 0.71));
	VectorSet4(water[12], (float)v.viewportX / tr.waterSceneImage->width,
		(float)v.viewportY / tr.waterSceneImage->height,
		(float)v.viewportWidth / tr.waterSceneImage->width,
		(float)v.viewportHeight / tr.waterSceneImage->height);
	VectorSet4(water[13], r_waterWaves->integer ? 1.0f : 0.0f,
		Com_Clamp(0.0f, 4.0f, r_waterWaveAmplitude->value),
		Com_Clamp(0.25f, 4.0f, r_waterWaveLength->value),
		Com_Clamp(0.0f, 4.0f, r_waterWaveSpeed->value));
	VectorSet4(water[14], Com_Clamp(0.0f, 2.0f, r_waterWaveChoppiness->value),
		(float)Com_Clampi(0, 2, r_waterWaveQuality->integer),
		Com_Clamp(0.0f, 4.0f, r_waterWaveMicro->value),
		r_waterWaveShallow->integer ? 1.0f : 0.0f);
	s_water.viewFlags = (sun ? 2 : 0) | (RB_VolumetricLookupReady() ? 4 : 0) |
		(r_waterSurfaceDepthReject->integer ? 8 : 0) | (backEnd.waterSurfaceSSR ? 16 : 0) |
		(tr.envBrdfImage ? 512 : 0);
	if (r_waterSnell->integer && r_waterSnellDebug->integer == 1 && R_LiquidPointClass(v.ori.origin) >= 0)
		s_water.viewFlags |= 1024;
	s_water.fallbackCubemap = r_cubeMapping->integer && !(v.flags & VPF_NOCUBEMAPS) ? R_CubemapForPoint(v.ori.origin) : 0;
	if (backEnd.waterSurfaceSSR)
	{
		RB_ScreenGetViewInfo(s_water.viewInfo);
		int steps, refine;
		qboolean hiZ;
		RB_SSRTraceParams(&steps, &refine, &hiZ);
		VectorSet4(s_water.ssrSettings[0], (float)Com_Clampi(1, 256, steps), (float)Com_Clampi(0, 16, refine),
			r_ssrMaxDistance->value, r_ssrThickness->value);
		VectorSet4(s_water.ssrSettings[1], MAX(r_ssrMaxRoughness->value, 0.05f), r_ssrEdgeFade->value, SSR_COLOR_MIPS - 1, 0.0f);
		VectorSet4(s_water.ssrSettings[2], SCREEN_HIZ_MIPS - 1, 0.0f, r_znear->value, (float)Com_Clampi(8, 1024, steps * 3));
	}
}

static void R_WaterBuildWindWaveSet(const waterBody_t& body, const waterDynamics_t& dynamics,
	float amplitude, float wavelength, float response, float rainNoise, const vec2_t windDirection,
	waterWaveSet_t& set)
{
	vec2_t mainDirection = { 0.8f, 0.6f };
	if (body.flowSpeed > 0.001f)
	{
		mainDirection[0] = body.flowDirection[0];
		mainDirection[1] = body.flowDirection[1];
		const float length = sqrtf(mainDirection[0] * mainDirection[0] + mainDirection[1] * mainDirection[1]);
		if (length < 1.0e-5f) { mainDirection[0] = 0.8f; mainDirection[1] = 0.6f; }
		else { mainDirection[0] /= length; mainDirection[1] /= length; }
	}
	const float directionBias = Com_Clamp(0.0f, 0.88f, response * 0.88f);
	mainDirection[0] = mainDirection[0] * (1.0f - directionBias) + windDirection[0] * directionBias;
	mainDirection[1] = mainDirection[1] * (1.0f - directionBias) + windDirection[1] * directionBias;
	const float mainLength = sqrtf(mainDirection[0] * mainDirection[0] + mainDirection[1] * mainDirection[1]);
	if (mainLength < 1.0e-5f) { mainDirection[0] = 0.8f; mainDirection[1] = 0.6f; }
	else { mainDirection[0] /= mainLength; mainDirection[1] /= mainLength; }
	VectorCopy2(mainDirection, set.direction);
	set.strength = response;
	set.choppiness = dynamics.choppiness * (1.0f + 1.4f * response);
	set.micro = dynamics.microNormal * body.waveMultiplier * (1.0f + 1.6f * response + rainNoise);
	const float directionMix = body.flowSpeed > 0.001f ? 0.72f : (0.22f + 0.58f * response);
	const float baseK = 6.2831853f / MAX(wavelength, 4.0f);
	const float waveSpeed = dynamics.speed * Com_Clamp(0.0f, 4.0f, r_waterWaveSpeed->value) * 16.0f;
	// Mode 1 normalizes finite-depth dispersion around the authored primary
	// wave, preserving compatibility. Mode 2 uses an independent renderer
	// gravity calibration for still/outdoor bodies; flow-dominated profiles
	// deliberately retain mode 1 because authored flow controls their motion.
	const float depth = body.depthAverage;
	const float baseDepthTerm = depth > 0.0f ? tanhf(baseK * depth) : 1.0f;
	const int dispersionMode = Com_Clampi(0, 2, r_waterWaveDispersion->integer);
	const bool physicalDispersion = dispersionMode == 2 && body.flowSpeed <= 0.001f;
	for (int i = 0; i < 8; ++i)
	{
		const float fi = (float)i;
		float dx = s_waterWaveDirectionBasis[i][0] * (1.0f - directionMix) + mainDirection[0] * directionMix;
		float dy = s_waterWaveDirectionBasis[i][1] * (1.0f - directionMix) + mainDirection[1] * directionMix;
		const float invLength = 1.0f / sqrtf(MAX(dx * dx + dy * dy, 1.0e-8f));
		dx *= invLength; dy *= invLength;
		const float scale = i < 4 ? (1.0f - 0.15f * fi) : (0.34f - 0.035f * (fi - 4.0f));
		const float lambdaScale = i < 4 ? (1.0f - 0.13f * fi) : (0.28f - 0.025f * (fi - 4.0f));
		const float bandEnergy = 1.0f + response * (i < 4 ? 1.35f : 0.85f) + (i >= 4 ? rainNoise : 0.0f);
		const float a = amplitude * scale * (i < 4 ? 0.28f : 0.10f) * bandEnergy;
		const float k = 6.2831853f / MAX(wavelength * lambdaScale, 4.0f);
		VectorSet4(set.terms[i], dx, dy, a, k);
		const float depthTerm = depth > 0.0f ? tanhf(k * depth) : 1.0f;
		const float normalizedOmega = waveSpeed * baseK *
			sqrtf(MAX(k * depthTerm / MAX(baseK * baseDepthTerm, 1.0e-8f), 0.0f));
		const float linearOmega = waveSpeed * k;
		if (dispersionMode == 0)
			set.omega[i] = linearOmega;
		else if (physicalDispersion)
			set.omega[i] = sqrtf(MAX(r_waterWaveGravity->value * k * depthTerm, 0.0f));
		else
			set.omega[i] = linearOmega + (normalizedOmega - linearOmega) * dynamics.dispersion;
	}
}

static float R_WaterResolveWind(waterBody_t& body, const precipitationState_t& precipitation,
	vec2_t direction, float& rainNoise)
{
	const waterDynamics_t& dynamics = s_dynamics[body.dynamics];
	vec3_t center = {
		0.5f * (body.bounds[0][0] + body.bounds[1][0]),
		0.5f * (body.bounds[0][1] + body.bounds[1][1]),
		body.bounds[1][2] + 4.0f
	};
	body.wind.outdoor = R_IsOutside(center);
	vec3_t resolved = { 0.8f, 0.6f, 0.0f };
	float strength = 0.0f;
	int source = WWIND_NONE;
	bool valid = false;
	if (body.hasExplicitWind)
	{
		VectorCopy(body.explicitWindDirection, resolved);
		strength = body.explicitWindStrength; source = WWIND_MAP; valid = true;
	}
	else if (s_water.mapWindValid)
	{
		VectorCopy(s_water.mapWindDirection, resolved);
		strength = s_water.mapWindStrength; source = WWIND_MAP; valid = true;
	}
	else
	{
		vec3_t weatherWind;
		VectorCopy(precipitation.wind, weatherWind);
		const float weatherStrength = VectorNormalize(weatherWind);
		// Rain particles get their lateral force from this same resolved field.
		// Rain presence alone is deliberately not a wind source.
		if (weatherStrength > 1.0e-4f)
		{
			VectorCopy(weatherWind, resolved);
			strength = Com_Clamp(0.0f, 1.0f, weatherStrength / 3.0f);
			source = WWIND_WEATHER; valid = true;
		}
	}
	if (!valid && dynamics.defaultWind > 0.0f)
	{
		if (body.flowSpeed > 0.001f) VectorCopy(body.flowDirection, resolved);
		strength = dynamics.defaultWind; source = WWIND_PROFILE; valid = true;
	}
	float horizontal = sqrtf(resolved[0] * resolved[0] + resolved[1] * resolved[1]);
	if (horizontal < 1.0e-5f) { resolved[0] = 0.8f; resolved[1] = 0.6f; horizontal = 1.0f; }
	direction[0] = resolved[0] / horizontal; direction[1] = resolved[1] / horizontal;
	body.wind.resolved[0] = direction[0]; body.wind.resolved[1] = direction[1]; body.wind.resolved[2] = 0.0f;
	body.wind.strength = strength; body.wind.source = source; body.wind.valid = valid;
	const float exposure = body.wind.outdoor ? 1.0f : 0.05f;
	rainNoise = 0.0f;
	if (precipitation.current && body.wind.outdoor)
		rainNoise = precipitation.subtype == RAIN_WEATHER_HEAVY ? 0.16f :
			(precipitation.subtype == RAIN_WEATHER_NORMAL || precipitation.subtype == RAIN_WEATHER_ACID ? 0.06f :
			(precipitation.subtype == RAIN_WEATHER_LIGHT ? 0.025f : 0.0f));
	return r_waterWind->integer && valid ? Com_Clamp(0.0f, 1.5f,
		strength * dynamics.windCoupling * exposure * r_waterWindScale->value) : 0.0f;
}

static void R_WaterUpdateWindWaves(waterBody_t& body, float amplitude, float wavelength,
	float now, const precipitationState_t& precipitation, vec4_t *water)
{
	// A body can have several BSP ranges and can be queried by shoreline/gameplay
	// before it is drawn. Resolve its invariant terms once for a renderer time,
	// then only copy the cached old/target sets for subsequent draws/queries.
	if (body.wind.updateTime != now)
	{
		vec2_t windDirection;
		float rainNoise;
		const float response = R_WaterResolveWind(body, precipitation, windDirection, rainNoise);
		waterWaveSet_t desired;
		R_WaterBuildWindWaveSet(body, s_dynamics[body.dynamics], amplitude, wavelength,
			response, r_waterWind->integer ? rainNoise : 0.0f, windDirection, desired);
		if (!body.wind.initialized)
		{
			body.wind.oldSet = body.wind.targetSet = desired;
			body.wind.blendStart = now;
			body.wind.blend = 1.0f;
			body.wind.initialized = true;
		}
		const float transition = MAX(r_waterWindTransition->value, 0.25f);
		body.wind.blend = Com_Clamp(0.0f, 1.0f, (now - body.wind.blendStart) / transition);
		const float directionDelta = body.wind.targetSet.direction[0] * desired.direction[0] +
			body.wind.targetSet.direction[1] * desired.direction[1];
		bool spectrumChanged = false;
		for (int i = 0; i < 8 && !spectrumChanged; ++i)
		{
			spectrumChanged = fabsf(body.wind.targetSet.terms[i][2] - desired.terms[i][2]) > 1.0e-4f ||
				fabsf(body.wind.targetSet.terms[i][3] - desired.terms[i][3]) > 1.0e-6f ||
				fabsf(body.wind.targetSet.omega[i] - desired.omega[i]) > 1.0e-5f;
		}
		if (body.wind.blend >= 1.0f && (directionDelta < 0.985f ||
			fabsf(body.wind.targetSet.strength - desired.strength) > 0.04f ||
			fabsf(body.wind.targetSet.micro - desired.micro) > 0.04f || spectrumChanged))
		{
			body.wind.oldSet = body.wind.targetSet;
			body.wind.targetSet = desired;
			body.wind.blendStart = now;
			body.wind.blend = 0.0f;
		}
		body.wind.rainNoise = rainNoise;
		body.wind.updateTime = now;
	}
	for (int i = 0; i < 8; ++i)
	{
		VectorCopy4(body.wind.targetSet.terms[i], water[WATER_U_TARGET_TERMS + i]);
		VectorCopy4(body.wind.oldSet.terms[i], water[WATER_U_OLD_TERMS + i]);
		water[WATER_U_TARGET_OMEGA + i / 4][i & 3] = body.wind.targetSet.omega[i];
		water[WATER_U_OLD_OMEGA + i / 4][i & 3] = body.wind.oldSet.omega[i];
	}
	water[18][2] = body.wind.oldSet.choppiness +
		(body.wind.targetSet.choppiness - body.wind.oldSet.choppiness) * body.wind.blend;
	water[15][3] = body.wind.oldSet.micro +
		(body.wind.targetSet.micro - body.wind.oldSet.micro) * body.wind.blend;
	vec2_t current = {
		body.wind.oldSet.direction[0] + (body.wind.targetSet.direction[0] - body.wind.oldSet.direction[0]) * body.wind.blend,
		body.wind.oldSet.direction[1] + (body.wind.targetSet.direction[1] - body.wind.oldSet.direction[1]) * body.wind.blend
	};
	const float currentLength = sqrtf(current[0] * current[0] + current[1] * current[1]);
	if (currentLength > 1.0e-5f) { current[0] /= currentLength; current[1] /= currentLength; }
	VectorSet4(water[WATER_U_WIND], body.wind.resolved[0], body.wind.resolved[1], body.wind.strength, (float)body.wind.source);
	VectorSet4(water[WATER_U_WIND + 1], current[0], current[1], body.wind.targetSet.direction[0], body.wind.targetSet.direction[1]);
	VectorSet4(water[WATER_U_WIND + 2], body.wind.blend, (float)r_waterWindDebug->integer, body.wind.outdoor ? 1.0f : 0.0f, body.wind.rainNoise);
}

static bool R_WaterBodyCoversXY(const waterBody_t& body, const vec3_t position)
{
	// Use the submitted interface triangles rather than an AABB. This keeps
	// disconnected pools and holes in a concave body from stealing the camera.
	for (int surfaceNum : body.surfaces)
	{
		const msurface_t& ms = s_water.world->surfaces[surfaceNum];
		if (!(ms.waterKey & WATERKEY_INTERFACE) || !ms.data ||
			!(*ms.data == SF_FACE || *ms.data == SF_GRID || *ms.data == SF_TRIANGLES))
			continue;
		const srfBspSurface_t *surface = (const srfBspSurface_t *)ms.data;
		for (int i = 0; i + 2 < surface->numIndexes; i += 3)
		{
			const int ia = surface->indexes[i], ib = surface->indexes[i + 1], ic = surface->indexes[i + 2];
			if (ia < 0 || ib < 0 || ic < 0 || ia >= surface->numVerts || ib >= surface->numVerts || ic >= surface->numVerts)
				continue;
			if (R_WaterInteractionPointInTriangle(position[0], position[1],
				surface->verts[ia].xyz, surface->verts[ib].xyz, surface->verts[ic].xyz))
				return true;
		}
	}
	return false;
}

static float R_WaterlineShoreAttenuation(const waterBody_t& body, const vec3_t position,
	float *interactionAttenuation)
{
	*interactionAttenuation = 1.0f;
	if (!r_waterShoreline->integer || !r_waterGeometry->integer)
		return 1.0f;
	float distance = 65504.0f;
	bool rigid = false;
	for (const waterContactSegment_t& contact : s_water.contacts)
	{
		if (contact.bodyId != body.id) continue;
		const float d = R_WaterPointSegmentDistance2D(position, contact.a, contact.b);
		if (d < distance) { distance = d; rigid = contact.rigid; }
	}
	if (distance >= 65000.0f) return 1.0f;
	const float profile = Com_Clamp(0.0f, 1.5f, s_dynamics[body.dynamics].shoreline);
	const float edge = rigid ? Com_Clamp(0.03f, 0.08f, 0.03f + profile * 0.033333f) :
		Com_Clamp(0.15f, 0.40f, 0.15f + profile * 0.25f);
	const float interactionEdge = rigid ? Com_Clamp(0.45f, 0.55f, 0.45f + profile * 0.05f) :
		Com_Clamp(0.60f, 0.75f, 0.60f + profile * 0.10f);
	const float width = MAX(r_waterShoreAttenuation->value * (rigid ? 1.0f : 0.35f), 1.0f);
	const float x = Com_Clamp(0.0f, 1.0f, distance / width);
	const float smooth = x * x * (3.0f - 2.0f * x);
	const float attenuation = edge + (1.0f - edge) * smooth;
	*interactionAttenuation = MAX(attenuation, interactionEdge);
	return attenuation;
}

static float R_WaterlineWaveHeight(const waterBody_t& body, const vec3_t position,
	float time, float shoreAttenuation, vec4_t *water)
{
	if (!r_waterWaves->integer || !r_waterGeometry->integer || body.geometryVerts <= 0)
		return 0.0f;
	const int shadingCount = r_waterWaveQuality->integer <= 0 ? 2 : (r_waterWaveQuality->integer == 1 ? 4 : 8);
	const int geometryCount = R_WaterGeometryComponentCount(body, &water[WATER_U_TARGET_TERMS]);
	const float qualityAmplitude = shadingCount == 2 ? 1.86f : (shadingCount == 8 ? 0.59f : 1.0f);
	float shallow = 1.0f;
	if (r_waterWaveShallow->integer && body.depthAverage > 0.0f)
	{
		const float x = Com_Clamp(0.0f, 1.0f, body.depthAverage / 32.0f);
		shallow = x * x * (3.0f - 2.0f * x);
	}
	auto evaluate = [&](int firstTerm) -> float {
		const int omegaBase = firstTerm == WATER_U_TARGET_TERMS ? WATER_U_TARGET_OMEGA : WATER_U_OLD_OMEGA;
		float height = 0.0f;
		for (int component = 0; component < geometryCount; ++component)
		{
			const vec4_t& term = water[firstTerm + component];
			const float phase = term[3] * (term[0] * position[0] + term[1] * position[1]) -
				water[omegaBase + component / 4][component & 3] * time + component * 1.37f;
			height += term[2] * shallow * qualityAmplitude * sinf(phase);
		}
		return height;
	};
	const float blend = Com_Clamp(0.0f, 1.0f, water[WATER_U_WIND + 2][0]);
	return (evaluate(WATER_U_OLD_TERMS) +
		(evaluate(WATER_U_TARGET_TERMS) - evaluate(WATER_U_OLD_TERMS)) * blend) * shoreAttenuation;
}

qboolean R_WaterlineQuery(const vec3_t position, float time, waterlineQuery_t *query)
{
	static int cachedFrame = -1;
	static const world_t *cachedWorld = nullptr;
	static float cachedTime = 0.0f;
	static vec3_t cachedPosition = {};
	static waterlineQuery_t cachedQuery = {};
	if (cachedFrame == tr.frameCount && cachedWorld == tr.world && cachedTime == time &&
		VectorCompare(cachedPosition, position))
	{
		*query = cachedQuery;
		return query->valid;
	}
	const auto start = std::chrono::steady_clock::now();
	Com_Memset(query, 0, sizeof(*query));
	query->gameplayClass = R_LiquidPointClass(position);
	query->bodyId = 0;
	query->liquidClass = -1;
	if (!s_water.world || s_water.world != tr.world || !r_waterSurface->integer)
		return qfalse;
	const float stateTime = time;
	time = r_waterWaveTime->value >= 0.0f ? r_waterWaveTime->value : time;
	const waterBody_t *best = nullptr;
	float bestDistance = 1.0e30f;
	bool bestContainsCamera = false;
	for (const waterBody_t& body : s_water.bodies)
	{
		if (body.waterfall || body.model != 0 || body.planeNormal[2] < 0.7f ||
			position[0] < body.bounds[0][0] - 1.0f || position[0] > body.bounds[1][0] + 1.0f ||
			position[1] < body.bounds[0][1] - 1.0f || position[1] > body.bounds[1][1] + 1.0f ||
			!R_WaterBodyCoversXY(body, position))
			continue;
		const float base = (body.planeDist - body.planeNormal[0] * position[0] -
			body.planeNormal[1] * position[1]) / body.planeNormal[2];
		bool containsCamera = false;
		for (int brushIndex : body.brushes)
			if (brushIndex >= 0 && brushIndex < (int)s_water.brushes.size() &&
				R_WaterInsideBrush(s_water.brushes[brushIndex], position, 0.5f))
			{ containsCamera = true; break; }
		const float distance = fabsf(position[2] - base);
		if ((containsCamera && !bestContainsCamera) || (containsCamera == bestContainsCamera && distance < bestDistance))
		{
			bestDistance = distance;
			bestContainsCamera = containsCamera;
			best = &body;
		}
	}
	if (!best) return qfalse;

	query->valid = qtrue;
	query->bodyId = best->id;
	query->liquidClass = best->liquidClass;
	query->baseHeight = (best->planeDist - best->planeNormal[0] * position[0] -
		best->planeNormal[1] * position[1]) / best->planeNormal[2];
	float interactionAttenuation = 1.0f;
	query->shoreAttenuation = R_WaterlineShoreAttenuation(*best, position, &interactionAttenuation);
	vec4_t water[WATER_UNIFORM_VEC4S] = {};
	const waterDynamics_t& dynamics = s_dynamics[best->dynamics];
	const float legacyScale = best->legacy.deformAmplitude > 0.5f ? 0.2f : 1.0f;
	const float amplitude = dynamics.amplitude * 8.0f * legacyScale * best->waveMultiplier *
		Com_Clamp(0.0f, 4.0f, r_waterWaveAmplitude->value);
	const float wavelength = MAX(dynamics.wavelength * Com_Clamp(0.25f, 4.0f, r_waterWaveLength->value), 8.0f);
	R_WaterUpdateWindWaves(const_cast<waterBody_t&>(*best), amplitude, wavelength, stateTime,
		tr.refdef.precipitation, water);
	const float macro = R_WaterlineWaveHeight(*best, position, time, query->shoreAttenuation, water);
	const float interaction = (r_waterGeometry->integer && best->geometryVerts > 0 && r_waterInteraction->integer) ?
		(R_WaterInteractionSurfaceHeight(*best, position) - query->baseHeight) * interactionAttenuation : 0.0f;
	query->surfaceHeight = query->baseHeight + macro + interaction;
	query->relativeHeight = position[2] - query->surfaceHeight;
	query->queryUsec = std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - start).count();
	cachedFrame = tr.frameCount;
	cachedWorld = tr.world;
	cachedTime = stateTime;
	VectorCopy(position, cachedPosition);
	cachedQuery = *query;
	return qtrue;
}

static void R_WaterPackMotionSnapshot(waterMotionSnapshot_t& snapshot,
	const waterDrawUniforms_t& water, const vec4_t flowImpact, const shader_t *shader)
{
	Com_Memset(snapshot.slots, 0, sizeof(snapshot.slots));
	snapshot.slots[WATER_MOTION_FRAME][0] = water[WATER_U_FRAME][2];
	VectorCopy4(water[WATER_U_WAVE_CONTROL], snapshot.slots[WATER_MOTION_WAVE_CONTROL]);
	VectorSet4(snapshot.slots[WATER_MOTION_WAVE_QUALITY], water[WATER_U_WAVE_QUALITY][0],
		water[WATER_U_WAVE_QUALITY][1], water[WATER_U_BODY_FLOW_DEPTH][2], water[WATER_U_WAVE_DEBUG][2]);
	VectorCopy4(water[WATER_U_BODY_WAVES], snapshot.slots[WATER_MOTION_BODY_WAVES]);
	VectorCopy4(water[WATER_U_FLOW], snapshot.slots[WATER_MOTION_FLOW]);
	VectorSet4(snapshot.slots[WATER_MOTION_FLOW_DETAIL], water[WATER_U_FLOW + 1][0],
		water[WATER_U_FLOW + 1][1], water[WATER_U_WAVE_QUALITY][3], water[WATER_U_FLOW + 1][3]);
	VectorCopy4(water[WATER_U_BODY_MAPPING], snapshot.slots[WATER_MOTION_BODY_MAPPING]);
	VectorCopy4(water[WATER_U_BODY_MAPPING + 1], snapshot.slots[WATER_MOTION_INTERACTION]);
	VectorCopy4(water[WATER_U_BODY_MAPPING + 3], snapshot.slots[WATER_MOTION_INTERACTION_RECT]);
	VectorSet4(snapshot.slots[WATER_MOTION_SHORE], water[WATER_U_SHORELINE][0],
		water[WATER_U_SHORELINE][1], water[WATER_U_BODY_BOUNDS][3], water[WATER_U_GEOMETRY_FOAM][1]);
	for (int i = 0; i < 4; ++i)
		VectorCopy4(water[WATER_U_WATERFALL + i], snapshot.slots[WATER_MOTION_WATERFALL + i]);
	for (int i = 0; i < 8; ++i)
	{
		VectorCopy4(water[WATER_U_TARGET_TERMS + i], snapshot.slots[WATER_MOTION_TARGET_TERMS + i]);
		VectorCopy4(water[WATER_U_OLD_TERMS + i], snapshot.slots[WATER_MOTION_OLD_TERMS + i]);
	}
	VectorSet4(snapshot.slots[WATER_MOTION_WIND_GEOMETRY], water[WATER_U_WIND + 2][0],
		water[WATER_U_GEOMETRY][0], water[WATER_U_WATERFALL][0] > 0.5f ||
		(water[WATER_U_GEOMETRY_FOAM][2] > 2.5f && water[WATER_U_GEOMETRY_FOAM][2] < 3.5f) ? 1.0f : 0.0f,
		shader ? -shader->timeOffset : 0.0f);
	VectorCopy4(water[WATER_U_TARGET_OMEGA], snapshot.slots[WATER_MOTION_TARGET_OMEGA]);
	VectorCopy4(water[WATER_U_TARGET_OMEGA + 1], snapshot.slots[WATER_MOTION_TARGET_OMEGA + 1]);
	VectorCopy4(water[WATER_U_OLD_OMEGA], snapshot.slots[WATER_MOTION_OLD_OMEGA]);
	VectorCopy4(water[WATER_U_OLD_OMEGA + 1], snapshot.slots[WATER_MOTION_OLD_OMEGA + 1]);
	VectorCopy4(flowImpact, snapshot.slots[WATER_MOTION_FLOW_IMPACT]);
	snapshot.renderedFrame = backEndData->realFrameNumber;
	snapshot.generation = s_water.motionGeneration;
	snapshot.shader = shader;
	snapshot.valid = true;
}

static bool R_WaterPreviousModelValid(void)
{
	if (!backEnd.currentEntity || backEnd.currentEntity == &tr.worldEntity)
		return true;
	const int entityNum = (int)(backEnd.currentEntity - backEnd.refdef.entities);
	return entityNum >= 0 && entityNum < backEnd.refdef.num_entities &&
		tr.previousEntityUboOffsets[entityNum] != -1;
}

static bool R_WaterLegacyMotionValid(const shader_t *shader)
{
	if (!shader || !shader->numDeforms) return true;
	if (ShaderRequiresCPUDeforms(shader) || shader->clampTime != 0.0f) return false;
	const deform_t type = shader->deforms[0].deformation;
	return type == DEFORM_NONE || type == DEFORM_NORMALS || type == DEFORM_WAVE ||
		type == DEFORM_BULGE || type == DEFORM_BULGE_UNIFORM || type == DEFORM_MOVE;
}

void RB_WaterSurfaceSetupDraw( const shaderCommands_t *input, UniformDataWriter& uniforms,
	SamplerBindingsWriter& samplers )
{
	RB_WaterSurfaceCacheView();
	waterDrawUniforms_t water(s_water.viewWater);
	const uint32_t key = input->waterKey;
	const int bodyId = (int)(key >> 20);
	waterBody_t *body = bodyId > 0 && bodyId <= (int)s_water.bodies.size() ? &s_water.bodies[bodyId - 1] : nullptr;
	const waterDynamics_t& dynamics = s_dynamics[body ? body->dynamics : 0];
	const float legacyWaveScale = body && body->legacy.deformAmplitude > 0.5f ? 0.2f : 1.0f;
	const float bodyWaveMultiplier = body ? body->waveMultiplier : 1.0f;
	VectorSet4(water[WATER_U_BODY_WAVES], dynamics.amplitude * 8.0f * legacyWaveScale * bodyWaveMultiplier,
		dynamics.wavelength, dynamics.speed,
		Com_Clamp(0.0f, 2.0f, dynamics.microNormal * bodyWaveMultiplier));
	VectorSet4(water[WATER_U_BODY_BOUNDS], body ? body->bounds[0][0] : -1.0e6f,
		body ? body->bounds[0][1] : -1.0e6f,
		body ? body->bounds[1][0] : 1.0e6f,
		dynamics.shoreline);
	VectorSet4(water[WATER_U_BODY_FLOW_DEPTH], body ? body->flowDirection[0] : 0.0f, body ? body->flowDirection[1] : 0.0f,
		body ? body->depthAverage : 0.0f, body ? (float)body->dynamics : 0.0f);
	VectorSet4(water[WATER_U_WAVE_DEBUG], (float)r_waterWaveDebug->integer, (float)bodyId,
		dynamics.choppiness, body ? body->waveMultiplier : 1.0f);
	VectorSet4(water[WATER_U_GEOMETRY_FOAM], body ? body->legacy.deformAmplitude : 0.0f,
		body && body->geometryVerts > 0 && ((body->waterfall && r_waterfallGeometry->integer) || (!body->waterfall && r_waterGeometry->integer)) ? 1.0f : 0.0f,
		(float)r_waterGeometryDebug->integer, body ? body->foamMultiplier : 1.0f);
	const float flowScale = Com_Clamp(0.0f, 4.0f, r_waterFlowSpeed->value);
	VectorSet4(water[WATER_U_FLOW], body ? body->flowVelocity[0] * flowScale : 0.0f,
		body ? body->flowVelocity[1] * flowScale : 0.0f,
		body ? body->flowVelocity[2] * flowScale : 0.0f,
		r_waterFlow->integer && body && body->flowSpeed > 0.0f ? 1.0f : 0.0f);
	VectorSet4(water[WATER_U_FLOW + 1], body ? body->flowSpeed * flowScale : 0.0f,
		Com_Clamp(0.0f, 4.0f, r_waterFlowDetail->value), (float)r_waterFlowDebug->integer,
		body ? (float)(body->flowSource * 4 + body->flowConfidence) : 0.0f);
	const waterInteractionState_t *interaction = body && body->interaction.image ? &body->interaction : nullptr;
	const float extentX = body ? MAX(body->bounds[1][0] - body->bounds[0][0], 1.0f) : 1.0f;
	const float extentY = body ? MAX(body->bounds[1][1] - body->bounds[0][1], 1.0f) : 1.0f;
	VectorSet4(water[WATER_U_BODY_MAPPING], body ? body->bounds[0][0] : 0.0f, body ? body->bounds[0][1] : 0.0f,
		1.0f / extentX, 1.0f / extentY);
	VectorSet4(water[WATER_U_BODY_MAPPING + 1], interaction ? interaction->texelX : 1.0f, interaction ? interaction->texelY : 1.0f,
		interaction && r_waterInteraction->integer ? 1.0f : 0.0f, (float)r_waterInteractionDebug->integer);
	if (interaction) VectorCopy4(interaction->lastSource, water[WATER_U_BODY_MAPPING + 2]); else VectorClear4(water[WATER_U_BODY_MAPPING + 2]);
	if (interaction && s_water.interactionAtlas)
		VectorSet4(water[WATER_U_BODY_MAPPING + 3], (interaction->atlasX + 0.5f) / s_water.interactionAtlasWidth,
			(interaction->atlasY + 0.5f) / s_water.interactionAtlasHeight,
			(interaction->width - 1.0f) / s_water.interactionAtlasWidth,
			(interaction->height - 1.0f) / s_water.interactionAtlasHeight);
	else VectorSet4(water[WATER_U_BODY_MAPPING + 3], 0, 0, 1, 1);
	VectorSet4(water[WATER_U_SHORELINE], r_waterShoreline->integer ? 1.0f : 0.0f,
		MAX(r_waterShoreAttenuation->value, 1.0f), MAX(r_waterShoreContactSoftness->value, 0.25f),
		(float)r_waterShoreDebug->integer);
	VectorSet4(water[WATER_U_INTERSECTION_FOAM], r_waterIntersectionFoam->integer ? 1.0f : 0.0f,
		MAX(r_waterIntersectionFoamWidth->value, 0.5f),
		Com_Clamp(0.0f, 4.0f, r_waterIntersectionFoamStrength->value),
		(float)r_waterIntersectionFoamDebug->integer);
	const waterFoamState_t *foam = body && body->foam.active ? &body->foam : nullptr;
	VectorSet4(water[WATER_U_FOAM], foam && r_waterFoamField->integer ? 1.0f : 0.0f,
		(float)r_waterFoamDebug->integer, foam ? foam->texelX : 1.0f, foam ? foam->texelY : 1.0f);
	const bool waterfallImpact = foam && R_WaterfallImpactTargetsBody(body);
	const bool impactTurbulence = waterfallImpact && r_waterfallImpactTurbulence->value > 0.0f;
	const float whitewaterStrength = r_waterWhitewater->integer ? r_waterWhitewaterStrength->value : 0.0f;
	VectorSet4(water[WATER_U_WHITEWATER], foam && (r_waterWhitewater->integer || impactTurbulence) ? 1.0f : 0.0f,
		Com_Clamp(0.0f, 4.0f, impactTurbulence ? MAX(whitewaterStrength,
			r_waterfallImpactTurbulence->value) : whitewaterStrength),
		Com_Clamp(0.0f, 4.0f, r_waterWhitewaterFoam->value),
		(float)r_waterWhitewaterDebug->integer);
	const bool waterfall = body && body->waterfall && (key & WATERKEY_WATERFALL);
	const waterfallProfile_t& waterfallProfile = body ? body->waterfallValues : s_waterfallProfiles[WFPROFILE_MEDIUM];
	const float waterfallProfileStrength = !body ? 1.0f : Com_Clamp(0.0f, 4.0f,
		body->waterfallProfile == WFPROFILE_SMALL ? r_waterfallSmallStrength->value :
		(body->waterfallProfile == WFPROFILE_HEAVY ? r_waterfallHeavyStrength->value : r_waterfallMediumStrength->value));
	const float profileCoverage = body && body->waterfall ?
		s_water.waterfallProfileCoverage[body->waterfallProfile] : 0.0f;
	const float distanceLod = body ? 1.0f - Com_Clamp(0.0f, 1.0f, (body->viewDistance - 1200.0f) / 3600.0f) : 1.0f;
	const float coverageLod = Com_Clamp(0.0f, 1.0f, (profileCoverage - 0.008f) / 0.16f);
	const float requestedLod = r_waterfallQuality->integer <= 0 ? 0.32f :
		(r_waterfallQuality->integer >= 2 ? 1.0f : 0.68f);
	const float waterfallLod = MIN(requestedLod, MAX(distanceLod, coverageLod));
	VectorSet4(water[WATER_U_WATERFALL], waterfall ? 1.0f : 0.0f, (float)r_waterfallQuality->integer,
		(float)r_waterfallDebug->integer, waterfallProfile.thickness);
	VectorSet4(water[WATER_U_WATERFALL + 1], body ? body->fallDirection[0] : 0.0f, body ? body->fallDirection[1] : 0.0f,
		body ? body->fallDirection[2] : -1.0f, waterfallProfile.flowSpeed);
	VectorSet4(water[WATER_U_WATERFALL + 2], body ? body->planeNormal[0] : 0.0f, body ? body->planeNormal[1] : 0.0f,
		body ? body->planeNormal[2] : 1.0f, body ? 1.0f / MAX(body->fallMax - body->fallMin, 1.0f) : 1.0f);
	VectorSet4(water[WATER_U_WATERFALL + 3], body ? body->fallMin : 0.0f, waterfallProfile.breakup * waterfallProfileStrength,
		waterfallProfile.normalScale * waterfallProfileStrength,
		waterfallProfile.aeration * waterfallProfileStrength * Com_Clamp(0.0f, 4.0f, r_waterfallWhitewater->value));
	VectorSet4(water[WATER_U_WATERFALL + 4], waterfallProfile.opacity, waterfallProfile.spray * waterfallProfileStrength,
		waterfallProfile.impact * waterfallProfileStrength,
		body ? (float)body->waterfallProfile : 0.0f);
	// Per-profile coverage is shared by disconnected stock strips (the dam has
	// eight real BSP gaps), so they retain one coherent quality decision without
	// merging geometry or adding transparent passes.  z gates refraction/SSR;
	// w simplifies high-frequency foam/spray breakup at distance.
	VectorSet4(water[WATER_U_WATERFALL + 5], waterfallProfileStrength, waterfallLod,
		waterfallLod < 0.34f ? 0.0f : (waterfallLod < 0.67f ? 0.5f : 1.0f),
		waterfallLod < 0.34f ? 0.35f : (waterfallLod < 0.67f ? 0.7f : 1.0f));
	if (waterfall)
	{
		// Falling-sheet motion is intrinsic to the dedicated waterfall path.  It
		// must not depend on the optional horizontal-water flow visualization.
		VectorSet4(water[WATER_U_FLOW], body->fallDirection[0] * waterfallProfile.flowSpeed,
			body->fallDirection[1] * waterfallProfile.flowSpeed,
			body->fallDirection[2] * waterfallProfile.flowSpeed, 1.0f);
		water[WATER_U_FLOW + 1][0] = waterfallProfile.flowSpeed;
		water[WATER_U_SURFACE][1] = MAX(water[WATER_U_SURFACE][1], 0.34f);
		water[WATER_U_SURFACE][3] = Com_Clamp(0.0f, 4.0f, r_waterfallRefraction->value);
		water[WATER_U_OPTICS][0] *= 0.28f;
		water[WATER_U_OPTICS][1] *= 0.35f * water[WATER_U_WATERFALL + 5][2];
	}
	// The analytic spectrum is shared by the vertex displacement and fragment
	// normal.  Direction, amplitude and wave number are constant for this body
	// and draw; computing them here avoids normalization, trigonometry and
	// divisions for every component of every covered fragment.
	const float wavelength = MAX(water[WATER_U_BODY_WAVES][1] * water[WATER_U_WAVE_CONTROL][2], 8.0f);
	const float amplitude = water[WATER_U_BODY_WAVES][0] * water[WATER_U_WAVE_CONTROL][1];
	if (body)
		R_WaterUpdateWindWaves(*body, amplitude, wavelength, backEnd.refdef.floatTime,
			backEnd.refdef.precipitation, water.Data());
	else
	{
		waterBody_t fallback = {};
		fallback.waveMultiplier = 1.0f;
		fallback.dynamics = 0;
		waterWaveSet_t set;
		vec2_t direction = { 0.8f, 0.6f };
		R_WaterBuildWindWaveSet(fallback, dynamics, amplitude, wavelength, 0.0f, 0.0f, direction, set);
		for (int i = 0; i < 8; ++i)
		{
			VectorCopy4(set.terms[i], water[WATER_U_TARGET_TERMS + i]);
			VectorCopy4(set.terms[i], water[WATER_U_OLD_TERMS + i]);
			water[WATER_U_TARGET_OMEGA + i / 4][i & 3] = set.omega[i];
			water[WATER_U_OLD_OMEGA + i / 4][i & 3] = set.omega[i];
		}
		VectorSet4(water[WATER_U_WIND + 1], set.direction[0], set.direction[1], set.direction[0], set.direction[1]);
		water[WATER_U_WIND + 2][0] = 1.0f;
	}
	const int geometryComponents = body ? R_WaterGeometryComponentCount(*body,
		water.Data() + WATER_U_TARGET_TERMS) : 0;
	VectorSet4(water[WATER_U_GEOMETRY], (float)geometryComponents, body ? body->geometryMaxEdge : 0.0f,
		body ? body->geometryMaxEdge * 4.0f : 0.0f, 0.0f);
	const int liquidClass = Com_Clampi(LIQUID_WATER, LIQUID_SLIME, (key >> 4) & 3);
	const float *extinction = s_water.extinction[liquidClass];
	const bool froxel = (s_water.viewFlags & 4) != 0;
	const bool froxelLiquid = froxel && backEnd.currentEntity == &tr.worldEntity &&
		(key & WATERKEY_WORLD_BRUSH) && (R_LiquidClassMask() & (key >> 16)) != 0;
	VectorSet4(water[WATER_U_EXTINCTION], extinction[0] * extinction[3], extinction[1] * extinction[3], extinction[2] * extinction[3],
		froxelLiquid ? 1.0f : 0.0f);
	VectorCopy4(s_water.albedo[liquidClass], water[WATER_U_SCATTER]);
	int flags = s_water.viewFlags;
	int cubemapIndex = input->cubemapIndex;
	if (cubemapIndex <= 0 || cubemapIndex > tr.numCubemaps || !tr.cubemaps[cubemapIndex - 1].image)
		cubemapIndex = s_water.fallbackCubemap;
	const bool cubemap = r_cubeMapping->integer && !(backEnd.viewParms.flags & VPF_NOCUBEMAPS) &&
		cubemapIndex > 0 && cubemapIndex <= tr.numCubemaps && tr.cubemaps[cubemapIndex - 1].image;
	if (cubemap) flags |= 1;
	if ((key >> 8) & WATERSURF_FOG_MEDIUM) flags |= 32;
	if (liquidClass == LIQUID_SLIME) flags |= 64;
	if ((key >> 8) & WATERSURF_OVERRIDE) flags |= 128;
	if ((key >> 8) & WATERSURF_EXPERIMENTAL) flags |= 256;
	water[WATER_U_DEBUG][2] = (float)flags;
	image_t *waterRainImage = R_WeatherWaterRippleImage(qfalse);
	const precipitationState_t& precipitation = backEnd.refdef.precipitation;
	const bool waterRain = waterRainImage && body && !body->waterfall &&
		r_waterRainRipples->integer;
	const vec4_t waterRainParams = {
		waterRain ? Com_Clamp(0.0f, 4.0f, r_waterRainRippleImpulse->value) : 0.0f,
		waterRain ? Com_Clamp(0.0f, 4.0f, r_waterRainRippleMicro->value) *
			Com_Clamp(0.0f, 2.0f, precipitation.normalizedIntensity) : 0.0f,
		Com_Clamp(0.25f, 4.0f, r_weatherPuddleRippleFieldScale->value),
		(float)Com_Clampi(0, 4, r_waterRainRippleDebug->integer)
	};
	uniforms.SetUniformVec4(UNIFORM_WATERRAINPARAMS, waterRainParams);
	const qboolean atmosphereOptics = RB_AtmosphereWaterSetup(uniforms, samplers);
	const vec4_t optics2 = { r_waterSnell->integer ? (float)r_waterSnellOptics->integer : 0.0f,
		atmosphereOptics ? 1.0f : 0.0f, 0.0f, 0.0f };
	uniforms.SetUniformVec4(UNIFORM_WATEROPTICS2, optics2);
	if (tr.weatherSystem)
		uniforms.SetUniformMatrix4x4(UNIFORM_WEATHERMVP, tr.weatherSystem->weatherMVP);
	uniforms.SetUniformVec4(UNIFORM_WATER, water[WATER_U_SURFACE], WATER_UNIFORM_VEC4S);
	samplers.AddStaticImage(tr.waterSceneImage, 0);
	samplers.AddStaticImage(tr.waterDepthImage, 1);
	samplers.AddStaticImage(tr.waterNormalImage, 2);
	samplers.AddStaticImage(tr.waterFoamPatternImage ? tr.waterFoamPatternImage : tr.whiteImage, 16);
	const bool flowField = body && !body->waterfall && r_waterFlow->integer && body->flowField.valid;
	samplers.AddStaticImage(flowField ? body->flowField.image : tr.whiteImage, 17);
	vec4_t flowParams[2];
	{
		// [0] field enabled, body flow speed (u/s), wall impact enabled, stagnation head (world units)
		// [1] two-phase period (s), foam pattern world scale, flow debug, unused
		const float speed = flowField ? body->flowSpeed * Com_Clamp(0.0f, 4.0f, r_waterFlowSpeed->value) : 0.0f;
		const bool impact = flowField && r_waterFlowImpact->integer;
		VectorSet4(flowParams[0], flowField ? 1.0f : 0.0f, speed, impact ? 1.0f : 0.0f,
			impact ? R_WaterFlowImpactHead(*body) : 0.0f);
		VectorSet4(flowParams[1], Com_Clamp(0.6f, 2.0f, 64.0f / MAX(speed, 1.0f)), 1.0f / 96.0f,
			(float)r_waterFlowDebug->integer, 0.0f);
		uniforms.SetUniformVec4(UNIFORM_WATERFLOWPARAMS, flowParams[0], 2);
	}
	samplers.AddStaticImage(interaction ? s_water.interactionAtlas : tr.whiteImage, 19);
	if (r_waterSSRWaveReprojection && r_waterSSRWaveReprojection->integer)
	{
		if (bodyId >= (int)s_water.pendingMotionSnapshots.size())
		{
			s_water.pendingMotionSnapshots.resize(bodyId + 1);
			s_water.motionSnapshots.resize(bodyId + 1);
		}
		if (bodyId > 0)
		{
			const auto snapshotStart = std::chrono::high_resolution_clock::now();
			R_WaterPackMotionSnapshot(s_water.pendingMotionSnapshots[bodyId], water, flowParams[0], input->shader);
			s_water.pendingMotionSnapshots[bodyId].legacySupported = R_WaterLegacyMotionValid(input->shader);
			s_water.pendingMotionSnapshots[bodyId].modelTransformSupported = R_WaterPreviousModelValid();
			s_water.pendingMotionSnapshots[bodyId].interactionSupported = !interaction ||
				!backEnd.currentEntity || backEnd.currentEntity == &tr.worldEntity;
			s_water.motionSnapshotUsec += std::chrono::duration<double, std::micro>(
				std::chrono::high_resolution_clock::now() - snapshotStart).count();
			s_water.motionSnapshotCount++;
		}

		waterMotionSnapshot_t previous = {};
		const bool bodyValid = bodyId > 0 && bodyId < (int)s_water.motionSnapshots.size() &&
			s_water.motionSnapshots[bodyId].valid && s_water.history.valid &&
			s_water.motionSnapshots[bodyId].renderedFrame == s_water.history.frameNumber &&
			s_water.motionSnapshots[bodyId].generation == s_water.motionGeneration &&
			s_water.motionSnapshots[bodyId].shader == input->shader;
		const bool screenValid = RB_ScreenHistoryValid(s_water.history, s_water.reflectionScale) != qfalse;
		const bool modelValid = R_WaterPreviousModelValid();
		const bool legacyValid = R_WaterLegacyMotionValid(input->shader);
		bool interactionValid = true;
		const bool movingInteraction = interaction && backEnd.currentEntity &&
			backEnd.currentEntity != &tr.worldEntity;
		if (interaction)
			interactionValid = s_water.previousInteractionAtlas &&
				s_water.interactionHistoryFrame == s_water.history.frameNumber &&
				!movingInteraction;
		const bool drawValid = bodyValid && screenValid && modelValid && legacyValid && interactionValid;
		if (!drawValid)
			s_water.motionInvalidation = !bodyValid ? WM_INVALID_BODY_IDENTITY :
				(!screenValid ? WM_INVALID_SCREEN_HISTORY : (!modelValid ? WM_INVALID_MODEL_IDENTITY :
				(!legacyValid ? WM_INVALID_LEGACY_DEFORM : (movingInteraction ?
				WM_INVALID_MOVING_INTERACTION : WM_INVALID_INTERACTION_REMAP))));
		if (bodyValid) previous = s_water.motionSnapshots[bodyId];
		previous.slots[WATER_MOTION_FRAME][1] = drawValid ? 1.0f : 0.0f;
		previous.slots[WATER_MOTION_FRAME][2] = interactionValid ? 1.0f : 0.0f;
		previous.slots[WATER_MOTION_FRAME][3] = (float)r_waterMotionDebug->integer;
		previous.slots[WATER_MOTION_INTERACTION][3] = (float)s_water.motionInvalidation;
		uniforms.SetUniformVec4(UNIFORM_WATERMOTION, previous.slots[0], WATER_MOTION_VEC4S);
		samplers.AddStaticImage(interaction && s_water.previousInteractionAtlas ?
			s_water.previousInteractionAtlas : tr.whiteImage, 24);
	}
	samplers.AddStaticImage(waterRain ? waterRainImage : tr.whiteImage, TB_WATERRAIN);
	samplers.AddStaticImage(foam ? ((r_waterFoamDebug->integer == 3 || r_waterWhitewaterDebug->integer == 4) ? foam->sourceImage : foam->image[foam->current]) : tr.whiteImage,
		TB_WATERFOAM);
	samplers.AddStaticImage(tr.waterGlowImage, 9);
	if (tr.envBrdfImage) samplers.AddStaticImage(tr.envBrdfImage, 3);
	if (cubemap)
	{
		const cubemap_t *cm = &tr.cubemaps[cubemapIndex - 1];
		samplers.AddStaticImage(cm->image, 4);
		vec4_t info;
		VectorSubtract(cm->origin, backEnd.viewParms.ori.origin, info);
		info[3] = 1.0f;
		VectorScale4(info, 1.0f / MAX(cm->parallaxRadius, 1.0f), info);
		uniforms.SetUniformVec4(UNIFORM_CUBEMAPINFO, info);
	}
	if (flags & 2) samplers.AddStaticImage(tr.sunShadowArrayImage, TB_SHADOWMAP);
	if (backEnd.waterSurfaceSSR)
	{
		uniforms.SetUniformVec4(UNIFORM_SSRPROJECTION, s_water.viewInfo.projection);
		uniforms.SetUniformVec4(UNIFORM_SSRDEPTHPARAMS, s_water.viewInfo.depthParams);
		uniforms.SetUniformVec4(UNIFORM_SSRVIEWPORT, s_water.viewInfo.viewport);
		vec4_t texelSize;
		RB_ScreenTexelSize(texelSize, tr.renderFbo->width, tr.renderFbo->height, tr.renderFbo->width, tr.renderFbo->height);
		uniforms.SetUniformVec4(UNIFORM_SSRTEXELSIZE, texelSize);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS, s_water.ssrSettings[0]);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS2, s_water.ssrSettings[1]);
		uniforms.SetUniformVec4(UNIFORM_SSRSETTINGS3, s_water.ssrSettings[2]);
		samplers.AddStaticImage(tr.screenHiZImage, 10);
		samplers.AddStaticImage(tr.ssrColorImage, 11);
	}
	if (froxel) RB_VolumetricSetupFogDraw(1, uniforms, samplers);
}

/*
=================
RB_WaterSurfacePrepare / RB_WaterSurfaceFinish

Around the water slot of the main pass (RB_SubmitRenderPass), renderFbo bound:
the scene under the water and its depth are copied (MSAA: resolved) once.
=================
*/

static float RB_WaterIntersectionLinearDepth(float depth, const viewParms_t& view)
{
	return view.projectionMatrix[14] /
		(depth * 2.0f - 1.0f + view.projectionMatrix[10]);
}

static void RB_WaterIntersectionWorldPosition(int x, int y, float depth,
	const viewParms_t& view, int targetWidth, int targetHeight, vec3_t out)
{
	const float u = (x + 0.5f) / targetWidth;
	const float v = (y + 0.5f) / targetHeight;
	const float vx = (float)view.viewportX / targetWidth;
	const float vy = (float)view.viewportY / targetHeight;
	const float vw = (float)view.viewportWidth / targetWidth;
	const float vh = (float)view.viewportHeight / targetHeight;
	const float ndcX = (u - vx) / vw * 2.0f - 1.0f;
	const float ndcY = (v - vy) / vh * 2.0f - 1.0f;
	const float z = RB_WaterIntersectionLinearDepth(depth, view);
	const float side = (ndcX + view.projectionMatrix[8]) * z / view.projectionMatrix[0];
	const float up = (ndcY + view.projectionMatrix[9]) * z / view.projectionMatrix[5];
	vec3_t right;
	VectorScale(view.ori.axis[1], -1.0f, right);
	VectorCopy(view.ori.origin, out);
	VectorMA(out, side, right, out);
	VectorMA(out, up, view.ori.axis[2], out);
	VectorMA(out, z, view.ori.axis[0], out);
}

// Project the body-local simulation cells through the same opaque depth copy
// sampled by the water shader.  Only a narrow, locally coherent, near-vertical
// contact survives; floors just under shallow water and arbitrary silhouettes
// therefore do not turn into white outlines.  The one-frame delayed result is
// a source for the persistent field, while the shader supplies the current
// frame's immediate response.
static void RB_WaterIntersectionInject(void)
{
	if (!r_waterIntersectionFoam->integer || !r_waterInteraction->integer ||
		r_waterIntersectionFoamStrength->value <= 0.0f ||
		r_waterIntersectionFoamPersistence->value <= 0.0f || !s_water.interactionAtlas)
		return;
	const viewParms_t& view = backEnd.viewParms;
	const int x0 = s_water.copyRect[0], y0 = s_water.copyRect[1];
	const int width = s_water.copyRect[2], height = s_water.copyRect[3];
	if (width < 3 || height < 3) return;
	static std::vector<float> depth;
	depth.resize((size_t)width * height);
	qglReadPixels(x0, y0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
	const float band = MAX(r_waterIntersectionFoamWidth->value, 0.5f);
	const float strength = Com_Clamp(0.0f, 4.0f, r_waterIntersectionFoamStrength->value);
	matrix_t viewProjection;
	Matrix16Multiply(view.projectionMatrix, view.world.modelViewMatrix, viewProjection);
	auto sampleDepth = [&](int x, int y) -> float {
		x = Com_Clampi(x0, x0 + width - 1, x);
		y = Com_Clampi(y0, y0 + height - 1, y);
		return depth[(size_t)(y - y0) * width + x - x0];
	};
	for (waterBody_t& body : s_water.bodies)
	{
		waterInteractionState_t& sim = body.interaction;
		if (!sim.image || fabsf(body.planeNormal[2]) < 0.35f) continue;
		for (int gy = 0; gy < sim.height; ++gy) for (int gx = 0; gx < sim.width; ++gx)
		{
			const size_t i = (size_t)gy * sim.width + gx;
			if (!sim.mask[i]) continue;
			vec3_t waterPoint = {
				body.bounds[0][0] + (gx + 0.5f) * sim.texelX,
				body.bounds[0][1] + (gy + 0.5f) * sim.texelY, 0.0f };
			waterPoint[2] = (body.planeDist - body.planeNormal[0] * waterPoint[0] -
				body.planeNormal[1] * waterPoint[1]) / body.planeNormal[2];
			vec4_t clip;
			for (int row = 0; row < 4; ++row)
				clip[row] = viewProjection[row] * waterPoint[0] + viewProjection[4 + row] * waterPoint[1] +
					viewProjection[8 + row] * waterPoint[2] + viewProjection[12 + row];
			if (clip[3] <= 0.001f) continue;
			const float ndcX = clip[0] / clip[3], ndcY = clip[1] / clip[3];
			const int sx = (int)floorf(view.viewportX + (ndcX * 0.5f + 0.5f) * view.viewportWidth);
			const int sy = (int)floorf(view.viewportY + (ndcY * 0.5f + 0.5f) * view.viewportHeight);
			if (sx <= x0 || sy <= y0 || sx >= x0 + width - 1 || sy >= y0 + height - 1) continue;
			const float dc = sampleDepth(sx, sy);
			if (dc >= 0.999999f) continue;
			vec3_t scene, px0, px1, py0, py1, dx, dy, sceneNormal;
			RB_WaterIntersectionWorldPosition(sx, sy, dc, view, tr.waterDepthImage->width, tr.waterDepthImage->height, scene);
			const float signedDistance = DotProduct(scene, body.planeNormal) - body.planeDist;
			if (fabsf(signedDistance) >= band) continue;
			const float dl = sampleDepth(sx - 1, sy), dr = sampleDepth(sx + 1, sy);
			const float dd = sampleDepth(sx, sy - 1), du = sampleDepth(sx, sy + 1);
			if (dl >= 0.999999f || dr >= 0.999999f || dd >= 0.999999f || du >= 0.999999f) continue;
			RB_WaterIntersectionWorldPosition(sx - 1, sy, dl, view, tr.waterDepthImage->width, tr.waterDepthImage->height, px0);
			RB_WaterIntersectionWorldPosition(sx + 1, sy, dr, view, tr.waterDepthImage->width, tr.waterDepthImage->height, px1);
			RB_WaterIntersectionWorldPosition(sx, sy - 1, dd, view, tr.waterDepthImage->width, tr.waterDepthImage->height, py0);
			RB_WaterIntersectionWorldPosition(sx, sy + 1, du, view, tr.waterDepthImage->width, tr.waterDepthImage->height, py1);
			VectorSubtract(px1, px0, dx); VectorSubtract(py1, py0, dy);
			CrossProduct(dx, dy, sceneNormal);
			const float normalLength = VectorNormalize(sceneNormal);
			if (normalLength < 1.0e-4f) continue;
			const float vertical = 1.0f - fabsf(DotProduct(sceneNormal, body.planeNormal));
			const float normalConfidence = Com_Clamp(0.0f, 1.0f, (vertical - 0.15f) / 0.55f);
			const float derivative = MAX(VectorLength(dx), VectorLength(dy));
			const float depthConfidence = 1.0f - Com_Clamp(0.0f, 1.0f, derivative / (band * 8.0f));
			const float bandConfidence = 1.0f - Com_Clamp(0.0f, 1.0f, fabsf(signedDistance) / band);
			const float source = bandConfidence * normalConfidence * depthConfidence * strength;
			if (source <= 0.01f) continue;
			const float clampedSource = Com_Clamp(0.0f, 1.0f, source);
			sim.intersectionFoam[i] = MAX(sim.intersectionFoam[i], clampedSource);
			sim.intersectionPending = true;
			if (R_WaterInteractionGpuEnabled() && sim.gpuState[0])
			{
				if (!sim.gpuSourceDirty)
				{
					sim.gpuSourceMinX = sim.gpuSourceMaxX = gx;
					sim.gpuSourceMinY = sim.gpuSourceMaxY = gy;
				}
				else
				{
					sim.gpuSourceMinX = MIN(sim.gpuSourceMinX, gx);
					sim.gpuSourceMinY = MIN(sim.gpuSourceMinY, gy);
					sim.gpuSourceMaxX = MAX(sim.gpuSourceMaxX, gx);
					sim.gpuSourceMaxY = MAX(sim.gpuSourceMaxY, gy);
				}
				sim.gpuSourceData[i * 4 + 3] = MAX(sim.gpuSourceData[i * 4 + 3], clampedSource);
				sim.gpuSourceDirty = true;
			}
			sim.active = sim.dirty = true;
			sim.quietTime = 0.0f;
		}
	}
}

/*
=================
RB_WaterIntersectionCompute

GL 4.3 replacement of RB_WaterIntersectionInject: the same contact test per
simulation cell in a compute kernel that samples the depth copy directly, so
the frame never waits for the opaque pass to reach system memory. The source
stays on the GPU (the step and foam programs sample sim.gpuIntersection);
only the per-body maximum comes back, through a fenced buffer that is read
when the GPU is done with it, to wake sleeping bodies a frame or two later.
=================
*/
static void RB_WaterIntersectionReleaseHits(void)
{
	for (int i = 0; i < WATER_INTERSECTION_HIT_RING; ++i)
	{
		if (s_water.intersectionHitSync[i])
			qglDeleteSync(s_water.intersectionHitSync[i]);
		s_water.intersectionHitSync[i] = nullptr;
	}
	if (s_water.intersectionHitBuffers[0])
		qglDeleteBuffers(WATER_INTERSECTION_HIT_RING, s_water.intersectionHitBuffers);
	Com_Memset(s_water.intersectionHitBuffers, 0, sizeof(s_water.intersectionHitBuffers));
	s_water.intersectionHitNext = 0;
}

static void RB_WaterIntersectionPollHits(void)
{
	for (int i = 0; i < WATER_INTERSECTION_HIT_RING; ++i)
	{
		GLsync sync = s_water.intersectionHitSync[i];
		if (!sync)
			continue;
		const GLenum status = qglClientWaitSync(sync, 0, 0);
		if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
			continue;
		qglDeleteSync(sync);
		s_water.intersectionHitSync[i] = nullptr;
		if (s_water.intersectionHitGeneration[i] != s_water.intersectionGeneration)
			continue;
		uint32_t hits[WATER_INTERSECTION_MAX_SLOTS];
		qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_water.intersectionHitBuffers[i]);
		qglGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(hits), hits);
		qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		for (waterBody_t& body : s_water.bodies)
		{
			waterInteractionState_t& sim = body.interaction;
			if (sim.intersectionSlot < 0)
				continue;
			float source;
			memcpy(&source, &hits[sim.intersectionSlot], sizeof(source));
			// what the CPU path does for every contact cell
			if (source > 0.01f)
			{
				sim.active = sim.dirty = true;
				sim.quietTime = 0.0f;
				s_water.intersectionWakes++;
			}
		}
	}
}

static void RB_WaterIntersectionCompute(void)
{
	s_water.intersectionDispatches = 0;
	if (!r_waterIntersectionFoam->integer || !r_waterInteraction->integer ||
		r_waterIntersectionFoamStrength->value <= 0.0f ||
		r_waterIntersectionFoamPersistence->value <= 0.0f || !s_water.interactionAtlas)
		return;
	const viewParms_t& view = backEnd.viewParms;
	const int width = s_water.copyRect[2], height = s_water.copyRect[3];
	if (width < 3 || height < 3)
		return;

	if (!s_water.intersectionHitBuffers[0])
	{
		qglGenBuffers(WATER_INTERSECTION_HIT_RING, s_water.intersectionHitBuffers);
		for (int i = 0; i < WATER_INTERSECTION_HIT_RING; ++i)
		{
			qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_water.intersectionHitBuffers[i]);
			qglBufferData(GL_SHADER_STORAGE_BUFFER, WATER_INTERSECTION_MAX_SLOTS * sizeof(uint32_t),
				NULL, GL_DYNAMIC_READ);
		}
	}
	// an entry the GPU has not finished after the whole ring is dropped, never waited for
	const int entry = s_water.intersectionHitNext;
	if (s_water.intersectionHitSync[entry])
	{
		qglDeleteSync(s_water.intersectionHitSync[entry]);
		s_water.intersectionHitSync[entry] = nullptr;
	}
	static const uint32_t zero[WATER_INTERSECTION_MAX_SLOTS] = {};
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, s_water.intersectionHitBuffers[entry]);
	qglBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), zero);
	qglBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_water.intersectionHitBuffers[entry]);

	shaderProgram_t *sp = &tr.waterIntersectionComputeShader;
	GLSL_BindProgram(sp);
	matrix_t viewProjection;
	Matrix16Multiply(view.projectionMatrix, view.world.modelViewMatrix, viewProjection);
	GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, viewProjection);
	GL_BindToTMU(tr.waterDepthImage, TB_LIGHTMAP);
	const float band = MAX(r_waterIntersectionFoamWidth->value, 0.5f);
	const float strength = Com_Clamp(0.0f, 4.0f, r_waterIntersectionFoamStrength->value);
	const float *p = view.projectionMatrix;
	// the previous frame's step and foam passes sampled these images
	qglMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	int dispatched = 0;
	for (waterBody_t& body : s_water.bodies)
	{
		waterInteractionState_t& sim = body.interaction;
		if (!sim.gpuIntersection || sim.intersectionSlot < 0 || !sim.gpuState[0] ||
			fabsf(body.planeNormal[2]) < 0.35f)
			continue;
		vec4_t params[11];
		VectorSet4(params[0], body.bounds[0][0], body.bounds[0][1], sim.texelX, sim.texelY);
		VectorSet4(params[1], body.planeNormal[0], body.planeNormal[1], body.planeNormal[2], body.planeDist);
		VectorSet4(params[2], (float)s_water.copyRect[0], (float)s_water.copyRect[1], (float)width, (float)height);
		VectorSet4(params[3], (float)view.viewportX, (float)view.viewportY,
			(float)view.viewportWidth, (float)view.viewportHeight);
		VectorSet4(params[4], (float)tr.waterDepthImage->width, (float)tr.waterDepthImage->height, band, strength);
		VectorSet4(params[5], p[0], p[5], p[8], p[9]);
		VectorSet4(params[6], p[10], p[14], (float)sim.intersectionSlot, 0.0f);
		VectorSet4(params[7], view.ori.origin[0], view.ori.origin[1], view.ori.origin[2], 0.0f);
		VectorSet4(params[8], view.ori.axis[0][0], view.ori.axis[0][1], view.ori.axis[0][2], 0.0f);
		VectorSet4(params[9], view.ori.axis[1][0], view.ori.axis[1][1], view.ori.axis[1][2], 0.0f);
		VectorSet4(params[10], view.ori.axis[2][0], view.ori.axis[2][1], view.ori.axis[2][2], 0.0f);
		GLSL_SetUniformVec4N(sp, UNIFORM_WATERINTERSECTION, params[0], 11);
		GL_BindToTMU(sim.gpuState[sim.gpuCurrent], TB_COLORMAP);
		qglBindImageTexture(0, sim.gpuIntersection->texnum, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
		qglDispatchCompute((sim.width + 7) / 8, (sim.height + 7) / 8, 1);
		sim.intersectionFresh = sim.intersectionFoamFresh = true;
		++dispatched;
	}
	qglBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
	qglBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	GLSL_BindNullProgram();
	if (!dispatched)
		return;
	// sampled by the next step / foam update, read back by RB_WaterIntersectionPollHits
	qglMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
	s_water.intersectionHitSync[entry] = qglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	s_water.intersectionHitGeneration[entry] = s_water.intersectionGeneration;
	s_water.intersectionHitNext = (entry + 1) % WATER_INTERSECTION_HIT_RING;
	s_water.intersectionDispatches = dispatched;
}

void RB_WaterSurfacePrepare( void )
{
	FBO_t *oldFbo = glState.currentFBO;
	const viewParms_t& viewParms = backEnd.viewParms;

	R_PushDebugGroup(AL_STAGE, "Water surface");
	s_water.timer = RB_ScreenBeginTimer("Water surface");

	// Blits honor the scissor.  Only the submitted main-view rectangle can be
	// sampled by this water pass; avoid copying unused render-target pixels
	// (notably split-screen / oversized render targets) without changing any
	// sample visible to the shader.
	GL_SetViewportAndScissor(s_water.copyRect[0], s_water.copyRect[1],
		s_water.copyRect[2], s_water.copyRect[3]);
	FBO_FastBlitIndexed(tr.renderFbo, tr.waterCopyFbo, 0, 0, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	FBO_FastBlitIndexed(tr.renderFbo, tr.waterCopyFbo, 1, 1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	if (R_WaterIntersectionComputeEnabled())
		RB_WaterIntersectionCompute();
	else
	{
		FBO_Bind(tr.waterCopyFbo);
		RB_WaterIntersectionInject();
	}

	FBO_Bind(oldFbo);
	GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
		viewParms.viewportWidth, viewParms.viewportHeight);
}

qboolean RB_WaterSurfaceReflectionBegin( void )
{
	if (!backEnd.waterSurfaceSSR || !tr.waterReflectionFbo[0])
		return qfalse;
	s_water.reflectionHistoryValid = (qboolean)(r_ssrTemporal->integer &&
		RB_ScreenHistoryValid(s_water.history, s_water.reflectionScale));
	s_water.reflectionCurrent = s_water.history.valid ? 1 - s_water.history.current : 0;
	FBO_t *fbo = tr.waterReflectionFbo[s_water.reflectionCurrent];
	FBO_Bind(fbo);
	GL_SetViewportAndScissor(0, 0, fbo->width, fbo->height);
	GL_State(GLS_DEPTHMASK_TRUE);
	GL_SetScreenAuxWrite(true);
	qglClearBufferfv(GL_COLOR, 0, colorBlack);
	qglClearBufferfv(GL_COLOR, 1, colorBlack);
	qglClearBufferfv(GL_COLOR, 2, colorBlack);
	qglClear(GL_DEPTH_BUFFER_BIT);
	const viewParms_t& v = backEnd.viewParms;
	const int x = (int)floorf(v.viewportX / s_water.reflectionScale);
	const int y = (int)floorf(v.viewportY / s_water.reflectionScale);
	const int x1 = (int)ceilf((v.viewportX + v.viewportWidth) / s_water.reflectionScale);
	const int y1 = (int)ceilf((v.viewportY + v.viewportHeight) / s_water.reflectionScale);
	GL_SetViewportAndScissor(x, y, x1 - x, y1 - y);
	return qtrue;
}

static void RB_WaterMotionCommit(void)
{
	if (!r_waterSSRWaveReprojection || !r_waterSSRWaveReprojection->integer ||
		s_water.motionCommitFrame == backEndData->realFrameNumber ||
		!RB_WaterPrimaryFrame())
		return;

	// Draw uniforms were captured while building this primary view.  Promote
	// only bodies actually rendered into the reflection history.
	if (s_water.motionSnapshots.size() < s_water.pendingMotionSnapshots.size())
		s_water.motionSnapshots.resize(s_water.pendingMotionSnapshots.size());
	for (size_t i = 1; i < s_water.pendingMotionSnapshots.size(); ++i)
	{
		waterMotionSnapshot_t& pending = s_water.pendingMotionSnapshots[i];
		if (pending.valid && pending.renderedFrame == backEndData->realFrameNumber)
		{
			assert(pending.generation == s_water.motionGeneration);
			pending.generation = s_water.motionGeneration;
			s_water.motionSnapshots[i] = pending;
		}
		pending.valid = false;
	}

	// The other solver ping-pong image is a previous fixed step, not a rendered
	// frame.  Copy only resolved body rectangles after the reflection pass has
	// finished reading the previous-rendered atlas.
	if (s_water.previousInteractionAtlas && s_water.interactionAtlas && tr.waterInteractionFbo)
	{
		const auto copyStart = std::chrono::high_resolution_clock::now();
		FBO_t *oldFbo = glState.currentFBO;
		FBO_Bind(tr.waterInteractionFbo);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			s_water.interactionAtlas->texnum, 0);
		qglReadBuffer(GL_COLOR_ATTACHMENT0);
		GL_BindToTMU(s_water.previousInteractionAtlas, 24);
		for (waterBody_t& body : s_water.bodies)
		{
			waterInteractionState_t& sim = body.interaction;
			if (!sim.image || !sim.resolvedForMotionHistory) continue;
			qglCopyTexSubImage2D(GL_TEXTURE_2D, 0, sim.atlasX, sim.atlasY,
				sim.atlasX, sim.atlasY, sim.width, sim.height);
			sim.resolvedForMotionHistory = false;
			sim.lastMotionCopyFrame = backEndData->realFrameNumber;
		}
		FBO_Bind(oldFbo);
		s_water.motionCopyMsec = std::chrono::duration<float, std::milli>(
			std::chrono::high_resolution_clock::now() - copyStart).count();
	}
	assert(!s_water.history.valid || s_water.history.frameNumber + 1 == backEndData->realFrameNumber ||
		!RB_ScreenHistoryValid(s_water.history, s_water.reflectionScale));
	s_water.interactionHistoryFrame = backEndData->realFrameNumber;
	s_water.motionCommitFrame = backEndData->realFrameNumber;
}

void RB_WaterSurfaceReflectionEnd( void )
{
	screenViewInfo_t info;
	RB_ScreenGetViewInfo(info);
	RB_WaterMotionCommit();
	RB_ScreenStoreHistory(s_water.history, info, s_water.reflectionScale, s_water.reflectionCurrent);
	s_water.reflectionReady = qtrue;
	FBO_Bind(tr.renderFbo);
	const viewParms_t& v = backEnd.viewParms;
	GL_SetViewportAndScissor(v.viewportX, v.viewportY, v.viewportWidth, v.viewportHeight);
}

void RB_WaterSurfaceBindReflection( shaderProgram_t *program, qboolean trace )
{
	vec4_t pass = {trace ? 1.0f : 0.0f,
		trace ? (s_water.reflectionHistoryValid ? 1.0f : 0.0f) : (s_water.reflectionReady ? 1.0f : 0.0f),
		Com_Clamp(0.0f, 0.95f, r_ssrTemporalWeight->value), s_water.reflectionScale};
	GLSL_SetUniformVec4(program, UNIFORM_WATERPASS, pass);
	if (!backEnd.waterSurfaceSSR || !tr.waterReflectionImage[0])
		return;
	const int index = trace ? 1 - s_water.reflectionCurrent : s_water.reflectionCurrent;
	GL_BindToTMU(tr.waterReflectionImage[index], 12);
	GL_BindToTMU(tr.waterReflectionGeomImage[index], 14);
	GL_BindToTMU(tr.waterReflectionHitImage[index], 15);
	if (trace && s_water.reflectionHistoryValid)
		GLSL_SetUniformMatrix4x4(program, UNIFORM_SSRREPROJECT, s_water.history.viewProjection);
}

void RB_WaterSurfaceFinish( void )
{
	RB_ScreenEndTimer(s_water.timer);
	s_water.timer = -1;
	R_PushDebugGroup(AL_STAGE, "Mainpass");
}

/*
=================
RB_WaterSurfaceLegacyScissor

r_waterSurfaceDebug 9: the legacy stages of the classified surfaces are drawn
left of the split only (RB_DrawItems), the water program right of it.
=================
*/
void RB_WaterSurfaceLegacyScissor( qboolean enable )
{
	const viewParms_t& viewParms = backEnd.viewParms;
	if ( enable )
	{
		const int split = (int)(Com_Clamp(0.0f, 1.0f, r_waterSurfaceSplit->value) * (float)viewParms.viewportWidth);
		qglScissor(viewParms.viewportX, viewParms.viewportY, split, viewParms.viewportHeight);
	}
	else
	{
		qglScissor(viewParms.viewportX, viewParms.viewportY, viewParms.viewportWidth, viewParms.viewportHeight);
	}
}

/*
=================
RB_WaterSurfaceDistortion

The refraction pass of the view (refractionFill, after tone mapping) takes
the refractive shaders: not a refractive shader classified as water in a view
with the water program, which draws it in the main pass instead.
=================
*/
qboolean RB_WaterSurfaceDistortion( const shader_t *shader, uint32_t waterKey )
{
	if ( !shader->useDistortion )
		return qfalse;
	const qboolean enabled = (waterKey & WATERKEY_WATERFALL) ?
		(qboolean)(r_waterfall && r_waterfall->integer) : (qboolean)r_waterSurface->integer;
	if ( enabled && (waterKey & WATERKEY_INTERFACE) && backEnd.waterSurfaceView && tr.waterSurfaceShader[0].program )
		return qfalse;
	return qtrue;
}
