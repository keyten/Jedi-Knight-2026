/*
===========================================================================
Copyright (C) 2026 OpenJK contributors
This file is part of the OpenJK source code under GPL version 2.
===========================================================================
*/

#include "tr_local.h"

static qboolean s_resources = qfalse;
static screenHistory_t s_history = {};
static vec3_t s_previousCenters[LTC_SCREEN_SABERS];
static vec3_t s_previousAxes[LTC_SCREEN_SABERS];
static float s_previousHalfLengths[LTC_SCREEN_SABERS];
static unsigned s_readyFrame = ~0u;
static int s_readyView = -1;
static int s_readyScene = -1;
static int s_lastRays = 0;
static int s_lastHistoryChannels = 0;

void R_LtcSaberScreenInvalidate(void)
{
	s_history.valid = qfalse;
	s_readyFrame = ~0u;
	s_readyView = s_readyScene = -1;
	s_lastRays = s_lastHistoryChannels = 0;
}

void R_LtcSaberScreenStats_f(void)
{
	ri.Printf(PRINT_ALL,
		"LTC saber screen: up to %d rays at trace resolution, %d temporal channels accepted by CPU cuts\n",
		s_lastRays, s_lastHistoryChannels);
}

qboolean R_LtcSaberScreenResourcesEnabled(void)
{
	return s_resources;
}

void R_LtcSaberScreenSelectResources(void)
{
	GLint samplers = 0;
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &samplers);
	s_resources = (qboolean)(r_ltcAreaLights->integer && samplers > TB_LTC_SABER_SCREEN);
	R_LtcSaberScreenInvalidate();
}

void R_CreateLtcSaberScreenImages(int width, int height)
{
	tr.ltcSaberTraceImage = NULL;
	tr.ltcSaberScreenImage = NULL;
	for (int i = 0; i < 2; ++i)
	{
		tr.ltcSaberHistoryImage[i] = NULL;
		tr.ltcSaberHistoryDepthImage[i] = NULL;
	}
	if (!s_resources)
		return;
	tr.ltcSaberTraceImage = R_ScreenCreateImage("*ltcSaberTrace",
		Q_max(1, width / 2), Q_max(1, height / 2), GL_RGBA16F, qfalse);
	for (int i = 0; i < 2; ++i)
	{
		tr.ltcSaberHistoryImage[i] = R_ScreenCreateImage(
			va("*ltcSaberHistory%d", i), width, height, GL_RGBA16F, qfalse);
		tr.ltcSaberHistoryDepthImage[i] = R_ScreenCreateImage(
			va("*ltcSaberHistoryDepth%d", i), width, height, GL_R32F, qfalse);
	}
	tr.ltcSaberScreenImage = R_ScreenCreateImage("*ltcSaberScreen",
		width, height, GL_RGBA16F, qfalse);
	R_LtcSaberScreenInvalidate();
}

void R_CreateLtcSaberScreenFBOs(void)
{
	tr.ltcSaberTraceFbo = NULL;
	tr.ltcSaberScreenFbo = NULL;
	for (int i = 0; i < 2; ++i)
		tr.ltcSaberHistoryFbo[i] = NULL;
	if (!tr.ltcSaberScreenImage)
		return;
	tr.ltcSaberTraceFbo = R_ScreenCreateLevelFBO("_ltcSaberTrace",
		tr.ltcSaberTraceImage, 0);
	for (int i = 0; i < 2; ++i)
		tr.ltcSaberHistoryFbo[i] = R_ScreenCreatePairFBO(
			va("_ltcSaberHistory%d", i), tr.ltcSaberHistoryImage[i],
			tr.ltcSaberHistoryDepthImage[i]);
	tr.ltcSaberScreenFbo = R_ScreenCreateLevelFBO("_ltcSaberScreen",
		tr.ltcSaberScreenImage, 0);
}

qboolean RB_LtcSaberScreenReady(void)
{
	return (qboolean)(s_resources && tr.ltcSaberScreenImage &&
		r_ltcSaberShadows->integer == 2 &&
		s_readyFrame == backEndData->realFrameNumber &&
		s_readyScene == backEndData->currentFrame->currentScene &&
		s_readyView == backEnd.viewParms.currentViewParm &&
		backEnd.viewParms.viewParmType == VPT_MAIN);
}

