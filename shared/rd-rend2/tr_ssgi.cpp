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

// Screen-space diffuse global illumination (r_ssgi), dynamic first.
//
// Most static light of a Jedi Academy map is baked into its lightmaps, which
// already contain the bounced light. So by default the GI does not bounce the
// whole scene again: it bounces the *dynamic* outgoing radiance (dynamic
// lights: sabers, blaster bolts, explosions; with r_forwardPlus all clustered
// lights) plus physical emission. lightall writes that source, linear HDR, as
// a MRT output of the opaque pass (no second scene pass), both in the legacy
// and the Forward+ light loop (they share EvaluateDynamicLight):
//
//   5 ssgiAlbedoImage    RGBA8    rgb = sRGB diffuse albedo, a = receiver
//   6 ssgiRadianceImage  RGBA16F  rgb = source radiance, a = view depth
//   2 screenNormalImage  (shared with SSR, tr_screenspace.cpp)
//
//   source radiance = diffuse lobe of the dynamic lights (after shadows and
//                     receiver visibility), r_ssgiSource 0
//                   + explicit emission * r_ssgiEmissiveScale, sources 0 and 1
//                   + legacy glow / auto emissive stage color * r_ssgiGlowScale
//                     (default 0: no physical intensity)
//   r_ssgiSource 2: the whole opaque scene (experimental: baked light is
//                   bounced a second time)
//
// Passes, between the opaque sort and the rest of the main pass (after the
// shared depth pyramid, see RB_RenderScreenSpaceOpaque):
//
//   source    half resolution copy of the source (+ mips)       ssgiSource
//   trace     r_ssgiRays cosine weighted rays per receiver, shared ray march
//             (SSRMarchRay, linear or Hi-Z), half or full res   ssgiTrace/Hit
//   temporal  reprojected, validated, neighborhood clamped      ssgiHistory
//   denoise   edge-aware a-trous (depth plane, normal, luma)    ssgiDenoise
//   composite depth/normal-aware upsample, * albedo, added to color 0
//
// Neither r_ssr nor r_forwardPlus is required. r_ssgi 0 creates nothing and
// keeps lightall, renderFbo and the pass unchanged. See docs/rend2-ssgi.md.

#include "tr_local.h"

// Resources (and USE_SSGI in the GLSL header) are decided when the renderer
// builds its GPU shaders, see R_CreateScreenSpaceImages
static qboolean s_ssgiResources = qfalse;
static int s_gridScale = 2;		// trace resolution: full resolution / s_gridScale
static screenHistory_t s_history;

struct ssgiQualityPreset_t
{
	const char *name;
	int halfRes;
	int rays;
	int steps;		// linear march steps, Hi-Z: iterations / 3
	int refineSteps;
	int hiZ;
	int denoise;	// a-trous passes
};

// presets change the cost only, never the intensity
static const ssgiQualityPreset_t ssgiQualityPresets[] =
{
	{ "low",    1, 1,  8, 3, 0, 1 },
	{ "medium", 1, 1, 12, 4, 1, 2 },
	{ "high",   1, 2, 16, 5, 1, 2 },
	{ "ultra",  0, 2, 24, 6, 1, 3 },
};

#define SSGI_MAX_RAYS		8	// ssgi_trace.glsl
#define SSGI_MAX_DENOISE	4
#define SSGI_EDGE_FADE		0.05f

// lightall source bits (u_SSGIParams.x)
#define SSGI_SOURCE_DYNAMIC		1
#define SSGI_SOURCE_EMISSIVE	2
#define SSGI_SOURCE_GLOW		4

// r_ssgiDebug views
enum
{
	SSGI_DEBUG_HITMASK = 1,
	SSGI_DEBUG_HITDISTANCE,
	SSGI_DEBUG_RAW,
	SSGI_DEBUG_TEMPORAL,
	SSGI_DEBUG_HISTORYWEIGHT,
	SSGI_DEBUG_DENOISED,
	SSGI_DEBUG_DYNAMICSOURCE,
	SSGI_DEBUG_EMISSIVESOURCE,
	SSGI_DEBUG_INDIRECT,
	SSGI_DEBUG_ALBEDO,
};

// composite modes, ssgi_composite.glsl
enum
{
	SSGI_COMPOSITE_ADD = 0,
	SSGI_COMPOSITE_SHOW_GI,
	SSGI_COMPOSITE_SHOW_SOURCE,
	SSGI_COMPOSITE_SHOW_INDIRECT,
};

