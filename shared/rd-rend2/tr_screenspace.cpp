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

// Shared screen-space infrastructure of the passes that run between the
// opaque surfaces of a view and the rest of its main pass: screen-space
// reflections (r_ssr, tr_ssr.cpp), screen-space diffuse GI (r_ssgi,
// tr_ssgi.cpp) and the skin diffusion (r_skinSSS 2, tr_skinsss.cpp). Created
// when at least one consumer is enabled; no consumer requires another.
//
// Attachments of renderFbo written by the opaque lightall stages (only in
// views that use them, see RB_WritesScreenMaterial and GL_SetScreenAuxWrite):
//
//   2 screenNormalImage  RGB10_A2  rg = octahedral world normal, b = roughness,
//                                  a = SSR receiver (shared)
//   3 ssrSpecularImage   RGB10_A2  SSR only
//   4 ssrCubemapImage    RGBA16F   SSR only
//   5 ssgiAlbedoImage    RGBA8     SSGI only
//   6 ssgiRadianceImage  RGBA16F   SSGI only
//   7 skinDiffuseImage   RGBA16F   skin SSS only
//
// Absent attachments leave a GL_NONE gap in the draw buffers (the fragment
// outputs are bound to fixed locations, see GLSL_BindAttributeLocations).
//
// Per view, after the opaque sort (RB_RenderScreenSpaceOpaque):
//   resolve   MSAA: depth and the screen attachments
//   depth     hardware depth -> screenHiZ mip 0 (linear view depth), mips 1..
//             (closest depth), built once for all consumers
//   skin SSS  tr_skinsss.cpp (skin diffuse replaced by its diffused copy,
//             before the SSGI / SSR composites and the SSR color pyramid)
//   SSGI      tr_ssgi.cpp (its composite goes into the scene color first, so
//             the SSR color pyramid sees the indirect light)
//   SSR       tr_ssr.cpp
//
// The shared GLSL (encodings, view space reconstruction, the ray march) is
// the ssr_common.glsl library of every SSR and SSGI program.

#include "tr_local.h"

static qboolean s_screenResources = qfalse;
static qboolean s_screenDepthResources = qfalse;
static unsigned s_hiZFrame = ~0u;
static int s_hiZView = -1;
static int s_hiZScene = -1;

qboolean R_ScreenSpaceResourcesEnabled( void )
{
	return s_screenDepthResources;
}

/*
============================================================

Resources

============================================================
*/

