/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#include "ghoul2/g2_local.h"
#include "tr_cache.h"
#include "tr_allocator.h"
#include "tr_weather.h"
#include <algorithm>

#ifdef _G2_GORE
#include "G2_gore_r2.h"
#endif

static size_t STATIC_UNIFORM_BUFFER_SIZE = 1 * 1024 * 1024;
static size_t FRAME_UNIFORM_BUFFER_SIZE = 8*1024*1024;
static size_t FRAME_SCENE_UNIFORM_BUFFER_SIZE = 1 * 1024 * 1024;
static size_t FRAME_VERTEX_BUFFER_SIZE = 12*1024*1024;
static size_t FRAME_INDEX_BUFFER_SIZE = 4*1024*1024;

#if defined(_WIN32)
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

glconfig_t  glConfig;
glconfigExt_t glConfigExt;
glRefConfig_t glRefConfig;
glstate_t	glState;
window_t	window;

cvar_t	*se_language;

cvar_t	*r_verbose;
cvar_t	*r_ignore;

cvar_t	*r_detailTextures;

cvar_t	*r_znear;
cvar_t	*r_zproj;
cvar_t	*r_stereoSeparation;

cvar_t	*r_skipBackEnd;

cvar_t	*r_stereo;
cvar_t	*r_anaglyphMode;

cvar_t	*r_greyscale;

cvar_t	*r_measureOverdraw;

cvar_t	*r_inGameVideo;
cvar_t	*r_fastsky;
cvar_t	*r_drawSun;
cvar_t	*r_dynamiclight;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;
cvar_t	*r_autolodscalevalue;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_drawfog;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;

cvar_t	*r_volumetricFog;
cvar_t *r_entityLightGrid;
cvar_t *r_entityLightGridDebug;
cvar_t	*r_volumetricFogDefaultScale;
cvar_t	*r_volumetricFogSamples;
cvar_t	*r_volumetricFogScale;
cvar_t	*r_volumetricFogQuality;
cvar_t	*r_volumetricFogGridScale;
cvar_t	*r_volumetricFogSlices;
cvar_t	*r_volumetricFogFar;
cvar_t	*r_volumetricFogAnisotropy;
cvar_t	*r_volumetricFogTemporal;
cvar_t	*r_volumetricFogHistoryWeight;
cvar_t	*r_volumetricFogSunScale;
cvar_t	*r_volumetricFogDlightScale;
cvar_t	*r_volumetricFogLightTile;
cvar_t	*r_volumetricFogStaticScale;
cvar_t	*r_volumetricFogStaticDirectional;
cvar_t	*r_volumetricSelfShadow;
cvar_t	*r_volumetricSelfShadowSamples;
cvar_t	*r_volumetricSelfShadowDistance;
cvar_t	*r_volumetricSelfShadowOutsideHeightFog;
cvar_t	*r_volumetricSelfShadowMaxLights;
cvar_t	*r_volumetricMultiScatter;
cvar_t	*r_volumetricFogRGBExtinction;
cvar_t	*r_volumetricMultiScatterOctaves;
cvar_t	*r_volumetricMultiScatterAttenuation;
cvar_t	*r_volumetricMultiScatterContribution;
cvar_t	*r_volumetricMultiScatterPhase;
cvar_t	*r_volumetricMultiScatterLength;
cvar_t	*r_volumetricMultiScatterShadowFill;
cvar_t	*r_volumetricFogDlightShadows;
cvar_t	*r_volumetricFogBloom;
cvar_t	*r_volumetricEmission;
cvar_t	*r_volumetricFogReset;
cvar_t	*r_volumetricFogDebug;
cvar_t	*r_volumetricParticles;
cvar_t	*r_spotLights;
cvar_t	*r_spotLightShadows;
cvar_t	*r_spotLightDebug;
cvar_t	*r_spotLightCookies;
cvar_t	*r_spotLightCookieDebug;
cvar_t	*r_volumetricParticlesMax;
cvar_t	*r_volumetricParticlesScale;
cvar_t	*r_volumetricParticlesHistory;
cvar_t	*r_particleLighting;
cvar_t	*r_particleLightingMix;
cvar_t	*r_particleLightingScale;
cvar_t	*r_particleLightingFloor;
cvar_t	*r_particleLightingDebug;
cvar_t	*r_volumetricParticlesDebug;
cvar_t	*r_volumetricFogFreeze;
cvar_t	*r_volumetricFogHeight;
cvar_t	*r_volumetricFogHeightOpaqueDistance;
cvar_t	*r_volumetricFogHeightBase;
cvar_t	*r_volumetricFogHeightFalloff;
cvar_t	*r_volumetricFogHeightMaxDensity;
cvar_t	*r_volumetricFogHeightTopHeight;
cvar_t	*r_volumetricFogHeightColor;
cvar_t	*r_volumetricFogHeightExtinction;
cvar_t	*r_volumetricFogNoise;
cvar_t	*r_volumetricFogNoiseScale;
cvar_t	*r_volumetricFogNoiseContrast;
cvar_t	*r_volumetricFogNoiseDetailScale;
cvar_t	*r_volumetricFogNoiseDetailContrast;
cvar_t	*r_volumetricFogNoiseWind;

cvar_t	*r_allowExtensions;

cvar_t	*r_ext_compressed_textures;
cvar_t	*r_ext_multitexture;
cvar_t	*r_ext_compiled_vertex_array;
cvar_t	*r_ext_texture_env_add;
cvar_t	*r_ext_texture_filter_anisotropic;
cvar_t	*r_ext_preferred_tc_method;

cvar_t  *r_ext_draw_range_elements;
cvar_t  *r_ext_multi_draw_arrays;
cvar_t  *r_ext_texture_float;
cvar_t  *r_arb_half_float_pixel;
cvar_t  *r_ext_framebuffer_multisample;
cvar_t  *r_arb_seamless_cube_map;
cvar_t  *r_arb_vertex_type_2_10_10_10_rev;
cvar_t	*r_arb_buffer_storage;

cvar_t  *r_mergeMultidraws;
cvar_t  *r_mergeLeafSurfaces;

cvar_t  *r_smaa;
cvar_t  *r_smaa_quality;

cvar_t  *r_cameraExposure;

cvar_t  *r_externalGLSL;

cvar_t  *r_hdr;
cvar_t  *r_floatLightmap;

cvar_t  *r_toneMap;
cvar_t  *r_forceToneMap;
cvar_t  *r_forceToneMapMin;
cvar_t  *r_forceToneMapAvg;
cvar_t  *r_forceToneMapMax;

cvar_t  *r_autoExposure;
cvar_t  *r_forceAutoExposure;
cvar_t  *r_forceAutoExposureMin;
cvar_t  *r_forceAutoExposureMax;

cvar_t  *r_toneMapMode;
cvar_t  *r_toneMapDebug;
cvar_t  *r_exposureCompensation;
cvar_t  *r_linearLighting;
cvar_t  *r_colorGrading;
cvar_t  *r_colorGradingCompare;
cvar_t  *r_colorGradingLUT;
cvar_t  *r_colorGradingIntensity;
cvar_t  *r_autoEmissive;

cvar_t  *r_depthPrepass;
cvar_t  *r_ssao;
cvar_t  *r_aoMode;
cvar_t  *r_aoApply;
cvar_t  *r_aoCompare;
cvar_t  *r_aoMultiBounce;
cvar_t  *r_aoLightmapFraction;
cvar_t  *r_aoSpecOcclusion;
cvar_t  *r_aoDebug;
cvar_t  *r_gtaoQuality;
cvar_t  *r_gtaoHalfRes;
cvar_t  *r_gtaoRadius;
cvar_t  *r_gtaoFalloff;
cvar_t  *r_gtaoThickness;
cvar_t  *r_gtaoPower;
cvar_t  *r_gtaoDenoise;
cvar_t  *r_gtaoBentNormals;
cvar_t  *r_contactShadows;
cvar_t  *r_contactShadowLength;
cvar_t  *r_contactShadowSteps;
cvar_t  *r_contactShadowThickness;
cvar_t  *r_contactShadowStrength;

cvar_t  *r_rainLens;
cvar_t  *r_rainLensDensity;
cvar_t  *r_rainLensRefraction;
cvar_t  *r_rainLensDropSize;
cvar_t  *r_rainLensDebug;
cvar_t  *r_motionBlur;
cvar_t  *r_motionBlurShutterAngle;
cvar_t  *r_motionBlurReferenceFps;
cvar_t  *r_motionBlurShutterScale;
cvar_t  *r_motionBlurMaxPixels;
cvar_t  *r_motionBlurQuality;
cvar_t  *r_motionBlurSamples;
cvar_t  *r_motionBlurViewModelScale;
cvar_t  *r_motionBlurCutDistance;
cvar_t  *r_motionBlurCutAngle;
cvar_t  *r_motionBlurReset;
cvar_t  *r_motionBlurDebug;

cvar_t  *r_ssr;
cvar_t  *r_weatherWetness;
cvar_t  *r_weatherWetnessStrength;
cvar_t  *r_weatherWetnessRoughness;
cvar_t  *r_weatherWetnessDarkening;
cvar_t  *r_weatherWetnessNormal;
cvar_t  *r_weatherWetnessBias;
cvar_t  *r_weatherSurfaceDebug;
cvar_t  *r_weatherWetnessEntityFacing;
cvar_t  *r_weatherPuddles;
cvar_t  *r_weatherPuddleCoverage;
cvar_t  *r_weatherPuddleRoughness;
cvar_t  *r_weatherPuddleSlope;
cvar_t  *r_weatherPuddleScale;
cvar_t  *r_weatherPuddleUseHeightMap;
cvar_t  *r_weatherPuddleHeightSoftness;
cvar_t  *r_weatherPuddleWaterLevelBias;
cvar_t  *r_weatherPuddleRipples;
cvar_t  *r_weatherPuddleRippleStrength;
cvar_t  *r_weatherPuddleRippleScale;
cvar_t  *r_weatherPuddleRippleRate;
cvar_t  *r_weatherRunoff;
cvar_t  *r_weatherRunoffStrength;
cvar_t  *r_weatherRunoffSpeed;
cvar_t  *r_weatherRunoffScale;
cvar_t  *r_weatherRunoffProbe;
cvar_t  *r_weatherRunoffEntities;
cvar_t  *r_ssrQuality;
cvar_t  *r_ssrSteps;
cvar_t  *r_ssrRefineSteps;
cvar_t  *r_ssrMaxDistance;
cvar_t  *r_ssrThickness;
cvar_t  *r_ssrMaxRoughness;
cvar_t  *r_ssrEdgeFade;
cvar_t  *r_ssrHalfRes;
cvar_t  *r_ssrHiZ;
cvar_t  *r_ssrTemporal;
cvar_t  *r_ssrTemporalWeight;
cvar_t  *r_ssrBlendStrength;
cvar_t  *r_ssrCompare;
cvar_t  *r_ssrDebug;
cvar_t  *r_ssrEmitters;
cvar_t  *r_ssrEmitterIntensity;
cvar_t  *r_ssrEmitterMaxRoughness;
cvar_t  *r_ssrHitCache;
cvar_t  *r_ssrReceiverCull;

cvar_t  *r_ssgi;
cvar_t  *r_ssgiSource;
cvar_t  *r_ssgiIntensity;
cvar_t  *r_ssgiQuality;
cvar_t  *r_ssgiRays;
cvar_t  *r_ssgiSteps;
cvar_t  *r_ssgiMaxDistance;
cvar_t  *r_ssgiThickness;
cvar_t  *r_ssgiTemporal;
cvar_t  *r_ssgiHistoryWeight;
cvar_t  *r_ssgiDenoise;
cvar_t  *r_ssgiHalfRes;
cvar_t  *r_ssgiHiZ;
cvar_t  *r_ssgiEmissiveScale;
cvar_t  *r_ssgiGlowScale;
cvar_t  *r_ssgiCompare;
cvar_t  *r_ssgiDebug;
cvar_t  *r_ssgiFreezeHistory;

cvar_t  *r_skinSSS;
cvar_t  *r_skinSSSMixedHeads;
cvar_t  *r_skinSSSStrength;
cvar_t  *r_skinSSSWidth;
cvar_t  *r_skinSSSQuality;
cvar_t  *r_skinSSSWrap;
cvar_t  *r_skinSSSFollowSurface;
cvar_t  *r_skinSSSTransmission;
cvar_t  *r_skinSSSCompare;
cvar_t  *r_skinSSSDebug;

cvar_t  *r_autoPBR;
cvar_t  *r_autoPBRDebug;
cvar_t  *r_autoFoliage;
cvar_t  *r_autoFoliageDebug;
cvar_t  *r_grassCardMode;
cvar_t  *r_grassCardDebug;
cvar_t  *r_grassCardLodDist;
cvar_t  *r_grassCardWidth;
cvar_t  *r_foliageWind;
cvar_t  *r_foliageWindStrength;
cvar_t  *r_foliageWindSpeed;
cvar_t  *r_foliageWindDirection;
cvar_t  *r_foliageWindDebug;
cvar_t  *r_leafFlutter;
cvar_t  *r_leafFlutterStrength;
cvar_t  *r_leafFlutterSpeed;
cvar_t  *r_leafFlutterNormal;
cvar_t  *r_leafFlutterDebug;
cvar_t  *r_foliageInteraction;
cvar_t  *r_foliageInteractionStrength;
cvar_t  *r_foliageInteractionRadius;
cvar_t  *r_foliageInteractionMaxInteractors;
cvar_t  *r_foliageInteractionNPCs;
cvar_t  *r_foliageInteractionDebug;
cvar_t  *r_foliageBendField;
cvar_t  *r_foliageBendFieldSize;
cvar_t  *r_foliageBendFieldExtent;
cvar_t  *r_foliageBendFieldStrength;
cvar_t  *r_foliageBendFieldRecoveryTime;
cvar_t  *r_foliageBendFieldDamping;
cvar_t  *r_foliageBendFieldImpulse;
cvar_t  *r_foliageBendFieldDebug;
cvar_t  *r_plantWind;
cvar_t  *r_plantWindStrength;
cvar_t  *r_autoPBRConvert;
cvar_t  *r_autoPBRRoughness;
cvar_t  *r_diffuseBRDF;
cvar_t  *r_diffuseIBL;
cvar_t  *r_diffuseIBLStrength;
cvar_t  *r_diffuseIBLDebug;

cvar_t  *r_shaderProgramCache;
cvar_t  *r_shaderProgramCacheMaxMB;

cvar_t  *r_forwardPlus;
cvar_t  *r_forwardPlusTileSize;
cvar_t  *r_forwardPlusSlices;
cvar_t  *r_forwardPlusNearSlice;
cvar_t  *r_forwardPlusMaxLightsPerCluster;
cvar_t  *r_forwardPlusDebug;
cvar_t  *r_forwardPlusDebugLight;
cvar_t  *r_forwardPlusMaxShadowLights;
cvar_t  *r_ltcAreaLights;
cvar_t  *r_ltcDebug;
cvar_t  *r_ltcDebugLight;
cvar_t  *r_ltcIntensityScale;
cvar_t  *r_ltcStaticDiffuse;
cvar_t  *r_ltcMaxLights;
cvar_t  *r_ltcAutoAreaLights;
cvar_t  *r_ltcSaberAreaLights;

cvar_t  *r_normalMapping;
cvar_t  *r_specularMapping;
cvar_t  *r_deluxeMapping;
cvar_t  *r_deluxeSpecular;
cvar_t  *r_parallaxMapping;
cvar_t  *r_pomSelfShadow;
cvar_t  *r_pomSelfShadowLightMode;
cvar_t  *r_pomSelfShadowMaxLocalLights;
cvar_t  *r_pomSelfShadowSteps;
cvar_t  *r_pomSelfShadowStrength;
cvar_t  *r_pomSelfShadowBias;
cvar_t  *r_pomSelfShadowSoftness;
cvar_t  *r_pomAdaptiveSteps;
cvar_t  *r_pomMinSteps;
cvar_t  *r_pomMaxSteps;
cvar_t  *r_pomBinarySteps;
cvar_t  *r_pomFadeStart;
cvar_t  *r_pomFadeEnd;
cvar_t  *r_pomDebug;
cvar_t  *r_pomDebugFreezeLight;
cvar_t  *r_pomSilhouette;
cvar_t  *r_pomSilhouetteDistance;
cvar_t  *r_pomSilhouetteFade;
cvar_t  *r_pomSilhouetteSteps;
cvar_t  *r_pomSilhouetteMaxSteps;
cvar_t  *r_pomSilhouetteBinarySteps;
cvar_t  *r_pomSilhouetteViewDependence;
cvar_t  *r_pomSilhouetteShadows;
cvar_t  *r_pomSilhouetteContactShadows;
cvar_t  *r_pomSilhouetteDebug;
cvar_t  *r_autoPOMSilhouetteMode;
cvar_t	*r_forceParallaxBias;
cvar_t  *r_cubeMapping;
cvar_t	*r_cubeMappingBounces;
cvar_t  *r_baseNormalX;
cvar_t  *r_baseNormalY;
cvar_t  *r_baseParallax;
cvar_t  *r_baseSpecular;
cvar_t  *r_dlightMode;
cvar_t  *r_pshadowDist;
cvar_t  *r_imageUpsample;
cvar_t  *r_imageUpsampleMaxSize;
cvar_t  *r_imageUpsampleType;
cvar_t  *r_genNormalMaps;
cvar_t  *r_forceSun;
cvar_t  *r_forceSunMapLightScale;
cvar_t  *r_forceSunLightScale;
cvar_t  *r_forceSunAmbientScale;
cvar_t  *r_sunlightMode;
cvar_t  *r_drawSunRays;
cvar_t  *r_sunShadows;
cvar_t  *r_shadowFilter;
cvar_t  *r_shadowMapSize;
cvar_t  *r_shadowCascadeZNear;
cvar_t  *r_shadowCascadeZFar;
cvar_t  *r_shadowCascadeZBias;
cvar_t  *r_sunShadowMode;
cvar_t  *r_sunShadowAlphaCasters;
cvar_t  *r_shadowCascadeBlend;
cvar_t  *r_shadowDepthBias;
cvar_t  *r_shadowNormalBias;
cvar_t  *r_shadowSlopeBias;
cvar_t  *r_shadowReceiverBiasClamp;
cvar_t  *r_shadowPCSS;
cvar_t  *r_shadowPCSSQuality;
cvar_t  *r_shadowSunAngularDiameter;
cvar_t  *r_shadowPCSSMaxPenumbra;
cvar_t  *r_shadowDebug;
cvar_t  *r_shadowCasterLod;
cvar_t  *r_shadowCasterStats;
cvar_t  *r_dlightShadowBias;
cvar_t  *r_contactShadowSoft;
cvar_t	*r_ignoreDstAlpha;
cvar_t	*r_refractionChromaticAberration;

cvar_t	*r_ignoreGLErrors;
cvar_t	*r_logFile;

cvar_t	*r_texturebits;

cvar_t	*r_drawBuffer;
cvar_t	*r_lightmap;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_shadows;
cvar_t	*r_flares;
cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_roundImagesDown;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_markcount;
cvar_t	*r_textureMode;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_shadowOffsetFactor;
cvar_t	*r_shadowOffsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;



cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;
cvar_t	*r_printShaders;
cvar_t	*r_saveFontData;

#ifdef _DEBUG
cvar_t	*r_noPrecacheGLA;
#endif

cvar_t	*r_noServerGhoul2;
cvar_t	*r_Ghoul2AnimSmooth=0;
cvar_t	*r_Ghoul2UnSqashAfterSmooth=0;
//cvar_t	*r_Ghoul2UnSqash;
//cvar_t	*r_Ghoul2TimeBase=0; from single player
//cvar_t	*r_Ghoul2NoLerp;
//cvar_t	*r_Ghoul2NoBlend;
//cvar_t	*r_Ghoul2BlendMultiplier=0;

cvar_t	*broadsword=0;
cvar_t	*broadsword_kickbones=0;
cvar_t	*broadsword_kickorigin=0;
cvar_t	*broadsword_playflop=0;
cvar_t	*broadsword_dontstopanim=0;
cvar_t	*broadsword_waitforshot=0;
cvar_t	*broadsword_smallbbox=0;
cvar_t	*broadsword_extra1=0;
cvar_t	*broadsword_extra2=0;

cvar_t	*broadsword_effcorr=0;
cvar_t	*broadsword_ragtobase=0;
cvar_t	*broadsword_dircap=0;

cvar_t	*r_marksOnTriangleMeshes;

cvar_t	*r_aviMotionJpegQuality;
cvar_t	*r_screenshotJpegQuality;
cvar_t	*r_surfaceSprites;

// the limits apply to the sum of all scenes in a frame --
// the main view, all the 3D icons, etc
#define	DEFAULT_MAX_POLYS		600
#define	DEFAULT_MAX_POLYVERTS	3000
cvar_t	*r_maxpolys;
cvar_t	*r_maxpolyverts;
int		max_polys;
int		max_polyverts;

cvar_t	*r_dynamicGlow;
cvar_t	*r_dynamicGlowPasses;
cvar_t	*r_dynamicGlowDelta;
cvar_t	*r_dynamicGlowIntensity;
cvar_t	*r_dynamicGlowSoft;
cvar_t	*r_dynamicGlowWidth;
cvar_t	*r_dynamicGlowHeight;
cvar_t	*r_dynamicGlowBloom;
cvar_t	*r_bloom;
cvar_t	*r_bloomIntensity;
cvar_t	*r_bloomThreshold;
cvar_t	*r_bloomKnee;
cvar_t	*r_bloomScatter;
cvar_t	*r_bloomSceneIntensity;

cvar_t *r_debugContext;
cvar_t *r_gl43;
cvar_t *r_debugWeather;
cvar_t *r_weatherCull;
cvar_t *r_weatherDebugChunks;
cvar_t *r_rainStreaks;
cvar_t *r_rainStreakWidth;
cvar_t *r_rainStreakLength;
cvar_t *r_rainStreakOpacity;
cvar_t *r_rainStreakLighting;
cvar_t *r_rainStreakDebug;
cvar_t *r_rainSplashes;
cvar_t *r_rainSplashSize;
cvar_t *r_rainSplashLifetime;
cvar_t *r_rainSplashOpacity;
cvar_t *r_rainSplashDebug;

cvar_t	*r_aspectCorrectFonts;

cvar_t	*r_patchStitching;

extern void	RB_SetGL2D (void);
static void R_Splash()
{
	const GLfloat black[] = { 0.0f, 0.0f, 0.0f, 1.0f };

	GL_SetViewportAndScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	qglClearBufferfv(GL_COLOR, 0, black);
	qglClear(GL_DEPTH_BUFFER_BIT);

	GLSL_InitSplashScreenShader();

	GL_Cull(CT_TWO_SIDED);

	image_t *pImage = R_FindImageFile( "menu/splash", IMGTYPE_COLORALPHA, IMGFLAG_NONE);
	if (pImage )
		GL_Bind( pImage );

	GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(&tr.splashScreenShader);
	RB_InstantTriangle();

	ri.WIN_Present(&window);
}

/*
** GLW_CheckForExtension

  Cannot use strstr directly to differentiate between (for eg) reg_combiners and reg_combiners2
*/
bool GL_CheckForExtension(const char *ext)
{
	const char *ptr = Q_stristr( glConfigExt.originalExtensionString, ext );
	if (ptr == NULL)
		return false;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify it's complete string.
}


void GLW_InitTextureCompression( void )
{
	bool newer_tc, old_tc;

	// Check for available tc methods.
	newer_tc = GL_CheckForExtension("ARB_texture_compression") && GL_CheckForExtension("EXT_texture_compression_s3tc");
	old_tc = GL_CheckForExtension("GL_S3_s3tc");

	if ( old_tc )
	{
		Com_Printf ("...GL_S3_s3tc available\n" );
	}

	if ( newer_tc )
	{
		Com_Printf ("...GL_EXT_texture_compression_s3tc available\n" );
	}

	if ( !r_ext_compressed_textures->value )
	{
		// Compressed textures are off
		glConfig.textureCompression = TC_NONE;
		Com_Printf ("...ignoring texture compression\n" );
	}
	else if ( !old_tc && !newer_tc )
	{
		// Requesting texture compression, but no method found
		glConfig.textureCompression = TC_NONE;
		Com_Printf ("...no supported texture compression method found\n" );
		Com_Printf (".....ignoring texture compression\n" );
	}
	else
	{
		// some form of supported texture compression is avaiable, so see if the user has a preference
		if ( r_ext_preferred_tc_method->integer == TC_NONE )
		{
			// No preference, so pick the best
			if ( newer_tc )
			{
				Com_Printf ("...no tc preference specified\n" );
				Com_Printf (".....using GL_EXT_texture_compression_s3tc\n" );
				glConfig.textureCompression = TC_S3TC_DXT;
			}
			else
			{
				Com_Printf ("...no tc preference specified\n" );
				Com_Printf (".....using GL_S3_s3tc\n" );
				glConfig.textureCompression = TC_S3TC;
			}
		}
		else
		{
			// User has specified a preference, now see if this request can be honored
			if ( old_tc && newer_tc )
			{
				// both are avaiable, so we can use the desired tc method
				if ( r_ext_preferred_tc_method->integer == TC_S3TC )
				{
					Com_Printf ("...using preferred tc method, GL_S3_s3tc\n" );
					glConfig.textureCompression = TC_S3TC;
				}
				else
				{
					Com_Printf ("...using preferred tc method, GL_EXT_texture_compression_s3tc\n" );
					glConfig.textureCompression = TC_S3TC_DXT;
				}
			}
			else
			{
				// Both methods are not available, so this gets trickier
				if ( r_ext_preferred_tc_method->integer == TC_S3TC )
				{
					// Preferring to user older compression
					if ( old_tc )
					{
						Com_Printf ("...using GL_S3_s3tc\n" );
						glConfig.textureCompression = TC_S3TC;
					}
					else
					{
						// Drat, preference can't be honored
						Com_Printf ("...preferred tc method, GL_S3_s3tc not available\n" );
						Com_Printf (".....falling back to GL_EXT_texture_compression_s3tc\n" );
						glConfig.textureCompression = TC_S3TC_DXT;
					}
				}
				else
				{
					// Preferring to user newer compression
					if ( newer_tc )
					{
						Com_Printf ("...using GL_EXT_texture_compression_s3tc\n" );
						glConfig.textureCompression = TC_S3TC_DXT;
					}
					else
					{
						// Drat, preference can't be honored
						Com_Printf ("...preferred tc method, GL_EXT_texture_compression_s3tc not available\n" );
						Com_Printf (".....falling back to GL_S3_s3tc\n" );
						glConfig.textureCompression = TC_S3TC;
					}
				}
			}
		}
	}
}

