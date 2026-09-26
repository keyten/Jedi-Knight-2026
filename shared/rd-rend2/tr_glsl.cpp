/*
===========================================================================
Copyright (C) 2006-2009 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_glsl.c
#include "tr_local.h"
#include "tr_allocator.h"
#include "glsl_shaders.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

void GLSL_BindNullProgram(void);

const uniformBlockInfo_t uniformBlocksInfo[UNIFORM_BLOCK_COUNT] = {
	{ 0, "Camera", sizeof(CameraBlock) },
	{ 1, "Scene", sizeof(SceneBlock) },
	{ 2, "Lights", sizeof(LightsBlock) },
	{ 3, "Fogs", sizeof(FogsBlock) },
	{ 4, "Entity", sizeof(EntityBlock) },
	{ 5, "PreviousEntity", sizeof(EntityBlock) },
	{ 6, "ShaderInstance", sizeof(ShaderInstanceBlock) },
	{ 7, "Bones", sizeof(SkeletonBoneMatricesBlock) },
	{ 8, "PreviousBones", sizeof(SkeletonBoneMatricesBlock) },
	{ 9, "TemporalInfo", sizeof(TemporalBlock) },
	{ 10, "SurfaceSprite", sizeof(SurfaceSpriteBlock) },
	{ 11, "VolumetricFog", sizeof(VolumetricFogBlock) },
	{ 12, "FoliageInteraction", sizeof(FoliageInteractionBlock) },
};

typedef struct uniformInfo_s
{
	const char *name;
	int type;
	int size;
}
uniformInfo_t;

// These must be in the same order as in uniform_t in tr_local.h.
static uniformInfo_t uniformsInfo[] =
{
	{ "u_DiffuseMap",  GLSL_INT, 1 },
	{ "u_LightMap",    GLSL_INT, 1 },
	{ "u_NormalMap",   GLSL_INT, 1 },
	{ "u_DeluxeMap",   GLSL_INT, 1 },
	{ "u_SpecularMap", GLSL_INT, 1 },
	{ "u_SSAOMap",     GLSL_INT, 1 },
	{ "u_EmissiveMap", GLSL_INT, 1 },

	{ "u_TextureMap", GLSL_INT, 1 },
	{ "u_LevelsMap",  GLSL_INT, 1 },
	{ "u_CubeMap",    GLSL_INT, 1 },
	{ "u_EnvBrdfMap", GLSL_INT, 1 },
	{ "u_DiffuseIrradianceMap", GLSL_INT, 1 },
	{ "u_ProbeAverageMap", GLSL_INT, 1 },

	{ "u_ScreenImageMap", GLSL_INT, 1 },
	{ "u_ScreenDepthMap", GLSL_INT, 1 },

	{ "u_EdgeMap", GLSL_INT, 1 },
	{ "u_AreaMap", GLSL_INT, 1 },
	{ "u_SearchMap", GLSL_INT, 1 },
	{ "u_BlendMap", GLSL_INT, 1 },
	{ "u_VelocityMap", GLSL_INT, 1 },

	{ "u_VolumetricLightMap", GLSL_INT, 1 },

	{ "u_LightGridOrigin", GLSL_VEC3, 1 },
	{ "u_LightGridCellInverseSize", GLSL_VEC3, 1 },
	{ "u_EntityGridAmbient", GLSL_INT, 1 },
	{ "u_EntityGridDirected", GLSL_INT, 1 },
	{ "u_EntityGridDirection", GLSL_INT, 1 },

	{ "u_ShadowMap",  GLSL_INT, 1 },
	{ "u_ShadowMap2", GLSL_INT, 1 },

	{ "u_ShadowMvp",  GLSL_MAT4x4, 1 },
	{ "u_ShadowMvp2", GLSL_MAT4x4, 1 },
	{ "u_ShadowMvp3", GLSL_MAT4x4, 1 },

	{ "u_EnableTextures", GLSL_VEC4, 1 },
	{ "u_EmissiveParams", GLSL_VEC4, 1 },

	{ "u_DiffuseTexMatrix",  GLSL_VEC4, 1 },
	{ "u_DiffuseTexOffTurb", GLSL_VEC4, 1 },

	{ "u_TCGen0",        GLSL_INT, 1 },
	{ "u_TCGen0Vector0", GLSL_VEC3, 1 },
	{ "u_TCGen0Vector1", GLSL_VEC3, 1 },
	{ "u_TCGen1",        GLSL_INT, 1 },

	{ "u_ColorGen",  GLSL_INT, 1 },
	{ "u_AlphaGen",  GLSL_INT, 1 },
	{ "u_Color",     GLSL_VEC4, 1 },
	{ "u_BaseColor", GLSL_VEC4, 1 },
	{ "u_VertColor", GLSL_VEC4, 1 },
	{ "u_chromaticAberrationDelta", GLSL_FLOAT, 1 },

	{ "u_LightForward",   GLSL_VEC3, 1 },
	{ "u_LightUp",        GLSL_VEC3, 1 },
	{ "u_LightRight",     GLSL_VEC3, 1 },
	{ "u_LightOrigin",    GLSL_VEC4, 1 },
	{ "u_LightRadius",    GLSL_FLOAT, 1 },
	{ "u_Disintegration", GLSL_VEC4, 1 },
	{ "u_LightMask",    GLSL_INT, 1 },
	{ "u_FogIndex",    GLSL_INT, 1 },

	{ "u_FogColorMask", GLSL_VEC4, 1 },

	{ "u_ModelViewProjectionMatrix", GLSL_MAT4x4, 1 },
	{ "u_SpriteViewOrigin", GLSL_VEC3, 1 },
	{ "u_SpriteViewLeft",   GLSL_VEC3, 1 },
	{ "u_SpriteViewUp",     GLSL_VEC3, 1 },

	{ "u_VertexLerp" ,   GLSL_FLOAT, 1 },
	{ "u_NormalScale",   GLSL_VEC4, 1 },
	{ "u_SpecularScale", GLSL_VEC4, 1 },
	{ "u_MaterialDebug",  GLSL_VEC4, 1 },
	{ "u_FoliageDebug",   GLSL_VEC4, 1 },
	{ "u_AutoGrass",      GLSL_VEC4, 1 },
	{ "u_FoliageWind",    GLSL_VEC4, 1 },
	{ "u_FoliageWindParams", GLSL_VEC4, 1 },
	{ "u_LeafFlutter",       GLSL_VEC4, 1 },
	{ "u_LeafFlutterParams", GLSL_VEC4, 1 },
	{ "u_LeafFlutterDebug",  GLSL_FLOAT, 1 },
	{ "u_PlantBend",         GLSL_VEC4, 1 },
	{ "u_PlantBendParams",   GLSL_VEC4, 1 },
	{ "u_PlantBendTime",     GLSL_VEC4, 1 },
	{ "u_FoliageInteract",   GLSL_VEC4, 1 },
	{ "u_DiffuseBRDF",    GLSL_INT,  1 },
	{ "u_ParallaxBias",  GLSL_FLOAT, 1 },

	{ "u_ViewInfo",				GLSL_VEC4, 1 },

	{ "u_InvTexRes",           GLSL_VEC2, 1 },
	{ "u_AutoExposureMinMax",  GLSL_VEC2, 1 },
	{ "u_ToneMinAvgMaxLinear", GLSL_VEC3, 1 },
	{ "u_ToneMapParams",       GLSL_VEC4, 1 },
	{ "u_ColorGradingLut",     GLSL_INT,  1 },
	{ "u_ColorGradingParams",  GLSL_VEC4, 1 },

	{ "u_CubeMapInfo", GLSL_VEC4, 1 },
	{ "u_DiffuseIBLParams", GLSL_VEC4, 1 },

	{ "u_AlphaTestType",		GLSL_INT, 1 },

	{ "u_MapZExtents",			GLSL_VEC2, 1 },
	{ "u_ZoneOffset",			GLSL_VEC2, 9 },
	{ "u_EnvForce",				GLSL_VEC3, 1 },
	{ "u_RandomOffset",			GLSL_VEC4, 1 },
	{ "u_ChunkParticles",		GLSL_INT, 1 },
	{ "u_BloomStrength",		GLSL_FLOAT, 1 },
	{ "u_BloomMap",			GLSL_INT, 1 },
	{ "u_BloomParams",			GLSL_VEC4, 1 },
	{ "u_BloomSceneMap",		GLSL_INT, 1 },

	{ "u_AODepthMap",			GLSL_INT, 1 },
	{ "u_AOMap",				GLSL_INT, 1 },
	{ "u_LegacyAOMap",			GLSL_INT, 1 },
	{ "u_AOProjection",			GLSL_VEC4, 1 },
	{ "u_AODepthParams",		GLSL_VEC4, 1 },
	{ "u_AOViewport",			GLSL_VEC4, 1 },
	{ "u_AOTexelSize",			GLSL_VEC4, 1 },
	{ "u_AOSettings",			GLSL_VEC4, 1 },
	{ "u_AOSettings2",			GLSL_VEC4, 1 },
	{ "u_AOSettings3",			GLSL_VEC4, 1 },
	{ "u_AOLightDir",			GLSL_VEC3, 1 },
	{ "u_AOBentMap",			GLSL_INT, 1 },
	{ "u_AOViewToWorld",		GLSL_MAT4x4, 1 },

	{ "u_MBInvViewProjection",	GLSL_MAT4x4, 1 },
	{ "u_MBPrevViewProjection",	GLSL_MAT4x4, 1 },
	{ "u_MBParams",				GLSL_VEC4, 1 },
	{ "u_MBParams2",			GLSL_VEC4, 1 },
	{ "u_MBParams3",			GLSL_VEC4, 1 },

	{ "u_SSRNormalMap",			GLSL_INT, 1 },
	{ "u_SSRSpecularMap",		GLSL_INT, 1 },
	{ "u_SSRCubemapMap",		GLSL_INT, 1 },
	{ "u_SSRSceneMap",			GLSL_INT, 1 },
	{ "u_SSRTraceMap",			GLSL_INT, 1 },
	{ "u_SSRHistoryMap",		GLSL_INT, 1 },
	{ "u_SSRHistoryGeomMap",	GLSL_INT, 1 },
	{ "u_SSRHiZMap",			GLSL_INT, 1 },
	{ "u_SSRProjection",		GLSL_VEC4, 1 },
	{ "u_SSRDepthParams",		GLSL_VEC4, 1 },
	{ "u_SSRViewport",			GLSL_VEC4, 1 },
	{ "u_SSRTexelSize",			GLSL_VEC4, 1 },
	{ "u_SSRSettings",			GLSL_VEC4, 1 },
	{ "u_SSRSettings2",			GLSL_VEC4, 1 },
	{ "u_SSRSettings3",			GLSL_VEC4, 1 },
	{ "u_SSRWorldToView",		GLSL_MAT4x4, 1 },
	{ "u_SSRReproject",			GLSL_MAT4x4, 1 },
	{ "u_SSREmitters",			GLSL_VEC4, SSR_MAX_EMITTERS * 3 },
	{ "u_SSREmitterParams",		GLSL_VEC4, 1 },
	{ "u_SSGIAlbedoMap",		GLSL_INT, 1 },
	{ "u_SSGIRadianceMap",		GLSL_INT, 1 },
	{ "u_SSGISourceMap",		GLSL_INT, 1 },

	{ "u_FroxelFogMode",		GLSL_INT, 1 },
	{ "u_FroxelVolume",			GLSL_INT, 1 },
	{ "u_FroxelTail",			GLSL_INT, 1 },
	{ "u_FroxelSource",			GLSL_INT, 1 },
	{ "u_FroxelHistory",		GLSL_INT, 1 },
	{ "u_FroxelDynamic",		GLSL_INT, 1 },
	{ "u_FroxelCarry",			GLSL_INT, 1 },
	{ "u_VolumetricStaticGrid",	GLSL_INT, 1 },
	{ "u_VolumetricSunGrid",	GLSL_INT, 1 },
	{ "u_FroxelSlice",			GLSL_INT, 1 },
	{ "u_FroxelNoise",			GLSL_INT, 1 },

	{ "u_FPlusLights",			GLSL_INT, 1 },
	{ "u_FPlusGridMap",			GLSL_INT, 1 },
	{ "u_FPlusIndexMap",		GLSL_INT, 1 },

	{ "u_PomGroups",			GLSL_INT, 1 },
	{ "u_PomParams",			GLSL_VEC4, 1 },
	{ "u_PomParams2",			GLSL_VEC4, 1 },
	{ "u_PomFade",				GLSL_VEC4, 1 },

	{ "u_PomShadow",			GLSL_VEC4, 1 },
	{ "u_PomTraversal",			GLSL_VEC4, 1 },
	{ "u_PomLod",				GLSL_VEC4, 1 },
	{ "u_PomDebug",				GLSL_VEC4, 1 },

	{ "u_LtcMatrixMap",			GLSL_INT, 1 },
	{ "u_LtcAmplitudeMap",		GLSL_INT, 1 },

	{ "u_WeatherDepthMap",		GLSL_INT, 1 },
	{ "u_WeatherMvp",			GLSL_MAT4x4, 1 },
	{ "u_WetnessParams",		GLSL_VEC4, 1 },
	{ "u_WetnessParams2",		GLSL_VEC4, 1 },
	{ "u_WetnessParams3",		GLSL_VEC4, 1 },
	{ "u_PuddleParams",		GLSL_VEC4, 1 },
	{ "u_PuddleParams2",		GLSL_VEC4, 1 },
	{ "u_PuddleHeight",		GLSL_VEC4, 1 },
	{ "u_PuddleRipple",		GLSL_VEC4, 1 },
	{ "u_RunoffParams",		GLSL_VEC4, 1 },
	{ "u_RunoffParams2",		GLSL_VEC4, 1 },
	{ "u_RunoffFrame",		GLSL_VEC4, 1 },

	{ "u_SkinParams",			GLSL_VEC4, 1 },
	{ "u_SkinWrap",				GLSL_VEC4, 1 },
	{ "u_SkinMaskMap",			GLSL_INT, 1 },
	{ "u_SkinKernel",			GLSL_VEC4, SKIN_SSS_MAX_TAPS },
	{ "u_SkinSettings",			GLSL_VEC4, 1 },
	{ "u_SkinSettings2",		GLSL_VEC4, 1 },

	{ "u_WeatherType",			GLSL_INT, 1 },
	{ "u_RainStreak",			GLSL_VEC4, 1 },
	{ "u_RainShade",			GLSL_VEC4, 1 },
	{ "u_RainLight",			GLSL_VEC4, 1 },
	{ "u_CameraVelocity",		GLSL_VEC3, 1 },

	{ "u_WeatherSurfaceMap",	GLSL_INT, 1 },
	{ "u_SplashParams",			GLSL_VEC4, 1 },
	{ "u_SplashParams2",		GLSL_VEC4, 1 },

	{ "u_FoliageFieldMap",		GLSL_INT, 1 },
	{ "u_FoliageFieldPrevMap",	GLSL_INT, 1 },
	{ "u_FoliageFieldDebug",	GLSL_VEC4, 1 },
};

static_assert(ARRAY_LEN(uniformsInfo) == UNIFORM_COUNT,
	"uniformsInfo must stay in sync with uniform_t");

static void GLSL_PrintProgramInfoLog(GLuint object, qboolean developerOnly)
{
	char msgPart[1024];
	int maxLength = 0;
	int printLevel = developerOnly ? PRINT_DEVELOPER : PRINT_ALL;

	qglGetProgramiv(object, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength <= 0)
	{
		ri.Printf(printLevel, "No compile log.\n");
		return;
	}

	ri.Printf(printLevel, "compile log:\n");

	if (maxLength < 1023)
	{
		qglGetProgramInfoLog(object, maxLength, &maxLength, msgPart);

		msgPart[maxLength + 1] = '\0';

		ri.Printf(printLevel, "%s\n", msgPart);
	}
	else
	{
		char *msg = (char *)R_Malloc(maxLength, TAG_SHADERTEXT);

		qglGetProgramInfoLog(object, maxLength, &maxLength, msg);

		for(int i = 0; i < maxLength; i += 1023)
		{
			Q_strncpyz(msgPart, msg + i, sizeof(msgPart));

			ri.Printf(printLevel, "%s\n", msgPart);
		}

		Z_Free(msg);
	}
}

static void GLSL_PrintShaderInfoLog(GLuint object, qboolean developerOnly)
{
	char           *msg;
	static char     msgPart[1024];
	int             maxLength = 0;
	int             i;
	int             printLevel = developerOnly ? PRINT_DEVELOPER : PRINT_ALL;

	qglGetShaderiv(object, GL_INFO_LOG_LENGTH, &maxLength);

	if (maxLength <= 0)
	{
		ri.Printf(printLevel, "No compile log.\n");
		return;
	}

	ri.Printf(printLevel, "compile log:\n");

	if (maxLength < 1023)
	{
		qglGetShaderInfoLog(object, maxLength, &maxLength, msgPart);

		msgPart[maxLength + 1] = '\0';

		ri.Printf(printLevel, "%s\n", msgPart);
	}
	else
	{
		msg = (char *)R_Malloc(maxLength, TAG_SHADERTEXT);

		qglGetShaderInfoLog(object, maxLength, &maxLength, msg);

		for(i = 0; i < maxLength; i += 1024)
		{
			Q_strncpyz(msgPart, msg + i, sizeof(msgPart));

			ri.Printf(printLevel, "%s\n", msgPart);
		}

		Z_Free(msg);
	}
}

static void GLSL_PrintShaderSource(GLuint shader)
{
	int maxLength = 0;
	qglGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &maxLength);

	if ( maxLength == 0 )
	{
		Com_Printf("No shader source available to output\n");
		return;
	}

	char *msg = (char *)R_Malloc(maxLength, TAG_SHADERTEXT);
	qglGetShaderSource(shader, maxLength, nullptr, msg);

	for (int i = 0; i < maxLength; i += 1023)
	{
		char msgPart[1024];
		Q_strncpyz(msgPart, msg + i, sizeof(msgPart));
		ri.Printf(PRINT_ALL, "%s\n", msgPart);
	}

	Z_Free(msg);
}

static size_t GLSL_GetShaderHeader(
	GLenum shaderType,
	const GLcharARB *extra,
	const GPUShaderDesc *library,
	int firstLineNumber,
	char *dest,
	size_t size)
{
	float fbufWidthScale, fbufHeightScale;

	dest[0] = '\0';

	Q_strcat(dest, size, "#version 150 core\n");

	Q_strcat(dest, size,
					"#ifndef M_PI\n"
					"#define M_PI 3.14159265358979323846\n"
					"#endif\n");

	Q_strcat(dest, size,
					 va("#ifndef deformGen_t\n"
						"#define deformGen_t\n"
						"#define DEFORM_NONE %i\n"
						"#define DEFORM_WAVE %i\n"
						"#define DEFORM_NORMALS %i\n"
						"#define DEFORM_BULGE %i\n"
						"#define DEFORM_BULGE_UNIFORM %i\n"
						"#define DEFORM_MOVE %i\n"
						"#define DEFORM_PROJECTION_SHADOW %i\n"
						"#define DEFORM_DISINTEGRATION %i\n"
						"#define WF_NONE %i\n"
						"#define WF_SIN %i\n"
						"#define WF_SQUARE %i\n"
						"#define WF_TRIANGLE %i\n"
						"#define WF_SAWTOOTH %i\n"
						"#define WF_INVERSE_SAWTOOTH %i\n"
						"#endif\n",
						DEFORM_NONE,
						DEFORM_WAVE,
						DEFORM_NORMALS,
						DEFORM_BULGE,
						DEFORM_BULGE_UNIFORM,
						DEFORM_MOVE,
						DEFORM_PROJECTION_SHADOW,
						DEFORM_DISINTEGRATION,
						GF_NONE,
						GF_SIN,
						GF_SQUARE,
						GF_TRIANGLE,
						GF_SAWTOOTH,
						GF_INVERSE_SAWTOOTH));

	Q_strcat(dest, size,
					 va("#ifndef tcGen_t\n"
						"#define tcGen_t\n"
						"#define TCGEN_LIGHTMAP %i\n"
						"#define TCGEN_LIGHTMAP1 %i\n"
						"#define TCGEN_LIGHTMAP2 %i\n"
						"#define TCGEN_LIGHTMAP3 %i\n"
						"#define TCGEN_TEXTURE %i\n"
						"#define TCGEN_ENVIRONMENT_MAPPED %i\n"
						"#define TCGEN_ENVIRONMENT_MAPPED_SP %i\n"
						"#define TCGEN_ENVIRONMENT_MAPPED_SP_FP %i\n"
						"#define TCGEN_FOG %i\n"
						"#define TCGEN_VECTOR %i\n"
						"#endif\n",
						TCGEN_LIGHTMAP,
						TCGEN_LIGHTMAP1,
						TCGEN_LIGHTMAP2,
						TCGEN_LIGHTMAP3,
						TCGEN_TEXTURE,
						TCGEN_ENVIRONMENT_MAPPED,
						TCGEN_ENVIRONMENT_MAPPED_SP,
						TCGEN_ENVIRONMENT_MAPPED_SP_FP,
						TCGEN_FOG,
						TCGEN_VECTOR));

	Q_strcat(dest, size,
					 va("#ifndef colorGen_t\n"
						"#define colorGen_t\n"
						"#define CGEN_LIGHTING_DIFFUSE %i\n"
						"#define CGEN_DISINTEGRATION_1 %i\n"
						"#define CGEN_DISINTEGRATION_2 %i\n"
						"#endif\n",
						CGEN_LIGHTING_DIFFUSE,
						CGEN_DISINTEGRATION_1,
						CGEN_DISINTEGRATION_2));

	Q_strcat(dest, size,
					 va("#ifndef alphaGen_t\n"
						"#define alphaGen_t\n"
						"#define AGEN_LIGHTING_SPECULAR %i\n"
						"#define AGEN_LIGHTING_SPECULAR_STATIC %i\n"
						"#define AGEN_PORTAL %i\n"
						"#endif\n",
						AGEN_LIGHTING_SPECULAR,
						AGEN_LIGHTING_SPECULAR_STATIC,
						AGEN_PORTAL));

	Q_strcat(dest, size,
					 va("#define ALPHA_TEST_GT0 %i\n"
						"#define ALPHA_TEST_LT128 %i\n"
						"#define ALPHA_TEST_GE128 %i\n"
						"#define ALPHA_TEST_GE192 %i\n"
						"#define ALPHA_TEST_E255 %i\n",
						ALPHA_TEST_GT0,
						ALPHA_TEST_LT128,
						ALPHA_TEST_GE128,
						ALPHA_TEST_GE192,
						ALPHA_TEST_E255));

	Q_strcat(dest, size,
		va("#define MAX_G2_BONES %i\n",
			MAX_G2_BONES));

	Q_strcat(dest, size,
		va("#define MAX_GPU_FOGS %i\n",
			MAX_GPU_FOGS));

	Q_strcat(dest, size,
		va("#define MAX_DLIGHTS %i\n",
			MAX_DLIGHTS));

	Q_strcat(dest, size,
		va("#define DSHADOW_MAP_SIZE %i\n",
			DSHADOW_MAP_SIZE));

	fbufWidthScale = (float)glConfig.vidWidth;
	fbufHeightScale = (float)glConfig.vidHeight;
	Q_strcat(dest, size,
					 va("#ifndef r_FBufScale\n"
						"#define r_FBufScale vec2(%f, %f)\n"
						"#endif\n",
						fbufWidthScale,
						fbufHeightScale));

	if (r_deluxeSpecular->value > 0.000001f)
		Q_strcat(dest, size, va("#define r_deluxeSpecular %f\n", r_deluxeSpecular->value));

	if (r_volumetricFog->integer)
		Q_strcat(dest, size, va("#define r_volumetricFogSamples %i\n", r_volumetricFogSamples->integer));

	// froxel volumetric fog (tr_volumetric.cpp): also enables the froxel
	// lookup of the fog pass, generic and surface sprite programs
	if (R_VolumetricFroxelEnabled())
	{
		Q_strcat(dest, size, "#define USE_FROXEL_FOG\n");
		// local fog volumes (tr_fogvolume.cpp), sizes of the VolumetricFog block
		Q_strcat(dest, size, va("#define MAX_GPU_FOG_VOLUMES %i\n", MAX_GPU_FOG_VOLUMES));
		Q_strcat(dest, size, va("#define FROXEL_MAX_SLICES %i\n", FROXEL_MAX_SLICES));
		Q_strcat(dest, size, va("#define FROXEL_LOCAL_POOL %i\n", FROXEL_LOCAL_POOL));
	}

	if (r_cubeMapping->integer)
	{
		Q_strcat(dest, size, va("#define CUBEMAP_RESOLUTION float(%i)\n", CUBE_MAP_SIZE));
		Q_strcat(dest, size, va("#define ROUGHNESS_MIPS float(%i)\n", CUBE_MAP_ROUGHNESS_MIPS));
	}

	Q_strcat(dest, size, "#define USE_ALPHA_TEST\n");

	// The modern sun path uses raw depth and manual comparisons. This must be
	// decided with texture creation, so the A/B switch is latched.
	if (r_sunShadowMode->integer)
		Q_strcat(dest, size, "#define USE_SHADOWS2\n");

	// screen-space AO / contact shadow map (u_SSAOMap), tr_ao.cpp
	if (R_AOResourcesEnabled())
		Q_strcat(dest, size, "#define USE_SSAO\n");

	// lightall writes the SSR material attachments of renderFbo, tr_ssr.cpp
	if (R_SSRResourcesEnabled())
		Q_strcat(dest, size, "#define USE_SSR\n");

	// lightall writes the SSGI source / receiver attachments, tr_ssgi.cpp
	if (R_SSGIResourcesEnabled())
		Q_strcat(dest, size, "#define USE_SSGI\n");

	// skin scattering, tr_skinsss.cpp: wrap / split of the skin diffuse light
	// (r_skinSSS >= 1), and the skin diffuse attachment of renderFbo (2)
	if (R_SkinSSSMode() >= 1)
		Q_strcat(dest, size, "#define USE_SKIN_SSS\n");
	if (R_SkinSSSResourcesEnabled())
		Q_strcat(dest, size, "#define USE_SKIN_SSS_BUFFER\n");

	// lightall wets rain exposed surfaces, tr_weather.cpp
	if (R_WeatherWetnessEnabled())
		Q_strcat(dest, size, "#define USE_WETNESS\n");

	if (r_hdr->integer && (r_toneMap->integer || r_forceToneMap->integer))
		Q_strcat(dest, size, "#define USE_TONEMAPPING\n");

	if (extra)
	{
		Q_strcat(dest, size, extra);
	}

	if (library)
	{
		// Shared functions (see GLSL_LoadGPUShader). Errors in them are
		// reported as source string 1 with the line numbers of their file.
		Q_strcat(dest, size, va("\n#line %d 1\n", library->firstLineNumber - 1));
		Q_strcat(dest, size, library->source);
		Q_strcat(dest, size, va("\n#line %d 0\n", firstLineNumber - 1));
	}
	else
	{
		// OK we added a lot of stuff but if we do something bad in the GLSL
		// shaders then we want the proper line so we have to reset the line
		// counting
		Q_strcat(dest, size, va("\n#line %d\n", firstLineNumber - 1));
	}

	return strlen(dest);
}

static bool GLSL_IsGPUShaderCompiled (GLuint shader)
{
	GLint compiled;
	qglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	return (compiled == GL_TRUE);
}

static GLuint GLSL_CompileGPUShader(
	GLuint program,
	const GLchar *buffer,
	int size,
	GLenum shaderType)
{
	GLuint shader = qglCreateShader(shaderType);
	if ( shader == 0 )
	{
		return 0;
	}

	qglShaderSource(shader, 1, &buffer, &size);
	qglCompileShader(shader);

	if ( !GLSL_IsGPUShaderCompiled(shader) )
	{
		GLSL_PrintShaderSource(shader);
		GLSL_PrintShaderInfoLog(shader, qfalse);

		qglDeleteShader(shader);

		ri.Error(ERR_FATAL, "Couldn't compile shader");
		return 0;
	}

	return shader;
}

static const char *GLSL_GetShaderFileSuffix( GLenum shaderType )
{
	static struct
	{
		GLenum shaderType;
		const char *extension;
	} shaderToFileExtensionSuffix[] = {
		{ GL_VERTEX_SHADER, "vp" },
		{ GL_FRAGMENT_SHADER, "fp" },
		{ GL_GEOMETRY_SHADER, "gp" },
	};

	for ( const auto& suffix : shaderToFileExtensionSuffix )
	{
		if ( shaderType == suffix.shaderType )
		{
			return suffix.extension;
		}
	}

	return nullptr;
}

static size_t GLSL_LoadGPUShaderSource(
	const char *name,
	const char *fallback,
	GLenum shaderType,
	char *dest,
	int destSize)
{
	const char *shaderSuffix = GLSL_GetShaderFileSuffix(shaderType);
	assert(shaderSuffix != nullptr);

	char filename[MAX_QPATH];
	Com_sprintf(filename, sizeof(filename), "glsl/%s_%s.glsl", name, shaderSuffix);

	int shaderTextLen = 0;
	GLcharARB *buffer = nullptr;
	if ( r_externalGLSL->integer )
	{
		shaderTextLen = ri.FS_ReadFile(filename, (void **)&buffer);
	}

	const char *shaderText = nullptr;
	if ( !buffer )
	{
		if ( fallback )
		{
			ri.Printf(PRINT_DEVELOPER, "...loading built-in '%s'\n", filename);
			shaderText = fallback;
			shaderTextLen = strlen(shaderText);
			ri.Printf(PRINT_DEVELOPER, "...loading '%s'\n", filename);
		}
		else
		{
			ri.Printf(PRINT_DEVELOPER, "couldn't load '%s'\n", filename);
			return 0;
		}
	}
	else
	{
		ri.Printf(PRINT_DEVELOPER, "...loading '%s'\n", filename);
		shaderText = buffer;
	}

	int result = 0;
	if ( destSize >= (shaderTextLen + 1) )
	{
		Q_strncpyz(dest, shaderText, destSize);
		result = strlen(dest);
	}

	if ( buffer )
	{
		ri.FS_FreeFile(buffer);
	}

	return result;
}

static void GLSL_LinkProgram(GLuint program)
{
	qglLinkProgram(program);

	GLint linked;
	qglGetProgramiv(program, GL_LINK_STATUS, &linked);
	if ( linked != GL_TRUE )
	{
		GLSL_PrintProgramInfoLog(program, qfalse);
		ri.Printf(PRINT_ALL, "\n");
		ri.Error(ERR_FATAL, "shaders failed to link");
	}
}

#if defined(_DEBUG)
static void GLSL_ShowProgramUniforms(GLuint program)
{
	int             i, count, size;
	GLenum			type;
	char            uniformName[1000];

	// install the executables in the program object as part of current state.
	qglUseProgram(program);

	// check for GL Errors

	// query the number of active uniforms
	qglGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);

	// Loop over each of the active uniforms, and set their value
	for(i = 0; i < count; i++)
	{
		qglGetActiveUniform(program, i, sizeof(uniformName), NULL, &size, &type, uniformName);

		ri.Printf(PRINT_DEVELOPER, "active uniform: '%s'\n", uniformName);
	}

	qglUseProgram(0);
}
#endif

static void GLSL_BindShaderInterface( shaderProgram_t *program )
{
	static const char *shaderInputNames[] = {
		"attr_Position",  // ATTR_INDEX_POSITION
		"attr_TexCoord0",  // ATTR_INDEX_TEXCOORD0
		"attr_TexCoord1",  // ATTR_INDEX_TEXCOORD1
		"attr_TexCoord2",  // ATTR_INDEX_TEXCOORD2
		"attr_TexCoord3",  // ATTR_INDEX_TEXCOORD3
		"attr_TexCoord4",  // ATTR_INDEX_TEXCOORD4
		"attr_Tangent",  // ATTR_INDEX_TANGENT
		"attr_Normal",  // ATTR_INDEX_NORMAL
		"attr_Color",  // ATTR_INDEX_COLOR
		"attr_LightDirection",  // ATTR_INDEX_LIGHTDIRECTION
		"attr_BoneIndexes",  // ATTR_INDEX_BONE_INDEXES
		"attr_BoneWeights",  // ATTR_INDEX_BONE_WEIGHTS
		"attr_Position2",  // ATTR_INDEX_POSITION2
		"attr_Tangent2",  // ATTR_INDEX_TANGENT2
		"attr_Normal2",  // ATTR_INDEX_NORMAL2
	};

	static const char *xfbVarNames[XFB_VAR_COUNT] = {
		"var_Position",
		"var_Velocity",
		"var_Impact",
	};

	static const char *shaderOutputNames[] = {
		"out_Color",  // Color output
		"out_Glow",  // Glow output
		// screen-space attachments of renderFbo, tr_screenspace.cpp; the
		// index is the attachment (SCREEN_ATTACHMENT_*)
		"out_SSRNormal",  // shared normal (SSR, SSGI)
		"out_SSRSpecular",  // SSR, tr_ssr.cpp
		"out_SSRCubemap",
		"out_SSGIAlbedo",  // SSGI, tr_ssgi.cpp
		"out_SSGIRadiance",
		"out_SkinDiffuse",  // skin SSS, tr_skinsss.cpp
	};

	const uint32_t attribs = program->attribs;
	if (attribs != 0)
	{
		for ( int attribIndex = 0; attribIndex < ATTR_INDEX_MAX; ++attribIndex )
		{
			if ( !(attribs & (1u << attribIndex)) )
			{
				continue;
			}

			qglBindAttribLocation(program->program, attribIndex, shaderInputNames[attribIndex]);
		}
	}

	for ( size_t outputIndex = 0; outputIndex < ARRAY_LEN(shaderOutputNames); ++outputIndex )
	{
		qglBindFragDataLocation(program->program, outputIndex, shaderOutputNames[outputIndex]);
	}

	const uint32_t xfbVars = program->xfbVariables;
	if (xfbVars != 0)
	{
		size_t activeXfbVarsCount = 0;
		const char *activeXfbVarNames[XFB_VAR_COUNT] = {};

		for (uint32_t xfbVarIndex = 0; xfbVarIndex < XFB_VAR_COUNT; ++xfbVarIndex)
		{
			if ((xfbVars & (1u << xfbVarIndex)) != 0)
			{
				activeXfbVarNames[activeXfbVarsCount++] = xfbVarNames[xfbVarIndex];
			}
		}

		qglTransformFeedbackVaryings(
			program->program, activeXfbVarsCount, activeXfbVarNames, GL_INTERLEAVED_ATTRIBS);
	}
}

GLenum ToGLShaderType( GPUShaderType type )
{
	switch ( type )
	{
		case GPUSHADER_VERTEX:
			return GL_VERTEX_SHADER;

		case GPUSHADER_FRAGMENT:
			return GL_FRAGMENT_SHADER;

		case GPUSHADER_GEOMETRY:
			return GL_GEOMETRY_SHADER;

		default:
			assert(!"Invalid shader type");
			return 0;
	}

	return 0;
}

/*
=============================================================

GLSL PROGRAM CACHE (r_glslCache), see docs/rend2-shader-cache.md

Linked program binaries (GL_ARB_get_program_binary), one file per renderer
in the home path. The key is a hash of the complete stage sources, which
already contain every #define derived from latched cvars: each cvar
combination gets its own entries, and the programs of the other combinations
stay in the file. The whole file is ignored after a driver change (vendor /
renderer / version string), a rejected binary is compiled again.

=============================================================
*/