void R_ScreenImageFilter( image_t *image, int maxLevel, qboolean linear )
{
	GL_Bind(image);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
	if ( linear )
	{
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			maxLevel > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			maxLevel > 0 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
}

image_t *R_ScreenCreateImage( const char *name, int width, int height, int internalFormat, qboolean linear )
{
	image_t *image = R_CreateImage(
		name, NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, internalFormat);
	R_ScreenImageFilter(image, 0, linear);
	return image;
}

image_t *R_ScreenCreateMipImage(
	const char *name, int width, int height, int internalFormat,
	GLenum format, GLenum type, int numLevels, qboolean linear )
{
	image_t *image = R_CreateImage(
		name, NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE,
		internalFormat);

	GL_Bind(image);
	for ( int level = 1; level < numLevels; level++ )
	{
		qglTexImage2D(
			GL_TEXTURE_2D, level, internalFormat,
			Q_max(1, width >> level), Q_max(1, height >> level), 0,
			format, type, NULL);
	}
	R_ScreenImageFilter(image, numLevels - 1, linear);
	return image;
}

void R_CreateScreenSpaceImages( int width, int height, int hdrFormat )
{
	// GPU shaders are kept over a map change (only a vid_restart rebuilds
	// them). They were compiled with or without USE_SSR / USE_SSGI, so keep
	// what they expect; r_ssr and r_ssgi are latched anyway.
	if ( !tr.textureColorShader[0].program )
	{
		R_SSRSelectResources();
		R_SSGISelectResources();
		R_SkinSSSSelectResources();
		R_LtcSaberScreenSelectResources();
		s_screenResources = (qboolean)(R_SSRResourcesEnabled() || R_SSGIResourcesEnabled() ||
			R_SkinSSSResourcesEnabled());
		s_screenDepthResources = (qboolean)(s_screenResources || R_LtcSaberScreenResourcesEnabled());
	}
	s_hiZFrame = ~0u;
	s_hiZView = -1;
	s_hiZScene = -1;

	tr.screenNormalImage = NULL;
	tr.screenHiZImage = NULL;

	if ( s_screenResources )
	{
		// shared material attachment of renderFbo (MSAA: resolve target)
		tr.screenNormalImage = R_ScreenCreateImage("*screenNormal", width, height, GL_RGB10_A2, qfalse);
	}
	if ( s_screenDepthResources )
	{
		// mip 0: linear view depth, mips: closest depth of the 2x2 texels
		tr.screenHiZImage = R_ScreenCreateMipImage(
			"*screenHiZ", width, height, GL_R32F, GL_RED, GL_FLOAT, SCREEN_HIZ_MIPS, qfalse);
	}

	R_CreateSSRImages(width, height, hdrFormat);
	R_CreateSSGIImages(width, height, hdrFormat);
	R_CreateSkinSSSImages(width, height);
	R_CreateLtcSaberScreenImages(width, height);

	GL_SelectTexture(0);
}

/*
=================
R_AttachScreenSpaceRenderTargets

Called by FBO_Init with fbo bound: the screen attachments of renderFbo
(multisample renderbuffers with MSAA) and of the MSAA resolve FBO.
=================
*/
void R_AttachScreenSpaceRenderTargets( FBO_t *fbo, int multisample )
{
	if ( !s_screenResources || !fbo )
		return;

	const bool ssr = R_SSROpaqueResourcesEnabled() != qfalse;
	const bool ssgi = R_SSGIResourcesEnabled() != qfalse;
	const bool skin = R_SkinSSSResourcesEnabled() != qfalse;

	if ( multisample )
	{
		FBO_CreateBuffer(fbo, GL_RGB10_A2, SCREEN_ATTACHMENT_NORMAL, multisample);
		if ( ssr )
		{
			FBO_CreateBuffer(fbo, GL_RGB10_A2, SCREEN_ATTACHMENT_SSR_SPECULAR, multisample);
			FBO_CreateBuffer(fbo, GL_RGBA16F, SCREEN_ATTACHMENT_SSR_CUBEMAP, multisample);
		}
		if ( ssgi )
		{
			FBO_CreateBuffer(fbo, GL_RGBA8, SCREEN_ATTACHMENT_SSGI_ALBEDO, multisample);
			FBO_CreateBuffer(fbo, GL_RGBA16F, SCREEN_ATTACHMENT_SSGI_RADIANCE, multisample);
		}
		if ( skin )
			FBO_CreateBuffer(fbo, GL_RGBA16F, SCREEN_ATTACHMENT_SKIN, multisample);
	}
	else
	{
		FBO_AttachTextureImage(tr.screenNormalImage, SCREEN_ATTACHMENT_NORMAL);
		if ( ssr )
		{
			FBO_AttachTextureImage(tr.ssrSpecularImage, SCREEN_ATTACHMENT_SSR_SPECULAR);
			FBO_AttachTextureImage(tr.ssrCubemapImage, SCREEN_ATTACHMENT_SSR_CUBEMAP);
		}
		if ( ssgi )
		{
			FBO_AttachTextureImage(tr.ssgiAlbedoImage, SCREEN_ATTACHMENT_SSGI_ALBEDO);
			FBO_AttachTextureImage(tr.ssgiRadianceImage, SCREEN_ATTACHMENT_SSGI_RADIANCE);
		}
		if ( skin )
			FBO_AttachTextureImage(tr.skinDiffuseImage, SCREEN_ATTACHMENT_SKIN);
	}
}

FBO_t *R_ScreenCreateLevelFBO( const char *name, image_t *image, int level )
{
	const int w = Q_max(1, image->width >> level);
	const int h = Q_max(1, image->height >> level);
	FBO_t *fbo = FBO_Create(name, w, h);

	FBO_Bind(fbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, image->texnum, level);
	glState.currentFBO->colorImage[0] = image;
	glState.currentFBO->colorBuffers[0] = image->texnum;
	qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(fbo);
	return fbo;
}

// color 0 and 1 of a FBO (two targets written by one pass)
FBO_t *R_ScreenCreatePairFBO( const char *name, image_t *image0, image_t *image1 )
{
	FBO_t *fbo = FBO_Create(name, image0->width, image0->height);
	FBO_Bind(fbo);
	FBO_AttachTextureImage(image0, 0);
	FBO_AttachTextureImage(image1, 1);
	const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	qglDrawBuffers(2, bufs);
	R_CheckFBO(fbo);
	return fbo;
}

void R_CreateScreenSpaceFBOs( void )
{
	for ( int i = 0; i < SCREEN_HIZ_MIPS; i++ )
		tr.screenHiZFbo[i] = NULL;
	tr.screenCompositeFbo = NULL;

	if ( s_screenDepthResources )
	{
		for ( int i = 0; i < SCREEN_HIZ_MIPS; i++ )
			tr.screenHiZFbo[i] = R_ScreenCreateLevelFBO(va("_screenHiZ%d", i), tr.screenHiZImage, i);
	}
	if ( s_screenResources )
	{
		// color 0 of renderFbo only: the composites must not touch the glow
		// and material attachments, and must not have the sampled depth attached
		tr.screenCompositeFbo = FBO_Create("_screenComposite", tr.renderFbo->width, tr.renderFbo->height);
		FBO_Bind(tr.screenCompositeFbo);
		if ( tr.msaaResolveFbo )
		{
			qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER, tr.renderFbo->colorBuffers[0]);
			glState.currentFBO->colorBuffers[0] = tr.renderFbo->colorBuffers[0];
		}
		else
		{
			FBO_AttachTextureImage(tr.renderImage, 0);
		}
		qglDrawBuffer(GL_COLOR_ATTACHMENT0);
		R_CheckFBO(tr.screenCompositeFbo);
	}

	R_CreateSSRFBOs();
	R_CreateSSGIFBOs();
	R_CreateSkinSSSFBOs();
	R_CreateLtcSaberScreenFBOs();

	if ( s_screenResources )
	{
		// the context starts with all color masks enabled
		glState.screenAuxWrite = true;
		GL_ResetScreenAuxWrite();
	}
}