// Truncates the GL extensions string by only allowing up to 'maxExtensions' extensions in the string.
static const char *TruncateGLExtensionsString (const char *extensionsString, int maxExtensions)
{
	const char *p = extensionsString;
	const char *q;
	int numExtensions = 0;
	size_t extensionsLen = strlen (extensionsString);

	char *truncatedExtensions;

	while ( (q = strchr (p, ' ')) != NULL && numExtensions < maxExtensions )
	{
		p = q + 1;
		numExtensions++;
	}

	if ( q != NULL )
	{
		// We still have more extensions. We'll call this the end

		extensionsLen = p - extensionsString - 1;
	}

	truncatedExtensions = (char *)Z_Malloc(extensionsLen + 1, TAG_GENERAL);
	Q_strncpyz (truncatedExtensions, extensionsString, extensionsLen + 1);

	return truncatedExtensions;
}

static const char *GetGLExtensionsString()
{
	GLint numExtensions;
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
	size_t extensionStringLen = 0;

	for ( int i = 0; i < numExtensions; i++ )
	{
		extensionStringLen += strlen((const char *)qglGetStringi(GL_EXTENSIONS, i)) + 1;
	}

	char *extensionString = (char *)Z_Malloc(extensionStringLen + 1, TAG_GENERAL);
	char *p = extensionString;
	for ( int i = 0; i < numExtensions; i++ )
	{
		const char *extension = (const char *)qglGetStringi(GL_EXTENSIONS, i);
		while ( *extension != '\0' )
			*p++ = *extension++;

		*p++ = ' ';
	}

	*p = '\0';
	assert((size_t)(p - extensionString) == extensionStringLen);

	return extensionString;
}

/*
** InitOpenGL
**
** This function is responsible for initializing a valid OpenGL subsystem.  This
** is done by calling GLimp_Init (which gives us a working OGL subsystem) then
** setting variables, checking GL constants, and reporting the gfx system config
** to the user.
*/
static void InitOpenGL( void )
{
	//
	// initialize OS specific portions of the renderer
	//
	// GLimp_Init directly or indirectly references the following cvars:
	//		- r_fullscreen
	//		- r_mode
	//		- r_(color|depth|stencil)bits
	//		- r_ignorehwgamma
	//		- r_gamma
	//

	if ( glConfig.vidWidth == 0 )
	{
		windowDesc_t windowDesc = {};
		memset(&glConfig, 0, sizeof(glConfig));

		windowDesc.api = GRAPHICS_API_OPENGL;
		windowDesc.gl.majorVersion = r_gl43->integer ? 4 : 3;
		windowDesc.gl.minorVersion = r_gl43->integer ? 3 : 2;
		windowDesc.gl.profile = GLPROFILE_CORE;
		if ( r_debugContext->integer )
			windowDesc.gl.contextFlags = GLCONTEXT_DEBUG;

		GLimp_ConfigureContext(&windowDesc);
		window = ri.WIN_Init(&windowDesc, &glConfig);

		GLimp_InitCoreFunctions();

		Com_Printf( "GL_RENDERER: %s\n", (char *)qglGetString (GL_RENDERER) );

		// get our config strings
		glConfig.vendor_string = (const char *)qglGetString (GL_VENDOR);
		glConfig.renderer_string = (const char *)qglGetString (GL_RENDERER);
		glConfig.version_string = (const char *)qglGetString (GL_VERSION);
		glConfig.extensions_string = GetGLExtensionsString();

		glConfigExt.originalExtensionString = glConfig.extensions_string;
		glConfig.extensions_string = TruncateGLExtensionsString(glConfigExt.originalExtensionString, 128);

		// OpenGL driver constants
		qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );

		// Determine GPU IHV
		if ( Q_stristr( glConfig.vendor_string, "ATI Technologies Inc." ) )
		{
			glRefConfig.hardwareVendor = IHV_AMD;
		}
		else if ( Q_stristr( glConfig.vendor_string, "NVIDIA" ) )
		{
			glRefConfig.hardwareVendor = IHV_NVIDIA;
		}
		else if ( Q_stristr( glConfig.vendor_string, "INTEL") )
		{
			glRefConfig.hardwareVendor = IHV_INTEL;
		}
		else
		{
			glRefConfig.hardwareVendor = IHV_UNKNOWN;
		}

		// stubbed or broken drivers may have reported 0...
		glConfig.maxTextureSize = Q_max(0, glConfig.maxTextureSize);

		// initialize extensions
		GLimp_InitExtensions();
		GLimp_InitModernFunctions();

		// Create the default VAO
		GLuint vao;
		qglGenVertexArrays(1, &vao);
		qglBindVertexArray(vao);
#ifndef __APPLE__
		if (glRefConfig.annotateResources) qglObjectLabel(GL_VERTEX_ARRAY, vao, -1, "GlobalVAO");
#endif
		tr.globalVao = vao;

		// set default state
		GL_SetDefaultState();

		R_Splash();	//get something on screen asap
	}
	else
	{
		// set default state
		GL_SetDefaultState();
	}
}

/*
==================
GL_CheckErrors
==================
*/
void GL_CheckErrs( const char *file, int line ) {
#if defined(_DEBUG)
	GLenum	err;
	char	s[64];

	err = qglGetError();
	if ( err == GL_NO_ERROR ) {
		return;
	}
	if ( r_ignoreGLErrors->integer ) {
		return;
	}
	switch( err ) {
		case GL_INVALID_ENUM:
			strcpy( s, "GL_INVALID_ENUM" );
			break;
		case GL_INVALID_VALUE:
			strcpy( s, "GL_INVALID_VALUE" );
			break;
		case GL_INVALID_OPERATION:
			strcpy( s, "GL_INVALID_OPERATION" );
			break;
		case GL_OUT_OF_MEMORY:
			strcpy( s, "GL_OUT_OF_MEMORY" );
			break;
		default:
			Com_sprintf( s, sizeof(s), "%i", err);
			break;
	}

	ri.Error( ERR_FATAL, "GL_CheckErrors: %s in %s at line %d", s , file, line);
#endif
}

/*
==============================================================================

						SCREEN SHOTS

NOTE TTimo
some thoughts about the screenshots system:
screenshots get written in fs_homepath + fs_gamedir
vanilla q3 .. baseq3/screenshots/ *.tga
team arena .. missionpack/screenshots/ *.tga

two commands: "screenshot" and "screenshotJPEG"
we use statics to store a count and start writing the first screenshot/screenshot????.tga (.jpg) available
(with FS_FileExists / FS_FOpenFileWrite calls)
FIXME: the statics don't get a reinit between fs_game changes

==============================================================================
*/

/*
==================
RB_ReadPixels

Reads an image but takes care of alignment issues for reading RGB images.

Reads a minimum offset for where the RGB data starts in the image from
integer stored at pointer offset. When the function has returned the actual
offset was written back to address offset. This address will always have an
alignment of packAlign to ensure efficient copying.

Stores the length of padding after a line of pixels to address padlen

Return value must be freed with ri.Hunk_FreeTempMemory()
==================
*/

static byte *RB_ReadPixels(
	int x, int y, int width, int height, size_t *offset, int *padlen)
{
	byte *buffer, *bufstart;
	int padwidth, linelen;
	GLint packAlign;

	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	linelen = width * 3;
	padwidth = PAD(linelen, packAlign);

	// Allocate a few more bytes so that we can choose an alignment we like
	buffer = (byte *)ri.Hunk_AllocateTempMemory(padwidth * height + *offset + packAlign - 1);

	bufstart = (byte*)(PADP((intptr_t) buffer + *offset, packAlign));
	qglReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, bufstart);

	*offset = bufstart - buffer;
	*padlen = padwidth - linelen;

	return buffer;
}

static void ConvertRGBtoBGR(
	byte *dst, const byte *src, int stride, int width, int height)
{
	const byte *row = src;
	for (int y = 0; y < height; ++y)
	{
		const byte *pixelRGB = row;
		for (int x = 0; x < width; ++x)
		{
			// swap rgb to bgr
			const byte temp = pixelRGB[0];
			*dst++ = pixelRGB[2];
			*dst++ = pixelRGB[1];
			*dst++ = temp;

			pixelRGB += 3;
		}

		row += stride;
	}
}

static void R_SaveTGA(
	const char *filename,
	const byte *pixels,
	int width,
	int height,
	int stride)
{
	const size_t headerSize = 18;
	const size_t pixelBufferSize = stride * height;
	const size_t bufferSize = headerSize + pixelBufferSize;

	byte *buffer = (byte *)ri.Hunk_AllocateTempMemory(bufferSize);

	// Write TGA header
	Com_Memset(buffer, 0, headerSize);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24; // pixel size

	ConvertRGBtoBGR(buffer + headerSize, pixels, stride, width, height);

	ri.FS_WriteFile(filename, buffer, bufferSize);
	ri.Hunk_FreeTempMemory(buffer);
}

/*
==================
R_SaveScreenshotTGA
==================
*/
static void R_SaveScreenshotTGA(
	const screenshotReadback_t *screenshotReadback, byte *pixels)
{
	R_SaveTGA(
		screenshotReadback->filename,
		pixels,
		screenshotReadback->width,
		screenshotReadback->height,
		screenshotReadback->strideInBytes);
}

/*
==================
R_SaveScreenshotPNG
==================
*/
static void R_SaveScreenshotPNG(
	const screenshotReadback_t *screenshotReadback, byte *pixels)
{
	RE_SavePNG(
		screenshotReadback->filename,
		pixels,
		screenshotReadback->width,
		screenshotReadback->height,
		3);
}

/*
==================
R_SaveScreenshotJPG
==================
*/
static void R_SaveScreenshotJPG(
	const screenshotReadback_t *screenshotReadback, byte *pixels)
{
	RE_SaveJPG(
		screenshotReadback->filename,
		r_screenshotJpegQuality->integer,
		screenshotReadback->width,
		screenshotReadback->height,
		pixels,
		screenshotReadback->strideInBytes - screenshotReadback->rowInBytes);
}

void R_SaveScreenshot(screenshotReadback_t *screenshotReadback)
{
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, screenshotReadback->pbo);

	byte *pixelBuffer = static_cast<byte *>(
		qglMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));

	if (pixelBuffer == nullptr)
	{
		ri.Printf(
			PRINT_ALL,
			S_COLOR_RED "Failed to read screenshot data from GPU\n");
	}
	else
	{
		const int height = screenshotReadback->height;
		const int stride = screenshotReadback->strideInBytes;
		const size_t pixelBufferSize = stride * height;

		byte *pixels = (byte *)ri.Hunk_AllocateTempMemory(pixelBufferSize);
		Com_Memcpy(pixels, pixelBuffer, pixelBufferSize);
		qglUnmapBuffer(GL_PIXEL_PACK_BUFFER);

		if (glConfig.deviceSupportsGamma)
			R_GammaCorrect(pixels, pixelBufferSize);

		switch (screenshotReadback->format)
		{
			case SSF_JPEG:
				R_SaveScreenshotJPG(screenshotReadback, pixels);
				break;

			case SSF_TGA:
				R_SaveScreenshotTGA(screenshotReadback, pixels);
				break;

			case SSF_PNG:
				R_SaveScreenshotPNG(screenshotReadback, pixels);
				break;
		}

		ri.Hunk_FreeTempMemory(pixels);
	}

	qglDeleteBuffers(1, &screenshotReadback->pbo);
	screenshotReadback->pbo = 0;
}

/*
==================
R_TakeScreenshotCmd
==================
*/
const void *RB_TakeScreenshotCmd( const void *data ) {
	const screenshotCommand_t *cmd;

	cmd = (const screenshotCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	const int frameNumber = backEndData->realFrameNumber;
	gpuFrame_t *thisFrame = &backEndData->frames[frameNumber % MAX_FRAMES];
	screenshotReadback_t *screenshot = &thisFrame->screenshotReadback;

	GLint packAlign;
	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	const int linelen = cmd->width * 3;
	const int strideInBytes = PAD(linelen, packAlign);

	qglGenBuffers(1, &screenshot->pbo);
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, screenshot->pbo);
	qglBufferData(
		GL_PIXEL_PACK_BUFFER,
		strideInBytes * cmd->height,
		nullptr,
		GL_STATIC_COPY);
	qglReadPixels(
		cmd->x, cmd->y, cmd->width, cmd->height, GL_RGB, GL_UNSIGNED_BYTE, 0);

	screenshot->strideInBytes = strideInBytes;
	screenshot->rowInBytes = linelen;
	screenshot->width = cmd->width;
	screenshot->height = cmd->height;
	screenshot->format = cmd->format;
	Q_strncpyz(
		screenshot->filename, cmd->fileName, sizeof(screenshot->filename));

	return (const void *)(cmd + 1);
}

/*
==================
R_TakeScreenshot
==================
*/
void R_TakeScreenshot( int x, int y, int width, int height, char *name, screenshotFormat_t format ) {
	static char	fileName[MAX_OSPATH]; // bad things if two screenshots per frame?
	screenshotCommand_t	*cmd;

	cmd = (screenshotCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SCREENSHOT;

	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	Q_strncpyz( fileName, name, sizeof(fileName) );
	cmd->fileName = fileName;
	cmd->format = format;
}

/*
==================
R_ScreenshotFilename
==================
*/
void R_ScreenshotFilename( char *buf, int bufSize, const char *ext ) {
	time_t rawtime;
	char timeStr[32] = {0}; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime

	Com_sprintf( buf, bufSize, "screenshots/shot%s%s", timeStr, ext );
}

/*
====================
R_LevelShot

levelshots are specialized 256*256 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
#define LEVELSHOTSIZE 256
static void R_LevelShot( void ) {
	char		checkname[MAX_OSPATH];
	byte		*buffer;
	byte		*source, *allsource;
	byte		*src, *dst;
	size_t		offset = 0;
	int			padlen;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;

	Com_sprintf( checkname, sizeof(checkname), "levelshots/%s.tga", tr.world->baseName );

	allsource = RB_ReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, &offset, &padlen);
	source = allsource + offset;

	buffer = (byte *)ri.Hunk_AllocateTempMemory(LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18);
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = LEVELSHOTSIZE & 255;
	buffer[13] = LEVELSHOTSIZE >> 8;
	buffer[14] = LEVELSHOTSIZE & 255;
	buffer[15] = LEVELSHOTSIZE >> 8;
	buffer[16] = 24;	// pixel size

	// resample from source
	xScale = glConfig.vidWidth / (4.0*LEVELSHOTSIZE);
	yScale = glConfig.vidHeight / (3.0*LEVELSHOTSIZE);
	for ( y = 0 ; y < LEVELSHOTSIZE ; y++ ) {
		for ( x = 0 ; x < LEVELSHOTSIZE ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + 3 * ( glConfig.vidWidth * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * LEVELSHOTSIZE + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	// gamma correct
	if ( ( tr.overbrightBits > 0 ) && glConfig.deviceSupportsGamma ) {
		R_GammaCorrect( buffer + 18, LEVELSHOTSIZE * LEVELSHOTSIZE * 3 );
	}

	ri.FS_WriteFile( checkname, buffer, LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18 );

	ri.Hunk_FreeTempMemory( buffer );
	ri.Hunk_FreeTempMemory( allsource );

	ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
}

/*
==================
R_ScreenShotTGA_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
==================
*/
void R_ScreenShotTGA_f (void) {
	char checkname[MAX_OSPATH] = {0};
	qboolean silent = qfalse;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
		silent = qtrue;

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	}
	else {
		// timestamp the file
		R_ScreenshotFilename( checkname, sizeof( checkname ), ".tga" );

		if ( ri.FS_FileExists( checkname ) ) {
			Com_Printf( "ScreenShot: Couldn't create a file\n");
			return;
 		}
	}

	R_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, checkname, SSF_TGA );

	if ( !silent )
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
}

void R_ScreenShotPNG_f (void) {
	char checkname[MAX_OSPATH] = {0};
	qboolean silent = qfalse;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
		silent = qtrue;

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s.png", ri.Cmd_Argv( 1 ) );
	}
	else {
		// timestamp the file
		R_ScreenshotFilename( checkname, sizeof( checkname ), ".png" );

		if ( ri.FS_FileExists( checkname ) ) {
			Com_Printf( "ScreenShot: Couldn't create a file\n");
			return;
 		}
	}

	R_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, checkname, SSF_PNG );

	if ( !silent )
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
}

void R_ScreenShotJPEG_f (void) {
	char checkname[MAX_OSPATH] = {0};
	qboolean silent = qfalse;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
		silent = qtrue;

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	}
	else {
		// timestamp the file
		R_ScreenshotFilename( checkname, sizeof( checkname ), ".jpg" );

		if ( ri.FS_FileExists( checkname ) ) {
			Com_Printf( "ScreenShot: Couldn't create a file\n");
			return;
 		}
	}

	R_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, checkname, SSF_JPEG );

	if ( !silent )
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
}

//============================================================================

/*
==================
RB_TakeVideoFrameCmd
==================
*/
const void *RB_TakeVideoFrameCmd( const void *data )
{
	const videoFrameCommand_t	*cmd;
	byte				*cBuf;
	size_t				memcount, linelen;
	int				padwidth, avipadwidth, padlen, avipadlen;
	GLint packAlign;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	cmd = (const videoFrameCommand_t *)data;

	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	linelen = cmd->width * 3;

	// Alignment stuff for glReadPixels
	padwidth = PAD(linelen, packAlign);
	padlen = padwidth - linelen;
	// AVI line padding
	avipadwidth = PAD(linelen, AVI_LINE_PADDING);
	avipadlen = avipadwidth - linelen;

	cBuf = (byte*)(PADP(cmd->captureBuffer, packAlign));

	qglReadPixels(0, 0, cmd->width, cmd->height, GL_RGB,
		GL_UNSIGNED_BYTE, cBuf);

	memcount = padwidth * cmd->height;

	// gamma correct
	if(glConfig.deviceSupportsGamma)
		R_GammaCorrect(cBuf, memcount);

	if(cmd->motionJpeg)
	{
		memcount = RE_SaveJPGToBuffer(cmd->encodeBuffer, linelen * cmd->height,
			r_aviMotionJpegQuality->integer,
			cmd->width, cmd->height, cBuf, padlen);
		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, memcount);
	}
	else
	{
		byte *lineend, *memend;
		byte *srcptr, *destptr;

		srcptr = cBuf;
		destptr = cmd->encodeBuffer;
		memend = srcptr + memcount;

		// swap R and B and remove line paddings
		while(srcptr < memend)
		{
			lineend = srcptr + linelen;
			while(srcptr < lineend)
			{
				*destptr++ = srcptr[2];
				*destptr++ = srcptr[1];
				*destptr++ = srcptr[0];
				srcptr += 3;
			}

			Com_Memset(destptr, '\0', avipadlen);
			destptr += avipadlen;

			srcptr += padlen;
		}

		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, avipadwidth * cmd->height);
	}

	return (const void *)(cmd + 1);
}

//============================================================================

/*
** GL_SetDefaultState
*/
void GL_SetDefaultState( void )
{
	qglClearDepth( 1.0f );

	qglCullFace(GL_FRONT);

	// initialize downstream texture unit if we're running
	// in a multitexture environment
	GL_SelectTexture( 1 );
	GL_TextureMode( r_textureMode->string );
	GL_SelectTexture( 0 );

	GL_TextureMode( r_textureMode->string );

	//qglShadeModel( GL_SMOOTH );
	qglDepthFunc( GL_LEQUAL );

	Com_Memset(&glState, 0, sizeof(glState));

	//
	// make sure our GL state vector is set correctly
	//
	glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;
	glState.maxDepth = 1.0f;
	qglDepthRange(0.0f, 1.0f);

	qglUseProgram(0);

	qglBindBuffer(GL_ARRAY_BUFFER, 0);
	qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	qglDepthMask( GL_TRUE );
	qglDisable( GL_DEPTH_TEST );
	qglEnable( GL_SCISSOR_TEST );
	qglEnable(GL_PROGRAM_POINT_SIZE);
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
	glState.blend = false;

	qglEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	qglStencilFunc(GL_ALWAYS, 0, 0xff);
	qglStencilOpSeparate(GL_FRONT, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
	qglStencilOpSeparate(GL_BACK, GL_KEEP, GL_DECR_WRAP, GL_KEEP);

	// set default vertex color
	qglVertexAttrib4f(ATTR_INDEX_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);

	// invalidate all vertex step rates
	// this ensures that attributes that have a set divisor will have a 
	// correct divisor set after GL_SetDefaultState is called
	for (int i = 0; i < ATTR_INDEX_MAX; i++)
	{
		glState.currentVaoAttribs[i].stepRate = -1;
	}
}

/*
================
R_PrintLongString

Workaround for ri.Printf's 1024 characters buffer limit.
================
*/
void R_PrintLongString(const char *string) {
	char buffer[1024];
	const char *p;
	int size = strlen(string);

	p = string;
	while(size > 0)
	{
		Q_strncpyz(buffer, p, sizeof (buffer) );
		ri.Printf( PRINT_ALL, "%s", buffer );
		p += 1023;
		size -= 1023;
	}
}

/*
================
GfxInfo_f
================
*/
static void GfxInfo_f( void )
{
	const char *enablestrings[] =
	{
		"disabled",
		"enabled"
	};
	const char *fsstrings[] =
	{
		"windowed",
		"fullscreen"
	};
	const char *noborderstrings[] =
	{
		"",
		"noborder "
	};

	int fullscreen = ri.Cvar_VariableIntegerValue("r_fullscreen");
	int noborder = ri.Cvar_VariableIntegerValue("r_noborder");

	ri.Printf( PRINT_ALL, "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "GL_VERSION: %s\n", glConfig.version_string );
	R_PrintModernCapabilities();
	ri.Printf( PRINT_ALL, "GL_EXTENSIONS: " );
	R_PrintLongString( glConfigExt.originalExtensionString );
	ri.Printf( PRINT_ALL, "\n" );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	ri.Printf( PRINT_ALL, "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
	ri.Printf( PRINT_ALL, "MODE: %d, %d x %d %s%s hz:",
				ri.Cvar_VariableIntegerValue("r_mode"),
				glConfig.vidWidth, glConfig.vidHeight,
				fullscreen == 0 ? noborderstrings[noborder == 1] : noborderstrings[0],
				fsstrings[fullscreen == 1] );
	if ( glConfig.displayFrequency )
	{
		ri.Printf( PRINT_ALL, "%d\n", glConfig.displayFrequency );
	}
	else
	{
		ri.Printf( PRINT_ALL, "N/A\n" );
	}
	if ( glConfig.deviceSupportsGamma )
	{
		ri.Printf( PRINT_ALL, "GAMMA: hardware w/ %d overbright bits\n", tr.overbrightBits );
	}
	else
	{
		ri.Printf( PRINT_ALL, "GAMMA: software w/ %d overbright bits\n", tr.overbrightBits );
	}

	ri.Printf( PRINT_ALL, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_ALL, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_ALL, "texture bits: %d\n", r_texturebits->integer );

	if ( r_vertexLight->integer )
	{
		ri.Printf( PRINT_ALL, "HACK: using vertex lightmap approximation\n" );
	}
	int displayRefresh = ri.Cvar_VariableIntegerValue("r_displayRefresh");
	if ( displayRefresh ) {
		ri.Printf( PRINT_ALL, "Display refresh set to %d\n", displayRefresh );
	}

	if ( r_finish->integer ) {
		ri.Printf( PRINT_ALL, "Forcing glFinish\n" );
	}

	ri.Printf( PRINT_ALL, "Dynamic Glow: %s\n", enablestrings[r_dynamicGlow->integer != 0] );
}

/*
================
GfxMemInfo_f
================
*/
void GfxMemInfo_f( void )
{
	switch (glRefConfig.memInfo)
	{
		case MI_NONE:
		{
			ri.Printf(PRINT_ALL, "No extension found for GPU memory info.\n");
		}
		break;
		case MI_NVX:
		{
			int value;

			qglGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &value);
			ri.Printf(PRINT_ALL, "GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX: %ikb\n", value);

			qglGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &value);
			ri.Printf(PRINT_ALL, "GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX: %ikb\n", value);

			qglGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &value);
			ri.Printf(PRINT_ALL, "GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX: %ikb\n", value);

			qglGetIntegerv(GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX, &value);
			ri.Printf(PRINT_ALL, "GPU_MEMORY_INFO_EVICTION_COUNT_NVX: %i\n", value);

			qglGetIntegerv(GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX, &value);
			ri.Printf(PRINT_ALL, "GPU_MEMORY_INFO_EVICTED_MEMORY_NVX: %ikb\n", value);
		}
		break;
		case MI_ATI:
		{
			// GL_ATI_meminfo
			int value[4];

			qglGetIntegerv(GL_VBO_FREE_MEMORY_ATI, &value[0]);
			ri.Printf(PRINT_ALL, "VBO_FREE_MEMORY_ATI: %ikb total %ikb largest aux: %ikb total %ikb largest\n", value[0], value[1], value[2], value[3]);

			qglGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, &value[0]);
			ri.Printf(PRINT_ALL, "TEXTURE_FREE_MEMORY_ATI: %ikb total %ikb largest aux: %ikb total %ikb largest\n", value[0], value[1], value[2], value[3]);

			qglGetIntegerv(GL_RENDERBUFFER_FREE_MEMORY_ATI, &value[0]);
			ri.Printf(PRINT_ALL, "RENDERBUFFER_FREE_MEMORY_ATI: %ikb total %ikb largest aux: %ikb total %ikb largest\n", value[0], value[1], value[2], value[3]);
		}
		break;
	}
}

