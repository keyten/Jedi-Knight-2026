/*
 * MD3 leaf flutter (r_leafFlutter). A very small, spatially coherent vertex
 * displacement of the MD3 surfaces classified FOLIAGE_LEAF by the automatic
 * foliage classification (tr_foliage.cpp). Trunks, branches and the tree
 * itself never move. The motion is computed in the vertex shaders
 * (glsl/leaf_flutter.glsl, pasted into lightall, generic, fogpass and
 * velocity); this file only feeds them per frame and per draw constants.
 * No CPU simulation and no new shader permutations: every draw writes the
 * uniforms, amplitude 0 for everything that must stay still.
 *
 * The same library swings the leaves when a Force push / pull wave passes
 * (r_foliageForceWaves, tr_foliageinteract.cpp), also with r_leafFlutter 0.
 */
#include "tr_local.h"

// r_leafFlutterDebug bits
enum
{
	LEAFFLUTTER_DEBUG_EXAGGERATE = 1,	// x8 amplitude
	LEAFFLUTTER_DEBUG_FREEZE     = 2,	// time stops, both velocity frames equal
	LEAFFLUTTER_DEBUG_HIGHLIGHT  = 4,	// flat color on fluttering surfaces
	LEAFFLUTTER_DEBUG_MAGNITUDE  = 8	// color by displacement magnitude
};

// amplitude in world units at r_leafFlutterStrength 1: about 1 % of a stock
// Yavin leaf card (130 - 500 units across)
static const float LEAFFLUTTER_BASE_AMPLITUDE = 1.5f;
// largest |offset| / amplitude of LeafFlutterOffset (leaf_flutter.glsl)
static const float LEAFFLUTTER_MAX_OFFSET = 0.81f;

static struct
{
	float time;
	float previousTime;
	float frozenTime;	// < 0: not frozen
} leafFlutter = { 0.0f, 0.0f, -1.0f };

static int LeafFlutterDebug(void)
{
	return r_leafFlutterDebug ? r_leafFlutterDebug->integer : 0;
}

static float LeafFlutterAmplitude(void)
{
	float amplitude = LEAFFLUTTER_BASE_AMPLITUDE * r_leafFlutterStrength->value;
	if (LeafFlutterDebug() & LEAFFLUTTER_DEBUG_EXAGGERATE)
		amplitude *= 8.0f;
	return amplitude;
}

bool R_LeafFlutterActive(void)
{
	return r_leafFlutter->integer && r_autoFoliage->integer &&
		r_leafFlutterStrength->value > 0.0f;
}

// Force push / pull waves move the leaves too (only the waves, without
// r_leafFlutter)
static bool R_LeafForceWavesActive(void)
{
	return r_autoFoliage->integer && R_FoliageForceWavesEnabled();
}

bool R_LeafFlutterSurface(const drawSurf_t *drawSurf)
{
	return drawSurf->foliage.cls == FOLIAGE_LEAF && (R_LeafFlutterActive() || R_LeafForceWavesActive());
}

// Model bounds stay static; the culling of map object models grows by the
// largest possible offset instead (a constant, nothing per frame).
float R_LeafFlutterCullMargin(const mdvModel_t *model)
{
	if (!model || !(model->foliageSignals & FOLIAGE_MODEL_OBJECT))
		return 0.0f;
	const float flutter = R_LeafFlutterActive() ? LeafFlutterAmplitude() * LEAFFLUTTER_MAX_OFFSET : 0.0f;
	const float wave = R_LeafForceWavesActive() ? R_FoliageLeafWaveAmplitude() : 0.0f;
	return flutter + wave > 0.0f ? flutter + wave + 0.5f : 0.0f;
}