#define GLSL_CACHE_MAGIC	0x43473252u	// "R2GC"
#define GLSL_CACHE_VERSION	1u
#define GLSL_CACHE_MAX_AGE	16u			// rewrites an unused entry survives

#ifdef REND2_SP
#define GLSL_CACHE_FILE		"glslcache/rend2_sp.bin"
#else
#define GLSL_CACHE_FILE		"glslcache/rend2_mp.bin"
#endif

struct glslCacheFileHeader_t
{
	uint32_t magic;
	uint32_t version;
	uint64_t driverHash;
	uint32_t generation;	// incremented by every rewrite
	uint32_t numEntries;
};

struct glslCacheFileEntry_t
{
	uint64_t key;
	uint32_t format;
	uint32_t length;
	uint32_t lastUsed;		// generation of the last rewrite that saw it used
	uint32_t pad;
};

struct glslCacheEntry_t
{
	GLenum format;
	uint32_t lastUsed;
	std::vector<uint8_t> data;
};

static struct
{
	bool enabled;
	bool dirty;				// rewrite the file at the end of GLSL_LoadGPUShaders
	uint64_t driverHash;
	uint32_t generation;	// generation written by this run
	std::unordered_map<uint64_t, glslCacheEntry_t> entries;
	int hits;
	int stored;
	int rejected;
	size_t fileSize;
} s_glslCache;

static const uint64_t GLSL_HASH_SEED = 14695981039346656037ull;	// FNV-1a 64

