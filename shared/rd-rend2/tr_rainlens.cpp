/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

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

// Rain droplets on the camera lens (r_rainLens), glsl/rainlens.glsl.
//
// Optional polish, off by default. One fullscreen HDR pass in RB_PostProcess
// after the SMAA T2x temporal resolve and motion blur, before bloom
// extraction and tone mapping (see docs/rend2-rain-lens.md). It reads the
// resolved scene and writes rainLensImage (a dedicated full resolution HDR
// target: textureScratchImage is 256x256 RGBA8), then copies it back.
//
// Active only for the main world view of the first scene while it rains
// and the camera is outside (R_IsOutside, decided on the front end). The
// drops themselves are procedural in the shader; the only backend state is
// the lens clock, the last time the camera saw the rain and a short ramp up.

#include "tr_local.h"
#include "tr_weather.h"

// drops already on the lens run off within this time under cover
#define RAIN_LENS_DRAIN_SECONDS 1.2f
// new drops ramp up over this time after stepping out
#define RAIN_LENS_RAMP_SECONDS 1.5f

static float s_lensTime;
static float s_lastExposedTime;
static float s_wet;
static int s_lastRefdefTime;
static qboolean s_clockValid;
static qboolean s_debugOutput;
static unsigned s_lastFrame;

static void R_RainLensResetState( void )
{
	s_lensTime = 0.0f;
	s_lastExposedTime = -1000.0f;
	s_wet = 0.0f;
	s_lastRefdefTime = 0;
	s_clockValid = qfalse;
	s_debugOutput = qfalse;
	s_lastFrame = 0;
}

void R_CreateRainLensImages( int width, int height, int hdrFormat )
{
	R_RainLensResetState();
	tr.rainLensImage = NULL;

	if ( !r_rainLens->integer )
		return;

	if ( !r_hdr->integer )
	{
		ri.Printf(PRINT_WARNING, "r_rainLens needs r_hdr 1, lens rain disabled\n");
		return;
	}

	tr.rainLensImage = R_CreateImage(
		"*rainLens", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);
}

/*
=============
R_RainLensExposure

Front end, R_AddPostProcessCmd. Returns the rain intensity (0..1) the lens
is exposed to: zero unless it rains, the scene is the main world view and
the camera is outside. r_rainLensDebug forces it on for inspection.
=============
*/
float R_RainLensExposure( const trRefdef_t *refdef, const viewParms_t *viewParms )
{
	if ( !r_rainLens->integer || !tr.rainLensImage || !tr.world )
		return 0.0f;

	if ( refdef->rdflags & RDF_NOWORLDMODEL )
		return 0.0f;

	if ( viewParms->viewParmType != VPT_MAIN || viewParms->isPortal || viewParms->isMirror
		|| (viewParms->flags & VPF_DEPTHSHADOW) )
		return 0.0f;

	if ( r_rainLensDebug->integer )
		return 1.0f;

	if ( !tr.weatherSystem )
		return 0.0f;

	const weatherObject_t *rain = &tr.weatherSystem->weatherSlots[WEATHER_RAIN];
	if ( !rain->active )
		return 0.0f;

	vec3_t origin;
	VectorCopy(refdef->vieworg, origin);
	if ( !R_IsOutside(origin) )
		return 0.0f;

	// "rain" 1000, "heavyrain" 2000, "heavyrainfog" 5000 particles
	return Com_Clamp(0.25f, 1.0f, rain->particleCount / 5000.0f);
}

static int RB_RainLensBeginTimer( const char *name )
{
	if ( !glRefConfig.timerQuery || r_speeds->integer != 100 )
		return -1;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	if ( tr.numTimedBlocks >= (MAX_GPU_TIMERS / 2) || frame->numTimers + 2 > MAX_GPU_TIMERS )
		return -1;

	const int handle = tr.numTimedBlocks++;
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	gpuTimedBlock_t *timedBlock = frame->timedBlocks + handle;
	timedBlock->beginTimer = timer->queryName;
	timedBlock->name = name;
	frame->numTimedBlocks++;

	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
	return handle;
}

static void RB_RainLensEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