/*
============================================================

Color masks of the screen attachments

============================================================
*/

void GL_SetScreenAuxWrite( bool enable )
{
	if ( !s_screenResources || glState.screenAuxWrite == enable )
		return;

	const GLboolean mask = enable ? GL_TRUE : GL_FALSE;
	for ( int i = SCREEN_ATTACHMENT_FIRST; i <= SCREEN_ATTACHMENT_LAST; i++ )
		qglColorMaski(i, mask, mask, mask, mask);
	glState.screenAuxWrite = enable;
}

// after a qglColorMask call (which sets the masks of all draw buffers)
void GL_ResetScreenAuxWrite( void )
{
	if ( !s_screenResources )
		return;

	glState.screenAuxWrite = true;
	GL_SetScreenAuxWrite(false);
}

/*
============================================================

GPU timers (r_speeds 100), same bookkeeping as RB_BeginTimedBlock

============================================================
*/

int RB_ScreenBeginTimer( const char *name )
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

void RB_ScreenEndTimer( int handle )
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

View

============================================================
*/

/*
=================
RB_ScreenSpaceBeginView

Called by RB_BeginDrawingView with the view's target bound. Decides which
screen-space passes the view gets and clears the screen attachments.
=================
*/
void RB_ScreenSpaceBeginView( void )
{
	backEnd.ssrView = qfalse;
	backEnd.ssgiView = qfalse;
	backEnd.skinSSSView = qfalse;
	backEnd.skinSSSDraws = 0;
	backEnd.screenAuxView = qfalse;

	if ( !s_screenResources )
		return;

	const viewParms_t& viewParms = backEnd.viewParms;
	if ( viewParms.flags & (VPF_DEPTHSHADOW | VPF_NOCUBEMAPS) )
		return;

	// the reconstruction needs an unmodified perspective projection of a
	// view rendered into renderFbo: no sky portals, mirrors/portals (oblique
	// near plane), cubemap or shadow views
	if ( viewParms.isSkyPortal || viewParms.isPortal )
		return;
	if ( viewParms.targetFbo != NULL && viewParms.targetFbo != tr.renderFbo )
		return;
	if ( glState.currentFBO != tr.renderFbo )
		return;
	if ( !tr.world || (backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE)) )
		return;
	if ( backEnd.framePostProcessed )
		return;

	backEnd.ssrView = RB_SSRWantsView();
	backEnd.ssgiView = RB_SSGIWantsView();
	backEnd.skinSSSView = RB_SkinSSSWantsView();
	if ( !backEnd.ssrView && !backEnd.ssgiView && !backEnd.skinSSSView )
		return;

	backEnd.screenAuxView = qtrue;

	// receiver = 0, depth = 0 (never matches: not a receiver)
	const vec4_t clearNormal = { 0.5f, 0.5f, 1.0f, 0.0f };
	const vec4_t clearZero = { 0.0f, 0.0f, 0.0f, 0.0f };
	GL_SetScreenAuxWrite(true);
	qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_NORMAL, clearNormal);
	if ( R_SSROpaqueResourcesEnabled() )
	{
		qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_SSR_SPECULAR, colorBlack);
		qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_SSR_CUBEMAP, clearZero);
	}
	if ( R_SSGIResourcesEnabled() )
	{
		qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_SSGI_ALBEDO, clearZero);
		qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_SSGI_RADIANCE, clearZero);
	}
	if ( R_SkinSSSResourcesEnabled() )
		qglClearBufferfv(GL_COLOR, SCREEN_ATTACHMENT_SKIN, clearZero);
	GL_SetScreenAuxWrite(false);
}

