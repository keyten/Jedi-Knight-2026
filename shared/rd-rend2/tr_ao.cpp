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

// Screen-space ambient occlusion and contact shadows.
//
// Runs right after the depth prepass of a world view, so the main (lightall)
// pass of the same view can sample the result (TB_SSAOMAP, u_SSAOMap):
//
//   legacy SSAO (r_aoMode 1): the original rend2 SSAO, unchanged
//     depth -> hdrDepth -> ssao -> depthBlur x2 -> screenSsao (half res)
//   GTAO (r_aoMode 2):
//     depth -> aoDepth mip 0 (linear view depth, half or full res)
//           -> aoDepth mips 1..AO_DEPTH_MIPS-1
//           -> gtao main -> gtao[0] -> denoise x r_gtaoDenoise (ping-pong)
//   composite (full res), when anything but plain legacy SSAO is needed:
//     r = AO (legacy, GTAO depth-aware upsampled, or split), g = sun contact
//     shadow -> screenAo
//
// lightall gets screenSsao (plain legacy SSAO, bit exact with the old path),
// screenAo, or the white image (nothing computed for this view).

#include "tr_local.h"

// Resources (and USE_SSAO in the GLSL header) are decided when the renderer
// builds its GPU shaders, see R_CreateAOImages
static qboolean s_aoResources = qfalse;

// state of the last view that computed AO, for r_debugAO
static image_t *s_debugGtaoImage = NULL;
static image_t *s_debugFinalImage = NULL;
static float s_debugZFar = 4096.0f;

struct gtaoQualityPreset_t
{
	const char *name;
	int slices;
	int stepsPerSide;
};

static const gtaoQualityPreset_t gtaoQualityPresets[] =
{
	{ "low",    1, 3 },
	{ "medium", 2, 4 },
	{ "high",   3, 6 },
	{ "ultra",  6, 8 },
};

qboolean R_AOResourcesEnabled( void )
{
	return s_aoResources;
}

int R_AOMode( void )
{
	if ( !s_aoResources )
		return AO_MODE_OFF;

	int mode = r_aoMode->integer;
	if ( mode < 0 )
		mode = r_ssao->integer ? AO_MODE_LEGACY : AO_MODE_OFF;
	return Com_Clampi(AO_MODE_OFF, AO_MODE_GTAO, mode);
}

