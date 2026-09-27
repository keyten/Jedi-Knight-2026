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

// Screen-space reflections (r_ssr), blended with the cubemap reflections.
//
// lightall keeps adding the parallax corrected, roughness prefiltered
// cubemap reflection C = cubemap radiance * W to the scene, W being the
// specular IBL weight (F0 * EnvBRDF.x + EnvBRDF.y, with specular occlusion).
// With r_ssr it also writes extra attachments of renderFbo (only opaque
// lightall stages, see RB_WritesScreenMaterial and GL_SetScreenAuxWrite):
//
//   2 screenNormalImage RGB10_A2  rg = octahedral world normal, b = roughness,
//                                 a = receiver (shared, tr_screenspace.cpp)
//   3 ssrSpecularImage  RGB10_A2  rgb = sqrt(W)
//   4 ssrCubemapImage   RGBA16F   rgb = C, a = view depth (validates the data)
//
// The main pass of a view is split after the opaque sort (RB_SubmitRenderPass).
// There RB_RenderSSR (from RB_RenderScreenSpaceOpaque) traces the opaque HDR
// scene and replaces a part of the cubemap reflection:
//
//   color += confidence * (SSR radiance * W - C)
//
// so a reliable hit shows the screen-space reflection instead of the cubemap
// one (never both), and a miss keeps the cubemap reflection unchanged. Decals,
// fog and blended surfaces are drawn afterwards, on top of the result.
//
//   shared    MSAA resolve of depth and the attachments, linear depth +
//             closest depth mips (screenHiZ), tr_screenspace.cpp
//   copy      opaque scene color -> ssrColor mip 0, mips 1.. (roughness blur,
//             premultiplied coverage: no view model, nothing outside the view)
//   classify  r_ssrCull: depth mask of the pixels that need a ray, so the
//             trace only runs there (early depth test)
//   trace     ray march (linear or Hi-Z), full or half res -> ssrTrace[current].
//             Hit cache (r_ssrHitCache): the hit of the previous frame
//             (ssrTrace[previous]) is reused without a march while it is
//             still the reflection of the ray; each pixel is traced again
//             once every 4 frames anyway
//   resolve   hit -> radiance from the color pyramid (cone of the roughness),
//             depth/normal aware upsampling -> ssrResolve (premultiplied)
//   temporal  optional, reprojected history -> ssrHistory[current], rejected
//             where the receiver or the reflected hit changed
//   composite additive signed delta into color 0 of renderFbo
//
// r_ssr 0 creates none of it (the shared parts only exist when SSGI needs
// them): lightall and the pass are unchanged.
// See docs/rend2-ssr.md.

#include "tr_local.h"

// Resources (and USE_SSR in the GLSL header) are decided when the renderer
// builds its GPU shaders, see R_CreateScreenSpaceImages
static qboolean s_ssrResources = qfalse;
static qboolean s_ssrTemporalResources = qfalse;

// trace grid scale (1 = full, 2 = half resolution rays): a resource size,
// decided with the resources (r_ssrHalfRes / r_ssrQuality, vid_restart)
static int s_gridScale = 1;

struct ssrQualityPreset_t
{
	const char *name;
	int steps;		// linear march steps, Hi-Z: iterations / 3
	int refineSteps;
	int halfRes;
	int hiZ;
};

static const ssrQualityPreset_t& RB_SSRQuality( void );

static const ssrQualityPreset_t ssrQualityPresets[] =
{
	{ "low",    16, 4, 1, 0 },
	{ "medium", 24, 5, 1, 1 },
	{ "high",   40, 6, 0, 1 },
	{ "ultra",  64, 8, 0, 1 },
};

// specular weights below this are not traced (nothing visible to replace)
#define SSR_MIN_WEIGHT		0.004f

// previous view of the history (hit cache and temporal accumulation), the
// trace and history images of the last frame are [s_history.current]
static screenHistory_t s_history;
static matrix_t s_prevViewToWorld;

// state of the last traced view, for r_ssrDebug
static struct
{
	unsigned frameNumber;
	image_t *finalImage;
	image_t *traceImage;
	screenViewInfo_t info;
	float traceScale;
	float maxDistance;
	qboolean valid;
} s_debug;

qboolean R_SSRResourcesEnabled( void )
{
	return s_ssrResources;
}

qboolean R_SSRWantsVelocity( void )
{
	return s_ssrTemporalResources;
}