static uint64_t GLSL_HashBytes( uint64_t hash, const void *data, size_t length )
{
	const uint8_t *bytes = (const uint8_t *)data;
	for ( size_t i = 0; i < length; ++i )
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

static uint64_t GLSL_HashString( uint64_t hash, const char *text )
{
	if ( !text )
		text = "";
	return GLSL_HashBytes(hash, text, strlen(text) + 1);
}

static void GLSL_CacheBegin( void )
{
	s_glslCache.entries.clear();
	s_glslCache.dirty = false;
	s_glslCache.hits = 0;
	s_glslCache.stored = 0;
	s_glslCache.rejected = 0;
	s_glslCache.fileSize = 0;
	s_glslCache.generation = 1;
	s_glslCache.enabled = r_glslCache->integer && glRefConfig.programBinary;
	if ( !s_glslCache.enabled )
		return;

	uint64_t driverHash = GLSL_HashString(GLSL_HASH_SEED, glConfig.vendor_string);
	driverHash = GLSL_HashString(driverHash, glConfig.renderer_string);
	driverHash = GLSL_HashString(driverHash, glConfig.version_string);
	s_glslCache.driverHash = driverHash;

	void *buffer = nullptr;
	const long fileLength = ri.FS_ReadFile(GLSL_CACHE_FILE, &buffer);
	if ( fileLength <= 0 || !buffer )
		return;

	// never trust the file: every size is checked against what is left
	const uint8_t *data = (const uint8_t *)buffer;
	size_t offset = 0;
	glslCacheFileHeader_t header;
	bool valid = (size_t)fileLength >= sizeof(header);
	if ( valid )
	{
		memcpy(&header, data, sizeof(header));
		offset = sizeof(header);
		valid = header.magic == GLSL_CACHE_MAGIC && header.version == GLSL_CACHE_VERSION;
	}

	if ( !valid )
		ri.Printf(PRINT_WARNING, "GLSL cache: %s is not a cache file of this renderer, rebuilding it\n", GLSL_CACHE_FILE);
	else if ( header.driverHash != driverHash )
	{
		ri.Printf(PRINT_ALL, "GLSL cache: graphics driver changed, rebuilding %s\n", GLSL_CACHE_FILE);
		valid = false;
	}

	if ( valid )
	{
		s_glslCache.generation = header.generation + 1;
		for ( uint32_t i = 0; i < header.numEntries; ++i )
		{
			glslCacheFileEntry_t fileEntry;
			if ( (size_t)fileLength - offset < sizeof(fileEntry) )
				break;
			memcpy(&fileEntry, data + offset, sizeof(fileEntry));
			offset += sizeof(fileEntry);
			if ( fileEntry.length == 0 || (size_t)fileLength - offset < fileEntry.length )
				break;

			glslCacheEntry_t& entry = s_glslCache.entries[fileEntry.key];
			entry.format = fileEntry.format;
			entry.lastUsed = fileEntry.lastUsed;
			entry.data.assign(data + offset, data + offset + fileEntry.length);
			offset += fileEntry.length;
		}

		if ( s_glslCache.entries.size() != header.numEntries )
		{
			ri.Printf(PRINT_WARNING, "GLSL cache: %s is truncated, keeping %d of %u programs\n",
				GLSL_CACHE_FILE, (int)s_glslCache.entries.size(), header.numEntries);
			s_glslCache.dirty = true;
		}
		s_glslCache.fileSize = (size_t)fileLength;
	}
	else
	{
		s_glslCache.dirty = true;
	}

	ri.FS_FreeFile(buffer);
}

static glslCacheEntry_t *GLSL_CacheFind( uint64_t key )
{
	if ( !s_glslCache.enabled )
		return nullptr;
	auto it = s_glslCache.entries.find(key);
	return it != s_glslCache.entries.end() ? &it->second : nullptr;
}

static void GLSL_CacheMarkUsed( glslCacheEntry_t *entry )
{
	// refresh entries before they age out, even when nothing else changed
	if ( s_glslCache.generation - entry->lastUsed > GLSL_CACHE_MAX_AGE / 2 )
		s_glslCache.dirty = true;
	entry->lastUsed = s_glslCache.generation;
}

static void GLSL_CacheStore( uint64_t key, GLuint program )
{
	GLint length = 0;
	qglGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
	if ( length <= 0 )
		return;

	std::vector<uint8_t> data((size_t)length);
	GLsizei written = 0;
	GLenum format = 0;
	qglGetProgramBinary(program, length, &written, &format, data.data());
	if ( written <= 0 )
		return;
	data.resize((size_t)written);

	glslCacheEntry_t& entry = s_glslCache.entries[key];
	entry.format = format;
	entry.lastUsed = s_glslCache.generation;
	entry.data.swap(data);
	s_glslCache.stored++;
	s_glslCache.dirty = true;
}

static void GLSL_CacheEnd( void )
{
	if ( !s_glslCache.enabled )
		return;

	// drop programs of cvar combinations / sources not used for a while
	for ( auto it = s_glslCache.entries.begin(); it != s_glslCache.entries.end(); )
	{
		if ( s_glslCache.generation - it->second.lastUsed > GLSL_CACHE_MAX_AGE )
		{
			it = s_glslCache.entries.erase(it);
			s_glslCache.dirty = true;
		}
		else
			++it;
	}

	// size limit: least recently used first
	size_t total = sizeof(glslCacheFileHeader_t);
	for ( const auto& it : s_glslCache.entries )
		total += sizeof(glslCacheFileEntry_t) + it.second.data.size();
	const size_t maxBytes = (size_t)Com_Clampi(16, 4096, r_glslCacheMaxMB->integer) * 1024 * 1024;
	if ( total > maxBytes )
	{
		std::vector<std::pair<uint32_t, uint64_t>> byAge;
		for ( const auto& it : s_glslCache.entries )
			byAge.emplace_back(it.second.lastUsed, it.first);
		std::sort(byAge.begin(), byAge.end());
		for ( size_t i = 0; i < byAge.size() && total > maxBytes; ++i )
		{
			const glslCacheEntry_t& entry = s_glslCache.entries[byAge[i].second];
			total -= sizeof(glslCacheFileEntry_t) + entry.data.size();
			s_glslCache.entries.erase(byAge[i].second);
		}
		s_glslCache.dirty = true;
	}

	if ( s_glslCache.dirty )
	{
		std::vector<uint8_t> file;
		file.reserve(total);

		glslCacheFileHeader_t header = {};
		header.magic = GLSL_CACHE_MAGIC;
		header.version = GLSL_CACHE_VERSION;
		header.driverHash = s_glslCache.driverHash;
		header.generation = s_glslCache.generation;
		header.numEntries = (uint32_t)s_glslCache.entries.size();
		file.insert(file.end(), (const uint8_t *)&header, (const uint8_t *)(&header + 1));

		for ( const auto& it : s_glslCache.entries )
		{
			glslCacheFileEntry_t fileEntry = {};
			fileEntry.key = it.first;
			fileEntry.format = it.second.format;
			fileEntry.length = (uint32_t)it.second.data.size();
			fileEntry.lastUsed = it.second.lastUsed;
			file.insert(file.end(), (const uint8_t *)&fileEntry, (const uint8_t *)(&fileEntry + 1));
			file.insert(file.end(), it.second.data.begin(), it.second.data.end());
		}

		ri.FS_WriteFile(GLSL_CACHE_FILE, file.data(), (int)file.size());
		s_glslCache.fileSize = file.size();
	}

	// the binaries are not needed after loading
	std::unordered_map<uint64_t, glslCacheEntry_t>().swap(s_glslCache.entries);
}

class ShaderProgramBuilder
{
	public:
		ShaderProgramBuilder();
		~ShaderProgramBuilder();

		ShaderProgramBuilder(const ShaderProgramBuilder&) = delete;
		ShaderProgramBuilder& operator=(const ShaderProgramBuilder&) = delete;

		void Start(
			const char *name,
			const uint32_t attribs,
			const uint32_t xfbVariables);
		bool AddShader(const GPUShaderDesc& shaderDesc, const char *extra, const GPUShaderDesc *library);
		bool Build(shaderProgram_t *program);

	private:
		static const size_t MAX_SHADER_SOURCE_LEN = 16384;

		// stage sources are compiled in Build, unless the program cache has them
		struct PendingShader
		{
			GLenum apiShader;
			GPUShaderType type;
			std::string source;
		};

		void ReleaseShaders();

		const char *name;
		uint32_t attribs;
		uint32_t xfbVariables;
		GLuint program;
		GLuint shaderNames[GPUSHADER_TYPE_COUNT];
		size_t numShaderNames;
		std::string shaderSource;
		std::vector<PendingShader> pendingShaders;
		uint64_t cacheKey;
};

ShaderProgramBuilder::ShaderProgramBuilder()
	: name(nullptr)
	, attribs(0)
	, program(0)
	, shaderNames()
	, numShaderNames(0)
	, shaderSource(MAX_SHADER_SOURCE_LEN, '\0')
	, cacheKey(GLSL_HASH_SEED)
{
}

ShaderProgramBuilder::~ShaderProgramBuilder()
{
	if ( program )
	{
		ReleaseShaders();
		qglDeleteProgram(program);
	}
}

void ShaderProgramBuilder::Start(
	const char *name,
	const uint32_t attribs,
	const uint32_t xfbVariables)
{
	this->program = qglCreateProgram();
	this->name = name;
	this->attribs = attribs;
	this->xfbVariables = xfbVariables;

	pendingShaders.clear();
	uint32_t keyData[3] = { GLSL_CACHE_VERSION, attribs, xfbVariables };
	cacheKey = GLSL_HashString(GLSL_HASH_SEED, name);
	cacheKey = GLSL_HashBytes(cacheKey, keyData, sizeof(keyData));
}

bool ShaderProgramBuilder::AddShader( const GPUShaderDesc& shaderDesc, const char *extra, const GPUShaderDesc *library )
{
	static const int MAX_ATTEMPTS = 3;
	const GLenum apiShader = ToGLShaderType(shaderDesc.type);

	{
		// A header that doesn't fit is silently truncated, only a failed
		// source load below grows the buffer. Size it from the actual
		// source, library and extra defines up front.
		size_t minSize = MAX_SHADER_SOURCE_LEN;
		if ( shaderDesc.source )
			minSize += strlen(shaderDesc.source);
		if ( library )
			minSize += strlen(library->source);
		if ( extra )
			minSize += strlen(extra);
		if ( shaderSource.size() < minSize )
		{
			shaderSource.resize(minSize);
		}
	}

	size_t sourceLen = 0;
	size_t headerLen = 0;
	int attempts = 0;
	while ( sourceLen == 0 && attempts < MAX_ATTEMPTS )
	{
		headerLen = GLSL_GetShaderHeader(
			apiShader,
			extra,
			library,
			shaderDesc.firstLineNumber,
			&shaderSource[0],
			shaderSource.size());

		sourceLen = GLSL_LoadGPUShaderSource(
				name,
				shaderDesc.source,
				apiShader,
				&shaderSource[headerLen],
				shaderSource.size() - headerLen);

		if ( sourceLen == 0 )
		{
			shaderSource.resize(shaderSource.size() * 2);
		}

		++attempts;
	}

	if ( sourceLen == 0 )
	{
		ri.Printf(
			PRINT_ALL,
			"ShaderProgramBuilder::AddShader: Failed to allocate enough memory for "
			"shader '%s'\n",
			name);

		return false;
	}

	PendingShader pending;
	pending.apiShader = apiShader;
	pending.type = shaderDesc.type;
	pending.source.assign(shaderSource.c_str(), sourceLen + headerLen);
	cacheKey = GLSL_HashBytes(cacheKey, &apiShader, sizeof(apiShader));
	cacheKey = GLSL_HashBytes(cacheKey, pending.source.data(), pending.source.size());
	pendingShaders.push_back(std::move(pending));

	return true;
}

bool ShaderProgramBuilder::Build( shaderProgram_t *shaderProgram )
{
	const size_t nameBufferSize = strlen(name) + 1;
	shaderProgram->name = (char *)R_Malloc(nameBufferSize, TAG_GENERAL);
	Q_strncpyz(shaderProgram->name, name, nameBufferSize);

	shaderProgram->attribs = attribs;
	shaderProgram->xfbVariables = xfbVariables;

	// linked binary from the disk cache (attribute, output and transform
	// feedback locations are part of it; block bindings and sampler units are
	// set after loading, as for a compiled program)
	bool loaded = false;
	if ( glslCacheEntry_t *entry = GLSL_CacheFind(cacheKey) )
	{
		qglProgramBinary(program, entry->format, entry->data.data(), (GLsizei)entry->data.size());
		GLint linked = GL_FALSE;
		qglGetProgramiv(program, GL_LINK_STATUS, &linked);
		if ( linked == GL_TRUE )
		{
			GLSL_CacheMarkUsed(entry);
			s_glslCache.hits++;
			loaded = true;
		}
		else
		{
			// e.g. a driver that rejects its own old binaries: compile it
			while ( qglGetError() != GL_NO_ERROR )
				;
			s_glslCache.entries.erase(cacheKey);
			s_glslCache.rejected++;
			s_glslCache.dirty = true;
			qglDeleteProgram(program);
			program = qglCreateProgram();
		}
	}

	if ( !loaded )
	{
		for ( const PendingShader& pending : pendingShaders )
		{
			const GLuint shader = GLSL_CompileGPUShader(
				program,
				pending.source.c_str(),
				(int)pending.source.size(),
				pending.apiShader);
			if ( shader == 0 )
			{
				ri.Printf(
					PRINT_ALL,
					"ShaderProgramBuilder::Build: Unable to load \"%s\"\n",
					name);
				return false;
			}

			if (glRefConfig.annotateResources) qglObjectLabel(GL_SHADER, shader, -1, va("%s_%i", name, pending.type));

			qglAttachShader(program, shader);
			shaderNames[numShaderNames++] = shader;
		}

		shaderProgram->program = program;
		GLSL_BindShaderInterface(shaderProgram);
		if ( s_glslCache.enabled )
			qglProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		GLSL_LinkProgram(program);

		if ( s_glslCache.enabled )
			GLSL_CacheStore(cacheKey, program);
	}

	shaderProgram->program = program;
	if (glRefConfig.annotateResources) qglObjectLabel(GL_PROGRAM, program, -1, name);

	ReleaseShaders();
	pendingShaders.clear();
	program = 0;

	return true;
}

void ShaderProgramBuilder::ReleaseShaders()
{
	for ( size_t i = 0; i < numShaderNames; ++i )
	{
		qglDetachShader(program, shaderNames[i]);
		qglDeleteShader(shaderNames[i]);
	}

	numShaderNames = 0;
}

// fragmentLibrary: optional block of shared GLSL functions (e.g. the fragment
// block of output_transform.glsl) inserted into the fragment shader after the
// defines, so several programs can use the same code. vertexLibrary does the
// same for the vertex shader (e.g. the vertex block of leaf_flutter.glsl).
static bool GLSL_LoadGPUShader(
	ShaderProgramBuilder& builder,
	shaderProgram_t *program,
	const char *name,
	const uint32_t attribs,
	const uint32_t xfbVariables,
	const GLcharARB *extra,
	const GPUProgramDesc& programDesc,
	const GPUShaderDesc *fragmentLibrary = nullptr,
	const GPUShaderDesc *vertexLibrary = nullptr)
{
	builder.Start(name, attribs, xfbVariables);
	for ( size_t i = 0; i < programDesc.numShaders; ++i )
	{
		const GPUShaderDesc& shaderDesc = programDesc.shaders[i];
		const GPUShaderDesc *library = nullptr;
		if ( shaderDesc.type == GPUSHADER_FRAGMENT )
			library = fragmentLibrary;
		else if ( shaderDesc.type == GPUSHADER_VERTEX )
			library = vertexLibrary;
		if ( !builder.AddShader(shaderDesc, extra, library) )
		{
			return false;
		}
	}
	return builder.Build(program);
}

void GLSL_InitUniforms(shaderProgram_t *program)
{
	program->uniforms = (GLint *)R_Malloc(
			UNIFORM_COUNT * sizeof(*program->uniforms), TAG_GENERAL);
	program->uniformBufferOffsets = (short *)R_Malloc(
			UNIFORM_COUNT * sizeof(*program->uniformBufferOffsets), TAG_GENERAL);

	GLint *uniforms = program->uniforms;
	int size = 0;
	for (int i = 0; i < UNIFORM_COUNT; i++)
	{
		uniforms[i] = qglGetUniformLocation(program->program, uniformsInfo[i].name);
		if (uniforms[i] == -1)
			continue;

		program->uniformBufferOffsets[i] = size;
		switch(uniformsInfo[i].type)
		{
			case GLSL_INT:
				size += sizeof(GLint) * uniformsInfo[i].size;
				break;
			case GLSL_FLOAT:
				size += sizeof(GLfloat) * uniformsInfo[i].size;
				break;
			case GLSL_VEC2:
				size += sizeof(float) * 2 * uniformsInfo[i].size;
				break;
			case GLSL_VEC3:
				size += sizeof(float) * 3 * uniformsInfo[i].size;
				break;
			case GLSL_VEC4:
				size += sizeof(float) * 4 * uniformsInfo[i].size;
				break;
			case GLSL_MAT4x3:
				size += sizeof(float) * 12 * uniformsInfo[i].size;
				break;
			case GLSL_MAT4x4:
				size += sizeof(float) * 16 * uniformsInfo[i].size;
				break;
			default:
				break;
		}
	}

	program->uniformBuffer = (char *)R_Malloc(size, TAG_SHADERTEXT, qtrue);

	// r_foliageField: every program with the foliage interaction library
	// samples the bend field on its own fixed units (tr_foliagefield.cpp)
	if (uniforms[UNIFORM_FOLIAGEFIELDMAP] != -1 || uniforms[UNIFORM_FOLIAGEFIELDPREVMAP] != -1)
	{
		GLint previousProgram = 0;
		qglGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
		qglUseProgram(program->program);
		GLSL_SetUniformInt(program, UNIFORM_FOLIAGEFIELDMAP, TB_FOLIAGEFIELD);
		GLSL_SetUniformInt(program, UNIFORM_FOLIAGEFIELDPREVMAP, TB_FOLIAGEFIELD_PREV);
		qglUseProgram(previousProgram);
	}

	program->uniformBlocks = 0;
	for ( int i = 0; i < UNIFORM_BLOCK_COUNT; ++i )
	{
		const GLuint blockIndex = qglGetUniformBlockIndex(
			program->program, uniformBlocksInfo[i].name);
		if (blockIndex == GL_INVALID_INDEX)
			continue;
		ri.Printf(
			PRINT_DEVELOPER,
			"Binding block %d (name '%s', size %zu bytes) to slot %d\n",
			blockIndex,
			uniformBlocksInfo[i].name,
			uniformBlocksInfo[i].size,
			uniformBlocksInfo[i].slot);
		qglUniformBlockBinding(
			program->program, blockIndex, uniformBlocksInfo[i].slot);
		program->uniformBlocks |= (1u << i);
	}

	GLint numActiveUniformBlocks = 0;
	qglGetProgramiv(program->program, GL_ACTIVE_UNIFORM_BLOCKS, &numActiveUniformBlocks);
	ri.Printf(PRINT_DEVELOPER, "..num uniform blocks: %d\n", numActiveUniformBlocks);
	for (int i = 0; i < numActiveUniformBlocks; ++i)
	{
		char blockName[512];
		qglGetActiveUniformBlockName(
			program->program,
			i,
			sizeof(blockName),
			nullptr,
			blockName);

		GLint blockSize = 0;
		qglGetActiveUniformBlockiv(
			program->program, i, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);

		ri.Printf(PRINT_DEVELOPER, "..block %d: %s (%d bytes)\n", i, blockName, blockSize);
		GLint numMembers = 0;
		qglGetActiveUniformBlockiv(
			program->program, i, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &numMembers);

		if (numMembers > 0)
		{
			GLuint memberIndices[128];
			qglGetActiveUniformBlockiv(
				program->program,
				i,
				GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,
				(GLint *)memberIndices);

			GLint memberOffsets[128];
			qglGetActiveUniformsiv(
				program->program,
				numMembers,
				memberIndices,
				GL_UNIFORM_OFFSET,
				memberOffsets);

			GLint memberTypes[128];
			qglGetActiveUniformsiv(
				program->program,
				numMembers,
				memberIndices,
				GL_UNIFORM_TYPE,
				memberTypes);

			for (int j = 0; j < numMembers; ++j)
			{
				char memberName[512];
				qglGetActiveUniformName(
					program->program,
					memberIndices[j],
					sizeof(memberName),
					nullptr,
					memberName);

				ri.Printf(PRINT_DEVELOPER, "....uniform '%s'\n", memberName);
				ri.Printf(PRINT_DEVELOPER, "......offset: %d\n", memberOffsets[j]);
				switch (memberTypes[j])
				{
				case GL_FLOAT:
					ri.Printf(PRINT_DEVELOPER, "......type: float\n");
					break;
				case GL_FLOAT_VEC2:
					ri.Printf(PRINT_DEVELOPER, "......type: vec2\n");
					break;
				case GL_FLOAT_VEC3:
					ri.Printf(PRINT_DEVELOPER, "......type: vec3\n");
					break;
				case GL_FLOAT_VEC4:
					ri.Printf(PRINT_DEVELOPER, "......type: vec4\n");
					break;
				case GL_INT:
					ri.Printf(PRINT_DEVELOPER, "......type: int\n");
					break;
				default:
					ri.Printf(PRINT_DEVELOPER, "......type: other\n");
					break;
				}
			}
		}
	}
}

void GLSL_FinishGPUShader(shaderProgram_t *program)
{
#if defined(_DEBUG)
	GLSL_ShowProgramUniforms(program->program);
	GL_CheckErrors();
#endif
}

void GLSL_SetUniforms( shaderProgram_t *program, UniformData *uniformData )
{
	if (uniformData == nullptr)
		return;

	UniformData *data = uniformData;
	if (data == nullptr)
		return;

	while ( data->index != UNIFORM_COUNT )
	{
		switch ( uniformsInfo[data->index].type )
		{
			case GLSL_INT:
			{
				assert(data->numElements == 1);
				GLint *value = (GLint *)(data + 1);
				GLSL_SetUniformInt(program, data->index, *value);
				data = reinterpret_cast<UniformData *>(value + data->numElements);
				break;
			}

			case GLSL_FLOAT:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformFloatN(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements);
				break;
			}

			case GLSL_VEC2:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformVec2N(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*2);
				break;
			}

			case GLSL_VEC3:
			{
				assert(data->numElements == 1);
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformVec3(program, data->index, value);
				data = reinterpret_cast<UniformData *>(value + data->numElements*3);
				break;
			}

			case GLSL_VEC4:
			{
				assert(data->numElements == 1);
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformVec4(program, data->index, value);
				data = reinterpret_cast<UniformData *>(value + data->numElements*4);
				break;
			}

			case GLSL_MAT4x3:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformMatrix4x3(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*12);
				break;
			}

			case GLSL_MAT4x4:
			{
				GLfloat *value = (GLfloat *)(data + 1);
				GLSL_SetUniformMatrix4x4(program, data->index, value, data->numElements);
				data = reinterpret_cast<UniformData *>(value + data->numElements*16);
				break;
			}

			default:
			{
				assert(!"Invalid uniform data type");
				return;
			}
		}
	}
}