static void R_AOImageNearest( image_t *image, int maxLevel )
{
	GL_Bind(image);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		maxLevel > 0 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void R_CreateAOImages( int width, int height )
{
	// GPU shaders are kept over a map change (only a vid_restart rebuilds
	// them). They were compiled with or without USE_SSAO, so keep what they
	// expect; the cvars only take effect when the shaders are rebuilt.
	if ( !tr.textureColorShader[0].program )
	{
		s_aoResources = (qboolean)(
			r_ssao->integer ||
			r_aoMode->integer > 0 ||
			r_contactShadows->integer);
	}

	s_debugGtaoImage = NULL;
	s_debugFinalImage = NULL;

	if ( !s_aoResources )
		return;

	const int flags = IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE;

	// legacy SSAO. r = AO, g = 1 (lightall reads g as the contact shadow)
	tr.screenSsaoImage = R_CreateImage(
		"*screenSsao", NULL, width / 2, height / 2, IMGTYPE_COLORALPHA,
		flags, GL_RG8);
	tr.hdrDepthImage = R_CreateImage(
		"*hdrDepth", NULL, width, height, IMGTYPE_COLORALPHA,
		flags, GL_R32F);

	// GTAO
	const int aoWidth  = r_gtaoHalfRes->integer ? Q_max(1, width / 2) : width;
	const int aoHeight = r_gtaoHalfRes->integer ? Q_max(1, height / 2) : height;

	tr.aoDepthImage = R_CreateImage(
		"*aoDepth", NULL, aoWidth, aoHeight, IMGTYPE_COLORALPHA,
		flags | IMGFLAG_MUTABLE, GL_R32F);
	GL_Bind(tr.aoDepthImage);
	for ( int level = 1; level < AO_DEPTH_MIPS; level++ )
	{
		qglTexImage2D(
			GL_TEXTURE_2D, level, GL_R32F,
			Q_max(1, aoWidth >> level), Q_max(1, aoHeight >> level), 0,
			GL_RED, GL_FLOAT, NULL);
	}
	R_AOImageNearest(tr.aoDepthImage, AO_DEPTH_MIPS - 1);

	for ( int i = 0; i < 2; i++ )
	{
		tr.gtaoImage[i] = R_CreateImage(
			va("*gtao%d", i), NULL, aoWidth, aoHeight, IMGTYPE_COLORALPHA,
			flags, GL_RGBA8);
		R_AOImageNearest(tr.gtaoImage[i], 0);
	}

	// final AO + contact shadow, sampled by lightall at full resolution
	tr.screenAoImage = R_CreateImage(
		"*screenAo", NULL, width, height, IMGTYPE_COLORALPHA,
		flags, GL_RG8);

	GL_SelectTexture(0);
}

/*
============================================================

GPU timers (r_speeds 100), same bookkeeping as RB_BeginTimedBlock

============================================================
*/

static int RB_AOBeginTimer( const char *name )
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

static void RB_AOEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

/*
============================================================

Passes

============================================================
*/

struct aoViewInfo_t
{
	vec4_t projection;	// P[0], P[5], P[8], P[9]
	vec4_t depthParams;	// P[14], P[10], zFar, sky threshold
	vec4_t viewport;	// view rectangle in texture coordinates
	float pixelViewSize;	// view space size of one screen pixel at depth 1
};

static void RB_AOSetViewUniforms( shaderProgram_t *sp, const aoViewInfo_t& info )
{
	GLSL_SetUniformVec4(sp, UNIFORM_AOPROJECTION, info.projection);
	GLSL_SetUniformVec4(sp, UNIFORM_AODEPTHPARAMS, info.depthParams);
	GLSL_SetUniformVec4(sp, UNIFORM_AOVIEWPORT, info.viewport);
}

static void RB_AOBeginPass( FBO_t *fbo, shaderProgram_t *sp )
{
	FBO_Bind(fbo);
	GL_SetViewportAndScissor(0, 0, fbo->width, fbo->height);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(sp);
}

// The original rend2 SSAO, unchanged
static void RB_RenderLegacySSAO( void )
{
	const int timer = RB_AOBeginTimer("AO legacy SSAO");

	// need the depth in a texture we can do GL_LINEAR sampling on, so
	// copy it to an HDR image
	FBO_FastBlitFromTexture(tr.renderDepthImage, tr.hdrDepthFbo, NULL, NULL, 0);

	const float zmax = backEnd.viewParms.zFar;
	const float zmin = r_znear->value;
	const vec4_t viewInfo = { zmax / zmin, zmax, 0.0f, 0.0f };

	FBO_Bind(tr.quarterFbo[0]);

	GL_SetViewportAndScissor(0, 0, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height);

	GL_State( GLS_DEPTHTEST_DISABLE );

	GLSL_BindProgram(&tr.ssaoShader);

	GL_BindToTMU(tr.hdrDepthImage, TB_COLORMAP);
	GLSL_SetUniformVec4(&tr.ssaoShader, UNIFORM_VIEWINFO, viewInfo);

	RB_InstantTriangle();

	FBO_Bind(tr.quarterFbo[1]);

	GL_SetViewportAndScissor(0, 0, tr.quarterFbo[1]->width, tr.quarterFbo[1]->height);

	GLSL_BindProgram(&tr.depthBlurShader[0]);

	GL_BindToTMU(tr.quarterImage[0],  TB_COLORMAP);
	GL_BindToTMU(tr.hdrDepthImage, TB_LIGHTMAP);
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);

	RB_InstantTriangle();

	FBO_Bind(tr.screenSsaoFbo);

	GL_SetViewportAndScissor(0, 0, tr.screenSsaoFbo->width, tr.screenSsaoFbo->height);

	GLSL_BindProgram(&tr.depthBlurShader[1]);

	GL_BindToTMU(tr.quarterImage[1],  TB_COLORMAP);
	GL_BindToTMU(tr.hdrDepthImage, TB_LIGHTMAP);
	GLSL_SetUniformVec4(&tr.depthBlurShader[1], UNIFORM_VIEWINFO, viewInfo);

	RB_InstantTriangle();

	RB_AOEndTimer(timer);
}