/*
============================================================

Resources

============================================================
*/

// called by R_CreateScreenSpaceImages when the GPU shaders are (re)built
void R_SSRSelectResources( void )
{
	s_ssrResources = (qboolean)(r_ssr->integer != 0);
	if ( s_ssrResources && !r_specularMapping->integer )
		ri.Printf(PRINT_WARNING, "r_ssr: r_specularMapping is off, no material has specular reflections\n");
	s_ssrTemporalResources = (qboolean)(s_ssrResources && r_ssrTemporal->integer);

	const int halfRes = r_ssrHalfRes->integer >= 0 ? r_ssrHalfRes->integer : RB_SSRQuality().halfRes;
	s_gridScale = halfRes ? 2 : 1;
}

void R_CreateSSRImages( int width, int height, int hdrFormat )
{
	Com_Memset(&s_history, 0, sizeof(s_history));
	Com_Memset(&s_debug, 0, sizeof(s_debug));

	tr.ssrSpecularImage = NULL;
	tr.ssrCubemapImage = NULL;
	tr.ssrColorImage = NULL;
	tr.ssrTraceImage[0] = tr.ssrTraceImage[1] = NULL;
	tr.ssrResolveImage = NULL;
	for ( int i = 0; i < 2; i++ )
	{
		tr.ssrHistoryImage[i] = NULL;
		tr.ssrHistoryGeomImage[i] = NULL;
	}

	if ( !s_ssrResources )
		return;

	// material attachments of renderFbo (MSAA: resolve targets), the normal
	// attachment is shared (tr_screenspace.cpp)
	tr.ssrSpecularImage = R_ScreenCreateImage("*ssrSpecular", width, height, GL_RGB10_A2, qfalse);
	tr.ssrCubemapImage = R_ScreenCreateImage("*ssrCubemap", width, height, GL_RGBA16F, qfalse);

	// opaque scene color pyramid, same format as renderFbo color 0 (the MSAA
	// resolve blit needs identical formats)
	const qboolean floatColor = (qboolean)(hdrFormat == GL_RGBA16F);
	tr.ssrColorImage = R_ScreenCreateMipImage(
		"*ssrColor", width, height, hdrFormat,
		GL_RGBA, floatColor ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, SSR_COLOR_MIPS, qtrue);

	// trace resolution; this frame's hits and the previous frame's (hit cache)
	const int traceWidth = (width + s_gridScale - 1) / s_gridScale;
	const int traceHeight = (height + s_gridScale - 1) / s_gridScale;
	for ( int i = 0; i < 2; i++ )
		tr.ssrTraceImage[i] = R_ScreenCreateImage(va("*ssrTrace%d", i), traceWidth, traceHeight, GL_RGBA16, qfalse);
	tr.ssrResolveImage = R_ScreenCreateImage("*ssrResolve", width, height, GL_RGBA16F, qfalse);

	if ( s_ssrTemporalResources )
	{
		for ( int i = 0; i < 2; i++ )
		{
			tr.ssrHistoryImage[i] = R_ScreenCreateImage(
				va("*ssrHistory%d", i), width, height, GL_RGBA16F, qtrue);
			tr.ssrHistoryGeomImage[i] = R_ScreenCreateImage(
				va("*ssrHistoryGeom%d", i), width, height, GL_RGBA16F, qfalse);
		}
	}
}

void R_CreateSSRFBOs( void )
{
	for ( int i = 0; i < SSR_COLOR_MIPS; i++ )
		tr.ssrColorFbo[i] = NULL;
	tr.ssrTraceFbo[0] = tr.ssrTraceFbo[1] = NULL;
	tr.ssrResolveFbo = NULL;
	tr.ssrHistoryFbo[0] = tr.ssrHistoryFbo[1] = NULL;

	if ( !s_ssrResources )
		return;

	for ( int i = 0; i < SSR_COLOR_MIPS; i++ )
		tr.ssrColorFbo[i] = R_ScreenCreateLevelFBO(va("_ssrColor%d", i), tr.ssrColorImage, i);

	for ( int i = 0; i < 2; i++ )
	{
		// depth: the classification of the pixels that need a ray (r_ssrCull)
		tr.ssrTraceFbo[i] = R_ScreenCreateLevelFBO(va("_ssrTrace%d", i), tr.ssrTraceImage[i], 0);
		FBO_CreateBuffer(tr.ssrTraceFbo[i], GL_DEPTH_COMPONENT16, 0, 0);
		R_CheckFBO(tr.ssrTraceFbo[i]);
	}
	tr.ssrResolveFbo = R_ScreenCreateLevelFBO("_ssrResolve", tr.ssrResolveImage, 0);

	if ( s_ssrTemporalResources )
	{
		for ( int i = 0; i < 2; i++ )
		{
			tr.ssrHistoryFbo[i] = R_ScreenCreatePairFBO(
				va("_ssrHistory%d", i), tr.ssrHistoryImage[i], tr.ssrHistoryGeomImage[i]);
		}
	}
}