void GLSL_SetUniformInt(shaderProgram_t *program, int uniformNum, GLint value)
{
	GLint *uniforms = program->uniforms;
	GLint *compare = (GLint *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_INT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformInt: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (value == *compare)
	{
		return;
	}

	*compare = value;

	qglUniform1i(uniforms[uniformNum], value);
}

void GLSL_SetUniformFloat(shaderProgram_t *program, int uniformNum, GLfloat value)
{
	GLint *uniforms = program->uniforms;
	GLfloat *compare = (GLfloat *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_FLOAT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloat: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (value == *compare)
	{
		return;
	}

	*compare = value;

	qglUniform1f(uniforms[uniformNum], value);
}

void GLSL_SetUniformVec2(shaderProgram_t *program, int uniformNum, const vec2_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC2)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec2: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (v[0] == compare[0] && v[1] == compare[1])
	{
		return;
	}

	compare[0] = v[0];
	compare[1] = v[1];

	qglUniform2f(uniforms[uniformNum], v[0], v[1]);
}

void GLSL_SetUniformVec2N(shaderProgram_t *program, int uniformNum, const float *v, int numVec2s)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC2)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec2: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numVec2s)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec2N: uniform %i only has %d elements! Tried to set %d\n",
			uniformNum,
			uniformsInfo[uniformNum].size,
			numVec2s);
		return;
	}

	if (memcmp(compare, v, sizeof(vec2_t) * numVec2s) == 0)
	{
		return;
	}

	memcpy(compare, v, sizeof(vec2_t) * numVec2s);

	qglUniform2fv(uniforms[uniformNum], numVec2s, v);
}

void GLSL_SetUniformVec4N(shaderProgram_t *program, int uniformNum, const float *v, int numVec4s)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC4)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec4N: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numVec4s)
	{
		ri.Printf(PRINT_WARNING, "GLSL_SetUniformVec4N: uniform %i only has %d elements! Tried to set %d\n",
			uniformNum,
			uniformsInfo[uniformNum].size,
			numVec4s);
		return;
	}

	if (memcmp(compare, v, sizeof(vec4_t) * numVec4s) == 0)
	{
		return;
	}

	memcpy(compare, v, sizeof(vec4_t) * numVec4s);

	qglUniform4fv(uniforms[uniformNum], numVec4s, v);
}

void GLSL_SetUniformVec3(shaderProgram_t *program, int uniformNum, const vec3_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC3)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec3: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (VectorCompare(v, compare))
	{
		return;
	}

	VectorCopy(v, compare);

	qglUniform3f(uniforms[uniformNum], v[0], v[1], v[2]);
}

void GLSL_SetUniformVec4(shaderProgram_t *program, int uniformNum, const vec4_t v)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_VEC4)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformVec4: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (VectorCompare4(v, compare))
	{
		return;
	}

	VectorCopy4(v, compare);

	qglUniform4f(uniforms[uniformNum], v[0], v[1], v[2], v[3]);
}

void GLSL_SetUniformFloatN(shaderProgram_t *program, int uniformNum, const float *v, int numFloats)
{
	GLint *uniforms = program->uniforms;
	float *compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_FLOAT)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloatN: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numFloats)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformFloatN: uniform %i only has %d elements! Tried to set %d\n",
					uniformNum,
					uniformsInfo[uniformNum].size,
					numFloats );
		return;
	}

	if ( memcmp( compare, v, sizeof( float ) * numFloats ) == 0 )
	{
		return;
	}

	memcpy( compare, v, sizeof( float ) * numFloats );

	qglUniform1fv(uniforms[uniformNum], numFloats, v);
}

void GLSL_SetUniformMatrix4x3(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements)
{
	GLint *uniforms = program->uniforms;
	float *compare;

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_MAT4x3)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformMatrix4x3: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numElements)
		return;

	compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);
	if (memcmp (matrix, compare, sizeof (float) * 12 * numElements) == 0)
	{
		return;
	}

	Com_Memcpy (compare, matrix, sizeof (float) * 12 * numElements);

	qglUniformMatrix4x3fv(uniforms[uniformNum], numElements, GL_FALSE, matrix);
}

void GLSL_SetUniformMatrix4x4(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements)
{
	GLint *uniforms = program->uniforms;
	float *compare;

	if (uniforms[uniformNum] == -1)
		return;

	if (uniformsInfo[uniformNum].type != GLSL_MAT4x4)
	{
		ri.Printf( PRINT_WARNING, "GLSL_SetUniformMatrix4x4: wrong type for uniform %i in program %s\n", uniformNum, program->name);
		return;
	}

	if (uniformsInfo[uniformNum].size < numElements)
		return;

	compare = (float *)(program->uniformBuffer + program->uniformBufferOffsets[uniformNum]);
	if (memcmp (matrix, compare, sizeof (float) * 16 * numElements) == 0)
	{
		return;
	}

	Com_Memcpy (compare, matrix, sizeof (float) * 16 * numElements);

	qglUniformMatrix4fv(uniforms[uniformNum], numElements, GL_FALSE, matrix);
}

void GLSL_DeleteGPUShader(shaderProgram_t *program)
{
	if(program->program)
	{
		qglDeleteProgram(program->program);

		Z_Free(program->name);
		Z_Free(program->uniformBuffer);
		Z_Free(program->uniformBufferOffsets);
		Z_Free(program->uniforms);

		Com_Memset(program, 0, sizeof(*program));
	}
}