qboolean RB_ScreenSpaceActive( void )
{
	return (qboolean)((backEnd.ssrView || backEnd.ssgiView || backEnd.skinSSSView) &&
		!backEnd.depthFill && !backEnd.refractionFill);
}

/*
============================================================

Shared passes

============================================================
*/

void RB_ScreenSetViewUniforms( shaderProgram_t *sp, const screenViewInfo_t& info )
{
	GLSL_SetUniformVec4(sp, UNIFORM_SSRPROJECTION, info.projection);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRDEPTHPARAMS, info.depthParams);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRVIEWPORT, info.viewport);
	GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRWORLDTOVIEW, info.worldToView, 1);
}

void RB_ScreenBeginPass( FBO_t *fbo, shaderProgram_t *sp, int width, int height, uint32_t stateBits )
{
	FBO_Bind(fbo);
	GL_SetViewportAndScissor(0, 0, width, height);
	GL_State(stateBits);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(sp);
}

void RB_ScreenTexelSize( vec4_t out, int srcWidth, int srcHeight, int dstWidth, int dstHeight )
{
	VectorSet4(out,
		1.0f / Q_max(1, srcWidth), 1.0f / Q_max(1, srcHeight),
		1.0f / Q_max(1, dstWidth), 1.0f / Q_max(1, dstHeight));
}

void RB_ScreenSetLevelRange( image_t *image, int tmu, int baseLevel, int maxLevel )
{
	GL_BindToTMU(image, tmu);
	GL_SelectTexture(tmu);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, baseLevel);
	qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
}