/*
============================================================

View

============================================================
*/

// the view gets SSR (called by RB_ScreenSpaceBeginView for views that can
// have screen-space passes)
qboolean RB_SSRWantsView( void )
{
	if ( !s_ssrResources )
		return qfalse;

	// SSR at zero strength without debug views is the legacy look: skip
	// the work (the split screen compare still needs it)
	return (qboolean)(r_ssrStrength->value > 0.0f || r_ssrDebug->integer || r_ssrCompare->integer);
}

static const ssrQualityPreset_t& RB_SSRQuality( void )
{
	return ssrQualityPresets[Com_Clampi(0, ARRAY_LEN(ssrQualityPresets) - 1, r_ssrQuality->integer)];
}

// levels of the shared depth pyramid the SSR of this view traces
int RB_SSRDepthLevels( void )
{
	const qboolean hiZ = (qboolean)(r_ssrHiZ->integer >= 0 ? r_ssrHiZ->integer : RB_SSRQuality().hiZ);
	return hiZ ? SCREEN_HIZ_MIPS : 1;
}

/*
============================================================

Passes

============================================================
*/

static void RB_SSRBindMaterial( void )
{
	RB_ScreenBindGeometry();
}

static void RB_SSRBuildColorPyramid( const screenViewInfo_t& info )
{
	// MSAA resolves here. Blits are clipped by the scissor rectangle
	GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
	FBO_FastBlit(tr.renderFbo, NULL, tr.ssrColorFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	// mip 1 masks the view model and the outside of the view (linear depth),
	// the next mips filter the premultiplied coverage
	for ( int level = 1; level < SSR_COLOR_MIPS; level++ )
	{
		shaderProgram_t *sp = &tr.ssrDownsampleShader[level == 1 ? 1 : 0];
		FBO_t *fbo = tr.ssrColorFbo[level];
		RB_ScreenBeginPass(fbo, sp, fbo->width, fbo->height);
		if ( level == 1 )
		{
			RB_SSRBindMaterial();
			RB_ScreenSetViewUniforms(sp, info);
		}
		RB_ScreenSetLevelRange(tr.ssrColorImage, TB_SPECULARMAP, level - 1, level - 1);

		vec4_t texelSize;
		const FBO_t *src = tr.ssrColorFbo[level - 1];
		RB_ScreenTexelSize(texelSize, src->width, src->height, fbo->width, fbo->height);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		RB_InstantTriangle();
	}
	RB_ScreenSetLevelRange(tr.ssrColorImage, TB_SPECULARMAP, 0, SSR_COLOR_MIPS - 1);
}

/*
=================
RB_SSRCollectEmitters

Light saber blades and additive effect primitives (blaster bolts, muzzle
flashes, lines, cylinders) are blended surfaces without depth, drawn after
the SSR, and not in the cubemaps either. They are reflected analytically: the
composite intersects the reflection ray with capsule / sphere proxies built
here. As in a mirror, RF_FIRST_PERSON entities are left out and
RF_THIRD_PERSON ones are included.

Per emitter, 3 vec4 in SSR view space: (a, radius), (b, sphere), (color, 0).
Returns the number of emitters.
=================
*/
struct ssrEmitter_t
{
	vec3_t a;
	vec3_t b;
	float radius;
	float sphere;
	vec3_t color;
	float importance;
};

static void RB_SSRTransformPoint( const matrix_t m, const vec3_t in, vec3_t out )
{
	out[0] = m[0] * in[0] + m[4] * in[1] + m[8]  * in[2] + m[12];
	out[1] = m[1] * in[0] + m[5] * in[1] + m[9]  * in[2] + m[13];
	out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

static int RB_SSRCollectEmitters( const screenViewInfo_t& info, vec4_t *out )
{
	if ( !r_ssrEmitters->integer || r_ssrEmitterIntensity->value <= 0.0f )
		return 0;

	ssrEmitter_t emitters[SSR_MAX_EMITTERS];
	int numEmitters = 0;
	const float intensity = r_ssrEmitterIntensity->value;
	const float *viewOrigin = backEnd.viewParms.ori.origin;

	for ( int i = 0; i < backEnd.refdef.num_entities; i++ )
	{
		const refEntity_t& e = backEnd.refdef.entities[i].e;
		if ( e.renderfx & RF_FIRST_PERSON )
			continue;

		ssrEmitter_t emitter;
		emitter.radius = e.radius;
		emitter.sphere = 0.0f;
		switch ( e.reType )
		{
			case RT_SABER_GLOW:
				VectorCopy(e.origin, emitter.a);
				VectorMA(e.origin, e.saberLength, e.axis[0], emitter.b);
				break;
			case RT_LINE:
			case RT_ORIENTEDLINE:
			case RT_ELECTRICITY:
				VectorCopy(e.origin, emitter.a);
				VectorCopy(e.oldorigin, emitter.b);
				break;
			case RT_CYLINDER:
				VectorCopy(e.origin, emitter.a);
				VectorCopy(e.oldorigin, emitter.b);
				emitter.radius = Q_max(e.radius, e.rotation);
				break;
			case RT_SPRITE:
				VectorCopy(e.origin, emitter.a);
				VectorCopy(e.origin, emitter.b);
				emitter.sphere = 1.0f;
				break;
			default:
				continue;
		}

		if ( !e.customShader || emitter.radius <= 0.0f )
			continue;

		// additive shaders only: alpha blended smoke and sprites occlude
		// rather than emit
		const shader_t *shader = R_GetShaderByHandle(e.customShader);
		if ( !shader || shader == tr.defaultShader )
			continue;
		const shaderStage_t *stage = shader->stages[0];
		if ( !stage || !stage->active )
			continue;
		if ( (stage->stateBits & GLS_DSTBLEND_BITS) != GLS_DSTBLEND_ONE )
			continue;

		const image_t *image = stage->bundle[0].image[0];
		float scale = intensity / 255.0f;
		if ( (stage->stateBits & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_SRC_ALPHA )
			scale *= e.shaderRGBA[3] / 255.0f;
		for ( int c = 0; c < 3; c++ )
			emitter.color[c] = (image ? image->emissiveColor[c] : 0.5f) * e.shaderRGBA[c] * scale;

		const float luma = 0.2126f * emitter.color[0] + 0.7152f * emitter.color[1] + 0.0722f * emitter.color[2];
		if ( luma < 0.01f )
			continue;

		vec3_t mid;
		VectorAdd(emitter.a, emitter.b, mid);
		VectorScale(mid, 0.5f, mid);
		emitter.importance = luma * (emitter.radius + Distance(emitter.a, emitter.b)) /
			Q_max(64.0f, Distance(mid, viewOrigin));

		// keep the most important ones
		if ( numEmitters < SSR_MAX_EMITTERS )
		{
			emitters[numEmitters++] = emitter;
		}
		else
		{
			int weakest = 0;
			for ( int j = 1; j < numEmitters; j++ )
			{
				if ( emitters[j].importance < emitters[weakest].importance )
					weakest = j;
			}
			if ( emitter.importance > emitters[weakest].importance )
				emitters[weakest] = emitter;
		}
	}

	for ( int i = 0; i < numEmitters; i++ )
	{
		const ssrEmitter_t& emitter = emitters[i];
		vec3_t a, b;
		RB_SSRTransformPoint(info.worldToView, emitter.a, a);
		RB_SSRTransformPoint(info.worldToView, emitter.b, b);
		VectorSet4(out[i * 3 + 0], a[0], a[1], a[2], emitter.radius);
		VectorSet4(out[i * 3 + 1], b[0], b[1], b[2], emitter.sphere);
		VectorSet4(out[i * 3 + 2], emitter.color[0], emitter.color[1], emitter.color[2], 0.0f);
	}

	return numEmitters;
}

/*
=================
RB_RenderSSR

Called by RB_RenderScreenSpaceOpaque between the opaque sort and the rest of
the main pass of a view, after the shared inputs (depth pyramid, MSAA resolve).
=================
*/
void RB_RenderSSR( const screenViewInfo_t& info )
{
	const viewParms_t& viewParms = backEnd.viewParms;

	R_PushDebugGroup(AL_STAGE, "SSR");

	const ssrQualityPreset_t& quality = RB_SSRQuality();
	const int steps = r_ssrSteps->integer > 0 ? r_ssrSteps->integer : quality.steps;
	const int refineSteps = r_ssrRefineSteps->integer > 0 ? r_ssrRefineSteps->integer : quality.refineSteps;
	const qboolean hiZ = (qboolean)(r_ssrHiZ->integer >= 0 ? r_ssrHiZ->integer : quality.hiZ);
	const qboolean temporal = (qboolean)(s_ssrTemporalResources && r_ssrTemporal->integer);
	const float maxDistance = r_ssrMaxDistance->value;
	const float traceScale = (float)s_gridScale;

	// the trace resolution is a resource size
	static qboolean s_resolutionNoted = qfalse;
	const int halfRes = r_ssrHalfRes->integer >= 0 ? r_ssrHalfRes->integer : quality.halfRes;
	if ( (halfRes ? 2 : 1) != s_gridScale && !s_resolutionNoted )
	{
		ri.Printf(PRINT_ALL, "SSR: the trace resolution (r_ssrHalfRes / r_ssrQuality) changes at the next vid_restart\n");
		s_resolutionNoted = qtrue;
	}

	const int width = tr.renderFbo->width;
	const int height = tr.renderFbo->height;
	const int traceWidth = tr.ssrTraceImage[0]->width;
	const int traceHeight = tr.ssrTraceImage[0]->height;

	// history of the previous frame: hit cache and temporal accumulation
	const qboolean historyValid = RB_ScreenHistoryValid(s_history, traceScale);
	const int previous = s_history.current;
	const int current = previous ^ 1;
	const qboolean hitCache = (qboolean)(historyValid && r_ssrHitCache->integer);
	const qboolean velocity = RB_ScreenVelocityValid();

	// this view space -> previous frame clip space, previous view space ->
	// this view space
	matrix_t reproject, prevViewToView;
	Matrix16Multiply(historyValid ? s_history.viewProjection : info.viewProjection,
		info.viewToWorld, reproject);
	Matrix16Multiply(info.worldToView, historyValid ? s_prevViewToWorld : info.viewToWorld, prevViewToView);

	// Jitter and the half resolution phase only change over the frames when
	// the temporal accumulation averages them, else they would flicker. The
	// phase visits the 2x2 block diagonally first: (0,0) (1,1) (1,0) (0,1)
	static const int phaseOrder[4] = { 0, 3, 1, 2 };
	const unsigned frame = backEndData->realFrameNumber;
	const float jitterFrame = temporal ? (float)(frame & 63) : 0.0f;
	const float phase = (temporal && s_gridScale > 1) ? (float)phaseOrder[frame & 3] : 0.0f;

	// inputs (depth and the MSAA resolve of the attachments are shared)
	int timer = RB_ScreenBeginTimer("SSR inputs");
	RB_SSRBuildColorPyramid(info);
	RB_ScreenEndTimer(timer);

	// trace
	timer = RB_ScreenBeginTimer("SSR trace");
	{
		FBO_t *traceFbo = tr.ssrTraceFbo[current];

		vec4_t texelSize, settings, settings2, settings3, settings4;
		RB_ScreenTexelSize(texelSize, width, height, traceWidth, traceHeight);
		VectorSet4(settings,
			(float)Com_Clampi(1, 256, steps),
			(float)Com_Clampi(0, 16, refineSteps),
			maxDistance,
			r_ssrThickness->value);
		VectorSet4(settings2,
			r_ssrMaxRoughness->value,
			r_ssrEdgeFade->value,
			traceScale,
			jitterFrame);
		VectorSet4(settings3,
			(float)(SCREEN_HIZ_MIPS - 1),
			SSR_MIN_WEIGHT,
			r_znear->value,
			(float)Com_Clampi(8, 1024, steps * 3));
		VectorSet4(settings4,
			hitCache ? 1.0f : 0.0f,
			velocity ? 1.0f : 0.0f,
			(float)(frame & 3),
			phase);

		uint32_t traceState = GLS_DEPTHTEST_DISABLE;
		if ( r_ssrCull->integer )
		{
			// the pixels that need a ray get the nearest depth, the trace is
			// drawn behind them: everything else is rejected before shading
			// and keeps the cleared miss
			shaderProgram_t *sp = &tr.ssrTraceShader[SSRDEF_CLASSIFY];
			RB_ScreenBeginPass(traceFbo, sp, traceWidth, traceHeight,
				GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_LESS);
			const float clearDepth = 1.0f;
			qglClearBufferfv(GL_COLOR, 0, colorBlack);
			qglClearBufferfv(GL_DEPTH, 0, &clearDepth);
			// clears honor the masks: the color mask goes off after them
			GL_State(GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_LESS | GLS_COLORMASK_BITS);
			RB_SSRBindMaterial();
			RB_ScreenSetViewUniforms(sp, info);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS3, settings3);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS4, settings4);
			RB_InstantTriangle();

			traceState = GLS_DEPTHFUNC_GREATER;
		}

		shaderProgram_t *sp = &tr.ssrTraceShader[hiZ ? SSRDEF_TRACE_HIZ : SSRDEF_TRACE];
		RB_ScreenBeginPass(traceFbo, sp, traceWidth, traceHeight, traceState);
		RB_SSRBindMaterial();
		GL_BindToTMU(tr.ssrTraceImage[previous], TB_SSR_PREVHIT);
		GL_BindToTMU(velocity ? tr.velocityImage : tr.whiteImage, TB_SSAOMAP);
		RB_ScreenSetViewUniforms(sp, info);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRREPROJECT, reproject, 1);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRPREVVIEWTOVIEW, prevViewToView, 1);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS3, settings3);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS4, settings4);
		RB_InstantTriangle();
	}
	RB_ScreenEndTimer(timer);

	image_t *traceImage = tr.ssrTraceImage[current];

	// hit -> radiance
	timer = RB_ScreenBeginTimer("SSR resolve");
	{
		shaderProgram_t *sp = &tr.ssrResolveShader;
		RB_ScreenBeginPass(tr.ssrResolveFbo, sp, width, height);
		RB_SSRBindMaterial();
		GL_BindToTMU(tr.ssrColorImage, TB_SPECULARMAP);
		GL_BindToTMU(traceImage, TB_SHADOWMAP);
		RB_ScreenSetViewUniforms(sp, info);

		vec4_t texelSize, settings, settings2;
		RB_ScreenTexelSize(texelSize, width, height, width, height);
		VectorSet4(settings, traceScale, maxDistance, (float)(SSR_COLOR_MIPS - 1), 0.0f);
		VectorSet4(settings2, r_ssrMaxRoughness->value, phase, 0.0f, 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);
		RB_InstantTriangle();
	}
	RB_ScreenEndTimer(timer);

	image_t *finalImage = tr.ssrResolveImage;

	// temporal accumulation
	if ( temporal )
	{
		timer = RB_ScreenBeginTimer("SSR temporal");

		shaderProgram_t *sp = &tr.ssrTemporalShader;
		RB_ScreenBeginPass(tr.ssrHistoryFbo[current], sp, width, height);
		RB_SSRBindMaterial();
		GL_BindToTMU(tr.ssrResolveImage, TB_SHADOWMAP);
		GL_BindToTMU(tr.ssrHistoryImage[previous], TB_CUBEMAP);
		GL_BindToTMU(tr.ssrHistoryGeomImage[previous], TB_ENVBRDFMAP);
		GL_BindToTMU(velocity ? tr.velocityImage : tr.whiteImage, TB_SSAOMAP);
		GL_BindToTMU(traceImage, TB_SSR_HIT);
		GL_BindToTMU(tr.ssrTraceImage[previous], TB_SSR_PREVHIT);
		RB_ScreenSetViewUniforms(sp, info);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRREPROJECT, reproject, 1);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRPREVVIEWTOVIEW, prevViewToView, 1);

		vec4_t texelSize, settings;
		RB_ScreenTexelSize(texelSize, width, height, width, height);
		VectorSet4(settings,
			historyValid ? 1.0f : 0.0f,
			r_ssrTemporalWeight->value,
			velocity ? 1.0f : 0.0f,
			traceScale);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
		RB_InstantTriangle();

		finalImage = tr.ssrHistoryImage[current];

		RB_ScreenEndTimer(timer);
	}

	// the trace (and history) of this frame are the next frame's history
	RB_ScreenStoreHistory(s_history, info, traceScale, current);
	Com_Memcpy(s_prevViewToWorld, info.viewToWorld, sizeof(matrix_t));

	// composite: replace the cubemap reflection where the SSR is reliable
	timer = RB_ScreenBeginTimer("SSR composite");
	{
		const int debugView = r_ssrDebug->integer;
		const qboolean sceneDebug = (qboolean)(debugView >= 7 && debugView <= 11);
		const float splitX = r_ssrCompare->integer ?
			viewParms.viewportX + 0.5f * viewParms.viewportWidth : -1.0f;

		shaderProgram_t *sp = &tr.ssrCompositeShader;
		FBO_Bind(tr.screenCompositeFbo);
		GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
			viewParms.viewportWidth, viewParms.viewportHeight);
		GL_Cull(CT_TWO_SIDED);
		GLSL_BindProgram(sp);
		RB_SSRBindMaterial();
		GL_BindToTMU(finalImage, TB_SHADOWMAP);
		GL_BindToTMU(traceImage, TB_CUBEMAP);
		RB_ScreenSetViewUniforms(sp, info);

		// light sabers and effects
		vec4_t emitterData[SSR_MAX_EMITTERS * 3];
		const int numEmitters = RB_SSRCollectEmitters(info, emitterData);
		if ( numEmitters > 0 )
			GLSL_SetUniformVec4N(sp, UNIFORM_SSREMITTERS, emitterData[0], numEmitters * 3);
		vec4_t emitterParams, settings2;
		VectorSet4(emitterParams, (float)numEmitters, 0.0f, r_ssrEmitterMaxRoughness->value, 0.0f);
		VectorSet4(settings2, traceScale, maxDistance, 0.0f, 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SSREMITTERPARAMS, emitterParams);
		GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS2, settings2);

		vec4_t settings;
		const float strength = Com_Clamp(0.0f, 1.0f, r_ssrStrength->value);
		if ( sceneDebug )
		{
			GL_State(GLS_DEPTHTEST_DISABLE);
			VectorSet4(settings, strength, splitX, (float)debugView, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();
		}
		else if ( r_hdr->integer )
		{
			// float target: one signed additive pass
			GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
			VectorSet4(settings, strength, splitX, 0.0f, 0.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();
		}
		else
		{
			// normalized target: blending clamps negative sources, subtract
			// the replaced cubemap part, then add the SSR part
			GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
			qglBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
			VectorSet4(settings, strength, splitX, 0.0f, 1.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();

			qglBlendEquation(GL_FUNC_ADD);
			VectorSet4(settings, strength, splitX, 0.0f, 2.0f);
			GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);
			RB_InstantTriangle();
		}
	}
	RB_ScreenEndTimer(timer);

	s_debug.frameNumber = backEndData->realFrameNumber;
	s_debug.finalImage = finalImage;
	s_debug.traceImage = traceImage;
	s_debug.info = info;
	s_debug.traceScale = traceScale;
	s_debug.maxDistance = maxDistance;
	s_debug.valid = qtrue;
}

/*
============================================================

Debug views

============================================================
*/

// r_ssrDebug 1-6 and 12: buffers drawn over the final image at the end of the
// post process chain (7-11 replace the scene color in RB_RenderSSR and go
// through the tone mapping)
void RB_SSRDebugOverlay( void )
{
	const int debugView = r_ssrDebug->integer;
	if ( !s_ssrResources || debugView < 1 || (debugView > 6 && debugView != 12) )
		return;

	if ( !s_debug.valid || s_debug.frameNumber != backEndData->realFrameNumber || !s_debug.finalImage )
		return;

	FBO_Bind(NULL);
	GL_SetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	shaderProgram_t *sp = &tr.ssrDebugShader;
	GLSL_BindProgram(sp);
	RB_SSRBindMaterial();
	GL_BindToTMU(s_debug.traceImage, TB_SHADOWMAP);
	GL_BindToTMU(s_debug.finalImage, TB_CUBEMAP);
	RB_ScreenSetViewUniforms(sp, s_debug.info);

	vec4_t settings;
	VectorSet4(settings, (float)debugView, s_debug.traceScale, s_debug.maxDistance, 0.0f);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRSETTINGS, settings);

	RB_InstantTriangle();
	GL_SelectTexture(0);
}