static float RB_GTAORadius( void )
{
	return Com_Clamp(1.0f, 512.0f, r_gtaoRadius->value);
}

static float RB_GTAOFalloffRange( void )
{
	return Com_Clamp(0.05f, 1.0f, r_gtaoFalloff->value) * RB_GTAORadius();
}

static void RB_RenderGTAODepth( const aoViewInfo_t& info )
{
	const int timer = RB_AOBeginTimer("AO GTAO depth");

	vec4_t settings;

	// linearize (and downsample) the depth buffer
	shaderProgram_t *sp = &tr.gtaoDepthShader[0];
	RB_AOBeginPass(tr.aoDepthFbo[0], sp);
	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	VectorSet4(settings, r_gtaoHalfRes->integer ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);
	RB_AOSetViewUniforms(sp, info);
	RB_InstantTriangle();

	// mips. Sampling is restricted to the source level while rendering the
	// next one, which avoids a feedback loop on the same texture
	sp = &tr.gtaoDepthShader[1];
	GLSL_BindProgram(sp);
	VectorSet4(settings, RB_GTAORadius() * 0.75f, RB_GTAOFalloffRange() * 0.75f, 0.0f, 0.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);
	for ( int level = 1; level < AO_DEPTH_MIPS; level++ )
	{
		FBO_Bind(tr.aoDepthFbo[level]);
		GL_SetViewportAndScissor(0, 0, tr.aoDepthFbo[level]->width, tr.aoDepthFbo[level]->height);
		GL_BindToTMU(tr.aoDepthImage, TB_COLORMAP);
		GL_SelectTexture(TB_COLORMAP);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, level - 1);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level - 1);
		RB_InstantTriangle();
	}
	GL_BindToTMU(tr.aoDepthImage, TB_COLORMAP);
	GL_SelectTexture(TB_COLORMAP);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, AO_DEPTH_MIPS - 1);

	RB_AOEndTimer(timer);
}