// state of the last traced view, for the r_ssgiDebug overlays
static struct
{
	unsigned frameNumber;
	image_t *historyGeom;
	qboolean valid;
} s_debug;

static const ssgiQualityPreset_t& R_SSGIQuality( void )
{
	return ssgiQualityPresets[Com_Clampi(0, ARRAY_LEN(ssgiQualityPresets) - 1, r_ssgiQuality->integer)];
}

qboolean R_SSGIResourcesEnabled( void )
{
	return s_ssgiResources;
}

// temporal accumulation is a runtime switch: keep the velocity buffer
qboolean R_SSGIWantsVelocity( void )
{
	return s_ssgiResources;
}

/*
============================================================

Resources

============================================================
*/

// called by R_CreateScreenSpaceImages when the GPU shaders are (re)built
void R_SSGISelectResources( void )
{
	s_ssgiResources = (qboolean)(r_ssgi->integer != 0);

	// the trace resolution is a resource size: decided here, like r_ssgi
	const int halfRes = r_ssgiHalfRes->integer >= 0 ? r_ssgiHalfRes->integer : R_SSGIQuality().halfRes;
	s_gridScale = halfRes ? 2 : 1;
}

void R_CreateSSGIImages( int width, int height, int hdrFormat )
{
	Com_Memset(&s_history, 0, sizeof(s_history));
	Com_Memset(&s_debug, 0, sizeof(s_debug));

	tr.ssgiAlbedoImage = NULL;
	tr.ssgiRadianceImage = NULL;
	tr.ssgiSceneImage = NULL;
	tr.ssgiSourceImage = NULL;
	tr.ssgiTraceImage = NULL;
	tr.ssgiHitImage = NULL;
	for ( int i = 0; i < 2; i++ )
	{
		tr.ssgiHistoryImage[i] = NULL;
		tr.ssgiHistoryGeomImage[i] = NULL;
		tr.ssgiDenoiseImage[i] = NULL;
	}

	if ( !s_ssgiResources )
		return;

	// attachments of renderFbo (MSAA: resolve targets)
	tr.ssgiAlbedoImage = R_ScreenCreateImage("*ssgiAlbedo", width, height, GL_RGBA8, qfalse);
	tr.ssgiRadianceImage = R_ScreenCreateImage("*ssgiRadiance", width, height, GL_RGBA16F, qfalse);

	// same format as renderFbo color 0 (the MSAA resolve blit needs identical formats)
	tr.ssgiSceneImage = R_ScreenCreateImage("*ssgiScene", width, height, hdrFormat, qfalse);

	tr.ssgiSourceImage = R_ScreenCreateMipImage(
		"*ssgiSource", (width + 1) / 2, (height + 1) / 2, GL_RGBA16F,
		GL_RGBA, GL_HALF_FLOAT, SSGI_SOURCE_MIPS, qtrue);

	const int traceWidth = (width + s_gridScale - 1) / s_gridScale;
	const int traceHeight = (height + s_gridScale - 1) / s_gridScale;
	tr.ssgiTraceImage = R_ScreenCreateImage("*ssgiTrace", traceWidth, traceHeight, GL_RGBA16F, qfalse);
	tr.ssgiHitImage = R_ScreenCreateImage("*ssgiHit", traceWidth, traceHeight, GL_RG16F, qfalse);
	for ( int i = 0; i < 2; i++ )
	{
		tr.ssgiHistoryImage[i] = R_ScreenCreateImage(
			va("*ssgiHistory%d", i), traceWidth, traceHeight, GL_RGBA16F, qfalse);
		tr.ssgiHistoryGeomImage[i] = R_ScreenCreateImage(
			va("*ssgiHistoryGeom%d", i), traceWidth, traceHeight, GL_RGBA16F, qfalse);
		tr.ssgiDenoiseImage[i] = R_ScreenCreateImage(
			va("*ssgiDenoise%d", i), traceWidth, traceHeight, GL_RGBA16F, qfalse);
	}
}

