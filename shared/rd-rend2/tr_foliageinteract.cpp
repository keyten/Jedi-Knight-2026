/*
 * Foliage character interaction (r_foliageInteraction).
 *
 * cgame sends the real character bodies once per frame (the predicted player
 * first, then the nearest NPCs) as vertical capsules through the optional
 * renderer extension GetRefFoliageAPI; never the camera. Two consumers:
 *   - the grass sprites: the bend field (tr_foliagefield.cpp) turns the
 *     capsules into a per patch grass state, which glsl/surface_sprites.glsl
 *     bends as an inextensible blade;
 *   - the MD3 plants: the frond chains of tr_plantsim.cpp collide with them on
 *     the CPU; glsl/plant_skin.glsl draws the result.
 *
 * Force push / pull (r_foliageForceWaves): cgame sends one wave per cast
 * (GetRefFoliageWaveAPI). Its front leaves the caster at FORCEWAVE_SPEED;
 * whatever it passes inside the cone gets a velocity kick away from (push) or
 * towards (pull) the caster, spread over the time the front needs to cross
 * it (a smoothstep pulse, integrated exactly over any time step). The fronds
 * (tr_plantsim.cpp) and the grass field (glsl/foliage_field.glsl) take the
 * kick as state and spring back by themselves; the stateless leaves
 * (glsl/leaf_flutter.glsl) swing as a damped oscillator from the arrival.
 *
 * Per scene the FoliageInteraction uniform block carries the colliders of this
 * frame and of the previous frame, the bend field parameters and the waves. Every view
 * of the scene (main, mirrors / portals, sun and point shadows) uses the same
 * world space state; the sky portal view and non world scenes get none.
 */
#include "tr_local.h"

// r_foliageInteractionDebug bits
enum
{
	FOLIAGEINTERACT_DEBUG_CAPSULES   = 1,	// draw the colliders
	FOLIAGEINTERACT_DEBUG_EXAGGERATE = 2,	// x2 radius and strength
	FOLIAGEINTERACT_DEBUG_NO_WIND    = 4,	// interaction only
	FOLIAGEINTERACT_DEBUG_HEAT       = 8,	// color by collider contact
	FOLIAGEINTERACT_DEBUG_FREEZE     = 16,	// keep the colliders of one frame
	FOLIAGEINTERACT_DEBUG_PLAYER     = 32	// player only
};

// Force push / pull waves
const int FORCEWAVE_MAX = 8;				// live waves (the oldest drops out)
const float FORCEWAVE_SPEED = 1400.0f;		// front speed (units / s): 512 units in 0.37 s
const float FORCEWAVE_WIDTH = 48.0f;		// half width of the front: the kick lasts 2 w / speed
const float FORCEWAVE_VERTICAL = 160.0f;	// full kick within this height of the caster, none beyond 2x
const float FORCEWAVE_PLANT_KICK = 420.0f;	// velocity kick of a frond node at strength 1 (units / s)
const float FORCEWAVE_GRASS_KICK = 180.0f;	// velocity kick of the grass field at strength 1 (units / s)
const float FORCEWAVE_LEAF_AMPLITUDE = 6.0f;	// largest leaf offset at strength 1 (units)
const float FORCEWAVE_LEAF_TAIL = 3.0f;		// s a tree keeps swinging after the front (glsl: e^-5.4)

struct ForceWave
{
	vec3_t origin;
	vec2_t dir;			// horizontal aim (unit), 0 = all around
	float cone;			// cosine of the half angle, -1 = all around
	float radius;
	float kick;			// strength, negative = pull
	float t0;			// refdef time (s) the wave left, < 0 until latched
};

static struct
{
	// last submission from cgame
	foliageInteractor_t submitted[MAX_FOLIAGE_INTERACTORS];
	int submittedCount;
	int submittedFrame;			// tr.frameCount of the submission

	// the colliders of this frame and of the previous one, as uploaded
	vec4_t current[MAX_FOLIAGE_INTERACTORS * 2];
	vec4_t previous[MAX_FOLIAGE_INTERACTORS * 2];
	int currentCount;
	int previousCount;
	int latchedFrame;			// tr.frameCount of the last latch, -1 none
	int latchedTime;			// refdef time of the last latch

	// the scene being drawn
	int sceneCount;				// colliders of the current scene (0: none)
	bool sceneField;			// the current scene samples the persistent field

