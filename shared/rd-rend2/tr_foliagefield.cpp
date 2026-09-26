/*
 * Persistent foliage bend field (r_foliageField) of the character interaction
 * (tr_foliageinteract.cpp).
 *
 * The direct push of the colliders is stateless: a plant snaps back to its
 * wind pose the frame a character leaves it. The field adds a short history
 * without any per plant state: one world XY texture around the player, each
 * texel a patch of ground holding the bend of the plants on it and its
 * velocity (RGBA16F: rg = bend, ba = velocity). Once per frame a fullscreen
 * fragment pass (glsl/foliage_field.glsl, two ping-pong textures, no compute:
 * rend2 runs on GL 3.2) moves every patch with a damped spring towards the
 * push of the characters standing on it, or back to rest. The grass sprites
 * sample it at their tuft anchor, the MD3 plants at their root; the leaf
 * flutter of the trees never does.
 *
 * The field is gameplay state, not camera state: it follows the player
 * collider (never the view origin, so cinematics and third person don't move
 * it), snapped to whole texels so the stored state scrolls without any
 * resampling. It is updated by the first world scene of a frame (the same
 * latch as the colliders); every view of that frame (mirrors, portals,
 * shadows, motion vectors) only samples it. Cleared on map load, vid_restart,
 * pauses / time jumps, teleports (a shift of more than half the field) and
 * r_foliageFieldClear.
 */
#include "tr_local.h"

// r_foliageFieldDebug bits
enum
{
	FOLIAGEFIELD_DEBUG_OVERLAY    = 1,	// the field in a corner of the screen
	FOLIAGEFIELD_DEBUG_BOUNDS     = 2,	// outline of the covered square
	FOLIAGEFIELD_DEBUG_FREEZE     = 4,	// no update
	FOLIAGEFIELD_DEBUG_EXAGGERATE = 8,	// x3 field strength
	FOLIAGEFIELD_DEBUG_NO_DIRECT  = 16,	// the field alone, no direct push
	FOLIAGEFIELD_DEBUG_NO_FIELD   = 32	// the direct push alone (the field still updates)
};

static struct
{
	bool resources;
	int size;						// texels per side
	image_t *image[2];
	FBO_t *fbo[2];

	// state of the textures
	bool valid;						// image[current] holds the field at origin
	int current;					// this frame's state
	int previous;					// the previous frame's state (== current: no motion)
	int originX, originY;			// field center in texels (world = origin * texel size)
	float prevCenter[2];			// world center of the previous state
	float extent;					// world size of the square
	const world_t *world;
	int lastTime;					// refdef time of the last latch
	bool clearRequested;

	// this frame
	bool frameActive;				// the field is sampled this frame
	bool pending;					// command not queued yet
	int target;						// state written this frame, -1 none
	int source;
	bool clear;
	int shiftX, shiftY;
	float dt;
} s_ff = {};

static int FoliageFieldDebug(void)
{
	return r_foliageFieldDebug ? r_foliageFieldDebug->integer : 0;
}

bool R_FoliageFieldActive(void)
{
	return s_ff.resources && r_foliageField->integer && R_FoliageInteractionActive();
}

/*
=============
Resources: two RGBA16F squares, sampled with linear filtering (the plants sit
between texel centers), always created (256 KB at 128 x 128) so r_foliageField
toggles without vid_restart; r_foliageFieldSize is latched.
=============
*/
void R_CreateFoliageFieldImages(void)
{
	s_ff = {};
	const int requested = r_foliageFieldSize->integer;
	s_ff.size = requested >= 256 ? 256 : (requested >= 128 ? 128 : 64);
	for (int i = 0; i < 2; i++)
	{
		s_ff.image[i] = R_ScreenCreateImage(va("*foliageField%d", i), s_ff.size, s_ff.size,
			GL_RGBA16F, qtrue);
	}
}

void R_CreateFoliageFieldFBOs(void)
{
	for (int i = 0; i < 2; i++)
		s_ff.fbo[i] = s_ff.image[i] ? R_ScreenCreateLevelFBO(va("_foliageField%d", i), s_ff.image[i], 0) : NULL;
	s_ff.resources = s_ff.fbo[0] && s_ff.fbo[1];
	s_ff.valid = false;
}

void R_FoliageFieldReset(void)
{
	s_ff.valid = false;
	s_ff.frameActive = false;
	s_ff.pending = false;
}

void R_FoliageFieldClear_f(void)
{
	s_ff.clearRequested = true;
}

