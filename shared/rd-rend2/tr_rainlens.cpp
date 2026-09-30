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
// Optional polish, off by default. A transient lower resolution lens field
// and one fullscreen HDR composite in RB_PostProcess
// after the SMAA T2x temporal resolve and motion blur, before bloom
// extraction and tone mapping (see docs/rend2-rain-lens.md). It reads the
// resolved scene and writes rainLensImage (a dedicated full resolution HDR
// target: textureScratchImage is 256x256 RGBA8). The caller uses that target
// as the scene source for the remaining color passes; modern bloom also reads
// the lens field to refract the emissive MRT.
//
// Active only for the main world view of the first scene while it rains
// and the camera is outside (R_IsOutside, decided on the front end). The
// default drops are procedural; r_rainLensSimulation keeps water on a small
// CPU lattice and uploads it to the same lens-field/optics path.

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
	tr.rainLensFieldImage = NULL;
	tr.rainLensSimImage = NULL;

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
	// Keep small beads several field pixels wide at ordinary drop sizes.
	const int fieldHeight = Q_min(height, Q_max(540, height / 2));
	const int fieldWidth = Q_max(1, width * fieldHeight / height);
	tr.rainLensFieldImage = R_CreateImage(
		"*rainLensField", NULL, fieldWidth, fieldHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
	if (r_rainLensSimulation->integer)
	{
		const int simHeight = 256;
		const int simWidth = Q_max(1, width * simHeight / height);
		tr.rainLensSimImage = R_CreateImage(
			"*rainLensSim", NULL, simWidth, simHeight, IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
		R_RainLensSimInit(simWidth, simHeight);
	}
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

	if ( !tr.rainLensImage || !tr.rainLensFbo || !tr.rainLensFieldImage || !tr.rainLensFieldFbo
		|| (r_rainLensSimulation->integer && !tr.rainLensSimImage) )
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
		if (r_rainLensSimulation->integer)
			R_RainLensSimClear();
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
	if (r_rainLensSimulation->integer)
	{
		const qboolean active = RB_RainLensSimUpdate(dt, exposure);
		return (qboolean)(active || r_rainLensDebug->integer);
	}
	if ( exposure > 0.0f )
	{
		s_lastExposedTime = s_lensTime;
		s_wet = Q_min(1.0f, s_wet + dt / RAIN_LENS_RAMP_SECONDS);
	}
	else if ( s_lensTime - s_lastExposedTime >= RAIN_LENS_DRAIN_SECONDS )
	{
		// Keep the existing population stable while it drains. Once the lens
		// is clear, new rain has to build up again on the next exposure.
		s_wet = 0.0f;
	}

	if ( r_rainLensDebug->integer )
		return qtrue;

	if ( r_rainLensDensity->value <= 0.0f )
		return qfalse;

	return (qboolean)(s_lensTime - s_lastExposedTime < RAIN_LENS_DRAIN_SECONDS);
}

/*
=============
RB_RainLens

srcFbo holds the HDR scene (MSAA resolved, temporally resolved and motion
blurred). Evaluates droplet geometry into a lower resolution field,
then refracts the HDR scene into rainLensImage. The caller uses rainLensFbo
for subsequent color passes. Procedural debug views use the original direct
shader; simulation debug views use the field composite. Both are drawn by
RB_RainLensDebugOverlay.
=============
*/
void RB_RainLens( FBO_t *srcFbo, float exposure )
{
	if ( !srcFbo )
		return;

	const int debugView = r_rainLensDebug->integer;

	// exposure is zero while draining: keep the last density
	static float s_intensity = 1.0f;
	if ( exposure > 0.0f )
		s_intensity = exposure;

	const float density = debugView ? 1.0f :
		Com_Clamp(0.0f, 1.0f, r_rainLensDensity->value) * s_intensity * s_wet;

	const int timer = RB_RainLensBeginTimer("Rain lens");

	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	vec4_t params, params2;
	VectorSet4(params, s_lensTime, density,
		Com_Clamp(0.0f, 4.0f, r_rainLensRefraction->value),
		Com_Clamp(0.25f, 4.0f, r_rainLensDropSize->value));
	VectorSet4(params2, s_lastExposedTime, RAIN_LENS_DRAIN_SECONDS,
		(float)debugView, tr.linearLight ? 0.0f : 1.0f);
	if ( debugView && !r_rainLensSimulation->integer )
	{
		FBO_Bind(tr.rainLensFbo);
		GL_SetViewportAndScissor(0, 0, tr.rainLensFbo->width, tr.rainLensFbo->height);
		shaderProgram_t *sp = &tr.rainLensShader[RAINLENSDEF_DEBUG];
		GLSL_BindProgram(sp);
		GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
		GLSL_SetUniformVec4(sp, UNIFORM_RAINLENSPARAMS, params);
		GLSL_SetUniformVec4(sp, UNIFORM_RAINLENSPARAMS2, params2);
		RB_InstantTriangle();
		s_debugOutput = qtrue;
	}
	else
	{
		const qboolean simulation = (qboolean)r_rainLensSimulation->integer;
		const qboolean updateField = (qboolean)(!simulation || RB_RainLensSimUpload());
		if (updateField)
		{
			FBO_Bind(tr.rainLensFieldFbo);
			GL_SetViewportAndScissor(0, 0, tr.rainLensFieldFbo->width, tr.rainLensFieldFbo->height);
			shaderProgram_t *field = &tr.rainLensShader[
				simulation ? RAINLENSDEF_SIMULATION : RAINLENSDEF_FIELD];
			GLSL_BindProgram(field);
			if (simulation)
				GL_BindToTMU(tr.rainLensSimImage, TB_COLORMAP);
			VectorSet4(params2, s_lastExposedTime, RAIN_LENS_DRAIN_SECONDS,
				(float)tr.rainLensFieldFbo->width, (float)tr.rainLensFieldFbo->height);
			GLSL_SetUniformVec4(field, UNIFORM_RAINLENSPARAMS, params);
			GLSL_SetUniformVec4(field, UNIFORM_RAINLENSPARAMS2, params2);
			RB_InstantTriangle();
		}

		FBO_Bind(tr.rainLensFbo);
		GL_SetViewportAndScissor(0, 0, tr.rainLensFbo->width, tr.rainLensFbo->height);
		shaderProgram_t *composite = &tr.rainLensCompositeShader;
		GLSL_BindProgram(composite);
		GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
		GL_BindToTMU(tr.rainLensFieldImage, TB_LIGHTMAP);
		GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS, params);
		VectorSet4(params2, s_lastExposedTime, RAIN_LENS_DRAIN_SECONDS,
			(float)debugView, tr.linearLight ? 0.0f : 1.0f);
		GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS2, params2);
		RB_InstantTriangle();
		if (debugView)
			s_debugOutput = qtrue;
	}

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