	// Force push / pull waves, the oldest first
	ForceWave waves[FORCEWAVE_MAX];
	int numWaves;
	// the newest live ones packed for the shaders (latched once per frame)
	vec4_t wavePack[FOLIAGE_FORCEWAVE_GPU * 3];
	int wavePackCount;
} s_fi = {};

static int FoliageInteractionDebug(void)
{
	return r_foliageInteractionDebug ? r_foliageInteractionDebug->integer : 0;
}

bool R_FoliageInteractionActive(void)
{
	return r_foliageInteraction->integer && r_foliageInteractionStrength->value > 0.0f;
}

// FOLIAGE_PLANT motion: the skinned MD3 plants and grass cards (tr_plantsim.cpp)
uint8_t R_FoliageMotionClass(const drawSurf_t *drawSurf)
{
	if (R_LeafFlutterSurface(drawSurf))
		return FOLIAGE_LEAF;
	if ((drawSurf->foliage.cls == FOLIAGE_PLANT || drawSurf->foliage.cls == FOLIAGE_GRASS) && R_PlantSimActive())
		return FOLIAGE_PLANT;
	return FOLIAGE_NONE;
}

// this frame's colliders (2 vec4 each, the FoliageInteraction layout), for
// the plant simulation; 0 when the interaction is off or none were latched
int R_FoliageInteractionColliders(const vec4_t **colliders)
{
	*colliders = s_fi.current;
	if (!R_FoliageInteractionActive() || s_fi.latchedFrame != tr.frameCount)
		return 0;
	return s_fi.currentCount;
}

/*
=============================================================

Force push / pull waves

=============================================================
*/

bool R_FoliageForceWavesEnabled(void)
{
	return R_FoliageInteractionActive() && r_foliageForceWaves->integer &&
		r_foliageForceWaveStrength->value > 0.0f;
}

// largest leaf offset of a wave (for the cull margin of the trees)
float R_FoliageLeafWaveAmplitude(void)
{
	if (!R_FoliageForceWavesEnabled() || !r_autoFoliage->integer)
		return 0.0f;
	return FORCEWAVE_LEAF_AMPLITUDE * MIN(r_foliageForceWaveStrength->value, 4.0f) * 4.0f;
}

/*
=============
RE_AddFoliageForceWave

Optional renderer extension (tr_public.h): cgame, once per Force push or
pull cast. Starts at the refdef time of the next world scene.
=============
*/
void RE_AddFoliageForceWave(const foliageForceWave_t *in)
{
	if (!in || !R_FoliageForceWavesEnabled() || !tr.world)
		return;

	ForceWave w = {};
	VectorCopy(in->origin, w.origin);
	const float len = sqrtf(in->direction[0] * in->direction[0] + in->direction[1] * in->direction[1]);
	if (len > 0.2f)
	{
		// the cone is horizontal: a push aimed down at the grass parts it too
		w.dir[0] = in->direction[0] / len;
		w.dir[1] = in->direction[1] / len;
		w.cone = Com_Clamp(-1.0f, 0.95f, in->cone);
	}
	else
	{
		// aimed (almost) straight up or down: all around
		w.cone = -1.0f;
	}
	w.radius = Com_Clamp(64.0f, 2048.0f, in->radius);
	w.kick = Com_Clamp(0.0f, 4.0f, in->strength) * r_foliageForceWaveStrength->value;
	if (in->flags & FOLIAGE_FORCEWAVE_PULL)
		w.kick = -w.kick;
	w.t0 = -1.0f;
	if (w.kick == 0.0f)
		return;

	if (s_fi.numWaves == FORCEWAVE_MAX)
	{
		memmove(&s_fi.waves[0], &s_fi.waves[1], (FORCEWAVE_MAX - 1) * sizeof(s_fi.waves[0]));
		--s_fi.numWaves;
	}
	s_fi.waves[s_fi.numWaves++] = w;
}

// seconds a wave lives: its front crosses the radius, then the leaves settle
static float ForceWaveLifetime(const ForceWave &w)
{
	return (w.radius + FORCEWAVE_WIDTH) / FORCEWAVE_SPEED + FORCEWAVE_LEAF_TAIL;
}