// returns the image holding the final GTAO result
static image_t *RB_RenderGTAO( const aoViewInfo_t& info )
{
	RB_RenderGTAODepth(info);

	const float aoWidth = (float)tr.aoDepthImage->width;
	const float aoHeight = (float)tr.aoDepthImage->height;
	const vec4_t texelSize = { 1.0f / aoWidth, 1.0f / aoHeight, 1.0f / aoWidth, 1.0f / aoHeight };

	// main pass
	int timer = RB_AOBeginTimer("AO GTAO main");

	const gtaoQualityPreset_t& quality =
		gtaoQualityPresets[Com_Clampi(0, ARRAY_LEN(gtaoQualityPresets) - 1, r_gtaoQuality->integer)];
	const float aoPixelScale = r_gtaoHalfRes->integer ? 2.0f : 1.0f;

	vec4_t settings, settings2;
	VectorSet4(settings,
		(float)quality.slices,
		(float)quality.stepsPerSide,
		RB_GTAORadius(),
		RB_GTAOFalloffRange());
	VectorSet4(settings2,
		Com_Clamp(0.0f, 4.0f, r_gtaoThickness->value),
		Com_Clamp(0.1f, 8.0f, r_gtaoPower->value),
		// at most a quarter of the view width, beyond that the depth mips
		// get too coarse to find thin occluders anyway
		Q_max(8.0f, 0.25f * backEnd.viewParms.viewportWidth / aoPixelScale),
		info.pixelViewSize * aoPixelScale);

	shaderProgram_t *sp = &tr.gtaoShader;
	RB_AOBeginPass(tr.gtaoFbo[0], sp);
	GL_BindToTMU(tr.aoDepthImage, TB_COLORMAP);
	RB_AOSetViewUniforms(sp, info);
	GLSL_SetUniformVec4(sp, UNIFORM_AOTEXELSIZE, texelSize);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS2, settings2);
	RB_InstantTriangle();

	RB_AOEndTimer(timer);

	// spatial denoise. r_debugAO 2 shows the raw result
	int numPasses = Com_Clampi(0, 3, r_gtaoDenoise->integer);
	if ( r_debugAO->integer == 2 )
		numPasses = 0;

	int current = 0;
	if ( numPasses > 0 )
	{
		timer = RB_AOBeginTimer("AO GTAO denoise");

		sp = &tr.gtaoDenoiseShader;
		for ( int pass = 0; pass < numPasses; pass++ )
		{
			RB_AOBeginPass(tr.gtaoFbo[current ^ 1], sp);
			GL_BindToTMU(tr.gtaoImage[current], TB_COLORMAP);
			GL_BindToTMU(tr.aoDepthImage, TB_LIGHTMAP);
			RB_AOSetViewUniforms(sp, info);
			GLSL_SetUniformVec4(sp, UNIFORM_AOTEXELSIZE, texelSize);
			VectorSet4(settings, (float)(1 << pass), 0.0f, 0.0f, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);
			RB_InstantTriangle();
			current ^= 1;
		}

		RB_AOEndTimer(timer);
	}

	return tr.gtaoImage[current];
}

static void RB_RenderAOComposite(
	const aoViewInfo_t& info,
	int aoSource,
	image_t *gtaoResult,
	qboolean contactShadows )
{
	const int timer = RB_AOBeginTimer("AO composite/contact");

	shaderProgram_t *sp = &tr.aoCompositeShader;
	RB_AOBeginPass(tr.screenAoFbo, sp);

	GL_BindToTMU(gtaoResult ? gtaoResult : tr.whiteImage, TB_COLORMAP);
	GL_BindToTMU(tr.renderDepthImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.aoDepthImage, TB_NORMALMAP);
	GL_BindToTMU(tr.screenSsaoImage, TB_DELUXEMAP);

	RB_AOSetViewUniforms(sp, info);

	const vec4_t texelSize = {
		1.0f / tr.aoDepthImage->width, 1.0f / tr.aoDepthImage->height,
		1.0f / tr.screenAoImage->width, 1.0f / tr.screenAoImage->height };
	GLSL_SetUniformVec4(sp, UNIFORM_AOTEXELSIZE, texelSize);

	vec4_t settings, settings2;
	VectorSet4(settings,
		(float)aoSource,
		info.viewport[0] + 0.5f * info.viewport[2],
		contactShadows ? 1.0f : 0.0f,
		Com_Clamp(0.0f, 1.0f, r_contactShadowStrength->value));
	VectorSet4(settings2,
		Com_Clamp(1.0f, 128.0f, r_contactShadowLength->value),
		(float)Com_Clampi(2, 32, r_contactShadowSteps->integer),
		Com_Clamp(0.1f, 64.0f, r_contactShadowThickness->value),
		info.pixelViewSize);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS2, settings2);
	const vec4_t settings3 = { r_contactShadowSoft->integer ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS3, settings3);

	// the sun in view space (x right, y up, z forward)
	const vec3_t *axis = backEnd.viewParms.ori.axis;
	const float *sunDir = backEnd.refdef.sunDir;
	vec3_t lightDir = {
		-DotProduct(sunDir, axis[1]),
		DotProduct(sunDir, axis[2]),
		DotProduct(sunDir, axis[0]) };
	VectorNormalize(lightDir);
	GLSL_SetUniformVec3(sp, UNIFORM_AOLIGHTDIR, lightDir);

	RB_InstantTriangle();

	RB_AOEndTimer(timer);
}

