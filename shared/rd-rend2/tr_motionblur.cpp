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

// Velocity based camera and object motion blur (r_motionBlur).
//
// Uses the temporal data of SMAA T2x: the velocity buffer written by the
// depth prepass (velocity.glsl) with the previous view projection, previous
// entity matrices and previous bones. The blur runs on the HDR scene in
// RB_PostProcess, after the SMAA T2x temporal resolve (its history stays
// sharp) and before bloom, tone mapping and the UI (glsl/motionblur.glsl).
//
// Shutter: the exposure time is r_motionBlurShutterAngle / 360 of a frame
// at r_motionBlurReferenceFps, so the blur length only depends on the screen
// speed, not on the frame rate. The velocity buffer holds the motion over
// the real frame interval, the pass scales it by exposure / frame interval.
//
// History: the previous frame data is ignored (no blur, no object motion,
// the previous view projection is the current one) on the first frame after
// a map load, menus, a camera cut or teleport, a large FOV change or when
// game code sets r_motionBlurReset, see RB_MotionBlurUpdateHistory.

#include "tr_local.h"

#include <chrono>

// Resources are decided when the images are created (r_motionBlur is latched)
static qboolean s_mbResources = qfalse;

// smoothed real time between the last two main views, seconds
static double s_frameInterval = 1.0 / 60.0;
static qboolean s_frameIntervalValid = qfalse;

static const char *s_lastResetReason = NULL;

// the output of the last pass is a r_motionBlurDebug view
static qboolean s_debugOutput = qfalse;

// max samples per pixel of r_motionBlurQuality 0, 1, 2
static const int motionBlurQualitySamples[] = { 6, 10, 16 };

qboolean R_MotionBlurEnabled( void )
{
	return s_mbResources;
}

void R_CreateMotionBlurImages( int width, int height, int hdrFormat )
{
	s_mbResources = qfalse;
	s_debugOutput = qfalse;
	s_frameIntervalValid = qfalse;
	s_lastResetReason = NULL;
	tr.motionBlurImage = NULL;
	tr.temporalHistoryValid = qtrue;

	if ( !r_motionBlur->integer )
		return;

	if ( !r_hdr->integer )
	{
		// without r_hdr the scene buffer is the display referred LDR image
		ri.Printf(PRINT_WARNING, "r_motionBlur needs r_hdr 1, motion blur disabled\n");
		return;
	}

	s_mbResources = qtrue;
	tr.motionBlurImage = R_CreateImage(
		"*motionBlur", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);
}

/*
============================================================

Temporal history

============================================================
*/