// once per frame (the first world scene): new waves start now, finished ones
// go, the newest live ones are packed for the shaders
static void R_FoliageForceWavesLatch(float now)
{
	int live = 0;
	for (int i = 0; i < s_fi.numWaves; ++i)
	{
		ForceWave &w = s_fi.waves[i];
		if (w.t0 < 0.0f)
			w.t0 = now;
		// a time jump back (map restart, demo seek) ends it too
		if (now - w.t0 > ForceWaveLifetime(w) || now < w.t0 - 0.5f || !R_FoliageForceWavesEnabled())
			continue;
		s_fi.waves[live++] = w;
	}
	s_fi.numWaves = live;

	// FoliageForceWavePack: (origin, radius), (aim x, y, cone, kick), (t0,
	// seconds since t0)
	s_fi.wavePackCount = 0;
	for (int i = s_fi.numWaves - 1; i >= 0 && s_fi.wavePackCount < FOLIAGE_FORCEWAVE_GPU; --i)
	{
		const ForceWave &w = s_fi.waves[i];
		float *pack = s_fi.wavePack[s_fi.wavePackCount * 3];
		VectorSet4(pack + 0, w.origin[0], w.origin[1], w.origin[2], w.radius);
		VectorSet4(pack + 4, w.dir[0], w.dir[1], w.cone, w.kick);
		VectorSet4(pack + 8, w.t0, now - w.t0, 0.0f, 0.0f);
		++s_fi.wavePackCount;
	}
}

static float ForceWaveSmoothStep(float edge0, float edge1, float x)
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

// how much of a wave reaches the point p (distance, cone, height falloffs) and
// the horizontal way it is thrown (away from the caster), 0 outside. Same as
// FoliageForceWaveWeight in glsl/foliage_interact.glsl (without the height
// there: the grass field has none)
static float ForceWaveWeight(const ForceWave &w, const float *p, float &d, vec2_t n)
{
	const float dx = p[0] - w.origin[0];
	const float dy = p[1] - w.origin[1];
	d = sqrtf(dx * dx + dy * dy);
	if (d >= w.radius)
		return 0.0f;
	if (d > 1e-3f)
	{
		n[0] = dx / d;
		n[1] = dy / d;
	}
	else if (w.cone > -1.0f)
	{
		n[0] = w.dir[0];
		n[1] = w.dir[1];
	}
	else
	{
		n[0] = 1.0f;
		n[1] = 0.0f;
	}
	float weight = 1.0f;
	if (w.cone > -1.0f)
		weight *= ForceWaveSmoothStep(w.cone - 0.15f, w.cone + 0.05f, n[0] * w.dir[0] + n[1] * w.dir[1]);
	weight *= 1.0f - ForceWaveSmoothStep(FORCEWAVE_VERTICAL, 2.0f * FORCEWAVE_VERTICAL, fabsf(p[2] - w.origin[2]));
	// strongest near the caster, nothing at the reach
	weight *= (1.0f - ForceWaveSmoothStep(0.6f * w.radius, w.radius, d)) * (1.0f - 0.5f * d / w.radius);
	return weight;
}

// part of the kick given while the front moves across the point at distance
// d: 0 before it arrives, 1 once it is past
static float ForceWavePassed(const ForceWave &w, float t, float d)
{
	return ForceWaveSmoothStep(-FORCEWAVE_WIDTH, FORCEWAVE_WIDTH, FORCEWAVE_SPEED * (t - w.t0) - d);
}

/*
=============
R_FoliageForceWaveReaches

A front passes through the sphere between the times t0 and t1 (refdef
seconds): the plant must be stepped (and in detail).
=============
*/
bool R_FoliageForceWaveReaches(const float *center, float radius, float t0, float t1)
{
	for (int i = 0; i < s_fi.numWaves; ++i)
	{
		const ForceWave &w = s_fi.waves[i];
		if (w.t0 < 0.0f)
			continue;
		const float d = sqrtf((center[0] - w.origin[0]) * (center[0] - w.origin[0]) +
			(center[1] - w.origin[1]) * (center[1] - w.origin[1]));
		const float nearest = MAX(d - radius, 0.0f);
		if (nearest >= w.radius)
			continue;
		const float front0 = FORCEWAVE_SPEED * (t0 - w.t0) - FORCEWAVE_WIDTH;
		const float front1 = FORCEWAVE_SPEED * (t1 - w.t0) + FORCEWAVE_WIDTH;
		if (front1 > nearest && front0 < d + radius)
			return true;
	}
	return false;
}