static void RB_ScreenBuildViewInfo( screenViewInfo_t& info )
{
	const viewParms_t& viewParms = backEnd.viewParms;
	const float *proj = viewParms.projectionMatrix;

	VectorSet4(info.projection, proj[0], proj[5], proj[8], proj[9]);
	VectorSet4(info.depthParams, proj[14], proj[10], viewParms.zFar,
		2.0f / (proj[0] * Q_max(1, viewParms.viewportWidth)));
	VectorSet4(info.viewport,
		viewParms.viewportX / (float)tr.renderFbo->width,
		viewParms.viewportY / (float)tr.renderFbo->height,
		viewParms.viewportWidth / (float)tr.renderFbo->width,
		viewParms.viewportHeight / (float)tr.renderFbo->height);

	// screen-space view space: x right, y up, z forward (as tr_ao.cpp)
	vec3_t right, up, forward;
	VectorScale(viewParms.ori.axis[1], -1.0f, right);
	VectorCopy(viewParms.ori.axis[2], up);
	VectorCopy(viewParms.ori.axis[0], forward);
	VectorNormalize(right);
	VectorNormalize(up);
	VectorNormalize(forward);
	const float *o = viewParms.ori.origin;

	// column major
	float *m = info.worldToView;
	m[0] = right[0]; m[4] = right[1]; m[8]  = right[2]; m[12] = -DotProduct(right, o);
	m[1] = up[0];    m[5] = up[1];    m[9]  = up[2];    m[13] = -DotProduct(up, o);
	m[2] = forward[0]; m[6] = forward[1]; m[10] = forward[2]; m[14] = -DotProduct(forward, o);
	m[3] = 0.0f;     m[7] = 0.0f;     m[11] = 0.0f;     m[15] = 1.0f;

	float *v = info.viewToWorld;
	v[0] = right[0];   v[1] = right[1];   v[2]  = right[2];   v[3]  = 0.0f;
	v[4] = up[0];      v[5] = up[1];      v[6]  = up[2];      v[7]  = 0.0f;
	v[8] = forward[0]; v[9] = forward[1]; v[10] = forward[2]; v[11] = 0.0f;
	v[12] = o[0];      v[13] = o[1];      v[14] = o[2];       v[15] = 1.0f;

	Matrix16Multiply(proj, viewParms.world.modelViewMatrix, info.viewProjection);
}

void RB_ScreenGetViewInfo( screenViewInfo_t& info )
{
	RB_ScreenBuildViewInfo(info);
}