/*
=============
R_FoliageFieldLatch

The first world scene of a frame (R_FoliageInteractionLatch, after this
frame's colliders): moves the field with the player and decides the update of
this frame. consecutive: this frame directly follows the previous latch (no
pause, cut, menu or time jump). player: this frame's player collider or NULL.
=============
*/
void R_FoliageFieldLatch(const refdef_t *fd, bool consecutive, const foliageInteractor_t *player)
{
	s_ff.frameActive = false;
	s_ff.pending = false;
	s_ff.target = -1;
	if (!R_FoliageFieldActive())
	{
		s_ff.valid = false;
		return;
	}

	const int debug = FoliageFieldDebug();
	const float extent = Com_Clamp(256.0f, 8192.0f, r_foliageFieldExtent->value);
	const float texel = extent / s_ff.size;

	bool clear = !s_ff.valid || !consecutive || s_ff.world != tr.world ||
		s_ff.extent != extent || s_ff.clearRequested;

	// centered on the player body; without one (no collider this frame) the
	// field stays where it is, or starts at the view
	int originX = s_ff.originX, originY = s_ff.originY;
	if (player)
	{
		originX = (int)floorf(player->base[0] / texel + 0.5f);
		originY = (int)floorf(player->base[1] / texel + 0.5f);
	}
	else if (clear)
	{
		originX = (int)floorf(fd->vieworg[0] / texel + 0.5f);
		originY = (int)floorf(fd->vieworg[1] / texel + 0.5f);
	}
	const int shiftX = originX - s_ff.originX;
	const int shiftY = originY - s_ff.originY;
	if (abs(shiftX) > s_ff.size / 2 || abs(shiftY) > s_ff.size / 2)
		clear = true;	// teleport

	float dt = consecutive ? (fd->time - s_ff.lastTime) * 0.001f : 0.0f;
	dt = Com_Clamp(0.0f, 0.1f, dt);

	// the previous frame's field, for the motion vectors
	s_ff.previous = s_ff.current;
	s_ff.prevCenter[0] = s_ff.originX * s_ff.extent / s_ff.size;
	s_ff.prevCenter[1] = s_ff.originY * s_ff.extent / s_ff.size;

	// frozen or paused: the field keeps its origin (and no motion)
	const bool update = clear || (!(debug & FOLIAGEFIELD_DEBUG_FREEZE) && dt > 0.0f);
	if (update)
	{
		s_ff.source = s_ff.current;
		s_ff.target = 1 - s_ff.current;
		s_ff.clear = clear;
		s_ff.shiftX = clear ? 0 : shiftX;
		s_ff.shiftY = clear ? 0 : shiftY;
		s_ff.dt = dt;
		s_ff.current = s_ff.target;
		s_ff.originX = originX;
		s_ff.originY = originY;
	}

	s_ff.extent = extent;
	if (clear || !update)
	{
		// no history: the previous frame looks like this one
		s_ff.previous = s_ff.current;
		s_ff.prevCenter[0] = s_ff.originX * texel;
		s_ff.prevCenter[1] = s_ff.originY * texel;
	}

	s_ff.valid = true;
	s_ff.world = tr.world;
	s_ff.lastTime = fd->time;
	s_ff.clearRequested = false;
	s_ff.frameActive = true;
	s_ff.pending = true;
}