/*
=============
R_FoliageForceWaveImpulse

The velocity change (world units / s) the waves give a point p between the
times t0 and t1 (refdef seconds), added to dv. False when none reaches it.
=============
*/
bool R_FoliageForceWaveImpulse(const float *p, float t0, float t1, float *dv)
{
	bool any = false;
	for (int i = 0; i < s_fi.numWaves; ++i)
	{
		const ForceWave &w = s_fi.waves[i];
		if (w.t0 < 0.0f)
			continue;
		float d;
		vec2_t n;
		const float weight = ForceWaveWeight(w, p, d, n);
		if (weight <= 0.0f)
			continue;
		const float part = ForceWavePassed(w, t1, d) - ForceWavePassed(w, t0, d);
		if (part <= 0.0f)
			continue;
		const float kick = FORCEWAVE_PLANT_KICK * w.kick * weight * part;
		dv[0] += n[0] * kick;
		dv[1] += n[1] * kick;
		any = true;
	}
	return any;
}

// leaf_flutter.glsl, every draw with the library: the waves of this frame
// (count 0 = none, also for the draws that must stay still)
void RB_SetLeafForceWaveUniforms(UniformDataWriter& writer, bool active)
{
	const int count = active ? s_fi.wavePackCount : 0;
	writer.SetUniformVec4(UNIFORM_LEAFFORCEWAVE, (float)count, FORCEWAVE_SPEED, FORCEWAVE_LEAF_AMPLITUDE, 0.0f);
	if (count)
		writer.SetUniformVec4(UNIFORM_LEAFFORCEWAVES, s_fi.wavePack[0], count * 3);
}

/*
=============
RE_SetFoliageInteractors

Optional renderer extension (tr_public.h), called by cgame once per frame
before the main scene.
=============
*/
void RE_SetFoliageInteractors(const foliageInteractor_t *interactors, int count)
{
	if (!interactors || count < 0)
		count = 0;
	count = MIN(count, MAX_FOLIAGE_INTERACTORS);
	if (count)
		Com_Memcpy(s_fi.submitted, interactors, count * sizeof(*interactors));
	s_fi.submittedCount = count;
	s_fi.submittedFrame = tr.frameCount;
}

/*
=============
R_FoliageInteractionLatch

The first world scene of a frame takes this frame's colliders; the previous
ones become the motion vector history. Called once per frame.
=============
*/
static void R_FoliageInteractionLatch(const refdef_t *fd)
{
	R_FoliageForceWavesLatch(fd->time * 0.001f);

	const int debug = FoliageInteractionDebug();
	const bool consecutive = s_fi.latchedFrame == tr.frameCount - 1 &&
		fd->time >= s_fi.latchedTime && fd->time - s_fi.latchedTime <= 250;

	// the grass bend field follows this frame's player body
	const foliageInteractor_t *player = NULL;
	if (s_fi.submittedFrame == tr.frameCount)
	{
		for (int i = 0; i < s_fi.submittedCount && !player; ++i)
		{
			if (s_fi.submitted[i].flags & FOLIAGE_INTERACTOR_PLAYER)
				player = &s_fi.submitted[i];
		}
	}

	if ((debug & FOLIAGEINTERACT_DEBUG_FREEZE) && s_fi.latchedFrame >= 0)
	{
		// frozen: the same colliders in both frames, no motion
		Com_Memcpy(s_fi.previous, s_fi.current, sizeof(s_fi.previous));
		s_fi.previousCount = s_fi.currentCount;
		s_fi.latchedFrame = tr.frameCount;
		s_fi.latchedTime = fd->time;
		R_FoliageFieldLatch(fd, consecutive, player);
		return;
	}

	// the previous frame's upload is exactly what was drawn last frame
	Com_Memcpy(s_fi.previous, s_fi.current, sizeof(s_fi.previous));
	s_fi.previousCount = s_fi.currentCount;

	float radiusScale = r_foliageInteractionRadius->value;
	if (debug & FOLIAGEINTERACT_DEBUG_EXAGGERATE)
		radiusScale *= 2.0f;
	const bool playerOnly = !r_foliageInteractionNPCs->integer || (debug & FOLIAGEINTERACT_DEBUG_PLAYER);
	const int maxCount = Com_Clampi(1, MAX_FOLIAGE_INTERACTORS, r_foliageInteractionMaxInteractors->integer);

	int count = 0;
	if (s_fi.submittedFrame == tr.frameCount)
	{
		// cgame keeps the player first and sorts the others by distance, so
		// a smaller r_foliageInteractionMaxInteractors drops the farthest
		for (int i = 0; i < s_fi.submittedCount && count < maxCount; ++i)
		{
			const foliageInteractor_t *in = &s_fi.submitted[i];
			if (playerOnly && !(in->flags & FOLIAGE_INTERACTOR_PLAYER))
				continue;
			const float radius = MAX(in->radius, 1.0f) * radiusScale;
			const float height = MAX(in->height, 1.0f);
			VectorSet4(s_fi.current[count * 2 + 0],
				in->base[0], in->base[1], in->base[2], in->base[2] + height);
			// airborne bodies still part the grass they touch, but leave
			// nothing in the persistent field
			VectorSet4(s_fi.current[count * 2 + 1],
				radius, in->velocity[0], in->velocity[1],
				(in->flags & FOLIAGE_INTERACTOR_AIRBORNE) ? 1.0f : 0.0f);
			++count;
		}
	}
	s_fi.currentCount = count;

	// no usable history (first frame, cut, pause, map load): no motion
	if (!consecutive || !tr.temporalHistoryValid)
	{
		Com_Memcpy(s_fi.previous, s_fi.current, sizeof(s_fi.previous));
		s_fi.previousCount = s_fi.currentCount;
	}

	s_fi.latchedFrame = tr.frameCount;
	s_fi.latchedTime = fd->time;

	R_FoliageFieldLatch(fd, consecutive, player);
}