static bool GLSL_IsValidPermutationForGeneric (int shaderCaps)
{
#ifdef REND2_SP_MD3
	if ( (shaderCaps & GENERICDEF_USE_VERTEX_ANIMATION) &&
			(shaderCaps & GENERICDEF_USE_SKELETAL_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

static bool GLSL_IsValidPermutationForFog (int shaderCaps)
{
#ifdef REND2_SP_MD3
	if ( (shaderCaps & FOGDEF_USE_VERTEX_ANIMATION) &&
			(shaderCaps & FOGDEF_USE_SKELETAL_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

static bool GLSL_IsValidPermutationForLight (int lightType, int shaderCaps)
{
	if (!lightType && (shaderCaps & LIGHTDEF_USE_PARALLAXMAP))
		return false;

#ifdef REND2_SP_MD3
	if ( (shaderCaps & LIGHTDEF_USE_SKELETAL_ANIMATION) &&
			(shaderCaps & LIGHTDEF_USE_VERTEX_ANIMATION) )
		return false;
#endif // REND2_SP
	return true;
}

Block *FindBlock( const char *name, Block *blocks, size_t numBlocks )
{
	for ( size_t i = 0; i < numBlocks; ++i )
	{
		Block *block = blocks + i;
		if ( Q_stricmpn(block->blockHeaderTitle, name, block->blockHeaderTitleLength) == 0 )
		{
			return block;
		}
	}

	return nullptr;
}

void GLSL_InitSplashScreenShader()
{
	const char *vs =
		"#version 150 core\n"
		"out vec2 var_TexCoords;\n"
		"void main() {\n"
		"  vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);\n"
		"  gl_Position = vec4(position, 0.0, 1.0);\n"
		"  var_TexCoords = vec2(position.x * 0.5 + 0.5, 2.0 - (position.y * 0.5 + 0.5));\n"
		"}";

	const char *fs =
		"#version 150 core\n"
		"uniform sampler2D u_SplashTexture;\n"
		"in vec2 var_TexCoords;\n"
		"out vec4 out_Color;\n"
		"void main() {\n"
		"  out_Color = texture(u_SplashTexture, var_TexCoords);\n"
		"}";

	GLuint vshader = qglCreateShader(GL_VERTEX_SHADER);
	qglShaderSource(vshader, 1, &vs, NULL);
	qglCompileShader(vshader);

	GLuint fshader = qglCreateShader(GL_FRAGMENT_SHADER);
	qglShaderSource(fshader, 1, &fs, NULL);
	qglCompileShader(fshader);

	GLuint program = qglCreateProgram();
	qglAttachShader(program, vshader);
	qglAttachShader(program, fshader);
	qglLinkProgram(program);

	size_t splashLen = strlen("splash");
	tr.splashScreenShader.program = program;
	tr.splashScreenShader.name = (char *)R_Malloc(splashLen + 1, TAG_GENERAL);
	GLSL_InitUniforms(&tr.splashScreenShader);
	Q_strncpyz(tr.splashScreenShader.name, "splash", splashLen + 1);
}

static const GPUProgramDesc *LoadProgramSource(
	const char *programName, Allocator& allocator, const GPUProgramDesc& fallback )
{
	const GPUProgramDesc *result = &fallback;

	if ( r_externalGLSL->integer )
	{
		char *buffer;
		char programPath[MAX_QPATH];
		Com_sprintf(programPath, sizeof(programPath), "glsl/%s.glsl", programName);

		long size = ri.FS_ReadFile(programPath, (void **)&buffer);
		if ( size )
		{
			GPUProgramDesc *externalProgramDesc = ojkAlloc<GPUProgramDesc>(allocator);
			*externalProgramDesc = ParseProgramSource(allocator, buffer);
			result = externalProgramDesc;
			ri.FS_FreeFile(buffer);
		}
	}

	return result;
}

// Shared exposure/tone mapping functions (glsl/output_transform.glsl) used by
// the tone map and refraction programs
static const GPUShaderDesc *LoadOutputTransformLibrary( Allocator& allocator )
{
	const GPUProgramDesc *programDesc =
		LoadProgramSource("output_transform", allocator, fallback_output_transformProgram);
	for ( size_t i = 0; i < programDesc->numShaders; ++i )
	{
		if ( programDesc->shaders[i].type == GPUSHADER_FRAGMENT )
		{
			return &programDesc->shaders[i];
		}
	}

	ri.Error(ERR_FATAL, "Could not load output_transform shader library!");
	return nullptr;
}

// Froxel volumetric fog functions (glsl/volumetric_common.glsl), inserted into
// the volumetric programs and, with r_volumetricFog 2 only, into the programs
// with a legacy fog path. nullptr otherwise: the other modes keep their source.
static const GPUShaderDesc *LoadVolumetricLibrary( Allocator& allocator )
{
	if ( !R_VolumetricFroxelEnabled() )
		return nullptr;

	const GPUProgramDesc *programDesc =
		LoadProgramSource("volumetric_common", allocator, fallback_volumetric_commonProgram);
	for ( size_t i = 0; i < programDesc->numShaders; ++i )
	{
		if ( programDesc->shaders[i].type == GPUSHADER_FRAGMENT )
		{
			return &programDesc->shaders[i];
		}
	}

	ri.Error(ERR_FATAL, "Could not load volumetric_common shader library!");
	return nullptr;
}

static const GPUShaderDesc *GLSL_CombineLibraries(
	Allocator& allocator, const GPUShaderDesc *a, const GPUShaderDesc *b );

static const GPUShaderDesc *LoadVertexLibrary(
	Allocator& allocator, const char *name, const GPUProgramDesc& fallback )
{
	const GPUProgramDesc *programDesc = LoadProgramSource(name, allocator, fallback);
	for ( size_t i = 0; i < programDesc->numShaders; ++i )
	{
		if ( programDesc->shaders[i].type == GPUSHADER_VERTEX )
		{
			return &programDesc->shaders[i];
		}
	}

	ri.Error(ERR_FATAL, "Could not load %s shader library!", name);
	return nullptr;
}

// Foliage character colliders (glsl/foliage_interact.glsl,
// tr_foliageinteract.cpp): the FoliageInteraction block and the bend
// functions, for the surface sprites (grass)
static const GPUShaderDesc *LoadFoliageInteractLibrary( Allocator& allocator )
{
	return LoadVertexLibrary(allocator, "foliage_interact", fallback_foliage_interactProgram);
}

// MD3 foliage motion, vertex functions shared by every program that draws a
// FOLIAGE_LEAF or FOLIAGE_PLANT surface (lightall, generic, fogpass,
// velocity), so all passes move it the same way: leaf flutter
// (glsl/leaf_flutter.glsl, tr_leafflutter.cpp), the character colliders and
// the plant root bend (glsl/foliage_interact.glsl + plant_bend.glsl)
static const GPUShaderDesc *LoadLeafFlutterLibrary( Allocator& allocator )
{
	const GPUShaderDesc *leaf =
		LoadVertexLibrary(allocator, "leaf_flutter", fallback_leaf_flutterProgram);
	const GPUShaderDesc *plant =
		LoadVertexLibrary(allocator, "plant_bend", fallback_plant_bendProgram);
	return GLSL_CombineLibraries(allocator,
		GLSL_CombineLibraries(allocator, leaf, LoadFoliageInteractLibrary(allocator)), plant);
}

// texture units of the froxel volume lookup (FroxelFog)
static void GLSL_SetFroxelLookupUnits( shaderProgram_t *program )
{
	GLSL_SetUniformInt(program, UNIFORM_FROXELVOLUME, TB_CUBEMAP);
	GLSL_SetUniformInt(program, UNIFORM_FROXELTAIL, TB_ENVBRDFMAP);
}

static int GLSL_LoadGPUProgramGeneric(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("generic\0");

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("generic", allocator, fallback_genericProgram);
	const GPUShaderDesc *volumetricLibrary = LoadVolumetricLibrary(allocator);
	const GPUShaderDesc *leafFlutterLibrary = LoadLeafFlutterLibrary(allocator);
	for ( int i = 0; i < GENERICDEF_COUNT; i++ )
	{
		if (!GLSL_IsValidPermutationForGeneric(i))
		{
			continue;
		}

		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_NORMAL | ATTR_COLOR;
		Q_strncpyz(name, "generic\0", nameLen + 1);
		extradefines[0] = '\0';

		if (i & GENERICDEF_USE_DEFORM_VERTEXES)
		{
			Q_strcat(name, sizeof(name), "_DEFORM");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");
		}

		if (i & GENERICDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(name, sizeof(name), "_TCGENMOD");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}
#ifdef REND2_SP_MD3
		if (i & GENERICDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_VA");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & GENERICDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_SK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & GENERICDEF_USE_FOG)
		{
			Q_strcat(name, sizeof(name), "_FOG");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FOG\n");
			if (r_volumetricFog->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_VOLUMETRIC_FOG\n");
		}

		if (i & GENERICDEF_USE_RGBAGEN)
		{
			Q_strcat(name, sizeof(name), "_RGBGEN");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_RGBAGEN\n");
		}

		if (i & GENERICDEF_USE_FLARE_TEST)
		{
			Q_strcat(name, sizeof(name), "_FLARE");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FLARE_TEST\n");

		}

		/*if (i & GENERICDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");*/

		if (!GLSL_LoadGPUShader(builder, &tr.genericShader[i], name, attribs, NO_XFB_VARS,
				extradefines, *programDesc, volumetricLibrary, leafFlutterLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load generic shader!");
		}

		GLSL_InitUniforms(&tr.genericShader[i]);

		qglUseProgram(tr.genericShader[i].program);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_LIGHTMAP,   TB_LIGHTMAP);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_EMISSIVEMAP, TB_EMISSIVEMAP);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_VOLUMETRICLIGHTMAP, 2);
		GLSL_SetUniformInt(&tr.genericShader[i], UNIFORM_SCREENDEPTHMAP, TB_SHADOWMAP);
		GLSL_SetFroxelLookupUnits(&tr.genericShader[i]);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.genericShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramFogPass(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("fogpass\0");

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("fogpass", allocator, fallback_fogpassProgram);
	const GPUShaderDesc *volumetricLibrary = LoadVolumetricLibrary(allocator);
	const GPUShaderDesc *leafFlutterLibrary = LoadLeafFlutterLibrary(allocator);
	for (int i = 0; i < FOGDEF_COUNT; i++)
	{
		if (!GLSL_IsValidPermutationForFog(i))
		{
			continue;
		}

		uint32_t attribs =
			(ATTR_POSITION | ATTR_NORMAL | ATTR_TEXCOORD0);
		Q_strncpyz(name, "fogpass\0", nameLen + 1);
		extradefines[0] = '\0';

		if (i & FOGDEF_USE_DEFORM_VERTEXES)
		{
			Q_strcat(name, sizeof(name), "_DEFORM");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");
		}

#ifdef REND2_SP_MD3
		if (i & FOGDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_VA");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & FOGDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_SK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & FOGDEF_USE_FALLBACK_GLOBAL_FOG)
		{
			Q_strcat(name, sizeof(name), "_FALLBACK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FALLBACK_GLOBAL_FOG\n");
		}
		/*if (i & FOGDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");*/
		if (r_volumetricFog->integer)
		{
			Q_strcat(name, sizeof(name), "_VOLUMETRIC");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VOLUMETRIC_FOG\n");
		}

		if (!GLSL_LoadGPUShader(builder, &tr.fogShader[i], name, attribs, NO_XFB_VARS,
				extradefines, *programDesc, volumetricLibrary, leafFlutterLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load fogpass shader!");
		}

		GLSL_InitUniforms(&tr.fogShader[i]);

		qglUseProgram(tr.fogShader[i].program);
		//if (i & FOGDEF_USE_ALPHA_TEST)
		GLSL_SetUniformInt(&tr.fogShader[i], UNIFORM_DIFFUSEMAP, 0);
		GLSL_SetUniformInt(&tr.fogShader[i], UNIFORM_VOLUMETRICLIGHTMAP, 2);
		GLSL_SetFroxelLookupUnits(&tr.fogShader[i]);

		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.fogShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramVelocityPass(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("velocity\0");

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("velocity", allocator, fallback_velocityProgram);
	const GPUShaderDesc *leafFlutterLibrary = LoadLeafFlutterLibrary(allocator);
	for (int i = 0; i < VELOCITYDEF_COUNT; i++)
	{
		if (!GLSL_IsValidPermutationForFog(i))
		{
			continue;
		}

		uint32_t attribs =
			(ATTR_POSITION | ATTR_NORMAL | ATTR_TEXCOORD0);
		Q_strncpyz(name, "velocity\0", nameLen + 1);
		extradefines[0] = '\0';

		if (i & VELOCITYDEF_USE_DEFORM_VERTEXES)
		{
			Q_strcat(name, sizeof(name), "_DEFORM");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");
		}

		if (i & VELOCITYDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(name, sizeof(name), "_TCGENMOD");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}

		if (i & VELOCITYDEF_USE_RGBAGEN)
		{
			Q_strcat(name, sizeof(name), "_RGBGEN");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_RGBAGEN\n");
		}

#ifdef REND2_SP_MD3
		if (i & FOGDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_VA");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & VELOCITYDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_SK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		if (i & VELOCITYDEF_USE_PARALLAXMAP)
		{
			Q_strcat(name, sizeof(name), "_NH");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_PARALLAXMAP\n");
			attribs |= ATTR_TANGENT;
		}

		if (!GLSL_LoadGPUShader(builder, &tr.velocityShader[i], name, attribs, NO_XFB_VARS,
			extradefines, *programDesc, nullptr, leafFlutterLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load velocity shader!");
		}

		GLSL_InitUniforms(&tr.velocityShader[i]);

		qglUseProgram(tr.velocityShader[i].program);
		//if (i & FOGDEF_USE_ALPHA_TEST)
		GLSL_SetUniformInt(&tr.velocityShader[i], UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
		GLSL_SetUniformInt(&tr.velocityShader[i], UNIFORM_NORMALMAP, TB_NORMALMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.velocityShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramRefraction(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("refraction\0");

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("refraction", allocator, fallback_refractionProgram);
	const GPUShaderDesc *outputTransform = LoadOutputTransformLibrary(allocator);
	for (int i = 0; i < REFRACTIONDEF_COUNT; i++)
	{
		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_NORMAL | ATTR_COLOR;
		Q_strncpyz(name, "refraction\0", nameLen + 1);
		extradefines[0] = '\0';

		if (i & REFRACTIONDEF_USE_DEFORM_VERTEXES)
		{
			Q_strcat(name, sizeof(name), "_DEFORM");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_DEFORM_VERTEXES\n");
		}

		if (i & REFRACTIONDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(name, sizeof(name), "_TCGENMOD");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}

		if (i & REFRACTIONDEF_USE_RGBAGEN)
		{
			Q_strcat(name, sizeof(name), "_RGBGEN");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_RGBAGEN\n");
		}
#ifdef REND2_SP_MD3
		if (i & REFRACTIONDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_VA");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;
		}
#endif // REND2_SP
		if (i & REFRACTIONDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_SK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		/*if (i & REFRACTIONDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");*/

		if (i & REFRACTIONDEF_USE_SRGB_TRANSFORM)
		{
			Q_strcat(name, sizeof(name), "_SRGB");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_LINEAR_LIGHT\n");
		}

		if (!GLSL_LoadGPUShader(builder, &tr.refractionShader[i], name, attribs, NO_XFB_VARS,
			extradefines, *programDesc, outputTransform))
		{
			ri.Error(ERR_FATAL, "Could not load refraction shader!");
		}

		GLSL_InitUniforms(&tr.refractionShader[i]);

		qglUseProgram(tr.refractionShader[i].program);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_TEXTUREMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_LEVELSMAP, TB_LEVELSMAP);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_SCREENDEPTHMAP, TB_SHADOWMAP);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_COLORGRADINGLUT, TB_COLORGRADINGLUT);
		GLSL_SetUniformInt(&tr.refractionShader[i], UNIFORM_BLOOMMAP, TB_SPECULARMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.refractionShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

// Silhouette POM (tr_pom_silhouette.cpp, r_pomSilhouette 1 at load): the
// fragment functions of glsl/pom_silhouette.glsl
static const GPUShaderDesc *LoadPomSilhouetteLibrary( Allocator& allocator )
{
	const GPUProgramDesc *programDesc =
		LoadProgramSource("pom_silhouette", allocator, fallback_pom_silhouetteProgram);
	for ( size_t i = 0; i < programDesc->numShaders; ++i )
	{
		if ( programDesc->shaders[i].type == GPUSHADER_FRAGMENT )
		{
			return &programDesc->shaders[i];
		}
	}

	ri.Error(ERR_FATAL, "Could not load pom_silhouette shader library!");
	return nullptr;
}

// Two libraries of one stage in one (a program takes one): b follows a with its
// own line numbers, errors in b are reported as source string 2
static const GPUShaderDesc *GLSL_CombineLibraries(
	Allocator& allocator, const GPUShaderDesc *a, const GPUShaderDesc *b )
{
	if ( !a )
		return b;
	if ( !b )
		return a;

	const char *lineDirective = va("\n#line %d 2\n", b->firstLineNumber - 1);
	const size_t size = strlen(a->source) + strlen(lineDirective) + strlen(b->source) + 1;
	char *source = ojkAllocArray<char>(allocator, size);
	Q_strncpyz(source, a->source, size);
	Q_strcat(source, size, lineDirective);
	Q_strcat(source, size, b->source);

	GPUShaderDesc *combined = ojkAlloc<GPUShaderDesc>(allocator);
	combined->type = a->type;
	combined->source = source;
	combined->firstLineNumber = a->firstLineNumber;
	return combined;
}

static bool GLSL_PomSilhouetteEnabled( void )
{
	if ( !r_pomSilhouette->integer || !r_normalMapping->integer )
		return false;

	GLint maxFragmentSamplers = 0;
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentSamplers);
	if ( maxFragmentSamplers <= TB_POM_GROUPS )
	{
		static bool warned = false;
		if ( !warned )
			ri.Printf(PRINT_WARNING, "r_pomSilhouette: %d fragment texture units, %d needed, disabled\n",
				maxFragmentSamplers, TB_POM_GROUPS + 1);
		warned = true;
		return false;
	}
	return true;
}

// POMSDEF_* index of the silhouette variant of lightall permutation i, -1 if
// the permutation has none (world surfaces only, see R_PomSilhouetteShaderReason)
static int GLSL_PomSilhouetteLightallIndex( int i )
{
	const int lightType = i & LIGHTDEF_LIGHTTYPE_MASK;
	if ( !(i & LIGHTDEF_USE_PARALLAXMAP) ||
		(lightType != LIGHTDEF_USE_LIGHTMAP && lightType != LIGHTDEF_USE_LIGHT_VERTEX) ||
		(i & (LIGHTDEF_USE_TCGEN_AND_TCMOD | LIGHTDEF_USE_SKELETAL_ANIMATION)) )
		return -1;
#ifdef REND2_SP_MD3
	if ( i & LIGHTDEF_USE_VERTEX_ANIMATION )
		return -1;
#endif
	int index = 0;
	if ( lightType == LIGHTDEF_USE_LIGHT_VERTEX )
		index |= POMSDEF_LIGHT_VERTEX;
	if ( i & LIGHTDEF_USE_SPEC_GLOSS )
		index |= POMSDEF_SPEC_GLOSS;
	if ( i & LIGHTDEF_USE_CLOTH_BRDF )
		index |= POMSDEF_CLOTH_BRDF;
	return index;
}

static void GLSL_SetPomSilhouetteUnits( shaderProgram_t *program )
{
	GLSL_SetUniformInt(program, UNIFORM_NORMALMAP, TB_NORMALMAP);
	GLSL_SetUniformInt(program, UNIFORM_POMGROUPS, TB_POM_GROUPS);
}

static int GLSL_LoadGPUProgramLightAll(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("lightall\0");

	char extradefines[1600];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("lightall", allocator, fallback_lightallProgram);
	const GPUShaderDesc *pomLibrary = nullptr;
	const GPUShaderDesc *leafFlutterLibrary = LoadLeafFlutterLibrary(allocator);
	const bool useFastLight =
		(!r_normalMapping->integer && !r_specularMapping->integer);
	GLint maxFragmentSamplers = 0;
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentSamplers);
	const bool useEntityGpuGrid =
		(r_entityLightGrid->integer == 2 || r_entityLightGridDebug->integer != 0) &&
		maxFragmentSamplers > TB_ENTITYGRID_DIRECTION;
	const bool useEntityGrid = r_entityLightGrid->integer == 1 || useEntityGpuGrid;
	for ( int i = 0; i < LIGHTDEF_COUNT; i++ )
	{
		int lightType = i & LIGHTDEF_LIGHTTYPE_MASK;

		// skip impossible combos
		if (!GLSL_IsValidPermutationForLight (lightType, i))
			continue;

		uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0 | ATTR_COLOR | ATTR_NORMAL;

		Q_strncpyz(name, "lightall\0", nameLen + 1);
		extradefines[0] = '\0';

		if (r_hdr->integer && !glRefConfig.floatLightmap)
			Q_strcat(extradefines, sizeof(extradefines), "#define RGBM_LIGHTMAP\n");

		// r_forwardPlusDebug views (latched): only compiled when asked for
		if (r_forwardPlusDebug->integer)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FPLUS_DEBUG\n");

		// LTC area lights (latched, tr_arealights.cpp): the rectangle
		// integration is only compiled in when enabled, off costs nothing
		if (r_ltcAreaLights->integer && maxFragmentSamplers > TB_LTC_AMPLITUDE)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_LTC\n");
			if (r_ltcDebug->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_LTC_DEBUG\n");
		}

		// POM self shadow rays and debug views (latched, tr_pom.cpp): only in
		// the parallax permutations, compiled only when asked for
		if (i & LIGHTDEF_USE_PARALLAXMAP)
		{
			if (r_pomSelfShadow->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_POM_SELFSHADOW\n");
			if (r_pomDebug->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_POM_DEBUG\n");
		}

		if (lightType)
		{
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT\n");

			if (useFastLight && !(lightType == LIGHTDEF_USE_LIGHT_VECTOR && useEntityGrid))
			{
				Q_strcat(name, sizeof(name), "_FAST");
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_FAST_LIGHT\n");
			}
			if (r_dlightMode->integer >= 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_DSHADOWS\n");

			switch (lightType)
			{
				case LIGHTDEF_USE_LIGHTMAP:
				{
					Q_strcat(name, sizeof(name), "_LMAP");
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHTMAP\n");

					if (r_deluxeMapping->integer && !useFastLight)
						Q_strcat(extradefines, sizeof(extradefines), "#define USE_DELUXEMAP\n");

					attribs |= ATTR_TEXCOORD1 | ATTR_LIGHTDIRECTION;
					break;
				}

				case LIGHTDEF_USE_LIGHT_VECTOR:
				{
					Q_strcat(name, sizeof(name), "_GRID");
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT_VECTOR\n");
					Q_strcat(extradefines, sizeof(extradefines), va("#define ENTITY_GRID_LDR_RANGE %d.0\n", MAXLIGHTMAPS));
					if (useEntityGrid)
						Q_strcat(extradefines, sizeof(extradefines), "#define USE_ENTITY_GRID\n");
					if (useEntityGpuGrid)
						Q_strcat(extradefines, sizeof(extradefines), "#define USE_ENTITY_GPU_GRID\n");
					break;
				}

				case LIGHTDEF_USE_LIGHT_VERTEX:
				{
					Q_strcat(name, sizeof(name), "_VERT");
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_LIGHT_VERTEX\n");
					attribs |= ATTR_LIGHTDIRECTION;
					break;
				}

				default:
					break;
			}

			if (r_normalMapping->integer)
			{
				Q_strcat(name, sizeof(name), "_N");
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_NORMALMAP\n");

				if (i & LIGHTDEF_USE_PARALLAXMAP)
				{
					Q_strcat(name, sizeof(name), "H");
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_PARALLAXMAP\n");
				}
				attribs |= ATTR_TANGENT;
			}

			if (r_specularMapping->integer)
			{
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SPECULARMAP\n");
				if (i & LIGHTDEF_USE_SPEC_GLOSS)
				{
					Q_strcat(name, sizeof(name), "_SG");
					Q_strcat(extradefines, sizeof(extradefines), "#define USE_SPECGLOSS\n");
				}
				else
				{
					Q_strcat(name, sizeof(name), "_MR");
				}
			}

			if (r_cubeMapping->integer)
			{
				Q_strcat(name, sizeof(name), "_ENV");
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_CUBEMAP\n");
			}
			if (r_diffuseIBL->integer)
			{
				Q_strcat(name, sizeof(name), "_DIBL");
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_DIFFUSE_IBL\n");
			}
		}

		if (r_sunlightMode->integer)
		{
			Q_strcat(name, sizeof(name), "_SUN");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOWMAP\n");

			if (r_sunlightMode->integer == 1)
				Q_strcat(extradefines, sizeof(extradefines), "#define SHADOWMAP_MODULATE\n");
			else if (r_sunlightMode->integer == 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_PRIMARY_LIGHT\n");

			if (r_shadowFilter->integer >= 1)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOW_FILTER\n");

			if (r_shadowFilter->integer >= 2)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SHADOW_FILTER2\n");

			Q_strcat(
				extradefines, sizeof(extradefines),
				va("#define r_shadowMapSize %d\n", r_shadowMapSize->integer));
			Q_strcat(
				extradefines, sizeof(extradefines),
				va("#define r_shadowCascadeZFar %f\n", r_shadowCascadeZFar->value));
		}

		if (i & LIGHTDEF_USE_TCGEN_AND_TCMOD)
		{
			Q_strcat(name, sizeof(name), "_TCGENMOD");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCGEN\n");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_TCMOD\n");
		}

		if (i & LIGHTDEF_USE_CLOTH_BRDF)
		{
			Q_strcat(name, sizeof(name), "_CLOTH");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_CLOTH_BRDF\n");
		}
#ifdef REND2_SP_MD3
		if (i & LIGHTDEF_USE_VERTEX_ANIMATION)
		{
			Q_strcat(name, sizeof(extradefines), "_VA");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTEX_ANIMATION\n");
			attribs |= ATTR_POSITION2 | ATTR_NORMAL2;

			if (r_normalMapping->integer)
				attribs |= ATTR_TANGENT2;
		}
		else
#endif // REND2_SP
		if (i & LIGHTDEF_USE_SKELETAL_ANIMATION)
		{
			Q_strcat(name, sizeof(name), "_SK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");
			attribs |= ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;
		}

		/*if (i & LIGHTDEF_USE_ALPHA_TEST)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_ALPHA_TEST\n");*/

		/*if (i & LIGHTDEF_USE_GLOW_BUFFER)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_GLOW_BUFFER\n");*/

		if (!GLSL_LoadGPUShader(builder, &tr.lightallShader[i], name, attribs, NO_XFB_VARS,
				extradefines, *programDesc, nullptr, leafFlutterLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load lightall shader!");
		}

		shaderProgram_t *program = &tr.lightallShader[i];
		for ( int variant = 0; variant < 2; variant++ )
		{
			if ( variant == 1 )
			{
				// silhouette POM set (tr_pom_silhouette.cpp): the same permutation
				// with the shell trace, world lightall stages only
				const int pomIndex = GLSL_PomSilhouetteLightallIndex(i);
				if ( pomIndex < 0 || !GLSL_PomSilhouetteEnabled() )
					break;
				if ( !pomLibrary )
					pomLibrary = LoadPomSilhouetteLibrary(allocator);
				program = &tr.lightallSilhouetteShader[pomIndex];
				Q_strcat(name, sizeof(name), "_SPOM");
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_SILHOUETTE_POM\n");
				if (!GLSL_LoadGPUShader(builder, program, name, attribs | ATTR_POSITION2 | ATTR_TANGENT,
						NO_XFB_VARS, extradefines, *programDesc, pomLibrary, leafFlutterLibrary))
				{
					ri.Error(ERR_FATAL, "Could not load lightall silhouette POM shader!");
				}
			}

			GLSL_InitUniforms(program);

			qglUseProgram(program->program);
			GLSL_SetUniformInt(program, UNIFORM_DIFFUSEMAP,  TB_DIFFUSEMAP);
			GLSL_SetUniformInt(program, UNIFORM_LIGHTMAP,    TB_LIGHTMAP);
			GLSL_SetUniformInt(program, UNIFORM_NORMALMAP,   TB_NORMALMAP);
			GLSL_SetUniformInt(program, UNIFORM_DELUXEMAP,   TB_DELUXEMAP);
			GLSL_SetUniformInt(program, UNIFORM_SPECULARMAP, TB_SPECULARMAP);
			GLSL_SetUniformInt(program, UNIFORM_SHADOWMAP,   TB_SHADOWMAP);
			GLSL_SetUniformInt(program, UNIFORM_CUBEMAP,     TB_CUBEMAP);
			GLSL_SetUniformInt(program, UNIFORM_ENVBRDFMAP,  TB_ENVBRDFMAP);
			GLSL_SetUniformInt(program, UNIFORM_DIFFUSEIRRADIANCEMAP, TB_DIFFUSEIRRADIANCEMAP);
			GLSL_SetUniformInt(program, UNIFORM_PROBEAVERAGEMAP, TB_PROBEAVERAGEMAP);
			GLSL_SetUniformInt(program, UNIFORM_SHADOWMAP2,  TB_SHADOWMAPARRAY);
			GLSL_SetUniformInt(program, UNIFORM_SSAOMAP,     TB_SSAOMAP);
			GLSL_SetUniformInt(program, UNIFORM_EMISSIVEMAP, TB_EMISSIVEMAP);
			GLSL_SetUniformInt(program, UNIFORM_ENTITYGRIDAMBIENT, TB_ENTITYGRID_AMBIENT);
			GLSL_SetUniformInt(program, UNIFORM_ENTITYGRIDDIRECTED, TB_ENTITYGRID_DIRECTED);
			GLSL_SetUniformInt(program, UNIFORM_ENTITYGRIDDIRECTION, TB_ENTITYGRID_DIRECTION);
			// always set: an unset buffer sampler would alias unit 0 (u_DiffuseMap)
			GLSL_SetUniformInt(program, UNIFORM_FPLUSLIGHTS,  TB_FPLUS_LIGHTS);
			GLSL_SetUniformInt(program, UNIFORM_FPLUSGRID,    TB_FPLUS_GRID);
			GLSL_SetUniformInt(program, UNIFORM_FPLUSINDICES, TB_FPLUS_INDICES);
			GLSL_SetUniformInt(program, UNIFORM_WEATHERDEPTHMAP, TB_WEATHERDEPTH);
			GLSL_SetUniformInt(program, UNIFORM_LTCMATRIXMAP, TB_LTC_MATRIX);
			GLSL_SetUniformInt(program, UNIFORM_LTCAMPLITUDEMAP, TB_LTC_AMPLITUDE);
			GLSL_SetUniformInt(program, UNIFORM_SKINMASKMAP, TB_SKINMASK);
			if ( variant == 1 )
				GLSL_SetPomSilhouetteUnits(program);
			qglUseProgram(0);

			GLSL_FinishGPUShader(program);
		}

		++numPrograms;
	}

	return numPrograms;
}

// depth prepass / sun cascade and fog pass programs of silhouette POM shells
static int GLSL_LoadGPUProgramPomSilhouette(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	if ( !GLSL_PomSilhouetteEnabled() )
		return 0;

	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	const GPUShaderDesc *pomLibrary = LoadPomSilhouetteLibrary(allocator);

	const GPUProgramDesc *depthDesc =
		LoadProgramSource("pom_silhouette_depth", allocator, fallback_pom_silhouette_depthProgram);
	for ( int i = 0; i < POMSDEF_DEPTH_COUNT; i++ )
	{
		const uint32_t attribs =
			ATTR_POSITION | ATTR_NORMAL | ATTR_TANGENT | ATTR_TEXCOORD0 | ATTR_POSITION2;
		const char *name = (i == POMSDEF_DEPTH_VELOCITY) ? "pom_silhouette_velocity" : "pom_silhouette_depth";
		const char *defines = (i == POMSDEF_DEPTH_VELOCITY) ?
			"#define USE_SILHOUETTE_POM\n#define USE_VELOCITY\n" : "#define USE_SILHOUETTE_POM\n";

		if (!GLSL_LoadGPUShader(builder, &tr.pomSilhouetteDepthShader[i], name, attribs, NO_XFB_VARS,
				defines, *depthDesc, pomLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load pom_silhouette_depth shader!");
		}

		GLSL_InitUniforms(&tr.pomSilhouetteDepthShader[i]);
		qglUseProgram(tr.pomSilhouetteDepthShader[i].program);
		GLSL_SetPomSilhouetteUnits(&tr.pomSilhouetteDepthShader[i]);
		qglUseProgram(0);
		GLSL_FinishGPUShader(&tr.pomSilhouetteDepthShader[i]);
		++numPrograms;
	}

	// fog pass of shells: FOGDEF permutation 0 / FOGDEF_USE_FALLBACK_GLOBAL_FOG
	const GPUProgramDesc *fogDesc =
		LoadProgramSource("fogpass", allocator, fallback_fogpassProgram);
	const GPUShaderDesc *fogLibrary =
		GLSL_CombineLibraries(allocator, LoadVolumetricLibrary(allocator), pomLibrary);
	for ( int i = 0; i < 2; i++ )
	{
		char name[64];
		char extradefines[512];
		const uint32_t attribs =
			ATTR_POSITION | ATTR_NORMAL | ATTR_TANGENT | ATTR_TEXCOORD0 | ATTR_POSITION2;
		Q_strncpyz(name, "fogpass_SPOM", sizeof(name));
		Q_strncpyz(extradefines, "#define USE_SILHOUETTE_POM\n", sizeof(extradefines));
		if (i)
		{
			Q_strcat(name, sizeof(name), "_FALLBACK");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_FALLBACK_GLOBAL_FOG\n");
		}
		if (r_volumetricFog->integer)
		{
			Q_strcat(name, sizeof(name), "_VOLUMETRIC");
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VOLUMETRIC_FOG\n");
		}

		if (!GLSL_LoadGPUShader(builder, &tr.fogSilhouetteShader[i], name, attribs, NO_XFB_VARS,
				extradefines, *fogDesc, fogLibrary, LoadLeafFlutterLibrary(allocator)))
		{
			ri.Error(ERR_FATAL, "Could not load fogpass silhouette POM shader!");
		}

		GLSL_InitUniforms(&tr.fogSilhouetteShader[i]);
		qglUseProgram(tr.fogSilhouetteShader[i].program);
		GLSL_SetUniformInt(&tr.fogSilhouetteShader[i], UNIFORM_DIFFUSEMAP, 0);
		GLSL_SetUniformInt(&tr.fogSilhouetteShader[i], UNIFORM_VOLUMETRICLIGHTMAP, 2);
		GLSL_SetFroxelLookupUnits(&tr.fogSilhouetteShader[i]);
		GLSL_SetPomSilhouetteUnits(&tr.fogSilhouetteShader[i]);
		qglUseProgram(0);
		GLSL_FinishGPUShader(&tr.fogSilhouetteShader[i]);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramBasicWithDefinitions(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc,
	shaderProgram_t *shaderProgram,
	const char *programName,
	const GPUProgramDesc& programFallback,
	const char *extraDefines,
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0,
	const uint32_t xfbVariables = NO_XFB_VARS)
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	const GPUProgramDesc *programDesc =
		LoadProgramSource(programName, allocator, programFallback);
	if (!GLSL_LoadGPUShader(
			builder,
			shaderProgram,
			programName,
			attribs,
			xfbVariables,
			extraDefines,
			*programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load %s shader!", programName);
	}

	return 1;
}

static int GLSL_LoadGPUProgramBasic(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc,
	shaderProgram_t *shaderProgram,
	const char *programName,
	const GPUProgramDesc& programFallback,
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0,
	const uint32_t xfbVariables = NO_XFB_VARS)
{
	return GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		shaderProgram,
		programName,
		programFallback,
		nullptr,
		attribs,
		xfbVariables);
}

static int GLSL_LoadGPUProgramTextureColor(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	const char *extradefines = "#define USE_VERTICES\n";
	GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES],
		"texturecolor",
		fallback_texturecolorProgram,
		extradefines);

	GLSL_InitUniforms(&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES]);

	qglUseProgram(tr.textureColorShader[TEXCOLORDEF_USE_VERTICES].program);
	GLSL_SetUniformInt(&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES]);

	GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		&tr.textureColorShader[TEXCOLORDEF_SCREEN_TRIANGLE],
		"texturecolor",
		fallback_texturecolorProgram,
		"",
		0);

	GLSL_InitUniforms(&tr.textureColorShader[TEXCOLORDEF_SCREEN_TRIANGLE]);

	qglUseProgram(tr.textureColorShader[TEXCOLORDEF_SCREEN_TRIANGLE].program);
	GLSL_SetUniformInt(&tr.textureColorShader[TEXCOLORDEF_SCREEN_TRIANGLE], UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.textureColorShader[TEXCOLORDEF_SCREEN_TRIANGLE]);

	return 2;
}

static int GLSL_LoadGPUProgramPShadow(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	const char *extradefines = "#define USE_PCF\n#define USE_DISCARD\n";

	GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		&tr.pshadowShader,
		"pshadow",
		fallback_pshadowProgram,
		extradefines,
		ATTR_POSITION | ATTR_NORMAL);

	GLSL_InitUniforms(&tr.pshadowShader);

	qglUseProgram(tr.pshadowShader.program);
	GLSL_SetUniformInt(&tr.pshadowShader, UNIFORM_SHADOWMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.pshadowShader);

	return 1;
}

static int GLSL_LoadGPUProgramVShadow(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("shadowvolume", allocator, fallback_shadowvolumeProgram);
	const uint32_t attribs = ATTR_POSITION | ATTR_BONE_INDEXES | ATTR_BONE_WEIGHTS;

	extradefines[0] = '\0';
	Q_strcat(extradefines, sizeof(extradefines), "#define USE_SKELETAL_ANIMATION\n");

	if (!GLSL_LoadGPUShader(builder, &tr.volumeShadowShader, "shadowvolume", attribs, NO_XFB_VARS,
		extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load shadowvolume shader!");
	}

	GLSL_InitUniforms(&tr.volumeShadowShader);
	GLSL_FinishGPUShader(&tr.volumeShadowShader);

	return 1;
}

static int GLSL_LoadGPUProgramDownscale4x(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.down4xShader,
		"down4x",
		fallback_down4xProgram);

	GLSL_InitUniforms(&tr.down4xShader);

	qglUseProgram(tr.down4xShader.program);
	GLSL_SetUniformInt(&tr.down4xShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.down4xShader);

	return 1;
}

static int GLSL_LoadGPUProgramBokeh(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.bokehShader,
		"bokeh",
		fallback_bokehProgram);

	GLSL_InitUniforms(&tr.bokehShader);

	qglUseProgram(tr.bokehShader.program);
	GLSL_SetUniformInt(&tr.bokehShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.bokehShader);

	return 1;
}

static int GLSL_LoadGPUProgramTonemap(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("tonemap", allocator, fallback_tonemapProgram);
	const GPUShaderDesc *outputTransform = LoadOutputTransformLibrary(allocator);
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;

	extradefines[0] = '\0';
	if (r_smaa->integer == 1)
	{
		Q_strcat(extradefines, sizeof(extradefines),
			va( "#define USE_SMAA\n"
				"#define SMAA_RT_METRICS vec4(1.0 / %f, 1.0 / %f, %f, %f)\n",
				(float)glConfig.vidWidth,
				(float)glConfig.vidHeight,
				(float)glConfig.vidWidth,
				(float)glConfig.vidHeight));

	}

	if (!GLSL_LoadGPUShader(builder, &tr.tonemapShader[0], "tonemap", attribs, NO_XFB_VARS,
		extradefines, *programDesc, outputTransform))
	{
		ri.Error(ERR_FATAL, "Could not load tonemap shader!");
	}

	Q_strcat(extradefines, sizeof(extradefines), "#define USE_LINEAR_LIGHT\n");
	if (!GLSL_LoadGPUShader(builder, &tr.tonemapShader[1], "tonemap_SRGB", attribs, NO_XFB_VARS,
		extradefines, *programDesc, outputTransform))
	{
		ri.Error(ERR_FATAL, "Could not load tonemap shader!");
	}

	for (int i = 0; i < 2; i++)
	{
		GLSL_InitUniforms(&tr.tonemapShader[i]);
		qglUseProgram(tr.tonemapShader[i].program);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_TEXTUREMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_LEVELSMAP, TB_LEVELSMAP);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_COLORGRADINGLUT, TB_COLORGRADINGLUT);
		GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_BLOOMMAP, TB_SPECULARMAP);
		if (r_smaa->integer == 1)
			GLSL_SetUniformInt(&tr.tonemapShader[i], UNIFORM_BLENDMAP, 2);

		qglUseProgram(0);
		GLSL_FinishGPUShader(&tr.tonemapShader[i]);
	}
	return 2;
}

static int GLSL_LoadGPUProgramCalcLuminanceLevel(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("calclevels4x", allocator, fallback_calclevels4xProgram);
	for ( int i = 0; i < 2; i++ )
	{
		const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
		extradefines[0] = '\0';

		if (!i)
			Q_strcat(extradefines, sizeof(extradefines), "#define FIRST_PASS\n");

		if (!GLSL_LoadGPUShader(builder, &tr.calclevels4xShader[i], "calclevels4x", attribs,
				NO_XFB_VARS, extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load calclevels4x shader!");
		}

		GLSL_InitUniforms(&tr.calclevels4xShader[i]);

		qglUseProgram(tr.calclevels4xShader[i].program);
		GLSL_SetUniformInt(&tr.calclevels4xShader[i], UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.calclevels4xShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramHighPass(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.highpassShader,
		"highpass",
		fallback_highpassProgram);

	GLSL_InitUniforms(&tr.highpassShader);

	qglUseProgram(tr.highpassShader.program);
	GLSL_SetUniformInt(&tr.highpassShader, UNIFORM_SCREENIMAGEMAP, TB_COLORMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.highpassShader);

	return 1;
}

static int GLSL_LoadGPUProgramSSAO(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.ssaoShader,
		"ssao",
		fallback_ssaoProgram);

	GLSL_InitUniforms(&tr.ssaoShader);

	qglUseProgram(tr.ssaoShader.program);
	GLSL_SetUniformInt(&tr.ssaoShader, UNIFORM_SCREENDEPTHMAP, TB_COLORMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.ssaoShader);

	return 1;
}

// GTAO, AO composite / contact shadows and AO debug views (tr_ao.cpp)
static int GLSL_LoadGPUProgramScreenSpaceAO(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	if (!R_AOResourcesEnabled())
		return 0;

	int numPrograms = 0;
	for (int i = 0; i < 2; i++)
	{
		shaderProgram_t *sp = &tr.gtaoDepthShader[i];
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			sp,
			"gtao_depth",
			fallback_gtao_depthProgram,
			i == 0 ? "#define LINEARIZE\n" : nullptr);

		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENDEPTHMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AODEPTHMAP, TB_COLORMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	// 1 = with the bent normal output (r_gtaoBentNormals)
	for (int i = 0; i < 2; i++)
	{
		shaderProgram_t *sp = &tr.gtaoShader[i];
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			sp,
			"gtao",
			fallback_gtaoProgram,
			i == 1 ? "#define BENT_NORMAL\n" : nullptr);
		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_AODEPTHMAP, TB_COLORMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	for (int i = 0; i < 2; i++)
	{
		shaderProgram_t *sp = &tr.gtaoDenoiseShader[i];
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			sp,
			"gtao_denoise",
			fallback_gtao_denoiseProgram,
			i == 1 ? "#define BENT_NORMAL\n" : nullptr);
		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_AOMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AODEPTHMAP, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AOBENTMAP, TB_NORMALMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	{
		shaderProgram_t *sp = &tr.aoCompositeShader;
		GLSL_LoadGPUProgramBasic(builder, scratchAlloc, sp, "ao_composite", fallback_ao_compositeProgram);
		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_AOMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENDEPTHMAP, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AODEPTHMAP, TB_NORMALMAP);
		GLSL_SetUniformInt(sp, UNIFORM_LEGACYAOMAP, TB_DELUXEMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AOBENTMAP, TB_SPECULARMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	{
		shaderProgram_t *sp = &tr.aoDebugShader;
		GLSL_LoadGPUProgramBasic(builder, scratchAlloc, sp, "ao_debug", fallback_ao_debugProgram);
		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENIMAGEMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_AODEPTHMAP, TB_LIGHTMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramMotionBlur(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	// Always built: GPU shaders survive a map change, r_motionBlur (latched)
	// may be turned on in between
	static const char *defines[MOTIONBLURDEF_COUNT] =
	{
		nullptr,
		"#define USE_LOW_QUALITY\n",
		"#define USE_DEBUG\n",
	};

	int numPrograms = 0;
	for (int i = 0; i < MOTIONBLURDEF_COUNT; i++)
	{
		shaderProgram_t *sp = &tr.motionBlurShader[i];
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			sp,
			"motionblur",
			fallback_motionblurProgram,
			defines[i]);

		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENIMAGEMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_VELOCITYMAP, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENDEPTHMAP, TB_NORMALMAP);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	}

	return numPrograms;
}

// Screen-space passes: the shared depth pyramid (tr_screenspace.cpp),
// reflections (tr_ssr.cpp) and diffuse GI (tr_ssgi.cpp). Every program gets
// the fragment block of ssr_common.glsl (encodings, view space
// reconstruction, the ray march).
static int GLSL_LoadGPUProgramScreenSpace(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	if (!R_ScreenSpaceResourcesEnabled())
		return 0;

	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	const GPUShaderDesc *common = nullptr;
	{
		const GPUProgramDesc *commonDesc =
			LoadProgramSource("ssr_common", allocator, fallback_ssr_commonProgram);
		for ( size_t i = 0; i < commonDesc->numShaders; ++i )
		{
			if ( commonDesc->shaders[i].type == GPUSHADER_FRAGMENT )
				common = &commonDesc->shaders[i];
		}
		if ( !common )
			ri.Error(ERR_FATAL, "Could not load ssr_common shader library!");
	}

	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
	int numPrograms = 0;

	auto load = [&]( shaderProgram_t *sp, const char *name, const char *programName,
		const GPUProgramDesc& fallback, const char *defines )
	{
		const GPUProgramDesc *programDesc =
			LoadProgramSource(programName, allocator, fallback);
		if ( !GLSL_LoadGPUShader(builder, sp, name, attribs, NO_XFB_VARS,
				defines ? defines : "", *programDesc, common) )
		{
			ri.Error(ERR_FATAL, "Could not load %s shader!", name);
		}

		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENDEPTHMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRNORMALMAP, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRSPECULARMAP, TB_NORMALMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRCUBEMAPMAP, TB_DELUXEMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRSCENEMAP, TB_SPECULARMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRTRACEMAP, TB_SHADOWMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRHISTORYMAP, TB_CUBEMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRHISTORYGEOMMAP, TB_ENVBRDFMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSRHIZMAP, TB_SHADOWMAPARRAY);
		GLSL_SetUniformInt(sp, UNIFORM_VELOCITYMAP, TB_SSAOMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SSGIALBEDOMAP, TB_SSGI_ALBEDO);
		GLSL_SetUniformInt(sp, UNIFORM_SSGIRADIANCEMAP, TB_SSGI_RADIANCE);
		GLSL_SetUniformInt(sp, UNIFORM_SSGISOURCEMAP, TB_SSGI_SOURCE);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	};

	load(&tr.screenHiZShader[0], "ssr_hiz_linearize", "ssr_hiz", fallback_ssr_hizProgram, "#define LINEARIZE\n");
	load(&tr.screenHiZShader[1], "ssr_hiz", "ssr_hiz", fallback_ssr_hizProgram, nullptr);

	if (R_SSRResourcesEnabled())
	{
		load(&tr.ssrDownsampleShader, "ssr_downsample", "ssr_downsample", fallback_ssr_downsampleProgram, nullptr);
		load(&tr.ssrTraceShader[SSRDEF_TRACE], "ssr_trace", "ssr_trace", fallback_ssr_traceProgram, nullptr);
		load(&tr.ssrTraceShader[SSRDEF_TRACE_HIZ], "ssr_trace_hiz", "ssr_trace", fallback_ssr_traceProgram, "#define USE_HIZ\n");
		load(&tr.ssrResolveShader, "ssr_resolve", "ssr_resolve", fallback_ssr_resolveProgram, nullptr);
		load(&tr.ssrTemporalShader, "ssr_temporal", "ssr_temporal", fallback_ssr_temporalProgram, nullptr);
		load(&tr.ssrCompositeShader, "ssr_composite", "ssr_composite", fallback_ssr_compositeProgram, nullptr);
		load(&tr.ssrDebugShader, "ssr_debug", "ssr_debug", fallback_ssr_debugProgram, nullptr);
	}

	if (R_SSGIResourcesEnabled())
	{
		load(&tr.ssgiSourceShader, "ssgi_source", "ssgi_source", fallback_ssgi_sourceProgram, nullptr);
		load(&tr.ssgiTraceShader[SSGIDEF_TRACE], "ssgi_trace", "ssgi_trace", fallback_ssgi_traceProgram, nullptr);
		load(&tr.ssgiTraceShader[SSGIDEF_TRACE_HIZ], "ssgi_trace_hiz", "ssgi_trace", fallback_ssgi_traceProgram, "#define USE_HIZ\n");
		load(&tr.ssgiTemporalShader, "ssgi_temporal", "ssgi_temporal", fallback_ssgi_temporalProgram, nullptr);
		load(&tr.ssgiDenoiseShader, "ssgi_denoise", "ssgi_denoise", fallback_ssgi_denoiseProgram, nullptr);
		load(&tr.ssgiCompositeShader, "ssgi_composite", "ssgi_composite", fallback_ssgi_compositeProgram, nullptr);
		load(&tr.ssgiDebugShader, "ssgi_debug", "ssgi_debug", fallback_ssgi_debugProgram, nullptr);
	}

	if (R_SkinSSSResourcesEnabled())
	{
		load(&tr.skinSSSShader[SKINSSSDEF_BLUR_H], "skin_sss_h", "skin_sss", fallback_skin_sssProgram, "#define USE_HORIZONTAL\n");
		load(&tr.skinSSSShader[SKINSSSDEF_BLUR_V], "skin_sss_v", "skin_sss", fallback_skin_sssProgram, "#define USE_VERTICAL\n");
		load(&tr.skinSSSShader[SKINSSSDEF_COMPOSITE], "skin_sss_composite", "skin_sss", fallback_skin_sssProgram, "#define USE_COMPOSITE\n");
	}

	return numPrograms;
}

// Froxel volumetric fog (tr_volumetric.cpp). Every program gets the fragment
// block of volumetric_common.glsl.
// Persistent foliage bend field (tr_foliagefield.cpp): the update pass and the
// r_foliageFieldDebug 1 overlay, with the collider functions of
// foliage_interact.glsl as fragment library
static const GPUShaderDesc *LoadFoliageInteractLibrary( Allocator& allocator );

static int GLSL_LoadGPUProgramFoliageField(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	const GPUShaderDesc *library = LoadFoliageInteractLibrary(allocator);
	const GPUProgramDesc *programDesc =
		LoadProgramSource("foliage_field", allocator, fallback_foliage_fieldProgram);
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;

	struct { shaderProgram_t *sp; const char *name; const char *defines; } programs[] = {
		{ &tr.foliageFieldShader, "foliage_field", "" },
		{ &tr.foliageFieldDebugShader, "foliage_field_debug", "#define DEBUG_VIEW\n" },
	};
	for ( const auto& program : programs )
	{
		if ( !GLSL_LoadGPUShader(builder, program.sp, program.name, attribs, NO_XFB_VARS,
				program.defines, *programDesc, library) )
		{
			ri.Error(ERR_FATAL, "Could not load %s shader!", program.name);
		}
		GLSL_InitUniforms(program.sp);
		GLSL_FinishGPUShader(program.sp);
	}
	return 2;
}

static int GLSL_LoadGPUProgramVolumetric(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	if (!R_VolumetricFroxelEnabled())
		return 0;

	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());
	const GPUShaderDesc *common = LoadVolumetricLibrary(allocator);
	const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
	int numPrograms = 0;

	auto load = [&]( shaderProgram_t *sp, const char *name, const GPUProgramDesc& fallback )
	{
		const GPUProgramDesc *programDesc =
			LoadProgramSource(name, allocator, fallback);
		// the density noise is sampled by the injection and the debug views only
		const bool noise = (sp == &tr.volumetricInjectShader || sp == &tr.volumetricDebugShader);
		if ( !GLSL_LoadGPUShader(builder, sp, name, attribs, NO_XFB_VARS,
				noise ? "#define USE_FROXEL_NOISE\n" : "", *programDesc, common) )
		{
			ri.Error(ERR_FATAL, "Could not load %s shader!", name);
		}

		GLSL_InitUniforms(sp);
		qglUseProgram(sp->program);
		// every sampler on its own unit, see RB_VolumetricBuild / RB_VolumetricComposite
		GLSL_SetUniformInt(sp, UNIFORM_FROXELHISTORY, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELSOURCE, sp == &tr.volumetricDebugShader ? TB_LIGHTMAP : TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SCREENDEPTHMAP, TB_COLORMAP);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELCARRY, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_VOLUMETRICSTATICGRID, TB_LIGHTMAP);
		GLSL_SetUniformInt(sp, UNIFORM_VOLUMETRICSUNGRID, TB_NORMALMAP);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELDYNAMIC, TB_NORMALMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SHADOWMAP, TB_SHADOWMAP);
		GLSL_SetUniformInt(sp, UNIFORM_SHADOWMAP2, TB_SHADOWMAPARRAY);
		GLSL_SetUniformInt(sp, UNIFORM_FROXELNOISE, TB_DELUXEMAP);
		GLSL_SetFroxelLookupUnits(sp);
		qglUseProgram(0);
		GLSL_FinishGPUShader(sp);
		++numPrograms;
	};

	load(&tr.volumetricInjectShader, "volumetric_inject", fallback_volumetric_injectProgram);
	load(&tr.volumetricIntegrateShader, "volumetric_integrate", fallback_volumetric_integrateProgram);
	load(&tr.volumetricCompositeShader, "volumetric_composite", fallback_volumetric_compositeProgram);
	load(&tr.volumetricDebugShader, "volumetric_debug", fallback_volumetric_debugProgram);

	return numPrograms;
}

static int GLSL_LoadGPUProgramPrefilterEnvMap(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.prefilterEnvMapShader,
		"prefilterEnvMap",
		fallback_prefilterEnvMapProgram);

	GLSL_InitUniforms(&tr.prefilterEnvMapShader);

	qglUseProgram(tr.prefilterEnvMapShader.program);
	GLSL_SetUniformInt(&tr.prefilterEnvMapShader, UNIFORM_CUBEMAP, TB_CUBEMAP);
	qglUseProgram(0);

	GLSL_FinishGPUShader(&tr.prefilterEnvMapShader);

	return 1;
}

static int GLSL_LoadGPUProgramDiffuseIrradiance(
	ShaderProgramBuilder& builder, Allocator& scratchAlloc)
{
	GLSL_LoadGPUProgramBasic(builder, scratchAlloc, &tr.probeAverageShader,
		"probeAverage", fallback_probeAverageProgram);
	GLSL_InitUniforms(&tr.probeAverageShader);
	qglUseProgram(tr.probeAverageShader.program);
	GLSL_SetUniformInt(&tr.probeAverageShader, UNIFORM_CUBEMAP, TB_CUBEMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.probeAverageShader);

	GLSL_LoadGPUProgramBasic(builder, scratchAlloc, &tr.diffuseIrradianceShader,
		"diffuseIrradiance", fallback_diffuseIrradianceProgram);
	GLSL_InitUniforms(&tr.diffuseIrradianceShader);
	qglUseProgram(tr.diffuseIrradianceShader.program);
	GLSL_SetUniformInt(&tr.diffuseIrradianceShader, UNIFORM_CUBEMAP, TB_CUBEMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.diffuseIrradianceShader);
	return 2;
}

static int GLSL_LoadGPUProgramDepthBlur(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("depthBlur", allocator, fallback_depthblurProgram);
	for ( int i = 0; i < 2; i++ )
	{
		const uint32_t attribs = ATTR_POSITION | ATTR_TEXCOORD0;
		extradefines[0] = '\0';

		if (i & 1)
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_VERTICAL_BLUR\n");
		else
			Q_strcat(extradefines, sizeof(extradefines), "#define USE_HORIZONTAL_BLUR\n");


		if (!GLSL_LoadGPUShader(builder, &tr.depthBlurShader[i], "depthBlur", attribs, NO_XFB_VARS,
				extradefines, *programDesc))
		{
			ri.Error(ERR_FATAL, "Could not load depthBlur shader!");
		}

		GLSL_InitUniforms(&tr.depthBlurShader[i]);

		qglUseProgram(tr.depthBlurShader[i].program);
		GLSL_SetUniformInt(&tr.depthBlurShader[i], UNIFORM_SCREENIMAGEMAP, TB_COLORMAP);
		GLSL_SetUniformInt(&tr.depthBlurShader[i], UNIFORM_SCREENDEPTHMAP, TB_LIGHTMAP);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.depthBlurShader[i]);

		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramGaussianBlur(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("gaussian_blur", allocator, fallback_gaussian_blurProgram);
	const uint32_t attribs = 0;

	extradefines[0] = '\0';
	Q_strcat (extradefines, sizeof (extradefines), "#define BLUR_X");

	if (!GLSL_LoadGPUShader(builder, &tr.gaussianBlurShader[0], "gaussian_blur", attribs,
			NO_XFB_VARS, extradefines, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load gaussian_blur (X-direction) shader!");
	}

	if (!GLSL_LoadGPUShader(builder, &tr.gaussianBlurShader[1], "gaussian_blur", attribs,
			NO_XFB_VARS, nullptr, *programDesc))
	{
		ri.Error(ERR_FATAL, "Could not load gaussian_blur (Y-direction) shader!");
	}

	int numPrograms = 0;
	for ( int i = 0; i < 2; i++ )
	{
		GLSL_InitUniforms(&tr.gaussianBlurShader[i]);
		GLSL_FinishGPUShader(&tr.gaussianBlurShader[i]);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramDynamicGlowUpsample(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.dglowUpsample,
		"dglow_upsample",
		fallback_dglow_upsampleProgram,
		0);

	GLSL_InitUniforms(&tr.dglowUpsample);
	GLSL_FinishGPUShader(&tr.dglowUpsample);
	return 1;
}

static int GLSL_LoadGPUProgramDynamicGlowDownsample(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.dglowDownsample,
		"dglow_downsample",
		fallback_dglow_downsampleProgram,
		0);

	GLSL_InitUniforms(&tr.dglowDownsample);
	GLSL_FinishGPUShader(&tr.dglowDownsample);
	return 1;
}

static int GLSL_LoadGPUProgramBloomPrefilter(
	ShaderProgramBuilder& builder, Allocator& scratchAlloc)
{
	GLSL_LoadGPUProgramBasic(builder, scratchAlloc, &tr.bloomPrefilter,
		"bloom_prefilter", fallback_bloom_prefilterProgram, 0);
	GLSL_InitUniforms(&tr.bloomPrefilter);
	qglUseProgram(tr.bloomPrefilter.program);
	GLSL_SetUniformInt(&tr.bloomPrefilter, UNIFORM_TEXTUREMAP, TB_COLORMAP);
	GLSL_SetUniformInt(&tr.bloomPrefilter, UNIFORM_BLOOMSCENEMAP, TB_LIGHTMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.bloomPrefilter);
	return 1;
}

static int GLSL_LoadGPUProgramSurfaceSprites(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	int numPrograms = 0;
	Allocator allocator(scratchAlloc.Base(), scratchAlloc.GetSize());

	char name[64];
	size_t nameLen = strlen("surface_sprites\0");

	char extradefines[1200];
	const GPUProgramDesc *programDesc =
		LoadProgramSource("surface_sprites", allocator, fallback_surface_spritesProgram);
	const GPUShaderDesc *volumetricLibrary = LoadVolumetricLibrary(allocator);
	// r_foliageInteraction: the character colliders bend the grass
	const GPUShaderDesc *foliageInteractLibrary = LoadFoliageInteractLibrary(allocator);
	const uint32_t attribs = ATTR_POSITION | ATTR_POSITION2 | ATTR_NORMAL | ATTR_COLOR;
	for ( int i = 0; i < SSDEF_COUNT; ++i )
	{
		Q_strncpyz(name, "surface_sprites\0", nameLen + 1);
		extradefines[0] = '\0';

		if ( (i & SSDEF_FACE_CAMERA) && (i & SSDEF_FACE_UP) )
			continue;

		// auto grass replaces the vertical/oriented billboard only
		if ((i & SSDEF_AUTO_GRASS) && (i & (SSDEF_FACE_CAMERA | SSDEF_FACE_UP |
				SSDEF_FX_SPRITE | SSDEF_FOG_MODULATE | SSDEF_ADDITIVE | SSDEF_FLATTENED)))
			continue;

		if (i & SSDEF_FACE_CAMERA)
		{
			Q_strcat(name, sizeof(name), "_FACE_CAM");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FACE_CAMERA\n");
		}
		else if (i & SSDEF_FACE_UP)
		{
			Q_strcat(name, sizeof(name), "_FACE_UP");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FACE_UP\n");
		}
		else if (i & SSDEF_FLATTENED)
		{
			Q_strcat(name, sizeof(name), "_FLATTENED");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FACE_FLATTENED\n");
		}
		if (i & SSDEF_FX_SPRITE)
		{
			Q_strcat(name, sizeof(name), "_FX");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define FX_SPRITE\n");
		}
		if ( i & SSDEF_USE_FOG )
		{
			Q_strcat(name, sizeof(name), "_FOG");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define USE_FOG\n");
			if (r_volumetricFog->integer)
				Q_strcat(extradefines, sizeof(extradefines), "#define USE_VOLUMETRIC_FOG\n");
		}

		/*if ( i & SSDEF_ALPHA_TEST )
			Q_strcat(extradefines, sizeof(extradefines),
					"#define USE_ALPHA_TEST\n");*/

		if (i & SSDEF_ADDITIVE)
		{
			Q_strcat(name, sizeof(name), "_ADDITIVE");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define ADDITIVE_BLEND\n");
		}
		if (i & SSDEF_VELOCITY)
		{
			Q_strcat(name, sizeof(name), "_VELOCITY");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define VELOCITY_PASS\n");
		}
		if (i & SSDEF_AUTO_GRASS)
		{
			Q_strcat(name, sizeof(name), "_AUTOGRASS");
			Q_strcat(extradefines, sizeof(extradefines),
				"#define AUTO_GRASS\n");
		}
		shaderProgram_t *program = tr.spriteShader + i;
		if (!GLSL_LoadGPUShader(builder, program, name, attribs, NO_XFB_VARS,
				extradefines, *programDesc, volumetricLibrary, foliageInteractLibrary))
		{
			ri.Error(ERR_FATAL, "Could not load surface sprites shader!");
		}

		GLSL_InitUniforms(program);
		qglUseProgram(program->program);
		GLSL_SetUniformInt(program, UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
		GLSL_SetUniformInt(program, UNIFORM_VOLUMETRICLIGHTMAP, 2);
		GLSL_SetFroxelLookupUnits(program);
		qglUseProgram(0);
		GLSL_FinishGPUShader(program);
		++numPrograms;
	}

	return numPrograms;
}

static int GLSL_LoadGPUProgramWeather(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc )
{
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.weatherShader,
		"weather",
		fallback_weatherProgram,
		ATTR_POSITION | ATTR_COLOR);

	GLSL_InitUniforms(&tr.weatherShader);
	qglUseProgram(tr.weatherShader.program);
	GLSL_SetUniformInt(&tr.weatherShader, UNIFORM_SHADOWMAP, TB_SHADOWMAP);
	GLSL_SetUniformInt(&tr.weatherShader, UNIFORM_DIFFUSEMAP, TB_DIFFUSEMAP);
	// r_rainLighting: merged light grid, read in the vertex shader
	GLSL_SetUniformInt(&tr.weatherShader, UNIFORM_VOLUMETRICLIGHTMAP, TB_LIGHTMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.weatherShader);

	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.weatherUpdateShader,
		"weatherUpdate",
		fallback_weatherUpdateProgram,
		ATTR_POSITION | ATTR_COLOR,
		(1u << XFB_VAR_POSITION) | (1u << XFB_VAR_VELOCITY));

	GLSL_InitUniforms(&tr.weatherUpdateShader);
	GLSL_FinishGPUShader(&tr.weatherUpdateShader);

	// r_rainSplashes: the rain update with impact detection writes a 40 byte
	// record (tr_weather.cpp rainSplashVertex_t), the plain one keeps 24
	GLSL_LoadGPUProgramBasicWithDefinitions(
		builder,
		scratchAlloc,
		&tr.weatherUpdateSplashShader,
		"weatherUpdate",
		fallback_weatherUpdateProgram,
		"#define USE_RAIN_SPLASHES\n",
		ATTR_POSITION | ATTR_COLOR | ATTR_TEXCOORD0,
		(1u << XFB_VAR_POSITION) | (1u << XFB_VAR_VELOCITY) | (1u << XFB_VAR_IMPACT));

	GLSL_InitUniforms(&tr.weatherUpdateSplashShader);
	qglUseProgram(tr.weatherUpdateSplashShader.program);
	GLSL_SetUniformInt(&tr.weatherUpdateSplashShader, UNIFORM_SHADOWMAP, TB_SHADOWMAP);
	GLSL_SetUniformInt(&tr.weatherUpdateSplashShader, UNIFORM_WEATHERSURFACEMAP, TB_NORMALMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.weatherUpdateSplashShader);

	// r_rainSplashes: splashes drawn from the impact state of the rain VBO
	GLSL_LoadGPUProgramBasic(
		builder,
		scratchAlloc,
		&tr.weatherSplashShader,
		"weatherSplash",
		fallback_weatherSplashProgram,
		ATTR_POSITION | ATTR_COLOR | ATTR_TEXCOORD0);

	GLSL_InitUniforms(&tr.weatherSplashShader);
	qglUseProgram(tr.weatherSplashShader.program);
	GLSL_SetUniformInt(&tr.weatherSplashShader, UNIFORM_SHADOWMAP, TB_SHADOWMAP);
	GLSL_SetUniformInt(&tr.weatherSplashShader, UNIFORM_VOLUMETRICLIGHTMAP, TB_LIGHTMAP);
	qglUseProgram(0);
	GLSL_FinishGPUShader(&tr.weatherSplashShader);

	return 4;
}

static int GLSL_LoadGPUProgramSMAA(
	ShaderProgramBuilder& builder,
	Allocator& scratchAlloc)
{
	char extradefines[1200];
	extradefines[0] = '\0';
	
	Q_strcat(extradefines, sizeof(extradefines),
		va(	"#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0\n"
			"#define SMAA_REPROJECTION_WEIGHT_SCALE 30.0\n"
			"#define SMAA_RT_METRICS vec4(1.0 / %f, 1.0 / %f, %f, %f)\n",
			(float)glConfig.vidWidth,
			(float)glConfig.vidHeight,
			(float)glConfig.vidWidth,
			(float)glConfig.vidHeight));
	if (r_smaa_quality->integer == 0)
	{
		Q_strcat(extradefines, sizeof(extradefines),
			"#define SMAA_THRESHOLD 0.15\n"
			"#define SMAA_MAX_SEARCH_STEPS 4\n"
			"#define SMAA_DISABLE_DIAG_DETECTION\n"
			"#define SMAA_DISABLE_CORNER_DETECTION\n"
		);
	}
	else if (r_smaa_quality->integer == 1)
	{
		Q_strcat(extradefines, sizeof(extradefines),
			"#define SMAA_THRESHOLD 0.1\n"
			"#define SMAA_MAX_SEARCH_STEPS 8\n"
			"#define SMAA_DISABLE_DIAG_DETECTION\n"
			"#define SMAA_DISABLE_CORNER_DETECTION\n"
		);
	}
	else if (r_smaa_quality->integer == 2)
	{
		Q_strcat(extradefines, sizeof(extradefines),
			"#define SMAA_THRESHOLD 0.1\n"
			"#define SMAA_MAX_SEARCH_STEPS 16\n"
			"#define SMAA_MAX_SEARCH_STEPS_DIAG 8\n"
			"#define SMAA_CORNER_ROUNDING 25\n"
		);
	}
	else
	{
		Q_strcat(extradefines, sizeof(extradefines),
			"#define SMAA_THRESHOLD 0.05\n"
			"#define SMAA_MAX_SEARCH_STEPS 32\n"
			"#define SMAA_MAX_SEARCH_STEPS_DIAG 16\n"
			"#define SMAA_CORNER_ROUNDING 25\n"

		);
	}

	if (r_smaa->integer == 2)
	{
		Q_strcat(extradefines, sizeof(extradefines),
			"#define SMAA_REPROJECTION 1\n"
		);
	}
	
	{
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			&tr.smaaEdgeShader,
			"smaaEdge",
			fallback_smaaEdgeProgram,
			extradefines);

		GLSL_InitUniforms(&tr.smaaEdgeShader);

		qglUseProgram(tr.smaaEdgeShader.program);
		GLSL_SetUniformInt(&tr.smaaEdgeShader, UNIFORM_SCREENIMAGEMAP, 0);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.smaaEdgeShader);
	}
	{
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			&tr.smaaBlendShader,
			"smaaBlendWeight",
			fallback_smaaBlendWeightProgram,
			extradefines);

		GLSL_InitUniforms(&tr.smaaBlendShader);

		qglUseProgram(tr.smaaBlendShader.program);
		GLSL_SetUniformInt(&tr.smaaBlendShader, UNIFORM_EDGEMAP, 0);
		GLSL_SetUniformInt(&tr.smaaBlendShader, UNIFORM_AREAMAP, 1);
		GLSL_SetUniformInt(&tr.smaaBlendShader, UNIFORM_SEARCHMAP, 2);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.smaaBlendShader);
	}
	{
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			&tr.smaaResolveShader,
			"smaaResolve",
			fallback_smaaResolveProgram,
			extradefines);

		GLSL_InitUniforms(&tr.smaaResolveShader);

		qglUseProgram(tr.smaaResolveShader.program);
		GLSL_SetUniformInt(&tr.smaaResolveShader, UNIFORM_TEXTUREMAP, 0);
		GLSL_SetUniformInt(&tr.smaaResolveShader, UNIFORM_BLENDMAP, 1);
		if (r_smaa->integer == 2)
			GLSL_SetUniformInt(&tr.smaaResolveShader, UNIFORM_VELOCITYMAP, 2);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.smaaResolveShader);
	}
	{
		GLSL_LoadGPUProgramBasicWithDefinitions(
			builder,
			scratchAlloc,
			&tr.smaaTemporalResolveShader,
			"smaaTemporalResolve",
			fallback_smaaTemporalResolveProgram,
			extradefines);

		GLSL_InitUniforms(&tr.smaaTemporalResolveShader);

		qglUseProgram(tr.smaaTemporalResolveShader.program);
		GLSL_SetUniformInt(&tr.smaaTemporalResolveShader, UNIFORM_TEXTUREMAP, 0);
		GLSL_SetUniformInt(&tr.smaaTemporalResolveShader, UNIFORM_BLENDMAP, 1);
		GLSL_SetUniformInt(&tr.smaaTemporalResolveShader, UNIFORM_VELOCITYMAP, 2);
		qglUseProgram(0);

		GLSL_FinishGPUShader(&tr.smaaTemporalResolveShader);
	}
	return 4;
}

void GLSL_LoadGPUShaders()
{
#if 0
	// vertex size = 48 bytes
	VertexFormat bspVertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 2, false, GL_HALF_FLOAT, false, 16 }, // tc1
			{ 2, false, GL_HALF_FLOAT, false, 20 }, // tc2
			{ 2, false, GL_HALF_FLOAT, false, 24 }, // tc3
			{ 2, false, GL_HALF_FLOAT, false, 28 }, // tc4
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 32 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 36 }, // normal
			{ 4, false, GL_FLOAT, false, 40 }, // color
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 44 }, // light dir
		}
	};

	// vertex size = 32 bytes
	VertexFormat rectVertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_FLOAT, false, 16 } // color
		}
	};

	// vertex size = 32 bytes
	VertexFormat g2VertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 16 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 20 }, // normal
			{ 4, true,  GL_UNSIGNED_BYTE, false, 24 }, // bone indices
			{ 4, false, GL_UNSIGNED_BYTE, true, 28 }, // bone weights
		}
	};

	// vertex size = 44 bytes
	VertexFormat md3VertexFormat = {
		{
			{ 3, false, GL_FLOAT, false, 0 }, // position
			{ 2, false, GL_HALF_FLOAT, false, 12 }, // tc0
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 16 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 20 }, // normal
			{ 3, false,p GL_FLOAT, false, 24 }, // pos2
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 36 }, // tangent
			{ 4, false, GL_UNSIGNED_INT_2_10_10_10_REV, true, 40 }, // normal
		}
	};