/*
=============
RB_RainLensUpdate

Advances the lens clock of the main scene and tells whether the pass runs
this frame: while exposed, and until the drops on the lens have drained.
=============
*/
qboolean RB_RainLensUpdate( float exposure )
{
	s_debugOutput = qfalse;

	if ( !tr.rainLensImage || !tr.rainLensFbo )
		return qfalse;

	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		return qfalse;

	// the first post processed world scene of a frame owns the lens
	if ( s_clockValid && s_lastFrame == backEndData->realFrameNumber )
		return qfalse;
	s_lastFrame = backEndData->realFrameNumber;

	// game time: pauses with the game, restarts on map change
	const int now = backEnd.refdef.time;
	const int delta = now - s_lastRefdefTime;
	if ( !s_clockValid || delta < 0 || delta > 1000 )
	{
		// cut: no drops carried over
		s_lastExposedTime = s_lensTime - 1000.0f;
		s_wet = 0.0f;
	}
	else
	{
		s_lensTime += delta * 0.001f;
	}
	s_lastRefdefTime = now;
	s_clockValid = qtrue;

	// keep the float clock precise
	if ( s_lensTime > 4096.0f )
	{
		s_lensTime -= 2048.0f;
		s_lastExposedTime -= 2048.0f;
	}

	const float dt = (delta > 0 && delta <= 1000) ? delta * 0.001f : 0.0f;
	if ( exposure > 0.0f )
	{
		s_lastExposedTime = s_lensTime;
		s_wet = Q_min(1.0f, s_wet + dt / RAIN_LENS_RAMP_SECONDS);
	}

	if ( r_rainLensDebug->integer )
		return qtrue;

	if ( r_rainLensAmount->value <= 0.0f )
		return qfalse;

	return (qboolean)(s_lensTime - s_lastExposedTime < RAIN_LENS_DRAIN_SECONDS);
}

/*
=============
RB_RainLens

srcFbo holds the HDR scene (MSAA resolved, temporally resolved and motion
blurred). Refracts it through the lens drops into rainLensImage and copies
the result back, so bloom, SMAA 1 and the tone map see the drops. Debug
views stay in rainLensImage and are drawn by RB_RainLensDebugOverlay.
=============
*/
void RB_RainLens( FBO_t *srcFbo, float exposure )
{
	if ( !srcFbo )
		return;

	const int debugView = r_rainLensDebug->integer;
	shaderProgram_t *sp = &tr.rainLensShader[debugView ? RAINLENSDEF_DEBUG : RAINLENSDEF_DEFAULT];

	// exposure is zero while draining: keep the last density
	static float s_intensity = 1.0f;
	if ( exposure > 0.0f )
		s_intensity = exposure;

	const float density = debugView ? 1.0f :
		Com_Clamp(0.0f, 1.0f, r_rainLensAmount->value) * s_intensity * s_wet;

	const int timer = RB_RainLensBeginTimer("Rain lens");

	FBO_Bind(tr.rainLensFbo);
	GL_SetViewportAndScissor(0, 0, tr.rainLensFbo->width, tr.rainLensFbo->height);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	GLSL_BindProgram(sp);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);

	vec4_t params, params2;
	VectorSet4(params, s_lensTime, density,
		Com_Clamp(0.0f, 4.0f, r_rainLensRefraction->value),
		Com_Clamp(0.25f, 4.0f, r_rainLensScale->value));
	VectorSet4(params2, s_lastExposedTime, RAIN_LENS_DRAIN_SECONDS,
		(float)debugView, tr.linearLight ? 0.0f : 1.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_RAINLENSPARAMS, params);
	GLSL_SetUniformVec4(sp, UNIFORM_RAINLENSPARAMS2, params2);

	RB_InstantTriangle();

	if ( debugView )
		s_debugOutput = qtrue;
	else
		// only the scene color attachment, srcFbo also has the glow target
		FBO_FastBlitIndexed(tr.rainLensFbo, srcFbo, 0, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	RB_RainLensEndTimer(timer);
}

// r_rainLensDebug views, drawn at the end of the post process chain
void RB_RainLensDebugOverlay( void )
{
	if ( !s_debugOutput || !r_rainLensDebug->integer )
		return;

	vec4i_t dstBox;
	VectorSet4(dstBox, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
	FBO_FastBlitFromTexture(tr.rainLensImage, NULL, dstBox, NULL, 0);
}