// MSAA: resolve depth and the screen attachments. Color is resolved by the
// consumers' own copies.
static void RB_ScreenResolveInputs( void )
{
	if ( !tr.msaaResolveFbo )
		return;

	// blits are clipped by the scissor rectangle
	GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
	FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	GL_SetScreenAuxWrite(true);
	for ( int i = SCREEN_ATTACHMENT_FIRST; i <= SCREEN_ATTACHMENT_LAST; i++ )
	{
		if ( tr.renderFbo->colorBuffers[i] && tr.msaaResolveFbo->colorBuffers[i] )
			FBO_FastBlitIndexed(tr.renderFbo, tr.msaaResolveFbo, i, i, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	GL_SetScreenAuxWrite(false);
}

static void RB_ScreenBuildDepth( const screenViewInfo_t& info, int numLevels )
{
	shaderProgram_t *sp = &tr.screenHiZShader[0];
	RB_ScreenBeginPass(tr.screenHiZFbo[0], sp, tr.screenHiZFbo[0]->width, tr.screenHiZFbo[0]->height);
	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	RB_ScreenSetViewUniforms(sp, info);
	RB_InstantTriangle();

	// closest depth mips. Sampling is restricted to the source level while
	// rendering the next one, which avoids a feedback loop
	sp = &tr.screenHiZShader[1];
	GLSL_BindProgram(sp);
	for ( int level = 1; level < numLevels; level++ )
	{
		FBO_t *fbo = tr.screenHiZFbo[level];
		FBO_Bind(fbo);
		GL_SetViewportAndScissor(0, 0, fbo->width, fbo->height);
		RB_ScreenSetLevelRange(tr.screenHiZImage, TB_SHADOWMAPARRAY, level - 1, level - 1);
		RB_InstantTriangle();
	}
	RB_ScreenSetLevelRange(tr.screenHiZImage, TB_SHADOWMAPARRAY, 0, SCREEN_HIZ_MIPS - 1);
}

void RB_ScreenPrepareSaberDepth(void)
{
	if (r_ltcSaberShadows->integer != 2 || !R_LtcSaberScreenResourcesEnabled() ||
		!tr.screenHiZImage || backEnd.viewParms.viewParmType != VPT_MAIN ||
		backEnd.viewParms.isPortal || backEnd.viewParms.isSkyPortal ||
		glState.currentFBO != tr.renderFbo || !tr.world)
		return;
	screenViewInfo_t info;
	RB_ScreenBuildViewInfo(info);
	if (tr.msaaResolveFbo)
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	if (s_hiZFrame != backEndData->realFrameNumber ||
		s_hiZScene != backEndData->currentFrame->currentScene ||
		s_hiZView != backEnd.viewParms.currentViewParm)
	{
		int timer = RB_ScreenBeginTimer("LTC saber Hi-Z");
		RB_ScreenBuildDepth(info, SCREEN_HIZ_MIPS);
		RB_ScreenEndTimer(timer);
		s_hiZFrame = backEndData->realFrameNumber;
		s_hiZView = backEnd.viewParms.currentViewParm;
		s_hiZScene = backEndData->currentFrame->currentScene;
	}
	RB_RenderLtcSaberScreen(info);
}

// shared textures of every screen-space program (ssr_common.glsl)
void RB_ScreenBindGeometry( void )
{
	GL_BindToTMU(tr.screenNormalImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.screenHiZImage, TB_SHADOWMAPARRAY);
	if ( R_SSROpaqueResourcesEnabled() )
	{
		GL_BindToTMU(tr.ssrSpecularImage, TB_NORMALMAP);
		GL_BindToTMU(tr.ssrCubemapImage, TB_DELUXEMAP);
	}
	if ( R_SSGIResourcesEnabled() )
	{
		GL_BindToTMU(tr.ssgiAlbedoImage, TB_SSGI_ALBEDO);
		GL_BindToTMU(tr.ssgiRadianceImage, TB_SSGI_RADIANCE);
	}
}

/*
=================
RB_RenderScreenSpaceOpaque

Called by RB_SubmitRenderPass between the opaque sort and the rest of the
main pass of a view (RB_ScreenSpaceActive), with renderFbo bound.
=================
*/
void RB_RenderScreenSpaceOpaque( void )
{
	if ( !RB_ScreenSpaceActive() )
		return;

	FBO_t *oldFbo = glState.currentFBO;
	const viewParms_t& viewParms = backEnd.viewParms;

	R_PushDebugGroup(AL_STAGE, "Screen space");

	screenViewInfo_t info;
	RB_ScreenBuildViewInfo(info);

	const int ssrLevels = backEnd.ssrView ? RB_SSRDepthLevels() : 0;
	const int ssgiLevels = backEnd.ssgiView ? RB_SSGIDepthLevels() : 0;

	// shared inputs, once for all consumers
	int timer = RB_ScreenBeginTimer("Screen geometry");
	RB_ScreenResolveInputs();
	if (s_hiZFrame != backEndData->realFrameNumber ||
		s_hiZScene != backEndData->currentFrame->currentScene ||
		s_hiZView != backEnd.viewParms.currentViewParm)
	{
		RB_ScreenBuildDepth(info, Q_max(1, Q_max(ssrLevels, ssgiLevels)));
		s_hiZFrame = backEndData->realFrameNumber;
		s_hiZView = backEnd.viewParms.currentViewParm;
		s_hiZScene = backEndData->currentFrame->currentScene;
	}
	RB_ScreenEndTimer(timer);

	// skin first: the SSGI and SSR composites (and the SSR color pyramid)
	// see the diffused skin. Nothing to do without skin stages in the view.
	if ( backEnd.skinSSSView && backEnd.skinSSSDraws > 0 )
		RB_RenderSkinSSS(info);
	if ( backEnd.ssgiView )
		RB_RenderSSGI(info);
	if ( backEnd.ssrView )
		RB_RenderSSR(info);

	// once per view: the rest of the pass draws on top of the result and
	// does not write the screen attachments
	backEnd.ssrView = qfalse;
	backEnd.ssgiView = qfalse;
	backEnd.skinSSSView = qfalse;
	backEnd.screenAuxView = qfalse;

	R_PushDebugGroup(AL_STAGE, "Mainpass");

	// back to the main pass of the view
	FBO_Bind(oldFbo);
	GL_SetProjectionMatrix(backEnd.viewParms.projectionMatrix);
	GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
		viewParms.viewportWidth, viewParms.viewportHeight);
	GL_SelectTexture(0);
}

void RB_ScreenSpaceDebugOverlay( void )
{
	if ( !s_screenResources )
		return;

	RB_SSRDebugOverlay();
	RB_SSGIDebugOverlay();
}

/*
============================================================

Temporal history cuts (SSR and SSGI keep their own state)

============================================================
*/

// camera cuts reset the temporal history
#define SCREEN_CUT_DISTANCE		192.0f
#define SCREEN_CUT_COS_ANGLE	0.8191520f	// cos(35 deg)
#define SCREEN_CUT_FOV			1.0f

qboolean RB_ScreenHistoryValid( const screenHistory_t& history, float traceScale )
{
	if ( !history.valid )
		return qfalse;

	const viewParms_t& viewParms = backEnd.viewParms;
	vec3_t forward;
	VectorCopy(viewParms.ori.axis[0], forward);
	VectorNormalize(forward);

	return (qboolean)!(
		history.frameNumber + 1 != backEndData->realFrameNumber ||
		history.world != tr.world ||
		!tr.temporalHistoryValid ||
		history.traceScale != traceScale ||
		history.viewport[0] != viewParms.viewportX ||
		history.viewport[1] != viewParms.viewportY ||
		history.viewport[2] != viewParms.viewportWidth ||
		history.viewport[3] != viewParms.viewportHeight ||
		Distance(history.origin, viewParms.ori.origin) > SCREEN_CUT_DISTANCE ||
		DotProduct(history.forward, forward) < SCREEN_CUT_COS_ANGLE ||
		fabsf(history.fovX - viewParms.fovX) > SCREEN_CUT_FOV ||
		fabsf(history.fovY - viewParms.fovY) > SCREEN_CUT_FOV);
}

void RB_ScreenStoreHistory( screenHistory_t& history, const screenViewInfo_t& info, float traceScale, int written )
{
	const viewParms_t& viewParms = backEnd.viewParms;

	history.valid = qtrue;
	history.frameNumber = backEndData->realFrameNumber;
	history.world = tr.world;
	history.traceScale = traceScale;
	VectorCopy(viewParms.ori.origin, history.origin);
	VectorCopy(viewParms.ori.axis[0], history.forward);
	VectorNormalize(history.forward);
	history.fovX = viewParms.fovX;
	history.fovY = viewParms.fovY;
	history.viewport[0] = viewParms.viewportX;
	history.viewport[1] = viewParms.viewportY;
	history.viewport[2] = viewParms.viewportWidth;
	history.viewport[3] = viewParms.viewportHeight;
	Com_Memcpy(history.viewProjection, info.viewProjection, sizeof(matrix_t));
	history.current = written;
}

// the velocity buffer is written by the depth prepass of the first scene of
// the frame
qboolean RB_ScreenVelocityValid( void )
{
	return (qboolean)(
		tr.velocityImage != NULL &&
		(r_depthPrepass->integer || r_ltcSaberShadows->integer == 2) &&
		backEndData->currentFrame &&
		backEndData->currentFrame->currentScene == 0);
}