/*
=============
R_FoliageFieldBlock

The field part of the FoliageInteraction block, every scene (all views of a
frame see the same field).
=============
*/
void R_FoliageFieldBlock(FoliageInteractionBlock *block, int interactionDebugBits)
{
	VectorSet4(block->field, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(block->fieldPrevious, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(block->fieldUpdate, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(block->fieldShift, 0.0f, 0.0f, 0.0f, 0.0f);
	if (!s_ff.frameActive)
		return;

	const int debug = FoliageFieldDebug();
	float scale = r_foliageFieldStrength->value;
	if (debug & FOLIAGEFIELD_DEBUG_EXAGGERATE)
		scale *= 3.0f;
	if (debug & FOLIAGEFIELD_DEBUG_NO_FIELD)
		scale = 0.0f;

	const float texel = s_ff.extent / s_ff.size;
	const float invExtent = 1.0f / s_ff.extent;
	VectorSet4(block->field, s_ff.originX * texel, s_ff.originY * texel, invExtent, scale);
	VectorSet4(block->fieldPrevious, s_ff.prevCenter[0], s_ff.prevCenter[1], invExtent, scale);

	// damped spring: r_foliageFieldRecovery = seconds until the swing is
	// down to ~5 % (envelope exp(-zeta w t) = e^-3), zeta = r_foliageFieldDamping
	const float zeta = Com_Clamp(0.2f, 1.0f, r_foliageFieldDamping->value);
	const float recovery = Com_Clamp(0.1f, 10.0f, r_foliageFieldRecovery->value);
	const float omega = 3.0f / (zeta * recovery);
	VectorSet4(block->fieldUpdate, s_ff.dt, omega * omega, 2.0f * zeta * omega,
		MAX(r_foliageFieldImpulse->value, 0.0f));
	VectorSet4(block->fieldShift, (float)s_ff.shiftX, (float)s_ff.shiftY, s_ff.clear ? 1.0f : 0.0f,
		(debug & FOLIAGEFIELD_DEBUG_NO_DIRECT) ? 1.0f : 0.0f);
	(void)interactionDebugBits;
}

/*
=============
R_FoliageFieldQueueUpdate

RB_UpdateFoliageInteractionConstants of the first world scene, once the
block is in the scene's buffer: the update runs in front of the scene's draws.
=============
*/
void R_FoliageFieldQueueUpdate(GLuint ubo, long uboOffset)
{
	if (!s_ff.pending)
		return;
	s_ff.pending = false;

	foliageFieldCommand_t *cmd = (foliageFieldCommand_t *)R_GetCommandBuffer(sizeof(*cmd));
	if (!cmd)
		return;
	cmd->commandId = RC_FOLIAGE_FIELD;
	cmd->ubo = ubo;
	cmd->uboOffset = uboOffset;
	cmd->source = s_ff.source;
	cmd->target = s_ff.target;
	cmd->current = s_ff.current;
	cmd->previous = s_ff.previous;
	cmd->clear = s_ff.target >= 0 && s_ff.clear ? qtrue : qfalse;
}

static void RB_FoliageFieldClearImage(int index)
{
	GLfloat clearColor[4];
	qglGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
	FBO_Bind(s_ff.fbo[index]);
	GL_SetViewportAndScissor(0, 0, s_ff.size, s_ff.size);
	qglClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	qglClear(GL_COLOR_BUFFER_BIT);
	qglClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
}

const void *RB_FoliageFieldCommand(const void *data)
{
	const foliageFieldCommand_t *cmd = (const foliageFieldCommand_t *)data;

	if (tess.numIndexes)
		RB_EndSurface();

	if (s_ff.resources && cmd->target >= 0)
	{
		const int timer = RB_ScreenBeginTimer("Foliage field");
		FBO_t *oldFbo = glState.currentFBO;

		GL_State(GLS_DEPTHTEST_DISABLE);
		if (cmd->clear)
		{
			RB_FoliageFieldClearImage(0);
			RB_FoliageFieldClearImage(1);
		}

		shaderProgram_t *sp = &tr.foliageFieldShader;
		FBO_Bind(s_ff.fbo[cmd->target]);
		GL_SetViewportAndScissor(0, 0, s_ff.size, s_ff.size);
		GL_Cull(CT_TWO_SIDED);
		GLSL_BindProgram(sp);
		RB_BindUniformBlock(cmd->ubo, UNIFORM_BLOCK_FOLIAGE_INTERACTION, cmd->uboOffset);
		// nothing samples the target while it is written
		GL_BindToTMU(s_ff.image[cmd->source], TB_FOLIAGEFIELD);
		GL_BindToTMU(s_ff.image[cmd->source], TB_FOLIAGEFIELD_PREV);
		RB_InstantTriangle();

		FBO_Bind(oldFbo);
		RB_ScreenEndTimer(timer);
	}

	// the draws of every view of this frame sample these
	if (s_ff.resources)
	{
		GL_BindToTMU(s_ff.image[cmd->current], TB_FOLIAGEFIELD);
		GL_BindToTMU(s_ff.image[cmd->previous], TB_FOLIAGEFIELD_PREV);
	}
	GL_SelectTexture(0);

	return (const void *)(cmd + 1);
}

/*
=============
RB_FoliageFieldDebugOverlay

r_foliageFieldDebug 1: bend vectors (left) and magnitude heat (right) of the
current field in the lower left corner, the player at the center
=============
*/
void RB_FoliageFieldDebugOverlay(void)
{
	if (!(FoliageFieldDebug() & FOLIAGEFIELD_DEBUG_OVERLAY) || !s_ff.frameActive)
		return;
	if (backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE))
		return;

	shaderProgram_t *sp = &tr.foliageFieldDebugShader;
	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(sp);
	GL_BindToTMU(s_ff.image[s_ff.current], TB_FOLIAGEFIELD);

	const float square = (float)MIN(256, glConfig.vidHeight / 3);
	vec4_t rect;
	VectorSet4(rect, 16.0f, 16.0f, square, 1.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_FOLIAGEFIELDDEBUG, rect);
	RB_InstantTriangle();
	GL_SelectTexture(0);
}

// r_foliageFieldDebug 2: the covered square (world XY), false when none
bool R_FoliageFieldBounds(vec2_t mins, vec2_t maxs)
{
	if (!s_ff.frameActive || !(FoliageFieldDebug() & FOLIAGEFIELD_DEBUG_BOUNDS))
		return false;
	const float texel = s_ff.extent / s_ff.size;
	const float half = s_ff.extent * 0.5f;
	mins[0] = s_ff.originX * texel - half;
	mins[1] = s_ff.originY * texel - half;
	maxs[0] = mins[0] + s_ff.extent;
	maxs[1] = mins[1] + s_ff.extent;
	return true;
}

void R_FoliageFieldInfo(void)
{
	const float texel = s_ff.size ? s_ff.extent / s_ff.size : 0.0f;
	ri.Printf(PRINT_ALL, "foliage field: %s, %dx%d RGBA16F (%d KB), extent %.0f (%.1f units / texel), "
		"center (%.0f %.0f), state %d, previous %d, last dt %.3f\n",
		R_FoliageFieldActive() ? (s_ff.frameActive ? "on" : "on, idle") : "off",
		s_ff.size, s_ff.size, s_ff.size * s_ff.size * 8 * 2 / 1024, s_ff.extent, texel,
		s_ff.originX * texel, s_ff.originY * texel, s_ff.current, s_ff.previous, s_ff.dt);
}