static void R_FoliageInteractionDebugCapsules(const refdef_t *fd);
static void R_FoliageFieldDebugBounds(const refdef_t *fd);
static void R_PlantDebugScene(const refdef_t *fd);

/*
=============
R_FoliageInteractionBeginScene

RE_BeginScene: decides the colliders of this scene. Only world scenes get any
(HUD models and menus never bend plants); the sky portal scene is skipped
here and its view is excluded per draw.
=============
*/
void R_FoliageInteractionBeginScene(const refdef_t *fd)
{
	s_fi.sceneCount = 0;
	s_fi.sceneField = false;
	R_PlantDebugScene(fd);
	if (!R_FoliageInteractionActive() || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) || !tr.world)
		return;

	if (s_fi.latchedFrame != tr.frameCount)
		R_FoliageInteractionLatch(fd);
	s_fi.sceneCount = s_fi.currentCount;
	// the field keeps bending the plants after the characters are gone
	s_fi.sceneField = R_FoliageFieldActive();

	if (FoliageInteractionDebug() & FOLIAGEINTERACT_DEBUG_CAPSULES)
		R_FoliageInteractionDebugCapsules(fd);
	R_FoliageFieldDebugBounds(fd);
}

// r_plantDebug lines: also without character interaction (wind only)
static void R_PlantDebugScene(const refdef_t *fd)
{
	if (!(fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) && tr.world)
		R_PlantSimDebugDraw(fd);
}

void R_FoliageInteractionReset(void)
{
	s_fi.submittedCount = 0;
	s_fi.submittedFrame = -1;
	s_fi.currentCount = s_fi.previousCount = 0;
	s_fi.latchedFrame = -1;
	s_fi.sceneCount = 0;
	s_fi.sceneField = false;
	s_fi.numWaves = 0;
	s_fi.wavePackCount = 0;
	R_FoliageFieldReset();
	// the plant instances point into the models of the previous map
	R_PlantSimReset();
}