/*
=================
RB_RenderScreenSpaceLighting

Called after the depth prepass of a view. Leaves the map for lightall in
backEnd.screenAoImage (white when nothing was computed).
=================
*/
void RB_RenderScreenSpaceLighting( void )
{
	backEnd.screenAoImage = tr.whiteImage;

	if ( !s_aoResources )
	{
		static qboolean warned = qfalse;
		if ( !warned && (r_aoMode->integer > 0 || r_contactShadows->integer) )
		{
			ri.Printf(PRINT_WARNING, "r_aoMode / r_contactShadows: the renderer was started without them, vid_restart to enable\n");
			warned = qtrue;
		}
		return;
	}

	const viewParms_t& viewParms = backEnd.viewParms;
	if ( viewParms.flags & VPF_DEPTHSHADOW )
		return;

	// the depth reconstruction needs an unmodified perspective projection of
	// a view rendered into renderFbo: no sky portals, mirrors/portals
	// (oblique near plane), cubemap or shadow views
	if ( viewParms.isSkyPortal || viewParms.isPortal )
		return;
	if ( viewParms.targetFbo != NULL && viewParms.targetFbo != tr.renderFbo )
		return;
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		return;

	const int debugView = r_debugAO->integer;
	const int mode = R_AOMode();
	const qboolean compare = (qboolean)(r_aoCompare->integer != 0);
	const qboolean contact = (qboolean)(
		r_contactShadows->integer &&
		r_sunlightMode->integer &&
		(viewParms.flags & VPF_USESUNLIGHT));

	const qboolean needLegacy = (qboolean)(mode == AO_MODE_LEGACY || compare || debugView == 1);
	const qboolean needGtao = (qboolean)(mode == AO_MODE_GTAO || compare || (debugView >= 2 && debugView <= 5));

	if ( !needLegacy && !needGtao && !contact )
		return;

	// With MSAA the depth prepass only resolves into renderDepthImage for
	// views that target renderFbo explicitly; the main view does not.
	if ( tr.msaaResolveFbo && viewParms.targetFbo != tr.renderFbo )
	{
		FBO_FastBlit(
			tr.renderFbo, NULL,
			tr.msaaResolveFbo, NULL,
			GL_DEPTH_BUFFER_BIT,
			GL_NEAREST);
	}

	aoViewInfo_t info;
	const float *proj = viewParms.projectionMatrix;
	VectorSet4(info.projection, proj[0], proj[5], proj[8], proj[9]);
	VectorSet4(info.depthParams, proj[14], proj[10], viewParms.zFar, viewParms.zFar * 0.99f);
	VectorSet4(info.viewport,
		viewParms.viewportX / (float)glConfig.vidWidth,
		viewParms.viewportY / (float)glConfig.vidHeight,
		viewParms.viewportWidth / (float)glConfig.vidWidth,
		viewParms.viewportHeight / (float)glConfig.vidHeight);
	info.pixelViewSize = 2.0f / (proj[0] * Q_max(1, viewParms.viewportWidth));

	if ( needLegacy )
		RB_RenderLegacySSAO();

	image_t *gtaoResult = NULL;
	if ( needGtao )
		gtaoResult = RB_RenderGTAO(info);

	int aoSource = AO_MODE_OFF;
	if ( compare )
		aoSource = 3;
	else if ( mode == AO_MODE_LEGACY )
		aoSource = 1;
	else if ( mode == AO_MODE_GTAO )
		aoSource = 2;

	if ( aoSource == 1 && !contact )
	{
		// plain legacy SSAO, sampled by lightall exactly as before
		backEnd.screenAoImage = tr.screenSsaoImage;
	}
	else if ( aoSource != AO_MODE_OFF || contact )
	{
		RB_RenderAOComposite(info, aoSource, gtaoResult, contact);
		backEnd.screenAoImage = tr.screenAoImage;
	}

	s_debugGtaoImage = gtaoResult;
	s_debugFinalImage = backEnd.screenAoImage;
	s_debugZFar = viewParms.zFar;
}