static double RB_RealTime( void )
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/*
=============
RB_MotionBlurUpdateHistory

Called from RB_UpdateTemporalConstants for every scene, before the entity
and bone constants look up their previous frame data. The first scene of a
frame records its main view and decides tr.temporalHistoryValid for the
whole frame.
=============
*/
void RB_MotionBlurUpdateHistory( gpuFrame_t *frame, const gpuFrame_t *previousFrame, const trRefdef_t *refdef )
{
	if ( !s_mbResources )
	{
		tr.temporalHistoryValid = qtrue;
		return;
	}

	if ( frame->currentScene != 0 )
		return;

	const qboolean worldView = (qboolean)(tr.world && !(refdef->rdflags & RDF_NOWORLDMODEL));
	frame->hasMainView = worldView;
	VectorCopy(refdef->vieworg, frame->viewOrigin);
	VectorCopy(refdef->viewaxis[0], frame->viewForward);
	frame->fovX = refdef->fov_x;
	frame->realTime = RB_RealTime();
	frame->world = tr.world;

	const char *reason = NULL;
	double dt = 0.0;
	if ( !worldView )
		reason = "no world view";
	else if ( !previousFrame || previousFrame == frame || !previousFrame->hasMainView )
		reason = "no previous frame";
	else if ( previousFrame->world != tr.world )
		reason = "map change";
	else if ( r_motionBlurReset->integer )
		reason = "r_motionBlurReset";
	else
	{
		dt = frame->realTime - previousFrame->realTime;
		const float cosCutAngle = cosf(DEG2RAD(r_motionBlurCutAngle->value));

		if ( dt <= 0.0 || dt > 0.25 )
			reason = "frame time";
		else if ( Distance(frame->viewOrigin, previousFrame->viewOrigin) > r_motionBlurCutDistance->value )
			reason = "camera teleport";
		else if ( DotProduct(frame->viewForward, previousFrame->viewForward) < cosCutAngle )
			reason = "camera cut";
		else if ( fabsf(frame->fovX - previousFrame->fovX) > 0.15f * previousFrame->fovX )
			reason = "fov change";
	}

	if ( r_motionBlurReset->integer )
		ri.Cvar_Set("r_motionBlurReset", "0");

	tr.temporalHistoryValid = (qboolean)(reason == NULL);

	if ( tr.temporalHistoryValid )
	{
		// integer millisecond timers and frame pacing jitter would make the
		// blur length flicker, smooth the interval a little
		s_frameInterval = s_frameIntervalValid ? (0.75 * s_frameInterval + 0.25 * dt) : dt;
		s_frameIntervalValid = qtrue;
	}
	else
	{
		s_frameIntervalValid = qfalse;
	}

	if ( r_motionBlurDebug->integer && reason && reason != s_lastResetReason )
		ri.Printf(PRINT_ALL, "motion blur: history reset (%s)\n", reason);
	s_lastResetReason = reason;
}

/*
============================================================

Pass

============================================================
*/

static int RB_MotionBlurBeginTimer( const char *name )
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

static void RB_MotionBlurEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

// exposure time / frame interval: scales the one frame velocity to the
// motion during the exposure
static float RB_MotionBlurExposureScale( void )
{
	const float shutter = r_motionBlurShutterAngle->value / 360.0f;
	const float referenceFps = r_motionBlurReferenceFps->value;

	float scale = shutter;
	if ( referenceFps > 0.0f && s_frameInterval > 0.0 )
		scale = (float)(shutter / (referenceFps * s_frameInterval));

	scale *= r_motionBlurShutterScale->value;
	return Com_Clamp(0.0f, 4.0f, scale);
}

// general 4x4 inverse (column major), false when singular
static qboolean RB_InvertMatrix( const matrix_t in, matrix_t out )
{
	double m[16], inv[16];
	for ( int i = 0; i < 16; i++ )
		m[i] = in[i];

	inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
	inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
	inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
	inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
	inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
	inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

	const double det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if ( fabs(det) < 1e-30 )
		return qfalse;

	for ( int i = 0; i < 16; i++ )
		out[i] = (float)(inv[i] / det);
	return qtrue;
}

/*
=============
RB_MotionBlurActive

Whether RB_MotionBlur runs for the scene being post processed: only the
first scene of a frame, which owns the temporal data, and only with a valid
history (the first frame after a cut is never blurred). Debug views always
run.
=============
*/
qboolean RB_MotionBlurActive( void )
{
	if ( !s_mbResources || !tr.motionBlurFbo || !backEndData->currentFrame )
		return qfalse;

	if ( backEndData->currentFrame->currentScene != 0 || !backEndData->currentFrame->hasMainView )
		return qfalse;

	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		return qfalse;

	if ( r_motionBlurDebug->integer )
		return qtrue;

	return (qboolean)(tr.temporalHistoryValid && RB_MotionBlurExposureScale() > 0.0f);
}