void RB_RenderLtcSaberScreen(const screenViewInfo_t& info)
{
	if (!s_resources || !tr.ltcSaberTraceFbo ||
		backEnd.viewParms.viewParmType != VPT_MAIN)
		return;
	vec4_t saber[4] = {};
	vec3_t centers[LTC_SCREEN_SABERS] = {};
	vec3_t axes[LTC_SCREEN_SABERS] = {};
	float halfLengths[LTC_SCREEN_SABERS] = {};
	int active = 0;
	s_lastHistoryChannels = 0;
	const qboolean cameraHistory = RB_ScreenHistoryValid(s_history, 0.5f);
	for (int i = 0; i < backEnd.refdef.num_dlights; ++i)
	{
		const dlight_t *light = &backEnd.refdef.dlights[i];
		if (light->areaType != DLIGHT_LINE ||
			light->areaDynamicShadowSlot < 0 ||
			light->areaDynamicShadowSlot >= LTC_SCREEN_SABERS)
			continue;
		const int channel = light->areaDynamicShadowSlot;
		VectorCopy(light->origin, centers[channel]);
		VectorCopy(light->areaRight, axes[channel]);
		halfLengths[channel] = light->halfWidth;
		VectorSet4(saber[channel * 2], light->origin[0], light->origin[1],
			light->origin[2], light->halfWidth);
		float historyWeight = 0.0f;
		if (cameraHistory &&
			Distance(light->origin, s_previousCenters[channel]) < 48.0f &&
			DotProduct(light->areaRight, s_previousAxes[channel]) > 0.8f &&
			fabsf(light->halfWidth - s_previousHalfLengths[channel]) < 12.0f)
		{
			const float move = Distance(light->origin, s_previousCenters[channel]);
			historyWeight = 0.88f * expf(-move / 24.0f) *
				Q_max(0.0f, DotProduct(light->areaRight, s_previousAxes[channel]));
		}
		VectorSet4(saber[channel * 2 + 1], light->areaRight[0],
			light->areaRight[1], light->areaRight[2], historyWeight);
		if (historyWeight > 0.0f)
			++s_lastHistoryChannels;
		++active;
	}
	if (!active)
	{
		s_lastRays = 0;
		s_readyFrame = ~0u;
		s_history.valid = qfalse;
		return;
	}
	const int width = tr.renderFbo->width;
	const int height = tr.renderFbo->height;
	s_lastRays = active * tr.ltcSaberTraceFbo->width * tr.ltcSaberTraceFbo->height;
	vec4_t params;
	VectorSet4(params, (float)(backEndData->realFrameNumber & 255),
		1.0f / width, 1.0f / height,
		cameraHistory ? (RB_ScreenVelocityValid() ? 2.0f : 1.0f) : 0.0f);
	vec4_t texelSize;
	RB_ScreenTexelSize(texelSize, width, height,
		tr.ltcSaberTraceFbo->width, tr.ltcSaberTraceFbo->height);
	const int previous = s_history.current;
	const int current = previous ^ 1;
	matrix_t reproject;
	Matrix16Multiply(cameraHistory ? s_history.viewProjection : info.viewProjection,
		info.viewToWorld, reproject);

	int timer = RB_ScreenBeginTimer("LTC saber screen trace");
	shaderProgram_t *sp = &tr.ltcSaberScreenShader[0];
	RB_ScreenBeginPass(tr.ltcSaberTraceFbo, sp,
		tr.ltcSaberTraceFbo->width, tr.ltcSaberTraceFbo->height);
	GL_BindToTMU(tr.screenHiZImage, TB_SHADOWMAPARRAY);
	RB_ScreenSetViewUniforms(sp, info);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSCREENPARAMS, params);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABER0, saber[0]);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABERAXIS0, saber[1]);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABER1, saber[2]);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABERAXIS1, saber[3]);
	RB_InstantTriangle();
	RB_ScreenEndTimer(timer);

	timer = RB_ScreenBeginTimer("LTC saber temporal");
	sp = &tr.ltcSaberScreenShader[1];
	RB_ScreenBeginPass(tr.ltcSaberHistoryFbo[current], sp, width, height);
	GL_BindToTMU(tr.screenHiZImage, TB_SHADOWMAPARRAY);
	GL_BindToTMU(tr.ltcSaberTraceImage, TB_SHADOWMAP);
	GL_BindToTMU(tr.ltcSaberHistoryImage[previous], TB_CUBEMAP);
	GL_BindToTMU(tr.ltcSaberHistoryDepthImage[previous], TB_ENVBRDFMAP);
	GL_BindToTMU(RB_ScreenVelocityValid() ? tr.velocityImage : tr.whiteImage, TB_SSAOMAP);
	RB_ScreenSetViewUniforms(sp, info);
	GLSL_SetUniformMatrix4x4(sp, UNIFORM_SSRREPROJECT, reproject, 1);
	GLSL_SetUniformVec4(sp, UNIFORM_SSRTEXELSIZE, texelSize);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSCREENPARAMS, params);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABERAXIS0, saber[1]);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSABERAXIS1, saber[3]);
	RB_InstantTriangle();
	RB_ScreenEndTimer(timer);

	timer = RB_ScreenBeginTimer("LTC saber filter");
	sp = &tr.ltcSaberScreenShader[2];
	RB_ScreenBeginPass(tr.ltcSaberScreenFbo, sp, width, height);
	GL_BindToTMU(tr.screenHiZImage, TB_SHADOWMAPARRAY);
	GL_BindToTMU(tr.ltcSaberHistoryImage[current], TB_SHADOWMAP);
	RB_ScreenSetViewUniforms(sp, info);
	GLSL_SetUniformVec4(sp, UNIFORM_LTCSCREENPARAMS, params);
	RB_InstantTriangle();
	RB_ScreenEndTimer(timer);

	RB_ScreenStoreHistory(s_history, info, 0.5f, current);
	for (int i = 0; i < LTC_SCREEN_SABERS; ++i)
	{
		VectorCopy(centers[i], s_previousCenters[i]);
		VectorCopy(axes[i], s_previousAxes[i]);
		s_previousHalfLengths[i] = halfLengths[i];
	}
	s_readyFrame = backEndData->realFrameNumber;
	s_readyView = backEnd.viewParms.currentViewParm;
	s_readyScene = backEndData->currentFrame->currentScene;
}