// Once per scene, next to the Scene / TemporalInfo blocks: the times of the
// current and the previous frame (the velocity pass evaluates the flutter at
// both). Freeze latches one time for both.
void RB_LeafFlutterBeginFrame(float currentTime, float previousTime)
{
	if (LeafFlutterDebug() & LEAFFLUTTER_DEBUG_FREEZE)
	{
		if (leafFlutter.frozenTime < 0.0f)
			leafFlutter.frozenTime = currentTime;
		currentTime = previousTime = leafFlutter.frozenTime;
	}
	else
	{
		leafFlutter.frozenTime = -1.0f;
	}

	// no usable history (map load, cut): no flutter velocity
	if (previousTime > currentTime || currentTime - previousTime > 0.25f)
		previousTime = currentTime;

	leafFlutter.time = currentTime;
	leafFlutter.previousTime = previousTime;
}

// 1 / horizontal extent of the current MD3 model around its origin, for
// LeafFlutterWeight (object space, independent of the entity scale)
static float LeafFlutterInvRadius(void)
{
	const trRefEntity_t *ent = backEnd.currentEntity;
	if (!ent || ent == &tr.worldEntity)
		return 0.0f;

	const model_t *model = R_GetModelByHandle(ent->e.hModel);
	if (!model || model->type != MOD_MESH || !model->data.mdv[0])
		return 0.0f;

	const mdvModel_t *mdv = model->data.mdv[0];
	float radius = 1.0f;
	for (int i = 0; i < 2; ++i)
	{
		radius = MAX(radius, fabsf(mdv->foliageMins[i]));
		radius = MAX(radius, fabsf(mdv->foliageMaxs[i]));
	}
	return 1.0f / radius;
}

// Called by every draw with a program that contains leaf_flutter.glsl. The
// values stick per program, so inactive draws must write amplitude 0 too.
void RB_SetLeafFlutterUniforms(UniformDataWriter& writer, bool active)
{
	if (!active)
	{
		writer.SetUniformVec4(UNIFORM_LEAFFLUTTER, 0.0f, 0.0f, 0.0f, 0.0f);
		writer.SetUniformVec4(UNIFORM_LEAFFLUTTERPARAMS, 0.0f, 0.0f, 0.0f, 0.0f);
		writer.SetUniformFloat(UNIFORM_LEAFFLUTTERDEBUG, 0.0f);
		RB_SetLeafForceWaveUniforms(writer, false);
		return;
	}
	RB_SetLeafForceWaveUniforms(writer, R_LeafForceWavesActive());
	// only the waves: the flutter itself stays at rest
	const bool flutter = R_LeafFlutterActive();

	const int debug = LeafFlutterDebug();
	// shared breeze direction with the surface sprite grass (r_foliageWind)
	const float yaw = DEG2RAD(r_foliageWindDirection->value);
	float normalAmount = r_leafFlutterNormal->value * MIN(r_leafFlutterStrength->value, 2.0f);
	if (debug & LEAFFLUTTER_DEBUG_EXAGGERATE)
		normalAmount *= 3.0f;

	writer.SetUniformVec4(UNIFORM_LEAFFLUTTER,
		cosf(yaw), sinf(yaw), flutter ? LeafFlutterAmplitude() : 0.0f, r_leafFlutterSpeed->value);
	writer.SetUniformVec4(UNIFORM_LEAFFLUTTERPARAMS,
		leafFlutter.time, leafFlutter.previousTime, LeafFlutterInvRadius(), flutter ? normalAmount : 0.0f);

	float debugView = 0.0f;
	if (!backEnd.depthFill && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW))
	{
		if (debug & LEAFFLUTTER_DEBUG_MAGNITUDE)
			debugView = 8.0f;
		else if (debug & LEAFFLUTTER_DEBUG_HIGHLIGHT)
			debugView = 4.0f;
	}
	writer.SetUniformFloat(UNIFORM_LEAFFLUTTERDEBUG, debugView);
}

// r_leafFlutterDebug 4 / 8: the u_MaterialDebug view of lightall / generic
// (the fragment shader picks the magnitude color for 8)
bool RB_LeafFlutterDebugColor(vec4_t color)
{
	const int debug = LeafFlutterDebug();
	if (!(debug & (LEAFFLUTTER_DEBUG_HIGHLIGHT | LEAFFLUTTER_DEBUG_MAGNITUDE)))
		return false;
	VectorSet4(color, 0.15f, 1.0f, 0.25f, 1.0f);
	return true;
}