static void R_CaptureFrameData_f()
{
	int argc = ri.Cmd_Argc();
	if ( argc <= 1 )
	{
		ri.Printf( PRINT_ALL, "Usage: %s <multi|single>\n", ri.Cmd_Argv(0));
		return;
	}


	const char *cmd = ri.Cmd_Argv(1);
	if ( Q_stricmp(cmd, "single") == 0 )
		tr.numFramesToCapture = 1;
	else if ( Q_stricmp(cmd, "multi") == 0 )
		tr.numFramesToCapture = atoi(ri.Cmd_Argv(1));

	int len = ri.FS_FOpenFileByMode("rend2.log", &tr.debugFile, FS_APPEND);
	if ( len == -1 || !tr.debugFile )
	{
		ri.Printf( PRINT_ERROR, "Failed to open rend2 log file\n" );
		tr.numFramesToCapture = 0;
	}
}

typedef struct consoleCommand_s {
	const char	*cmd;
	xcommand_t	func;
} consoleCommand_t;

static consoleCommand_t	commands[] = {
	{ "imagelist",			R_ImageList_f },
	{ "shaderlist",			R_ShaderList_f },
	{ "r_pbrDumpMaterials",	R_PBRDumpMaterials_f },
	{ "r_skinSSSKernel",	R_SkinSSSKernel_f },
	{ "r_skinSSSList",	R_SkinSSSList_f },
	{ "r_weatherMaterialList", R_WeatherMaterialList_f },
	{ "r_autoFoliageList", R_PrintAutoFoliage_f },
	{ "r_foliageInteractionList", R_FoliageInteractionList_f },
	{ "r_foliageBendFieldClear", R_FoliageFieldClear_f },
	{ "r_forwardPlusStats",	R_ForwardPlusStats_f },
	{ "r_pomSilhouetteInfo",	R_PomSilhouetteInfo_f },
	{ "r_autoPOMSilhouette",	R_AutoPomSilhouette_f },
	{ "r_forwardPlusSpawnTestLights",	R_SpawnTestLights_f },
	{ "r_forwardPlusBenchmark",	R_ForwardPlusBenchmark_f },
	{ "r_ltcReloadLights",	R_ReloadAreaLights_f },
	{ "r_ltcList",			R_AreaLightsList_f },
	{ "r_ltcNearest",		R_AreaLightsNearest_f },
	{ "r_ltcExtractLights",	R_ExtractAreaLights_f },
	{ "skinlist",			R_SkinList_f },
	{ "fontlist",			R_FontList_f },
	{ "screenshot",			R_ScreenShotJPEG_f },
	{ "screenshot_png",		R_ScreenShotPNG_f },
	{ "screenshot_tga",		R_ScreenShotTGA_f },
	{ "gfxinfo",			GfxInfo_f },
	{ "gfxmeminfo",			GfxMemInfo_f },
	{ "r_we",				R_WorldEffect_f },
	{ "r_vfog",				R_VolumetricFog_f },
	{ "r_vfogLightStats",	R_VolumetricLightStats_f },
	{ "r_fogvol",			R_FogVolume_f },
	{ "r_volparticles",		R_VolParticles_f },
	{ "r_spot",				R_Spot_f },
	//{ "imagecacheinfo",		RE_RegisterImages_Info_f },
	{ "modellist",			R_Modellist_f },
	//{ "modelcacheinfo",		RE_RegisterModels_Info_f },
	{ "vbolist",			R_VBOList_f },
	{ "capframes",			R_CaptureFrameData_f },
};

static const size_t numCommands = ARRAY_LEN( commands );