/*
=============
RB_UpdateFoliageInteractionConstants

RB_UpdateConstants, every scene: the FoliageInteraction block (the draws
of a scene without colliders are told so by their per draw uniforms).
=============
*/
void RB_UpdateFoliageInteractionConstants(gpuFrame_t *frame, const trRefdef_t *refdef, float previousTime)
{
	(void)refdef;
	(void)previousTime;
	tr.foliageInteractionUboOffset = -1;
	if (!R_FoliageInteractionActive())
		return;

	FoliageInteractionBlock block = {};
	const int debug = FoliageInteractionDebug();
	float strength = r_foliageInteractionStrength->value;
	if (debug & FOLIAGEINTERACT_DEBUG_EXAGGERATE)
		strength *= 2.0f;
	const int previousCount = s_fi.sceneCount ? s_fi.previousCount : 0;
	VectorSet4(block.params, (float)s_fi.sceneCount, (float)previousCount, strength,
		(debug & FOLIAGEINTERACT_DEBUG_NO_WIND) ? 1.0f : 0.0f);
	if (s_fi.sceneCount)
	{
		Com_Memcpy(block.current, s_fi.current, sizeof(block.current));
		Com_Memcpy(block.previous, s_fi.previous, sizeof(block.previous));
	}
	R_FoliageFieldBlock(&block, debug);
	// the grass field update takes the waves (glsl/foliage_field.glsl)
	VectorSet4(block.waveParams, (float)s_fi.wavePackCount, FORCEWAVE_WIDTH, FORCEWAVE_GRASS_KICK, FORCEWAVE_SPEED);
	if (s_fi.wavePackCount)
		Com_Memcpy(block.waves, s_fi.wavePack, s_fi.wavePackCount * 3 * sizeof(vec4_t));
	tr.foliageInteractionUboOffset = RB_AppendConstantsData(frame, &block, sizeof(block));

	// the first world scene of the frame updates the persistent field before
	// its draws, with the colliders of this block
	R_FoliageFieldQueueUpdate(frame->ubo[frame->currentScene], tr.foliageInteractionUboOffset);
}

UniformBlockBinding RB_GetFoliageInteractionBlockUniformBinding(void)
{
	const byte currentFrameScene = backEndData->currentFrame->currentScene;
	UniformBlockBinding binding = {};
	binding.ubo = backEndData->currentFrame->ubo[currentFrameScene];
	binding.block = UNIFORM_BLOCK_FOLIAGE_INTERACTION;
	// without a block the draws have the interaction off (per draw uniforms)
	binding.offset = (tr.foliageInteractionUboOffset == -1) ? 0 : tr.foliageInteractionUboOffset;
	return binding;
}

// the colliders reach the draws of this view: a world scene with colliders
// (or the persistent field), not the sky portal (its geometry lives at other
// coordinates)
static bool RB_FoliageInteractionInView(void)
{
	return tr.foliageInteractionUboOffset != -1 && (s_fi.sceneCount > 0 || s_fi.sceneField) &&
		backEnd.viewParms.viewParmType != VPT_SKYPORTAL;
}

static bool RB_FoliageDebugView(void)
{
	return !backEnd.depthFill && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW);
}

// SurfaceSprites (RB_SurfaceSprites), every sprite draw: u_FoliageInteract
void RB_SetSpriteInteractionUniforms(UniformDataWriter& writer)
{
	const bool on = RB_FoliageInteractionInView();
	const bool heat = on && (FoliageInteractionDebug() & FOLIAGEINTERACT_DEBUG_HEAT) && RB_FoliageDebugView();
	writer.SetUniformVec4(UNIFORM_FOLIAGEINTERACT, on ? 1.0f : 0.0f, heat ? 1.0f : 0.0f, 0.0f, 0.0f);
}

/*
=============
RB_SetFoliageMotionUniforms

Every draw with a program that contains the foliage vertex library (leaf
flutter + plant skin): the values stick per program, so draws that must stay
still write zeros too. cls: tess.foliageMotion (FOLIAGE_LEAF, FOLIAGE_PLANT or
FOLIAGE_NONE).
=============
*/
void RB_SetFoliageMotionUniforms(UniformDataWriter& writer, uint8_t cls)
{
	RB_SetLeafFlutterUniforms(writer, cls == FOLIAGE_LEAF);

	vec4_t skin;
	if (cls != FOLIAGE_PLANT || !RB_PlantSkinUniforms(skin))
		VectorSet4(skin, 0.0f, 0.0f, 0.0f, 0.0f);
	writer.SetUniformVec4(UNIFORM_PLANTSKIN, skin);
}

/*
=============
Debug capsules (r_foliageInteractionDebug 1): the colliders as line polygons,
yellow = player, cyan = NPC, to compare with the character bodies
=============
*/
qhandle_t RE_RegisterShaderFromImage(const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage);