void R_CreateSSGIFBOs( void )
{
	tr.ssgiSceneFbo = NULL;
	tr.ssgiSourceFbo = NULL;
	tr.ssgiTraceFbo = NULL;
	for ( int i = 0; i < 2; i++ )
	{
		tr.ssgiHistoryFbo[i] = NULL;
		tr.ssgiDenoiseFbo[i] = NULL;
	}

	if ( !s_ssgiResources )
		return;

	tr.ssgiSceneFbo = R_ScreenCreateLevelFBO("_ssgiScene", tr.ssgiSceneImage, 0);
	tr.ssgiSourceFbo = R_ScreenCreateLevelFBO("_ssgiSource", tr.ssgiSourceImage, 0);
	tr.ssgiTraceFbo = R_ScreenCreatePairFBO("_ssgiTrace", tr.ssgiTraceImage, tr.ssgiHitImage);
	for ( int i = 0; i < 2; i++ )
	{
		tr.ssgiHistoryFbo[i] = R_ScreenCreatePairFBO(
			va("_ssgiHistory%d", i), tr.ssgiHistoryImage[i], tr.ssgiHistoryGeomImage[i]);
		tr.ssgiDenoiseFbo[i] = R_ScreenCreateLevelFBO(va("_ssgiDenoise%d", i), tr.ssgiDenoiseImage[i], 0);
	}
}

/*
============================================================

Dependencies (printed once when they appear, nothing is enabled)

============================================================
*/

enum
{
	SSGI_NOTE_NO_DLIGHTS	= 1 << 0,
	SSGI_NOTE_NO_VELOCITY	= 1 << 1,
	SSGI_NOTE_FULL_SCENE	= 1 << 2,
	SSGI_NOTE_RESOLUTION	= 1 << 3,
	SSGI_NOTE_LATCHED		= 1 << 4,
};

// called once per frame (RE_BeginFrame)
void R_SSGICheckDependencies( void )
{
	static int s_shown = 0;

	int notes = 0;
	if ( s_ssgiResources )
	{
		const int source = Com_Clampi(0, 2, r_ssgiSource->integer);
		if ( source == 0 && !r_dynamiclight->integer )
			notes |= SSGI_NOTE_NO_DLIGHTS;
		if ( r_ssgiTemporal->integer && !r_depthPrepass->integer )
			notes |= SSGI_NOTE_NO_VELOCITY;
		if ( source == 2 )
			notes |= SSGI_NOTE_FULL_SCENE;

		const int halfRes = r_ssgiHalfRes->integer >= 0 ?
			r_ssgiHalfRes->integer : R_SSGIQuality().halfRes;
		if ( (halfRes ? 2 : 1) != s_gridScale )
			notes |= SSGI_NOTE_RESOLUTION;
	}
	else if ( r_ssgi->integer )
	{
		// r_ssgi 1 before the vid_restart that creates the resources
		notes |= SSGI_NOTE_LATCHED;
	}

	const int added = notes & ~s_shown;
	if ( added & SSGI_NOTE_NO_DLIGHTS )
		ri.Printf(PRINT_ALL, "SSGI: r_dynamiclight is 0, only emissive materials are bounced (r_ssgiSource 0)\n");
	if ( added & SSGI_NOTE_NO_VELOCITY )
		ri.Printf(PRINT_ALL, "SSGI: r_depthPrepass 0, no velocity buffer: moving objects are reprojected with the camera only\n");
	if ( added & SSGI_NOTE_FULL_SCENE )
		ri.Printf(PRINT_ALL, "SSGI: r_ssgiSource 2 is experimental, baked (lightmap) lighting is bounced a second time\n");
	if ( added & SSGI_NOTE_RESOLUTION )
		ri.Printf(PRINT_ALL, "SSGI: the trace resolution (r_ssgiHalfRes / r_ssgiQuality) changes at the next vid_restart\n");
	if ( added & SSGI_NOTE_LATCHED )
		ri.Printf(PRINT_ALL, "SSGI: r_ssgi takes effect after vid_restart\n");
	s_shown = notes;
}

/*
============================================================

View

============================================================
*/

// the view gets SSGI (called by RB_ScreenSpaceBeginView for views that can
// have screen-space passes)
qboolean RB_SSGIWantsView( void )
{
	if ( !s_ssgiResources )
		return qfalse;

	// zero intensity without debug views is the legacy look: skip the passes
	// (lightall still writes the attachments: isolates their cost)
	return (qboolean)(r_ssgiIntensity->value > 0.0f || r_ssgiDebug->integer || r_ssgiCompare->integer);
}

static qboolean RB_SSGIUseHiZ( void )
{
	return (qboolean)(r_ssgiHiZ->integer >= 0 ? r_ssgiHiZ->integer : R_SSGIQuality().hiZ);
}

int RB_SSGIDepthLevels( void )
{
	return RB_SSGIUseHiZ() ? SCREEN_HIZ_MIPS : 1;
}