/*
===============
R_Register
===============
*/
void R_Register( void )
{
	//
	// latched and archived variables
	//
	r_allowExtensions = ri.Cvar_Get( "r_allowExtensions", "1", CVAR_ARCHIVE | CVAR_LATCH, "Allow GL extensions" );
	r_ext_compressed_textures = ri.Cvar_Get( "r_ext_compress_textures", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable texture compression" );
	r_ext_multitexture = ri.Cvar_Get( "r_ext_multitexture", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_compiled_vertex_array = ri.Cvar_Get( "r_ext_compiled_vertex_array", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_texture_env_add = ri.Cvar_Get( "r_ext_texture_env_add", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_preferred_tc_method = ri.Cvar_Get( "r_ext_preferred_tc_method", "0", CVAR_ARCHIVE | CVAR_LATCH, "Preferred texture compression method" );

	r_ext_draw_range_elements = ri.Cvar_Get( "r_ext_draw_range_elements", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_multi_draw_arrays = ri.Cvar_Get( "r_ext_multi_draw_arrays", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_texture_float = ri.Cvar_Get( "r_ext_texture_float", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable floating-point textures" );
	r_arb_half_float_pixel = ri.Cvar_Get( "r_arb_half_float_pixel", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable ARB_half_float GL extension" );
	r_ext_framebuffer_multisample = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable framebuffer MSAA" );
	// We do MSAA resolving manually in rend2, so don't bother with the default framebuffer
	ri.Cvar_Set("r_ext_multisample_default_fb", "0");
	r_arb_seamless_cube_map = ri.Cvar_Get( "r_arb_seamless_cube_map", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable seamless cube map filtering GL extension" );
	r_arb_vertex_type_2_10_10_10_rev = ri.Cvar_Get( "r_arb_vertex_type_2_10_10_10_rev", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable 1010102 UI data type" );
	r_arb_buffer_storage = ri.Cvar_Get( "r_arb_buffer_storage", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable buffer storage GL extension" );
	r_ext_texture_filter_anisotropic = ri.Cvar_Get( "r_ext_texture_filter_anisotropic", "16", CVAR_ARCHIVE, "Disable/enable anisotropic texture filtering" );

	r_smaa = ri.Cvar_Get("r_smaa", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable SMAA");
	r_smaa_quality = ri.Cvar_Get("r_smaa_quality", "2", CVAR_ARCHIVE | CVAR_LATCH, "0: LOW | 1: MEDIUM | 2: HIGH | 3: ULTRA");

	r_dynamicGlow						= ri.Cvar_Get( "r_dynamicGlow",				"0",		CVAR_ARCHIVE, "" );
	r_dynamicGlowPasses					= ri.Cvar_Get( "r_dynamicGlowPasses",		"5",		CVAR_ARCHIVE, "" );
	r_dynamicGlowDelta					= ri.Cvar_Get( "r_dynamicGlowDelta",		"0.8f",		CVAR_ARCHIVE, "" );
	r_dynamicGlowIntensity				= ri.Cvar_Get( "r_dynamicGlowIntensity",	"1.13f",	CVAR_ARCHIVE, "" );
	r_dynamicGlowSoft					= ri.Cvar_Get( "r_dynamicGlowSoft",			"1",		CVAR_ARCHIVE, "" );
	r_dynamicGlowWidth					= ri.Cvar_Get( "r_dynamicGlowWidth",		"320",		CVAR_ARCHIVE|CVAR_LATCH, "" );
	r_dynamicGlowHeight					= ri.Cvar_Get( "r_dynamicGlowHeight",		"240",		CVAR_ARCHIVE|CVAR_LATCH, "" );
	r_dynamicGlowBloom					= ri.Cvar_Get( "r_dynamicGlowBloom",		"0.0",		CVAR_ARCHIVE, "");
	ri.Cvar_CheckRange(r_dynamicGlowBloom, 0.f, 2.f, qfalse);
	r_bloom = ri.Cvar_Get("r_bloom", "-1", CVAR_ARCHIVE, "-1: legacy glow, 0: off, 1: HDR bloom");
	ri.Cvar_CheckRange(r_bloom, -1, 1, qtrue);
	r_bloomIntensity = ri.Cvar_Get("r_bloomIntensity", "0.15", CVAR_ARCHIVE, "HDR bloom strength");
	ri.Cvar_CheckRange(r_bloomIntensity, 0.f, 4.f, qfalse);
	r_bloomThreshold = ri.Cvar_Get("r_bloomThreshold", "2.0", CVAR_ARCHIVE, "Scene-linear bloom threshold");
	ri.Cvar_CheckRange(r_bloomThreshold, 0.f, 64.f, qfalse);
	r_bloomKnee = ri.Cvar_Get("r_bloomKnee", "0.5", CVAR_ARCHIVE, "Bloom threshold soft knee fraction");
	ri.Cvar_CheckRange(r_bloomKnee, 0.f, 1.f, qfalse);
	r_bloomScatter = ri.Cvar_Get("r_bloomScatter", "0.7", CVAR_ARCHIVE, "Bloom pyramid upscale scatter");
	ri.Cvar_CheckRange(r_bloomScatter, 0.f, 1.f, qfalse);
	r_bloomSceneIntensity = ri.Cvar_Get("r_bloomSceneIntensity", "0", CVAR_ARCHIVE, "Optional HDR scene bloom, independent of emissive bloom");
	ri.Cvar_CheckRange(r_bloomSceneIntensity, 0.f, 2.f, qfalse);

	r_gl43 = ri.Cvar_Get( "r_gl43", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Prefer GL 4.3 and allow modern paths; 0 forces legacy paths after vid_restart" );
	r_debugContext						= ri.Cvar_Get( "r_debugContext",			"0",		CVAR_LATCH, "" );
	r_debugWeather						= ri.Cvar_Get( "r_debugWeather",			"0",		CVAR_ARCHIVE, "" );
	r_weatherCull = ri.Cvar_Get("r_weatherCull", "1", CVAR_ARCHIVE,
		"Cull weather chunks outside the camera frustum (0 draws all nine)");
	r_weatherDebugChunks = ri.Cvar_Get("r_weatherDebugChunks", "0", 0,
		"Draw weather chunk bounds and print mapping, visibility and particle counts (2 prints AABBs)");
	r_rainStreaks = ri.Cvar_Get("r_rainStreaks", "0", CVAR_ARCHIVE,
		"Rain streak rendering: 0 legacy additive, 1 lit premultiplied streaks sized by velocity with contact / near / sub-pixel fades");
	r_rainStreakWidth = ri.Cvar_Get("r_rainStreakWidth", "1", CVAR_ARCHIVE,
		"r_rainStreaks: streak width scale");
	r_rainStreakLength = ri.Cvar_Get("r_rainStreakLength", "1", CVAR_ARCHIVE,
		"r_rainStreaks: streak length scale (length also follows fall speed and wind)");
	r_rainStreakOpacity = ri.Cvar_Get("r_rainStreakOpacity", "1", CVAR_ARCHIVE,
		"r_rainStreaks: rain opacity scale");
	r_rainStreakLighting = ri.Cvar_Get("r_rainStreakLighting", "1", CVAR_ARCHIVE,
		"r_rainStreaks: 0 fixed tint (legacy brightness), 1 lit by the light grid and the sun");
	r_rainStreakDebug = ri.Cvar_Get("r_rainStreakDebug", "0", CVAR_CHEAT,
		"r_rainStreaks debug: 1 coverage, 2 distance fade (red near, blue far), 3 lighting factor, 4 weather depth contact, 5 particle variation");
	ri.Cvar_CheckRange(r_rainStreakWidth, 0.25f, 4.0f, qfalse);
	ri.Cvar_CheckRange(r_rainStreakLength, 0.25f, 4.0f, qfalse);
	ri.Cvar_CheckRange(r_rainStreakOpacity, 0.0f, 4.0f, qfalse);
	ri.Cvar_CheckRange(r_rainStreakLighting, 0.0f, 1.0f, qfalse);
	ri.Cvar_CheckRange(r_rainStreakDebug, 0, 5, qtrue);
	r_rainSplashes = ri.Cvar_Get("r_rainSplashes", "0", CVAR_ARCHIVE,
		"Rain impact splashes: drops crossing the rain occlusion map on the GPU make short-lived splashes (0 off)");
	r_rainSplashSize = ri.Cvar_Get("r_rainSplashSize", "6", CVAR_ARCHIVE,
		"r_rainSplashes: splash radius in world units");
	r_rainSplashLifetime = ri.Cvar_Get("r_rainSplashLifetime", "350", CVAR_ARCHIVE,
		"r_rainSplashes: splash lifetime in ms");
	r_rainSplashOpacity = ri.Cvar_Get("r_rainSplashOpacity", "0.5", CVAR_ARCHIVE,
		"r_rainSplashes: splash opacity");
	r_rainSplashDebug = ri.Cvar_Get("r_rainSplashDebug", "0", CVAR_CHEAT,
		"r_rainSplashes debug: 1 impact points (yellow new, red old, through walls), 2 crossing test (green above, red under, cyan accepted, magenta rejected), 3 trajectories; prints the live splash count");
	ri.Cvar_CheckRange(r_rainSplashSize, 1.0f, 64.0f, qfalse);
	ri.Cvar_CheckRange(r_rainSplashLifetime, 50.0f, 2000.0f, qfalse);
	ri.Cvar_CheckRange(r_rainSplashOpacity, 0.0f, 4.0f, qfalse);
	ri.Cvar_CheckRange(r_rainSplashDebug, 0, 3, qtrue);

	r_picmip = ri.Cvar_Get ("r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	ri.Cvar_CheckRange( r_picmip, 0, 16, qtrue );
	r_roundImagesDown = ri.Cvar_Get ("r_roundImagesDown", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_colorMipLevels = ri.Cvar_Get ("r_colorMipLevels", "0", CVAR_LATCH, "" );
	r_detailTextures = ri.Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_texturebits = ri.Cvar_Get( "r_texturebits", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_overBrightBits = ri.Cvar_Get ("r_overBrightBits", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_simpleMipMaps = ri.Cvar_Get( "r_simpleMipMaps", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_vertexLight = ri.Cvar_Get( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", 0, "");
	r_subdivisions = ri.Cvar_Get ("r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH, "");
	ri.Cvar_CheckRange( r_subdivisions, 4, 80, qfalse );
	r_stereo = ri.Cvar_Get( "r_stereo", "0", CVAR_ARCHIVE | CVAR_LATCH, "");
	r_greyscale = ri.Cvar_Get("r_greyscale", "0", CVAR_ARCHIVE | CVAR_LATCH, "");
	ri.Cvar_CheckRange(r_greyscale, 0, 1, qfalse);

	r_externalGLSL = ri.Cvar_Get( "r_externalGLSL", "0", CVAR_LATCH, "" );

	r_hdr = ri.Cvar_Get( "r_hdr", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable rendering in HDR" );
	r_floatLightmap = ri.Cvar_Get( "r_floatLightmap", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable HDR lightmap support" );

	r_toneMap = ri.Cvar_Get( "r_toneMap", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable tonemapping" );
	r_forceToneMap = ri.Cvar_Get( "r_forceToneMap", "0", CVAR_CHEAT, "" );
	r_forceToneMapMin = ri.Cvar_Get( "r_forceToneMapMin", "-8.0", CVAR_CHEAT, "" );
	r_forceToneMapAvg = ri.Cvar_Get( "r_forceToneMapAvg", "-1.0", CVAR_CHEAT, "" );
	r_forceToneMapMax = ri.Cvar_Get( "r_forceToneMapMax", "0.0", CVAR_CHEAT, "" );

	r_autoExposure = ri.Cvar_Get( "r_autoExposure", "1", CVAR_ARCHIVE, "Disable/enable auto exposure" );
	r_forceAutoExposure = ri.Cvar_Get( "r_forceAutoExposure", "0", CVAR_CHEAT, "" );
	r_forceAutoExposureMin = ri.Cvar_Get( "r_forceAutoExposureMin", "-3.0", CVAR_CHEAT, "" );
	r_forceAutoExposureMax = ri.Cvar_Get( "r_forceAutoExposureMax", "1.0", CVAR_CHEAT, "" );

	r_cameraExposure = ri.Cvar_Get( "r_cameraExposure", "0", CVAR_CHEAT, "" );

	r_toneMapMode = ri.Cvar_Get( "r_toneMapMode", "0", CVAR_ARCHIVE, "Tone mapping operator: 0 = legacy Rend2 filmic, 1 = ACES (fitted approximation), 2 = AgX-like (approximation)" );
	ri.Cvar_CheckRange( r_toneMapMode, 0, 2, qtrue );
	r_toneMapDebug = ri.Cvar_Get( "r_toneMapDebug", "0", CVAR_CHEAT, "Tone mapping debug view: 1 = legacy | r_toneMapMode split, 2 = legacy | ACES | AgX split, 3 = raw scene-linear, 4 = exposure-adjusted, 5 = exposure false color" );
	ri.Cvar_CheckRange( r_toneMapDebug, 0, 5, qtrue );
	r_exposureCompensation = ri.Cvar_Get( "r_exposureCompensation", "0", CVAR_ARCHIVE, "Exposure compensation in EV stops, applied in linear light before tone mapping" );
	ri.Cvar_CheckRange( r_exposureCompensation, -4.0f, 4.0f, qfalse );
	r_linearLighting = ri.Cvar_Get( "r_linearLighting", "0", CVAR_ARCHIVE | CVAR_LATCH, "Light maps without HDR lightmaps in linear light (experimental, needs r_hdr 1 and r_toneMap 1)" );
	r_colorGrading = ri.Cvar_Get( "r_colorGrading", "1", CVAR_ARCHIVE, "Color grading with a 3D LUT: 0 = off, 1 = on" );
	ri.Cvar_CheckRange( r_colorGrading, 0, 1, qtrue );
	r_colorGradingCompare = ri.Cvar_Get( "r_colorGradingCompare", "0", CVAR_ARCHIVE, "Color grading comparison: 0 = off, 1 = original on the left, graded on the right (needs r_colorGrading 1)" );
	ri.Cvar_CheckRange( r_colorGradingCompare, 0, 1, qtrue );
	r_colorGradingLUT = ri.Cvar_Get( "r_colorGradingLUT", "", CVAR_ARCHIVE, "Color grading LUT (.cube, or *identity). Overrides the LUT of the map (maps/<map>.cube), empty uses it" );
	r_colorGradingIntensity = ri.Cvar_Get( "r_colorGradingIntensity", "1", CVAR_ARCHIVE, "Color grading strength: 0 = none, 1 = full LUT" );
	ri.Cvar_CheckRange( r_colorGradingIntensity, 0.0f, 1.0f, qfalse );
	r_autoEmissive = ri.Cvar_Get( "r_autoEmissive", "0", CVAR_ARCHIVE, "Automatically use legacy glow and unlit additive stages as emissive sources" );
	ri.Cvar_CheckRange( r_autoEmissive, 0, 1, qtrue );

	r_depthPrepass = ri.Cvar_Get( "r_depthPrepass", "1", CVAR_ARCHIVE, "" );
	r_ssao = ri.Cvar_Get( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE, "" );
	r_aoMode = ri.Cvar_Get( "r_aoMode", "-1", CVAR_ARCHIVE, "Screen-space ambient occlusion: -1 = follow r_ssao (legacy), 0 = off, 1 = legacy SSAO, 2 = GTAO. Needs r_depthPrepass; turning it on from off needs a vid_restart" );
	ri.Cvar_CheckRange( r_aoMode, -1, 2, qtrue );
	r_aoApply = ri.Cvar_Get( "r_aoApply", "-1", CVAR_ARCHIVE, "How lighting uses AO: -1 = auto (legacy for SSAO, indirect-only for GTAO), 0 = legacy (ambient term and reflections), 1 = indirect-only (ambient, indirect share of baked light, specular occlusion)" );
	ri.Cvar_CheckRange( r_aoApply, -1, 1, qtrue );
	r_aoCompare = ri.Cvar_Get( "r_aoCompare", "0", CVAR_CHEAT, "Split screen AO comparison: legacy SSAO and its application on the left, GTAO on the right" );
	ri.Cvar_CheckRange( r_aoCompare, 0, 1, qtrue );
	r_aoMultiBounce = ri.Cvar_Get( "r_aoMultiBounce", "1", CVAR_ARCHIVE, "Indirect-only AO: multi-bounce approximation, bright surfaces keep more light in creases" );
	ri.Cvar_CheckRange( r_aoMultiBounce, 0, 1, qtrue );
	r_aoLightmapFraction = ri.Cvar_Get( "r_aoLightmapFraction", "0.5", CVAR_ARCHIVE, "Indirect-only AO: share of baked lightmap / vertex lighting treated as indirect light that AO attenuates" );
	ri.Cvar_CheckRange( r_aoLightmapFraction, 0.0f, 1.0f, qfalse );
	r_aoSpecOcclusion = ri.Cvar_Get( "r_aoSpecOcclusion", "-1", CVAR_ARCHIVE, "Indirect-only AO: specular occlusion of environment reflections: -1 = auto (2 with GTAO bent normals, else 1), 0 = scalar AO, 1 = from AO, roughness and N.V (Lagarde), 2 = visibility cone around the bent normal against the specular lobe cone" );
	ri.Cvar_CheckRange( r_aoSpecOcclusion, -1, 2, qtrue );
	r_aoDebug = ri.Cvar_Get( "r_aoDebug", "0", CVAR_CHEAT, "AO debug view: 1 = legacy SSAO, 2 = raw GTAO, 3 = denoised GTAO, 4 = reconstructed normals, 5 = linear depth, 6 = contact shadows, 7 = cascade shadows, 8 = sun visibility (cascade * contact * POM self shadow), 9 = diffuse ambient visibility, 10 = AO map used by lighting, 11 = bent normals, 12 = specular occlusion" );
	ri.Cvar_CheckRange( r_aoDebug, 0, 12, qtrue );
	r_gtaoQuality = ri.Cvar_Get( "r_gtaoQuality", "2", CVAR_ARCHIVE, "GTAO quality: 0 = low (1 slice, 3 steps), 1 = medium (2, 4), 2 = high (3, 6), 3 = ultra (6, 8)" );
	ri.Cvar_CheckRange( r_gtaoQuality, 0, 3, qtrue );
	r_gtaoHalfRes = ri.Cvar_Get( "r_gtaoHalfRes", "1", CVAR_ARCHIVE | CVAR_LATCH, "GTAO resolution: 1 = half resolution (depth-aware upsampling), 0 = full resolution" );
	ri.Cvar_CheckRange( r_gtaoHalfRes, 0, 1, qtrue );
	r_gtaoRadius = ri.Cvar_Get( "r_gtaoRadius", "32", CVAR_ARCHIVE, "GTAO effect radius in world units" );
	ri.Cvar_CheckRange( r_gtaoRadius, 1.0f, 512.0f, qfalse );
	r_gtaoFalloff = ri.Cvar_Get( "r_gtaoFalloff", "0.6", CVAR_ARCHIVE, "GTAO distance falloff, as a fraction of r_gtaoRadius" );
	ri.Cvar_CheckRange( r_gtaoFalloff, 0.05f, 1.0f, qfalse );
	r_gtaoThickness = ri.Cvar_Get( "r_gtaoThickness", "0.2", CVAR_ARCHIVE, "GTAO thin occluder compensation: 0 = occluders are solid, higher = objects in front of a surface occlude it less (fewer halos)" );
	ri.Cvar_CheckRange( r_gtaoThickness, 0.0f, 4.0f, qfalse );
	r_gtaoPower = ri.Cvar_Get( "r_gtaoPower", "1.5", CVAR_ARCHIVE, "GTAO strength, exponent applied to the visibility" );
	ri.Cvar_CheckRange( r_gtaoPower, 0.1f, 8.0f, qfalse );
	r_gtaoDenoise = ri.Cvar_Get( "r_gtaoDenoise", "2", CVAR_ARCHIVE, "GTAO edge-aware spatial denoise passes (0-3)" );
	ri.Cvar_CheckRange( r_gtaoDenoise, 0, 3, qtrue );
	r_gtaoBentNormals = ri.Cvar_Get( "r_gtaoBentNormals", "1", CVAR_ARCHIVE, "GTAO bent normals for indirect light only (diffuse irradiance direction, specular occlusion cone): 0 = off, up to 1 = strength" );
	ri.Cvar_CheckRange( r_gtaoBentNormals, 0.0f, 1.0f, qfalse );
	r_contactShadows = ri.Cvar_Get( "r_contactShadows", "0", CVAR_ARCHIVE, "Screen-space contact shadows for the sun (needs r_sunlightMode and r_depthPrepass; turning it on from off needs a vid_restart)" );
	ri.Cvar_CheckRange( r_contactShadows, 0, 1, qtrue );
	r_contactShadowLength = ri.Cvar_Get( "r_contactShadowLength", "16", CVAR_ARCHIVE, "Contact shadow ray length in world units" );
	ri.Cvar_CheckRange( r_contactShadowLength, 1.0f, 128.0f, qfalse );
	r_contactShadowSteps = ri.Cvar_Get( "r_contactShadowSteps", "12", CVAR_ARCHIVE, "Contact shadow ray march steps" );
	ri.Cvar_CheckRange( r_contactShadowSteps, 2, 32, qtrue );
	r_contactShadowThickness = ri.Cvar_Get( "r_contactShadowThickness", "6", CVAR_ARCHIVE, "Assumed occluder thickness for contact shadows in world units" );
	ri.Cvar_CheckRange( r_contactShadowThickness, 0.1f, 64.0f, qfalse );
	r_contactShadowStrength = ri.Cvar_Get( "r_contactShadowStrength", "0.85", CVAR_ARCHIVE, "Contact shadow strength" );
	ri.Cvar_CheckRange( r_contactShadowStrength, 0.0f, 1.0f, qfalse );

	r_rainLens = ri.Cvar_Get( "r_rainLens", "0", CVAR_ARCHIVE | CVAR_LATCH, "Rain droplets on the camera lens while it rains and the camera is outside (needs r_hdr)" );
	ri.Cvar_CheckRange( r_rainLens, 0, 1, qtrue );
	r_rainLensDensity = ri.Cvar_Get( "r_rainLensDensity", "0.5", CVAR_ARCHIVE, "Lens rain droplet density, 0..1" );
	r_rainLensRefraction = ri.Cvar_Get( "r_rainLensRefraction", "1.0", CVAR_ARCHIVE, "Lens rain droplet refraction strength" );
	r_rainLensDropSize = ri.Cvar_Get( "r_rainLensDropSize", "1.0", CVAR_ARCHIVE, "Lens rain droplet size, relative to the screen height" );
	r_rainLensDebug = ri.Cvar_Get( "r_rainLensDebug", "0", CVAR_CHEAT, "Lens rain debug view (forces the effect on): 1 = droplet mask, 2 = normal, 3 = UV offset, 4 = scene / composition split" );
	ri.Cvar_CheckRange( r_rainLensDebug, 0, 4, qtrue );

	r_motionBlur = ri.Cvar_Get( "r_motionBlur", "0", CVAR_ARCHIVE | CVAR_LATCH, "Velocity based camera and object motion blur (needs r_hdr)" );
	ri.Cvar_CheckRange( r_motionBlur, 0, 1, qtrue );
	r_motionBlurShutterAngle = ri.Cvar_Get( "r_motionBlurShutterAngle", "180", CVAR_ARCHIVE, "Motion blur shutter angle in degrees, relative to the r_motionBlurReferenceFps frame interval" );
	ri.Cvar_CheckRange( r_motionBlurShutterAngle, 0.0f, 360.0f, qfalse );
	r_motionBlurReferenceFps = ri.Cvar_Get( "r_motionBlurReferenceFps", "60", CVAR_ARCHIVE, "Frame rate the shutter angle refers to (frame rate independent exposure time), 0 = the actual frame interval" );
	ri.Cvar_CheckRange( r_motionBlurReferenceFps, 0.0f, 1000.0f, qfalse );
	r_motionBlurShutterScale = ri.Cvar_Get( "r_motionBlurShutterScale", "1", 0, "Motion blur exposure time multiplier for gameplay effects (e.g. Force Speed), set by game code" );
	ri.Cvar_CheckRange( r_motionBlurShutterScale, 0.0f, 4.0f, qfalse );
	r_motionBlurMaxPixels = ri.Cvar_Get( "r_motionBlurMaxPixels", "32", CVAR_ARCHIVE, "Maximum motion blur length in pixels at 1080p (scaled with the resolution)" );
	ri.Cvar_CheckRange( r_motionBlurMaxPixels, 1.0f, 128.0f, qfalse );
	r_motionBlurQuality = ri.Cvar_Get( "r_motionBlurQuality", "1", CVAR_ARCHIVE, "Motion blur quality: 0 = low, 1 = medium, 2 = high" );
	ri.Cvar_CheckRange( r_motionBlurQuality, 0, 2, qtrue );
	r_motionBlurSamples = ri.Cvar_Get( "r_motionBlurSamples", "0", CVAR_ARCHIVE, "Maximum motion blur samples per pixel, 0 = from r_motionBlurQuality" );
	ri.Cvar_CheckRange( r_motionBlurSamples, 0, 32, qtrue );
	r_motionBlurViewModelScale = ri.Cvar_Get( "r_motionBlurViewModelScale", "0.5", CVAR_ARCHIVE, "Motion blur strength on the first person view model" );
	ri.Cvar_CheckRange( r_motionBlurViewModelScale, 0.0f, 1.0f, qfalse );
	r_motionBlurCutDistance = ri.Cvar_Get( "r_motionBlurCutDistance", "256", CVAR_ARCHIVE, "Camera movement in one frame treated as a cut or teleport (resets the motion history)" );
	ri.Cvar_CheckRange( r_motionBlurCutDistance, 16.0f, 16384.0f, qfalse );
	r_motionBlurCutAngle = ri.Cvar_Get( "r_motionBlurCutAngle", "75", CVAR_ARCHIVE, "Camera rotation in degrees in one frame treated as a cut (resets the motion history)" );
	ri.Cvar_CheckRange( r_motionBlurCutAngle, 1.0f, 180.0f, qfalse );
	r_motionBlurReset = ri.Cvar_Get( "r_motionBlurReset", "0", 0, "Set to 1 by game code to reset the motion history (camera cut), cleared by the renderer" );
	r_motionBlurDebug = ri.Cvar_Get( "r_motionBlurDebug", "0", CVAR_CHEAT, "Motion blur debug view: 1 = velocity, 2 = camera velocity, 3 = object velocity, 4 = sample count, 5 = blur contribution" );
	ri.Cvar_CheckRange( r_motionBlurDebug, 0, 5, qtrue );

	r_ssr = ri.Cvar_Get( "r_ssr", "0", CVAR_ARCHIVE | CVAR_LATCH, "Screen-space reflections, blended with the cubemap reflections of PBR materials (needs r_specularMapping)" );
	ri.Cvar_CheckRange( r_ssr, 0, 1, qtrue );
	r_weatherWetness = ri.Cvar_Get( "r_weatherWetness", "0", CVAR_ARCHIVE | CVAR_LATCH, "Rain exposed surfaces of rain maps are wet (PBR roughness / albedo), uses the weather occlusion map" );
	ri.Cvar_CheckRange( r_weatherWetness, 0, 1, qtrue );
	r_weatherWetnessStrength = ri.Cvar_Get( "r_weatherWetnessStrength", "1", CVAR_ARCHIVE, "Wetness of fully rain exposed surfaces, 0-1" );
	r_weatherWetnessRoughness = ri.Cvar_Get( "r_weatherWetnessRoughness", "1", CVAR_ARCHIVE, "Scale of the per material wet smoothing (1 = material table, 0 = none)" );
	r_weatherWetnessDarkening = ri.Cvar_Get( "r_weatherWetnessDarkening", "1", CVAR_ARCHIVE, "Scale of the per material wet albedo darkening (1 = material table)" );
	r_weatherWetnessNormal = ri.Cvar_Get( "r_weatherWetnessNormal", "1", CVAR_ARCHIVE, "Scale of the per material wet normal flattening (1 = material table)" );
	r_weatherWetnessEntityFacing = ri.Cvar_Get( "r_weatherWetnessEntityFacing", "0.85", CVAR_ARCHIVE, "Wetness of vertical faces of characters and props (world walls: 0.5)" );
	r_weatherWetnessBias = ri.Cvar_Get( "r_weatherWetnessBias", "2", CVAR_ARCHIVE, "Rain occlusion depth bias in world units" );
	r_weatherSurfaceDebug = ri.Cvar_Get( "r_weatherSurfaceDebug", "0", CVAR_CHEAT, "1 rain exposure, 2 wetness mask (magenta: excluded), 3 effective roughness, 4 dry / wet split, 5 puddle slope, 6 puddle noise, 7 exposure x slope, 8 puddle mask, 9 puddle roughness, 10 puddle eligibility, 11 material height (magenta: none), 12 height low / high, 13 macro puddle mask, 14 micro depression mask, 15 combined puddle, 16 wet material class, 17 ripple height, 18 ripple normal offset, 19 ripple masking, 20 final normal, 21 geometric normal, 22 runoff slope class, 23 projected gravity, 24 runoff mask, 25 runoff flow field, 26 runoff roughness, 27 material weather on / off, 28 weatherResponse wetness, 29 puddle, 30 runoff scale, 31 exclusion reason" );
	r_weatherPuddles = ri.Cvar_Get( "r_weatherPuddles", "0", CVAR_ARCHIVE, "Procedural puddles on flat rain exposed world surfaces (needs r_weatherWetness)" );
	r_weatherPuddleCoverage = ri.Cvar_Get( "r_weatherPuddleCoverage", "0.35", CVAR_ARCHIVE, "Fraction of flat exposed area covered by puddles, 0-1" );
	r_weatherPuddleRoughness = ri.Cvar_Get( "r_weatherPuddleRoughness", "0.04", CVAR_ARCHIVE, "Roughness of the puddle water surface" );
	r_weatherPuddleSlope = ri.Cvar_Get( "r_weatherPuddleSlope", "0.90 0.98", CVAR_ARCHIVE, "Geometric normal z range where puddles fade in: min max" );
	r_weatherPuddleScale = ri.Cvar_Get( "r_weatherPuddleScale", "192", CVAR_ARCHIVE, "World size of the puddle noise pattern" );
	r_weatherPuddleUseHeightMap = ri.Cvar_Get( "r_weatherPuddleUseHeightMap", "1", CVAR_ARCHIVE, "Puddles follow the height map of parallax (normalHeightMap) materials: water fills cracks and low areas first" );
	r_weatherPuddleHeightSoftness = ri.Cvar_Get( "r_weatherPuddleHeightSoftness", "0.08", CVAR_ARCHIVE, "Water line transition width of height aware puddles, in 0..1 material height" );
	r_weatherPuddleWaterLevelBias = ri.Cvar_Get( "r_weatherPuddleWaterLevelBias", "0", CVAR_ARCHIVE, "Static water level bias of height aware puddles: > 0 floods more of the relief, < 0 only the deepest cracks" );
	r_weatherPuddleRipples = ri.Cvar_Get( "r_weatherPuddleRipples", "1", CVAR_ARCHIVE, "Procedural rain ripples on puddles (normal only, needs r_weatherPuddles and rain)" );
	r_weatherPuddleRippleStrength = ri.Cvar_Get( "r_weatherPuddleRippleStrength", "0.35", CVAR_ARCHIVE, "Peak slope of a puddle ripple ring (0..2)" );
	r_weatherPuddleRippleScale = ri.Cvar_Get( "r_weatherPuddleRippleScale", "20", CVAR_ARCHIVE, "World size of a ripple cell, a ring grows to 0.3 x this radius" );
	r_weatherPuddleRippleRate = ri.Cvar_Get( "r_weatherPuddleRippleRate", "1.1", CVAR_ARCHIVE, "Ripple rings per second per cell" );
	r_weatherRunoff = ri.Cvar_Get( "r_weatherRunoff", "0", CVAR_ARCHIVE, "Gravity driven water film / streaks on rain exposed slopes and walls (needs r_weatherWetness)" );
	r_weatherRunoffStrength = ri.Cvar_Get( "r_weatherRunoffStrength", "1", CVAR_ARCHIVE, "Runoff film strength" );
	r_weatherRunoffSpeed = ri.Cvar_Get( "r_weatherRunoffSpeed", "24", CVAR_ARCHIVE, "Runoff flow speed, world units per second" );
	r_weatherRunoffScale = ri.Cvar_Get( "r_weatherRunoffScale", "48", CVAR_ARCHIVE, "Runoff streak cell size, world units" );
	r_weatherRunoffProbe = ri.Cvar_Get( "r_weatherRunoffProbe", "1.5", CVAR_ARCHIVE, "Wall rain exposure probe distance in front of steep faces, weather map texels (0 off, max 32 units)" );
	r_weatherRunoffEntities = ri.Cvar_Get( "r_weatherRunoffEntities", "0", CVAR_ARCHIVE, "Runoff also on characters, props and movers (pattern glued to the entity)" );
	r_ssrQuality = ri.Cvar_Get( "r_ssrQuality", "1", CVAR_ARCHIVE, "SSR quality: 0 = low, 1 = medium, 2 = high, 3 = ultra" );
	ri.Cvar_CheckRange( r_ssrQuality, 0, 3, qtrue );
	r_ssrSteps = ri.Cvar_Get( "r_ssrSteps", "0", CVAR_ARCHIVE, "SSR ray march steps, 0 = from r_ssrQuality" );
	ri.Cvar_CheckRange( r_ssrSteps, 0, 256, qtrue );
	r_ssrRefineSteps = ri.Cvar_Get( "r_ssrRefineSteps", "0", CVAR_ARCHIVE, "SSR binary search steps after a ray crossing, 0 = from r_ssrQuality" );
	ri.Cvar_CheckRange( r_ssrRefineSteps, 0, 16, qtrue );
	r_ssrMaxDistance = ri.Cvar_Get( "r_ssrMaxDistance", "1024", CVAR_ARCHIVE, "SSR maximum ray length in world units (shortened on rough surfaces)" );
	ri.Cvar_CheckRange( r_ssrMaxDistance, 16.0f, 16384.0f, qfalse );
	r_ssrThickness = ri.Cvar_Get( "r_ssrThickness", "8", CVAR_ARCHIVE, "SSR assumed thickness of the depth buffer surfaces in world units (grows with the distance)" );
	ri.Cvar_CheckRange( r_ssrThickness, 0.5f, 256.0f, qfalse );
	r_ssrMaxRoughness = ri.Cvar_Get( "r_ssrMaxRoughness", "0.6", CVAR_ARCHIVE, "Rougher surfaces only use the cubemap reflection (SSR fades out towards this roughness)" );
	ri.Cvar_CheckRange( r_ssrMaxRoughness, 0.05f, 1.0f, qfalse );
	r_ssrEdgeFade = ri.Cvar_Get( "r_ssrEdgeFade", "0.1", CVAR_ARCHIVE, "SSR fade out band at the screen edges, fraction of the view size" );
	ri.Cvar_CheckRange( r_ssrEdgeFade, 0.0f, 0.5f, qfalse );
	r_ssrHalfRes = ri.Cvar_Get( "r_ssrHalfRes", "-1", CVAR_ARCHIVE, "SSR rays at half resolution: -1 = from r_ssrQuality, 0 = full, 1 = half" );
	ri.Cvar_CheckRange( r_ssrHalfRes, -1, 1, qtrue );
	r_ssrHiZ = ri.Cvar_Get( "r_ssrHiZ", "-1", CVAR_ARCHIVE, "SSR hierarchical depth tracing: -1 = from r_ssrQuality, 0 = off, 1 = on" );
	ri.Cvar_CheckRange( r_ssrHiZ, -1, 1, qtrue );
	r_ssrTemporal = ri.Cvar_Get( "r_ssrTemporal", "0", CVAR_ARCHIVE | CVAR_LATCH, "SSR temporal accumulation (reprojected history, uses the velocity buffer)" );
	ri.Cvar_CheckRange( r_ssrTemporal, 0, 1, qtrue );
	r_ssrTemporalWeight = ri.Cvar_Get( "r_ssrTemporalWeight", "0.9", CVAR_ARCHIVE, "SSR history weight of the temporal accumulation" );
	ri.Cvar_CheckRange( r_ssrTemporalWeight, 0.0f, 0.98f, qfalse );
	r_ssrBlendStrength = ri.Cvar_Get( "r_ssrBlendStrength", "1", CVAR_ARCHIVE, "SSR confidence scale: 0 = cubemap reflections only, 1 = full SSR where it is reliable" );
	ri.Cvar_CheckRange( r_ssrBlendStrength, 0.0f, 1.0f, qfalse );
	r_ssrCompare = ri.Cvar_Get( "r_ssrCompare", "0", CVAR_ARCHIVE, "SSR split screen: left half cubemap reflections only, right half with SSR" );
	ri.Cvar_CheckRange( r_ssrCompare, 0, 1, qtrue );
	r_ssrDebug = ri.Cvar_Get( "r_ssrDebug", "0", CVAR_CHEAT, "SSR debug view: 1 = material normal, 2 = roughness, 3 = specular reflectance, 4 = ray hit/miss, 5 = hit distance, 6 = confidence, 7 = raw SSR, 8 = cubemap reflection, 9 = hybrid reflection, 10 = replaced part (SSR - cubemap), 11 = light saber / effect reflections, 12 = hit cache (green = reused, red = traced)" );
	ri.Cvar_CheckRange( r_ssrDebug, 0, 12, qtrue );
	r_ssrEmitters = ri.Cvar_Get( "r_ssrEmitters", "1", CVAR_ARCHIVE, "SSR: reflect light sabers, blaster bolts and other additive effect primitives (analytic, also off screen)" );
	ri.Cvar_CheckRange( r_ssrEmitters, 0, 1, qtrue );
	r_ssrEmitterIntensity = ri.Cvar_Get( "r_ssrEmitterIntensity", "1", CVAR_ARCHIVE, "SSR: brightness of the light saber / effect reflections" );
	ri.Cvar_CheckRange( r_ssrEmitterIntensity, 0.0f, 16.0f, qfalse );
	r_ssrEmitterMaxRoughness = ri.Cvar_Get( "r_ssrEmitterMaxRoughness", "0.35", CVAR_ARCHIVE, "SSR: rougher surfaces do not reflect light sabers / effects (their dynamic light highlight already does)" );
	ri.Cvar_CheckRange( r_ssrEmitterMaxRoughness, 0.05f, 1.0f, qfalse );
	r_ssrHitCache = ri.Cvar_Get( "r_ssrHitCache", "1", CVAR_ARCHIVE, "SSR: reuse the ray hits of the previous frame while they are still valid (each pixel is traced again every 4 frames)" );
	ri.Cvar_CheckRange( r_ssrHitCache, 0, 1, qtrue );
	r_ssrReceiverCull = ri.Cvar_Get( "r_ssrReceiverCull", "1", CVAR_ARCHIVE, "SSR: run the ray march only on the pixels that need a ray (early depth test)" );
	ri.Cvar_CheckRange( r_ssrReceiverCull, 0, 1, qtrue );

	r_ssgi = ri.Cvar_Get( "r_ssgi", "0", CVAR_ARCHIVE | CVAR_LATCH, "Screen-space diffuse GI: bounces dynamic light (sabers, blasters, explosions) and emission, not the baked lighting (needs vid_restart)" );
	ri.Cvar_CheckRange( r_ssgi, 0, 1, qtrue );
	r_ssgiSource = ri.Cvar_Get( "r_ssgiSource", "0", CVAR_ARCHIVE, "SSGI source: 0 = dynamic lights + emissive (recommended), 1 = emissive only, 2 = full scene (EXPERIMENTAL: bounces baked lighting twice)" );
	ri.Cvar_CheckRange( r_ssgiSource, 0, 2, qtrue );
	r_ssgiIntensity = ri.Cvar_Get( "r_ssgiIntensity", "1", CVAR_ARCHIVE, "SSGI indirect light scale (1 = physically based diffuse bounce, 0 = off)" );
	ri.Cvar_CheckRange( r_ssgiIntensity, 0.0f, 8.0f, qfalse );
	r_ssgiQuality = ri.Cvar_Get( "r_ssgiQuality", "1", CVAR_ARCHIVE, "SSGI quality: 0 = low, 1 = medium, 2 = high, 3 = ultra (rays, steps, resolution, denoise; not the intensity)" );
	ri.Cvar_CheckRange( r_ssgiQuality, 0, 3, qtrue );
	r_ssgiRays = ri.Cvar_Get( "r_ssgiRays", "0", CVAR_ARCHIVE, "SSGI rays per traced pixel and frame, 0 = from r_ssgiQuality" );
	ri.Cvar_CheckRange( r_ssgiRays, 0, 8, qtrue );
	r_ssgiSteps = ri.Cvar_Get( "r_ssgiSteps", "0", CVAR_ARCHIVE, "SSGI ray march steps (Hi-Z: iterations / 3), 0 = from r_ssgiQuality" );
	ri.Cvar_CheckRange( r_ssgiSteps, 0, 256, qtrue );
	r_ssgiMaxDistance = ri.Cvar_Get( "r_ssgiMaxDistance", "256", CVAR_ARCHIVE, "SSGI maximum ray length in world units" );
	ri.Cvar_CheckRange( r_ssgiMaxDistance, 16.0f, 4096.0f, qfalse );
	r_ssgiThickness = ri.Cvar_Get( "r_ssgiThickness", "12", CVAR_ARCHIVE, "SSGI assumed thickness of the depth buffer surfaces in world units (grows with the distance)" );
	ri.Cvar_CheckRange( r_ssgiThickness, 0.5f, 256.0f, qfalse );
	r_ssgiTemporal = ri.Cvar_Get( "r_ssgiTemporal", "1", CVAR_ARCHIVE, "SSGI temporal accumulation (reprojected, validated and clamped history)" );
	ri.Cvar_CheckRange( r_ssgiTemporal, 0, 1, qtrue );
	r_ssgiHistoryWeight = ri.Cvar_Get( "r_ssgiHistoryWeight", "0.9", CVAR_ARCHIVE, "SSGI maximum history weight of the temporal accumulation" );
	ri.Cvar_CheckRange( r_ssgiHistoryWeight, 0.0f, 0.98f, qfalse );
	r_ssgiDenoise = ri.Cvar_Get( "r_ssgiDenoise", "-1", CVAR_ARCHIVE, "SSGI edge-aware denoise passes (0-4), -1 = from r_ssgiQuality" );
	ri.Cvar_CheckRange( r_ssgiDenoise, -1, 4, qtrue );
	r_ssgiHalfRes = ri.Cvar_Get( "r_ssgiHalfRes", "-1", CVAR_ARCHIVE, "SSGI rays at half resolution: -1 = from r_ssgiQuality, 0 = full, 1 = half (applied at vid_restart)" );
	ri.Cvar_CheckRange( r_ssgiHalfRes, -1, 1, qtrue );
	r_ssgiHiZ = ri.Cvar_Get( "r_ssgiHiZ", "-1", CVAR_ARCHIVE, "SSGI hierarchical depth tracing: -1 = from r_ssgiQuality, 0 = off, 1 = on" );
	ri.Cvar_CheckRange( r_ssgiHiZ, -1, 1, qtrue );
	r_ssgiEmissiveScale = ri.Cvar_Get( "r_ssgiEmissiveScale", "1", CVAR_ARCHIVE, "SSGI scale of explicit emissive materials (emissiveMap / emissiveColor / emissiveScale) as a light source" );
	ri.Cvar_CheckRange( r_ssgiEmissiveScale, 0.0f, 16.0f, qfalse );
	r_ssgiGlowScale = ri.Cvar_Get( "r_ssgiGlowScale", "0", CVAR_ARCHIVE, "SSGI scale of legacy glow / r_autoEmissive stages as a light source (no physical intensity, 0 = off)" );
	ri.Cvar_CheckRange( r_ssgiGlowScale, 0.0f, 4.0f, qfalse );
	r_ssgiCompare = ri.Cvar_Get( "r_ssgiCompare", "0", CVAR_ARCHIVE, "SSGI split screen: left half without, right half with screen-space GI" );
	ri.Cvar_CheckRange( r_ssgiCompare, 0, 1, qtrue );
	r_ssgiDebug = ri.Cvar_Get( "r_ssgiDebug", "0", CVAR_CHEAT, "SSGI debug view: 1 = ray hit mask, 2 = hit distance, 3 = raw one frame GI, 4 = temporal GI, 5 = history weight, 6 = denoised GI, 7 = dynamic light source, 8 = emissive source, 9 = final indirect light, 10 = receiver albedo" );
	ri.Cvar_CheckRange( r_ssgiDebug, 0, 10, qtrue );
	r_ssgiFreezeHistory = ri.Cvar_Get( "r_ssgiFreezeHistory", "0", CVAR_CHEAT, "SSGI debug: stop updating the temporal history" );
	ri.Cvar_CheckRange( r_ssgiFreezeHistory, 0, 1, qtrue );

	r_skinSSS = ri.Cvar_Get( "r_skinSSS", "0", CVAR_ARCHIVE | CVAR_LATCH, "Skin scattering of surfaces classified as skin: 0 = off, 1 = cheap wrapped diffuse (an approximation, not SSS), 2 = screen-space diffusion of the skin diffuse light (needs r_hdr, vid_restart)" );
	ri.Cvar_CheckRange( r_skinSSS, 0, 2, qtrue );
	r_skinSSSMixedHeads = ri.Cvar_Get( "r_skinSSSMixedHeads", "0", CVAR_ARCHIVE | CVAR_LATCH, "Skin scattering also on *_head textures, which mix hair, ears and neck in one texture (vid_restart)" );
	ri.Cvar_CheckRange( r_skinSSSMixedHeads, 0, 1, qtrue );
	r_skinSSSStrength = ri.Cvar_Get( "r_skinSSSStrength", "1", CVAR_ARCHIVE, "Skin scattering: share of the skin diffuse light replaced by its diffused copy (r_skinSSS 2)" );
	ri.Cvar_CheckRange( r_skinSSSStrength, 0.0f, 1.0f, qfalse );
	r_skinSSSWidth = ri.Cvar_Get( "r_skinSSSWidth", "8", CVAR_ARCHIVE, "Skin scattering: diffusion radius in millimeters, 8 = the physical skin profile, larger values widen the whole profile (1 map unit = 28 mm), r_skinSSS 2" );
	ri.Cvar_CheckRange( r_skinSSSWidth, 0.0f, 40.0f, qfalse );
	r_skinSSSQuality = ri.Cvar_Get( "r_skinSSSQuality", "1", CVAR_ARCHIVE, "Skin scattering: diffusion taps per pass, 0 = 11, 1 = 17, 2 = 25" );
	ri.Cvar_CheckRange( r_skinSSSQuality, 0, 2, qtrue );
	r_skinSSSWrap = ri.Cvar_Get( "r_skinSSSWrap", "0.3", CVAR_ARCHIVE, "Skin scattering, r_skinSSS 1: red wrap width of the wrapped diffuse (green 0.45x, blue 0.25x)" );
	ri.Cvar_CheckRange( r_skinSSSWrap, 0.0f, 1.0f, qfalse );
	r_skinSSSFollowSurface = ri.Cvar_Get( "r_skinSSSFollowSurface", "1", CVAR_ARCHIVE, "Skin scattering: how strongly depth and normal differences stop the diffusion (0 = plain profile blur inside the skin mask)" );
	ri.Cvar_CheckRange( r_skinSSSFollowSurface, 0.0f, 4.0f, qfalse );
	r_skinSSSTransmission = ri.Cvar_Get( "r_skinSSSTransmission", "0", CVAR_ARCHIVE, "Skin scattering: optional cheap back light transmission (ears, fingers) of the directed and dynamic lights, 0 = off" );
	ri.Cvar_CheckRange( r_skinSSSTransmission, 0.0f, 2.0f, qfalse );
	r_skinSSSCompare = ri.Cvar_Get( "r_skinSSSCompare", "0", CVAR_ARCHIVE, "Skin scattering split screen: left half without, right half with" );
	ri.Cvar_CheckRange( r_skinSSSCompare, 0, 1, qtrue );
	r_skinSSSDebug = ri.Cvar_Get( "r_skinSSSDebug", "0", CVAR_CHEAT, "Skin scattering debug view: 1 = classification, 2 = skin mask, 3 = raw skin diffuse, 4 = horizontal blur, 5 = vertical blur, 6 = final delta, 7 = all but the skin diffuse, 8 = kernel radius (pixels)" );
	ri.Cvar_CheckRange( r_skinSSSDebug, 0, 8, qtrue );
	r_autoPBR = ri.Cvar_Get( "r_autoPBR", "0", CVAR_ARCHIVE, "PBR parameters of legacy materials without authored specular / packed maps: 0 = current rend2 fallback, 1 = generic dielectric, 2 = heuristic material classes" );
	ri.Cvar_CheckRange( r_autoPBR, 0, 2, qtrue );
	r_autoPBRDebug = ri.Cvar_Get( "r_autoPBRDebug", "0", CVAR_CHEAT, "Auto PBR debug view: 1 = material class, 2 = parameter source (authored / auto), 3 = roughness (generated variation in grey, blue = not auto PBR)" );
	ri.Cvar_CheckRange( r_autoPBRDebug, 0, 3, qtrue );
	r_autoFoliage = ri.Cvar_Get( "r_autoFoliage", "0", CVAR_ARCHIVE,
		"MD3 foliage semantics: 0 = off, 1 = conservative stock-safe, 2 = broader experimental" );
	ri.Cvar_CheckRange( r_autoFoliage, 0, 2, qtrue );
	r_autoFoliageDebug = ri.Cvar_Get( "r_autoFoliageDebug", "0", CVAR_CHEAT,
		"Auto foliage colors: 1 = classified surfaces, 2 = also uncertain candidates" );
	ri.Cvar_CheckRange( r_autoFoliageDebug, 0, 2, qtrue );
	r_grassCardMode = ri.Cvar_Get( "r_grassCardMode", "0", CVAR_ARCHIVE,
		"Vegetation surface sprites as world stable cards: 0 = legacy billboards, 1 = two card cross, 2 = three card tuft, 3 = adaptive (three near, two far)" );
	ri.Cvar_CheckRange( r_grassCardMode, 0, 3, qtrue );
	r_grassCardDebug = ri.Cvar_Get( "r_grassCardDebug", "0", CVAR_CHEAT,
		"Auto grass debug: 1 = color by card, 2 = color by card direction, 3/4/5 = force 1/2/3 cards, 6 = color by lod (green 3, orange 2 cards)" );
	ri.Cvar_CheckRange( r_grassCardDebug, 0, 6, qtrue );
	r_grassCardLodDist = ri.Cvar_Get( "r_grassCardLodDist", "600", CVAR_ARCHIVE,
		"r_grassCardMode 3: distance where the third card starts to fade out" );
	ri.Cvar_CheckRange( r_grassCardLodDist, 16, 16384, qfalse );
	r_grassCardWidth = ri.Cvar_Get( "r_grassCardWidth", "1", CVAR_ARCHIVE,
		"r_grassCardMode: width scale of the cards" );
	ri.Cvar_CheckRange( r_grassCardWidth, 0.25f, 2.0f, qfalse );
	r_foliageWind = ri.Cvar_Get( "r_foliageWind", "0", CVAR_ARCHIVE,
		"Surface sprite wind: 0 = legacy circular sway, 1 = coherent breeze (gusts travelling downwind)" );
	ri.Cvar_CheckRange( r_foliageWind, 0, 1, qtrue );
	r_foliageWindStrength = ri.Cvar_Get( "r_foliageWindStrength", "1", CVAR_ARCHIVE,
		"r_foliageWind 1: global strength, multiplied by the ssWind / ssWindIdle of the material" );
	ri.Cvar_CheckRange( r_foliageWindStrength, 0.0f, 4.0f, qfalse );
	r_foliageWindSpeed = ri.Cvar_Get( "r_foliageWindSpeed", "1", CVAR_ARCHIVE,
		"r_foliageWind 1: time scale of gusts and sway" );
	ri.Cvar_CheckRange( r_foliageWindSpeed, 0.0f, 4.0f, qfalse );
	r_foliageWindDirection = ri.Cvar_Get( "r_foliageWindDirection", "30", CVAR_ARCHIVE,
		"r_foliageWind 1: wind yaw in degrees, 0 = +X" );
	ri.Cvar_CheckRange( r_foliageWindDirection, 0.0f, 360.0f, qfalse );
	r_foliageWindDebug = ri.Cvar_Get( "r_foliageWindDebug", "0", CVAR_CHEAT,
		"Foliage wind debug: 1 = exaggerated strength, 2 = color by displacement, 3 = freeze time, 4 = color by large gust wave" );
	ri.Cvar_CheckRange( r_foliageWindDebug, 0, 4, qtrue );
	r_leafFlutter = ri.Cvar_Get( "r_leafFlutter", "0", CVAR_ARCHIVE,
		"Leaf flutter: small coherent motion of MD3 leaf / vine cards classified by r_autoFoliage (needs r_autoFoliage >= 1); trunks and branches never move" );
	ri.Cvar_CheckRange( r_leafFlutter, 0, 1, qtrue );
	r_leafFlutterStrength = ri.Cvar_Get( "r_leafFlutterStrength", "1", CVAR_ARCHIVE,
		"r_leafFlutter: amplitude, 1 = 1.5 units" );
	ri.Cvar_CheckRange( r_leafFlutterStrength, 0.0f, 4.0f, qfalse );
	r_leafFlutterSpeed = ri.Cvar_Get( "r_leafFlutterSpeed", "1", CVAR_ARCHIVE,
		"r_leafFlutter: time scale" );
	ri.Cvar_CheckRange( r_leafFlutterSpeed, 0.0f, 4.0f, qfalse );
	r_leafFlutterNormal = ri.Cvar_Get( "r_leafFlutterNormal", "0.25", CVAR_ARCHIVE,
		"r_leafFlutter: normal wobble in step with the motion (per pixel lit leaves), 0 = position only" );
	ri.Cvar_CheckRange( r_leafFlutterNormal, 0.0f, 1.0f, qfalse );
	r_leafFlutterDebug = ri.Cvar_Get( "r_leafFlutterDebug", "0", CVAR_CHEAT,
		"Leaf flutter debug bits: 1 = x8 amplitude, 2 = freeze time, 4 = highlight fluttering surfaces, 8 = color by displacement" );
	ri.Cvar_CheckRange( r_leafFlutterDebug, 0, 15, qtrue );
	r_foliageInteraction = ri.Cvar_Get( "r_foliageInteraction", "0", CVAR_ARCHIVE,
		"Characters push grass (surface sprites) and FOLIAGE_PLANT models (ferns, needs r_autoFoliage) aside: player + nearest NPCs as capsules sent by cgame" );
	ri.Cvar_CheckRange( r_foliageInteraction, 0, 1, qtrue );
	r_foliageInteractionStrength = ri.Cvar_Get( "r_foliageInteractionStrength", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: bend strength (1 = a stem in full contact leans about 45 degrees, at most 65)" );
	ri.Cvar_CheckRange( r_foliageInteractionStrength, 0.0f, 4.0f, qfalse );
	r_foliageInteractionRadius = ri.Cvar_Get( "r_foliageInteractionRadius", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: scale of the collider radius (bounding box half width)" );
	ri.Cvar_CheckRange( r_foliageInteractionRadius, 0.5f, 3.0f, qfalse );
	r_foliageInteractionMaxInteractors = ri.Cvar_Get( "r_foliageInteractionMaxInteractors", "8", CVAR_ARCHIVE,
		"r_foliageInteraction: colliders per frame, the player first, then the nearest NPCs" );
	ri.Cvar_CheckRange( r_foliageInteractionMaxInteractors, 1, MAX_FOLIAGE_INTERACTORS, qtrue );
	r_foliageInteractionNPCs = ri.Cvar_Get( "r_foliageInteractionNPCs", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: 0 = player only, 1 = also the nearest NPCs / other players" );
	ri.Cvar_CheckRange( r_foliageInteractionNPCs, 0, 1, qtrue );
	r_foliageInteractionDebug = ri.Cvar_Get( "r_foliageInteractionDebug", "0", CVAR_CHEAT,
		"Foliage interaction debug bits: 1 = draw colliders, 2 = x2 radius and strength, 4 = interaction only (no wind), 8 = contact heat color, 16 = freeze colliders, 32 = player only" );
	ri.Cvar_CheckRange( r_foliageInteractionDebug, 0, 63, qtrue );
	r_foliageBendField = ri.Cvar_Get( "r_foliageBendField", "0", CVAR_ARCHIVE,
		"r_foliageInteraction: plants the characters leave spring back instead of snapping to rest (persistent bend field around the player)" );
	ri.Cvar_CheckRange( r_foliageBendField, 0, 1, qtrue );
	r_foliageBendFieldSize = ri.Cvar_Get( "r_foliageBendFieldSize", "128", CVAR_ARCHIVE | CVAR_LATCH,
		"r_foliageBendField: texels per side of the bend field (64, 128 or 256)" );
	ri.Cvar_CheckRange( r_foliageBendFieldSize, 64, 256, qtrue );
	r_foliageBendFieldExtent = ri.Cvar_Get( "r_foliageBendFieldExtent", "1024", CVAR_ARCHIVE,
		"r_foliageBendField: world size of the square around the player the field covers" );
	ri.Cvar_CheckRange( r_foliageBendFieldExtent, 256.0f, 8192.0f, qfalse );
	r_foliageBendFieldStrength = ri.Cvar_Get( "r_foliageBendFieldStrength", "1", CVAR_ARCHIVE,
		"r_foliageBendField: scale of the persistent bend (0 = the field has no effect)" );
	ri.Cvar_CheckRange( r_foliageBendFieldStrength, 0.0f, 4.0f, qfalse );
	r_foliageBendFieldRecoveryTime = ri.Cvar_Get( "r_foliageBendFieldRecoveryTime", "0.8", CVAR_ARCHIVE,
		"r_foliageBendField: seconds until a plant the characters left is back to rest (swing down to ~5 %)" );
	ri.Cvar_CheckRange( r_foliageBendFieldRecoveryTime, 0.1f, 10.0f, qfalse );
	r_foliageBendFieldDamping = ri.Cvar_Get( "r_foliageBendFieldDamping", "0.6", CVAR_ARCHIVE,
		"r_foliageBendField: spring damping ratio, 1 = no swing past rest, lower = more sway" );
	ri.Cvar_CheckRange( r_foliageBendFieldDamping, 0.2f, 1.0f, qfalse );
	r_foliageBendFieldImpulse = ri.Cvar_Get( "r_foliageBendFieldImpulse", "1", CVAR_ARCHIVE,
		"r_foliageBendField: extra momentum along the walk that running through adds" );
	ri.Cvar_CheckRange( r_foliageBendFieldImpulse, 0.0f, 4.0f, qfalse );
	r_foliageBendFieldDebug = ri.Cvar_Get( "r_foliageBendFieldDebug", "0", CVAR_CHEAT,
		"Foliage field debug bits: 1 = field overlay (vectors, magnitude), 2 = covered square, 4 = freeze, 8 = x3 strength, 16 = field only (no direct push), 32 = direct push only" );
	ri.Cvar_CheckRange( r_foliageBendFieldDebug, 0, 63, qtrue );
	r_plantWind = ri.Cvar_Get( "r_plantWind", "0", CVAR_ARCHIVE,
		"Breeze bend of FOLIAGE_PLANT models (ferns) around their root, 0 = none; direction and speed from r_foliageWindDirection / r_foliageWindSpeed (needs r_autoFoliage)" );
	ri.Cvar_CheckRange( r_plantWind, 0, 1, qtrue );
	r_plantWindStrength = ri.Cvar_Get( "r_plantWindStrength", "1", CVAR_ARCHIVE, "Plant root wind bend strength, 0-4 (needs r_plantWind 1)" );
	ri.Cvar_CheckRange( r_plantWindStrength, 0.0f, 4.0f, qfalse );
	r_autoPBRConvert = ri.Cvar_Get( "r_autoPBRConvert", "0", CVAR_ARCHIVE | CVAR_LATCH, "Convert legacy shaders with alphaGen lightingSpecular / tcGen environment stages (vertex lit in rend2) to per pixel lightall materials; the specular mask becomes spatial roughness / metalness" );
	ri.Cvar_CheckRange( r_autoPBRConvert, 0, 1, qtrue );
	r_autoPBRRoughness = ri.Cvar_Get( "r_autoPBRRoughness", "0", CVAR_ARCHIVE, "Auto PBR roughness of legacy diffuse-only materials: 0 = constant class roughness, 1 = subtle variation generated from the diffuse detail (maps are built at load, turning it on needs vid_restart)" );
	ri.Cvar_CheckRange( r_autoPBRRoughness, 0, 1, qtrue );
	r_diffuseBRDF = ri.Cvar_Get( "r_diffuseBRDF", "0", CVAR_ARCHIVE, "Standard PBR diffuse BRDF: 0 = Lambert, 1 = Burley/Disney" );
	ri.Cvar_CheckRange( r_diffuseBRDF, 0, 1, qtrue );
	r_diffuseIBL = ri.Cvar_Get( "r_diffuseIBL", "0", CVAR_ARCHIVE | CVAR_LATCH, "Directional light-grid ambient from runtime cubemap probes" );
	ri.Cvar_CheckRange( r_diffuseIBL, 0, 1, qtrue );
	r_diffuseIBLStrength = ri.Cvar_Get( "r_diffuseIBLStrength", "1", CVAR_ARCHIVE, "Strength of probe directional ambient modulation" );
	ri.Cvar_CheckRange( r_diffuseIBLStrength, 0.0f, 1.0f, qfalse );
	r_diffuseIBLDebug = ri.Cvar_Get( "r_diffuseIBLDebug", "0", CVAR_CHEAT, "Diffuse IBL debug: 1 irradiance, 2 factor, 3 old ambient, 4 new ambient, 5 selected probe" );
	ri.Cvar_CheckRange( r_diffuseIBLDebug, 0, 5, qtrue );

	// Forward+ / clustered dynamic lights (tr_forwardplus.cpp), off by default
	// compiled GLSL program cache on disk (tr_glsl.cpp)
	r_shaderProgramCache = ri.Cvar_Get( "r_shaderProgramCache", "1", CVAR_ARCHIVE | CVAR_LATCH, "Keep compiled GLSL programs in glslcache/ so later starts skip compiling them (needs GL_ARB_get_program_binary)" );
	ri.Cvar_CheckRange( r_shaderProgramCache, 0, 1, qtrue );
	r_shaderProgramCacheMaxMB = ri.Cvar_Get( "r_shaderProgramCacheMaxMB", "512", CVAR_ARCHIVE, "Size limit of the GLSL program cache file, least recently used programs are dropped first" );
	ri.Cvar_CheckRange( r_shaderProgramCacheMaxMB, 16, 4096, qtrue );

	r_forwardPlus = ri.Cvar_Get( "r_forwardPlus", "0", CVAR_ARCHIVE, "Dynamic lights: 0 = legacy (32 lights, per surface masks), 1 = Forward+ clustered light lists (up to 256 lights)" );
	ri.Cvar_CheckRange( r_forwardPlus, 0, 1, qtrue );
	r_forwardPlusTileSize = ri.Cvar_Get( "r_forwardPlusTileSize", "64", CVAR_ARCHIVE, "Forward+: screen tile size in pixels" );
	ri.Cvar_CheckRange( r_forwardPlusTileSize, 16, 256, qtrue );
	r_forwardPlusSlices = ri.Cvar_Get( "r_forwardPlusSlices", "16", CVAR_ARCHIVE, "Forward+: logarithmic depth slices per tile (1 = 2D tiles only)" );
	ri.Cvar_CheckRange( r_forwardPlusSlices, 1, 64, qtrue );
	r_forwardPlusNearSlice = ri.Cvar_Get( "r_forwardPlusNearSlice", "48", CVAR_ARCHIVE, "Forward+: view depth covered by the first depth slice" );
	ri.Cvar_CheckRange( r_forwardPlusNearSlice, 1.0f, 1024.0f, qfalse );
	r_forwardPlusMaxLightsPerCluster = ri.Cvar_Get( "r_forwardPlusMaxLightsPerCluster", "64", CVAR_ARCHIVE, "Forward+: lights kept per cluster, the least important ones are dropped" );
	ri.Cvar_CheckRange( r_forwardPlusMaxLightsPerCluster, 1, 255, qtrue );
	r_forwardPlusDebug = ri.Cvar_Get( "r_forwardPlusDebug", "0", CVAR_CHEAT | CVAR_LATCH, "Forward+ debug view: 1 tiles, 2 depth slice, 3 cluster, 4 lights per cluster, 5 overflow, 6 shadowed lights, 7 unshadowed lights, 8 light spheres, 9 light r_forwardPlusDebugLight" );
	ri.Cvar_CheckRange( r_forwardPlusDebug, 0, 9, qtrue );
	r_forwardPlusDebugLight = ri.Cvar_Get( "r_forwardPlusDebugLight", "0", CVAR_CHEAT, "Forward+: light index shown by r_forwardPlusDebug 9" );
	r_forwardPlusMaxShadowLights = ri.Cvar_Get( "r_forwardPlusMaxShadowLights", "4", CVAR_ARCHIVE, "Forward+: dynamic lights with a shadow cube (needs r_dlightMode 2), the most important ones get them" );
	ri.Cvar_CheckRange( r_forwardPlusMaxShadowLights, 0, MAX_DLIGHT_SHADOWS, qtrue );

	r_ltcAreaLights = ri.Cvar_Get( "r_ltcAreaLights", "0", CVAR_ARCHIVE | CVAR_LATCH, "LTC rectangle / line area lights (maps/<map>.arealights.json, saber lines), requires r_forwardPlus 1" );
	r_ltcDebug = ri.Cvar_Get( "r_ltcDebug", "0", CVAR_CHEAT | CVAR_LATCH, "LTC area light debug: 1 specular, 2 diffuse, 3 source mode, 4 area lights per cluster, 5 influence bounds, 6 outlines, 7 normals / axes, 8 strongest light id" );
	r_ltcDebugLight = ri.Cvar_Get( "r_ltcDebugLight", "-1", CVAR_CHEAT, "LTC area light highlighted by r_ltcDebug (map light id, -1 = nearest)" );
	r_ltcIntensityScale = ri.Cvar_Get( "r_ltcIntensityScale", "1", CVAR_ARCHIVE, "LTC area lights: radiance multiplier" );
	r_ltcStaticDiffuse = ri.Cvar_Get( "r_ltcStaticDiffuse", "0", CVAR_ARCHIVE, "LTC area lights: also diffuse for static_specular map lights (their diffuse is usually baked in the lightmap)" );
	r_ltcMaxLights = ri.Cvar_Get( "r_ltcMaxLights", "64", CVAR_ARCHIVE, "LTC area lights: map lights per scene, nearest first" );
	ri.Cvar_CheckRange( r_ltcMaxLights, 0, MAX_RENDER_DLIGHTS, qtrue );
	r_ltcAutoAreaLights = ri.Cvar_Get( "r_ltcAutoAreaLights", "1", CVAR_ARCHIVE, "LTC area lights from the emissive surfaces of maps without an .arealights.json (at map load, r_ltcReloadLights): 0 off, 1 confident lamp shapes, 2 also loosely fitted ones (glow / emissive / surfacelight surfaces)" );
	r_ltcSaberAreaLights = ri.Cvar_Get( "r_ltcSaberAreaLights", "0", CVAR_ARCHIVE, "Saber blades light as LTC lines instead of a point light (requires r_ltcAreaLights 1)" );

	r_normalMapping = ri.Cvar_Get( "r_normalMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable normal mapping" );
	r_specularMapping = ri.Cvar_Get( "r_specularMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable specular mapping" );
	r_deluxeMapping = ri.Cvar_Get( "r_deluxeMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable reading deluxemaps when compiled with q3map2" );
	r_deluxeSpecular = ri.Cvar_Get("r_deluxeSpecular", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable/scale the specular response from deluxemaps");
	r_cubeMapping = ri.Cvar_Get( "r_cubeMapping", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable cubemapping" );
	r_cubeMappingBounces = ri.Cvar_Get("r_cubeMappingBounces", "0", CVAR_ARCHIVE | CVAR_LATCH, "Renders cubemaps multiple times to get reflections in reflections");
	ri.Cvar_CheckRange(r_cubeMappingBounces, 0, 2, qfalse);
	r_baseNormalX = ri.Cvar_Get( "r_baseNormalX", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseNormalY = ri.Cvar_Get( "r_baseNormalY", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseParallax = ri.Cvar_Get( "r_baseParallax", "0.05", CVAR_ARCHIVE | CVAR_LATCH, "" );
   	r_baseSpecular = ri.Cvar_Get( "r_baseSpecular", "0.04", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_dlightMode = ri.Cvar_Get( "r_dlightMode", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_pshadowDist = ri.Cvar_Get( "r_pshadowDist", "128", CVAR_ARCHIVE, "" );
	r_imageUpsample = ri.Cvar_Get( "r_imageUpsample", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_imageUpsampleMaxSize = ri.Cvar_Get( "r_imageUpsampleMaxSize", "1024", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_imageUpsampleType = ri.Cvar_Get( "r_imageUpsampleType", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_genNormalMaps = ri.Cvar_Get( "r_genNormalMaps", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable generating normal maps from diffuse maps" );

	r_forceSun = ri.Cvar_Get( "r_forceSun", "0", CVAR_CHEAT, "" );
	r_forceSunMapLightScale = ri.Cvar_Get( "r_forceSunMapLightScale", "1.0", CVAR_CHEAT, "" );
	r_forceSunLightScale = ri.Cvar_Get( "r_forceSunLightScale", "1.0", CVAR_CHEAT, "" );
	r_forceSunAmbientScale = ri.Cvar_Get( "r_forceSunAmbientScale", "0.5", CVAR_CHEAT, "" );
	r_drawSunRays = ri.Cvar_Get( "r_drawSunRays", "0", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_sunlightMode = ri.Cvar_Get( "r_sunlightMode", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );

	r_volumetricFog = ri.Cvar_Get("r_volumetricFog", "0", CVAR_ARCHIVE | CVAR_LATCH, "Volumetric fog: 0 = off, 1 = light grid ray march (legacy), 2 = froxel volume with sun, dynamic lights, shadows and temporal reprojection");
	r_entityLightGrid = ri.Cvar_Get("r_entityLightGrid", "0", CVAR_ARCHIVE | CVAR_LATCH, "Entity BSP light grid: 0 = legacy, 1 = three CPU samples, 2 = per-fragment GPU samples");
	ri.Cvar_CheckRange(r_entityLightGrid, 0, 2, qtrue);
	r_entityLightGridDebug = ri.Cvar_Get("r_entityLightGridDebug", "0", CVAR_CHEAT | CVAR_LATCH, "Entity grid debug: 1 ambient, 2 direct, 3 direction, 4 validity, 5 cell, 6 legacy, 7 multi-point, 8 GPU, 9 difference");
	ri.Cvar_CheckRange(r_entityLightGridDebug, 0, 9, qtrue);
	ri.Cvar_CheckRange(r_volumetricFog, 0, 2, qtrue);
	r_volumetricFogDefaultScale = ri.Cvar_Get("r_volumetricFogDefaultScale", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "Scales volumetric fog density unless scale has been explicitly defined");
	r_volumetricFogSamples = ri.Cvar_Get("r_volumetricFogSamples", "48", CVAR_ARCHIVE | CVAR_LATCH, "How many ray samples to take");
	ri.Cvar_CheckRange(r_volumetricFogSamples, 16, 128, qfalse);
	r_volumetricFogScale = ri.Cvar_Get("r_volumetricFogScale", "1.0", CVAR_TEMP, "Temporarily scales volumetric fog density");
	r_volumetricFogQuality = ri.Cvar_Get("r_volumetricFogQuality", "1", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog (r_volumetricFog 2) preset: 0 = low, 1 = medium, 2 = high (grid scale and depth slices)");
	ri.Cvar_CheckRange(r_volumetricFogQuality, 0, 2, qtrue);
	r_volumetricFogGridScale = ri.Cvar_Get("r_volumetricFogGridScale", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: screen pixels per froxel, 0 = from r_volumetricFogQuality");
	ri.Cvar_CheckRange(r_volumetricFogGridScale, 0, 32, qtrue);
	r_volumetricFogSlices = ri.Cvar_Get("r_volumetricFogSlices", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: depth slices, 0 = from r_volumetricFogQuality");
	ri.Cvar_CheckRange(r_volumetricFogSlices, 0, 128, qtrue);
	r_volumetricFogFar = ri.Cvar_Get("r_volumetricFogFar", "0", CVAR_ARCHIVE, "Froxel fog: distance covered by the froxel slices, 0 = automatic (4096). Beyond it the medium of the last slice is extrapolated");
	ri.Cvar_CheckRange(r_volumetricFogFar, 0, 65536, qfalse);
	r_volumetricFogAnisotropy = ri.Cvar_Get("r_volumetricFogAnisotropy", "0.2", CVAR_ARCHIVE, "Froxel fog: Henyey-Greenstein g of the sun and dynamic light scattering, 0 = isotropic, > 0 forward, < 0 backward; the default of the media without their own g (fogAnisotropy, local volume and FX anisotropy)");
	ri.Cvar_CheckRange(r_volumetricFogAnisotropy, -0.9f, 0.9f, qfalse);
	r_volumetricFogTemporal = ri.Cvar_Get("r_volumetricFogTemporal", "1", CVAR_ARCHIVE, "Froxel fog: temporal reprojection and jittered sampling");
	ri.Cvar_CheckRange(r_volumetricFogTemporal, 0, 1, qtrue);
	r_volumetricFogHistoryWeight = ri.Cvar_Get("r_volumetricFogHistoryWeight", "0.9", CVAR_ARCHIVE, "Froxel fog: weight of the reprojected history");
	ri.Cvar_CheckRange(r_volumetricFogHistoryWeight, 0.0f, 0.98f, qfalse);
	r_volumetricFogSunScale = ri.Cvar_Get("r_volumetricFogSunScale", "1", CVAR_ARCHIVE, "Froxel fog: sun scattering multiplier");
	ri.Cvar_CheckRange(r_volumetricFogSunScale, 0.0f, 16.0f, qfalse);
	r_volumetricFogDlightScale = ri.Cvar_Get("r_volumetricFogDlightScale", "1", CVAR_ARCHIVE, "Froxel fog: dynamic light scattering multiplier");
	ri.Cvar_CheckRange(r_volumetricFogDlightScale, 0.0f, 16.0f, qfalse);
	r_volumetricFogLightTile = ri.Cvar_Get("r_volumetricFogLightTile", "8", CVAR_NONE, "Froxel fog: froxels per side of a dynamic light tile (4, 8, 16), see r_vfogLightStats");
	ri.Cvar_CheckRange(r_volumetricFogLightTile, 4.0f, 16.0f, qtrue);
	r_volumetricFogStaticScale = ri.Cvar_Get("r_volumetricFogStaticScale", "1", CVAR_ARCHIVE, "Froxel fog: baked (light grid) scattering multiplier");
	ri.Cvar_CheckRange(r_volumetricFogStaticScale, 0.0f, 16.0f, qfalse);
	r_volumetricFogStaticDirectional = ri.Cvar_Get("r_volumetricFogStaticDirectional", "0", CVAR_ARCHIVE, "Froxel fog: 1 = the directed (non-sun) light grid part gets the phase function along its baked direction, 0 = isotropic");
	r_volumetricSelfShadow = ri.Cvar_Get("r_volumetricSelfShadow", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: dense media shadow the light inside the media (current frame density along the light ray): 0 = off, 1 = sun, 2 = sun + the r_volumetricSelfShadowMaxLights strongest dynamic lights (vid_restart)");
	ri.Cvar_CheckRange(r_volumetricSelfShadow, 0, 2, qtrue);
	r_volumetricSelfShadowSamples = ri.Cvar_Get("r_volumetricSelfShadowSamples", "6", CVAR_ARCHIVE, "Froxel fog self-shadow: density samples along the sun ray (dynamic lights use half, at least 3)");
	ri.Cvar_CheckRange(r_volumetricSelfShadowSamples, 2, 16, qtrue);
	r_volumetricSelfShadowDistance = ri.Cvar_Get("r_volumetricSelfShadowDistance", "768", CVAR_ARCHIVE, "Froxel fog self-shadow: length of the light ray march in world units (clipped to the froxel volume)");
	ri.Cvar_CheckRange(r_volumetricSelfShadowDistance, 32, 8192, qfalse);
	r_volumetricSelfShadowOutsideHeightFog = ri.Cvar_Get("r_volumetricSelfShadowOutsideHeightFog", "1", CVAR_ARCHIVE, "Froxel fog self-shadow: 1 = beyond the march the height fog is integrated analytically (other media there count as empty), 0 = nothing beyond the march");
	ri.Cvar_CheckRange(r_volumetricSelfShadowOutsideHeightFog, 0, 1, qtrue);
	r_volumetricSelfShadowMaxLights = ri.Cvar_Get("r_volumetricSelfShadowMaxLights", "2", CVAR_ARCHIVE, "Froxel fog self-shadow, r_volumetricSelfShadow 2: number of the strongest dynamic lights with a media shadow (0-4)");
	ri.Cvar_CheckRange(r_volumetricSelfShadowMaxLights, 0, 4, qtrue);
	// approximate multiple scattering (Wrenninge et al. 2013 octaves), docs/rend2-volumetric-fog.md
	r_volumetricMultiScatter = ri.Cvar_Get("r_volumetricMultiScatter", "0", CVAR_ARCHIVE, "Froxel fog: approximate multiple scattering of dense media, returns part of the light removed by the media self-shadow (needs r_volumetricSelfShadow): 0 = off, 1 = sun, 2 = sun + the self-shadowed dynamic lights");
	ri.Cvar_CheckRange(r_volumetricMultiScatter, 0, 2, qtrue);
	r_volumetricFogRGBExtinction = ri.Cvar_Get("r_volumetricFogRGBExtinction", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: RGB extinction, media absorb the color channels differently (fogExtinctionColor, fog volume Extinction, r_volumetricFogHeightExtinction); 0 = scalar extinction (no extra memory), 1 = per channel transmittance (+24 bytes per froxel, two draw composite)");
	ri.Cvar_CheckRange(r_volumetricFogRGBExtinction, 0, 1, qtrue);
	r_volumetricMultiScatterOctaves = ri.Cvar_Get("r_volumetricMultiScatterOctaves", "2", CVAR_ARCHIVE, "Froxel fog multiple scattering: number of scattering octaves beyond the single scattering (1-3)");
	ri.Cvar_CheckRange(r_volumetricMultiScatterOctaves, 1, 3, qtrue);
	r_volumetricMultiScatterAttenuation = ri.Cvar_Get("r_volumetricMultiScatterAttenuation", "0.25", CVAR_ARCHIVE, "Froxel fog multiple scattering: optical depth scale a per octave (lower = light penetrates deeper; kept <= r_volumetricMultiScatterContribution)");
	ri.Cvar_CheckRange(r_volumetricMultiScatterAttenuation, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterContribution = ri.Cvar_Get("r_volumetricMultiScatterContribution", "0.5", CVAR_ARCHIVE, "Froxel fog multiple scattering: energy b per octave, multiplied by the albedo and the medium thickness");
	ri.Cvar_CheckRange(r_volumetricMultiScatterContribution, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterPhase = ri.Cvar_Get("r_volumetricMultiScatterPhase", "0.5", CVAR_ARCHIVE, "Froxel fog multiple scattering: anisotropy scale c per octave (0 = isotropic octaves)");
	ri.Cvar_CheckRange(r_volumetricMultiScatterPhase, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterLength = ri.Cvar_Get("r_volumetricMultiScatterLength", "64", CVAR_ARCHIVE, "Froxel fog multiple scattering: typical size of a dense medium in world units; media with extinction * length << 1 (thin fog) get almost none");
	ri.Cvar_CheckRange(r_volumetricMultiScatterLength, 1.0f, 4096.0f, qfalse);
	r_volumetricMultiScatterShadowFill = ri.Cvar_Get("r_volumetricMultiScatterShadowFill", "0.25", CVAR_ARCHIVE, "Froxel fog multiple scattering: how far the octaves may fill the geometry shadow in thick media (0-0.5)");
	ri.Cvar_CheckRange(r_volumetricMultiScatterShadowFill, 0.0f, 0.5f, qfalse);
	r_volumetricFogDlightShadows = ri.Cvar_Get("r_volumetricFogDlightShadows", "1", CVAR_ARCHIVE, "Froxel fog: dynamic lights use their shadow maps (needs r_dlightMode 2)");
	ri.Cvar_CheckRange(r_volumetricFogDlightShadows, 0, 1, qtrue);
	r_volumetricFogBloom = ri.Cvar_Get("r_volumetricFogBloom", "0", CVAR_ARCHIVE, "Froxel fog: bright in-scattering added to the glow buffer (bloom), 0 = none");
	ri.Cvar_CheckRange(r_volumetricFogBloom, 0.0f, 4.0f, qfalse);
	r_volumetricEmission = ri.Cvar_Get("r_volumetricEmission", "1", CVAR_ARCHIVE, "Froxel fog: scale of the emission of local fog volumes and FX particle media (glowing gas), 0 = off");
	ri.Cvar_CheckRange(r_volumetricEmission, 0.0f, 16.0f, qfalse);
	r_volumetricFogReset = ri.Cvar_Get("r_volumetricFogReset", "0", 0, "Set to 1 by game code to reset the froxel fog history (camera cut), cleared by the renderer");
	r_volumetricFogDebug = ri.Cvar_Get("r_volumetricFogDebug", "0", CVAR_CHEAT, "Froxel fog debug view: 1 density, 2 sun (unshadowed), 3 sun (shadowed), 4 dynamic lights, 5 baked light, 6 scattering, 7 transmittance, 8 history weight, 9 integrated volume, 10 slices, 11 density of the BSP fog volumes, 12 density of the height fog, 13 noise modulation, 14 density without noise, 15 density with noise, 16 density of the local fog volumes, 17 local vs other fog share, 18 local fog volume bounds, 19 local volumes per slice, 20-25 baked light grid terms, 26 density of the FX particle media, 27 FX particle history reduction, 28 FX particle proxy bounds, 29 dynamic lights per froxel cluster (cyan: spot lights), 30 scattering source, 31 emissive source, 32 combined source, 33 integrated emission, 34 history vs emission, 35 medium extinction, 36 albedo, 37 phase lobes, 38 mixed g, 39 sun phase, r_volumetricSelfShadow: 40 media density, 41 sun ray optical depth, 42 sun media transmittance, 43 sun geometry shadow only, 44 sun media shadow only, 45 sun both, r_volumetricMultiScatter: 46 sun single scattering, 47 sun multiple scattering term, 48 sun combined, 49 multiple scattering ratio, 50 optical depth (red: towards the sun, green: extinction * r_volumetricMultiScatterLength), r_volumetricFogRGBExtinction: 51 extinction sigma_t.rgb, 52 transmittance T.rgb, 53 color shift RGB - scalar, 54 |RGB - scalar| heat, 55 extinction chroma, 56 tail transmittance");
	ri.Cvar_CheckRange(r_volumetricFogDebug, 0, 56, qtrue);
	// volumetric FX particles (tr_volparticle.cpp): media of the .efx particles with a volumetricMedia block.
	// Mirrored by the SP cgame (only calls the engine with it set), so off by default.
	r_volumetricParticles = ri.Cvar_Get("r_volumetricParticles", "0", CVAR_ARCHIVE, "FX particles with a volumetricMedia block add participating media to the froxel fog (r_volumetricFog 2)");
	ri.Cvar_CheckRange(r_volumetricParticles, 0, 1, qtrue);
	r_volumetricParticlesMax = ri.Cvar_Get("r_volumetricParticlesMax", "128", CVAR_ARCHIVE, "r_volumetricParticles: most important particles uploaded per frame (the rest is capped)");
	ri.Cvar_CheckRange(r_volumetricParticlesMax, 0, MAX_GPU_VOL_PARTICLES, qtrue);
	r_volumetricParticlesScale = ri.Cvar_Get("r_volumetricParticlesScale", "1", CVAR_ARCHIVE, "r_volumetricParticles: extinction multiplier of the particle media");
	ri.Cvar_CheckRange(r_volumetricParticlesScale, 0, 16, qfalse);
	r_volumetricParticlesHistory = ri.Cvar_Get("r_volumetricParticlesHistory", "0.3", CVAR_ARCHIVE, "r_volumetricParticles: share of the temporal history weight kept where the particle density changed (0 = none, 1 = as static fog)");
	ri.Cvar_CheckRange(r_volumetricParticlesHistory, 0, 1, qfalse);
	r_particleLighting = ri.Cvar_Get("r_particleLighting", "0", CVAR_ARCHIVE | CVAR_LATCH, "Light alpha-blended sprite particles (smoke, dust) with the froxel fog light field (r_volumetricFog 2); additive sprites stay unlit");
	ri.Cvar_CheckRange(r_particleLighting, 0, 1, qtrue);
	r_particleLightingMix = ri.Cvar_Get("r_particleLightingMix", "1", CVAR_ARCHIVE, "r_particleLighting: blend between the authored (0) and the lit (1) particle color");
	ri.Cvar_CheckRange(r_particleLightingMix, 0, 1, qfalse);
	r_particleLightingScale = ri.Cvar_Get("r_particleLightingScale", "1", CVAR_ARCHIVE, "r_particleLighting: gain of the local light (1 = the map average light leaves the authored color unchanged)");
	ri.Cvar_CheckRange(r_particleLightingScale, 0, 16, qfalse);
	r_particleLightingFloor = ri.Cvar_Get("r_particleLightingFloor", "0.03", CVAR_ARCHIVE, "r_particleLighting: minimum light factor (unlit smoke never turns fully black)");
	ri.Cvar_CheckRange(r_particleLightingFloor, 0, 1, qfalse);
	r_particleLightingDebug = ri.Cvar_Get("r_particleLightingDebug", "0", CVAR_CHEAT, "r_particleLighting: 1 light field, 2 baked only, 3 sun only, 4 dynamic lights only, 5 classification (magenta lit, cyan unlit sprites)");
	ri.Cvar_CheckRange(r_particleLightingDebug, 0, 5, qtrue);
	r_spotLights = ri.Cvar_Get("r_spotLights", "1", CVAR_ARCHIVE, "Spot lights (GetRefSpotLightAPI, efx spot group): 0 = submitted as the point lights they would be without the cone");
	ri.Cvar_CheckRange(r_spotLights, 0, 1, qtrue);
	r_spotLightShadows = ri.Cvar_Get("r_spotLightShadows", "1", CVAR_ARCHIVE, "Spot lights (r_dlightMode 2): 1 = shadowed (one perspective view up to 60 degrees, else cube faces), 0 = no spot light shadows");
	ri.Cvar_CheckRange(r_spotLightShadows, 0, 1, qtrue);
	r_spotLightDebug = ri.Cvar_Get("r_spotLightDebug", "0", CVAR_CHEAT, "Spot lights: 1 cones + per second light list, 2 cones + shadow frustums, 3 surfaces lit by spots only (no fog dlights), 4 fog lit by spots only (no spots on surfaces)");
	ri.Cvar_CheckRange(r_spotLightDebug, 0, 4, qtrue);
	r_spotLightCookies = ri.Cvar_Get("r_spotLightCookies", "1", CVAR_ARCHIVE, "Spot light cookies / gobos (efx spot cookie, tr_lightcookie.cpp): 0 off, 1 intensity, 2 colored");
	ri.Cvar_CheckRange(r_spotLightCookies, 0, 2, qtrue);
	r_spotLightCookieDebug = ri.Cvar_Get("r_spotLightCookieDebug", "0", CVAR_CHEAT, "Spot light cookies on surfaces: 1 projected uv, 2 cookie factor, 3 cookie * shadow");
	ri.Cvar_CheckRange(r_spotLightCookieDebug, 0, 3, qtrue);
	r_volumetricParticlesDebug = ri.Cvar_Get("r_volumetricParticlesDebug", "0", CVAR_CHEAT, "r_volumetricParticles: 1 = print the culling statistics every 60 frames");
	r_volumetricFogFreeze = ri.Cvar_Get("r_volumetricFogFreeze", "0", CVAR_CHEAT, "Froxel fog: keep the current froxel volume and its camera (debugging)");
	ri.Cvar_CheckRange(r_volumetricFogFreeze, 0, 1, qtrue);
	r_volumetricFogHeight = ri.Cvar_Get("r_volumetricFogHeight", "0", CVAR_ARCHIVE, "Froxel fog (r_volumetricFog 2): height fog (ground haze) medium, 0 = off, 1 = on");
	ri.Cvar_CheckRange(r_volumetricFogHeight, 0, 1, qtrue);
	r_volumetricFogHeightOpaqueDistance = ri.Cvar_Get("r_volumetricFogHeightOpaqueDistance", "3000", CVAR_ARCHIVE, "Froxel fog height fog: distance at which the medium at the base height becomes opaque, as fogParms depthForOpaque (world units)");
	ri.Cvar_CheckRange(r_volumetricFogHeightOpaqueDistance, 1.0f, 1000000.0f, qfalse);
	r_volumetricFogHeightBase = ri.Cvar_Get("r_volumetricFogHeightBase", "auto", CVAR_ARCHIVE, "Froxel fog height fog: world Z of the base, auto = lowest floor; env.json HeightFog.base takes priority");
	r_volumetricFogHeightFalloff = ri.Cvar_Get("r_volumetricFogHeightFalloff", "256", CVAR_ARCHIVE, "Froxel fog height fog: height (world units) over which the density falls by a factor e above the base height");
	ri.Cvar_CheckRange(r_volumetricFogHeightFalloff, 1.0f, 65536.0f, qfalse);
	r_volumetricFogHeightMaxDensity = ri.Cvar_Get("r_volumetricFogHeightMaxDensity", "1", CVAR_ARCHIVE, "Froxel fog height fog: maximum density below the base height, as a multiple of the base density");
	ri.Cvar_CheckRange(r_volumetricFogHeightMaxDensity, 1.0f, 64.0f, qfalse);
	r_volumetricFogHeightTopHeight = ri.Cvar_Get("r_volumetricFogHeightTopHeight", "0", CVAR_ARCHIVE, "Froxel fog height fog: height above the base where the medium fades out (soft cutoff), 0 = none");
	ri.Cvar_CheckRange(r_volumetricFogHeightTopHeight, 0.0f, 65536.0f, qfalse);
	r_volumetricFogHeightColor = ri.Cvar_Get("r_volumetricFogHeightColor", "0.7 0.75 0.8", CVAR_ARCHIVE, "Froxel fog height fog: scattering color (albedo), \"r g b\" in 0..1 as fogParms");
	r_volumetricFogHeightExtinction = ri.Cvar_Get("r_volumetricFogHeightExtinction", "1 1 1", CVAR_ARCHIVE, "Froxel fog height fog: relative extinction per channel \"r g b\", normalized to mean 1 (r_volumetricFogRGBExtinction)");
	r_volumetricFogNoise = ri.Cvar_Get("r_volumetricFogNoise", "0", CVAR_ARCHIVE, "Froxel fog: media with world space noise density, bits: 1 height fog, 2 BSP fog volumes, 4 global fog, 8 local fog volumes with the noise flag (0 = homogeneous)");
	ri.Cvar_CheckRange(r_volumetricFogNoise, 0, 7, qtrue);
	r_volumetricFogNoiseScale = ri.Cvar_Get("r_volumetricFogNoiseScale", "4096", CVAR_ARCHIVE, "Froxel fog noise: period of the macro noise tile (world units)");
	ri.Cvar_CheckRange(r_volumetricFogNoiseScale, 64.0f, 65536.0f, qfalse);
	r_volumetricFogNoiseContrast = ri.Cvar_Get("r_volumetricFogNoiseContrast", "1", CVAR_ARCHIVE, "Froxel fog noise: contrast of the macro noise, 0 = homogeneous, 1 = density 0..2x, higher = sparser clumps (the mean density is kept)");
	ri.Cvar_CheckRange(r_volumetricFogNoiseContrast, 0.0f, 4.0f, qfalse);
	r_volumetricFogNoiseDetailScale = ri.Cvar_Get("r_volumetricFogNoiseDetailScale", "900", CVAR_ARCHIVE, "Froxel fog noise: period of the detail noise tile (world units)");
	ri.Cvar_CheckRange(r_volumetricFogNoiseDetailScale, 16.0f, 65536.0f, qfalse);
	r_volumetricFogNoiseDetailContrast = ri.Cvar_Get("r_volumetricFogNoiseDetailContrast", "0", CVAR_ARCHIVE, "Froxel fog noise: contrast of the detail noise (second texture sample), 0 = off");
	ri.Cvar_CheckRange(r_volumetricFogNoiseDetailContrast, 0.0f, 4.0f, qfalse);
	r_volumetricFogNoiseWind = ri.Cvar_Get("r_volumetricFogNoiseWind", "0 0 0", CVAR_ARCHIVE, "Froxel fog noise: wind, \"x y z\" world units per second, the noise drifts with it (0 = world stable)");

	r_sunShadows = ri.Cvar_Get( "r_sunShadows", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_shadowFilter = ri.Cvar_Get( "r_shadowFilter", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_shadowMapSize = ri.Cvar_Get( "r_shadowMapSize", "1024", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_shadowCascadeZNear = ri.Cvar_Get( "r_shadowCascadeZNear", "4", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_shadowCascadeZFar = ri.Cvar_Get( "r_shadowCascadeZFar", "3072", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_shadowCascadeZBias = ri.Cvar_Get( "r_shadowCascadeZBias", "-320", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_sunShadowMode = ri.Cvar_Get( "r_sunShadowMode", "1", CVAR_ARCHIVE | CVAR_LATCH, "Sun shadows: 0 legacy, 1 stabilized blended PCSS" );
	ri.Cvar_CheckRange( r_sunShadowMode, 0, 1, qtrue );
	r_sunShadowAlphaCasters = ri.Cvar_Get( "r_sunShadowAlphaCasters", "1", CVAR_ARCHIVE | CVAR_LATCH, "Shadows 2.0 foliage/cutout sun casters and receivers (q3map_alphashadow and surfaceSprites)" );
	ri.Cvar_CheckRange( r_sunShadowAlphaCasters, 0, 1, qtrue );
	r_shadowCascadeBlend = ri.Cvar_Get( "r_shadowCascadeBlend", "0.10", CVAR_ARCHIVE, "Cascade transition width as a fraction of the smaller adjacent cascade" );
	ri.Cvar_CheckRange( r_shadowCascadeBlend, 0.0f, 0.3f, qfalse );
	r_shadowDepthBias = ri.Cvar_Get( "r_shadowDepthBias", "0.15", CVAR_ARCHIVE, "Sun shadow constant receiver bias in world units" );
	ri.Cvar_CheckRange( r_shadowDepthBias, 0.0f, 8.0f, qfalse );
	r_shadowNormalBias = ri.Cvar_Get( "r_shadowNormalBias", "0.75", CVAR_ARCHIVE, "Sun shadow normal offset in cascade texels" );
	ri.Cvar_CheckRange( r_shadowNormalBias, 0.0f, 8.0f, qfalse );
	r_shadowSlopeBias = ri.Cvar_Get( "r_shadowSlopeBias", "1.0", CVAR_ARCHIVE, "Sun shadow receiver-plane depth bias scale" );
	ri.Cvar_CheckRange( r_shadowSlopeBias, 0.0f, 4.0f, qfalse );
	r_shadowReceiverBiasClamp = ri.Cvar_Get( "r_shadowReceiverBiasClamp", "4.0", CVAR_ARCHIVE, "Maximum receiver-plane correction in world units" );
	ri.Cvar_CheckRange( r_shadowReceiverBiasClamp, 0.0f, 32.0f, qfalse );
	r_shadowPCSS = ri.Cvar_Get( "r_shadowPCSS", "1", CVAR_ARCHIVE, "Contact-hardening PCSS for sun cascades" );
	ri.Cvar_CheckRange( r_shadowPCSS, 0, 1, qtrue );
	r_shadowPCSSQuality = ri.Cvar_Get( "r_shadowPCSSQuality", "1", CVAR_ARCHIVE, "Sun PCSS quality: 0 low, 1 high, 2 ultra" );
	ri.Cvar_CheckRange( r_shadowPCSSQuality, 0, 2, qtrue );
	r_shadowSunAngularDiameter = ri.Cvar_Get( "r_shadowSunAngularDiameter", "0.53", CVAR_ARCHIVE, "Apparent sun diameter in degrees" );
	ri.Cvar_CheckRange( r_shadowSunAngularDiameter, 0.0f, 8.0f, qfalse );
	r_shadowPCSSMaxPenumbra = ri.Cvar_Get( "r_shadowPCSSMaxPenumbra", "32", CVAR_ARCHIVE, "Maximum PCSS penumbra radius in world units" );
	ri.Cvar_CheckRange( r_shadowPCSSMaxPenumbra, 0.0f, 256.0f, qfalse );
	r_shadowDebug = ri.Cvar_Get( "r_shadowDebug", "0", CVAR_CHEAT, "Sun shadow debug: 1 cascades, 2 depth, 3 PCF, 4 blockers, 5 penumbra, 6 PCSS, 7 contact, 8 final, 9 bias, 10 point shadows, 11 G2 receivers" );
	ri.Cvar_CheckRange( r_shadowDebug, 0, 11, qtrue );
	r_shadowCasterLod = ri.Cvar_Get( "r_shadowCasterLod", "0", CVAR_ARCHIVE, "Ghoul2 models in sun cascades and dlight shadow cubes use the LOD of the camera view (0 = LOD of the shadow view)" );
	ri.Cvar_CheckRange( r_shadowCasterLod, 0, 1, qtrue );
	r_shadowCasterStats = ri.Cvar_Get( "r_shadowCasterStats", "0", CVAR_CHEAT, "Print Ghoul2 shadow caster counts and LODs per view type once per second" );
	r_dlightShadowBias = ri.Cvar_Get( "r_dlightShadowBias", "0", CVAR_ARCHIVE, "Dynamic light shadow cube bias: 0 legacy slope offset, 1 texel scaled normal + clamped slope bias" );
	ri.Cvar_CheckRange( r_dlightShadowBias, 0, 1, qtrue );
	r_contactShadowSoft = ri.Cvar_Get( "r_contactShadowSoft", "0", CVAR_ARCHIVE, "Contact shadows: 0 first hit, 1 soft depth weighted hits with steps packed near the receiver" );
	ri.Cvar_CheckRange( r_contactShadowSoft, 0, 1, qtrue );
	r_ignoreDstAlpha = ri.Cvar_Get( "r_ignoreDstAlpha", "1", CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_refractionChromaticAberration = ri.Cvar_Get( "r_refractionChromaticAberration", "0.05", CVAR_ARCHIVE, "" );
	ri.Cvar_CheckRange(r_refractionChromaticAberration, 0.f, 0.3f, qfalse);

	//
	// temporary latched variables that can only change over a restart
	//
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", CVAR_LATCH|CVAR_CHEAT, "" );
	r_mapOverBrightBits = ri.Cvar_Get ("r_mapOverBrightBits", "0", CVAR_LATCH, "" );
	r_intensity = ri.Cvar_Get ("r_intensity", "1", CVAR_LATCH, "" );
	r_singleShader = ri.Cvar_Get ("r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH, "" );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_ARCHIVE|CVAR_CHEAT, "" );
	r_lodbias = ri.Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE, "" );
	r_flares = ri.Cvar_Get ("r_flares", "0", CVAR_ARCHIVE, "" );
	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_CHEAT, "" );
	ri.Cvar_CheckRange( r_znear, 0.001f, 200, qfalse );
	r_autolodscalevalue	= ri.Cvar_Get( "r_autolodscalevalue", "0", CVAR_ROM, "" );
	r_zproj = ri.Cvar_Get( "r_zproj", "64", CVAR_ARCHIVE, "" );
	r_stereoSeparation = ri.Cvar_Get( "r_stereoSeparation", "64", CVAR_ARCHIVE, "" );
	r_ignoreGLErrors = ri.Cvar_Get( "r_ignoreGLErrors", "1", CVAR_ARCHIVE, "" );
	r_fastsky = ri.Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE, "" );
	r_inGameVideo = ri.Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE, "" );
	r_drawSun = ri.Cvar_Get( "r_drawSun", "0", CVAR_ARCHIVE, "" );
	r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE, "" );
	r_finish = ri.Cvar_Get ("r_finish", "0", CVAR_ARCHIVE, "");
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE, "" );
	r_markcount = ri.Cvar_Get( "r_markcount", "100", CVAR_ARCHIVE, "" );
	r_gamma = ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE, "" );
	r_facePlaneCull = ri.Cvar_Get ("r_facePlaneCull", "1", CVAR_ARCHIVE, "" );

	r_parallaxMapping = ri.Cvar_Get("r_parallaxMapping", "0", CVAR_ARCHIVE, "Disable/enable parallax mapping");
	r_pomSelfShadow = ri.Cvar_Get( "r_pomSelfShadow", "0", CVAR_ARCHIVE | CVAR_LATCH, "POM self shadowing of direct light (sun, dynamic lights), needs r_parallaxMapping 1; vid_restart to compile it in" );
	ri.Cvar_CheckRange( r_pomSelfShadow, 0, 1, qtrue );
	r_pomSelfShadowLightMode = ri.Cvar_Get( "r_pomSelfShadowLightMode", "1", CVAR_ARCHIVE, "POM self shadowed dynamic lights: 0 sun only, 1 sun + strongest local light, 2 sun + strongest r_pomSelfShadowMaxLocalLights, 3 all lights" );
	ri.Cvar_CheckRange( r_pomSelfShadowLightMode, 0, 3, qtrue );
	r_pomSelfShadowMaxLocalLights = ri.Cvar_Get( "r_pomSelfShadowMaxLocalLights", "2", CVAR_ARCHIVE, "Local lights with POM self shadow per pixel for r_pomSelfShadowLightMode 2" );
	ri.Cvar_CheckRange( r_pomSelfShadowMaxLocalLights, 1, 4, qtrue );
	r_pomSelfShadowSteps = ri.Cvar_Get( "r_pomSelfShadowSteps", "12", CVAR_ARCHIVE, "Height samples of a POM self shadow ray" );
	ri.Cvar_CheckRange( r_pomSelfShadowSteps, 4, 32, qtrue );
	r_pomSelfShadowStrength = ri.Cvar_Get( "r_pomSelfShadowStrength", "1", CVAR_ARCHIVE, "POM self shadow strength, 0 none .. 1 physical (times the pomSelfShadow material keyword)" );
	ri.Cvar_CheckRange( r_pomSelfShadowStrength, 0, 1, qfalse );
	r_pomSelfShadowBias = ri.Cvar_Get( "r_pomSelfShadowBias", "0.02", CVAR_ARCHIVE, "Start of the POM self shadow ray above the hit, in height field depth (0..1)" );
	ri.Cvar_CheckRange( r_pomSelfShadowBias, 0, 0.25, qfalse );
	r_pomSelfShadowSoftness = ri.Cvar_Get( "r_pomSelfShadowSoftness", "4", CVAR_ARCHIVE, "POM self shadow penumbra: occluder penetration to full shadow, higher = harder" );
	ri.Cvar_CheckRange( r_pomSelfShadowSoftness, 0.5, 64, qfalse );
	r_pomAdaptiveSteps = ri.Cvar_Get( "r_pomAdaptiveSteps", "0", CVAR_ARCHIVE, "POM view ray steps by view angle (r_pomMinSteps frontal .. r_pomMaxSteps grazing), 0 = legacy 16 + 8" );
	ri.Cvar_CheckRange( r_pomAdaptiveSteps, 0, 1, qtrue );
	r_pomMinSteps = ri.Cvar_Get( "r_pomMinSteps", "8", CVAR_ARCHIVE, "POM linear view ray steps at normal incidence (r_pomAdaptiveSteps 1)" );
	ri.Cvar_CheckRange( r_pomMinSteps, 4, 64, qtrue );
	r_pomMaxSteps = ri.Cvar_Get( "r_pomMaxSteps", "32", CVAR_ARCHIVE, "POM linear view ray steps at grazing angles (r_pomAdaptiveSteps 1)" );
	ri.Cvar_CheckRange( r_pomMaxSteps, 4, 64, qtrue );
	r_pomBinarySteps = ri.Cvar_Get( "r_pomBinarySteps", "8", CVAR_ARCHIVE, "POM binary refinement steps (r_pomAdaptiveSteps 1)" );
	ri.Cvar_CheckRange( r_pomBinarySteps, 0, 16, qtrue );
	r_pomFadeStart = ri.Cvar_Get( "r_pomFadeStart", "0", CVAR_ARCHIVE, "Distance where POM starts to fade to normal mapping (with r_pomFadeEnd > r_pomFadeStart, 0 = off)" );
	ri.Cvar_CheckRange( r_pomFadeStart, 0, 65536, qfalse );
	r_pomFadeEnd = ri.Cvar_Get( "r_pomFadeEnd", "0", CVAR_ARCHIVE, "Distance beyond which POM is skipped (normal mapping only), 0 = no fade" );
	ri.Cvar_CheckRange( r_pomFadeEnd, 0, 65536, qfalse );
	r_pomDebug = ri.Cvar_Get( "r_pomDebug", "0", CVAR_CHEAT | CVAR_LATCH, "POM debug view: 1 raw depth, 2 displaced UV, 3 view ray depth, 4 sun self shadow, 5 local light self shadow, 6 view steps, 7 shadow steps, 8 parallax fade (vid_restart)" );
	ri.Cvar_CheckRange( r_pomDebug, 0, 8, qtrue );
	r_pomDebugFreezeLight = ri.Cvar_Get( "r_pomDebugFreezeLight", "0", CVAR_CHEAT, "Freeze the sun direction used by POM self shadowing" );
	ri.Cvar_CheckRange( r_pomDebugFreezeLight, 0, 1, qtrue );
	r_pomSilhouette = ri.Cvar_Get( "r_pomSilhouette", "0", CVAR_ARCHIVE | CVAR_LATCH, "Silhouette parallax occlusion mapping for materials with the pomSilhouette keyword (needs r_parallaxMapping 1)" );
	ri.Cvar_CheckRange( r_pomSilhouette, 0, 1, qtrue );
	r_pomSilhouetteDistance = ri.Cvar_Get( "r_pomSilhouetteDistance", "512", CVAR_ARCHIVE, "Silhouette POM: view distance where the shell ends and ordinary POM takes over (0 = ordinary POM everywhere)" );
	ri.Cvar_CheckRange( r_pomSilhouetteDistance, 0.0f, 8192.0f, qfalse );
	r_pomSilhouetteFade = ri.Cvar_Get( "r_pomSilhouetteFade", "96", CVAR_ARCHIVE, "Silhouette POM: width of the dithered crossfade band to ordinary POM, world units (0 = hard switch)" );
	ri.Cvar_CheckRange( r_pomSilhouetteFade, 0.0f, 1024.0f, qfalse );
	r_pomSilhouetteSteps = ri.Cvar_Get( "r_pomSilhouetteSteps", "12", CVAR_ARCHIVE, "Silhouette POM: linear ray steps when looking along the surface normal" );
	ri.Cvar_CheckRange( r_pomSilhouetteSteps, 4, 64, qtrue );
	r_pomSilhouetteMaxSteps = ri.Cvar_Get( "r_pomSilhouetteMaxSteps", "48", CVAR_ARCHIVE, "Silhouette POM: linear ray steps at grazing angles" );
	ri.Cvar_CheckRange( r_pomSilhouetteMaxSteps, 4, 128, qtrue );
	r_pomSilhouetteBinarySteps = ri.Cvar_Get( "r_pomSilhouetteBinarySteps", "6", CVAR_ARCHIVE, "Silhouette POM: binary refinement steps after the linear search" );
	ri.Cvar_CheckRange( r_pomSilhouetteBinarySteps, 0, 16, qtrue );
	r_pomSilhouetteViewDependence = ri.Cvar_Get( "r_pomSilhouetteViewDependence", "1", CVAR_ARCHIVE, "Silhouette POM: how fast the step count grows towards grazing angles (0 = always r_pomSilhouetteSteps)" );
	ri.Cvar_CheckRange( r_pomSilhouetteViewDependence, 0.0f, 4.0f, qfalse );
	r_pomSilhouetteShadows = ri.Cvar_Get( "r_pomSilhouetteShadows", "1", CVAR_ARCHIVE, "Silhouette POM: shells cast the displaced surface into the sun shadow cascades" );
	ri.Cvar_CheckRange( r_pomSilhouetteShadows, 0, 1, qtrue );
	r_pomSilhouetteContactShadows = ri.Cvar_Get( "r_pomSilhouetteContactShadows", "0", CVAR_ARCHIVE, "Silhouette POM: apply screen-space sun contact shadows to shell pixels (they march the displaced depth and turn the grooves hard black)" );
	ri.Cvar_CheckRange( r_pomSilhouetteContactShadows, 0, 1, qtrue );
	r_pomSilhouetteDebug = ri.Cvar_Get( "r_pomSilhouetteDebug", "0", CVAR_CHEAT, "Silhouette POM debug view: 1 translucent shell, 2 shell wireframe, 3 original mesh wireframe, 4 top cap / walls, 5 boundary walls, 6 discarded shell pixels, 7 virtual hit depth, 8 ray steps, 9 split ordinary POM | silhouette POM, 10 linear depth, 11 material normal" );
	ri.Cvar_CheckRange( r_pomSilhouetteDebug, 0, 11, qtrue );
	r_autoPOMSilhouetteMode = ri.Cvar_Get( "r_autoPOMSilhouetteMode", "0", CVAR_ARCHIVE, "Silhouette POM for every material with an ordinary POM height map (set with the r_autoPOMSilhouette command, needs r_pomSilhouette 1)" );
	ri.Cvar_CheckRange( r_autoPOMSilhouetteMode, 0, 1, qtrue );

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.6", CVAR_CHEAT, "" );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT, "" );

	r_anaglyphMode = ri.Cvar_Get("r_anaglyphMode", "0", CVAR_ARCHIVE, "");
	r_mergeMultidraws = ri.Cvar_Get("r_mergeMultidraws", "1", CVAR_ARCHIVE, "");
	r_mergeLeafSurfaces = ri.Cvar_Get("r_mergeLeafSurfaces", "1", CVAR_ARCHIVE, "");

	//
	// temporary variables that can change at any time
	//
	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_TEMP, "" );

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP, "" );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT, "" );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0, "" );
	r_saveFontData = ri.Cvar_Get( "r_saveFontData", "0", 0, "" );

	r_forceParallaxBias = ri.Cvar_Get("r_forceParallaxBias", "0", CVAR_TEMP, "");
	ri.Cvar_CheckRange(r_forceParallaxBias, 0.0f, 1.0f, qfalse);

	r_nocurves = ri.Cvar_Get ("r_nocurves", "0", CVAR_CHEAT, "" );
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", CVAR_CHEAT, "" );
	r_drawfog = ri.Cvar_Get("r_drawfog", "2", CVAR_CHEAT, "");
	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", 0, "" );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_CHEAT, "" );

	r_skipBackEnd = ri.Cvar_Get ("r_skipBackEnd", "0", CVAR_CHEAT, "");

	r_measureOverdraw = ri.Cvar_Get( "r_measureOverdraw", "0", CVAR_CHEAT, "" );
	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", CVAR_CHEAT, "" );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", CVAR_CHEAT, "");
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT, "" );
	r_ignore = ri.Cvar_Get( "r_ignore", "1", CVAR_CHEAT, "" );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT, "");
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT, "");
	r_showcluster = ri.Cvar_Get ("r_showcluster", "0", CVAR_CHEAT, "");
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_CHEAT, "");
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT, "" );
	r_logFile = ri.Cvar_Get( "r_logFile", "0", CVAR_CHEAT, "" );
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_CHEAT, "");
	r_nobind = ri.Cvar_Get ("r_nobind", "0", CVAR_CHEAT, "");
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_CHEAT, "");
	r_showsky = ri.Cvar_Get ("r_showsky", "0", CVAR_CHEAT, "");
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_CHEAT, "");
	r_clear = ri.Cvar_Get ("r_clear", "0", CVAR_CHEAT, "");
	r_offsetFactor = ri.Cvar_Get( "r_offsetfactor", "-1", CVAR_CHEAT, "" );
	r_offsetUnits = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT, "" );

	r_shadowOffsetFactor = ri.Cvar_Get("r_shadowOffsetFactor", "1.0", CVAR_CHEAT, "");
	r_shadowOffsetUnits = ri.Cvar_Get("r_shadowOffsetUnits", "1.0", CVAR_CHEAT, "");

	r_drawBuffer = ri.Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT, "" );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT, "");
	r_noportals = ri.Cvar_Get ("r_noportals", "0", CVAR_CHEAT, "");
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0, "" );

	r_marksOnTriangleMeshes = ri.Cvar_Get("r_marksOnTriangleMeshes", "0", CVAR_ARCHIVE, "");

	r_aviMotionJpegQuality = ri.Cvar_Get("r_aviMotionJpegQuality", "90", CVAR_ARCHIVE, "");
	r_screenshotJpegQuality = ri.Cvar_Get("r_screenshotJpegQuality", "90", CVAR_ARCHIVE, "");
	r_surfaceSprites = ri.Cvar_Get("r_surfaceSprites", "1", CVAR_ARCHIVE, "");

	r_aspectCorrectFonts = ri.Cvar_Get( "r_aspectCorrectFonts", "0", CVAR_ARCHIVE, "" );
	r_maxpolys = ri.Cvar_Get( "r_maxpolys", XSTRING( DEFAULT_MAX_POLYS ), 0, "");
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", XSTRING( DEFAULT_MAX_POLYVERTS ), 0, "" );

/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
	r_noPrecacheGLA						= ri.Cvar_Get( "r_noPrecacheGLA",					"0",						CVAR_CHEAT, "" );
#endif
	r_noServerGhoul2					= ri.Cvar_Get( "r_noserverghoul2",					"0",						CVAR_CHEAT, "" );
	r_Ghoul2AnimSmooth					= ri.Cvar_Get( "r_ghoul2animsmooth",				"0.3",						CVAR_NONE, "" );
	r_Ghoul2UnSqashAfterSmooth			= ri.Cvar_Get( "r_ghoul2unsqashaftersmooth",		"1",						CVAR_NONE, "" );
	broadsword							= ri.Cvar_Get( "broadsword",						"0",						CVAR_ARCHIVE, "" );
	broadsword_kickbones				= ri.Cvar_Get( "broadsword_kickbones",				"1",						CVAR_NONE, "" );
	broadsword_kickorigin				= ri.Cvar_Get( "broadsword_kickorigin",			"1",						CVAR_NONE, "" );
	broadsword_dontstopanim				= ri.Cvar_Get( "broadsword_dontstopanim",			"0",						CVAR_NONE, "" );
	broadsword_waitforshot				= ri.Cvar_Get( "broadsword_waitforshot",			"0",						CVAR_NONE, "" );
	broadsword_playflop					= ri.Cvar_Get( "broadsword_playflop",				"1",						CVAR_NONE, "" );
	broadsword_smallbbox				= ri.Cvar_Get( "broadsword_smallbbox",				"0",						CVAR_NONE, "" );
	broadsword_extra1					= ri.Cvar_Get( "broadsword_extra1",				"0",						CVAR_NONE, "" );
	broadsword_extra2					= ri.Cvar_Get( "broadsword_extra2",				"0",						CVAR_NONE, "" );
	broadsword_effcorr					= ri.Cvar_Get( "broadsword_effcorr",				"1",						CVAR_NONE, "" );
	broadsword_ragtobase				= ri.Cvar_Get( "broadsword_ragtobase",				"2",						CVAR_NONE, "" );
	broadsword_dircap					= ri.Cvar_Get( "broadsword_dircap",				"64",						CVAR_NONE, "" );
/*
Ghoul2 Insert End
*/

	r_patchStitching = ri.Cvar_Get("r_patchStitching", "1", CVAR_ARCHIVE, "Enable stitching of neighbouring patch surfaces" );

	se_language = ri.Cvar_Get ( "se_language", "english", CVAR_ARCHIVE | CVAR_NORESTART, "" );

	for ( size_t i = 0; i < numCommands; i++ )
		ri.Cmd_AddCommand( commands[i].cmd, commands[i].func, "" );
}

void R_InitQueries(void)
{
	if (r_drawSunRays->integer)
		qglGenQueries(ARRAY_LEN(tr.sunFlareQuery), tr.sunFlareQuery);
}

void R_ShutDownQueries(void)
{
	if (r_drawSunRays->integer)
		qglDeleteQueries(ARRAY_LEN(tr.sunFlareQuery), tr.sunFlareQuery);
}

void RE_SetLightStyle (int style, int color);

#ifdef _G2_GORE
static void R_InitGoreVertexData(gpuFrame_t* currentFrame)
{
	static int numGoreArrays = 0;
	currentFrame->goreVBO = R_CreateVBO(
		nullptr,
		sizeof(g2GoreVert_t) * (MAX_GORE_RECORDS + 1) * MAX_GORE_VERTS,
		VBO_USAGE_DYNAMIC, va("Gore_%i", numGoreArrays));

	currentFrame->goreVBO->offsets[ATTR_INDEX_POSITION] = offsetof(g2GoreVert_t, position);
	currentFrame->goreVBO->offsets[ATTR_INDEX_NORMAL] = offsetof(g2GoreVert_t, normal);
	currentFrame->goreVBO->offsets[ATTR_INDEX_TEXCOORD0] = offsetof(g2GoreVert_t, texCoords);
	currentFrame->goreVBO->offsets[ATTR_INDEX_BONE_INDEXES] = offsetof(g2GoreVert_t, bonerefs);
	currentFrame->goreVBO->offsets[ATTR_INDEX_BONE_WEIGHTS] = offsetof(g2GoreVert_t, weights);
	currentFrame->goreVBO->offsets[ATTR_INDEX_TANGENT] = offsetof(g2GoreVert_t, tangents);

	currentFrame->goreVBO->strides[ATTR_INDEX_POSITION] = sizeof(g2GoreVert_t);
	currentFrame->goreVBO->strides[ATTR_INDEX_NORMAL] = sizeof(g2GoreVert_t);
	currentFrame->goreVBO->strides[ATTR_INDEX_TEXCOORD0] = sizeof(g2GoreVert_t);
	currentFrame->goreVBO->strides[ATTR_INDEX_BONE_INDEXES] = sizeof(g2GoreVert_t);
	currentFrame->goreVBO->strides[ATTR_INDEX_BONE_WEIGHTS] = sizeof(g2GoreVert_t);
	currentFrame->goreVBO->strides[ATTR_INDEX_TANGENT] = sizeof(g2GoreVert_t);

	currentFrame->goreVBO->sizes[ATTR_INDEX_POSITION] = sizeof(vec3_t);
	currentFrame->goreVBO->sizes[ATTR_INDEX_NORMAL] = sizeof(uint32_t);
	currentFrame->goreVBO->sizes[ATTR_INDEX_TEXCOORD0] = sizeof(vec2_t);
	currentFrame->goreVBO->sizes[ATTR_INDEX_BONE_WEIGHTS] = sizeof(byte);
	currentFrame->goreVBO->sizes[ATTR_INDEX_BONE_INDEXES] = sizeof(byte);
	currentFrame->goreVBO->sizes[ATTR_INDEX_TANGENT] = sizeof(uint32_t);

	currentFrame->goreIBO = R_CreateIBO(
		nullptr,
		sizeof(glIndex_t) * (MAX_GORE_RECORDS + 1) * MAX_GORE_INDECIES,
		VBO_USAGE_DYNAMIC, va("Gore_%i", numGoreArrays));

	if ( glRefConfig.immutableBuffers )
	{
		const GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

		R_BindVBO(currentFrame->goreVBO);
		currentFrame->goreVBOMemory = qglMapBufferRange(GL_ARRAY_BUFFER, 0,
			currentFrame->goreVBO->vertexesSize, mapFlags);

		R_BindIBO(currentFrame->goreIBO);
		currentFrame->goreIBOMemory = qglMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0,
			currentFrame->goreIBO->indexesSize, mapFlags);
	}
	else
	{
		currentFrame->goreVBOMemory = nullptr;
		currentFrame->goreIBOMemory = nullptr;
	}

	numGoreArrays++;
	GL_CheckErrors();
}
#endif

static void R_InitBackEndFrameData()
{
	GLuint timerQueries[MAX_GPU_TIMERS*MAX_FRAMES];
	qglGenQueries(MAX_GPU_TIMERS*MAX_FRAMES, timerQueries);

	// For temporal data we need ubo buffers between frames for 
	// reading last frame data without fear of writing next frames data into them
	bool reserveTemporalUbo = (r_smaa->integer == 2
		// || r_smaa->integer == 4
		// || r_taa->integer
		|| (r_ssr->integer && r_ssrTemporal->integer)
		|| r_ssgi->integer
		|| r_motionBlur->integer
		);

	if (reserveTemporalUbo)
		backEndData->numFrameUbos = (MAX_FRAMES + 1) * MAX_SCENES;
	else
		backEndData->numFrameUbos = MAX_FRAMES * MAX_SCENES;

	backEndData->frameUbos = (uint32_t*)Z_Malloc(backEndData->numFrameUbos * sizeof(*backEndData->frameUbos), TAG_GENERAL);
	backEndData->cachePreviousFrameUbos = reserveTemporalUbo;

	qglGenBuffers(backEndData->numFrameUbos, backEndData->frameUbos);

	for ( int i = 0; i < MAX_FRAMES; i++ )
	{
		gpuFrame_t *frame = backEndData->frames + i;
		const GLbitfield mapBits = GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT | GL_MAP_PERSISTENT_BIT;

		for (byte j = 0; j < MAX_SCENES; j++)
		{
			size_t BUFFER_SIZE = j == 0 ? FRAME_UNIFORM_BUFFER_SIZE : FRAME_SCENE_UNIFORM_BUFFER_SIZE;
			frame->ubo[j] = backEndData->frameUbos[i * MAX_SCENES + j];
			frame->uboWriteOffset[j] = 0;
			frame->uboSize[j] = BUFFER_SIZE;
			qglBindBuffer(GL_UNIFORM_BUFFER, frame->ubo[j]);
			glState.currentGlobalUBO = frame->ubo[j];

			if (glRefConfig.annotateResources) qglObjectLabel(GL_BUFFER, frame->ubo[j], -1, va("FrameUBO_%i_%i", i, j));

			// TODO: persistently mapped UBOs
			qglBufferData(GL_UNIFORM_BUFFER, BUFFER_SIZE,
				nullptr, GL_DYNAMIC_DRAW);
		}

		frame->dynamicVbo = R_CreateVBO(nullptr, FRAME_VERTEX_BUFFER_SIZE,
				VBO_USAGE_DYNAMIC, va("Frame_%i", i));
		frame->dynamicVboCommitOffset = 0;
		frame->dynamicVboWriteOffset = 0;

		frame->dynamicIbo = R_CreateIBO(nullptr, FRAME_INDEX_BUFFER_SIZE,
				VBO_USAGE_DYNAMIC, va("Frame_%i", i));
		frame->dynamicIboCommitOffset = 0;
		frame->dynamicIboWriteOffset = 0;

		if ( glRefConfig.immutableBuffers )
		{
			R_BindVBO(frame->dynamicVbo);
			frame->dynamicVboMemory = qglMapBufferRange(GL_ARRAY_BUFFER, 0,
				frame->dynamicVbo->vertexesSize, mapBits);

			R_BindIBO(frame->dynamicIbo);
			frame->dynamicIboMemory = qglMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0,
				frame->dynamicIbo->indexesSize, mapBits);
		}
		else
		{
			frame->dynamicVboMemory = nullptr;
			frame->dynamicIboMemory = nullptr;
		}

		for ( int j = 0; j < MAX_GPU_TIMERS; j++ )
		{
			gpuTimer_t *timer = frame->timers + j;
			timer->queryName = timerQueries[i*MAX_GPU_TIMERS + j];
		}
#ifdef _G2_GORE
		R_InitGoreVertexData(frame);
#endif
	}

	if (reserveTemporalUbo)
	{
		// Allocate the spare ubo for last frame infos
		gpuFrame_t *frame = &backEndData->frames[0];
		for (byte j = 0; j < MAX_SCENES; j++)
		{
			size_t BUFFER_SIZE = j == 0 ? FRAME_UNIFORM_BUFFER_SIZE : FRAME_SCENE_UNIFORM_BUFFER_SIZE;
			frame->ubo[j] = backEndData->frameUbos[MAX_FRAMES * MAX_SCENES + j];
			frame->uboWriteOffset[j] = 0;
			frame->uboSize[j] = BUFFER_SIZE;
			qglBindBuffer(GL_UNIFORM_BUFFER, frame->ubo[j]);
			glState.currentGlobalUBO = frame->ubo[j];

			if (glRefConfig.annotateResources) qglObjectLabel(GL_BUFFER, frame->ubo[j], -1, va("FrameUBO_spare_%i", j));

			// TODO: persistently mapped UBOs
			qglBufferData(GL_UNIFORM_BUFFER, BUFFER_SIZE,
				nullptr, GL_DYNAMIC_DRAW);
		}
	}

	backEndData->currentFrame = backEndData->frames;
}

static void R_InitStaticConstants()
{
	const int alignment = glRefConfig.uniformBufferOffsetAlignment - 1;
	size_t alignedBlockSize = 0;

	qglBindBuffer(GL_UNIFORM_BUFFER, tr.staticUbo);
	if (glRefConfig.annotateResources) qglObjectLabel(GL_BUFFER, tr.staticUbo, -1, "StaticUBO");
	qglBufferData(
		GL_UNIFORM_BUFFER,
		STATIC_UNIFORM_BUFFER_SIZE,
		nullptr,
		GL_STATIC_DRAW);

	// Setup static 2d camera data
	EntityBlock entity2DBlock = {};
	entity2DBlock.fxVolumetricBase = -1.0f;
	Matrix16Identity(entity2DBlock.modelMatrix);
	tr.entity2DUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, 0, sizeof(entity2DBlock), &entity2DBlock);
	alignedBlockSize += (sizeof(EntityBlock) + alignment) & ~alignment;

	// Setup static 2d camera data
	CameraBlock a2DCameraBlock = {};
	Matrix16Ortho(
		0.0f,
		640.0f,
		480.0f,
		0.0f,
		0.0f,
		1.0f,
		a2DCameraBlock.viewProjectionMatrix);

	tr.camera2DUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.camera2DUboOffset, sizeof(a2DCameraBlock), &a2DCameraBlock);
	alignedBlockSize += (sizeof(CameraBlock) + alignment) & ~alignment;

	// Setup static flare entity data
	EntityBlock entityFlareBlock = {};
	entityFlareBlock.fxVolumetricBase = -1.0f;
	Matrix16Identity(entityFlareBlock.modelMatrix);

	tr.entityFlareUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.entityFlareUboOffset, sizeof(entityFlareBlock), &entityFlareBlock);
	alignedBlockSize += (sizeof(EntityBlock) + alignment) & ~alignment;

	// Setup default light block
	LightsBlock lightsBlock = {};
	lightsBlock.numLights = 0;

	tr.defaultLightsUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.defaultLightsUboOffset, sizeof(lightsBlock), &lightsBlock);
	alignedBlockSize += (sizeof(LightsBlock) + alignment) & ~alignment;

	// Setup default scene block
	SceneBlock sceneBlock = {};
	sceneBlock.globalFogIndex = -1;
	sceneBlock.currentTime = 0.1f;
	sceneBlock.frameTime = 0.1f;

	tr.defaultSceneUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.defaultSceneUboOffset, sizeof(sceneBlock), &sceneBlock);
	alignedBlockSize += (sizeof(SceneBlock) + alignment) & ~alignment;

	// Setup default fogs block
	FogsBlock fogsBlock = {};
	fogsBlock.numFogs = 0;
	tr.defaultFogsUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.defaultFogsUboOffset, sizeof(fogsBlock), &fogsBlock);
	alignedBlockSize += (sizeof(FogsBlock) + alignment) & ~alignment;

	// Setup default shader instance block
	ShaderInstanceBlock shaderInstanceBlock = {};
	tr.defaultShaderInstanceUboOffset = alignedBlockSize;
	qglBufferSubData(
		GL_UNIFORM_BUFFER, tr.defaultShaderInstanceUboOffset, sizeof(shaderInstanceBlock), &shaderInstanceBlock);
	alignedBlockSize += (sizeof(ShaderInstanceBlock) + alignment) & ~alignment;

	qglBindBuffer(GL_UNIFORM_BUFFER, 0);
	glState.currentGlobalUBO = -1;

	GL_CheckErrors();
}

static void R_ShutdownBackEndFrameData()
{
	if ( !backEndData )
		return;

	qglDeleteBuffers(backEndData->numFrameUbos, backEndData->frameUbos);
	Z_Free(backEndData->frameUbos);

	for ( int i = 0; i < MAX_FRAMES; i++ )
	{
		gpuFrame_t *frame = backEndData->frames + i;

		if (frame->sync)
		{
			qglDeleteSync(frame->sync);
			frame->sync = NULL;
		}

		if ( glRefConfig.immutableBuffers )
		{
			R_BindVBO(frame->dynamicVbo);
			R_BindIBO(frame->dynamicIbo);
			qglUnmapBuffer(GL_ARRAY_BUFFER);
			qglUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

#ifdef _G2_GORE
			R_BindVBO(frame->goreVBO);
			R_BindIBO(frame->goreIBO);
			qglUnmapBuffer(GL_ARRAY_BUFFER);
			qglUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
#endif
		}

		for ( int j = 0; j < MAX_GPU_TIMERS; j++ )
		{
			gpuTimer_t *timer = frame->timers + j;
			qglDeleteQueries(1, &timer->queryName);
		}
	}
}

static bool r_cacheGPUShaders = false;

void R_ClearTr(void)
{
	if (r_cacheGPUShaders)
	{
		// clear all but GPU shaders in tr
		Com_Memset(&tr, 0, (byte*)&tr.splashScreenShader - (byte*)&tr);
		Com_Memset(&tr.staticUbo, 0, sizeof(tr) - ((byte*)&tr.staticUbo - (byte*)&tr));	
	}
	else
		// clear all of tr
		Com_Memset(&tr, 0, sizeof(tr));
}

static bool r_inited = false;
/*
===============
R_Init
===============
*/
void R_Init( void ) {
	byte *ptr;
	int i;

	if (r_inited)
		return;

	ri.Printf( PRINT_ALL, "----- R_Init -----\n" );

	// clear all our internal state
	R_ClearTr();
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
	Com_Memset( &tess, 0, sizeof( tess ) );


	//
	// init function tables
	//
	for ( i = 0; i < FUNCTABLE_SIZE; i++ )
	{
		tr.sinTable[i]		= sin( DEG2RAD( i * 360.0f / ( ( float ) ( FUNCTABLE_SIZE - 1 ) ) ) );
		tr.squareTable[i]	= ( i < FUNCTABLE_SIZE/2 ) ? 1.0f : -1.0f;
		tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];

		if ( i < FUNCTABLE_SIZE / 2 )
		{
			if ( i < FUNCTABLE_SIZE / 4 )
			{
				tr.triangleTable[i] = ( float ) i / ( FUNCTABLE_SIZE / 4 );
			}
			else
			{
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i-FUNCTABLE_SIZE / 4];
			}
		}
		else
		{
			tr.triangleTable[i] = -tr.triangleTable[i-FUNCTABLE_SIZE/2];
		}
	}

	R_InitFogTable();

	R_ImageLoader_Init();
	R_NoiseInit();
	R_Register();

	max_polys = Q_min( r_maxpolys->integer, DEFAULT_MAX_POLYS );
	max_polyverts = Q_min( r_maxpolyverts->integer, DEFAULT_MAX_POLYVERTS );

	ptr = (byte*)ri.Hunk_Alloc(
		sizeof( *backEndData ) +
		sizeof(srfPoly_t) * max_polys +
		sizeof(polyVert_t) * max_polyverts +
		sizeof(Allocator) +
		PER_FRAME_MEMORY_BYTES,
		h_low);

	backEndData = (backEndData_t *)ptr;
	ptr = (byte *)(backEndData + 1);

	backEndData->polys = (srfPoly_t *)ptr;
	ptr += sizeof(*backEndData->polys) * max_polys;

	backEndData->polyVerts = (polyVert_t *)ptr;
	ptr += sizeof(*backEndData->polyVerts) * max_polyverts;

	backEndData->perFrameMemory = new(ptr) Allocator(ptr + sizeof(*backEndData->perFrameMemory), PER_FRAME_MEMORY_BYTES);

	R_InitNextFrame();

	for ( int i = 0; i < MAX_LIGHT_STYLES; i++ )
	{
		RE_SetLightStyle (i, -1);
	}

	R_InitImagesPool();

	InitOpenGL();

	R_InitGPUBuffers();

	R_InitStaticConstants();
	R_InitBackEndFrameData();
	R_InitImages();

	FBO_Init();

	if (!r_cacheGPUShaders)
		GLSL_LoadGPUShaders();
	r_cacheGPUShaders = false;

	R_InitShaders (qfalse);

	R_InitSkins();

	R_InitFonts();

	R_ModelInit();

	R_InitDecals();

	R_InitQueries();

	R_InitWeatherSystem();

#if defined(_DEBUG)
	GLenum err = qglGetError();
	if ( err != GL_NO_ERROR )
		ri.Printf( PRINT_ALL, "glGetError() = 0x%x\n", err );
#endif

	RestoreGhoul2InfoArray();

	// print info
	GfxInfo_f();
	r_inited = true;
	ri.Printf( PRINT_ALL, "----- finished R_Init -----\n" );
}

/*
===============
RE_Shutdown
===============
*/
void RE_Shutdown( qboolean destroyWindow, qboolean restarting ) {

	ri.Printf( PRINT_ALL, "RE_Shutdown( %i )\n", destroyWindow );

	for ( size_t i = 0; i < numCommands; i++ )
		ri.Cmd_RemoveCommand( commands[i].cmd );

	// Flush here to make sure all the fences are processed
	qglFlush();

	R_IssuePendingRenderCommands();

	R_ShutdownBackEndFrameData();

	R_ShutdownWeatherSystem();

	R_ShutdownFonts();
	
	if (r_inited)
	{
		R_ShutDownQueries();
		FBO_Shutdown();
		R_DeleteTextures();
		R_LightCookiesShutdown();
		R_DestroyGPUBuffers();
		R_ShutdownForwardPlus();
		R_ClearAreaLights();
		R_FoliageInteractionReset();
		R_ShutdownPomSilhouette();
		R_ShutdownVolumetric();

		if (!destroyWindow && !restarting)
		{
			r_cacheGPUShaders = true;
			glState.currentProgram = 0;
			qglUseProgram(0);
		}
		else
			GLSL_ShutdownGPUShaders();
	}

	if (destroyWindow && restarting && tr.registered)
	{
		ri.Z_Free((void *)glConfig.extensions_string);
		ri.Z_Free((void *)glConfigExt.originalExtensionString);

		qglDeleteVertexArrays(1, &tr.globalVao);
		SaveGhoul2InfoArray();
	}

	// shut down platform specific OpenGL stuff
	if ( destroyWindow ) {
		ri.WIN_Shutdown();
	}

	tr.registered = qfalse;
	r_inited = false;
	backEndData = NULL;
}

/*
=============
RE_EndRegistration

Touch all images to make sure they are resident
=============
*/
void RE_EndRegistration( void ) {
	R_IssuePendingRenderCommands();
	if (!ri.Sys_LowPhysicalMemory()) {
		RB_ShowImages();
	}
}

// HACK
extern qboolean gG2_GBMNoReconstruct;
extern qboolean gG2_GBMUseSPMethod;
static void G2API_BoltMatrixReconstruction( qboolean reconstruct ) { gG2_GBMNoReconstruct = (qboolean)!reconstruct; }
static void G2API_BoltMatrixSPMethod( qboolean spMethod ) { gG2_GBMUseSPMethod = spMethod; }

static float GetDistanceCull( void ) { return tr.distanceCull; }

extern void R_SVModelInit( void ); //tr_model.cpp

static void GetRealRes( int *w, int *h ) {
	*w = glConfig.vidWidth;
	*h = glConfig.vidHeight;
}

// STUBS, REPLACEME
qboolean stub_InitializeWireframeAutomap() { return qtrue; }

void RE_GetLightStyle(int style, color4ub_t color)
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_GetLightStyle: %d is out of range", style );
		return;
	}

	byteAlias_t *baDest = (byteAlias_t *)&color, *baSource = (byteAlias_t *)&styleColors[style];
	baDest->i = baSource->i;
}

void RE_SetLightStyle(int style, int color)
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_SetLightStyle: %d is out of range", style );
		return;
	}

	byteAlias_t *ba = (byteAlias_t *)&styleColors[style];
	if ( ba->i != color) {
		ba->i = color;
	}
}

void RE_GetBModelVerts(int bmodelIndex, vec3_t *verts, vec3_t normal);
void RE_WorldEffectCommand(const char *cmd);

void stub_RE_AddWeatherZone ( vec3_t mins, vec3_t maxs ) {} // Intentionally left blank. Rend2 reads the zones manually on bsp load
static void RE_SetRefractionProperties ( float distortionAlpha, float distortionStretch, qboolean distortionPrePost, qboolean distortionNegate ) { }

void C_LevelLoadBegin(const char *psMapName, ForceReload_e eForceReload)
{
	static char sPrevMapName[MAX_QPATH]={0};
	bool bDeleteModels = eForceReload == eForceReload_MODELS || eForceReload == eForceReload_ALL;

	if( bDeleteModels )
		CModelCache->DeleteAll();
	else if( ri.Cvar_VariableIntegerValue( "sv_pure" ) )
		CModelCache->DumpNonPure();

	tr.numBSPModels = 0;

	/* If we're switching to the same level, don't increment current level */
	if (Q_stricmp( psMapName,sPrevMapName ))
	{
		Q_strncpyz( sPrevMapName, psMapName, sizeof(sPrevMapName) );
		tr.currentLevel++;
	}
}

int C_GetLevel( void )
{
	return tr.currentLevel;
}

void C_LevelLoadEnd( void )
{
	CModelCache->LevelLoadEnd( qfalse );
	ri.SND_RegisterAudio_LevelLoadEnd( qfalse );
	ri.S_RestartMusic();
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAreaLightAPI

Optional extension (tr_public.h): LTC area lights, tr_arealights.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refAreaLightExport_t* QDECL GetRefAreaLightAPI ( void ) {
	static const refAreaLightExport_t areaLights = { RE_AddAreaLightToScene, RE_AddLineLightToScene };
	return &areaLights;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefFoliageAPI

Optional extension (tr_public.h): foliage interaction, tr_foliageinteract.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refFoliageExport_t* QDECL GetRefFoliageAPI ( void ) {
	static const refFoliageExport_t foliage = { RE_SetFoliageInteractors };
	return &foliage;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefFogVolumeAPI

Optional extension (tr_public.h): local fog volumes, tr_fogvolume.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refFogVolumeExport_t* QDECL GetRefFogVolumeAPI ( void ) {
	static const refFogVolumeExport_t fogVolumes = { RE_AddFogVolumeToScene };
	return &fogVolumes;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefVolParticleAPI

Optional extension (tr_public.h): volumetric FX particles, tr_volparticle.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refVolParticleExport_t* QDECL GetRefVolParticleAPI ( void ) {
	static const refVolParticleExport_t volParticles = { RE_AddVolumetricParticleToScene };
	return &volParticles;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefSpotLightAPI

Optional extension (tr_public.h): spot lights, tr_spotlight.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refSpotLightExport_t* QDECL GetRefSpotLightAPI ( void ) {
	static const refSpotLightExport_t spotLights = { RE_AddSpotLightToScene, RE_RegisterLightCookie };
	return &spotLights;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" {
Q_EXPORT refexport_t* QDECL GetRefAPI ( int apiVersion, refimport_t *rimp ) {
	static refexport_t	re;

	assert( rimp );
	ri = *rimp;

	Com_Memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf(PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n",
			REF_API_VERSION, apiVersion );
		return NULL;
	}

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;

	re.BeginRegistration = RE_BeginRegistration;
	re.RegisterModel = RE_RegisterModel;
	re.RegisterServerModel = RE_RegisterServerModel;
	re.RegisterSkin = RE_RegisterSkin;
	re.RegisterServerSkin = RE_RegisterServerSkin;
	re.RegisterShader = RE_RegisterShader;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.ShaderNameFromIndex = RE_ShaderNameFromIndex;
	re.LoadWorld = RE_LoadWorldMap;
	re.SetWorldVisData = RE_SetWorldVisData;
	re.EndRegistration = RE_EndRegistration;

	re.BeginFrame = RE_BeginFrame;
	re.EndFrame = RE_EndFrame;

	re.MarkFragments = R_MarkFragments;
	re.LerpTag = R_LerpTag;
	re.ModelBounds = R_ModelBounds;

	re.DrawRotatePic = RE_RotatePic;
	re.DrawRotatePic2 = RE_RotatePic2;

	re.ClearScene = RE_ClearScene;
	re.ClearDecals = RE_ClearDecals;
	re.AddRefEntityToScene = RE_AddRefEntityToScene;
	re.AddMiniRefEntityToScene = RE_AddMiniRefEntityToScene;
	re.AddPolyToScene = RE_AddPolyToScene;
	re.AddDecalToScene = RE_AddDecalToScene;
	re.LightForPoint = R_LightForPoint;
	re.AddLightToScene = RE_AddLightToScene;
	re.AddAdditiveLightToScene = RE_AddAdditiveLightToScene;
	re.RenderScene = RE_RenderScene;

	re.SetColor = RE_SetColor;
	re.DrawStretchPic = RE_StretchPic;
	re.DrawStretchRaw = RE_StretchRaw;
	re.UploadCinematic = RE_UploadCinematic;

	re.RegisterFont = RE_RegisterFont;
	re.Font_StrLenPixels = RE_Font_StrLenPixels;
	re.Font_StrLenChars = RE_Font_StrLenChars;
	re.Font_HeightPixels = RE_Font_HeightPixels;
	re.Font_DrawString = RE_Font_DrawString;
	re.Language_IsAsian = Language_IsAsian;
	re.Language_UsesSpaces = Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString = AnyLanguage_ReadCharFromString;
	re.RemapShader = R_RemapShader;
	re.GetEntityToken = R_GetEntityToken;
	re.inPVS = R_inPVS;

	re.GetLightStyle = RE_GetLightStyle;
	re.SetLightStyle = RE_SetLightStyle;
	re.GetBModelVerts = RE_GetBModelVerts;

	re.SetRangedFog = RE_SetRangedFog;
	re.SetRefractionProperties = RE_SetRefractionProperties;
	re.GetDistanceCull = GetDistanceCull;
	re.GetRealRes = GetRealRes;
	// R_AutomapElevationAdjustment
	re.InitializeWireframeAutomap = stub_InitializeWireframeAutomap;
	re.AddWeatherZone = stub_RE_AddWeatherZone;
	re.WorldEffectCommand = RE_WorldEffectCommand;
	re.RegisterMedia_LevelLoadBegin = C_LevelLoadBegin;
	re.RegisterMedia_LevelLoadEnd = C_LevelLoadEnd;
	re.RegisterMedia_GetLevel = C_GetLevel;
	re.RegisterImages_LevelLoadEnd = C_Images_LevelLoadEnd;
	re.RegisterModels_LevelLoadEnd = C_Models_LevelLoadEnd;

	re.TakeVideoFrame = RE_TakeVideoFrame;

	re.InitSkins							= R_InitSkins;
	re.InitShaders							= R_InitShaders;
	re.SVModelInit							= R_SVModelInit;
	re.HunkClearCrap						= RE_HunkClearCrap;

	re.G2API_AddBolt						= G2API_AddBolt;
	re.G2API_AddBoltSurfNum					= G2API_AddBoltSurfNum;
	re.G2API_AddSurface						= G2API_AddSurface;
	re.G2API_AnimateG2ModelsRag				= G2API_AnimateG2ModelsRag;
	re.G2API_AttachEnt						= G2API_AttachEnt;
	re.G2API_AttachG2Model					= G2API_AttachG2Model;
	re.G2API_AttachInstanceToEntNum			= G2API_AttachInstanceToEntNum;
	re.G2API_AbsurdSmoothing				= G2API_AbsurdSmoothing;
	re.G2API_BoltMatrixReconstruction		= G2API_BoltMatrixReconstruction;
	re.G2API_BoltMatrixSPMethod				= G2API_BoltMatrixSPMethod;
	re.G2API_CleanEntAttachments			= G2API_CleanEntAttachments;
	re.G2API_CleanGhoul2Models				= G2API_CleanGhoul2Models;
	re.G2API_ClearAttachedInstance			= G2API_ClearAttachedInstance;
	re.G2API_CollisionDetect				= G2API_CollisionDetect;
	re.G2API_CollisionDetectCache			= G2API_CollisionDetectCache;
	re.G2API_CopyGhoul2Instance				= G2API_CopyGhoul2Instance;
	re.G2API_CopySpecificG2Model			= G2API_CopySpecificG2Model;
	re.G2API_DetachG2Model					= G2API_DetachG2Model;
	re.G2API_DoesBoneExist					= G2API_DoesBoneExist;
	re.G2API_DuplicateGhoul2Instance		= G2API_DuplicateGhoul2Instance;
	re.G2API_FreeSaveBuffer					= G2API_FreeSaveBuffer;
	re.G2API_GetAnimFileName				= G2API_GetAnimFileName;
	re.G2API_GetAnimFileNameIndex			= G2API_GetAnimFileNameIndex;
	re.G2API_GetAnimRange					= G2API_GetAnimRange;
	re.G2API_GetBoltMatrix					= G2API_GetBoltMatrix;
	re.G2API_GetBoneAnim					= G2API_GetBoneAnim;
	re.G2API_GetBoneIndex					= G2API_GetBoneIndex;
	re.G2API_GetGhoul2ModelFlags			= G2API_GetGhoul2ModelFlags;
	re.G2API_GetGLAName						= G2API_GetGLAName;
	re.G2API_GetModelName					= G2API_GetModelName;
	re.G2API_GetParentSurface				= G2API_GetParentSurface;
	re.G2API_GetRagBonePos					= G2API_GetRagBonePos;
	re.G2API_GetSurfaceIndex				= G2API_GetSurfaceIndex;
	re.G2API_GetSurfaceName					= G2API_GetSurfaceName;
	re.G2API_GetSurfaceOnOff				= G2API_GetSurfaceOnOff;
	re.G2API_GetSurfaceRenderStatus			= G2API_GetSurfaceRenderStatus;
	re.G2API_GetTime						= G2API_GetTime;
	re.G2API_Ghoul2Size						= G2API_Ghoul2Size;
	re.G2API_GiveMeVectorFromMatrix			= G2API_GiveMeVectorFromMatrix;
	re.G2API_HasGhoul2ModelOnIndex			= G2API_HasGhoul2ModelOnIndex;
	re.G2API_HaveWeGhoul2Models				= G2API_HaveWeGhoul2Models;
	re.G2API_IKMove							= G2API_IKMove;
	re.G2API_InitGhoul2Model				= G2API_InitGhoul2Model;
	re.G2API_IsGhoul2InfovValid				= G2API_IsGhoul2InfovValid;
	re.G2API_IsPaused						= G2API_IsPaused;
	re.G2API_ListBones						= G2API_ListBones;
	re.G2API_ListSurfaces					= G2API_ListSurfaces;
	re.G2API_LoadGhoul2Models				= G2API_LoadGhoul2Models;
	re.G2API_LoadSaveCodeDestructGhoul2Info	= G2API_LoadSaveCodeDestructGhoul2Info;
	re.G2API_OverrideServerWithClientData	= G2API_OverrideServerWithClientData;
	re.G2API_PauseBoneAnim					= G2API_PauseBoneAnim;
	re.G2API_PrecacheGhoul2Model			= G2API_PrecacheGhoul2Model;
	re.G2API_RagEffectorGoal				= G2API_RagEffectorGoal;
	re.G2API_RagEffectorKick				= G2API_RagEffectorKick;
	re.G2API_RagForceSolve					= G2API_RagForceSolve;
	re.G2API_RagPCJConstraint				= G2API_RagPCJConstraint;
	re.G2API_RagPCJGradientSpeed			= G2API_RagPCJGradientSpeed;
	re.G2API_RemoveBolt						= G2API_RemoveBolt;
	re.G2API_RemoveBone						= G2API_RemoveBone;
	re.G2API_RemoveGhoul2Model				= G2API_RemoveGhoul2Model;
	re.G2API_RemoveGhoul2Models				= G2API_RemoveGhoul2Models;
	re.G2API_RemoveSurface					= G2API_RemoveSurface;
	re.G2API_ResetRagDoll					= G2API_ResetRagDoll;
	re.G2API_SaveGhoul2Models				= G2API_SaveGhoul2Models;
	re.G2API_SetBoltInfo					= G2API_SetBoltInfo;
	re.G2API_SetBoneAngles					= G2API_SetBoneAngles;
	re.G2API_SetBoneAnglesIndex				= G2API_SetBoneAnglesIndex;
	re.G2API_SetBoneAnglesMatrix			= G2API_SetBoneAnglesMatrix;
	re.G2API_SetBoneAnglesMatrixIndex		= G2API_SetBoneAnglesMatrixIndex;
	re.G2API_SetBoneAnim					= G2API_SetBoneAnim;
	re.G2API_SetBoneAnimIndex				= G2API_SetBoneAnimIndex;
	re.G2API_SetBoneIKState					= G2API_SetBoneIKState;
	re.G2API_SetGhoul2ModelIndexes			= G2API_SetGhoul2ModelIndexes;
	re.G2API_SetGhoul2ModelFlags			= G2API_SetGhoul2ModelFlags;
	re.G2API_SetLodBias						= G2API_SetLodBias;
	re.G2API_SetNewOrigin					= G2API_SetNewOrigin;
	re.G2API_SetRagDoll						= G2API_SetRagDoll;
	re.G2API_SetRootSurface					= G2API_SetRootSurface;
	re.G2API_SetShader						= G2API_SetShader;
	re.G2API_SetSkin						= G2API_SetSkin;
	re.G2API_SetSurfaceOnOff				= G2API_SetSurfaceOnOff;
	re.G2API_SetTime						= G2API_SetTime;
	re.G2API_SkinlessModel					= G2API_SkinlessModel;
	re.G2API_StopBoneAngles					= G2API_StopBoneAngles;
	re.G2API_StopBoneAnglesIndex			= G2API_StopBoneAnglesIndex;
	re.G2API_StopBoneAnim					= G2API_StopBoneAnim;
	re.G2API_StopBoneAnimIndex				= G2API_StopBoneAnimIndex;

	#ifdef _G2_GORE
	re.G2API_GetNumGoreMarks				= G2API_GetNumGoreMarks;
	re.G2API_AddSkinGore					= G2API_AddSkinGore;
	re.G2API_ClearSkinGore					= G2API_ClearSkinGore;
	#endif // _SOF2

	/*
	Ghoul2 Insert End
	*/

	re.ext.Font_StrLenPixels = RE_Font_StrLenPixelsNew;

	return &re;
}
}