#endif

	ri.Printf(PRINT_ALL, "------- GLSL_InitGPUShaders -------\n");

	R_IssuePendingRenderCommands();

	int startTime = ri.Milliseconds();

	Allocator allocator(512 * 1024);
	ShaderProgramBuilder builder;
	GLSL_CacheBegin();

	int numGenShaders = 0;
	int numLightShaders = 0;
	int numEtcShaders = 0;
	numGenShaders += GLSL_LoadGPUProgramGeneric(builder, allocator);
	numLightShaders += GLSL_LoadGPUProgramLightAll(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramFogPass(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramVelocityPass(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramPomSilhouette(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramRefraction(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramTextureColor(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramPShadow(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramVShadow(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDownscale4x(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramBokeh(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramTonemap(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramCalcLuminanceLevel(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramHighPass(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramSSAO(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramScreenSpaceAO(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramMotionBlur(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramScreenSpace(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramVolumetric(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramFoliageField(builder, allocator);
	if (r_cubeMapping->integer)
		numEtcShaders += GLSL_LoadGPUProgramPrefilterEnvMap(builder, allocator);
	if (r_diffuseIBL->integer)
		numEtcShaders += GLSL_LoadGPUProgramDiffuseIrradiance(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDepthBlur(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramGaussianBlur(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDynamicGlowUpsample(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramDynamicGlowDownsample(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramBloomPrefilter(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramSurfaceSprites(builder, allocator);
	numEtcShaders += GLSL_LoadGPUProgramWeather(builder, allocator);
	if (r_smaa->integer)
		numEtcShaders += GLSL_LoadGPUProgramSMAA(builder, allocator);

	GLSL_CacheEnd();

	ri.Printf(PRINT_ALL, "loaded %i GLSL shaders (%i gen %i light %i etc) in %5.2f seconds\n",
		numGenShaders + numLightShaders + numEtcShaders, numGenShaders, numLightShaders,
		numEtcShaders, (ri.Milliseconds() - startTime) / 1000.0);
	if ( s_glslCache.enabled )
		ri.Printf(PRINT_ALL, "GLSL cache: %i from %s, %i compiled and stored, %i rejected, %.1f MB\n",
			s_glslCache.hits, GLSL_CACHE_FILE, s_glslCache.stored, s_glslCache.rejected,
			s_glslCache.fileSize / (1024.0 * 1024.0));
	else
		ri.Printf(PRINT_ALL, "GLSL cache: off (%s)\n",
			r_glslCache->integer ? "no GL_ARB_get_program_binary" : "r_glslCache 0");
}

void GLSL_ShutdownGPUShaders(void)
{
	int i;

	ri.Printf(PRINT_ALL, "------- GLSL_ShutdownGPUShaders -------\n");

	for ( int i = 0; i < ATTR_INDEX_MAX; i++ )
		qglDisableVertexAttribArray(i);

	GLSL_BindNullProgram();

	GLSL_DeleteGPUShader(&tr.splashScreenShader);

	for ( i = 0; i < GENERICDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.genericShader[i]);

	for (i = 0; i < REFRACTIONDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.refractionShader[i]);

	for (i = 0; i < TEXCOLORDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.textureColorShader[i]);

	for ( i = 0; i < FOGDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.fogShader[i]);

	for (i = 0; i < VELOCITYDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.velocityShader[i]);

	for ( i = 0; i < LIGHTDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.lightallShader[i]);

	// silhouette POM, only loaded with r_pomSilhouette
	for ( i = 0; i < POMSDEF_LIGHTALL_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.lightallSilhouetteShader[i]);
	for ( i = 0; i < POMSDEF_DEPTH_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.pomSilhouetteDepthShader[i]);
	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.fogSilhouetteShader[i]);

	GLSL_DeleteGPUShader(&tr.pshadowShader);
	GLSL_DeleteGPUShader(&tr.volumeShadowShader);
	GLSL_DeleteGPUShader(&tr.down4xShader);
	GLSL_DeleteGPUShader(&tr.bokehShader);

	for (i = 0; i < 2; ++i)
		GLSL_DeleteGPUShader(&tr.tonemapShader[i]);

	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.calclevels4xShader[i]);

	GLSL_DeleteGPUShader(&tr.highpassShader);
	GLSL_DeleteGPUShader(&tr.ssaoShader);

	for ( i = 0; i < 2; i++)
	{
		GLSL_DeleteGPUShader(&tr.gtaoDepthShader[i]);
		GLSL_DeleteGPUShader(&tr.gtaoShader[i]);
		GLSL_DeleteGPUShader(&tr.gtaoDenoiseShader[i]);
	}
	GLSL_DeleteGPUShader(&tr.aoCompositeShader);
	GLSL_DeleteGPUShader(&tr.aoDebugShader);

	for ( i = 0; i < MOTIONBLURDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.motionBlurShader[i]);

	GLSL_DeleteGPUShader(&tr.volumetricInjectShader);
	GLSL_DeleteGPUShader(&tr.volumetricIntegrateShader);
	GLSL_DeleteGPUShader(&tr.volumetricCompositeShader);
	GLSL_DeleteGPUShader(&tr.volumetricDebugShader);
	GLSL_DeleteGPUShader(&tr.foliageFieldShader);
	GLSL_DeleteGPUShader(&tr.foliageFieldDebugShader);

	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.screenHiZShader[i]);
	GLSL_DeleteGPUShader(&tr.ssrDownsampleShader);
	for ( i = 0; i < SSRDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.ssrTraceShader[i]);
	GLSL_DeleteGPUShader(&tr.ssrResolveShader);
	GLSL_DeleteGPUShader(&tr.ssrTemporalShader);
	GLSL_DeleteGPUShader(&tr.ssrCompositeShader);
	GLSL_DeleteGPUShader(&tr.ssrDebugShader);
	GLSL_DeleteGPUShader(&tr.ssgiSourceShader);
	for ( i = 0; i < SSGIDEF_COUNT; i++)
		GLSL_DeleteGPUShader(&tr.ssgiTraceShader[i]);
	GLSL_DeleteGPUShader(&tr.ssgiTemporalShader);
	GLSL_DeleteGPUShader(&tr.ssgiDenoiseShader);
	GLSL_DeleteGPUShader(&tr.ssgiCompositeShader);
	GLSL_DeleteGPUShader(&tr.ssgiDebugShader);
	for ( int i = 0; i < SKINSSSDEF_COUNT; i++ )
		GLSL_DeleteGPUShader(&tr.skinSSSShader[i]);

	for ( i = 0; i < 2; i++)
		GLSL_DeleteGPUShader(&tr.depthBlurShader[i]);

	GLSL_DeleteGPUShader(&tr.prefilterEnvMapShader);
	GLSL_DeleteGPUShader(&tr.probeAverageShader);
	GLSL_DeleteGPUShader(&tr.diffuseIrradianceShader);

	for (i = 0; i < 2; ++i)
		GLSL_DeleteGPUShader(&tr.gaussianBlurShader[i]);

	GLSL_DeleteGPUShader(&tr.dglowDownsample);
	GLSL_DeleteGPUShader(&tr.dglowUpsample);
	GLSL_DeleteGPUShader(&tr.bloomPrefilter);

	for (i = 0; i < SSDEF_COUNT; ++i)
		GLSL_DeleteGPUShader(&tr.spriteShader[i]);

	GLSL_DeleteGPUShader(&tr.weatherUpdateShader);
	GLSL_DeleteGPUShader(&tr.weatherUpdateSplashShader);
	GLSL_DeleteGPUShader(&tr.weatherShader);
	GLSL_DeleteGPUShader(&tr.weatherSplashShader);

	GLSL_DeleteGPUShader(&tr.smaaEdgeShader);
	GLSL_DeleteGPUShader(&tr.smaaBlendShader);
	GLSL_DeleteGPUShader(&tr.smaaResolveShader);
	GLSL_DeleteGPUShader(&tr.smaaTemporalResolveShader);

	glState.currentProgram = 0;
	qglUseProgram(0);
}

void GLSL_BindProgram(shaderProgram_t * program)
{
	if(!program)
	{
		GLSL_BindNullProgram();
		return;
	}

	if(r_logFile->integer)
	{
		// don't just call LogComment, or we will get a call to va() every frame!
		GLimp_LogComment(va("--- GL_BindProgram( %s ) ---\n", program->name));
	}

	if(glState.currentProgram != program)
	{
		qglUseProgram(program->program);
		glState.currentProgram = program;
		backEnd.pc.c_glslShaderBinds++;
	}
}


void GLSL_BindNullProgram(void)
{
	if(r_logFile->integer)
	{
		GLimp_LogComment("--- GL_BindNullProgram ---\n");
	}

	if(glState.currentProgram)
	{
		qglUseProgram(0);
		glState.currentProgram = NULL;
	}
}

void GLSL_VertexAttribsState(uint32_t stateBits, VertexArraysProperties *vertexArraysOut)
{
	VertexArraysProperties vertexArraysLocal;
	VertexArraysProperties *vertexArrays = vertexArraysOut;

	if ( !vertexArrays )
	{
		vertexArrays = &vertexArraysLocal;
	}

	if ( tess.useInternalVBO )
	{
		CalculateVertexArraysProperties(stateBits, vertexArrays);
		for ( int i = 0; i < vertexArrays->numVertexArrays; i++ )
		{
			int attributeIndex = vertexArrays->enabledAttributes[i];
			vertexArrays->offsets[attributeIndex] += backEndData->currentFrame->dynamicVboCommitOffset;
		}
	}
	else
	{
		CalculateVertexArraysFromVBO(stateBits, glState.currentVBO, vertexArrays);
	}

	GLSL_VertexAttribPointers(vertexArrays);

}

void GL_VertexArraysToAttribs(
	vertexAttribute_t *attribs,
	size_t attribsCount,
	const VertexArraysProperties *vertexArrays)
{
	assert(attribsCount == ATTR_INDEX_MAX);

	static const struct
	{
		int numComponents;
		GLboolean integerAttribute;
		GLenum type;
		GLboolean normalize;
	} attributes[ATTR_INDEX_MAX] = {
		{ 3, GL_FALSE, GL_FLOAT, GL_FALSE }, // position
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc0
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc1
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc2
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc3
		{ 2, GL_FALSE, GL_FLOAT, GL_FALSE }, // tc4
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // tangent
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // normal
		{ 4, GL_FALSE, GL_FLOAT, GL_FALSE }, // color
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // light direction
		{ 4, GL_TRUE,  GL_UNSIGNED_BYTE, GL_FALSE }, // bone indices
		{ 4, GL_FALSE, GL_UNSIGNED_BYTE, GL_TRUE }, // bone weights
		// pos2 exists in both games (silhouette POM shell data in MP / world)
		{ 3, GL_FALSE, GL_FLOAT, GL_FALSE }, // pos2
#ifdef REND2_SP_MD3
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // tangent2
		{ 4, GL_FALSE, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE }, // normal2
#endif // REND2_SP
	};

	for ( int i = 0; i < vertexArrays->numVertexArrays; i++ )
	{
		int attributeIndex = vertexArrays->enabledAttributes[i];
		vertexAttribute_t& attrib = attribs[i];

		attrib.vbo = glState.currentVBO;
		attrib.index = attributeIndex;
		attrib.numComponents = attributes[attributeIndex].numComponents;
		attrib.integerAttribute = attributes[attributeIndex].integerAttribute;
		attrib.type = attributes[attributeIndex].type;
		attrib.normalize = attributes[attributeIndex].normalize;
		attrib.stride = vertexArrays->strides[attributeIndex];
		attrib.offset = vertexArrays->offsets[attributeIndex];
		attrib.stepRate = vertexArrays->stepRates[attributeIndex];
	}
}

void GLSL_VertexAttribPointers(const VertexArraysProperties *vertexArrays)
{
	// don't just call LogComment, or we will get a call to va() every frame!
	if (r_logFile->integer)
	{
		GLimp_LogComment("--- GL_VertexAttribPointers() ---\n");
	}

	vertexAttribute_t attribs[ATTR_INDEX_MAX] = {};
	GL_VertexArraysToAttribs(attribs, ARRAY_LEN(attribs), vertexArrays);
	GL_VertexAttribPointers(vertexArrays->numVertexArrays, attribs);
}


shaderProgram_t *GLSL_GetGenericShaderProgram(int stage)
{
	shaderStage_t *pStage = tess.xstages[stage];
	int shaderAttribs = 0;

	/*if ( pStage->alphaTestType != ALPHA_TEST_NONE )
		shaderAttribs |= GENERICDEF_USE_ALPHA_TEST;*/

	if (backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1 | RF_DISINTEGRATE2))
		shaderAttribs |= GENERICDEF_USE_RGBAGEN;

	if (backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE2)
		shaderAttribs |= GENERICDEF_USE_DEFORM_VERTEXES;

	switch (pStage->rgbGen)
	{
		case CGEN_LIGHTING_DIFFUSE:
			shaderAttribs |= GENERICDEF_USE_RGBAGEN;
			break;
		default:
			break;
	}

	switch (pStage->alphaGen)
	{
		case AGEN_LIGHTING_SPECULAR:
		case AGEN_PORTAL:
			shaderAttribs |= GENERICDEF_USE_RGBAGEN;
			break;
		default:
			break;
	}

	// froxel height fog: also outside the fog volumes (RB_VolumetricHeightFogSurface)
	if ((tess.fogNum || RB_VolumetricHeightFogSurface(tess.shader->sort)) &&
		pStage->adjustColorsForFog != ACFF_NONE &&
		r_drawfog->integer &&
		!tess.shader->isSky)
		shaderAttribs |= GENERICDEF_USE_FOG;

	if (pStage->bundle[0].tcGen != TCGEN_TEXTURE)
	{
		shaderAttribs |= GENERICDEF_USE_TCGEN_AND_TCMOD;
	}

	if (tess.shader->numDeforms && !ShaderRequiresCPUDeforms(tess.shader))
	{
		shaderAttribs |= GENERICDEF_USE_DEFORM_VERTEXES;
	}
#ifdef REND2_SP_MD3
	if (glState.vertexAnimation)
	{
		shaderAttribs |= GENERICDEF_USE_VERTEX_ANIMATION;
	}
#endif // REND2_SP
	if (glState.skeletalAnimation)
	{
		shaderAttribs |= GENERICDEF_USE_SKELETAL_ANIMATION;
	}

	if (pStage->bundle[0].numTexMods)
	{
		shaderAttribs |= GENERICDEF_USE_TCGEN_AND_TCMOD;
	}

	if (backEnd.currentEntity == &backEnd.entityFlare)
	{
		shaderAttribs |= GENERICDEF_USE_FLARE_TEST;
	}

	/*if (pStage->glow)
	{
		shaderAttribs |= GENERICDEF_USE_GLOW_BUFFER;
	}*/

	return &tr.genericShader[shaderAttribs];
}