// u_SSGIParams of lightall (SceneBlock): which radiance lightall writes
void RB_SSGISceneParams( vec4_t ssgiParams )
{
	VectorSet4(ssgiParams, 0.0f, 0.0f, 0.0f, 0.0f);
	if ( !s_ssgiResources )
		return;

	const int source = Com_Clampi(0, 2, r_ssgiSource->integer);
	int bits = 0;
	if ( source == 0 )
		bits = SSGI_SOURCE_DYNAMIC | SSGI_SOURCE_EMISSIVE;
	else if ( source == 1 )
		bits = SSGI_SOURCE_EMISSIVE;
	if ( source != 2 && r_ssgiGlowScale->value > 0.0f )
		bits |= SSGI_SOURCE_GLOW;

	// source debug views show one part alone, whatever the source
	if ( r_ssgiDebug->integer == SSGI_DEBUG_DYNAMICSOURCE )
		bits = SSGI_SOURCE_DYNAMIC;
	else if ( r_ssgiDebug->integer == SSGI_DEBUG_EMISSIVESOURCE )
		bits = SSGI_SOURCE_EMISSIVE;

	VectorSet4(ssgiParams,
		(float)bits,
		tr.linearLight ? 1.0f : 0.0f,
		Q_max(0.0f, r_ssgiEmissiveScale->value),
		Q_max(0.0f, r_ssgiGlowScale->value));
}

/*
============================================================

Passes

============================================================
*/