static void R_FoliageDebugSegmentShader(qhandle_t shader, const refdef_t *fd, const vec3_t a, const vec3_t b, const byte *rgba)
{
	vec3_t dir, toEye, side;
	VectorSubtract(b, a, dir);
	VectorSubtract(a, fd->vieworg, toEye);
	CrossProduct(dir, toEye, side);
	if (VectorNormalize(side) < 1e-6f)
		return;
	VectorScale(side, 0.35f + 0.0015f * VectorLength(toEye), side);

	polyVert_t verts[4];
	VectorSubtract(a, side, verts[0].xyz);
	VectorAdd(a, side, verts[1].xyz);
	VectorAdd(b, side, verts[2].xyz);
	VectorSubtract(b, side, verts[3].xyz);
	for (int i = 0; i < 4; i++)
	{
		verts[i].st[0] = verts[i].st[1] = 0.5f;
		Com_Memcpy(verts[i].modulate, rgba, 4);
	}
	RE_AddPolyToScene(shader, 4, verts, 1);
}

// a debug line (camera facing quad), for r_plantDebug too
void R_FoliageDebugSegment(const refdef_t *fd, const vec3_t a, const vec3_t b, const byte *rgba)
{
	const qhandle_t shader = RE_RegisterShaderFromImage("*foliageInteractDebug", lightmaps2d,
		stylesDefault, tr.whiteImage, qfalse);
	R_FoliageDebugSegmentShader(shader, fd, a, b, rgba);
}

static void R_FoliageInteractionDebugCapsules(const refdef_t *fd)
{
	const qhandle_t shader = RE_RegisterShaderFromImage("*foliageInteractDebug", lightmaps2d,
		stylesDefault, tr.whiteImage, qfalse);
	static const byte player[4] = { 255, 220, 40, 255 };
	static const byte npc[4] = { 40, 220, 255, 255 };
	const int segments = 16;

	for (int i = 0; i < s_fi.currentCount; ++i)
	{
		const float *axis = s_fi.current[i * 2 + 0];
		const float *body = s_fi.current[i * 2 + 1];
		const byte *rgba = i == 0 && (s_fi.submittedCount > 0 &&
			(s_fi.submitted[0].flags & FOLIAGE_INTERACTOR_PLAYER)) ? player : npc;
		const float r = body[0];
		// the capsule segment (feet + r .. head - r) and its radius; the
		// grass reaction starts at 1.75 r (glsl/foliage_interact.glsl)
		const float z0 = axis[2] + r;
		const float z1 = MAX(axis[3] - r, z0);
		const float levels[4] = { axis[2], z0, z1, axis[3] };
		const float radii[4] = { r * 0.35f, r, r, r * 0.35f };
		for (int l = 0; l < 4; ++l)
		{
			for (int s = 0; s < segments; ++s)
			{
				const float a0 = (float)s / segments * 2.0f * M_PI;
				const float a1 = (float)(s + 1) / segments * 2.0f * M_PI;
				vec3_t p0, p1;
				VectorSet(p0, axis[0] + cosf(a0) * radii[l], axis[1] + sinf(a0) * radii[l], levels[l]);
				VectorSet(p1, axis[0] + cosf(a1) * radii[l], axis[1] + sinf(a1) * radii[l], levels[l]);
				R_FoliageDebugSegmentShader(shader, fd, p0, p1, rgba);
			}
		}
		for (int s = 0; s < 4; ++s)
		{
			const float a = s * 0.5f * M_PI;
			vec3_t p0, p1;
			VectorSet(p0, axis[0] + cosf(a) * r, axis[1] + sinf(a) * r, z0);
			VectorSet(p1, axis[0] + cosf(a) * r, axis[1] + sinf(a) * r, z1);
			R_FoliageDebugSegmentShader(shader, fd, p0, p1, rgba);
		}
		// velocity, 0.25 s ahead
		vec3_t foot, tip;
		VectorSet(foot, axis[0], axis[1], axis[2] + 2.0f);
		VectorSet(tip, axis[0] + body[1] * 0.25f, axis[1] + body[2] * 0.25f, axis[2] + 2.0f);
		if (DistanceSquared(foot, tip) > 1.0f)
			R_FoliageDebugSegmentShader(shader, fd, foot, tip, rgba);
	}

	// Force waves: the front inside its cone (orange = push, magenta = pull)
	// and the reach, faint
	static const byte push[4] = { 255, 140, 30, 255 };
	static const byte pull[4] = { 220, 60, 255, 255 };
	static const byte reach[4] = { 120, 120, 120, 255 };
	const float now = fd->time * 0.001f;
	for (int i = 0; i < s_fi.numWaves; ++i)
	{
		const ForceWave &w = s_fi.waves[i];
		if (w.t0 < 0.0f)
			continue;
		const float front = FORCEWAVE_SPEED * (now - w.t0);
		const float yaw = w.cone > -1.0f ? atan2f(w.dir[1], w.dir[0]) : 0.0f;
		const float half = w.cone > -1.0f ? acosf(Com_Clamp(-1.0f, 1.0f, w.cone - 0.05f)) : M_PI;
		const int arcs = 24;
		for (int ring = 0; ring < 2; ++ring)
		{
			const float r = ring ? w.radius : front;
			if (r <= 0.0f || r > w.radius)
				continue;
			for (int s = 0; s < arcs; ++s)
			{
				const float a0 = yaw - half + 2.0f * half * s / arcs;
				const float a1 = yaw - half + 2.0f * half * (s + 1) / arcs;
				vec3_t p0, p1;
				VectorSet(p0, w.origin[0] + cosf(a0) * r, w.origin[1] + sinf(a0) * r, w.origin[2]);
				VectorSet(p1, w.origin[0] + cosf(a1) * r, w.origin[1] + sinf(a1) * r, w.origin[2]);
				R_FoliageDebugSegmentShader(shader, fd, p0, p1, ring ? reach : (w.kick < 0.0f ? pull : push));
			}
		}
	}
}