/*
============================================================

lightall parameters and debug views

============================================================
*/

void RB_AOSceneParams( vec4_t aoParams, vec4_t aoParams2 )
{
	VectorSet4(aoParams, 0.0f, 0.0f, 0.0f, 0.0f);
	VectorSet4(aoParams2, 0.0f, 0.0f, 0.0f, 0.0f);

	if ( !s_aoResources )
		return;

	const int mode = R_AOMode();
	int application = r_aoApply->integer;
	if ( application < 0 )
		application = (mode == AO_MODE_GTAO) ? 1 : 0;
	application = Com_Clampi(0, 1, application);

	if ( r_aoCompare->integer )
		application = 2; // legacy application left, indirect-only right

	aoParams[0] = (float)application;
	aoParams[1] = Com_Clamp(0.0f, 1.0f, r_aoLightmapFraction->value);
	aoParams[2] = r_aoMultiBounce->integer ? 1.0f : 0.0f;
	aoParams[3] = 0.5f * glConfig.vidWidth;

	const int debugView = r_debugAO->integer;
	aoParams2[0] = (debugView >= 7 && debugView <= 9) ? (float)debugView : 0.0f;
}

qboolean RB_AODebugBypassesToneMap( void )
{
	return (qboolean)(
		(r_sunShadowMode->integer && r_shadowDebug->integer >= 1 && r_shadowDebug->integer <= 11) ||
		(s_aoResources && r_debugAO->integer >= 7 && r_debugAO->integer <= 9) ||
		(r_autoPBRDebug->integer >= 1 && r_autoPBRDebug->integer <= 2) ||
		(r_weatherWetness->integer && r_weatherWetnessDebug->integer >= 1 && r_weatherWetnessDebug->integer <= 26 && r_weatherWetnessDebug->integer != 4) ||
		(r_diffuseIBL->integer && r_diffuseIBLDebug->integer >= 1 && r_diffuseIBLDebug->integer <= 5) ||
		RB_ForwardPlusDebugBypassesToneMap() ||
		RB_SkinSSSDebugBypassesToneMap() ||
		RB_PomSilhouetteDebugBypassesToneMap());
}

// Full screen r_debugAO views of the AO buffers, drawn at the end of the post
// process chain
void RB_AODebugOverlay( void )
{
	const int debugView = r_debugAO->integer;
	if ( !s_aoResources || debugView <= 0 || (debugView >= 7 && debugView <= 9) || debugView > 10 )
		return;

	image_t *image = NULL;
	switch ( debugView )
	{
		case 1: // raw legacy SSAO
			image = tr.screenSsaoImage;
			break;
		case 2: // raw GTAO (denoiser skipped)
		case 3: // denoised GTAO
		case 4: // reconstructed normals
			image = s_debugGtaoImage;
			break;
		case 5: // linear depth
			image = tr.aoDepthImage;
			break;
		case 6: // contact shadow
		case 10: // final AO map lightall used
			image = s_debugFinalImage;
			break;
	}

	if ( !image )
		return;

	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	shaderProgram_t *sp = &tr.aoDebugShader;
	GLSL_BindProgram(sp);
	GL_BindToTMU(image, TB_COLORMAP);
	GL_BindToTMU(tr.aoDepthImage, TB_LIGHTMAP);

	vec4_t settings;
	VectorSet4(settings, (float)debugView, s_debugZFar, 0.0f, 0.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_AOSETTINGS, settings);

	RB_InstantTriangle();
}