/*
=================
RB_RenderSSGI

Called by RB_RenderScreenSpaceOpaque between the opaque sort and the rest of
the main pass of a view, after the shared inputs (depth pyramid, MSAA resolve
of the attachments) and before the SSR.
=================
*/
void RB_RenderSSGI( const screenViewInfo_t& info )
{
	const viewParms_t& viewParms = backEnd.viewParms;

	R_PushDebugGroup(AL_STAGE, "SSGI");

	const ssgiQualityPreset_t& quality = R_SSGIQuality();
	const int rays = Com_Clampi(1, SSGI_MAX_RAYS, r_ssgiRays->integer > 0 ? r_ssgiRays->integer : quality.rays);
	const int steps = Com_Clampi(1, 256, r_ssgiSteps->integer > 0 ? r_ssgiSteps->integer : quality.steps);
	const int denoisePasses = Com_Clampi(0, SSGI_MAX_DENOISE,
		r_ssgiDenoise->integer >= 0 ? r_ssgiDenoise->integer : quality.denoise);
	const qboolean hiZ = RB_SSGIUseHiZ();
	const int source = Com_Clampi(0, 2, r_ssgiSource->integer);
	const int debugView = r_ssgiDebug->integer;
	const qboolean linearScene = tr.linearLight;
	const float grid = (float)s_gridScale;
	const float maxDistance = r_ssgiMaxDistance->value;

	const int width = tr.renderFbo->width;
	const int height = tr.renderFbo->height;
	const int traceWidth = tr.ssgiTraceImage->width;
	const int traceHeight = tr.ssgiTraceImage->height;
	const int sourceWidth = tr.ssgiSourceImage->width;
	const int sourceHeight = tr.ssgiSourceImage->height;

	// source radiance at the hit points
	int timer = RB_ScreenBeginTimer("SSGI source");
	const qboolean needScene = (qboolean)(!linearScene || source == 2);
	if ( needScene )
	{
		// MSAA resolves here. Blits are clipped by the scissor rectangle
		GL_SetViewportAndScissor(0, 0, width, height);
		FBO_FastBlit(tr.renderFbo, NULL, tr.ssgiSceneFbo, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	{
		shaderProgram_t *sp = &tr.ssgiSourceShader;
		RB_ScreenBeginPass(tr.ssgiSourceFbo, sp, sourceWidth, sourceHeight);
		RB_ScreenBindGeometry();
		GL_BindToTMU(needScene ? tr.ssgiSceneImage : tr.whiteImage, TB_SPECULARMAP);

		vec4_t settings;
		VectorSet4(settings, source == 2 ? 1.0f : 0.0f, linearScene ? 1.0f : 0.0f, 0.0f, 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		RB_InstantTriangle();

		GL_Bind(tr.ssgiSourceImage);
		qglGenerateMipmap(GL_TEXTURE_2D);
	}
	RB_ScreenEndTimer(timer);

	// rays
	timer = RB_ScreenBeginTimer("SSGI trace");
	{
		shaderProgram_t *sp = &tr.ssgiTraceShader[hiZ ? SSGIDEF_TRACE_HIZ : SSGIDEF_TRACE];
		RB_ScreenBeginPass(tr.ssgiTraceFbo, sp, traceWidth, traceHeight);
		RB_ScreenBindGeometry();
		GL_BindToTMU(tr.ssgiSourceImage, TB_SSGI_SOURCE);
		RB_ScreenSetViewUniforms(sp, info);

		vec4_t texelSize, settings, settings2, settings3;
		VectorSet4(texelSize,
			1.0f / width, 1.0f / height,
			width / (2.0f * sourceWidth), height / (2.0f * sourceHeight));
		VectorSet4(settings,
			(float)steps,
			(float)quality.refineSteps,
			maxDistance,
			r_ssgiThickness->value);
		VectorSet4(settings2,
			(float)rays,
			SSGI_EDGE_FADE,
			grid,
			(float)(backEndData->realFrameNumber & 1023));
		VectorSet4(settings3,
			(float)(SCREEN_HIZ_MIPS - 1),
			(float)(SSGI_SOURCE_MIPS - 1),
			r_znear->value,
			(float)Com_Clampi(8, 1024, steps * 3));
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS3, settings3);
		RB_InstantTriangle();
	}
	RB_ScreenEndTimer(timer);

	image_t *rawImage = tr.ssgiTraceImage;
	image_t *temporalImage = rawImage;
	image_t *historyGeom = NULL;

	// temporal accumulation
	if ( r_ssgiTemporal->integer )
	{
		timer = RB_ScreenBeginTimer("SSGI temporal");

		if ( r_ssgiFreezeHistory->integer && s_history.valid )
		{
			// debug: keep showing the frozen history
			temporalImage = tr.ssgiHistoryImage[s_history.current];
			historyGeom = tr.ssgiHistoryGeomImage[s_history.current];
		}
		else
		{
			const qboolean historyValid = RB_ScreenHistoryValid(s_history, grid);
			const int previous = s_history.current;
			const int current = previous ^ 1;

			matrix_t reproject;
			Matrix16Multiply(historyValid ? s_history.viewProjection : info.viewProjection,
				info.viewToWorld, reproject);
			const qboolean velocity = RB_ScreenVelocityValid();

			shaderProgram_t *sp = &tr.ssgiTemporalShader;
			RB_ScreenBeginPass(tr.ssgiHistoryFbo[current], sp, traceWidth, traceHeight);
			RB_ScreenBindGeometry();
			GL_BindToTMU(rawImage, TB_SHADOWMAP);
			GL_BindToTMU(tr.ssgiHistoryImage[previous], TB_CUBEMAP);
			GL_BindToTMU(tr.ssgiHistoryGeomImage[previous], TB_ENVBRDFMAP);
			GL_BindToTMU(velocity ? tr.velocityImage : tr.whiteImage, TB_SSAOMAP);
			RB_ScreenSetViewUniforms(sp, info);
			GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRREPROJECT, reproject, 1);

			vec4_t texelSize, settings;
			RB_ScreenTexelSize(texelSize, width, height, traceWidth, traceHeight);
			VectorSet4(settings,
				historyValid ? 1.0f : 0.0f,
				Com_Clamp(0.0f, 0.98f, r_ssgiHistoryWeight->value),
				velocity ? 1.0f : 0.0f,
				grid);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();

			RB_ScreenStoreHistory(s_history, info, grid, current);
			temporalImage = tr.ssgiHistoryImage[current];
			historyGeom = tr.ssgiHistoryGeomImage[current];
		}

		RB_ScreenEndTimer(timer);
	}
	else
	{
		// turning it back on starts from scratch
		s_history.valid = qfalse;
	}

	// spatial denoise
	image_t *finalImage = temporalImage;
	if ( denoisePasses > 0 )
	{
		timer = RB_ScreenBeginTimer("SSGI denoise");
		shaderProgram_t *sp = &tr.ssgiDenoiseShader;
		for ( int pass = 0; pass < denoisePasses; pass++ )
		{
			const int target = pass & 1;
			RB_ScreenBeginPass(tr.ssgiDenoiseFbo[target], sp, traceWidth, traceHeight);
			RB_ScreenBindGeometry();
			GL_BindToTMU(finalImage, TB_SHADOWMAP);
			RB_ScreenSetViewUniforms(sp, info);

			vec4_t texelSize, settings;
			RB_ScreenTexelSize(texelSize, width, height, traceWidth, traceHeight);
			VectorSet4(settings, (float)(1 << pass), pass > 0 ? 1.0f : 0.0f, grid, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();

			finalImage = tr.ssgiDenoiseImage[target];
		}
		RB_ScreenEndTimer(timer);
	}

	// upsample, * albedo, into the scene color
	timer = RB_ScreenBeginTimer("SSGI composite");
	{
		int mode = SSGI_COMPOSITE_ADD;
		image_t *giImage = finalImage;
		switch ( debugView )
		{
			case SSGI_DEBUG_RAW:
				mode = SSGI_COMPOSITE_SHOW_GI;
				giImage = rawImage;
				break;
			case SSGI_DEBUG_TEMPORAL:
				mode = SSGI_COMPOSITE_SHOW_GI;
				giImage = temporalImage;
				break;
			case SSGI_DEBUG_DENOISED:
				mode = SSGI_COMPOSITE_SHOW_GI;
				break;
			case SSGI_DEBUG_DYNAMICSOURCE:
			case SSGI_DEBUG_EMISSIVESOURCE:
				mode = SSGI_COMPOSITE_SHOW_SOURCE;
				break;
			case SSGI_DEBUG_INDIRECT:
				mode = SSGI_COMPOSITE_SHOW_INDIRECT;
				break;
			default:
				break;
		}

		const float splitX = r_ssgiCompare->integer ?
			viewParms.viewportX + 0.5f * viewParms.viewportWidth : -1.0f;

		shaderProgram_t *sp = &tr.ssgiCompositeShader;
		FBO_Bind(tr.screenCompositeFbo);
		GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
			viewParms.viewportWidth, viewParms.viewportHeight);
		GL_Cull(CT_TWO_SIDED);
		if ( mode == SSGI_COMPOSITE_ADD )
			GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		else
			GL_State(GLS_DEPTHTEST_DISABLE);
		GLSL_BindProgram(sp);
		RB_ScreenBindGeometry();
		GL_BindToTMU(giImage, TB_SHADOWMAP);
		GL_BindToTMU(needScene ? tr.ssgiSceneImage : tr.whiteImage, TB_SPECULARMAP);
		RB_ScreenSetViewUniforms(sp, info);

		vec4_t texelSize, settings, settings2;
		RB_ScreenTexelSize(texelSize, width, height, traceWidth, traceHeight);
		VectorSet4(settings,
			Q_max(0.0f, r_ssgiIntensity->value),
			splitX,
			(float)mode,
			linearScene ? 1.0f : 0.0f);
		VectorSet4(settings2, grid, 0.0f, 0.0f, 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);
		RB_InstantTriangle();
	}
	RB_ScreenEndTimer(timer);

	s_debug.frameNumber = backEndData->realFrameNumber;
	s_debug.historyGeom = historyGeom;
	s_debug.valid = qtrue;
}

/*
============================================================

Debug views

============================================================
*/

// r_ssgiDebug 1, 2, 5, 10: buffers drawn over the final image at the end of
// the post process chain (the radiance views 3, 4, 6-9 replace the scene
// color in RB_RenderSSGI and go through the tone mapping)
void RB_SSGIDebugOverlay( void )
{
	const int debugView = r_ssgiDebug->integer;
	if ( !s_ssgiResources )
		return;
	if ( debugView != SSGI_DEBUG_HITMASK && debugView != SSGI_DEBUG_HITDISTANCE &&
		debugView != SSGI_DEBUG_HISTORYWEIGHT && debugView != SSGI_DEBUG_ALBEDO )
		return;
	if ( !s_debug.valid || s_debug.frameNumber != backEndData->realFrameNumber )
		return;

	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	shaderProgram_t *sp = &tr.ssgiDebugShader;
	GLSL_BindProgram(sp);
	RB_ScreenBindGeometry();
	GL_BindToTMU(tr.ssgiHitImage, TB_SHADOWMAP);
	GL_BindToTMU(s_debug.historyGeom ? s_debug.historyGeom : tr.whiteImage, TB_ENVBRDFMAP);

	vec4_t settings;
	VectorSet4(settings,
		(float)debugView,
		(float)s_gridScale,
		Com_Clamp(0.0f, 0.98f, r_ssgiHistoryWeight->value),
		s_debug.historyGeom ? 1.0f : 0.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);

	RB_InstantTriangle();
	GL_SelectTexture(0);
}