/*
=============
RB_MotionBlur

srcFbo holds the HDR scene (renderImage, MSAA already resolved) and its
depth. Blurs into motionBlurImage; the caller chains that color target to
later passes. Returns false if the pass could not render. Debug views stay
in motionBlurImage and are drawn by RB_MotionBlurDebugOverlay.
=============
*/
qboolean RB_MotionBlur( FBO_t *srcFbo )
{
	s_debugOutput = qfalse;

	if ( !srcFbo || !RB_MotionBlurActive() )
		return qfalse;

	const gpuFrame_t *frame = backEndData->currentFrame;
	const gpuFrame_t *previousFrame = backEndData->previousFrame;

	matrix_t invViewProjection;
	if ( !RB_InvertMatrix(frame->viewProjectionMatrix, invViewProjection) )
		return qfalse;

	// the same previous view projection as the velocity pass
	const float *prevViewProjection = frame->viewProjectionMatrix;
	if ( tr.temporalHistoryValid && previousFrame )
		prevViewProjection = previousFrame->viewProjectionMatrix;

	const int debugView = r_motionBlurDebug->integer;
	const int quality = Com_Clampi(0, 2, r_motionBlurQuality->integer);
	int maxSamples = r_motionBlurSamples->integer > 0 ?
		r_motionBlurSamples->integer : motionBlurQualitySamples[quality];
	maxSamples = Com_Clampi(2, 32, maxSamples);

	const float maxLength = r_motionBlurMaxPixels->value * glConfig.vidHeight / 1080.0f;
	const float exposureScale = tr.temporalHistoryValid ? RB_MotionBlurExposureScale() : 0.0f;

	// velocity is only written by the depth prepass; without it the camera
	// motion is reconstructed from depth everywhere
	const qboolean velocityValid = (qboolean)(
		tr.velocityImage && tr.depthVelocityFbo && r_depthPrepass->integer);

	shaderProgram_t *sp;
	if ( debugView )
		sp = &tr.motionBlurShader[MOTIONBLURDEF_DEBUG];
	else if ( quality == 0 )
		sp = &tr.motionBlurShader[MOTIONBLURDEF_LOW];
	else
		sp = &tr.motionBlurShader[MOTIONBLURDEF_DEFAULT];

	const int timer = RB_MotionBlurBeginTimer("Motion blur");

	FBO_Bind(tr.motionBlurFbo);
	GL_SetViewportAndScissor(0, 0, tr.motionBlurFbo->width, tr.motionBlurFbo->height);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	GLSL_BindProgram(sp);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GL_BindToTMU(velocityValid ? tr.velocityImage : tr.whiteImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.renderDepthImage, TB_NORMALMAP);

	const float *proj = backEnd.viewParms.projectionMatrix;
	vec4_t params, params2, params3;
	VectorSet4(params, exposureScale, maxLength, (float)maxSamples, velocityValid ? 1.0f : 0.0f);
	VectorSet4(params2,
		Com_Clamp(0.0f, 1.0f, r_motionBlurViewModelScale->value),
		proj[14], proj[10],
		tr.linearLight ? 0.0f : 1.0f);
	VectorSet4(params3, (float)debugView, (float)quality, 0.0f, 0.0f);

	GLSL_SetUniformMatrix4x4(sp, UNIFORM_MBINVVIEWPROJECTION, invViewProjection);
	GLSL_SetUniformMatrix4x4(sp, UNIFORM_MBPREVVIEWPROJECTION, prevViewProjection);
	GLSL_SetUniformVec4(sp, UNIFORM_MBPARAMS, params);
	GLSL_SetUniformVec4(sp, UNIFORM_MBPARAMS2, params2);
	GLSL_SetUniformVec4(sp, UNIFORM_MBPARAMS3, params3);

	RB_InstantTriangle();

	if ( debugView )
		s_debugOutput = qtrue;

	RB_MotionBlurEndTimer(timer);
	return qtrue;
}

// r_motionBlurDebug views, drawn at the end of the post process chain
void RB_MotionBlurDebugOverlay( void )
{
	if ( !s_debugOutput || !r_motionBlurDebug->integer )
		return;

	vec4i_t dstBox;
	VectorSet4(dstBox, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
	FBO_FastBlitFromTexture(tr.motionBlurImage, NULL, dstBox, NULL, 0);
}