// r_foliageBendFieldDebug 2: the square the persistent field covers, at the
// player's feet (green)
static void R_FoliageFieldDebugBounds(const refdef_t *fd)
{
	vec2_t mins, maxs;
	if (!R_FoliageFieldBounds(mins, maxs))
		return;

	const qhandle_t shader = RE_RegisterShaderFromImage("*foliageInteractDebug", lightmaps2d,
		stylesDefault, tr.whiteImage, qfalse);
	static const byte green[4] = { 60, 255, 90, 255 };
	float z = fd->vieworg[2] - 48.0f;
	for (int i = 0; i < s_fi.currentCount; ++i)
	{
		if (i < s_fi.submittedCount && (s_fi.submitted[i].flags & FOLIAGE_INTERACTOR_PLAYER))
			z = s_fi.current[i * 2][2] + 2.0f;
	}
	const float xs[4] = { mins[0], maxs[0], maxs[0], mins[0] };
	const float ys[4] = { mins[1], mins[1], maxs[1], maxs[1] };
	for (int i = 0; i < 4; ++i)
	{
		vec3_t a, b;
		VectorSet(a, xs[i], ys[i], z);
		VectorSet(b, xs[(i + 1) & 3], ys[(i + 1) & 3], z);
		R_FoliageDebugSegmentShader(shader, fd, a, b, green);
	}
}

// r_foliageInteractionDebug: print the colliders once
void R_FoliageInteractionList_f(void)
{
	ri.Printf(PRINT_ALL, "foliage interaction: %s, submitted %d (frame %d, now %d), uploaded %d, previous %d\n",
		R_FoliageInteractionActive() ? "on" : "off", s_fi.submittedCount, s_fi.submittedFrame,
		tr.frameCount, s_fi.currentCount, s_fi.previousCount);
	R_FoliageFieldInfo();
	ri.Printf(PRINT_ALL, "force waves: %s, %d live, %d on the GPU\n",
		R_FoliageForceWavesEnabled() ? "on" : "off", s_fi.numWaves, s_fi.wavePackCount);
	for (int i = 0; i < s_fi.numWaves; ++i)
	{
		const ForceWave &w = s_fi.waves[i];
		ri.Printf(PRINT_ALL, "  wave %d: %s from (%.0f %.0f %.0f) aim (%.2f %.2f) cone %.2f radius %.0f "
			"strength %.2f, start %.2f s\n", i, w.kick < 0.0f ? "pull" : "push", w.origin[0], w.origin[1],
			w.origin[2], w.dir[0], w.dir[1], w.cone, w.radius, fabsf(w.kick), w.t0);
	}
	for (int i = 0; i < s_fi.submittedCount; ++i)
	{
		const foliageInteractor_t *in = &s_fi.submitted[i];
		ri.Printf(PRINT_ALL, "  %2d: ent %4d %s feet (%.0f %.0f %.0f) height %.0f radius %.0f speed %.0f\n",
			i, in->id, (in->flags & FOLIAGE_INTERACTOR_PLAYER) ? "player" : "npc   ",
			in->base[0], in->base[1], in->base[2], in->height, in->radius,
			sqrtf(in->velocity[0] * in->velocity[0] + in->velocity[1] * in->velocity[1]));
	}
}
