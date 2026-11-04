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
#include "tr_lightrecon.h"
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
cvar_t	*r_atmosphere;
cvar_t	*r_atmosphereSky;
cvar_t	*r_atmosphereSkyBlend;
cvar_t	*r_atmosphereUnitScale;
cvar_t	*r_atmosphereAerialScale;
cvar_t	*r_atmosphereAltitude;
cvar_t	*r_atmosphereGroundZ;
cvar_t	*r_atmosphereRayleigh;
cvar_t	*r_atmosphereMie;
cvar_t	*r_atmosphereMieG;
cvar_t	*r_atmosphereOzone;
cvar_t	*r_atmosphereSunColor;
cvar_t	*r_atmosphereSunIntensity;
cvar_t	*r_atmosphereSunSize;
cvar_t	*r_atmosphereSunGlow;
cvar_t	*r_atmosphereStart;
cvar_t	*r_atmosphereDebug;
cvar_t	*r_clouds;
cvar_t	*r_cloudScale;
cvar_t	*r_cloudBase;
cvar_t	*r_cloudTop;
cvar_t	*r_cloudCoverage;
cvar_t	*r_cloudDensity;
cvar_t	*r_cloudAnisotropy;
cvar_t	*r_cloudWindSpeed;
cvar_t	*r_cloudWindDir;
cvar_t	*r_cloudSteps;
cvar_t	*r_cloudStepLength;
cvar_t	*r_cloudShadowSteps;
cvar_t	*r_cloudMSOctaves;
cvar_t	*r_cloudDetail;
cvar_t	*r_cloudAmbient;
cvar_t	*r_cloudTemporal;
cvar_t	*r_cloudMaxDistance;
cvar_t	*r_cloudShapeScale;
cvar_t	*r_cloudDetailScale;
cvar_t	*r_cloudWeatherScale;
cvar_t	*r_cloudLegacy;
cvar_t	*r_cloudSkyPortal;
cvar_t	*r_cloudShadows;
cvar_t	*r_cloudShadowExtent;
cvar_t	*r_cloudShadowInterval;
cvar_t	*r_cloudDebug;
cvar_t	*r_volumetricFogAnisotropy;
cvar_t	*r_volumetricFogTemporal;
cvar_t	*r_volumetricFogHistoryWeight;
cvar_t	*r_volumetricFogSunScale;
cvar_t	*r_volumetricFogDlightScale;
cvar_t	*r_volumetricFogLightTile;
cvar_t	*r_volumetricFogLightCompute;
cvar_t	*r_volumetricFogStaticScale;
#define LIGHTRECON_CONTROL(name, maximum, description) cvar_t *name;
#define LIGHTRECON_CONSUMER(name, kind, description) cvar_t *name;
#include "tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
cvar_t	*r_entityLightProbes;
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
cvar_t	*r_volumetricWater;
cvar_t	*r_volumetricWaterActive;
cvar_t	*r_volumetricWaterSurfaces;
cvar_t	*r_volumetricWaterSunPath;
cvar_t	*r_volumetricWaterCaustics;
cvar_t	*r_volumetricWaterCausticScale;
cvar_t	*r_volumetricWaterCausticSpeed;
cvar_t	*r_volumetricWaterCausticFocus;
cvar_t	*r_waterCausticsMode, *r_waterCausticsQuality, *r_waterCausticsMaxDepth;
cvar_t	*r_waterCausticsSlope, *r_waterCausticsFilter, *r_waterCausticsDebug;
cvar_t	*r_waterCausticsLightMask, *r_waterCausticsLocalMaxLights;
cvar_t	*r_waterCausticsBakedMode, *r_waterCausticsBakedStrength;
cvar_t	*r_volumetricWaterExtinction;
cvar_t	*r_volumetricWaterColor;
cvar_t	*r_volumetricWaterAlbedo;
cvar_t	*r_volumetricWaterAnisotropy;
cvar_t	*r_volumetricSlimeExtinction;
cvar_t	*r_volumetricSlimeColor;
cvar_t	*r_volumetricSlimeAlbedo;
cvar_t	*r_volumetricSlimeAnisotropy;
cvar_t	*r_volumetricLavaExtinction;
cvar_t	*r_volumetricLavaColor;
cvar_t	*r_volumetricLavaAlbedo;
cvar_t	*r_volumetricLavaAnisotropy;
cvar_t	*r_waterSurface;
cvar_t *r_waterfall, *r_waterfallQuality, *r_waterfallGeometry;
cvar_t *r_waterfallRefraction, *r_waterfallWhitewater, *r_waterfallDebug;
cvar_t *r_waterfallSmallStrength, *r_waterfallMediumStrength, *r_waterfallHeavyStrength;
cvar_t *r_waterfallMist, *r_waterfallMistDensity, *r_waterfallMistRadius;
cvar_t *r_waterfallMistBalance, *r_waterfallMistLens, *r_waterfallMistDebug;
cvar_t *r_waterfallImpact, *r_waterfallImpactRadius, *r_waterfallImpactImpulse;
cvar_t *r_waterfallImpactTurbulence, *r_waterfallImpactFoam, *r_waterfallImpactDebug;
cvar_t *r_waterGeometry, *r_waterGeometryEdge, *r_waterGeometryBodyVerts, *r_waterGeometryMapVerts, *r_waterGeometryDebug;
cvar_t *r_waterTessellation, *r_waterTessPixels, *r_waterTessMax, *r_waterTessForce;
cvar_t *r_waterTessMinEdge, *r_waterTessLodDistance;
cvar_t *r_offscreenPresent;
cvar_t *r_waterShoreline, *r_waterShoreAttenuation, *r_waterShoreContactSoftness;
cvar_t *r_waterShoreWetStrength, *r_waterShoreWetPersistence, *r_waterShoreFoam, *r_waterShoreDebug;
cvar_t	*r_waterSurfaceIOR;
cvar_t	*r_waterSurfaceRoughness;
cvar_t	*r_waterSurfaceNormal;
cvar_t *r_waterWaves, *r_waterWaveAmplitude, *r_waterWaveLength, *r_waterWaveSpeed;
cvar_t *r_waterWaveDispersion, *r_waterWaveGravity;
cvar_t *r_waterWind, *r_waterWindScale, *r_waterWindTransition, *r_waterWindDebug;
cvar_t *r_waterFlow, *r_waterFlowSpeed, *r_waterFlowDetail, *r_waterFlowDebug;
cvar_t *r_waterFlowField, *r_waterFlowImpact;
cvar_t *r_waterInteraction, *r_waterInteractionQuality, *r_waterInteractionStrength;
cvar_t *r_waterInteractionDamping, *r_waterInteractionSpeed, *r_waterInteractionMaxBodies;
cvar_t *r_waterInteractionMaxTexels, *r_waterInteractionMemoryMB, *r_waterInteractionDebug;
cvar_t *r_waterBathymetry, *r_waterBathymetryDebug, *r_waterInteractionVariableDepth;
cvar_t *r_waterRainRipples, *r_waterRainRippleDensity, *r_waterRainRippleImpulse;
cvar_t *r_waterRainRippleMicro, *r_waterRainRippleQuality, *r_waterRainRippleDebug;
cvar_t *r_waterIntersectionFoam, *r_waterIntersectionFoamWidth, *r_waterIntersectionFoamStrength;
cvar_t *r_waterIntersectionFoamPersistence, *r_waterIntersectionFoamDebug;
cvar_t *r_waterFoamField, *r_waterFoamResolution, *r_waterFoamDecay, *r_waterFoamAdvection;
cvar_t *r_waterFoamDiffusion, *r_waterFoamWaterfall, *r_waterFoamIntersection;
cvar_t *r_waterFoamImpact, *r_waterFoamShoreline, *r_waterFoamSteepness, *r_waterFoamDebug;
cvar_t *r_waterWhitewater, *r_waterWhitewaterThreshold, *r_waterWhitewaterStrength;
cvar_t *r_waterWhitewaterFoam, *r_waterWhitewaterDecay, *r_waterWhitewaterDebug;
cvar_t *r_waterSplashes, *r_waterSplashQuality, *r_waterSplashStrength;
cvar_t *r_waterSplashFoam, *r_waterSplashProjectiles, *r_waterSplashDebug;
cvar_t *r_waterWaveQuality, *r_waterWaveMicro, *r_waterWaveShallow, *r_waterWaveDebug, *r_waterWaveTime;
cvar_t *r_waterlineHysteresis, *r_waterlineDebug;
cvar_t	*r_waterSurfaceRefraction;
cvar_t	*r_waterSurfaceDepthReject;
cvar_t	*r_waterSurfaceReflection;
cvar_t	*r_waterSurfaceSSR;
cvar_t	*r_waterSSRWaveReprojection;
cvar_t	*r_waterSSROptimize;
cvar_t	*r_waterMotionDebug;
cvar_t	*r_waterSurfaceAbsorption;
cvar_t	*r_waterSurfaceDepthScale;
cvar_t	*r_waterSurfaceExperimental;
cvar_t	*r_waterSurfaceDebug;
cvar_t	*r_waterSnell;
cvar_t	*r_waterSnellDebug;
cvar_t	*r_waterSnellOptics;
cvar_t *fx_physicalizationAggregate;
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
cvar_t  *r_smaaQuality;

cvar_t  *r_cameraExposure;
cvar_t  *r_cameraMode;
cvar_t  *r_cameraISO;
cvar_t  *r_cameraAperture;
cvar_t  *r_cameraShutterSeconds;
cvar_t  *r_cameraAutoEVMin;
cvar_t  *r_cameraAutoEVMax;
cvar_t  *r_cameraDebug;

cvar_t  *r_externalGLSL;

cvar_t  *r_hdr;
cvar_t  *r_pbrPhysical;
cvar_t  *r_pbrWorldUnitsPerMeter;
cvar_t  *r_pbrPhysicalToScene;
cvar_t  *r_pbrLegacyReference;
cvar_t  *r_emissiveLegacyNits;
cvar_t  *r_emissiveDebug;
cvar_t  *r_pbrSunIlluminance;
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
cvar_t  *r_colorGradingLUT;
cvar_t  *r_colorGradingIntensity;
cvar_t  *r_autoEmissive;

cvar_t  *r_depthPrepass;
cvar_t  *r_ssao;
cvar_t  *r_aoMode;
cvar_t  *r_aoApply;
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
cvar_t  *r_rainLensQuality;
cvar_t  *r_rainLensDensity;
cvar_t  *r_rainLensRefraction;
cvar_t  *r_rainLensDropSize;
cvar_t  *r_rainLensFilm;
cvar_t  *r_rainLensFilmModel;
cvar_t  *r_rainLensFilmMicro;
cvar_t  *r_rainLensEmergeWater;
cvar_t  *r_rainLensEmergeTime;
cvar_t  *r_rainLensMist;
cvar_t  *r_rainLensImpactSplat;
cvar_t  *r_rainLensBlur;
cvar_t  *r_rainLensReflection;
cvar_t  *r_rainLensInertia;
cvar_t  *r_rainLensDebug;
cvar_t  *r_rainLensFieldHeight;
cvar_t  *r_rainLensAgentLimit;
cvar_t  *r_rainLensPinning;
cvar_t  *r_rainLensMerge;
cvar_t  *r_rainLensFilmDecay;
cvar_t  *r_rainLensWetDecay;
cvar_t  *r_rainLensHeavyFlow;
cvar_t  *r_rainLensPeripheralBias;
cvar_t  *r_rainLensPBO;
cvar_t  *r_rainLensMipBlur;
cvar_t  *r_motionBlur;
cvar_t  *r_motionBlurShutterAngle;
cvar_t  *r_motionBlurPhysicalShutter;
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
cvar_t  *r_weatherWetnessPersistence;
cvar_t  *r_weatherWetnessAccumulationRate;
cvar_t  *r_weatherWetnessDryRate;
cvar_t  *r_weatherWetnessUpdateHz;
cvar_t  *r_weatherWetnessResolution;
cvar_t  *r_weatherWetnessSteepDryScale;
cvar_t  *r_weatherWetnessAcidAccumulationScale;
cvar_t  *r_weatherWetnessAcidDryScale;
cvar_t  *r_weatherWetnessPersistenceDebug;
cvar_t  *r_acidRainVisuals;
cvar_t  *r_acidRainTint;
cvar_t  *r_acidRainStrength;
cvar_t  *r_weatherSurfaceDebug;
cvar_t  *r_weatherWetnessEntityFacing;
cvar_t  *r_weatherPuddles;
cvar_t  *r_weatherPuddleCoverage;
cvar_t  *r_weatherPuddleRoughness;
cvar_t  *r_weatherPuddleSlope;
cvar_t  *r_weatherPuddleScale;
cvar_t  *r_weatherPuddleEdgeModel;
cvar_t  *r_weatherPuddleFringeDarkening;
cvar_t  *r_weatherPuddleFilmRoughness;
cvar_t  *r_weatherPuddleUseHeightMap;
cvar_t  *r_weatherPuddleHeightSoftness;
cvar_t  *r_weatherPuddleWaterLevelBias;
cvar_t  *r_weatherPuddleRipples;
cvar_t  *r_weatherPuddleRippleStrength;
cvar_t  *r_weatherPuddleRippleScale;
cvar_t  *r_weatherPuddleRippleRate;
cvar_t  *r_weatherPuddleRippleImpulse;
cvar_t  *r_weatherPuddleRippleDamping;
cvar_t  *r_weatherPuddleRipplePropagation;
cvar_t  *r_weatherPuddleRippleResolution;
cvar_t  *r_weatherPuddleRippleFieldScale;
cvar_t  *r_weatherPuddleRippleDistance;
cvar_t  *r_weatherPuddleRippleDebug;
cvar_t  *r_weatherRunoff;
cvar_t  *r_weatherRunoffStrength;
cvar_t  *r_weatherRunoffSpeed;
cvar_t  *r_weatherRunoffScale;
cvar_t  *r_weatherRunoffProbe;
cvar_t  *r_weatherRunoffEntities;
cvar_t  *r_weatherRunoffTrailPersistence;
cvar_t  *r_weatherRunoffTrailDebug;
cvar_t  *r_ssrQuality;
cvar_t  *r_ssrSteps;
cvar_t  *r_ssrRefineSteps;
cvar_t  *r_ssrMaxDistance;
cvar_t  *r_ssrThickness;
cvar_t  *r_ssrMaxRoughness;
cvar_t  *r_ssrRoughOcclusion;
cvar_t  *r_ssrEdgeFade;
cvar_t  *r_ssrHalfRes;
cvar_t  *r_ssrHiZ;
cvar_t  *r_ssrTemporal;
cvar_t  *r_ssrTemporalWeight;
cvar_t  *r_ssrBlendStrength;
cvar_t  *r_ssrDebug;
cvar_t  *r_ssrEmitters;
cvar_t  *r_ssrEmitterIntensity;
cvar_t  *r_ssrEmitterMaxRoughness;
cvar_t  *r_ssrHitCache;
cvar_t  *r_ssrResolveCache;
cvar_t  *r_ssrReceiverCull;
cvar_t  *r_ssrFusedResolve;
cvar_t  *r_ssrFootprintDepth;
cvar_t  *r_ssrStochastic;
cvar_t  *r_ssrReflectedFog;

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
cvar_t  *r_skinSSSDebug;

cvar_t  *r_autoPBR;
cvar_t  *r_autoPBRDebug;
cvar_t  *r_materialModelDebug;
cvar_t  *r_materialModels;
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
cvar_t  *r_foliageForceWaves;
cvar_t  *r_foliageForceWaveStrength;
cvar_t  *r_foliageSaberCut;
cvar_t  *r_foliageSaberCutPieces;
cvar_t  *r_foliageSaberCutPieceTime;
cvar_t  *r_foliageBendFieldSize;
cvar_t  *r_foliageBendFieldExtent;
cvar_t  *r_foliageBendFieldStrength;
cvar_t  *r_foliageBendFieldRecoveryTime;
cvar_t  *r_foliageBendFieldDamping;
cvar_t  *r_foliageBendFieldImpulse;
cvar_t  *r_foliageBendFieldDebug;
cvar_t  *r_plantWind;
cvar_t  *r_plantWindStrength;
cvar_t  *r_plantSimDistance;
cvar_t  *r_plantWindDistance;
cvar_t  *r_plantSimMaxPlants;
cvar_t  *r_plantStiffness;
cvar_t  *r_plantDebug;
cvar_t  *r_grassBendSegments;
cvar_t  *r_grassBendLodDist;
cvar_t  *r_grassTrample;
cvar_t  *r_grassTrampleRecovery;
cvar_t  *r_autoPBRConvert;
cvar_t  *r_autoPBRRoughness;
cvar_t  *r_diffuseBRDF;
cvar_t  *r_brdfEnergyCompensation;
cvar_t  *r_pbrEnergyDebug;
cvar_t  *r_specularAA;
cvar_t  *r_specularAAStrength;
cvar_t  *r_specularAAMaxAdded;
cvar_t  *r_specularAADebug;
cvar_t  *r_pbrLegacyRoughness;
cvar_t  *r_diffuseIBL;
cvar_t  *r_diffuseIBLStrength;
cvar_t  *r_diffuseIBLDebug;

cvar_t  *r_shaderProgramCache;
cvar_t  *r_shaderProgramCacheMaxMB;
cvar_t  *r_shaderProgramLazy;
cvar_t  *r_shaderCompileProcesses;

cvar_t  *r_forwardPlus;
cvar_t  *r_forwardPlusTileSize;
cvar_t  *r_forwardPlusSlices;
cvar_t  *r_forwardPlusNearSlice;
cvar_t  *r_forwardPlusMaxLightsPerCluster;
cvar_t  *r_forwardPlusDebug;
cvar_t  *r_forwardPlusDebugLight;
cvar_t  *r_forwardPlusMaxShadowLights;
cvar_t  *r_forwardPlusCompute;
cvar_t  *r_ltcAreaLights;
cvar_t  *r_ltcDebug;
cvar_t  *r_ltcDebugLight;
cvar_t  *r_ltcIntensityScale;
cvar_t  *r_ltcSaberIntensityScale;
cvar_t  *r_ltcSaberLuminance;
cvar_t  *r_ltcStaticDiffuse;
cvar_t  *r_ltcMaxLights;
cvar_t  *r_ltcAutoAreaLights;
cvar_t  *r_ltcSaberAreaLights;
cvar_t  *r_ltcStaticShadows;
cvar_t  *r_ltcSaberShadows;

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
cvar_t  *r_sunShadowCache;
cvar_t  *r_sunShadowVerify;
cvar_t  *r_sunShadowBenchmark;
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
cvar_t  *r_sunShadowModelLayer;
cvar_t  *r_shadowDebug;
cvar_t  *r_shadowCasterLod;
cvar_t  *r_shadowCasterStats;
cvar_t  *r_shadowStats;
cvar_t  *r_shadowSinglePass;
cvar_t  *r_skyPortalCull;
cvar_t  *r_shadowLayered;
cvar_t  *r_shadowVerify;
cvar_t  *r_shadowCache;
cvar_t  *r_shadowStaticCache;
cvar_t  *r_dlightShadowBias;
cvar_t  *r_contactShadowSoft;
cvar_t  *r_entitySunLight;
cvar_t  *r_entityShadowAmbient;
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
cvar_t	*r_ragSoftLimits=0;
cvar_t	*r_ragSoftLimitZone=0;
cvar_t	*r_ragSoftLimitStiffness=0;
cvar_t	*r_ragNeutralPull=0;
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
cvar_t *r_rainVolume;
cvar_t *r_rainVolumeStrength;
cvar_t *r_rainVolumeHeavyMultiplier;
cvar_t *r_rainVolumeAnisotropy;
cvar_t *r_rainVolumeGroundStrength;
cvar_t *r_rainVolumeGroundHeight;
cvar_t *r_rainVolumeMaxDistance;
cvar_t *r_rainLOD;
cvar_t *r_rainLODNear;
cvar_t *r_rainLODMid;
cvar_t *r_rainLODDensityFloor;
cvar_t *r_rainLODFarVolumeCoupling;
cvar_t *r_rainLODDebug;
cvar_t *r_waterParticles;
cvar_t *r_waterParticlesDensity;
cvar_t *r_waterParticlesSize;
cvar_t *r_waterParticlesSpeed;
cvar_t *r_waterParticlesBubbles;
cvar_t *r_waterParticlesDistance;
cvar_t *r_waterParticlesProfile;
cvar_t *r_waterParticlesDebug;
cvar_t *r_rainStreakWidth;
cvar_t *r_rainStreakLength;
cvar_t *r_rainStreakOpacity;
cvar_t *r_rainStreakLighting;
cvar_t *r_rainDynamicLighting;
cvar_t *r_rainDynamicMaxLights;
cvar_t *r_rainDynamicDebug;
cvar_t *r_rainStreakDebug;
cvar_t *r_rainSplashes;
cvar_t *r_rainSplashSize;
cvar_t *r_rainSplashLifetime;
cvar_t *r_rainSplashOpacity;
cvar_t *r_rainSplashDebug;
cvar_t *r_rainSplashSurfaceResponse;
cvar_t *r_rainSplashSurfaceDebug;
cvar_t *r_rainSplashHardDryStrength;
cvar_t *r_rainSplashHardWetStrength;
cvar_t *r_rainSplashPuddleStrength;
cvar_t *r_rainSplashWaterStrength;
cvar_t *r_rainSplashSoilStrength;
cvar_t *r_rainSplashVegetationStrength;
cvar_t *r_rainSplashMetalStrength;

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
	{ "r_help",			R_CvarHelp_f },
	{ "r_compare",		R_Compare_f },
	{ "imagelist",			R_ImageList_f },
	{ "imagechecksums",		R_ImageChecksums_f },
	{ "glslusage",			GLSL_Usage_f },
	{ "shaderlist",			R_ShaderList_f },
	{ "r_pbrDumpMaterials",	R_PBRDumpMaterials_f },
	{ "r_skinSSSKernel",	R_SkinSSSKernel_f },
	{ "r_skinSSSList",	R_SkinSSSList_f },
	{ "r_weatherMaterialList", R_WeatherMaterialList_f },
	{ "r_precipitationInfo", R_PrecipitationInfo_f },
	{ "r_autoFoliageList", R_PrintAutoFoliage_f },
	{ "r_foliageInteractionList", R_FoliageInteractionList_f },
	{ "r_foliageBendFieldClear", R_FoliageFieldClear_f },
	{ "r_plantSimInfo", R_PlantSimInfo_f },
	{ "r_foliageSaberCutReset", R_PlantSimRestoreCuts_f },
	{ "r_foliageSaberCutTest", R_PlantSimCutTest_f },
	{ "r_forwardPlusStats",	R_ForwardPlusStats_f },
	{ "r_forwardPlusVerify",	R_ForwardPlusVerify_f },
	{ "r_vfogLightVerify",	R_VolumetricLightVerify_f },
	{ "r_pomSilhouetteInfo",	R_PomSilhouetteInfo_f },
	{ "r_autoPOMSilhouette",	R_AutoPomSilhouette_f },
	{ "r_forwardPlusSpawnTestLights",	R_SpawnTestLights_f },
	{ "r_forwardPlusBenchmark",	R_ForwardPlusBenchmark_f },
	{ "r_ltcReloadLights",	R_ReloadAreaLights_f },
	{ "r_ltcShadowStats",	R_LtcShadowStats_f },
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
	{ "r_atmosphereInfo",	R_AtmosphereInfo_f },
	{ "r_cloudInfo",		R_CloudInfo_f },
	{ "r_cloudLayerTest",	R_CloudLayerTest_f },
	{ "r_vfogLightStats",	R_VolumetricLightStats_f },
	{ "r_lightReconInfo",	R_LightReconInfo_f },
	{ "r_lightReconLuxels", R_LuxelDebug_f },
	{ "r_lightReconPreview", R_LightReconPreview_f },
	{ "r_lightReconDebug", R_LightReconDebug_f },
	{ "r_lightReconObservations", R_LightReconObservations_f },
	{ "r_lightReconEvidence", R_LightReconEvidence_f },
	{ "r_lightReconExport", R_LightReconExport_f },
	{ "r_pbrInfo",		R_PBRInfo_f },
	{ "r_pbrLight",		R_PBRLight_f },
	{ "r_fogvol",			R_FogVolume_f },
	{ "r_liquids",			R_Liquids_f },
	{ "r_waterInfo",		R_WaterInfo_f },
	{ "r_waterBodies",	R_WaterBodies_f },
	{ "r_waterfalls",	R_Waterfalls_f },
	{ "r_waterFlowOverride", R_WaterFlowOverride_f },
	{ "r_waterGeometryInfo", R_WaterGeometryInfo_f },
	{ "r_waterInteractionInfo", R_WaterInteractionInfo_f },
	{ "r_waterIntersectionVerify", R_WaterIntersectionVerify_f },
	{ "r_waterImpulse", R_WaterImpulse_f },
	{ "r_waterOverride",	R_WaterOverride_f },
	{ "r_volparticles",		R_VolParticles_f },
	{ "rainlens_clear",		R_RainLensClear_f },
	{ "rainlens_event",		R_RainLensEvent_f },
	{ "rainlens_profile",	R_RainLensProfile_f },
	{ "rainlens_stats",		R_RainLensStats_f },
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
	r_allowExtensions = R_Cvar( "r_allowExtensions", "1", CVAR_ARCHIVE | CVAR_LATCH, "Allow GL extensions" );
	r_ext_compressed_textures = R_Cvar( "r_ext_compress_textures", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable texture compression" );
	r_ext_multitexture = R_Cvar( "r_ext_multitexture", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_compiled_vertex_array = R_Cvar( "r_ext_compiled_vertex_array", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_texture_env_add = R_Cvar( "r_ext_texture_env_add", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_preferred_tc_method = R_Cvar( "r_ext_preferred_tc_method", "0", CVAR_ARCHIVE | CVAR_LATCH, "Preferred texture compression method" );

	r_ext_draw_range_elements = R_Cvar( "r_ext_draw_range_elements", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_multi_draw_arrays = R_Cvar( "r_ext_multi_draw_arrays", "1", CVAR_ARCHIVE | CVAR_LATCH, "Unused" );
	r_ext_texture_float = R_Cvar( "r_ext_texture_float", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable floating-point textures" );
	r_arb_half_float_pixel = R_Cvar( "r_arb_half_float_pixel", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable ARB_half_float GL extension" );
	r_ext_framebuffer_multisample = R_Cvar( "r_ext_multisample", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable framebuffer MSAA" );
	// We do MSAA resolving manually in rend2, so don't bother with the default framebuffer
	ri.Cvar_Set("r_ext_multisample_default_fb", "0");
	r_arb_seamless_cube_map = R_Cvar( "r_arb_seamless_cube_map", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable seamless cube map filtering GL extension" );
	r_arb_vertex_type_2_10_10_10_rev = R_Cvar( "r_arb_vertex_type_2_10_10_10_rev", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable 1010102 UI data type" );
	r_arb_buffer_storage = R_Cvar( "r_arb_buffer_storage", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable buffer storage GL extension" );
	r_ext_texture_filter_anisotropic = R_Cvar( "r_ext_texture_filter_anisotropic", "16", CVAR_ARCHIVE, "Disable/enable anisotropic texture filtering" );

	r_smaa = R_Cvar("r_smaa", "0", CVAR_ARCHIVE | CVAR_LATCH, "SMAA anti-aliasing: 0 off, 1 SMAA 1x, 2 SMAA T2x (temporal, jitters the view)");
	R_CvarRange(r_smaa, 0, 2, qtrue);
	r_smaaQuality = R_Cvar("r_smaaQuality", "2", CVAR_ARCHIVE | CVAR_LATCH, "SMAA quality: 0 low, 1 medium, 2 high, 3 ultra");
	R_CvarRange(r_smaaQuality, 0, 3, qtrue);

	r_dynamicGlow						= R_Cvar( "r_dynamicGlow", "0", CVAR_ARCHIVE, "Original JKA glow of glow-marked surfaces (used with r_bloom -1): 0 off, 1 on, 2 show only the glow" );
	r_dynamicGlowPasses					= R_Cvar( "r_dynamicGlowPasses", "5", CVAR_ARCHIVE, "Original glow: blur passes (more is wider)" );
	r_dynamicGlowDelta					= R_Cvar( "r_dynamicGlowDelta", "0.8f", CVAR_ARCHIVE, "Original glow: blur sample spread" );
	r_dynamicGlowIntensity				= R_Cvar( "r_dynamicGlowIntensity", "1.13f", CVAR_ARCHIVE, "Original glow: brightness" );
	r_dynamicGlowSoft					= R_Cvar( "r_dynamicGlowSoft", "1", CVAR_ARCHIVE, "Original glow: 0 add the glow, 1 soft screen blend" );
	r_dynamicGlowWidth					= R_Cvar( "r_dynamicGlowWidth", "320", CVAR_ARCHIVE|CVAR_LATCH, "Original glow: width of the glow buffer in pixels" );
	r_dynamicGlowHeight					= R_Cvar( "r_dynamicGlowHeight", "240", CVAR_ARCHIVE|CVAR_LATCH, "Original glow: height of the glow buffer in pixels" );
	r_dynamicGlowBloom					= R_Cvar( "r_dynamicGlowBloom", "0.0", CVAR_ARCHIVE, "Original glow (r_bloom -1): also let bright parts of the scene glow, 0 only glow-marked surfaces" );
	R_CvarRange(r_dynamicGlowBloom, 0.f, 2.f, qfalse);
	r_bloom = R_Cvar("r_bloom", "-1", CVAR_ARCHIVE, "-1: legacy glow, 0: off, 1: HDR bloom");
	R_CvarRange(r_bloom, -1, 1, qtrue);
	r_bloomIntensity = R_Cvar("r_bloomIntensity", "0.15", CVAR_ARCHIVE, "HDR bloom strength");
	R_CvarRange(r_bloomIntensity, 0.f, 4.f, qfalse);
	r_bloomThreshold = R_Cvar("r_bloomThreshold", "2.0", CVAR_ARCHIVE, "Scene-linear bloom threshold");
	R_CvarRange(r_bloomThreshold, 0.f, 64.f, qfalse);
	r_bloomKnee = R_Cvar("r_bloomKnee", "0.5", CVAR_ARCHIVE, "Bloom threshold soft knee fraction");
	R_CvarRange(r_bloomKnee, 0.f, 1.f, qfalse);
	r_bloomScatter = R_Cvar("r_bloomScatter", "0.7", CVAR_ARCHIVE, "Bloom pyramid upscale scatter");
	R_CvarRange(r_bloomScatter, 0.f, 1.f, qfalse);
	r_bloomSceneIntensity = R_Cvar("r_bloomSceneIntensity", "0", CVAR_ARCHIVE, "Optional HDR scene bloom, independent of emissive bloom");
	R_CvarRange(r_bloomSceneIntensity, 0.f, 2.f, qfalse);

	r_gl43 = R_Cvar( "r_gl43", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"Prefer GL 4.3 and allow modern paths; 0 forces legacy paths after vid_restart" );
	r_debugContext						= R_Cvar( "r_debugContext", "0", CVAR_LATCH, "Create an OpenGL debug context and print driver messages: 0 off, 1 on" );
	r_debugWeather						= R_Cvar( "r_debugWeather", "0", CVAR_ARCHIVE, "Weather debug: 1 particle info, 2 show the top-down weather depth map" );
	r_weatherCull = R_Cvar("r_weatherCull", "0", CVAR_ARCHIVE,
		"Cull weather chunks outside the camera frustum (0 draws all nine)");
	r_weatherDebugChunks = R_Cvar("r_weatherDebugChunks", "0", 0,
		"Draw weather chunk bounds and print mapping, visibility and particle counts (2 prints AABBs)");
	r_rainStreaks = R_Cvar("r_rainStreaks", "0", CVAR_ARCHIVE,
		"Rain streak rendering: 0 legacy additive, 1 lit premultiplied streaks sized by velocity with contact / near / sub-pixel fades");
	r_rainVolume = R_Cvar("r_rainVolume", "0", CVAR_ARCHIVE,
		"Add rain suspension as a weather-occluded froxel medium (requires r_volumetricFog 2)");
	r_rainVolumeStrength = R_Cvar("r_rainVolumeStrength", "0.00018", CVAR_ARCHIVE,
		"Normal-rain froxel extinction per world unit at full intensity");
	r_rainVolumeHeavyMultiplier = R_Cvar("r_rainVolumeHeavyMultiplier", "4", CVAR_ARCHIVE,
		"Heavy-rain multiplier for the froxel rain medium");
	r_rainVolumeAnisotropy = R_Cvar("r_rainVolumeAnisotropy", "0.72", CVAR_ARCHIVE,
		"Henyey-Greenstein anisotropy of the rain medium");
	r_rainVolumeGroundStrength = R_Cvar("r_rainVolumeGroundStrength", "0", CVAR_ARCHIVE,
		"Optional heavy-rain near-ground splash-mist extinction (0 disables it)");
	r_rainVolumeGroundHeight = R_Cvar("r_rainVolumeGroundHeight", "96", CVAR_ARCHIVE,
		"Height in world units of the heavy-rain splash-mist layer");
	r_rainVolumeMaxDistance = R_Cvar("r_rainVolumeMaxDistance", "6000", CVAR_ARCHIVE,
		"Maximum camera distance of the rain medium, with a soft outer fade");
	r_rainLOD = R_Cvar("r_rainLOD", "0", CVAR_ARCHIVE,
		"Distance LOD from real rain streaks to the far volumetric rain medium");
	r_rainLODNear = R_Cvar("r_rainLODNear", "1200", CVAR_ARCHIVE,
		"Distance through which rain keeps full streak density");
	r_rainLODMid = R_Cvar("r_rainLODMid", "3600", CVAR_ARCHIVE,
		"End of the reduced-density streak band; beyond this only the rain medium remains");
	r_rainLODDensityFloor = R_Cvar("r_rainLODDensityFloor", "0.25", CVAR_ARCHIVE,
		"Minimum stochastic streak density in the middle LOD band");
	r_rainLODFarVolumeCoupling = R_Cvar("r_rainLODFarVolumeCoupling", "1", CVAR_ARCHIVE,
		"Blend the rain medium from legacy all-distance (0) to the streak LOD handoff (1)");
	r_rainLODDebug = R_Cvar("r_rainLODDebug", "0", CVAR_CHEAT,
		"Show rain LOD distance bands: green near, yellow mid, blue far");
	R_CvarRange(r_rainVolume, 0, 1, qtrue);
	R_CvarRange(r_rainVolumeStrength, 0.0f, 0.01f, qfalse);
	R_CvarRange(r_rainVolumeHeavyMultiplier, 0.0f, 16.0f, qfalse);
	R_CvarRange(r_rainVolumeAnisotropy, -0.9f, 0.9f, qfalse);
	R_CvarRange(r_rainVolumeGroundStrength, 0.0f, 0.01f, qfalse);
	R_CvarRange(r_rainVolumeGroundHeight, 1.0f, 1024.0f, qfalse);
	R_CvarRange(r_rainVolumeMaxDistance, 64.0f, 32768.0f, qfalse);
	R_CvarRange(r_rainLOD, 0, 1, qtrue);
	R_CvarRange(r_rainLODNear, 0.0f, 32767.0f, qfalse);
	R_CvarRange(r_rainLODMid, 1.0f, 32768.0f, qfalse);
	R_CvarRange(r_rainLODDensityFloor, 0.05f, 1.0f, qfalse);
	R_CvarRange(r_rainLODFarVolumeCoupling, 0.0f, 1.0f, qfalse);
	r_rainStreakWidth = R_Cvar("r_rainStreakWidth", "1", CVAR_ARCHIVE,
		"r_rainStreaks: streak width scale");
	r_rainStreakLength = R_Cvar("r_rainStreakLength", "1", CVAR_ARCHIVE,
		"r_rainStreaks: streak length scale (length also follows fall speed and wind)");
	r_rainStreakOpacity = R_Cvar("r_rainStreakOpacity", "1", CVAR_ARCHIVE,
		"r_rainStreaks: rain opacity scale");
	r_rainStreakLighting = R_Cvar("r_rainStreakLighting", "1", CVAR_ARCHIVE,
		"r_rainStreaks: 0 fixed tint (legacy brightness), 1 lit by the light grid and the sun");
	r_rainDynamicLighting = R_Cvar("r_rainDynamicLighting", "1", CVAR_ARCHIVE,
		"Rain streak/splash dynamic lights: 0 current grid/sun only, 1 strongest nearby, 2 limited Forward+/UBO lights");
	r_rainDynamicMaxLights = R_Cvar("r_rainDynamicMaxLights", "4", CVAR_ARCHIVE,
		"Maximum nearby dynamic lights per rain streak or splash in r_rainDynamicLighting 2");
	r_rainDynamicDebug = R_Cvar("r_rainDynamicDebug", "0", CVAR_CHEAT,
		"Rain dynamic-light debug: 0 final, 1 static/grid, 2 dynamic, 3 selected light count");
	r_rainStreakDebug = R_Cvar("r_rainStreakDebug", "0", CVAR_CHEAT,
		"r_rainStreaks debug: 1 coverage, 2 distance fade (red near, blue far), 3 lighting factor, 4 weather depth contact, 5 particle variation");
	R_CvarRange(r_rainStreakWidth, 0.25f, 4.0f, qfalse);
	R_CvarRange(r_rainStreakLength, 0.25f, 4.0f, qfalse);
	R_CvarRange(r_rainStreakOpacity, 0.0f, 4.0f, qfalse);
	r_waterParticles = R_Cvar("r_waterParticles", "0", CVAR_ARCHIVE,
		"Underwater suspended particles and micro-bubbles around the camera (0 off, 1 on)");
	r_waterParticlesDensity = R_Cvar("r_waterParticlesDensity", "1", CVAR_ARCHIVE,
		"r_waterParticles: density scale of the body profile");
	r_waterParticlesSize = R_Cvar("r_waterParticlesSize", "1", CVAR_ARCHIVE,
		"r_waterParticles: particle size scale");
	r_waterParticlesSpeed = R_Cvar("r_waterParticlesSpeed", "1", CVAR_ARCHIVE,
		"r_waterParticles: drift and bubble rise speed scale");
	r_waterParticlesBubbles = R_Cvar("r_waterParticlesBubbles", "-1", CVAR_ARCHIVE,
		"r_waterParticles: share of micro-bubbles 0..1, -1 = the body profile's");
	r_waterParticlesDistance = R_Cvar("r_waterParticlesDistance", "160", CVAR_ARCHIVE,
		"r_waterParticles: radius of the particle volume around the camera (world units)");
	r_waterParticlesProfile = R_Cvar("r_waterParticlesProfile", "0", 0,
		"r_waterParticles: body profile, 0 auto (env.json WaterParticles, else clean), 1 clean, 2 murky, 3 dirty, -1 none");
	r_waterParticlesDebug = R_Cvar("r_waterParticlesDebug", "0", CVAR_CHEAT,
		"r_waterParticles debug: 1 placement (opaque, cyan motes, yellow bubbles), 2 no fog in front, 3 print stats every second");
	R_CvarRange(r_waterParticlesDensity, 0.0f, 8.0f, qfalse);
	R_CvarRange(r_waterParticlesSize, 0.25f, 4.0f, qfalse);
	R_CvarRange(r_waterParticlesSpeed, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterParticlesDistance, 32.0f, 512.0f, qfalse);
	R_CvarRange(r_rainStreakLighting, 0.0f, 1.0f, qfalse);
	R_CvarRange(r_rainDynamicLighting, 0, 2, qtrue);
	R_CvarRange(r_rainDynamicMaxLights, 1, 8, qtrue);
	R_CvarRange(r_rainDynamicDebug, 0, 3, qtrue);
	R_CvarRange(r_rainStreakDebug, 0, 5, qtrue);
	r_rainSplashes = R_Cvar("r_rainSplashes", "0", CVAR_ARCHIVE,
		"Rain impact splashes: drops crossing the rain occlusion map on the GPU make short-lived splashes (0 off)");
	r_rainSplashSize = R_Cvar("r_rainSplashSize", "6", CVAR_ARCHIVE,
		"r_rainSplashes: splash radius in world units");
	r_rainSplashLifetime = R_Cvar("r_rainSplashLifetime", "350", CVAR_ARCHIVE,
		"r_rainSplashes: splash lifetime in ms");
	r_rainSplashOpacity = R_Cvar("r_rainSplashOpacity", "0.5", CVAR_ARCHIVE,
		"r_rainSplashes: splash opacity");
	r_rainSplashDebug = R_Cvar("r_rainSplashDebug", "0", CVAR_CHEAT,
		"r_rainSplashes debug: 1 impact points (yellow new, red old, through walls), 2 crossing test (green above, red under, cyan accepted, magenta rejected), 3 trajectories; prints the live splash count");
	r_rainSplashSurfaceResponse = R_Cvar("r_rainSplashSurfaceResponse", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"Surface-dependent rain impacts: 0 exact legacy splash, 1 class response (vid_restart)");
	r_rainSplashSurfaceDebug = R_Cvar("r_rainSplashSurfaceDebug", "0", CVAR_CHEAT,
		"Show the top-down R8 weather impact class map");
	r_rainSplashHardDryStrength = R_Cvar("r_rainSplashHardDryStrength", "1", CVAR_ARCHIVE, "Hard dry rain impact response scale");
	r_rainSplashHardWetStrength = R_Cvar("r_rainSplashHardWetStrength", "1", CVAR_ARCHIVE, "Hard wet rain impact response scale");
	r_rainSplashPuddleStrength = R_Cvar("r_rainSplashPuddleStrength", "1", CVAR_ARCHIVE, "Puddle rain impact response scale");
	r_rainSplashWaterStrength = R_Cvar("r_rainSplashWaterStrength", "1", CVAR_ARCHIVE, "Real water rain impact response scale");
	r_rainSplashSoilStrength = R_Cvar("r_rainSplashSoilStrength", "1", CVAR_ARCHIVE, "Soft soil rain impact response scale");
	r_rainSplashVegetationStrength = R_Cvar("r_rainSplashVegetationStrength", "1", CVAR_ARCHIVE, "Vegetation rain impact response scale");
	r_rainSplashMetalStrength = R_Cvar("r_rainSplashMetalStrength", "1", CVAR_ARCHIVE, "Metal rain impact response scale");
	R_CvarRange(r_rainSplashSize, 1.0f, 64.0f, qfalse);
	R_CvarRange(r_rainSplashLifetime, 50.0f, 2000.0f, qfalse);
	R_CvarRange(r_rainSplashOpacity, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashDebug, 0, 3, qtrue);
	R_CvarRange(r_rainSplashSurfaceResponse, 0, 1, qtrue);
	R_CvarRange(r_rainSplashSurfaceDebug, 0, 1, qtrue);
	R_CvarRange(r_rainSplashHardDryStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashHardWetStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashPuddleStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashWaterStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashSoilStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashVegetationStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_rainSplashMetalStrength, 0.0f, 4.0f, qfalse);

	r_picmip = R_Cvar( "r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH, "Texture detail: 0 full resolution, each step halves the texture size" );
	R_CvarRange( r_picmip, 0, 16, qtrue );
	r_roundImagesDown = R_Cvar( "r_roundImagesDown", "1", CVAR_ARCHIVE | CVAR_LATCH, "Round non power of two images down instead of up: 0 up, 1 down" );
	r_colorMipLevels = R_Cvar( "r_colorMipLevels", "0", CVAR_LATCH, "Debug: color each texture mip level: 0 off, 1 on" );
	r_detailTextures = R_Cvar( "r_detailtextures", "1", CVAR_ARCHIVE | CVAR_LATCH, "Draw detail texture stages: 0 off, 1 on" );
	r_texturebits = R_Cvar( "r_texturebits", "0", CVAR_ARCHIVE | CVAR_LATCH, "Texture color depth: 0 driver default, 16 or 32" );
	r_overBrightBits = R_Cvar( "r_overBrightBits", "0", CVAR_ARCHIVE | CVAR_LATCH, "Hardware overbright: 0 off, 1 doubles the lighting range (needs gamma ramp support)" );
	r_simpleMipMaps = R_Cvar( "r_simpleMipMaps", "1", CVAR_ARCHIVE | CVAR_LATCH, "Simple box-filtered mip maps: 0 off, 1 on" );
	r_vertexLight = R_Cvar( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH, "Vertex lighting instead of lightmaps: 0 off, 1 on" );
	r_uiFullScreen = R_Cvar( "r_uifullscreen", "0", 0, "Set by the game while a fullscreen menu is up" );
	r_subdivisions = R_Cvar( "r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH, "Curved surface tessellation: lower numbers are finer" );
	R_CvarRange( r_subdivisions, 4, 80, qfalse );
	r_stereo = R_Cvar( "r_stereo", "0", CVAR_ARCHIVE | CVAR_LATCH, "Quad-buffered stereo rendering: 0 off, 1 on" );
	r_greyscale = R_Cvar( "r_greyscale", "0", CVAR_ARCHIVE | CVAR_LATCH, "Draw everything in grey: 0 color, 1 grey" );
	R_CvarRange(r_greyscale, 0, 1, qfalse);

	r_externalGLSL = R_Cvar( "r_externalGLSL", "0", CVAR_LATCH, "Load GLSL from glsl/*.glsl in the game data instead of the built-in shaders: 0 off, 1 on" );

	r_hdr = R_Cvar( "r_hdr", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable rendering in HDR" );
	r_pbrPhysical = R_Cvar("r_pbrPhysical", "0", CVAR_ARCHIVE | CVAR_LATCH, "0 exact Rend2 compatibility, 1 unified scene-referred physical PBR contract (vid_restart)");
	R_CvarRange(r_pbrPhysical, 0, 1, qtrue);
	r_pbrWorldUnitsPerMeter = R_Cvar("r_pbrWorldUnitsPerMeter", "33.333333", CVAR_ARCHIVE | CVAR_LATCH, "JA world units per metre for physical light distance and emitter area");
	R_CvarRange(r_pbrWorldUnitsPerMeter, 1.0f, 1000.0f, qfalse);
	r_pbrPhysicalToScene = R_Cvar("r_pbrPhysicalToScene", "0.0001", CVAR_ARCHIVE | CVAR_LATCH, "Canonical scene value per physical cd, lux or cd/m^2");
	R_CvarRange(r_pbrPhysicalToScene, 0.000001f, 1.0f, qfalse);
	r_pbrLegacyReference = R_Cvar("r_pbrLegacyReference", "1", CVAR_ARCHIVE | CVAR_LATCH, "Legacy light reference in canonical scene units at the migration anchor");
	R_CvarRange(r_pbrLegacyReference, 0.001f, 100.0f, qfalse);
	r_emissiveLegacyNits = R_Cvar("r_emissiveLegacyNits", "1000", CVAR_ARCHIVE | CVAR_LATCH, "Reference-white nits represented by legacy emissiveScale 1 in physical mode");
	R_CvarRange(r_emissiveLegacyNits, 1.0f, 100000.0f, qfalse);
	r_emissiveDebug = R_Cvar("r_emissiveDebug", "0", CVAR_CHEAT, "Emissive debug: 1 provenance, 2 false-color nits, 3 confidence");
	R_CvarRange(r_emissiveDebug, 0, 3, qtrue);
	r_pbrSunIlluminance = R_Cvar("r_pbrSunIlluminance", "0", CVAR_ARCHIVE, "Physical sun illuminance in lux; 0 migrates the map sun");
	R_CvarRange(r_pbrSunIlluminance, 0.0f, 200000.0f, qfalse);
	r_floatLightmap = R_Cvar( "r_floatLightmap", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable HDR lightmap support" );

	r_toneMap = R_Cvar( "r_toneMap", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable tonemapping" );
	r_forceToneMap = R_Cvar( "r_forceToneMap", "0", CVAR_CHEAT, "Debug: tone map with the r_forceToneMap* values instead of the measured ones: 0 off, 1 on" );
	r_forceToneMapMin = R_Cvar( "r_forceToneMapMin", "-8.0", CVAR_CHEAT, "Debug: forced tone map minimum, log2 luminance" );
	r_forceToneMapAvg = R_Cvar( "r_forceToneMapAvg", "-2.0", CVAR_CHEAT, "Debug: forced tone map average, log2 luminance" );
	r_forceToneMapMax = R_Cvar( "r_forceToneMapMax", "0.0", CVAR_CHEAT, "Debug: forced tone map maximum, log2 luminance" );

	r_autoExposure = R_Cvar( "r_autoExposure", "1", CVAR_ARCHIVE, "Disable/enable auto exposure" );
	r_forceAutoExposure = R_Cvar( "r_forceAutoExposure", "0", CVAR_CHEAT, "Debug: auto exposure with the r_forceAutoExposure* limits: 0 off, 1 on" );
	r_forceAutoExposureMin = R_Cvar( "r_forceAutoExposureMin", "-2.0", CVAR_CHEAT, "Debug: forced auto exposure minimum, stops" );
	r_forceAutoExposureMax = R_Cvar( "r_forceAutoExposureMax", "2.0", CVAR_CHEAT, "Debug: forced auto exposure maximum, stops" );

	r_cameraExposure = R_Cvar( "r_cameraExposure", "0", CVAR_CHEAT, "Legacy-only HDR buffer gain in EV stops; ignored by physical camera modes" );
	r_cameraMode = R_Cvar( "r_cameraMode", "0", CVAR_ARCHIVE, "Camera exposure: 0 = legacy, 1 = physical manual, 2 = physical auto" );
	R_CvarRange( r_cameraMode, 0, 2, qtrue );
	r_cameraISO = R_Cvar( "r_cameraISO", "100", CVAR_ARCHIVE, "Physical camera ISO speed (manual mode)" );
	R_CvarRange( r_cameraISO, 1.0f, 204800.0f, qfalse );
	r_cameraAperture = R_Cvar( "r_cameraAperture", "16", CVAR_ARCHIVE, "Physical camera aperture f-number N (manual mode; reserved for future physical DOF)" );
	R_CvarRange( r_cameraAperture, 0.5f, 64.0f, qfalse );
	r_cameraShutterSeconds = R_Cvar( "r_cameraShutterSeconds", "0.008", CVAR_ARCHIVE, "Physical camera shutter duration in seconds (manual mode; optional motion-blur coupling)" );
	R_CvarRange( r_cameraShutterSeconds, 0.000001f, 60.0f, qfalse );
	r_cameraAutoEVMin = R_Cvar( "r_cameraAutoEVMin", "-6", CVAR_ARCHIVE, "Minimum EV100 allowed in physical auto mode" );
	R_CvarRange( r_cameraAutoEVMin, -16.0f, 32.0f, qfalse );
	r_cameraAutoEVMax = R_Cvar( "r_cameraAutoEVMax", "20", CVAR_ARCHIVE, "Maximum EV100 allowed in physical auto mode" );
	R_CvarRange( r_cameraAutoEVMax, -16.0f, 32.0f, qfalse );
	r_cameraDebug = R_Cvar( "r_cameraDebug", "0", CVAR_CHEAT, "Print calibrated camera/meter state once per second" );
	R_CvarRange( r_cameraDebug, 0, 1, qtrue );

	r_toneMapMode = R_Cvar( "r_toneMapMode", "0", CVAR_ARCHIVE, "Tone mapping operator: 0 = legacy Rend2 filmic, 1 = ACES (fitted approximation), 2 = AgX-like (approximation)" );
	R_CvarRange( r_toneMapMode, 0, 2, qtrue );
	r_toneMapDebug = R_Cvar( "r_toneMapDebug", "0", CVAR_CHEAT, "Tone mapping debug view: 1 raw scene-linear, 2 exposure-adjusted, 3 exposure false color (old / new curves: r_toneMapCompare)");
	R_CvarRange(r_toneMapDebug, 0, 3, qtrue);
	r_exposureCompensation = R_Cvar( "r_exposureCompensation", "0", CVAR_ARCHIVE, "Exposure compensation in EV stops, applied in linear light before tone mapping" );
	R_CvarRange( r_exposureCompensation, -4.0f, 4.0f, qfalse );
	r_linearLighting = R_Cvar( "r_linearLighting", "0", CVAR_ARCHIVE | CVAR_LATCH, "Light maps without HDR lightmaps in linear light (experimental, needs r_hdr 1 and r_toneMap 1)" );
	r_colorGrading = R_Cvar( "r_colorGrading", "0", CVAR_ARCHIVE, "Color grading with a 3D LUT: 0 = off, 1 = on" );
	R_CvarRange( r_colorGrading, 0, 1, qtrue );
	r_colorGradingLUT = R_Cvar( "r_colorGradingLUT", "", CVAR_ARCHIVE, "Color grading LUT (.cube, or *identity). Overrides the LUT of the map (maps/<map>.cube), empty uses it" );
	r_colorGradingIntensity = R_Cvar( "r_colorGradingIntensity", "1", CVAR_ARCHIVE, "Color grading strength: 0 = none, 1 = full LUT" );
	R_CvarRange( r_colorGradingIntensity, 0.0f, 1.0f, qfalse );
	r_autoEmissive = R_Cvar( "r_autoEmissive", "0", CVAR_ARCHIVE, "Automatically use legacy glow and unlit additive stages as emissive sources" );
	R_CvarRange( r_autoEmissive, 0, 1, qtrue );

	r_depthPrepass = R_Cvar( "r_depthPrepass", "1", CVAR_ARCHIVE, "Depth pre-pass before the main pass (needed by SSAO, GTAO, SSR, SSGI, contact shadows, volumetric fog): 0 off, 1 on" );
	r_ssao = R_Cvar( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE, "Rend2's original screen-space ambient occlusion: 0 off, 1 on, 2 debug view" );
	r_aoMode = R_Cvar( "r_aoMode", "-1", CVAR_ARCHIVE, "Screen-space ambient occlusion: -1 = follow r_ssao (legacy), 0 = off, 1 = legacy SSAO, 2 = GTAO. Needs r_depthPrepass; turning it on from off needs a vid_restart" );
	R_CvarRange( r_aoMode, -1, 2, qtrue );
	r_aoApply = R_Cvar( "r_aoApply", "-1", CVAR_ARCHIVE, "How lighting uses AO: -1 = auto (legacy for SSAO, indirect-only for GTAO), 0 = legacy (ambient term and reflections), 1 = indirect-only (ambient, indirect share of baked light, specular occlusion)" );
	R_CvarRange( r_aoApply, -1, 1, qtrue );
	r_aoMultiBounce = R_Cvar( "r_aoMultiBounce", "1", CVAR_ARCHIVE, "Indirect-only AO: multi-bounce approximation, bright surfaces keep more light in creases" );
	R_CvarRange( r_aoMultiBounce, 0, 1, qtrue );
	r_aoLightmapFraction = R_Cvar( "r_aoLightmapFraction", "0.5", CVAR_ARCHIVE, "Indirect-only AO: share of baked lightmap / vertex lighting treated as indirect light that AO attenuates" );
	R_CvarRange( r_aoLightmapFraction, 0.0f, 1.0f, qfalse );
	r_aoSpecOcclusion = R_Cvar( "r_aoSpecOcclusion", "-1", CVAR_ARCHIVE, "Indirect-only AO: specular occlusion of environment reflections: -1 = auto (2 with GTAO bent normals, else 1), 0 = scalar AO, 1 = from AO, roughness and N.V (Lagarde), 2 = visibility cone around the bent normal against the specular lobe cone" );
	R_CvarRange( r_aoSpecOcclusion, -1, 2, qtrue );
	r_aoDebug = R_Cvar( "r_aoDebug", "0", CVAR_CHEAT, "AO debug view: 1 = legacy SSAO, 2 = raw GTAO, 3 = denoised GTAO, 4 = reconstructed normals, 5 = linear depth, 6 = contact shadows, 7 = cascade shadows, 8 = sun visibility (cascade * contact * POM self shadow), 9 = diffuse ambient visibility, 10 = AO map used by lighting, 11 = bent normals, 12 = specular occlusion" );
	R_CvarRange( r_aoDebug, 0, 12, qtrue );
	r_gtaoQuality = R_Cvar( "r_gtaoQuality", "2", CVAR_ARCHIVE, "GTAO quality: 0 = low (1 slice, 3 steps), 1 = medium (2, 4), 2 = high (3, 6), 3 = ultra (6, 8)" );
	R_CvarRange( r_gtaoQuality, 0, 3, qtrue );
	r_gtaoHalfRes = R_Cvar( "r_gtaoHalfRes", "1", CVAR_ARCHIVE | CVAR_LATCH, "GTAO resolution: 1 = half resolution (depth-aware upsampling), 0 = full resolution" );
	R_CvarRange( r_gtaoHalfRes, 0, 1, qtrue );
	r_gtaoRadius = R_Cvar( "r_gtaoRadius", "32", CVAR_ARCHIVE, "GTAO effect radius in world units" );
	R_CvarRange( r_gtaoRadius, 1.0f, 512.0f, qfalse );
	r_gtaoFalloff = R_Cvar( "r_gtaoFalloff", "0.6", CVAR_ARCHIVE, "GTAO distance falloff, as a fraction of r_gtaoRadius" );
	R_CvarRange( r_gtaoFalloff, 0.05f, 1.0f, qfalse );
	r_gtaoThickness = R_Cvar( "r_gtaoThickness", "0.2", CVAR_ARCHIVE, "GTAO thin occluder compensation: 0 = occluders are solid, higher = objects in front of a surface occlude it less (fewer halos)" );
	R_CvarRange( r_gtaoThickness, 0.0f, 4.0f, qfalse );
	r_gtaoPower = R_Cvar( "r_gtaoPower", "1.5", CVAR_ARCHIVE, "GTAO strength, exponent applied to the visibility" );
	R_CvarRange( r_gtaoPower, 0.1f, 8.0f, qfalse );
	r_gtaoDenoise = R_Cvar( "r_gtaoDenoise", "2", CVAR_ARCHIVE, "GTAO edge-aware spatial denoise passes (0-3)" );
	R_CvarRange( r_gtaoDenoise, 0, 3, qtrue );
	r_gtaoBentNormals = R_Cvar( "r_gtaoBentNormals", "1", CVAR_ARCHIVE, "GTAO bent normals for indirect light only (diffuse irradiance direction, specular occlusion cone): 0 = off, up to 1 = strength" );
	R_CvarRange( r_gtaoBentNormals, 0.0f, 1.0f, qfalse );
	r_contactShadows = R_Cvar( "r_contactShadows", "0", CVAR_ARCHIVE, "Screen-space contact shadows for the sun (needs r_sunlightMode and r_depthPrepass; turning it on from off needs a vid_restart)" );
	R_CvarRange( r_contactShadows, 0, 1, qtrue );
	r_contactShadowLength = R_Cvar( "r_contactShadowLength", "16", CVAR_ARCHIVE, "Contact shadow ray length in world units" );
	R_CvarRange( r_contactShadowLength, 1.0f, 128.0f, qfalse );
	r_contactShadowSteps = R_Cvar( "r_contactShadowSteps", "12", CVAR_ARCHIVE, "Contact shadow ray march steps" );
	R_CvarRange( r_contactShadowSteps, 2, 32, qtrue );
	r_contactShadowThickness = R_Cvar( "r_contactShadowThickness", "6", CVAR_ARCHIVE, "Assumed occluder thickness for contact shadows in world units" );
	R_CvarRange( r_contactShadowThickness, 0.1f, 64.0f, qfalse );
	r_contactShadowStrength = R_Cvar( "r_contactShadowStrength", "0.85", CVAR_ARCHIVE, "Contact shadow strength" );
	R_CvarRange( r_contactShadowStrength, 0.0f, 1.0f, qfalse );

	r_rainLens = R_Cvar( "r_rainLens", "0", CVAR_ARCHIVE | CVAR_LATCH, "Persistent water on the camera lens: rain, splashes, leaving water (needs r_hdr)" );
	R_CvarRange( r_rainLens, 0, 1, qtrue );
	r_rainLensQuality = R_Cvar( "r_rainLensQuality", "1", CVAR_ARCHIVE | CVAR_LATCH, "Lens water quality: field resolution, drop count, defocus samples (0..2)" );
	R_CvarRange( r_rainLensQuality, 0, 2, qtrue );
	r_rainLensDensity = R_Cvar( "r_rainLensDensity", "1.0", CVAR_ARCHIVE, "Lens water: rain input multiplier, 0..4" );
	r_rainLensRefraction = R_Cvar( "r_rainLensRefraction", "1.0", CVAR_ARCHIVE, "Lens water refraction strength, 0..4" );
	r_rainLensDropSize = R_Cvar( "r_rainLensDropSize", "1.0", CVAR_ARCHIVE, "Lens water drop size (geometry only, not the amount of rain), 0.25..4" );
	r_rainLensFilm = R_Cvar( "r_rainLensFilm", "1.0", CVAR_ARCHIVE, "Lens water thin film / trail visibility, 0..2 (above 1: stronger refraction only)" );
	r_rainLensFilmModel = R_Cvar( "r_rainLensFilmModel", "0", CVAR_ARCHIVE, "Lens water model: 0 = hybrid drops (A), 1 = film-first: short lived micro drops, refracting wet film (B)" );
	r_rainLensFilmMicro = R_Cvar( "r_rainLensFilmMicro", "1.0", CVAR_ARCHIVE, "Lens water film-first model: wet film micro refraction, 0..4" );
	r_rainLensEmergeWater = R_Cvar( "r_rainLensEmergeWater", "1.0", CVAR_ARCHIVE, "Lens water film-first model: water left on the lens leaving water (drop count and size), 0.25..3" );
	r_rainLensEmergeTime = R_Cvar( "r_rainLensEmergeTime", "1.0", CVAR_ARCHIVE, "Lens water film-first model: how long the film takes to drain and break into drops leaving water, 0.25..4 (below 1: sooner)" );
	r_rainLensMist = R_Cvar( "r_rainLensMist", "0.04", CVAR_ARCHIVE, "Lens water film-first model: rain reaching the lens when not looking into it (spray, mist), 0..1" );
	r_rainLensImpactSplat = R_Cvar( "r_rainLensImpactSplat", "0", CVAR_ARCHIVE, "Lens water film-first model: oblique rain impacts splat along their direction, throw spray and leave a smear, 0..2 (0 = off)" );
	r_rainLensBlur = R_Cvar( "r_rainLensBlur", "1.0", CVAR_ARCHIVE, "Lens water drop defocus, 0..2" );
	r_rainLensReflection = R_Cvar( "r_rainLensReflection", "1.0", CVAR_ARCHIVE, "Lens water reflection (environment cubemap / light grid) and light glints, 0..2; 0 = refraction only" );
	r_rainLensInertia = R_Cvar( "r_rainLensInertia", "0", CVAR_ARCHIVE, "Lens water reacts to strong camera acceleration, 0..2 (0 = off)" );
	r_rainLensDebug = R_Cvar( "r_rainLensDebug", "0", CVAR_CHEAT, "Lens water debug view (forces the pass on): 1 weight / blur, 2 normal, 3 UV offset, 5 agents, 6 film / wetness, 7 pinning, 8 transient state, 9 controller panel (before / after: r_rainLensCompare)");
	R_CvarRange( r_rainLensDebug, 0, 9, qtrue );
	r_rainLensFieldHeight = R_Cvar( "r_rainLensFieldHeight", "0", CVAR_CHEAT | CVAR_LATCH, "Lens water field height override, 0 = r_rainLensQuality" );
	r_rainLensAgentLimit = R_Cvar( "r_rainLensAgentLimit", "0", CVAR_CHEAT, "Lens water drop limit override (8..128), 0 = r_rainLensQuality" );
	r_rainLensPinning = R_Cvar( "r_rainLensPinning", "1.0", CVAR_CHEAT, "Lens water adhesion multiplier" );
	r_rainLensMerge = R_Cvar( "r_rainLensMerge", "1.0", CVAR_CHEAT, "Lens water merge distance multiplier" );
	r_rainLensFilmDecay = R_Cvar( "r_rainLensFilmDecay", "1.0", CVAR_CHEAT, "Lens water thin film lifetime multiplier" );
	r_rainLensWetDecay = R_Cvar( "r_rainLensWetDecay", "1.0", CVAR_CHEAT, "Lens water wet path lifetime multiplier" );
	r_rainLensHeavyFlow = R_Cvar( "r_rainLensHeavyFlow", "1.0", CVAR_CHEAT, "Lens water rivulet / sheet rate multiplier" );
	r_rainLensPeripheralBias = R_Cvar( "r_rainLensPeripheralBias", "1.0", CVAR_ARCHIVE, "Lens water: keep large drops, flows and sheets away from the screen centre, 0..2" );
	r_rainLensPBO = R_Cvar( "r_rainLensPBO", "0", CVAR_CHEAT, "Lens water: upload film / instances through a pixel buffer ring (profiling)" );
	r_rainLensMipBlur = R_Cvar( "r_rainLensMipBlur", "0", CVAR_CHEAT | CVAR_LATCH, "Lens water: drop defocus from a mipped half resolution scene copy instead of taps (profiling)" );

	r_motionBlur = R_Cvar( "r_motionBlur", "0", CVAR_ARCHIVE | CVAR_LATCH, "Velocity based camera and object motion blur (needs r_hdr)" );
	R_CvarRange( r_motionBlur, 0, 1, qtrue );
	r_motionBlurShutterAngle = R_Cvar( "r_motionBlurShutterAngle", "180", CVAR_ARCHIVE, "Motion blur shutter angle in degrees, relative to the r_motionBlurReferenceFps frame interval" );
	R_CvarRange( r_motionBlurShutterAngle, 0.0f, 360.0f, qfalse );
	r_motionBlurPhysicalShutter = R_Cvar( "r_motionBlurPhysicalShutter", "0", CVAR_ARCHIVE, "Use r_cameraShutterSeconds as motion-blur exposure duration in physical camera modes" );
	R_CvarRange( r_motionBlurPhysicalShutter, 0, 1, qtrue );
	r_motionBlurReferenceFps = R_Cvar( "r_motionBlurReferenceFps", "60", CVAR_ARCHIVE, "Frame rate the shutter angle refers to (frame rate independent exposure time), 0 = the actual frame interval" );
	R_CvarRange( r_motionBlurReferenceFps, 0.0f, 1000.0f, qfalse );
	r_motionBlurShutterScale = R_Cvar( "r_motionBlurShutterScale", "1", 0, "Motion blur exposure time multiplier for gameplay effects (e.g. Force Speed), set by game code" );
	R_CvarRange( r_motionBlurShutterScale, 0.0f, 4.0f, qfalse );
	r_motionBlurMaxPixels = R_Cvar( "r_motionBlurMaxPixels", "32", CVAR_ARCHIVE, "Maximum motion blur length in pixels at 1080p (scaled with the resolution)" );
	R_CvarRange( r_motionBlurMaxPixels, 1.0f, 128.0f, qfalse );
	r_motionBlurQuality = R_Cvar( "r_motionBlurQuality", "1", CVAR_ARCHIVE, "Motion blur quality: 0 = low, 1 = medium, 2 = high" );
	R_CvarRange( r_motionBlurQuality, 0, 2, qtrue );
	r_motionBlurSamples = R_Cvar( "r_motionBlurSamples", "0", CVAR_ARCHIVE, "Maximum motion blur samples per pixel, 0 = from r_motionBlurQuality" );
	R_CvarRange( r_motionBlurSamples, 0, 32, qtrue );
	r_motionBlurViewModelScale = R_Cvar( "r_motionBlurViewModelScale", "0.5", CVAR_ARCHIVE, "Motion blur strength on the first person view model" );
	R_CvarRange( r_motionBlurViewModelScale, 0.0f, 1.0f, qfalse );
	r_motionBlurCutDistance = R_Cvar( "r_motionBlurCutDistance", "256", CVAR_ARCHIVE, "Camera movement in one frame treated as a cut or teleport (resets the motion history)" );
	R_CvarRange( r_motionBlurCutDistance, 16.0f, 16384.0f, qfalse );
	r_motionBlurCutAngle = R_Cvar( "r_motionBlurCutAngle", "75", CVAR_ARCHIVE, "Camera rotation in degrees in one frame treated as a cut (resets the motion history)" );
	R_CvarRange( r_motionBlurCutAngle, 1.0f, 180.0f, qfalse );
	r_motionBlurReset = R_Cvar( "r_motionBlurReset", "0", 0, "Set to 1 by game code to reset the motion history (camera cut), cleared by the renderer" );
	r_motionBlurDebug = R_Cvar( "r_motionBlurDebug", "0", CVAR_CHEAT, "Motion blur debug view: 1 = velocity, 2 = camera velocity, 3 = object velocity, 4 = sample count, 5 = blur contribution" );
	R_CvarRange( r_motionBlurDebug, 0, 5, qtrue );

	r_ssr = R_Cvar( "r_ssr", "0", CVAR_ARCHIVE | CVAR_LATCH, "Screen-space reflections, blended with the cubemap reflections of PBR materials (needs r_specularMapping)" );
	R_CvarRange( r_ssr, 0, 1, qtrue );
	r_weatherWetness = R_Cvar( "r_weatherWetness", "0", CVAR_ARCHIVE | CVAR_LATCH, "Rain exposed surfaces of rain maps are wet (PBR roughness / albedo), uses the weather occlusion map" );
	R_CvarRange( r_weatherWetness, 0, 1, qtrue );
	r_weatherWetnessStrength = R_Cvar( "r_weatherWetnessStrength", "1", CVAR_ARCHIVE, "Wetness of fully rain exposed surfaces, 0-1" );
	r_weatherWetnessRoughness = R_Cvar( "r_weatherWetnessRoughness", "1", CVAR_ARCHIVE, "Scale of the per material wet smoothing (1 = material table, 0 = none)" );
	r_weatherWetnessDarkening = R_Cvar( "r_weatherWetnessDarkening", "1", CVAR_ARCHIVE, "Scale of the per material wet albedo darkening (1 = material table)" );
	r_weatherWetnessNormal = R_Cvar( "r_weatherWetnessNormal", "1", CVAR_ARCHIVE, "Scale of the per material wet normal flattening (1 = material table)" );
	r_weatherWetnessEntityFacing = R_Cvar( "r_weatherWetnessEntityFacing", "0.85", CVAR_ARCHIVE, "Wetness of vertical faces of characters and props (world walls: 0.5)" );
	r_weatherWetnessBias = R_Cvar( "r_weatherWetnessBias", "2", CVAR_ARCHIVE, "Rain occlusion depth bias in world units" );
	r_weatherWetnessPersistence = R_Cvar( "r_weatherWetnessPersistence", "0", CVAR_ARCHIVE | CVAR_LATCH, "Persist world wetness after rain stops (requires r_weatherWetness)" );
	R_CvarRange( r_weatherWetnessPersistence, 0, 1, qtrue );
	r_weatherWetnessAccumulationRate = R_Cvar( "r_weatherWetnessAccumulationRate", "0.20", CVAR_ARCHIVE, "Persistent world water deposited per second at full rain" );
	r_weatherWetnessDryRate = R_Cvar( "r_weatherWetnessDryRate", "0.02", CVAR_ARCHIVE, "Persistent world water removed per second (0.02 = about 50 seconds)" );
	r_weatherWetnessUpdateHz = R_Cvar( "r_weatherWetnessUpdateHz", "10", CVAR_ARCHIVE, "Persistent world wetness simulation updates per second" );
	r_weatherWetnessResolution = R_Cvar( "r_weatherWetnessResolution", "256", CVAR_ARCHIVE | CVAR_LATCH, "Persistent world wetness texture resolution (64-1024)" );
	r_weatherWetnessSteepDryScale = R_Cvar( "r_weatherWetnessSteepDryScale", "2", CVAR_ARCHIVE, "Drying multiplier for steep surfaces (selected from geometric normal)" );
	r_weatherWetnessAcidAccumulationScale = R_Cvar( "r_weatherWetnessAcidAccumulationScale", "1", CVAR_ARCHIVE, "Acid-rain persistent wetness accumulation multiplier" );
	r_weatherWetnessAcidDryScale = R_Cvar( "r_weatherWetnessAcidDryScale", "1", CVAR_ARCHIVE, "Acid-rain persistent wetness drying multiplier" );
	r_weatherWetnessPersistenceDebug = R_Cvar( "r_weatherWetnessPersistenceDebug", "0", CVAR_CHEAT, "1 shows persistent world water amount" );
	r_acidRainVisuals = R_Cvar( "r_acidRainVisuals", "0", CVAR_ARCHIVE, "Coherent optical acid-rain profile for rain, wet surfaces, puddles, runoff, volume and lens water" );
	R_CvarRange( r_acidRainVisuals, 0, 1, qtrue );
	r_acidRainTint = R_Cvar( "r_acidRainTint", "0.78 1.00 0.62", CVAR_ARCHIVE, "Linear RGB optical tint of acid rain" );
	r_acidRainStrength = R_Cvar( "r_acidRainStrength", "1", CVAR_ARCHIVE, "Strength of the coherent acid-rain optical profile" );
	R_CvarRange( r_acidRainStrength, 0.0f, 2.0f, qfalse );
	r_weatherSurfaceDebug = R_Cvar( "r_weatherSurfaceDebug", "0", CVAR_CHEAT, "Weather surface debug view: 1 rain exposure, 2 wetness mask (magenta: excluded), 3 effective roughness, 5 puddle slope, 6 puddle noise, 7 exposure x slope, 8 puddle mask, 9 puddle roughness, 10 puddle eligibility, 11 material height (magenta: none), 12 height low / high, 13 macro puddle mask, 14 micro depression mask, 15 combined puddle, 16 wet material class, 17 ripple height, 18 ripple normal offset, 19 ripple masking, 20 final normal, 21 geometric normal, 22 runoff slope class, 23 projected gravity, 24 runoff mask, 25 runoff flow field, 26 runoff roughness, 27 material weather on / off, 28 weatherResponse wetness, 29 puddle, 30 runoff scale, 31 exclusion reason, 33 puddle regions, 34 edge roughness, 35 edge darkening, 36 edge final normal (dry / wet: r_weatherWetnessCompare)");
	r_weatherPuddles = R_Cvar( "r_weatherPuddles", "0", CVAR_ARCHIVE, "Procedural puddles on flat rain exposed world surfaces (needs r_weatherWetness)" );
	r_weatherPuddleCoverage = R_Cvar( "r_weatherPuddleCoverage", "0.35", CVAR_ARCHIVE, "Fraction of flat exposed area covered by puddles, 0-1" );
	r_weatherPuddleRoughness = R_Cvar( "r_weatherPuddleRoughness", "0.04", CVAR_ARCHIVE, "Roughness of the puddle water surface" );
	r_weatherPuddleSlope = R_Cvar( "r_weatherPuddleSlope", "0.90 0.98", CVAR_ARCHIVE, "Geometric normal z range where puddles fade in: min max" );
	r_weatherPuddleScale = R_Cvar( "r_weatherPuddleScale", "192", CVAR_ARCHIVE, "World size of the puddle noise pattern" );
	r_weatherPuddleEdgeModel = R_Cvar( "r_weatherPuddleEdgeModel", "0", CVAR_ARCHIVE, "Layered dry, damp fringe, thin film and standing water puddle response" );
	r_weatherPuddleFringeDarkening = R_Cvar( "r_weatherPuddleFringeDarkening", "0.18", CVAR_ARCHIVE, "Additional albedo darkening at the damp puddle fringe (0..1)" );
	r_weatherPuddleFilmRoughness = R_Cvar( "r_weatherPuddleFilmRoughness", "0.18", CVAR_ARCHIVE, "Target roughness of the thin puddle film (0.02..1)" );
	r_weatherPuddleUseHeightMap = R_Cvar( "r_weatherPuddleUseHeightMap", "1", CVAR_ARCHIVE, "Puddles follow the height map of parallax (normalHeightMap) materials: water fills cracks and low areas first" );
	r_weatherPuddleHeightSoftness = R_Cvar( "r_weatherPuddleHeightSoftness", "0.08", CVAR_ARCHIVE, "Water line transition width of height aware puddles, in 0..1 material height" );
	r_weatherPuddleWaterLevelBias = R_Cvar( "r_weatherPuddleWaterLevelBias", "0", CVAR_ARCHIVE, "Static water level bias of height aware puddles: > 0 floods more of the relief, < 0 only the deepest cracks" );
	r_weatherPuddleRipples = R_Cvar( "r_weatherPuddleRipples", "0", CVAR_ARCHIVE, "Puddle ripples: 0 off, 1 procedural, 2 real rain impacts, 3 hybrid real near/procedural far" );
	r_weatherPuddleRippleStrength = R_Cvar( "r_weatherPuddleRippleStrength", "0.35", CVAR_ARCHIVE, "Peak slope of a puddle ripple ring (0..2)" );
	r_weatherPuddleRippleScale = R_Cvar( "r_weatherPuddleRippleScale", "20", CVAR_ARCHIVE, "World size of a ripple cell, a ring grows to 0.3 x this radius" );
	r_weatherPuddleRippleRate = R_Cvar( "r_weatherPuddleRippleRate", "1.1", CVAR_ARCHIVE, "Ripple rings per second per cell" );
	r_weatherPuddleRippleImpulse = R_Cvar( "r_weatherPuddleRippleImpulse", "0.45", CVAR_ARCHIVE, "Velocity impulse deposited by each real rain impact" );
	r_weatherPuddleRippleDamping = R_Cvar( "r_weatherPuddleRippleDamping", "2.5", CVAR_ARCHIVE, "Real puddle ripple damping per second" );
	r_weatherPuddleRipplePropagation = R_Cvar( "r_weatherPuddleRipplePropagation", "10", CVAR_ARCHIVE, "Real puddle ripple propagation in field texels per second" );
	r_weatherPuddleRippleResolution = R_Cvar( "r_weatherPuddleRippleResolution", "256", CVAR_ARCHIVE | CVAR_LATCH, "Real puddle ripple field resolution (64..1024; vid_restart)" );
	r_weatherPuddleRippleFieldScale = R_Cvar( "r_weatherPuddleRippleFieldScale", "1", CVAR_ARCHIVE, "World coverage of the real ripple field relative to the weather map" );
	r_weatherPuddleRippleDistance = R_Cvar( "r_weatherPuddleRippleDistance", "1500", CVAR_ARCHIVE, "Real ripple draw distance; hybrid fades to procedural beyond it" );
	r_weatherPuddleRippleDebug = R_Cvar( "r_weatherPuddleRippleDebug", "0", CVAR_CHEAT, "Real ripple debug: 1 impulses, 2 height, 3 velocity, 4 final normal" );
	r_waterRainRipples = R_Cvar( "r_waterRainRipples", "1", CVAR_ARCHIVE, "Rain disturbances on classified real water bodies" );
	R_CvarRange( r_waterRainRipples, 0, 1, qtrue );
	r_waterRainRippleDensity = R_Cvar( "r_waterRainRippleDensity", "0.22", CVAR_ARCHIVE, "Statistical fraction of real-water rain impacts injected as explicit ripples" );
	R_CvarRange( r_waterRainRippleDensity, 0.0f, 1.0f, qfalse );
	r_waterRainRippleImpulse = R_Cvar( "r_waterRainRippleImpulse", "0.55", CVAR_ARCHIVE, "Velocity impulse scale of explicit real-water rain ripples" );
	R_CvarRange( r_waterRainRippleImpulse, 0.0f, 8.0f, qfalse );
	r_waterRainRippleMicro = R_Cvar( "r_waterRainRippleMicro", "1", CVAR_ARCHIVE, "Aggregate rain micro-disturbance scale on real water" );
	R_CvarRange( r_waterRainRippleMicro, 0.0f, 4.0f, qfalse );
	r_waterRainRippleQuality = R_Cvar( "r_waterRainRippleQuality", "1", CVAR_ARCHIVE | CVAR_LATCH, "Real-water rain field quality: 0=128, 1=256, 2=512 (vid_restart)" );
	R_CvarRange( r_waterRainRippleQuality, 0, 2, qtrue );
	r_waterRainRippleDebug = R_Cvar( "r_waterRainRippleDebug", "0", CVAR_CHEAT, "Water rain debug: 1 impact mask, 2 source texture, 3 sparse field, 4 aggregate energy" );
	R_CvarRange( r_waterRainRippleDebug, 0, 4, qtrue );
	r_weatherRunoff = R_Cvar( "r_weatherRunoff", "0", CVAR_ARCHIVE, "Gravity driven water film / streaks on rain exposed slopes and walls (needs r_weatherWetness)" );
	r_weatherRunoffStrength = R_Cvar( "r_weatherRunoffStrength", "1", CVAR_ARCHIVE, "Runoff film strength" );
	r_weatherRunoffSpeed = R_Cvar( "r_weatherRunoffSpeed", "24", CVAR_ARCHIVE, "Runoff flow speed, world units per second" );
	r_weatherRunoffScale = R_Cvar( "r_weatherRunoffScale", "48", CVAR_ARCHIVE, "Runoff streak cell size, world units" );
	r_weatherRunoffProbe = R_Cvar( "r_weatherRunoffProbe", "1.5", CVAR_ARCHIVE, "Wall rain exposure probe distance in front of steep faces, weather map texels (0 off, max 32 units)" );
	r_weatherRunoffEntities = R_Cvar( "r_weatherRunoffEntities", "0", CVAR_ARCHIVE, "Runoff also on characters, props and movers (pattern glued to the entity)" );
	r_weatherRunoffTrailPersistence = R_Cvar( "r_weatherRunoffTrailPersistence", "0", CVAR_ARCHIVE, "Seconds world runoff trails remain dark and smooth after water passes (0 off; needs persistent wetness)" );
	R_CvarRange( r_weatherRunoffTrailPersistence, 0, 600, qfalse );
	r_weatherRunoffTrailDebug = R_Cvar( "r_weatherRunoffTrailDebug", "0", CVAR_CHEAT, "Persistent world runoff trail debug: 1 retained history, 2 final trail mask" );
	R_CvarRange( r_weatherRunoffTrailDebug, 0, 2, qtrue );
	r_ssrQuality = R_Cvar( "r_ssrQuality", "1", CVAR_ARCHIVE, "SSR quality: 0 = low, 1 = medium, 2 = high, 3 = ultra" );
	R_CvarRange( r_ssrQuality, 0, 3, qtrue );
	r_ssrSteps = R_Cvar( "r_ssrSteps", "0", CVAR_ARCHIVE, "SSR ray march steps, 0 = from r_ssrQuality" );
	R_CvarRange( r_ssrSteps, 0, 256, qtrue );
	r_ssrRefineSteps = R_Cvar( "r_ssrRefineSteps", "0", CVAR_ARCHIVE, "SSR binary search steps after a ray crossing, 0 = from r_ssrQuality" );
	R_CvarRange( r_ssrRefineSteps, 0, 16, qtrue );
	r_ssrMaxDistance = R_Cvar( "r_ssrMaxDistance", "1024", CVAR_ARCHIVE, "SSR maximum ray length in world units (shortened on rough surfaces)" );
	R_CvarRange( r_ssrMaxDistance, 16.0f, 16384.0f, qfalse );
	r_ssrThickness = R_Cvar( "r_ssrThickness", "8", CVAR_ARCHIVE, "SSR assumed thickness of the depth buffer surfaces in world units (grows with the distance)" );
	R_CvarRange( r_ssrThickness, 0.5f, 256.0f, qfalse );
	r_ssrMaxRoughness = R_Cvar( "r_ssrMaxRoughness", "0.6", CVAR_ARCHIVE, "Rougher surfaces only use the cubemap reflection (SSR fades out towards this roughness)" );
	R_CvarRange( r_ssrMaxRoughness, 0.05f, 1.0f, qfalse );
	r_ssrRoughOcclusion = R_Cvar( "r_ssrRoughOcclusion", "1", CVAR_ARCHIVE | CVAR_LATCH, "SSR on rough surfaces keeps the reflection occlusion of the cubemap it replaces (lightmap luminance scale and specular occlusion): 0 = off (SSR unoccluded, light ghosts in baked shadow), 1 = fades in from roughness 0.15 to 0.45" );
	r_ssrEdgeFade = R_Cvar( "r_ssrEdgeFade", "0.1", CVAR_ARCHIVE, "SSR fade out band at the screen edges, fraction of the view size" );
	R_CvarRange( r_ssrEdgeFade, 0.0f, 0.5f, qfalse );
	r_ssrHalfRes = R_Cvar( "r_ssrHalfRes", "-1", CVAR_ARCHIVE, "SSR rays at half resolution: -1 = from r_ssrQuality, 0 = full, 1 = half" );
	R_CvarRange( r_ssrHalfRes, -1, 1, qtrue );
	r_ssrHiZ = R_Cvar( "r_ssrHiZ", "-1", CVAR_ARCHIVE, "SSR hierarchical depth tracing: -1 = from r_ssrQuality, 0 = off, 1 = on" );
	R_CvarRange( r_ssrHiZ, -1, 1, qtrue );
	r_ssrTemporal = R_Cvar( "r_ssrTemporal", "0", CVAR_ARCHIVE | CVAR_LATCH, "SSR temporal accumulation (reprojected history, uses the velocity buffer)" );
	R_CvarRange( r_ssrTemporal, 0, 1, qtrue );
	r_ssrTemporalWeight = R_Cvar( "r_ssrTemporalWeight", "0.9", CVAR_ARCHIVE, "SSR history weight of the temporal accumulation" );
	R_CvarRange( r_ssrTemporalWeight, 0.0f, 0.98f, qfalse );
	r_ssrBlendStrength = R_Cvar( "r_ssrBlendStrength", "1", CVAR_ARCHIVE, "SSR confidence scale: 0 = cubemap reflections only, 1 = full SSR where it is reliable" );
	R_CvarRange( r_ssrBlendStrength, 0.0f, 1.0f, qfalse );
	r_ssrDebug = R_Cvar( "r_ssrDebug", "0", CVAR_CHEAT, "SSR debug view: 1 = material normal, 2 = roughness, 3 = specular reflectance, 4 = ray hit/miss, 5 = hit distance, 6 = confidence, 7 = raw SSR, 8 = cubemap reflection, 9 = hybrid reflection, 10 = replaced part (SSR - cubemap), 11 = light saber / effect reflections, 12 = hit cache (green = reused, red = traced)" );
	R_CvarRange( r_ssrDebug, 0, 12, qtrue );
	r_ssrEmitters = R_Cvar( "r_ssrEmitters", "1", CVAR_ARCHIVE, "SSR: reflect light sabers, blaster bolts and other additive effect primitives (analytic, also off screen)" );
	R_CvarRange( r_ssrEmitters, 0, 1, qtrue );
	r_ssrEmitterIntensity = R_Cvar( "r_ssrEmitterIntensity", "1", CVAR_ARCHIVE, "SSR: brightness of the light saber / effect reflections" );
	R_CvarRange( r_ssrEmitterIntensity, 0.0f, 16.0f, qfalse );
	r_ssrEmitterMaxRoughness = R_Cvar( "r_ssrEmitterMaxRoughness", "0.35", CVAR_ARCHIVE, "SSR: rougher surfaces do not reflect light sabers / effects (their dynamic light highlight already does)" );
	R_CvarRange( r_ssrEmitterMaxRoughness, 0.05f, 1.0f, qfalse );
	r_ssrHitCache = R_Cvar( "r_ssrHitCache", "0", CVAR_ARCHIVE, "SSR: reuse validated hits (needs depth prepass, Hi-Z); refresh every 4 frames per 8x8 tile, all rotating half-res phases within 16. Off by default: validating costs about as much as the march" );
	R_CvarRange( r_ssrHitCache, 0, 1, qtrue );
	r_ssrResolveCache = R_Cvar( "r_ssrResolveCache", "1", CVAR_ARCHIVE, "SSR: resolve half-res rays to radiance once per frame, then upsample (0 = per-pixel resolve)" );
	R_CvarRange( r_ssrResolveCache, 0, 1, qtrue );
	r_ssrReceiverCull = R_Cvar( "r_ssrReceiverCull", "-1", CVAR_ARCHIVE, "SSR: classify the pixels that need a ray in an early depth pass (1), or discard the others in the trace itself (0); -1 = automatic: classify only while under ~15% of the trace texels need a ray" );
	R_CvarRange( r_ssrReceiverCull, -1, 1, qtrue );
	r_ssrFusedResolve = R_Cvar( "r_ssrFusedResolve", "1", CVAR_ARCHIVE, "SSR: without temporal accumulation the composite resolves the rays itself instead of a separate full resolution resolve pass (0 = separate pass)" );
	R_CvarRange( r_ssrFusedResolve, 0, 1, qtrue );
	r_ssrFootprintDepth = R_Cvar( "r_ssrFootprintDepth", "1", CVAR_ARCHIVE, "SSR: rough reflections do not blur surfaces in front of the hit into it (Hi-Z closest depth of the blur footprint, needs Hi-Z tracing)" );
	R_CvarRange( r_ssrFootprintDepth, 0, 1, qtrue );
	r_ssrStochastic = R_Cvar( "r_ssrStochastic", "0", CVAR_ARCHIVE | CVAR_LATCH, "SSR: stochastic GGX reflections, one ray per pixel sampled from the roughness lobe, reused by the neighbours and averaged over the frames, instead of the mirror ray with a cone blur (needs r_ssrTemporal 1, vid_restart)" );
	R_CvarRange( r_ssrStochastic, 0, 1, qtrue );
	r_ssrReflectedFog = R_Cvar( "r_ssrReflectedFog", "1", CVAR_ARCHIVE | CVAR_LATCH, "SSR: fog along the reflected ray, the froxel fog (r_volumetricFog 2) between the reflecting surface and what it reflects, marched through the froxel media (needs vid_restart)" );
	R_CvarRange( r_ssrReflectedFog, 0, 1, qtrue );

	r_ssgi = R_Cvar( "r_ssgi", "0", CVAR_ARCHIVE | CVAR_LATCH, "Screen-space diffuse GI: bounces dynamic light (sabers, blasters, explosions) and emission, not the baked lighting (needs vid_restart)" );
	R_CvarRange( r_ssgi, 0, 1, qtrue );
	r_ssgiSource = R_Cvar( "r_ssgiSource", "0", CVAR_ARCHIVE, "SSGI source: 0 = dynamic lights + emissive (recommended), 1 = emissive only, 2 = full scene (EXPERIMENTAL: bounces baked lighting twice)" );
	R_CvarRange( r_ssgiSource, 0, 2, qtrue );
	r_ssgiIntensity = R_Cvar( "r_ssgiIntensity", "1", CVAR_ARCHIVE, "SSGI indirect light scale (1 = physically based diffuse bounce, 0 = off)" );
	R_CvarRange( r_ssgiIntensity, 0.0f, 8.0f, qfalse );
	r_ssgiQuality = R_Cvar( "r_ssgiQuality", "1", CVAR_ARCHIVE, "SSGI quality: 0 = low, 1 = medium, 2 = high, 3 = ultra (rays, steps, resolution, denoise; not the intensity)" );
	R_CvarRange( r_ssgiQuality, 0, 3, qtrue );
	r_ssgiRays = R_Cvar( "r_ssgiRays", "0", CVAR_ARCHIVE, "SSGI rays per traced pixel and frame, 0 = from r_ssgiQuality" );
	R_CvarRange( r_ssgiRays, 0, 8, qtrue );
	r_ssgiSteps = R_Cvar( "r_ssgiSteps", "0", CVAR_ARCHIVE, "SSGI ray march steps (Hi-Z: iterations / 3), 0 = from r_ssgiQuality" );
	R_CvarRange( r_ssgiSteps, 0, 256, qtrue );
	r_ssgiMaxDistance = R_Cvar( "r_ssgiMaxDistance", "256", CVAR_ARCHIVE, "SSGI maximum ray length in world units" );
	R_CvarRange( r_ssgiMaxDistance, 16.0f, 4096.0f, qfalse );
	r_ssgiThickness = R_Cvar( "r_ssgiThickness", "12", CVAR_ARCHIVE, "SSGI assumed thickness of the depth buffer surfaces in world units (grows with the distance)" );
	R_CvarRange( r_ssgiThickness, 0.5f, 256.0f, qfalse );
	r_ssgiTemporal = R_Cvar( "r_ssgiTemporal", "1", CVAR_ARCHIVE, "SSGI temporal accumulation (reprojected, validated and clamped history)" );
	R_CvarRange( r_ssgiTemporal, 0, 1, qtrue );
	r_ssgiHistoryWeight = R_Cvar( "r_ssgiHistoryWeight", "0.9", CVAR_ARCHIVE, "SSGI maximum history weight of the temporal accumulation" );
	R_CvarRange( r_ssgiHistoryWeight, 0.0f, 0.98f, qfalse );
	r_ssgiDenoise = R_Cvar( "r_ssgiDenoise", "-1", CVAR_ARCHIVE, "SSGI edge-aware denoise passes (0-4), -1 = from r_ssgiQuality" );
	R_CvarRange( r_ssgiDenoise, -1, 4, qtrue );
	r_ssgiHalfRes = R_Cvar( "r_ssgiHalfRes", "-1", CVAR_ARCHIVE, "SSGI rays at half resolution: -1 = from r_ssgiQuality, 0 = full, 1 = half (applied at vid_restart)" );
	R_CvarRange( r_ssgiHalfRes, -1, 1, qtrue );
	r_ssgiHiZ = R_Cvar( "r_ssgiHiZ", "-1", CVAR_ARCHIVE, "SSGI hierarchical depth tracing: -1 = from r_ssgiQuality, 0 = off, 1 = on" );
	R_CvarRange( r_ssgiHiZ, -1, 1, qtrue );
	r_ssgiEmissiveScale = R_Cvar( "r_ssgiEmissiveScale", "1", CVAR_ARCHIVE, "SSGI scale of explicit emissive materials (emissiveMap / emissiveColor / emissiveScale) as a light source" );
	R_CvarRange( r_ssgiEmissiveScale, 0.0f, 16.0f, qfalse );
	r_ssgiGlowScale = R_Cvar( "r_ssgiGlowScale", "0", CVAR_ARCHIVE, "SSGI scale of legacy glow / r_autoEmissive stages as a light source (no physical intensity, 0 = off)" );
	R_CvarRange( r_ssgiGlowScale, 0.0f, 4.0f, qfalse );
	r_ssgiDebug = R_Cvar( "r_ssgiDebug", "0", CVAR_CHEAT, "SSGI debug view: 1 = ray hit mask, 2 = hit distance, 3 = raw one frame GI, 4 = temporal GI, 5 = history weight, 6 = denoised GI, 7 = dynamic light source, 8 = emissive source, 9 = final indirect light, 10 = receiver albedo" );
	R_CvarRange( r_ssgiDebug, 0, 10, qtrue );
	r_ssgiFreezeHistory = R_Cvar( "r_ssgiFreezeHistory", "0", CVAR_CHEAT, "SSGI debug: stop updating the temporal history" );
	R_CvarRange( r_ssgiFreezeHistory, 0, 1, qtrue );

	r_skinSSS = R_Cvar( "r_skinSSS", "0", CVAR_ARCHIVE | CVAR_LATCH, "Skin scattering of surfaces classified as skin: 0 = off, 1 = cheap wrapped diffuse (an approximation, not SSS), 2 = screen-space diffusion of the skin diffuse light (needs r_hdr, vid_restart)" );
	R_CvarRange( r_skinSSS, 0, 2, qtrue );
	r_skinSSSMixedHeads = R_Cvar( "r_skinSSSMixedHeads", "0", CVAR_ARCHIVE | CVAR_LATCH, "Skin scattering also on *_head textures, which mix hair, ears and neck in one texture (vid_restart)" );
	R_CvarRange( r_skinSSSMixedHeads, 0, 1, qtrue );
	r_skinSSSStrength = R_Cvar( "r_skinSSSStrength", "1", CVAR_ARCHIVE, "Skin scattering: share of the skin diffuse light replaced by its diffused copy (r_skinSSS 2)" );
	R_CvarRange( r_skinSSSStrength, 0.0f, 1.0f, qfalse );
	r_skinSSSWidth = R_Cvar( "r_skinSSSWidth", "8", CVAR_ARCHIVE, "Skin scattering: diffusion radius in millimeters, 8 = the physical skin profile, larger values widen the whole profile (1 map unit = 28 mm), r_skinSSS 2" );
	R_CvarRange( r_skinSSSWidth, 0.0f, 40.0f, qfalse );
	r_skinSSSQuality = R_Cvar( "r_skinSSSQuality", "1", CVAR_ARCHIVE, "Skin scattering: diffusion taps per pass, 0 = 11, 1 = 17, 2 = 25" );
	R_CvarRange( r_skinSSSQuality, 0, 2, qtrue );
	r_skinSSSWrap = R_Cvar( "r_skinSSSWrap", "0.3", CVAR_ARCHIVE, "Skin scattering, r_skinSSS 1: red wrap width of the wrapped diffuse (green 0.45x, blue 0.25x)" );
	R_CvarRange( r_skinSSSWrap, 0.0f, 1.0f, qfalse );
	r_skinSSSFollowSurface = R_Cvar( "r_skinSSSFollowSurface", "1", CVAR_ARCHIVE, "Skin scattering: how strongly depth and normal differences stop the diffusion (0 = plain profile blur inside the skin mask)" );
	R_CvarRange( r_skinSSSFollowSurface, 0.0f, 4.0f, qfalse );
	r_skinSSSTransmission = R_Cvar( "r_skinSSSTransmission", "0", CVAR_ARCHIVE, "Skin scattering: optional cheap back light transmission (ears, fingers) of the directed and dynamic lights, 0 = off" );
	R_CvarRange( r_skinSSSTransmission, 0.0f, 2.0f, qfalse );
	r_skinSSSDebug = R_Cvar( "r_skinSSSDebug", "0", CVAR_CHEAT, "Skin scattering debug view: 1 = classification, 2 = skin mask, 3 = raw skin diffuse, 4 = horizontal blur, 5 = vertical blur, 6 = final delta, 7 = all but the skin diffuse, 8 = kernel radius (pixels)" );
	R_CvarRange( r_skinSSSDebug, 0, 8, qtrue );
	r_autoPBR = R_Cvar( "r_autoPBR", "0", CVAR_ARCHIVE, "PBR parameters of legacy materials without authored specular / packed maps: 0 = current rend2 fallback, 1 = generic dielectric, 2 = heuristic material classes" );
	R_CvarRange( r_autoPBR, 0, 2, qtrue );
	r_autoPBRDebug = R_Cvar( "r_autoPBRDebug", "0", CVAR_CHEAT, "Auto PBR debug view: 1 = material class, 2 = parameter source (authored / auto), 3 = roughness (generated variation in grey, blue = not auto PBR)" );
	R_CvarRange( r_autoPBRDebug, 0, 3, qtrue );
	r_materialModelDebug = R_Cvar( "r_materialModelDebug", "0", CVAR_CHEAT, "Hair/eye debug: 1 = type, 2 = tangent, 3 = bitangent, 4 = anisotropy direction, 5 = hair specular, 6 = eye specular, 7 = mask" );
	R_CvarRange( r_materialModelDebug, 0, 7, qtrue );
	r_materialModels = R_Cvar( "r_materialModels", "0", CVAR_ARCHIVE, "Hair and eye shading of materials with materialType hair / eye and of the stock hair surfaces: 0 off (Rend2's standard material), 1 on" );
	R_CvarRange( r_materialModels, 0, 1, qtrue );
	r_autoFoliage = R_Cvar( "r_autoFoliage", "0", CVAR_ARCHIVE,
		"MD3 foliage semantics: 0 = off, 1 = conservative stock-safe, 2 = broader experimental" );
	R_CvarRange( r_autoFoliage, 0, 2, qtrue );
	r_autoFoliageDebug = R_Cvar( "r_autoFoliageDebug", "0", CVAR_CHEAT,
		"Auto foliage colors: 1 = classified surfaces, 2 = also uncertain candidates" );
	R_CvarRange( r_autoFoliageDebug, 0, 2, qtrue );
	r_grassCardMode = R_Cvar( "r_grassCardMode", "0", CVAR_ARCHIVE,
		"Vegetation surface sprites as world stable cards: 0 = legacy billboards, 1 = two card cross, 2 = three card tuft, 3 = adaptive (three near, two far)" );
	R_CvarRange( r_grassCardMode, 0, 3, qtrue );
	r_grassCardDebug = R_Cvar( "r_grassCardDebug", "0", CVAR_CHEAT,
		"Auto grass debug: 1 = color by card, 2 = color by card direction, 3/4/5 = force 1/2/3 cards, 6 = color by lod (green 3, orange 2 cards)" );
	R_CvarRange( r_grassCardDebug, 0, 6, qtrue );
	r_grassCardLodDist = R_Cvar( "r_grassCardLodDist", "600", CVAR_ARCHIVE,
		"r_grassCardMode 3: distance where the third card starts to fade out" );
	R_CvarRange( r_grassCardLodDist, 16, 16384, qfalse );
	r_grassCardWidth = R_Cvar( "r_grassCardWidth", "1", CVAR_ARCHIVE,
		"r_grassCardMode: width scale of the cards" );
	R_CvarRange( r_grassCardWidth, 0.25f, 2.0f, qfalse );
	r_foliageWind = R_Cvar( "r_foliageWind", "0", CVAR_ARCHIVE,
		"Surface sprite wind: 0 = legacy circular sway, 1 = coherent breeze (gusts travelling downwind)" );
	R_CvarRange( r_foliageWind, 0, 1, qtrue );
	r_foliageWindStrength = R_Cvar( "r_foliageWindStrength", "1", CVAR_ARCHIVE,
		"r_foliageWind 1: global strength, multiplied by the ssWind / ssWindIdle of the material" );
	R_CvarRange( r_foliageWindStrength, 0.0f, 4.0f, qfalse );
	r_foliageWindSpeed = R_Cvar( "r_foliageWindSpeed", "1", CVAR_ARCHIVE,
		"r_foliageWind 1: time scale of gusts and sway" );
	R_CvarRange( r_foliageWindSpeed, 0.0f, 4.0f, qfalse );
	r_foliageWindDirection = R_Cvar( "r_foliageWindDirection", "30", CVAR_ARCHIVE,
		"r_foliageWind 1: wind yaw in degrees, 0 = +X" );
	R_CvarRange( r_foliageWindDirection, 0.0f, 360.0f, qfalse );
	r_foliageWindDebug = R_Cvar( "r_foliageWindDebug", "0", CVAR_CHEAT,
		"Foliage wind debug: 1 = exaggerated strength, 2 = color by displacement, 3 = freeze time, 4 = color by large gust wave" );
	R_CvarRange( r_foliageWindDebug, 0, 4, qtrue );
	r_leafFlutter = R_Cvar( "r_leafFlutter", "0", CVAR_ARCHIVE,
		"Leaf flutter: small coherent motion of MD3 leaf / vine cards classified by r_autoFoliage (needs r_autoFoliage >= 1); trunks and branches never move" );
	R_CvarRange( r_leafFlutter, 0, 1, qtrue );
	r_leafFlutterStrength = R_Cvar( "r_leafFlutterStrength", "1", CVAR_ARCHIVE,
		"r_leafFlutter: amplitude, 1 = 1.5 units" );
	R_CvarRange( r_leafFlutterStrength, 0.0f, 4.0f, qfalse );
	r_leafFlutterSpeed = R_Cvar( "r_leafFlutterSpeed", "1", CVAR_ARCHIVE,
		"r_leafFlutter: time scale" );
	R_CvarRange( r_leafFlutterSpeed, 0.0f, 4.0f, qfalse );
	r_leafFlutterNormal = R_Cvar( "r_leafFlutterNormal", "0.25", CVAR_ARCHIVE,
		"r_leafFlutter: normal wobble in step with the motion (per pixel lit leaves), 0 = position only" );
	R_CvarRange( r_leafFlutterNormal, 0.0f, 1.0f, qfalse );
	r_leafFlutterDebug = R_Cvar( "r_leafFlutterDebug", "0", CVAR_CHEAT,
		"Leaf flutter debug bits: 1 = x8 amplitude, 2 = freeze time, 4 = highlight fluttering surfaces, 8 = color by displacement" );
	R_CvarRange( r_leafFlutterDebug, 0, 15, qtrue );
	r_foliageInteraction = R_Cvar( "r_foliageInteraction", "0", CVAR_ARCHIVE,
		"Characters push grass (surface sprites) and FOLIAGE_PLANT models (ferns, needs r_autoFoliage) aside: player + nearest NPCs as capsules sent by cgame" );
	R_CvarRange( r_foliageInteraction, 0, 1, qtrue );
	r_foliageInteractionStrength = R_Cvar( "r_foliageInteractionStrength", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: bend strength (1 = a stem in full contact leans about 45 degrees, at most 65)" );
	R_CvarRange( r_foliageInteractionStrength, 0.0f, 4.0f, qfalse );
	r_foliageInteractionRadius = R_Cvar( "r_foliageInteractionRadius", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: scale of the collider radius (bounding box half width)" );
	R_CvarRange( r_foliageInteractionRadius, 0.5f, 3.0f, qfalse );
	r_foliageInteractionMaxInteractors = R_Cvar( "r_foliageInteractionMaxInteractors", "8", CVAR_ARCHIVE,
		"r_foliageInteraction: colliders per frame, the player first, then the nearest NPCs" );
	R_CvarRange( r_foliageInteractionMaxInteractors, 1, MAX_FOLIAGE_INTERACTORS, qtrue );
	r_foliageInteractionNPCs = R_Cvar( "r_foliageInteractionNPCs", "1", CVAR_ARCHIVE,
		"r_foliageInteraction: 0 = player only, 1 = also the nearest NPCs / other players" );
	R_CvarRange( r_foliageInteractionNPCs, 0, 1, qtrue );
	r_foliageInteractionDebug = R_Cvar( "r_foliageInteractionDebug", "0", CVAR_CHEAT,
		"Foliage interaction debug bits: 1 = draw colliders, 2 = x2 radius and strength, 4 = interaction only (no wind), 8 = grass contact heat color (plants: r_plantDebug 8), 16 = freeze colliders, 32 = player only" );
	R_CvarRange( r_foliageInteractionDebug, 0, 63, qtrue );
	r_foliageForceWaves = R_Cvar( "r_foliageForceWaves", "0", CVAR_ARCHIVE,
		"r_foliageInteraction: a Force push / pull of the player or an NPC throws the plants, grass and leaves in its cone away from / towards the caster" );
	R_CvarRange( r_foliageForceWaves, 0, 1, qtrue );
	r_foliageForceWaveStrength = R_Cvar( "r_foliageForceWaveStrength", "1", CVAR_ARCHIVE,
		"Strength of the Force push / pull through the foliage (r_foliageForceWaves), 0 = none" );
	R_CvarRange( r_foliageForceWaveStrength, 0.0f, 4.0f, qfalse );
	r_foliageSaberCut = R_Cvar( "r_foliageSaberCut", "0", CVAR_ARCHIVE,
		"An attacking saber of the player or an NPC cuts the MD3 plants (fronds, grass cards) it sweeps through: a part cut off falls, a partial cut stays (needs the plant simulation: r_autoFoliage with r_plantWind or r_foliageInteraction)" );
	R_CvarRange( r_foliageSaberCut, 0, 1, qtrue );
	r_foliageSaberCutPieces = R_Cvar( "r_foliageSaberCutPieces", "48", CVAR_ARCHIVE,
		"Pieces cut off the plants (r_foliageSaberCut) alive at most, the oldest go first; 0 = cut off parts vanish" );
	R_CvarRange( r_foliageSaberCutPieces, 0, 256, qtrue );
	r_foliageSaberCutPieceTime = R_Cvar( "r_foliageSaberCutPieceTime", "20", CVAR_ARCHIVE,
		"Seconds a piece cut off a plant lies on the ground before it sinks away" );
	R_CvarRange( r_foliageSaberCutPieceTime, 0.0f, 600.0f, qfalse );
	r_foliageBendFieldSize = R_Cvar( "r_foliageBendFieldSize", "256", CVAR_ARCHIVE | CVAR_LATCH,
		"r_foliageInteraction: texels per side of the grass bend field (64, 128 or 256)" );
	R_CvarRange( r_foliageBendFieldSize, 64, 256, qtrue );
	r_foliageBendFieldExtent = R_Cvar( "r_foliageBendFieldExtent", "1024", CVAR_ARCHIVE,
		"Grass bend field: world size of the square around the player it covers" );
	R_CvarRange( r_foliageBendFieldExtent, 256.0f, 8192.0f, qfalse );
	r_foliageBendFieldStrength = R_Cvar( "r_foliageBendFieldStrength", "1", CVAR_ARCHIVE,
		"Grass bend field: scale of the displacement the characters push the grass by (0 = no grass interaction)" );
	R_CvarRange( r_foliageBendFieldStrength, 0.0f, 4.0f, qfalse );
	r_foliageBendFieldRecoveryTime = R_Cvar( "r_foliageBendFieldRecoveryTime", "0.8", CVAR_ARCHIVE,
		"Grass bend field: seconds until grass the characters left is back to rest (swing down to ~5 %)" );
	R_CvarRange( r_foliageBendFieldRecoveryTime, 0.1f, 10.0f, qfalse );
	r_foliageBendFieldDamping = R_Cvar( "r_foliageBendFieldDamping", "0.6", CVAR_ARCHIVE,
		"Grass bend field: spring damping ratio, 1 = no swing past rest, lower = more sway" );
	R_CvarRange( r_foliageBendFieldDamping, 0.2f, 1.0f, qfalse );
	r_foliageBendFieldImpulse = R_Cvar( "r_foliageBendFieldImpulse", "1", CVAR_ARCHIVE,
		"Grass bend field: drag along the walk that running through adds" );
	R_CvarRange( r_foliageBendFieldImpulse, 0.0f, 4.0f, qfalse );
	r_foliageBendFieldDebug = R_Cvar( "r_foliageBendFieldDebug", "0", CVAR_CHEAT,
		"Grass bend field debug bits: 1 = field overlay (displacement, magnitude), 2 = covered square, 4 = freeze, 8 = x3 strength" );
	R_CvarRange( r_foliageBendFieldDebug, 0, 15, qtrue );
	r_plantWind = R_Cvar( "r_plantWind", "0", CVAR_ARCHIVE,
		"Breeze on the fronds of FOLIAGE_PLANT / GRASS models (ferns, grass cards), 0 = none; direction and speed from r_foliageWindDirection / r_foliageWindSpeed (needs r_autoFoliage)" );
	R_CvarRange( r_plantWind, 0, 1, qtrue );
	r_plantWindStrength = R_Cvar( "r_plantWindStrength", "1", CVAR_ARCHIVE, "Plant frond wind strength, 0-4 (needs r_plantWind 1)" );
	R_CvarRange( r_plantWindStrength, 0.0f, 4.0f, qfalse );
	r_plantSimDistance = R_Cvar( "r_plantSimDistance", "1500", CVAR_ARCHIVE,
		"Plants closer than this (or touched by a character) get the full frond simulation (120 Hz sub steps), farther ones one coarse step per frame" );
	R_CvarRange( r_plantSimDistance, 0.0f, 16384.0f, qfalse );
	r_plantWindDistance = R_Cvar( "r_plantWindDistance", "3000", CVAR_ARCHIVE,
		"Plants farther than this stay at rest (no wind)" );
	R_CvarRange( r_plantWindDistance, 0.0f, 32768.0f, qfalse );
	r_plantSimMaxPlants = R_Cvar( "r_plantSimMaxPlants", "96", CVAR_ARCHIVE,
		"Plants simulated per frame at most (the ones a character touches always are)" );
	R_CvarRange( r_plantSimMaxPlants, 0, 1024, qtrue );
	r_plantStiffness = R_Cvar( "r_plantStiffness", "1", CVAR_ARCHIVE,
		"Bending stiffness of the plant fronds (1 = default, higher = stiffer and faster swing); applies to plants seen after a change" );
	R_CvarRange( r_plantStiffness, 0.1f, 8.0f, qfalse );
	r_plantDebug = R_Cvar( "r_plantDebug", "0", CVAR_CHEAT,
		"Plant frond debug bits: 1 = centerlines and nodes, 2 = color by segment, 4 = color by bone, 8 = centerlines colored by contact side, 16 = print the simulation cost, 32 = saber cut blade steps (red: cut)" );
	R_CvarRange( r_plantDebug, 0, 63, qtrue );
	r_grassBendSegments = R_Cvar( "r_grassBendSegments", "4", CVAR_ARCHIVE,
		"Segments of a near grass sprite (curved blade), 1 = flat card everywhere" );
	R_CvarRange( r_grassBendSegments, 1, 8, qtrue );
	r_grassBendLodDist = R_Cvar( "r_grassBendLodDist", "800", CVAR_ARCHIVE,
		"Grass sprite surfaces closer than this are drawn as curved strips (r_grassBendSegments), farther ones as cards on the same curve" );
	R_CvarRange( r_grassBendLodDist, 0.0f, 8192.0f, qfalse );
	r_grassTrample = R_Cvar( "r_grassTrample", "0.25", CVAR_ARCHIVE,
		"r_foliageInteraction: part of the bend grass keeps after a character stood on it (slow recovery, r_grassTrampleRecovery), 0 = none" );
	R_CvarRange( r_grassTrample, 0.0f, 1.0f, qfalse );
	r_grassTrampleRecovery = R_Cvar( "r_grassTrampleRecovery", "6", CVAR_ARCHIVE,
		"Seconds trampled grass needs to stand up again" );
	R_CvarRange( r_grassTrampleRecovery, 0.5f, 60.0f, qfalse );
	r_autoPBRConvert = R_Cvar( "r_autoPBRConvert", "0", CVAR_ARCHIVE | CVAR_LATCH, "Convert legacy shaders with alphaGen lightingSpecular / tcGen environment stages (vertex lit in rend2) to per pixel lightall materials; the specular mask becomes spatial roughness / metalness" );
	R_CvarRange( r_autoPBRConvert, 0, 1, qtrue );
	r_autoPBRRoughness = R_Cvar( "r_autoPBRRoughness", "0", CVAR_ARCHIVE, "Auto PBR roughness of legacy diffuse-only materials: 0 = constant class roughness, 1 = subtle variation generated from the diffuse detail (maps are built at load, turning it on needs vid_restart)" );
	R_CvarRange( r_autoPBRRoughness, 0, 1, qtrue );
	r_diffuseBRDF = R_Cvar( "r_diffuseBRDF", "0", CVAR_ARCHIVE, "Standard PBR diffuse BRDF: 0 = Lambert, 1 = Burley/Disney" );
	R_CvarRange( r_diffuseBRDF, 0, 1, qtrue );
	r_brdfEnergyCompensation = R_Cvar( "r_brdfEnergyCompensation", "0", CVAR_ARCHIVE, "Energy-conserving GGX: multiple-scattering compensation (rough metal keeps its brightness) and the diffuse / specular energy split; 0 Rend2's original BRDF");
	R_CvarRange( r_brdfEnergyCompensation, 0, 1, qtrue );
	r_pbrEnergyDebug = R_Cvar( "r_pbrEnergyDebug", "0", CVAR_CHEAT, "PBR energy debug view: 1 diffuse, 2 specular, 3 total, 4 unreflected white-furnace energy (old/new: r_brdfEnergyCompensationCompare 1)");
	R_CvarRange(r_pbrEnergyDebug, 0, 4, qtrue);
	r_specularAA = R_Cvar( "r_specularAA", "0", CVAR_ARCHIVE | CVAR_LATCH, "Derivative and normal-mip variance filtering for GGX specular; vid_restart to compile it in" );
	R_CvarRange( r_specularAA, 0, 1, qtrue );
	r_specularAAStrength = R_Cvar( "r_specularAAStrength", "0.15", CVAR_ARCHIVE, "Specular AA normal variance scale (0..0.25)" );
	R_CvarRange( r_specularAAStrength, 0.0f, 0.25f, qfalse );
	r_specularAAMaxAdded = R_Cvar( "r_specularAAMaxAdded", "0.25", CVAR_ARCHIVE, "Maximum roughness specular AA may add (0..1)" );
	R_CvarRange( r_specularAAMaxAdded, 0.0f, 1.0f, qfalse );
	r_specularAADebug = R_Cvar( "r_specularAADebug", "0", CVAR_CHEAT, "Specular AA debug: 1 material roughness, 2 material alpha, 3 effective roughness, 4 added roughness, 5 normal variance, 6 effective alpha" );
	R_CvarRange( r_specularAADebug, 0, 6, qtrue );
	r_pbrLegacyRoughness = R_Cvar( "r_pbrLegacyRoughness", "1", CVAR_ARCHIVE, "Migrate legacy AutoPBR, spec-gloss and scalar alpha-like roughness to perceptual roughness; 0 provides an A/B strict interpretation" );
	R_CvarRange( r_pbrLegacyRoughness, 0, 1, qtrue );
	r_diffuseIBL = R_Cvar( "r_diffuseIBL", "0", CVAR_ARCHIVE | CVAR_LATCH, "Directional light-grid ambient from runtime cubemap probes" );
	R_CvarRange( r_diffuseIBL, 0, 1, qtrue );
	r_diffuseIBLStrength = R_Cvar( "r_diffuseIBLStrength", "1", CVAR_ARCHIVE, "Strength of probe directional ambient modulation" );
	R_CvarRange( r_diffuseIBLStrength, 0.0f, 1.0f, qfalse );
	r_diffuseIBLDebug = R_Cvar( "r_diffuseIBLDebug", "0", CVAR_CHEAT, "Diffuse IBL debug: 1 irradiance, 2 factor, 3 old ambient, 4 new ambient, 5 selected probe" );
	R_CvarRange( r_diffuseIBLDebug, 0, 5, qtrue );

	// Forward+ / clustered dynamic lights (tr_forwardplus.cpp), off by default
	// compiled GLSL program cache on disk (tr_glsl.cpp)
	r_shaderProgramCache = R_Cvar( "r_shaderProgramCache", "0", CVAR_ARCHIVE | CVAR_LATCH, "Keep compiled GLSL programs in glslcache/ so later starts skip compiling them (needs GL_ARB_get_program_binary)" );
	R_CvarRange( r_shaderProgramCache, 0, 1, qtrue );
	r_shaderProgramCacheMaxMB = R_Cvar( "r_shaderProgramCacheMaxMB", "512", CVAR_ARCHIVE, "Size limit of the GLSL program cache file, least recently used programs are dropped first" );
	R_CvarRange( r_shaderProgramCacheMaxMB, 16, 4096, qtrue );
	r_shaderProgramLazy = R_Cvar( "r_shaderProgramLazy", "0", CVAR_ARCHIVE | CVAR_LATCH, "Build the lightall / generic / fog / velocity / refraction / surface sprite GLSL permutations when a level or a draw first needs them instead of all at startup" );
	R_CvarRange( r_shaderProgramLazy, 0, 1, qtrue );
	r_shaderCompileProcesses = R_Cvar( "r_shaderCompileProcesses", "0", CVAR_ARCHIVE, "Link batches of GLSL programs that are not cached in this many rend2_glslc helper processes (-1 = half the CPU threads, at most 8; 0 = in the game process only)" );
	R_CvarRange( r_shaderCompileProcesses, -1, 16, qtrue );

	r_forwardPlus = R_Cvar( "r_forwardPlus", "0", CVAR_ARCHIVE, "Dynamic lights: 0 = legacy (32 lights, per surface masks), 1 = Forward+ clustered light lists (up to 256 lights)" );
	R_CvarRange( r_forwardPlus, 0, 1, qtrue );
	r_forwardPlusTileSize = R_Cvar( "r_forwardPlusTileSize", "64", CVAR_ARCHIVE, "Forward+: screen tile size in pixels" );
	R_CvarRange( r_forwardPlusTileSize, 16, 256, qtrue );
	r_forwardPlusSlices = R_Cvar( "r_forwardPlusSlices", "16", CVAR_ARCHIVE, "Forward+: logarithmic depth slices per tile (1 = 2D tiles only)" );
	R_CvarRange( r_forwardPlusSlices, 1, 64, qtrue );
	r_forwardPlusNearSlice = R_Cvar( "r_forwardPlusNearSlice", "48", CVAR_ARCHIVE, "Forward+: view depth covered by the first depth slice" );
	R_CvarRange( r_forwardPlusNearSlice, 1.0f, 1024.0f, qfalse );
	r_forwardPlusMaxLightsPerCluster = R_Cvar( "r_forwardPlusMaxLightsPerCluster", "64", CVAR_ARCHIVE, "Forward+: lights kept per cluster, the least important ones are dropped" );
	R_CvarRange( r_forwardPlusMaxLightsPerCluster, 1, 255, qtrue );
	r_forwardPlusDebug = R_Cvar( "r_forwardPlusDebug", "0", CVAR_CHEAT | CVAR_LATCH, "Forward+ debug view: 1 tiles, 2 depth slice, 3 cluster, 4 lights per cluster, 5 overflow, 6 shadowed lights, 7 unshadowed lights, 8 light spheres, 9 light r_forwardPlusDebugLight" );
	R_CvarRange( r_forwardPlusDebug, 0, 9, qtrue );
	r_forwardPlusDebugLight = R_Cvar( "r_forwardPlusDebugLight", "0", CVAR_CHEAT, "Forward+: light index shown by r_forwardPlusDebug 9" );
	r_forwardPlusMaxShadowLights = R_Cvar( "r_forwardPlusMaxShadowLights", "4", CVAR_ARCHIVE, "Forward+: dynamic lights with a shadow cube (needs r_dlightMode 2), the most important ones get them" );
	R_CvarRange( r_forwardPlusMaxShadowLights, 0, MAX_DLIGHT_SHADOWS, qtrue );
	r_forwardPlusCompute = R_Cvar( "r_forwardPlusCompute", "1", CVAR_ARCHIVE, "Forward+: 1 = cluster lists built by a GL 4.3 compute shader when available (r_gl43), 0 = built on the CPU" );
	R_CvarRange( r_forwardPlusCompute, 0, 1, qtrue );

	r_ltcAreaLights = R_Cvar( "r_ltcAreaLights", "0", CVAR_ARCHIVE | CVAR_LATCH, "LTC rectangle / line area lights (maps/<map>.arealights.json, saber lines), requires r_forwardPlus 1" );
	r_ltcDebug = R_Cvar( "r_ltcDebug", "0", CVAR_CHEAT | CVAR_LATCH, "LTC debug: 1-8 lighting, 9 static visibility, 10 cache slot, 11 saber cube, 12 saber screen, 13 confidence, 14 hybrid visibility" );
	R_CvarRange( r_ltcDebug, 0, 14, qtrue );
	r_ltcDebugLight = R_Cvar( "r_ltcDebugLight", "-1", CVAR_CHEAT, "LTC area light highlighted by r_ltcDebug (map light id, -1 = nearest)" );
	r_ltcIntensityScale = R_Cvar( "r_ltcIntensityScale", "1", CVAR_ARCHIVE, "LTC area lights other than sabers: radiance multiplier" );
	r_ltcSaberIntensityScale = R_Cvar( "r_ltcSaberIntensityScale", "1", CVAR_ARCHIVE, "LTC saber surface lighting: radiance multiplier" );
	r_ltcSaberLuminance = R_Cvar("r_ltcSaberLuminance", "40000", CVAR_ARCHIVE,
		"Physical mode: artistic saber blade luminance in cd/m^2 (nits)");
	R_CvarRange(r_ltcSaberLuminance, 0.0f, 10000000.0f, qfalse);
	r_ltcStaticDiffuse = R_Cvar( "r_ltcStaticDiffuse", "0", CVAR_ARCHIVE, "LTC area lights: also diffuse for static_specular map lights (their diffuse is usually baked in the lightmap)" );
	r_ltcMaxLights = R_Cvar( "r_ltcMaxLights", "64", CVAR_ARCHIVE, "LTC area lights: map lights per scene, nearest first" );
	R_CvarRange( r_ltcMaxLights, 0, MAX_RENDER_DLIGHTS, qtrue );
	r_ltcAutoAreaLights = R_Cvar( "r_ltcAutoAreaLights", "1", CVAR_ARCHIVE, "LTC area lights from the emissive surfaces of maps without an .arealights.json (at map load, r_ltcReloadLights): 0 off, 1 confident lamp shapes, 2 also loosely fitted ones (glow / emissive / surfacelight surfaces)" );
	r_ltcSaberAreaLights = R_Cvar( "r_ltcSaberAreaLights", "0", CVAR_ARCHIVE, "Saber lighting: 0 dynamic point; 1 LTC line for surfaces and fog; 2 LTC line for surfaces, dynamic point shadow for fog" );
	R_CvarRange( r_ltcSaberAreaLights, 0, 2, qtrue );
	r_ltcStaticShadows = R_Cvar( "r_ltcStaticShadows", "0", CVAR_ARCHIVE, "Static rectangle LTC shadows: 0 off, 1 cached world, 2 cached world plus dynamic characters" );
	R_CvarRange( r_ltcStaticShadows, 0, 2, qtrue );
	r_ltcSaberShadows = R_Cvar( "r_ltcSaberShadows", "-1", CVAR_ARCHIVE, "Saber LTC shadows: -1 midpoint cube, 0 off, 1 anisotropic cube, 2 hybrid screen space, 3 three point reference" );
	R_CvarRange( r_ltcSaberShadows, -1, 3, qtrue );

	r_normalMapping = R_Cvar( "r_normalMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable normal mapping" );
	r_specularMapping = R_Cvar( "r_specularMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable specular mapping" );
	r_deluxeMapping = R_Cvar( "r_deluxeMapping", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable reading deluxemaps when compiled with q3map2" );
	r_deluxeSpecular = R_Cvar("r_deluxeSpecular", "1", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable/scale the specular response from deluxemaps");
	r_cubeMapping = R_Cvar( "r_cubeMapping", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable cubemapping" );
	r_cubeMappingBounces = R_Cvar("r_cubeMappingBounces", "0", CVAR_ARCHIVE | CVAR_LATCH, "Renders cubemaps multiple times to get reflections in reflections");
	R_CvarRange(r_cubeMappingBounces, 0, 2, qfalse);
	r_baseNormalX = R_Cvar( "r_baseNormalX", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "Scale of the normal map X component" );
	r_baseNormalY = R_Cvar( "r_baseNormalY", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "Scale of the normal map Y component" );
	r_baseParallax = R_Cvar( "r_baseParallax", "0.05", CVAR_ARCHIVE | CVAR_LATCH, "Parallax depth of materials that don't set their own" );
   	r_baseSpecular = R_Cvar( "r_baseSpecular", "0.04", CVAR_ARCHIVE | CVAR_LATCH, "Specular reflectance (F0) of materials without a specular map" );
	r_dlightMode = R_Cvar( "r_dlightMode", "1", CVAR_ARCHIVE | CVAR_LATCH, "Dynamic lights: 0 off, 1 lights, 2 lights with shadows" );
	r_pshadowDist = R_Cvar( "r_pshadowDist", "128", CVAR_ARCHIVE, "Distance up to which projected character shadows are drawn" );
	r_imageUpsample = R_Cvar( "r_imageUpsample", "0", CVAR_ARCHIVE | CVAR_LATCH, "Upsample small textures at load: 0 off, 1 2x, 2 4x" );
	r_imageUpsampleMaxSize = R_Cvar( "r_imageUpsampleMaxSize", "1024", CVAR_ARCHIVE | CVAR_LATCH, "Largest size an upsampled texture may reach" );
	r_imageUpsampleType = R_Cvar( "r_imageUpsampleType", "1", CVAR_ARCHIVE | CVAR_LATCH, "Upsampling filter: 0 nearest, 1 bilinear, 2 bicubic" );
	r_genNormalMaps = R_Cvar( "r_genNormalMaps", "0", CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable generating normal maps from diffuse maps" );

	r_forceSun = R_Cvar( "r_forceSun", "0", CVAR_CHEAT, "Realtime sun on maps without q3gl2_sun in their sky shader: 0 off, 1 on, 2 test mode (the sun circles the sky)" );
	r_forceSunMapLightScale = R_Cvar( "r_forceSunMapLightScale", "1.0", CVAR_CHEAT, "With r_forceSun: scale of the lightmap brightness" );
	r_forceSunLightScale = R_Cvar( "r_forceSunLightScale", "1.0", CVAR_CHEAT, "With r_forceSun: scale of the sun brightness" );
	r_forceSunAmbientScale = R_Cvar( "r_forceSunAmbientScale", "0.5", CVAR_CHEAT, "With r_forceSun: brightness left in sun shadow, 0 black, 1 no shadow" );
	r_drawSunRays = R_Cvar( "r_drawSunRays", "0", CVAR_ARCHIVE | CVAR_LATCH, "Sun rays post effect: 0 off, 1 on" );
	r_sunlightMode = R_Cvar( "r_sunlightMode", "1", CVAR_ARCHIVE | CVAR_LATCH, "Realtime sun light: 0 off, 1 shadows darken the baked light, 2 full realtime sun" );

	r_volumetricFog = R_Cvar("r_volumetricFog", "0", CVAR_ARCHIVE | CVAR_LATCH, "Volumetric fog: 0 = off, 1 = light grid ray march (legacy), 2 = froxel volume with sun, dynamic lights, shadows and temporal reprojection");
	r_entityLightGrid = R_Cvar("r_entityLightGrid", "0", CVAR_ARCHIVE | CVAR_LATCH, "Entity BSP light grid: 0 = legacy, 1 = three CPU samples, 2 = per-fragment GPU samples");
	R_CvarRange(r_entityLightGrid, 0, 2, qtrue);
	r_entityLightGridDebug = R_Cvar("r_entityLightGridDebug", "0", CVAR_CHEAT | CVAR_LATCH, "Entity grid debug: 1 ambient, 2 direct, 3 direction, 4 validity, 5 cell, 6 legacy, 7 multi-point, 8 GPU, 9 difference, 10 L1 probe C0, 11 L1 probe direction (r_entityLightProbes)");
	R_CvarRange(r_entityLightGridDebug, 0, 11, qtrue);
	R_CvarRange(r_volumetricFog, 0, 2, qtrue);
	r_volumetricFogDefaultScale = R_Cvar("r_volumetricFogDefaultScale", "1.0", CVAR_ARCHIVE | CVAR_LATCH, "Scales volumetric fog density unless scale has been explicitly defined");
	r_volumetricFogSamples = R_Cvar("r_volumetricFogSamples", "48", CVAR_ARCHIVE | CVAR_LATCH, "How many ray samples to take");
	R_CvarRange(r_volumetricFogSamples, 16, 128, qfalse);
	r_volumetricFogScale = R_Cvar("r_volumetricFogScale", "1.0", CVAR_TEMP, "Temporarily scales volumetric fog density");
	r_volumetricFogQuality = R_Cvar("r_volumetricFogQuality", "1", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog (r_volumetricFog 2) preset: 0 = low, 1 = medium, 2 = high (grid scale and depth slices)");
	R_CvarRange(r_volumetricFogQuality, 0, 2, qtrue);
	r_volumetricFogGridScale = R_Cvar("r_volumetricFogGridScale", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: screen pixels per froxel, 0 = from r_volumetricFogQuality");
	R_CvarRange(r_volumetricFogGridScale, 0, 32, qtrue);
	r_volumetricFogSlices = R_Cvar("r_volumetricFogSlices", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: depth slices, 0 = from r_volumetricFogQuality");
	R_CvarRange(r_volumetricFogSlices, 0, 128, qtrue);
	r_volumetricFogFar = R_Cvar("r_volumetricFogFar", "0", CVAR_ARCHIVE, "Froxel fog: distance covered by the froxel slices, 0 = automatic (4096). Beyond it the medium of the last slice is extrapolated");
	R_CvarRange(r_volumetricFogFar, 0, 65536, qfalse);

	// long range atmosphere and aerial perspective (tr_atmosphere.cpp, docs/rend2-atmosphere.md)
	r_atmosphere = R_Cvar("r_atmosphere", "0", CVAR_ARCHIVE, "Long range atmosphere: aerial perspective of distant surfaces and the sky (Rayleigh / Mie), composited before the froxel fog. Needs a map sun (or r_forceSun)");
	r_atmosphereSky = R_Cvar("r_atmosphereSky", "0", CVAR_ARCHIVE, "Atmosphere sky: 0 overlay (skybox kept, horizon haze and sun glow), 1 blend skybox and analytic sky, 2 analytic sky (with the sun disc unless r_drawSun 1). Sky portal maps always use 0");
	R_CvarRange(r_atmosphereSky, 0, 2, qtrue);
	r_atmosphereSkyBlend = R_Cvar("r_atmosphereSkyBlend", "0.5", CVAR_ARCHIVE, "Atmosphere sky 1: share of the analytic sky");
	R_CvarRange(r_atmosphereSkyBlend, 0, 1, qfalse);
	r_atmosphereUnitScale = R_Cvar("r_atmosphereUnitScale", "0.03", CVAR_ARCHIVE, "Atmosphere: metres per world unit (0.03: the 64 unit player box is ~1.8 m)");
	R_CvarRange(r_atmosphereUnitScale, 0.001f, 10.0f, qfalse);
	r_atmosphereAerialScale = R_Cvar("r_atmosphereAerialScale", "1", CVAR_ARCHIVE, "Atmosphere: multiplier of the aerial perspective path length only (1 = physical; JA views are a few hundred metres of air, 8-30 makes the haze visible)");
	R_CvarRange(r_atmosphereAerialScale, 0.01f, 1000.0f, qfalse);
	r_atmosphereAltitude = R_Cvar("r_atmosphereAltitude", "0", CVAR_ARCHIVE, "Atmosphere: altitude of the ground in metres above sea level");
	R_CvarRange(r_atmosphereAltitude, 0, 20000, qfalse);
	r_atmosphereGroundZ = R_Cvar("r_atmosphereGroundZ", "auto", CVAR_ARCHIVE, "Atmosphere: world z of the ground (altitude 0 + r_atmosphereAltitude), auto = lowest floor of the map");
	r_atmosphereRayleigh = R_Cvar("r_atmosphereRayleigh", "1", CVAR_ARCHIVE, "Atmosphere: Rayleigh (air molecules) density multiplier");
	R_CvarRange(r_atmosphereRayleigh, 0, 100, qfalse);
	r_atmosphereMie = R_Cvar("r_atmosphereMie", "1", CVAR_ARCHIVE, "Atmosphere: Mie (aerosol, haze) density multiplier");
	R_CvarRange(r_atmosphereMie, 0, 100, qfalse);
	r_atmosphereMieG = R_Cvar("r_atmosphereMieG", "0.8", CVAR_ARCHIVE, "Atmosphere: Mie anisotropy g (Cornette-Shanks)");
	R_CvarRange(r_atmosphereMieG, -0.95f, 0.95f, qfalse);
	r_atmosphereOzone = R_Cvar("r_atmosphereOzone", "1", CVAR_ARCHIVE, "Atmosphere: ozone density multiplier (sky colour only)");
	R_CvarRange(r_atmosphereOzone, 0, 100, qfalse);
	r_atmosphereSunColor = R_Cvar("r_atmosphereSunColor", "1", CVAR_ARCHIVE, "Atmosphere sun illuminance: 0 map sun at the top of the atmosphere, 1 map sun at the ground (the air above the camera is undone), 2 white");
	R_CvarRange(r_atmosphereSunColor, 0, 2, qtrue);
	r_atmosphereSunIntensity = R_Cvar("r_atmosphereSunIntensity", "1", CVAR_ARCHIVE, "Atmosphere: sun illuminance multiplier");
	R_CvarRange(r_atmosphereSunIntensity, 0, 100, qfalse);
	r_atmosphereSunSize = R_Cvar("r_atmosphereSunSize", "0.53", CVAR_ARCHIVE, "Atmosphere: angular diameter of the sun disc in degrees");
	R_CvarRange(r_atmosphereSunSize, 0.05f, 10.0f, qfalse);
	r_atmosphereSunGlow = R_Cvar("r_atmosphereSunGlow", "1", CVAR_ARCHIVE, "Atmosphere sky 0: scale of the Mie sun glow over the skybox (lower it when the painted sun is elsewhere)");
	R_CvarRange(r_atmosphereSunGlow, 0, 10, qfalse);
	r_atmosphereStart = R_Cvar("r_atmosphereStart", "0", CVAR_ARCHIVE, "Atmosphere: distance in world units before which there is no aerial perspective");
	R_CvarRange(r_atmosphereStart, 0, 1000000, qfalse);
	r_atmosphereDebug = R_Cvar("r_atmosphereDebug", "0", CVAR_CHEAT, "Atmosphere debug view: 1 Rayleigh in-scattering, 2 Mie in-scattering, 3 transmittance, 4 aerial in-scattering, 5 atmosphere-only sky, 6 froxel (red) + atmosphere (green) opacity, 7 LUTs, 8 aerial distance bands (1, 2, 5, 10, 20 km)");
	R_CvarRange(r_atmosphereDebug, 0, 8, qtrue);
	// volumetric clouds (tr_clouds.cpp, docs/rend2-volumetric-clouds.md)
	r_clouds = R_Cvar("r_clouds", "0", CVAR_ARCHIVE | CVAR_LATCH, "Volumetric clouds: a ray marched cloud shell above the map (sun, self shadow, sky ambient), composited between the atmosphere and the froxel fog. Needs a map sun (or r_forceSun); vid_restart");
	R_CvarRange(r_clouds, 0, 1, qtrue);
	r_cloudScale = R_Cvar("r_cloudScale", "2", CVAR_ARCHIVE | CVAR_LATCH, "Clouds: march resolution divisor per axis (1, 2, 4), vid_restart");
	R_CvarRange(r_cloudScale, 1, 4, qtrue);
	r_cloudBase = R_Cvar("r_cloudBase", "1500", CVAR_ARCHIVE, "Clouds: altitude of the cloud base in metres above the ground (r_atmosphereGroundZ, r_atmosphereUnitScale)");
	R_CvarRange(r_cloudBase, 0, 20000, qfalse);
	r_cloudTop = R_Cvar("r_cloudTop", "4000", CVAR_ARCHIVE, "Clouds: altitude of the cloud tops in metres above the ground");
	R_CvarRange(r_cloudTop, 10, 30000, qfalse);
	r_cloudCoverage = R_Cvar("r_cloudCoverage", "0.5", CVAR_ARCHIVE, "Clouds: 0 clear, 0.5 the weather map, 1 overcast");
	R_CvarRange(r_cloudCoverage, 0, 1, qfalse);
	r_cloudDensity = R_Cvar("r_cloudDensity", "1", CVAR_ARCHIVE, "Clouds: extinction multiplier (1 = 60 per km in a full cloud)");
	R_CvarRange(r_cloudDensity, 0, 20, qfalse);
	r_cloudAnisotropy = R_Cvar("r_cloudAnisotropy", "0.6", CVAR_ARCHIVE, "Clouds: Henyey-Greenstein g of the forward lobe (silver lining towards the sun)");
	R_CvarRange(r_cloudAnisotropy, -0.9f, 0.95f, qfalse);
	r_cloudWindSpeed = R_Cvar("r_cloudWindSpeed", "10", CVAR_ARCHIVE, "Clouds: wind speed in metres per second");
	R_CvarRange(r_cloudWindSpeed, 0, 200, qfalse);
	r_cloudWindDir = R_Cvar("r_cloudWindDir", "45", CVAR_ARCHIVE, "Clouds: wind direction in degrees (world x axis = 0, y = 90)");
	R_CvarRange(r_cloudWindDir, -360, 360, qfalse);
	r_cloudSteps = R_Cvar("r_cloudSteps", "64", CVAR_ARCHIVE, "Clouds: max march steps per ray");
	R_CvarRange(r_cloudSteps, 8, 256, qtrue);
	r_cloudStepLength = R_Cvar("r_cloudStepLength", "150", CVAR_ARCHIVE, "Clouds: march step length in metres (the step count adapts to the path through the layer, at least 16, at most r_cloudSteps)");
	R_CvarRange(r_cloudStepLength, 5, 5000, qfalse);
	r_cloudShadowSteps = R_Cvar("r_cloudShadowSteps", "5", CVAR_ARCHIVE, "Clouds: light march steps towards the sun (self shadow); the shadow map uses twice as many");
	R_CvarRange(r_cloudShadowSteps, 1, 16, qtrue);
	r_cloudMSOctaves = R_Cvar("r_cloudMSOctaves", "3", CVAR_ARCHIVE, "Clouds: multiple scattering octaves (1 = single scattering)");
	R_CvarRange(r_cloudMSOctaves, 1, 4, qtrue);
	r_cloudDetail = R_Cvar("r_cloudDetail", "1", CVAR_ARCHIVE, "Clouds: erosion by the detail noise (0 = off, cheaper)");
	R_CvarRange(r_cloudDetail, 0, 2, qfalse);
	r_cloudAmbient = R_Cvar("r_cloudAmbient", "1", CVAR_ARCHIVE, "Clouds: sky ambient multiplier");
	R_CvarRange(r_cloudAmbient, 0, 10, qfalse);
	r_cloudTemporal = R_Cvar("r_cloudTemporal", "0.9", CVAR_ARCHIVE, "Clouds: weight of the reprojected history (0 = no temporal filter)");
	R_CvarRange(r_cloudTemporal, 0, 0.98f, qfalse);
	r_cloudMaxDistance = R_Cvar("r_cloudMaxDistance", "60", CVAR_ARCHIVE, "Clouds: march distance limit in km (faded out towards it)");
	R_CvarRange(r_cloudMaxDistance, 1, 500, qfalse);
	r_cloudShapeScale = R_Cvar("r_cloudShapeScale", "12", CVAR_ARCHIVE, "Clouds: size in km of one tile of the shape noise");
	R_CvarRange(r_cloudShapeScale, 0.05f, 1000, qfalse);
	r_cloudDetailScale = R_Cvar("r_cloudDetailScale", "1.5", CVAR_ARCHIVE, "Clouds: size in km of one tile of the detail (erosion) noise");
	R_CvarRange(r_cloudDetailScale, 0.01f, 100, qfalse);
	r_cloudWeatherScale = R_Cvar("r_cloudWeatherScale", "40", CVAR_ARCHIVE, "Clouds: size in km of one tile of the weather (coverage) map");
	R_CvarRange(r_cloudWeatherScale, 0.5f, 5000, qfalse);
	r_cloudLegacy = R_Cvar("r_cloudLegacy", "1", CVAR_ARCHIVE, "Clouds: SP fx_cloudlayer entities: 0 ignored (legacy haze drawn), 1 style hint (tube: open sky overhead, alt: thin haze), 2 literal disc / ring at the entity height (debug)");
	R_CvarRange(r_cloudLegacy, 0, 2, qtrue);
	r_cloudSkyPortal = R_Cvar("r_cloudSkyPortal", "0", CVAR_ARCHIVE, "Clouds on sky portal maps: 0 off, 1 composited over the portal scene (approximate)");
	R_CvarRange(r_cloudSkyPortal, 0, 1, qtrue);
	r_cloudShadows = R_Cvar("r_cloudShadows", "0", CVAR_ARCHIVE, "Clouds: shadows of the clouds on the world (sun of lightall, froxel fog sun)");
	R_CvarRange(r_cloudShadows, 0, 1, qtrue);
	r_cloudShadowExtent = R_Cvar("r_cloudShadowExtent", "8", CVAR_ARCHIVE, "Clouds: size in km of the cloud shadow map around the camera");
	R_CvarRange(r_cloudShadowExtent, 0.5f, 100, qfalse);
	r_cloudShadowInterval = R_Cvar("r_cloudShadowInterval", "1", CVAR_ARCHIVE, "Clouds: frames between cloud shadow map updates");
	R_CvarRange(r_cloudShadowInterval, 1, 60, qtrue);
	r_cloudDebug = R_Cvar("r_cloudDebug", "0", CVAR_CHEAT, "Clouds debug view: 1 interval, 2 max density, 3 shape noise, 4 detail noise, 5 sun transmittance, 6 phase, 7 temporal weight, 8 cloud radiance, 9 legacy mask (r) / coverage (g) / mode (b), 10 weather map, 11 cloud shadow map");
	R_CvarRange(r_cloudDebug, 0, 11, qtrue);
	r_volumetricFogAnisotropy = R_Cvar("r_volumetricFogAnisotropy", "0.2", CVAR_ARCHIVE, "Froxel fog: Henyey-Greenstein g of the sun and dynamic light scattering, 0 = isotropic, > 0 forward, < 0 backward; the default of the media without their own g (fogAnisotropy, local volume and FX anisotropy)");
	R_CvarRange(r_volumetricFogAnisotropy, -0.9f, 0.9f, qfalse);
	r_volumetricFogTemporal = R_Cvar("r_volumetricFogTemporal", "1", CVAR_ARCHIVE, "Froxel fog: temporal reprojection and jittered sampling");
	R_CvarRange(r_volumetricFogTemporal, 0, 1, qtrue);
	r_volumetricFogHistoryWeight = R_Cvar("r_volumetricFogHistoryWeight", "0.9", CVAR_ARCHIVE, "Froxel fog: weight of the reprojected history");
	R_CvarRange(r_volumetricFogHistoryWeight, 0.0f, 0.98f, qfalse);
	r_volumetricFogSunScale = R_Cvar("r_volumetricFogSunScale", "1", CVAR_ARCHIVE, "Froxel fog: sun scattering multiplier");
	R_CvarRange(r_volumetricFogSunScale, 0.0f, 16.0f, qfalse);
	r_volumetricFogDlightScale = R_Cvar("r_volumetricFogDlightScale", "1", CVAR_ARCHIVE, "Froxel fog: dynamic light scattering multiplier");
	R_CvarRange(r_volumetricFogDlightScale, 0.0f, 16.0f, qfalse);
	r_volumetricFogLightTile = R_Cvar("r_volumetricFogLightTile", "8", CVAR_NONE, "Froxel fog: froxels per side of a dynamic light tile (4, 8, 16), see r_vfogLightStats");
	R_CvarRange(r_volumetricFogLightTile, 4.0f, 16.0f, qtrue);
	r_volumetricFogLightCompute = R_Cvar("r_volumetricFogLightCompute", "1", CVAR_NONE, "Build froxel dynamic-light cluster lists on GL 4.3 compute");
	r_volumetricFogStaticScale = R_Cvar("r_volumetricFogStaticScale", "1", CVAR_ARCHIVE, "Froxel fog: baked (light grid) scattering multiplier");
	R_CvarRange(r_volumetricFogStaticScale, 0.0f, 16.0f, qfalse);
	// Canonical controls: independent build/load and opt-in consumption.
#define LIGHTRECON_GET R_Cvar
#include "tr_lightrecon_register.inc"
#undef LIGHTRECON_GET
	r_entityLightProbes = R_Cvar("r_entityLightProbes", "1", CVAR_ARCHIVE | CVAR_LATCH, "Entity light grid L1 probes with r_entityLightGrid 2: first order irradiance from the original BSP direction and ambient/directed colours; no source reconstruction (vid_restart)");
	R_CvarRange(r_entityLightProbes, 0, 1, qtrue);
	r_volumetricSelfShadow = R_Cvar("r_volumetricSelfShadow", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: dense media shadow the light inside the media (current frame density along the light ray): 0 = off, 1 = sun, 2 = sun + the r_volumetricSelfShadowMaxLights strongest dynamic lights (vid_restart)");
	R_CvarRange(r_volumetricSelfShadow, 0, 2, qtrue);
	r_volumetricSelfShadowSamples = R_Cvar("r_volumetricSelfShadowSamples", "6", CVAR_ARCHIVE, "Froxel fog self-shadow: density samples along the sun ray (dynamic lights use half, at least 3)");
	R_CvarRange(r_volumetricSelfShadowSamples, 2, 16, qtrue);
	r_volumetricSelfShadowDistance = R_Cvar("r_volumetricSelfShadowDistance", "768", CVAR_ARCHIVE, "Froxel fog self-shadow: length of the light ray march in world units (clipped to the froxel volume)");
	R_CvarRange(r_volumetricSelfShadowDistance, 32, 8192, qfalse);
	r_volumetricSelfShadowOutsideHeightFog = R_Cvar("r_volumetricSelfShadowOutsideHeightFog", "1", CVAR_ARCHIVE, "Froxel fog self-shadow: 1 = beyond the march the height fog is integrated analytically (other media there count as empty), 0 = nothing beyond the march");
	R_CvarRange(r_volumetricSelfShadowOutsideHeightFog, 0, 1, qtrue);
	r_volumetricSelfShadowMaxLights = R_Cvar("r_volumetricSelfShadowMaxLights", "2", CVAR_ARCHIVE, "Froxel fog self-shadow, r_volumetricSelfShadow 2: number of the strongest dynamic lights with a media shadow (0-4)");
	R_CvarRange(r_volumetricSelfShadowMaxLights, 0, 4, qtrue);
	// approximate multiple scattering (Wrenninge et al. 2013 octaves), docs/rend2-volumetric-fog.md
	r_volumetricMultiScatter = R_Cvar("r_volumetricMultiScatter", "0", CVAR_ARCHIVE, "Froxel fog: approximate multiple scattering of dense media, returns part of the light removed by the media self-shadow (needs r_volumetricSelfShadow): 0 = off, 1 = sun, 2 = sun + the self-shadowed dynamic lights");
	R_CvarRange(r_volumetricMultiScatter, 0, 2, qtrue);
	r_volumetricFogRGBExtinction = R_Cvar("r_volumetricFogRGBExtinction", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog: RGB extinction, media absorb the color channels differently (fogExtinctionColor, fog volume Extinction, r_volumetricFogHeightExtinction); 0 = scalar extinction (no extra memory), 1 = per channel transmittance (+24 bytes per froxel, two draw composite)");
	R_CvarRange(r_volumetricFogRGBExtinction, 0, 1, qtrue);
	r_volumetricMultiScatterOctaves = R_Cvar("r_volumetricMultiScatterOctaves", "2", CVAR_ARCHIVE, "Froxel fog multiple scattering: number of scattering octaves beyond the single scattering (1-3)");
	R_CvarRange(r_volumetricMultiScatterOctaves, 1, 3, qtrue);
	r_volumetricMultiScatterAttenuation = R_Cvar("r_volumetricMultiScatterAttenuation", "0.25", CVAR_ARCHIVE, "Froxel fog multiple scattering: optical depth scale a per octave (lower = light penetrates deeper; kept <= r_volumetricMultiScatterContribution)");
	R_CvarRange(r_volumetricMultiScatterAttenuation, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterContribution = R_Cvar("r_volumetricMultiScatterContribution", "0.5", CVAR_ARCHIVE, "Froxel fog multiple scattering: energy b per octave, multiplied by the albedo and the medium thickness");
	R_CvarRange(r_volumetricMultiScatterContribution, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterPhase = R_Cvar("r_volumetricMultiScatterPhase", "0.5", CVAR_ARCHIVE, "Froxel fog multiple scattering: anisotropy scale c per octave (0 = isotropic octaves)");
	R_CvarRange(r_volumetricMultiScatterPhase, 0.0f, 1.0f, qfalse);
	r_volumetricMultiScatterLength = R_Cvar("r_volumetricMultiScatterLength", "64", CVAR_ARCHIVE, "Froxel fog multiple scattering: typical size of a dense medium in world units; media with extinction * length << 1 (thin fog) get almost none");
	R_CvarRange(r_volumetricMultiScatterLength, 1.0f, 4096.0f, qfalse);
	r_volumetricMultiScatterShadowFill = R_Cvar("r_volumetricMultiScatterShadowFill", "0.25", CVAR_ARCHIVE, "Froxel fog multiple scattering: how far the octaves may fill the geometry shadow in thick media (0-0.5)");
	R_CvarRange(r_volumetricMultiScatterShadowFill, 0.0f, 0.5f, qfalse);
	r_volumetricFogDlightShadows = R_Cvar("r_volumetricFogDlightShadows", "1", CVAR_ARCHIVE, "Froxel fog: dynamic lights use their shadow maps (needs r_dlightMode 2)");
	R_CvarRange(r_volumetricFogDlightShadows, 0, 1, qtrue);
	r_volumetricFogBloom = R_Cvar("r_volumetricFogBloom", "0", CVAR_ARCHIVE, "Froxel fog: bright in-scattering added to the glow buffer (bloom), 0 = none");
	R_CvarRange(r_volumetricFogBloom, 0.0f, 4.0f, qfalse);
	r_volumetricEmission = R_Cvar("r_volumetricEmission", "0", CVAR_ARCHIVE, "Froxel fog: scale of the emission of local fog volumes and FX particle media (glowing gas), 0 = off");
	R_CvarRange(r_volumetricEmission, 0.0f, 16.0f, qfalse);
	// liquid media (tr_liquid.cpp): the water / slime / lava brushes of the world model
	// become media of the froxel fog, docs/rend2-volumetric-fog.md "Underwater medium"
	r_volumetricWater = R_Cvar("r_volumetricWater", "0", CVAR_ARCHIVE | CVAR_LATCH, "Froxel fog (r_volumetricFog 2): liquid brushes of the map are participating media, mask: 1 water, 2 slime, 4 lava (0 = off, the legacy look). cgame then skips the full screen tint of those liquids (r_volumetricWaterActive, cg_underwaterTint)");
	R_CvarRange(r_volumetricWater, 0, 7, qtrue);
	r_volumetricWaterActive = R_Cvar("r_volumetricWaterActive", "0", CVAR_ROM, "Set by the renderer: liquid classes drawn as a medium on this map (1 water, 2 slime, 4 lava), read by cgame");
	r_volumetricWaterSurfaces = R_Cvar("r_volumetricWaterSurfaces", "1", CVAR_ARCHIVE | CVAR_LATCH, "r_volumetricWater: the sun on surfaces inside a liquid passes the liquid above them (transmittance, caustics); needs r_sunlightMode");
	R_CvarRange(r_volumetricWaterSurfaces, 0, 1, qtrue);
	r_volumetricWaterSunPath = R_Cvar("r_volumetricWaterSunPath", "1", CVAR_ARCHIVE, "r_volumetricWater: exact path of the sunlight through the liquid to the surface (deep water is darker and bluer), and in the froxel volume the path of the dynamic lights through the liquid; 0 = unattenuated");
	R_CvarRange(r_volumetricWaterSunPath, 0, 1, qtrue);
	r_volumetricWaterCaustics = R_Cvar("r_volumetricWaterCaustics", "0.35", CVAR_ARCHIVE, "r_volumetricWater: strength of the sun caustics under water (0 = off, 1 = full pattern contrast); the average sunlight is kept");
	R_CvarRange(r_volumetricWaterCaustics, 0.0f, 1.0f, qfalse);
	r_volumetricWaterCausticScale = R_Cvar("r_volumetricWaterCausticScale", "160", CVAR_ARCHIVE, "r_volumetricWater: caustic pattern period in world units");
	R_CvarRange(r_volumetricWaterCausticScale, 8.0f, 4096.0f, qfalse);
	r_volumetricWaterCausticSpeed = R_Cvar("r_volumetricWaterCausticSpeed", "0.06", CVAR_ARCHIVE, "r_volumetricWater: caustic drift in pattern periods per second");
	R_CvarRange(r_volumetricWaterCausticSpeed, 0.0f, 4.0f, qfalse);
	r_volumetricWaterCausticFocus = R_Cvar("r_volumetricWaterCausticFocus", "48", CVAR_ARCHIVE, "r_volumetricWater: depth below the surface where the caustics reach full contrast (world units)");
	R_CvarRange(r_volumetricWaterCausticFocus, 1.0f, 4096.0f, qfalse);
	r_waterCausticsMode = R_Cvar("r_waterCausticsMode", "0", CVAR_ARCHIVE, "underwater caustics: 0 off, 1 original procedural, 2 actual water-surface driven");
	R_CvarRange(r_waterCausticsMode, 0, 2, qtrue);
	r_waterCausticsQuality = R_Cvar("r_waterCausticsQuality", "1", CVAR_ARCHIVE, "surface-driven caustic derivative quality: 0 32u, 1 16u, 2 8u base spacing");
	R_CvarRange(r_waterCausticsQuality, 0, 2, qtrue);
	r_waterCausticsMaxDepth = R_Cvar("r_waterCausticsMaxDepth", "2048", CVAR_ARCHIVE, "maximum liquid path depth for surface-driven caustic contrast");
	R_CvarRange(r_waterCausticsMaxDepth, 16.0f, 16384.0f, qfalse);
	r_waterCausticsSlope = R_Cvar("r_waterCausticsSlope", "1", CVAR_ARCHIVE, "surface slope contribution to surface-driven caustics");
	R_CvarRange(r_waterCausticsSlope, 0.0f, 4.0f, qfalse);
	r_waterCausticsFilter = R_Cvar("r_waterCausticsFilter", "1", CVAR_ARCHIVE, "receiver-depth footprint filter for surface-driven caustics");
	R_CvarRange(r_waterCausticsFilter, 0.0f, 4.0f, qfalse);
	r_waterCausticsDebug = R_Cvar("r_waterCausticsDebug", "0", CVAR_CHEAT, "surface caustics debug: 1 slope, 2 refracted ray, 3 focus, 4 weighted result, 5 projected receiver, 6 attenuation, 7 interaction");
	R_CvarRange(r_waterCausticsDebug, 0, 7, qtrue);
	r_waterCausticsLightMask = R_Cvar("r_waterCausticsLightMask", "1", CVAR_ARCHIVE | CVAR_LATCH, "caustic light sources: 1 sun, 2 dynamic point/spot, 4 directional baked light");
	R_CvarRange(r_waterCausticsLightMask, 0, 7, qtrue);
	r_waterCausticsLocalMaxLights = R_Cvar("r_waterCausticsLocalMaxLights", "1", CVAR_ARCHIVE, "maximum dynamic point/spot lights evaluated for water caustics per receiver");
	R_CvarRange(r_waterCausticsLocalMaxLights, 0, 4, qtrue);
	r_waterCausticsBakedMode = R_Cvar("r_waterCausticsBakedMode", "0", CVAR_ARCHIVE, "directional baked-light caustics: 0 off, 1 surface-driven, 2 original procedural pattern");
	R_CvarRange(r_waterCausticsBakedMode, 0, 2, qtrue);
	r_waterCausticsBakedStrength = R_Cvar("r_waterCausticsBakedStrength", "0.35", CVAR_ARCHIVE, "strength of liquid attenuation and caustic modulation on the directional baked-light share");
	R_CvarRange(r_waterCausticsBakedStrength, 0.0f, 1.0f, qfalse);
	r_volumetricWaterExtinction = R_Cvar("r_volumetricWaterExtinction", "0.0014", CVAR_ARCHIVE, "r_volumetricWater: water extinction per world unit (mean of the channels; 0.0014 = 1/e after ~700 units; as fogparms depthForOpaque D: extinction = 5.14 / D)");
	R_CvarRange(r_volumetricWaterExtinction, 0.0f, 1.0f, qfalse);
	r_volumetricWaterColor = R_Cvar("r_volumetricWaterColor", "2.0 0.75 0.25", CVAR_ARCHIVE, "r_volumetricWater: water relative extinction \"r g b\" (normalized to mean 1; red is absorbed first): RGB transmittance with r_volumetricFogRGBExtinction, the sun path color in both modes");
	r_volumetricWaterAlbedo = R_Cvar("r_volumetricWaterAlbedo", "0.10 0.45 0.75", CVAR_ARCHIVE, "r_volumetricWater: water single scattering albedo \"r g b\" (in-scattering color = albedo * relative extinction)");
	r_volumetricWaterAnisotropy = R_Cvar("r_volumetricWaterAnisotropy", "0.75", CVAR_ARCHIVE, "r_volumetricWater: water Henyey-Greenstein g (forward scattering)");
	R_CvarRange(r_volumetricWaterAnisotropy, -0.9f, 0.9f, qfalse);
	r_volumetricSlimeExtinction = R_Cvar("r_volumetricSlimeExtinction", "0.005", CVAR_ARCHIVE, "r_volumetricWater 2: slime extinction per world unit; the slime profile is also the medium of water brushes under a slime-flagged surface (vjun1) and of env.json \"Liquids\" rules");
	R_CvarRange(r_volumetricSlimeExtinction, 0.0f, 1.0f, qfalse);
	r_volumetricSlimeColor = R_Cvar("r_volumetricSlimeColor", "1.6 0.5 0.9", CVAR_ARCHIVE, "r_volumetricWater 2: slime relative extinction \"r g b\"");
	r_volumetricSlimeAlbedo = R_Cvar("r_volumetricSlimeAlbedo", "0.25 0.7 0.2", CVAR_ARCHIVE, "r_volumetricWater 2: slime single scattering albedo \"r g b\"");
	r_volumetricSlimeAnisotropy = R_Cvar("r_volumetricSlimeAnisotropy", "0.4", CVAR_ARCHIVE, "r_volumetricWater 2: slime Henyey-Greenstein g");
	R_CvarRange(r_volumetricSlimeAnisotropy, -0.9f, 0.9f, qfalse);
	r_volumetricLavaExtinction = R_Cvar("r_volumetricLavaExtinction", "0.02", CVAR_ARCHIVE, "r_volumetricWater 4: lava extinction per world unit");
	R_CvarRange(r_volumetricLavaExtinction, 0.0f, 1.0f, qfalse);
	r_volumetricLavaColor = R_Cvar("r_volumetricLavaColor", "0.6 1.2 1.2", CVAR_ARCHIVE, "r_volumetricWater 4: lava relative extinction \"r g b\"");
	r_volumetricLavaAlbedo = R_Cvar("r_volumetricLavaAlbedo", "0.6 0.15 0.02", CVAR_ARCHIVE, "r_volumetricWater 4: lava single scattering albedo \"r g b\" (no emission: absorption and scattering only)");
	r_volumetricLavaAnisotropy = R_Cvar("r_volumetricLavaAnisotropy", "0.3", CVAR_ARCHIVE, "r_volumetricWater 4: lava Henyey-Greenstein g");
	R_CvarRange(r_volumetricLavaAnisotropy, -0.9f, 0.9f, qfalse);
	// modern water surface (tr_watersurface.cpp, docs/rend2-water-surface.md)
	r_waterSurface = R_Cvar("r_waterSurface", "0", CVAR_ARCHIVE | CVAR_LATCH, "Modern water surface for the stock water (CONTENTS_WATER surfaces): Fresnel reflection (SSR -> cubemap), depth aware refraction, liquid absorption, before tone mapping. 0 = legacy water stages (no extra targets or passes); vid_restart");
	R_CvarRange(r_waterSurface, 0, 1, qtrue);
	r_waterfall = R_Cvar("r_waterfall", "0", CVAR_ARCHIVE | CVAR_LATCH, "Dedicated modern waterfall sheets selected by env.json Waterfalls metadata or the audited stock mapping; 0 restores exact legacy stages; vid_restart");
	r_waterfallQuality = R_Cvar("r_waterfallQuality", "1", CVAR_ARCHIVE, "Waterfall coherent breakup quality: 0 low, 1 medium, 2 high");
	r_waterfallGeometry = R_Cvar("r_waterfallGeometry", "1", CVAR_ARCHIVE | CVAR_LATCH, "Render-only waterfall subdivision; BSP collision is unchanged; vid_restart");
	r_waterfallRefraction = R_Cvar("r_waterfallRefraction", "1", CVAR_ARCHIVE, "Waterfall thin-sheet refraction strength");
	r_waterfallWhitewater = R_Cvar("r_waterfallWhitewater", "1", CVAR_ARCHIVE, "Waterfall aeration and lit whitewater scale");
	r_waterfallSmallStrength = R_Cvar("r_waterfallSmallStrength", "1", CVAR_ARCHIVE, "Small waterfall profile breakup, whitewater and spray strength");
	r_waterfallMediumStrength = R_Cvar("r_waterfallMediumStrength", "1", CVAR_ARCHIVE, "Medium waterfall profile breakup, whitewater and spray strength");
	r_waterfallHeavyStrength = R_Cvar("r_waterfallHeavyStrength", "1", CVAR_ARCHIVE, "Heavy/high-volume waterfall profile breakup, whitewater and spray strength");
	r_waterfallDebug = R_Cvar("r_waterfallDebug", "0", CVAR_CHEAT, "Waterfall debug: 1 classified, 2 flow, 3 along-fall, 4 displacement, 5 thickness, 6 raw refraction, 7 turbulence, 8 aeration, 9 foam source, 10 spray source, 11 impact region, 12 subdivided wireframe");
	r_waterfallMist = R_Cvar("r_waterfallMist", "0", CVAR_ARCHIVE, "Waterfall base spray/mist generated from authored emitters or classified sheet impact edges");
	r_waterfallMistDensity = R_Cvar("r_waterfallMistDensity", "1", CVAR_ARCHIVE, "Waterfall suspended mist density scale");
	r_waterfallMistRadius = R_Cvar("r_waterfallMistRadius", "1", CVAR_ARCHIVE, "Waterfall mist and lens emitter radius scale");
	r_waterfallMistBalance = R_Cvar("r_waterfallMistBalance", "0.65", CVAR_ARCHIVE, "Waterfall particle/mist balance: 0 keeps particle spray only, 1 uses full froxel mist");
	r_waterfallMistLens = R_Cvar("r_waterfallMistLens", "0", CVAR_ARCHIVE, "Couple nearby waterfall spray to the existing LensWater SPRAY input");
	r_waterfallMistDebug = R_Cvar("r_waterfallMistDebug", "0", CVAR_CHEAT, "Draw and list resolved waterfall mist emitters");
	r_waterfallImpact = R_Cvar("r_waterfallImpact", "0", CVAR_ARCHIVE, "Localized waterfall impact-pool ripples, turbulence and foam; requires r_waterInteraction and/or r_waterFoamField");
	r_waterfallImpactRadius = R_Cvar("r_waterfallImpactRadius", "1", CVAR_ARCHIVE, "Waterfall impact-pool authored/inferred radius multiplier");
	r_waterfallImpactImpulse = R_Cvar("r_waterfallImpactImpulse", "1", CVAR_ARCHIVE, "Waterfall impact-pool central downward impulse strength");
	r_waterfallImpactTurbulence = R_Cvar("r_waterfallImpactTurbulence", "1", CVAR_ARCHIVE, "Waterfall impact-pool stochastic secondary disturbance strength");
	r_waterfallImpactFoam = R_Cvar("r_waterfallImpactFoam", "1", CVAR_ARCHIVE, "Waterfall impact-pool persistent foam injection strength");
	r_waterfallImpactDebug = R_Cvar("r_waterfallImpactDebug", "0", CVAR_CHEAT, "Draw resolved waterfall impact regions and print resolution diagnostics");
	R_CvarRange(r_waterfall, 0, 1, qtrue);
	R_CvarRange(r_waterfallQuality, 0, 2, qtrue);
	R_CvarRange(r_waterfallGeometry, 0, 1, qtrue);
	R_CvarRange(r_waterfallRefraction, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallWhitewater, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallSmallStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallMediumStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallHeavyStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallDebug, 0, 12, qtrue);
	R_CvarRange(r_waterfallMist, 0, 1, qtrue);
	R_CvarRange(r_waterfallMistDensity, 0.0f, 8.0f, qfalse);
	R_CvarRange(r_waterfallMistRadius, 0.1f, 4.0f, qfalse);
	R_CvarRange(r_waterfallMistBalance, 0.0f, 1.0f, qfalse);
	R_CvarRange(r_waterfallMistLens, 0.0f, 2.0f, qfalse);
	R_CvarRange(r_waterfallMistDebug, 0, 1, qtrue);
	R_CvarRange(r_waterfallImpact, 0, 1, qtrue);
	R_CvarRange(r_waterfallImpactRadius, 0.1f, 4.0f, qfalse);
	R_CvarRange(r_waterfallImpactImpulse, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallImpactTurbulence, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallImpactFoam, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterfallImpactDebug, 0, 1, qtrue);
	r_waterSurfaceIOR = R_Cvar("r_waterSurfaceIOR", "1.333", CVAR_ARCHIVE, "r_waterSurface: index of refraction of the liquid (Fresnel and refraction)");
	R_CvarRange(r_waterSurfaceIOR, 1.0f, 2.0f, qfalse);
	r_waterSurfaceRoughness = R_Cvar("r_waterSurfaceRoughness", "0.06", CVAR_ARCHIVE, "r_waterSurface: base roughness (perceptual); the unresolved waves add to it");
	R_CvarRange(r_waterSurfaceRoughness, 0.0f, 1.0f, qfalse);
	r_waterSurfaceNormal = R_Cvar("r_waterSurfaceNormal", "1.0", CVAR_ARCHIVE, "r_waterSurface: detail wave normal strength (0 removes micro detail; ambient macro is separate)");
	R_CvarRange(r_waterSurfaceNormal, 0.0f, 4.0f, qfalse);
	r_waterWaves = R_Cvar("r_waterWaves", "0", CVAR_ARCHIVE, "Ambient macro/medium water waves; 0 keeps existing water");
	r_waterWind = R_Cvar("r_waterWind", "0", CVAR_ARCHIVE, "Wind-driven outdoor water response (requires r_waterWaves); 0 preserves existing waves");
	R_CvarRange(r_waterWind, 0, 1, qtrue);
	r_waterWindScale = R_Cvar("r_waterWindScale", "1", CVAR_ARCHIVE, "Global scale for resolved wind response on water");
	R_CvarRange(r_waterWindScale, 0.0f, 4.0f, qfalse);
	r_waterWindTransition = R_Cvar("r_waterWindTransition", "4", CVAR_ARCHIVE, "Seconds to crossfade old and target wind wave sets");
	R_CvarRange(r_waterWindTransition, 0.25f, 30.0f, qfalse);
	r_waterWindDebug = R_Cvar("r_waterWindDebug", "0", CVAR_CHEAT, "Water wind debug: 1 resolved wind, 2 current wave direction, 3 target direction, 4 blend/source");
	R_CvarRange(r_waterWindDebug, 0, 4, qtrue);
	r_waterFlow = R_Cvar("r_waterFlow", "0", CVAR_ARCHIVE, "Directional per-body water flow; 0 preserves the pre-flow water result");
	R_CvarRange(r_waterFlow, 0, 1, qtrue);
	r_waterFlowSpeed = R_Cvar("r_waterFlowSpeed", "1", CVAR_ARCHIVE, "Global multiplier for resolved per-body flow velocity");
	R_CvarRange(r_waterFlowSpeed, 0.0f, 4.0f, qfalse);
	r_waterFlowDetail = R_Cvar("r_waterFlowDetail", "1", CVAR_ARCHIVE, "Directional advection scale for medium and micro water detail");
	R_CvarRange(r_waterFlowDetail, 0.0f, 4.0f, qfalse);
	r_waterFlowDebug = R_Cvar("r_waterFlowDebug", "0", CVAR_CHEAT, "Water flow debug: 1 direction, 2 magnitude, 3 confidence/source, 4 advected detail, 5 field speed, 6 wall impingement, 7 field shore distance");
	R_CvarRange(r_waterFlowDebug, 0, 7, qtrue);
	r_waterFlowField = R_Cvar("r_waterFlowField", "0", CVAR_ARCHIVE | CVAR_LATCH, "Spatial per-body flow field baked at map load: slower water at banks, flow turning along walls (requires r_waterFlow 1)");
	r_waterFlowImpact = R_Cvar("r_waterFlowImpact", "0", CVAR_ARCHIVE | CVAR_LATCH, "Flow running into a wall piles up, sloshes, foams and wets the wall (requires r_waterFlowField 1)");
	r_waterInteraction = R_Cvar("r_waterInteraction", "0", CVAR_ARCHIVE | CVAR_LATCH, "Per-body interactive water disturbances; requires r_waterSurface and vid_restart");
	R_CvarRange(r_waterInteraction, 0, 1, qtrue);
	r_waterInteractionQuality = R_Cvar("r_waterInteractionQuality", "1", CVAR_ARCHIVE | CVAR_LATCH, "Interaction density: 0=32, 1=16, 2=8 world units per texel");
	R_CvarRange(r_waterInteractionQuality, 0, 2, qtrue);
	r_waterInteractionStrength = R_Cvar("r_waterInteractionStrength", "1", CVAR_ARCHIVE, "Global water interaction strength multiplier");
	R_CvarRange(r_waterInteractionStrength, 0.0f, 4.0f, qfalse);
	r_waterInteractionDamping = R_Cvar("r_waterInteractionDamping", "1", CVAR_ARCHIVE, "Profile disturbance damping multiplier");
	R_CvarRange(r_waterInteractionDamping, 0.25f, 4.0f, qfalse);
	r_waterInteractionSpeed = R_Cvar("r_waterInteractionSpeed", "1", CVAR_ARCHIVE, "Profile disturbance propagation-speed multiplier");
	R_CvarRange(r_waterInteractionSpeed, 0.25f, 4.0f, qfalse);
	r_waterInteractionMaxBodies = R_Cvar("r_waterInteractionMaxBodies", "8", CVAR_ARCHIVE | CVAR_LATCH, "Maximum bodies with interaction fields");
	R_CvarRange(r_waterInteractionMaxBodies, 1, 32, qtrue);
	r_waterInteractionMaxTexels = R_Cvar("r_waterInteractionMaxTexels", "524288", CVAR_ARCHIVE | CVAR_LATCH, "Total interaction texel budget");
	R_CvarRange(r_waterInteractionMaxTexels, 4096, 4194304, qtrue);
	r_waterInteractionMemoryMB = R_Cvar("r_waterInteractionMemoryMB", "16", CVAR_ARCHIVE | CVAR_LATCH, "GPU interaction-field memory budget in MiB");
	R_CvarRange(r_waterInteractionMemoryMB, 1, 128, qtrue);
	r_waterInteractionDebug = R_Cvar("r_waterInteractionDebug", "0", CVAR_CHEAT, "Interaction debug: 1 domain, 2 mask, 3 height, 4 velocity, 5 slope, 6 source, 7 foam, 8 active/sleep, 9 mapping");
	r_waterBathymetry = R_Cvar("r_waterBathymetry", "0", CVAR_ARCHIVE | CVAR_LATCH, "Build exact body-local liquid depth/confidence fields; requires water interaction and vid_restart");
	R_CvarRange(r_waterBathymetry, 0, 1, qtrue);
	r_waterBathymetryDebug = R_Cvar("r_waterBathymetryDebug", "0", CVAR_CHEAT, "Print body-local bathymetry coverage and confidence audit");
	R_CvarRange(r_waterBathymetryDebug, 0, 1, qtrue);
	r_waterInteractionVariableDepth = R_Cvar("r_waterInteractionVariableDepth", "0", CVAR_ARCHIVE, "Use bathymetry and g*div(H*grad(height)) in interactive water");
	R_CvarRange(r_waterInteractionVariableDepth, 0, 1, qtrue);
	R_CvarRange(r_waterInteractionDebug, 0, 9, qtrue);
	r_waterIntersectionFoam = R_Cvar("r_waterIntersectionFoam", "0", CVAR_ARCHIVE, "Opaque-object intersection foam on modern shallow water");
	r_waterIntersectionFoamWidth = R_Cvar("r_waterIntersectionFoamWidth", "8", CVAR_ARCHIVE, "World-space half-width of the object/water intersection band");
	r_waterIntersectionFoamStrength = R_Cvar("r_waterIntersectionFoamStrength", "1", CVAR_ARCHIVE, "Object/water intersection foam strength");
	r_waterIntersectionFoamPersistence = R_Cvar("r_waterIntersectionFoamPersistence", "1.5", CVAR_ARCHIVE, "Seconds for intersection foam in the interaction field to fade");
	r_waterIntersectionFoamDebug = R_Cvar("r_waterIntersectionFoamDebug", "0", CVAR_CHEAT, "Show the raw depth/normal-qualified object intersection mask");
	R_CvarRange(r_waterIntersectionFoam, 0, 1, qtrue);
	R_CvarRange(r_waterIntersectionFoamWidth, 0.5f, 64.0f, qfalse);
	R_CvarRange(r_waterIntersectionFoamStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterIntersectionFoamPersistence, 0.0f, 30.0f, qfalse);
	R_CvarRange(r_waterIntersectionFoamDebug, 0, 1, qtrue);
	r_waterFoamField = R_Cvar("r_waterFoamField", "0", CVAR_ARCHIVE | CVAR_LATCH, "Persistent body-local water foam fields; requires r_waterSurface and vid_restart (never ocean foam)");
	r_waterFoamResolution = R_Cvar("r_waterFoamResolution", "128", CVAR_ARCHIVE | CVAR_LATCH, "Longest side of each active local foam field");
	r_waterFoamDecay = R_Cvar("r_waterFoamDecay", "0.55", CVAR_ARCHIVE, "Persistent foam concentration decay per second");
	r_waterFoamAdvection = R_Cvar("r_waterFoamAdvection", "1", CVAR_ARCHIVE, "Foam advection multiplier for resolved body flow");
	r_waterFoamDiffusion = R_Cvar("r_waterFoamDiffusion", "0.12", CVAR_ARCHIVE, "Mild foam diffusion in texels per second");
	r_waterFoamWaterfall = R_Cvar("r_waterFoamWaterfall", "1", CVAR_ARCHIVE, "Waterfall/turbulent-stream foam source strength (0 disables this source)");
	r_waterFoamIntersection = R_Cvar("r_waterFoamIntersection", "1", CVAR_ARCHIVE, "Obstacle/intersection foam source strength (0 disables this source)");
	r_waterFoamImpact = R_Cvar("r_waterFoamImpact", "1", CVAR_ARCHIVE, "Impact-pool and wake foam source strength (0 disables this source)");
	r_waterFoamShoreline = R_Cvar("r_waterFoamShoreline", "1", CVAR_ARCHIVE, "Enabled shoreline foam source strength (0 disables this source)");
	r_waterFoamSteepness = R_Cvar("r_waterFoamSteepness", "0", CVAR_ARCHIVE, "Optional local wave-steepness foam source strength");
	r_waterFoamDebug = R_Cvar("r_waterFoamDebug", "0", CVAR_CHEAT, "Persistent foam debug: 1 concentration, 2 velocity, 3 sources");
	R_CvarRange(r_waterFoamField, 0, 1, qtrue);
	R_CvarRange(r_waterFoamResolution, 32, 512, qtrue);
	R_CvarRange(r_waterFoamDecay, 0.0f, 8.0f, qfalse);
	R_CvarRange(r_waterFoamAdvection, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamDiffusion, 0.0f, 1.0f, qfalse);
	R_CvarRange(r_waterFoamWaterfall, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamIntersection, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamImpact, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamShoreline, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamSteepness, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterFoamDebug, 0, 3, qtrue);
	r_waterWhitewater = R_Cvar("r_waterWhitewater", "0", CVAR_ARCHIVE | CVAR_LATCH, "Advected stream/rapid aeration; requires r_waterSurface and vid_restart");
	r_waterWhitewaterThreshold = R_Cvar("r_waterWhitewaterThreshold", "0.45", CVAR_ARCHIVE, "Turbulence source threshold for whitewater formation");
	r_waterWhitewaterStrength = R_Cvar("r_waterWhitewaterStrength", "1", CVAR_ARCHIVE, "Whitewater bulk optical strength");
	r_waterWhitewaterFoam = R_Cvar("r_waterWhitewaterFoam", "0.35", CVAR_ARCHIVE, "Persistent surface-foam injection from whitewater");
	r_waterWhitewaterDecay = R_Cvar("r_waterWhitewaterDecay", "1.1", CVAR_ARCHIVE, "Advected whitewater turbulence decay per second");
	r_waterWhitewaterDebug = R_Cvar("r_waterWhitewaterDebug", "0", CVAR_CHEAT, "Whitewater debug: 1 flow, 2 turbulence source, 3 final whitewater, 4 foam sources");
	R_CvarRange(r_waterWhitewater, 0, 1, qtrue);
	R_CvarRange(r_waterWhitewaterThreshold, 0.0f, 1.0f, qfalse);
	R_CvarRange(r_waterWhitewaterStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterWhitewaterFoam, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterWhitewaterDecay, 0.0f, 8.0f, qfalse);
	R_CvarRange(r_waterWhitewaterDebug, 0, 4, qtrue);
	r_waterSplashes = R_Cvar("r_waterSplashes", "0", CVAR_ARCHIVE, "Physical water entry, exit and projectile splashes");
	r_waterSplashQuality = R_Cvar("r_waterSplashQuality", "1", CVAR_ARCHIVE, "Physical splash particles: 0 low, 1 medium, 2 high");
	r_waterSplashStrength = R_Cvar("r_waterSplashStrength", "1", CVAR_ARCHIVE, "Physical splash impulse and spray strength scale");
	r_waterSplashFoam = R_Cvar("r_waterSplashFoam", "1", CVAR_ARCHIVE, "Physical splash foam scale");
	r_waterSplashProjectiles = R_Cvar("r_waterSplashProjectiles", "1", CVAR_ARCHIVE, "Projectile and thrown-saber water splashes");
	r_waterSplashDebug = R_Cvar("r_waterSplashDebug", "0", CVAR_CHEAT, "Show and print physical splash events");
	R_CvarRange(r_waterSplashQuality, 0, 2, qtrue);
	R_CvarRange(r_waterSplashStrength, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterSplashFoam, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterSplashProjectiles, 0, 1, qtrue);
	R_CvarRange(r_waterSplashDebug, 0, 1, qtrue);
	r_waterGeometry = R_Cvar("r_waterGeometry", "0", CVAR_ARCHIVE | CVAR_LATCH, "Subdivided render-only water mesh; requires r_waterSurface and vid_restart");
	R_CvarRange(r_waterGeometry, 0, 1, qtrue);
	r_waterGeometryEdge = R_Cvar("r_waterGeometryEdge", "64", CVAR_ARCHIVE | CVAR_LATCH, "Target maximum water mesh edge length in world units");
	r_waterGeometryBodyVerts = R_Cvar("r_waterGeometryBodyVerts", "250000", CVAR_ARCHIVE | CVAR_LATCH, "Maximum subdivided vertices per water body");
	r_waterGeometryMapVerts = R_Cvar("r_waterGeometryMapVerts", "1000000", CVAR_ARCHIVE | CVAR_LATCH, "Maximum subdivided vertices per map");
	r_waterGeometryDebug = R_Cvar("r_waterGeometryDebug", "0", CVAR_ARCHIVE, "Water geometry debug view");
	r_offscreenPresent = R_Cvar("r_offscreenPresent", "0", CVAR_LATCH, "Render the frame into an offscreen buffer and copy it to the window at swap, so screenshots work from a hidden window (scripted tests); requires vid_restart");
	r_waterTessellation = R_Cvar("r_waterTessellation", "0", CVAR_ARCHIVE | CVAR_LATCH, "Distance-adaptive GPU tessellation of the r_waterGeometry mesh (GL 4.3, r_gl43); coarser base mesh, requires vid_restart");
	R_CvarRange(r_waterTessellation, 0, 1, qtrue);
	r_waterTessPixels = R_Cvar("r_waterTessPixels", "96", CVAR_ARCHIVE, "Target tessellated water edge length in pixels (smaller is denser and slower; waves too short for the mesh stay in shading)");
	R_CvarRange(r_waterTessPixels, 2, 128, qfalse);
	r_waterTessMinEdge = R_Cvar("r_waterTessMinEdge", "0", CVAR_ARCHIVE, "Shortest tessellated water edge in world units: 64 = never finer than the static r_waterGeometry mesh, 0 = only r_waterTessMax limits the detail near the camera");
	r_waterTessLodDistance = R_Cvar("r_waterTessLodDistance", "0", CVAR_ARCHIVE, "Distance from the camera (world units) with the finest water tessellation; beyond it edges grow by r_waterTessPixels");
	r_waterTessMax = R_Cvar("r_waterTessMax", "16", CVAR_ARCHIVE | CVAR_LATCH, "Maximum water tessellation level (8, 16, 32): the base mesh is that much coarser; requires vid_restart");
	R_CvarRange(r_waterTessMax, 2, 64, qtrue);
	r_waterTessForce = R_Cvar("r_waterTessForce", "0", CVAR_CHEAT, "Water tessellation level for every edge (0: by distance), for tests");
	R_CvarRange(r_waterTessForce, 0, 64, qfalse);
	r_waterShoreline = R_Cvar("r_waterShoreline", "0", CVAR_ARCHIVE | CVAR_LATCH, "Displaced-water shoreline/contact treatment; requires r_waterSurface, r_waterGeometry and vid_restart");
	r_waterShoreAttenuation = R_Cvar("r_waterShoreAttenuation", "96", CVAR_ARCHIVE, "World distance over which macro waves recover from a rigid/shallow boundary");
	r_waterShoreContactSoftness = R_Cvar("r_waterShoreContactSoftness", "6", CVAR_ARCHIVE, "World width of the water/geometry soft contact and overlap");
	r_waterShoreWetStrength = R_Cvar("r_waterShoreWetStrength", "0.8", CVAR_ARCHIVE, "Water-contact wet-fringe strength through the existing material wetness response");
	r_waterShoreWetPersistence = R_Cvar("r_waterShoreWetPersistence", "8", CVAR_ARCHIVE, "Seconds for a retreated displaced waterline to dry");
	r_waterShoreFoam = R_Cvar("r_waterShoreFoam", "1", CVAR_ARCHIVE, "Profile-scaled shoreline source strength for the existing water foam field");
	r_waterShoreDebug = R_Cvar("r_waterShoreDebug", "0", CVAR_CHEAT, "Shoreline debug: 1 depth, 2 boundary distance, 3 attenuation, 4 displaced height, 5 rigid/contact class, 6 contact mask, 7 wet fringe, 8 foam source, 9 crack inspection");
	R_CvarRange(r_waterShoreline, 0, 1, qtrue);
	R_CvarRange(r_waterShoreAttenuation, 4.0f, 1024.0f, qfalse);
	R_CvarRange(r_waterShoreContactSoftness, 0.25f, 64.0f, qfalse);
	R_CvarRange(r_waterShoreWetStrength, 0.0f, 2.0f, qfalse);
	R_CvarRange(r_waterShoreWetPersistence, 0.0f, 120.0f, qfalse);
	R_CvarRange(r_waterShoreFoam, 0.0f, 4.0f, qfalse);
	R_CvarRange(r_waterShoreDebug, 0, 9, qtrue);
	R_CvarRange(r_waterWaves, 0, 1, qtrue);
	r_waterWaveAmplitude = R_Cvar("r_waterWaveAmplitude", "1", CVAR_ARCHIVE, "Ambient wave amplitude scale");
	R_CvarRange(r_waterWaveAmplitude, 0.0f, 4.0f, qfalse);
	r_waterWaveLength = R_Cvar("r_waterWaveLength", "1", CVAR_ARCHIVE, "Ambient wave wavelength scale");
	R_CvarRange(r_waterWaveLength, 0.25f, 4.0f, qfalse);
	r_waterWaveSpeed = R_Cvar("r_waterWaveSpeed", "1", CVAR_ARCHIVE, "Ambient wave speed scale");
	R_CvarRange(r_waterWaveSpeed, 0.0f, 4.0f, qfalse);
	r_waterWaveDispersion = R_Cvar("r_waterWaveDispersion", "1", CVAR_ARCHIVE, "Wave dispersion: 0 legacy nondispersive, 1 authored-speed finite-depth, 2 calibrated physical for non-flowing water");
	R_CvarRange(r_waterWaveDispersion, 0, 2, qtrue);
	r_waterWaveGravity = R_Cvar("r_waterWaveGravity", "313.6", CVAR_ARCHIVE, "Physical renderer water gravity in world units per second squared (dispersion mode 2 and variable-depth interaction)");
	R_CvarRange(r_waterWaveGravity, 1.0f, 4096.0f, qfalse);
	r_waterWaveQuality = R_Cvar("r_waterWaveQuality", "1", CVAR_ARCHIVE, "Ambient waves: 0 = 2 terms, 1 = 4 terms, 2 = 8 terms");
	R_CvarRange(r_waterWaveQuality, 0, 2, qtrue);
	r_waterWaveMicro = R_Cvar("r_waterWaveMicro", "1", CVAR_ARCHIVE, "Micro slope scale in addition to r_waterSurfaceNormal");
	R_CvarRange(r_waterWaveMicro, 0.0f, 4.0f, qfalse);
	r_waterWaveShallow = R_Cvar("r_waterWaveShallow", "1", CVAR_ARCHIVE, "Dampen analytical waves using stable body mean depth");
	R_CvarRange(r_waterWaveShallow, 0, 1, qtrue);
	r_waterWaveDebug = R_Cvar("r_waterWaveDebug", "0", CVAR_CHEAT, "Ambient waves debug view: 1 height, 2 macro normal, 3 micro normal, 4 combined, 5 profile, 6 depth mask, 7 displacement, 9 wave multiplier, 10 legacy deform (old / new: r_waterWavesCompare)");
	R_CvarRange(r_waterWaveDebug, 0, 10, qtrue);
	r_waterWaveTime = R_Cvar("r_waterWaveTime", "-1", CVAR_CHEAT, "Ambient wave time: -1 running, >=0 fixed seconds");
	R_CvarRange(r_waterWaveTime, -1.0f, 100000.0f, qfalse);
	r_waterlineHysteresis = R_Cvar("r_waterlineHysteresis", "1.5", CVAR_ARCHIVE, "Visual camera waterline enter/exit band in world units; fast crossings reduce the band");
	R_CvarRange(r_waterlineHysteresis, 0.0f, 16.0f, qfalse);
	r_waterlineDebug = R_Cvar("r_waterlineDebug", "0", CVAR_CHEAT, "Visual waterline diagnostics: 1 = once-per-second state, 2 = state plus recent camera-relative-height graph");
	R_CvarRange(r_waterlineDebug, 0, 2, qtrue);
	r_waterSurfaceRefraction = R_Cvar("r_waterSurfaceRefraction", "1.0", CVAR_ARCHIVE, "r_waterSurface: scale of the screen-space refraction offset (1 = physical for the depth behind the water)");
	R_CvarRange(r_waterSurfaceRefraction, 0.0f, 4.0f, qfalse);
	r_waterSurfaceDepthReject = R_Cvar("r_waterSurfaceDepthReject", "1", CVAR_ARCHIVE, "r_waterSurface: refraction never samples objects in front of the water (shrinks the offset, else none)");
	R_CvarRange(r_waterSurfaceDepthReject, 0, 1, qtrue);
	r_waterSurfaceReflection = R_Cvar("r_waterSurfaceReflection", "1.0", CVAR_ARCHIVE, "r_waterSurface: scale of the reflection (1 = physical)");
	R_CvarRange(r_waterSurfaceReflection, 0.0f, 4.0f, qfalse);
	r_waterSurfaceSSR = R_Cvar("r_waterSurfaceSSR", "1.0", CVAR_ARCHIVE, "r_waterSurface: weight of the screen-space reflections over the cubemap (independent of opaque r_ssr; 0 = cubemap only)");
	R_CvarRange(r_waterSurfaceSSR, 0.0f, 1.0f, qfalse);
	r_waterSSRWaveReprojection = R_Cvar("r_waterSSRWaveReprojection", "0", CVAR_ARCHIVE | CVAR_LATCH,
		"Use exact previous-rendered water geometry for temporal SSR reprojection");
	R_CvarRange(r_waterSSRWaveReprojection, 0, 1, qtrue);
	r_waterSSROptimize = R_Cvar("r_waterSSROptimize", "7", CVAR_ARCHIVE,
		"r_waterSurface SSR prepass savings, bits: 1 = the prepass tessellates for its own (reduced) resolution, 2 = it drops occluded water before the wave shading, 4 = it runs a prepass-only program without the shading code; 0 = the 10-19 prepass (A/B)");
	R_CvarRange(r_waterSSROptimize, 0, 7, qtrue);
	r_waterMotionDebug = R_Cvar("r_waterMotionDebug", "0", CVAR_CHEAT,
		"Water receiver motion debug (1 validity, 2 current, 3 previous, 4 screen motion, 5 analytic, 6 interaction, 7 waterfall/legacy, 8 previous UV, 9 invalidation, 10 camera-only split)");
	R_CvarRange(r_waterMotionDebug, 0, 10, qtrue);
	r_waterSurfaceAbsorption = R_Cvar("r_waterSurfaceAbsorption", "1.0", CVAR_ARCHIVE, "r_waterSurface: shared liquid extinction scale for surface and volume (r_volumetricWaterExtinction / Color, Slime)");
	R_CvarRange(r_waterSurfaceAbsorption, 0.0f, 16.0f, qfalse);
	r_waterSurfaceDepthScale = R_Cvar("r_waterSurfaceDepthScale", "1.0", CVAR_ARCHIVE, "r_waterSurface: scale of the path length under the surface (water thickness from the depth buffer)");
	R_CvarRange(r_waterSurfaceDepthScale, 0.0f, 16.0f, qfalse);
	r_waterSurfaceExperimental = R_Cvar("r_waterSurfaceExperimental", "0", CVAR_ARCHIVE, "r_waterSurface: also classify surfaces by a water-like shader name (pool, lake, river, water...) when they face up and have no water contents; experimental");
	R_CvarRange(r_waterSurfaceExperimental, 0, 1, qtrue);
	r_waterSurfaceDebug = R_Cvar("r_waterSurfaceDebug", "0", CVAR_CHEAT, "Modern water debug view: 1 classified surfaces (blue water, green slime, violet fog volume medium, yellow override, magenta experimental), 2 normal, 3 Fresnel / reflection weight, 4 path length under the surface, 5 raw refracted color, 6 SSR hit (green) / miss (red), 7 reflection source (red SSR, green cubemap, blue fallback), 8 transmittance, 10 refraction depth rejection, 11 roughness (old / new: r_waterSurfaceCompare)");
	R_CvarRange(r_waterSurfaceDebug, 0, 11, qtrue);
	r_waterSnell = R_Cvar("r_waterSnell", "0", CVAR_ARCHIVE, "r_waterSurface: seen from inside the liquid, the surface is the water -> air interface: Snell's window refracts the scene above, total internal reflection beyond the critical angle (exact Fresnel, IOR r_waterSurfaceIOR) reflects the scene in the liquid (SSR -> cubemap in the liquid -> the liquid) through the medium. 0 = the r_waterSurface view from inside unchanged");
	R_CvarRange(r_waterSnell, 0, 1, qtrue);
	r_waterSnellDebug = R_Cvar("r_waterSnellDebug", "0", CVAR_CHEAT, "r_waterSnell debug view: 1 = crossing direction (blue air -> water, orange water -> air, magenta stripes: camera contents disagree), 2 = total internal reflection share (red) / transmits (green), yellow critical angle, 3 = exact Fresnel, 4 = refracted direction (black: TIR), 5 = reflection (red) / transmission (green) weights, 6 = reflection source (red SSR, green cubemap, blue liquid / fallback)");
	R_CvarRange(r_waterSnellDebug, 0, 6, qtrue);
	r_waterSnellOptics = R_Cvar("r_waterSnellOptics", "0", CVAR_ARCHIVE | CVAR_LATCH, "Snell optics: 0 legacy, 1 underwater sun BTDF/eta transport, 2 also atmosphere through the air segment");
	R_CvarRange(r_waterSnellOptics, 0, 2, qtrue);
	r_volumetricFogReset = R_Cvar("r_volumetricFogReset", "0", 0, "Set to 1 by game code to reset the froxel fog history (camera cut), cleared by the renderer");
	r_volumetricFogDebug = R_Cvar("r_volumetricFogDebug", "0", CVAR_CHEAT, "Froxel fog debug view: 1 density, 2 sun (unshadowed), 3 sun (shadowed), 4 dynamic lights, 5 baked light, 6 scattering, 7 transmittance, 8 history weight, 9 integrated volume, 10 slices, 11 density of the BSP fog volumes, 12 density of the height fog, 13 noise modulation, 14 density without noise, 15 density with noise, 16 density of the local fog volumes, 17 local vs other fog share, 18 local fog volume bounds, 19 local volumes per slice, 20 baseline, 23 baked sun, 24 B+S, 25 split error (21/22 retired), 26 density of the FX particle media, 27 FX particle history reduction, 28 FX particle proxy bounds, 29 dynamic lights per froxel cluster (cyan: spot lights), 30 scattering source, 31 emissive source, 32 combined source, 33 integrated emission, 34 history vs emission, 35 medium extinction, 36 albedo, 37 phase lobes, 38 mixed g, 39 sun phase, r_volumetricSelfShadow: 40 media density, 41 sun ray optical depth, 42 sun media transmittance, 43 sun geometry shadow only, 44 sun media shadow only, 45 sun both, r_volumetricMultiScatter: 46 sun single scattering, 47 sun multiple scattering term, 48 sun combined, 49 multiple scattering ratio, 50 optical depth (red: towards the sun, green: extinction * r_volumetricMultiScatterLength), r_volumetricFogRGBExtinction: 51 extinction sigma_t.rgb, 52 transmittance T.rgb, 53 color shift RGB - scalar, 54 |RGB - scalar| heat, 55 extinction chroma, 56 tail transmittance, 57/58 retired, r_volumetricWater: 59 density of the liquids, 60 liquid brushes, 61 camera contents (CPU) vs GPU membership, 62 liquid boundary froxels, 63 liquid transmittance along the ray, 64 sun under water (transmittance * caustics), 65 liquid segment length, 66 liquid in-scattering only, 67 liquid transmittance (volume), 68 liquid medium sigma along the ray, 69 surfaces that bypass the liquid fog (magenta lightall blended, yellow generic blend, cyan immediate), 70 dynamic lights without the liquid light path (A/B with 4)");
	R_CvarRange(r_volumetricFogDebug, 0, 74, qtrue);
	// volumetric FX particles (tr_volparticle.cpp): media of the .efx particles with a volumetricMedia block.
	// Mirrored by the SP cgame (only calls the engine with it set), so off by default.
	r_volumetricParticles = R_Cvar("r_volumetricParticles", "0", CVAR_ARCHIVE, "FX particles with a volumetricMedia block add participating media to the froxel fog (r_volumetricFog 2)");
	R_CvarRange(r_volumetricParticles, 0, 1, qtrue);
	fx_physicalizationAggregate = R_Cvar("fx_physicalizationAggregate", "0", CVAR_ARCHIVE, "Experimental exact coalescing of coincident automatic non-emissive media only");
	r_volumetricParticlesMax = R_Cvar("r_volumetricParticlesMax", "128", CVAR_ARCHIVE, "r_volumetricParticles: most important particles uploaded per frame (the rest is capped)");
	R_CvarRange(r_volumetricParticlesMax, 0, MAX_GPU_VOL_PARTICLES, qtrue);
	r_volumetricParticlesScale = R_Cvar("r_volumetricParticlesScale", "1", CVAR_ARCHIVE, "r_volumetricParticles: extinction multiplier of the particle media");
	R_CvarRange(r_volumetricParticlesScale, 0, 16, qfalse);
	r_volumetricParticlesHistory = R_Cvar("r_volumetricParticlesHistory", "0.3", CVAR_ARCHIVE, "r_volumetricParticles: share of the temporal history weight kept where the particle density changed (0 = none, 1 = as static fog)");
	R_CvarRange(r_volumetricParticlesHistory, 0, 1, qfalse);
	r_particleLighting = R_Cvar("r_particleLighting", "0", CVAR_ARCHIVE | CVAR_LATCH, "Light alpha-blended sprite particles (smoke, dust) with the froxel fog light field (r_volumetricFog 2); additive sprites stay unlit");
	R_CvarRange(r_particleLighting, 0, 1, qtrue);
	r_particleLightingMix = R_Cvar("r_particleLightingMix", "1", CVAR_ARCHIVE, "r_particleLighting: blend between the authored (0) and the lit (1) particle color");
	R_CvarRange(r_particleLightingMix, 0, 1, qfalse);
	r_particleLightingScale = R_Cvar("r_particleLightingScale", "1", CVAR_ARCHIVE, "r_particleLighting: gain of the local light (1 = the map average light leaves the authored color unchanged)");
	R_CvarRange(r_particleLightingScale, 0, 16, qfalse);
	r_particleLightingFloor = R_Cvar("r_particleLightingFloor", "0.03", CVAR_ARCHIVE, "r_particleLighting: minimum light factor (unlit smoke never turns fully black)");
	R_CvarRange(r_particleLightingFloor, 0, 1, qfalse);
	r_particleLightingDebug = R_Cvar("r_particleLightingDebug", "0", CVAR_CHEAT, "r_particleLighting: 1 light field, 2 baked only, 3 sun only, 4 dynamic lights only, 5 classification (magenta lit, cyan unlit sprites)");
	R_CvarRange(r_particleLightingDebug, 0, 5, qtrue);
	r_spotLights = R_Cvar("r_spotLights", "0", CVAR_ARCHIVE, "Spot lights (GetRefSpotLightAPI, efx spot group): 0 = submitted as the point lights they would be without the cone");
	R_CvarRange(r_spotLights, 0, 1, qtrue);
	r_spotLightShadows = R_Cvar("r_spotLightShadows", "1", CVAR_ARCHIVE, "Spot lights (r_dlightMode 2): 1 = shadowed (one perspective view up to 60 degrees, else cube faces), 0 = no spot light shadows");
	R_CvarRange(r_spotLightShadows, 0, 1, qtrue);
	r_spotLightDebug = R_Cvar("r_spotLightDebug", "0", CVAR_CHEAT, "Spot lights: 1 cones + per second light list, 2 cones + shadow frustums, 3 surfaces lit by spots only (no fog dlights), 4 fog lit by spots only (no spots on surfaces)");
	R_CvarRange(r_spotLightDebug, 0, 4, qtrue);
	r_spotLightCookies = R_Cvar("r_spotLightCookies", "0", CVAR_ARCHIVE, "Spot light cookies / gobos (efx spot cookie, tr_lightcookie.cpp): 0 off, 1 intensity, 2 colored");
	R_CvarRange(r_spotLightCookies, 0, 2, qtrue);
	r_spotLightCookieDebug = R_Cvar("r_spotLightCookieDebug", "0", CVAR_CHEAT, "Spot light cookies on surfaces: 1 projected uv, 2 cookie factor, 3 cookie * shadow");
	R_CvarRange(r_spotLightCookieDebug, 0, 3, qtrue);
	r_volumetricParticlesDebug = R_Cvar("r_volumetricParticlesDebug", "0", CVAR_CHEAT, "r_volumetricParticles: 1 = print the culling statistics every 60 frames");
	r_volumetricFogFreeze = R_Cvar("r_volumetricFogFreeze", "0", CVAR_CHEAT, "Froxel fog: keep the current froxel volume and its camera (debugging)");
	R_CvarRange(r_volumetricFogFreeze, 0, 1, qtrue);
	r_volumetricFogHeight = R_Cvar("r_volumetricFogHeight", "0", CVAR_ARCHIVE, "Froxel fog (r_volumetricFog 2): height fog (ground haze) medium, 0 = off, 1 = on");
	R_CvarRange(r_volumetricFogHeight, 0, 1, qtrue);
	r_volumetricFogHeightOpaqueDistance = R_Cvar("r_volumetricFogHeightOpaqueDistance", "3000", CVAR_ARCHIVE, "Froxel fog height fog: distance at which the medium at the base height becomes opaque, as fogParms depthForOpaque (world units)");
	R_CvarRange(r_volumetricFogHeightOpaqueDistance, 1.0f, 1000000.0f, qfalse);
	r_volumetricFogHeightBase = R_Cvar("r_volumetricFogHeightBase", "auto", CVAR_ARCHIVE, "Froxel fog height fog: world Z of the base, auto = lowest floor; env.json HeightFog.base takes priority");
	r_volumetricFogHeightFalloff = R_Cvar("r_volumetricFogHeightFalloff", "256", CVAR_ARCHIVE, "Froxel fog height fog: height (world units) over which the density falls by a factor e above the base height");
	R_CvarRange(r_volumetricFogHeightFalloff, 1.0f, 65536.0f, qfalse);
	r_volumetricFogHeightMaxDensity = R_Cvar("r_volumetricFogHeightMaxDensity", "1", CVAR_ARCHIVE, "Froxel fog height fog: maximum density below the base height, as a multiple of the base density");
	R_CvarRange(r_volumetricFogHeightMaxDensity, 1.0f, 64.0f, qfalse);
	r_volumetricFogHeightTopHeight = R_Cvar("r_volumetricFogHeightTopHeight", "0", CVAR_ARCHIVE, "Froxel fog height fog: height above the base where the medium fades out (soft cutoff), 0 = none");
	R_CvarRange(r_volumetricFogHeightTopHeight, 0.0f, 65536.0f, qfalse);
	r_volumetricFogHeightColor = R_Cvar("r_volumetricFogHeightColor", "0.7 0.75 0.8", CVAR_ARCHIVE, "Froxel fog height fog: scattering color (albedo), \"r g b\" in 0..1 as fogParms");
	r_volumetricFogHeightExtinction = R_Cvar("r_volumetricFogHeightExtinction", "1 1 1", CVAR_ARCHIVE, "Froxel fog height fog: relative extinction per channel \"r g b\", normalized to mean 1 (r_volumetricFogRGBExtinction)");
	r_volumetricFogNoise = R_Cvar("r_volumetricFogNoise", "0", CVAR_ARCHIVE, "Froxel fog: media with world space noise density, bits: 1 height fog, 2 BSP fog volumes, 4 global fog, 8 local fog volumes with the noise flag (0 = homogeneous)");
	R_CvarRange(r_volumetricFogNoise, 0, 15, qtrue);
	r_volumetricFogNoiseScale = R_Cvar("r_volumetricFogNoiseScale", "4096", CVAR_ARCHIVE, "Froxel fog noise: period of the macro noise tile (world units)");
	R_CvarRange(r_volumetricFogNoiseScale, 64.0f, 65536.0f, qfalse);
	r_volumetricFogNoiseContrast = R_Cvar("r_volumetricFogNoiseContrast", "1", CVAR_ARCHIVE, "Froxel fog noise: contrast of the macro noise, 0 = homogeneous, 1 = density 0..2x, higher = sparser clumps (the mean density is kept)");
	R_CvarRange(r_volumetricFogNoiseContrast, 0.0f, 4.0f, qfalse);
	r_volumetricFogNoiseDetailScale = R_Cvar("r_volumetricFogNoiseDetailScale", "900", CVAR_ARCHIVE, "Froxel fog noise: period of the detail noise tile (world units)");
	R_CvarRange(r_volumetricFogNoiseDetailScale, 16.0f, 65536.0f, qfalse);
	r_volumetricFogNoiseDetailContrast = R_Cvar("r_volumetricFogNoiseDetailContrast", "0", CVAR_ARCHIVE, "Froxel fog noise: contrast of the detail noise (second texture sample), 0 = off");
	R_CvarRange(r_volumetricFogNoiseDetailContrast, 0.0f, 4.0f, qfalse);
	r_volumetricFogNoiseWind = R_Cvar("r_volumetricFogNoiseWind", "0 0 0", CVAR_ARCHIVE, "Froxel fog noise: wind, \"x y z\" world units per second, the noise drifts with it (0 = world stable)");

	r_sunShadows = R_Cvar( "r_sunShadows", "1", CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow maps: 0 off, 1 on" );
	r_shadowFilter = R_Cvar( "r_shadowFilter", "1", CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow edge filter: 0 none, 1 soft, 2 softer" );
	r_shadowMapSize = R_Cvar( "r_shadowMapSize", "1024", CVAR_ARCHIVE | CVAR_LATCH, "Resolution of the sun shadow maps" );
	r_shadowCascadeZNear = R_Cvar( "r_shadowCascadeZNear", "4", CVAR_ARCHIVE | CVAR_LATCH, "Near distance of the first sun shadow cascade" );
	r_shadowCascadeZFar = R_Cvar( "r_shadowCascadeZFar", "3072", CVAR_ARCHIVE | CVAR_LATCH, "Far distance of the last sun shadow cascade" );
	r_shadowCascadeZBias = R_Cvar( "r_shadowCascadeZBias", "-320", CVAR_ARCHIVE | CVAR_LATCH, "Depth bias of the sun shadow cascades" );
	r_sunShadowMode = R_Cvar( "r_sunShadowMode", "0", CVAR_ARCHIVE | CVAR_LATCH, "Sun shadows: 0 legacy, 1 stabilized blended PCSS" );
	R_CvarRange( r_sunShadowMode, 0, 1, qtrue );
	r_sunShadowCache = R_Cvar( "r_sunShadowCache", "0", CVAR_ARCHIVE | CVAR_LATCH, "Static BSP sun cache: 0 off, 1 exact reuse, 2 scrolling clipmaps (experimental), 3 persistent world-space disk pages (experimental)" );
	R_CvarRange( r_sunShadowCache, 0, 3, qtrue );
	r_sunShadowVerify = R_Cvar( "r_sunShadowVerify", "0", CVAR_CHEAT, "Compare cached sun depth with a full render: 1 next scene, 2 continuous (slow readbacks)" );
	R_CvarRange( r_sunShadowVerify, 0, 2, qtrue );
	r_sunShadowBenchmark = R_Cvar("r_sunShadowBenchmark", "0", CVAR_CHEAT, "Measure this many complete frame intervals, with GPU readbacks/stats disabled");
	R_CvarRange(r_sunShadowBenchmark, 0, 10000, qtrue);
	r_sunShadowAlphaCasters = R_Cvar( "r_sunShadowAlphaCasters", "1", CVAR_ARCHIVE | CVAR_LATCH, "Shadows 2.0 foliage/cutout sun casters and receivers (q3map_alphashadow and surfaceSprites)" );
	R_CvarRange( r_sunShadowAlphaCasters, 0, 1, qtrue );
	r_shadowCascadeBlend = R_Cvar( "r_shadowCascadeBlend", "0.10", CVAR_ARCHIVE, "Cascade transition width as a fraction of the smaller adjacent cascade" );
	R_CvarRange( r_shadowCascadeBlend, 0.0f, 0.3f, qfalse );
	r_shadowDepthBias = R_Cvar( "r_shadowDepthBias", "0.15", CVAR_ARCHIVE, "Sun shadow constant receiver bias in world units" );
	R_CvarRange( r_shadowDepthBias, 0.0f, 8.0f, qfalse );
	r_shadowNormalBias = R_Cvar( "r_shadowNormalBias", "0.75", CVAR_ARCHIVE, "Sun shadow normal offset in cascade texels" );
	R_CvarRange( r_shadowNormalBias, 0.0f, 8.0f, qfalse );
	r_shadowSlopeBias = R_Cvar( "r_shadowSlopeBias", "1.0", CVAR_ARCHIVE, "Sun shadow receiver-plane depth bias scale" );
	R_CvarRange( r_shadowSlopeBias, 0.0f, 4.0f, qfalse );
	r_shadowReceiverBiasClamp = R_Cvar( "r_shadowReceiverBiasClamp", "4.0", CVAR_ARCHIVE, "Maximum receiver-plane correction in world units" );
	R_CvarRange( r_shadowReceiverBiasClamp, 0.0f, 32.0f, qfalse );
	r_shadowPCSS = R_Cvar( "r_shadowPCSS", "1", CVAR_ARCHIVE, "Contact-hardening PCSS for sun cascades" );
	R_CvarRange( r_shadowPCSS, 0, 1, qtrue );
	r_shadowPCSSQuality = R_Cvar( "r_shadowPCSSQuality", "1", CVAR_ARCHIVE, "Sun PCSS quality: 0 low, 1 high, 2 ultra" );
	R_CvarRange( r_shadowPCSSQuality, 0, 2, qtrue );
	r_shadowSunAngularDiameter = R_Cvar( "r_shadowSunAngularDiameter", "0.53", CVAR_ARCHIVE, "Apparent sun diameter in degrees" );
	R_CvarRange( r_shadowSunAngularDiameter, 0.0f, 8.0f, qfalse );
	r_shadowPCSSMaxPenumbra = R_Cvar( "r_shadowPCSSMaxPenumbra", "32", CVAR_ARCHIVE, "Maximum PCSS penumbra radius in world units" );
	R_CvarRange( r_shadowPCSSMaxPenumbra, 0.0f, 256.0f, qfalse );
	r_sunShadowModelLayer = R_Cvar( "r_sunShadowModelLayer", "0", CVAR_ARCHIVE | CVAR_LATCH, "Sharp sun shadows of models (characters, NPCs, md3 props) over the softer scenery shadow, with r_sunShadowMode 1: 0 one shared shadow, 1 models in their own sharp layer, 2 the older variant: harder edges where character shadows meet scenery");
	R_CvarRange(r_sunShadowModelLayer, 0, 2, qtrue);
	r_shadowDebug = R_Cvar( "r_shadowDebug", "0", CVAR_CHEAT, "Sun shadow debug view: 1 cascades, 2 depth, 3 PCF, 4 blockers, 5 penumbra, 6 PCSS, 7 contact, 8 final, 9 bias, 10 point shadows, 11 G2 receivers, 12 sun on characters (r_entitySunLight), 13 model layer (r_sunShadowModelLayer 2)");
	R_CvarRange( r_shadowDebug, 0, 13, qtrue );
	r_shadowCasterLod = R_Cvar( "r_shadowCasterLod", "0", CVAR_ARCHIVE, "Ghoul2 models in sun cascades and dlight shadow cubes use the LOD of the camera view (0 = LOD of the shadow view)" );
	R_CvarRange( r_shadowCasterLod, 0, 1, qtrue );
	r_shadowCasterStats = R_Cvar( "r_shadowCasterStats", "0", CVAR_CHEAT, "Print Ghoul2 shadow caster counts and LODs per view type once per second" );
	r_shadowSinglePass = R_Cvar( "r_shadowSinglePass", "0", CVAR_ARCHIVE, "Point light shadows: 1 = one scene walk per light, filtered per cube face (same depth as 0)" );
	r_skyPortalCull = R_Cvar( "r_skyPortalCull", "0", CVAR_ARCHIVE, "1 = the skyportal view only shades the pixels the opaque world of the main view leaves uncovered" );
	r_shadowVerify = R_Cvar( "r_shadowVerify", "0", CVAR_CHEAT, "1 = next frame, render every point light shadow cube both ways (r_shadowSinglePass 0 and 1) and print the depth texels that differ" );
	r_shadowLayered = R_Cvar( "r_shadowLayered", "0", CVAR_ARCHIVE, "With r_shadowSinglePass 1: 1 = all 6 faces of a point light cube in one layered draw command when supported" );
	r_shadowCache = R_Cvar( "r_shadowCache", "0", CVAR_ARCHIVE, "Reuse unchanged point/spot shadow maps; conservative around animated world surfaces and dynamic casters" );
	R_CvarRange( r_shadowCache, 0, 1, qtrue );
	r_shadowStaticCache = R_Cvar( "r_shadowStaticCache", "0", CVAR_ARCHIVE, "Point/spot shadows: keep a copy of the static world depth per shadow slot and redraw only moving casters over it when the light is unchanged (needs GL 4.3 / ARB_copy_image); 2 = verify one frame against a full render" );
	R_CvarRange( r_shadowStaticCache, 0, 2, qtrue );
	r_shadowStats = R_Cvar( "r_shadowStats", "0", CVAR_CHEAT, "Print per-frame CPU front-end / submit cost of shadow and camera views once per second" );
	r_dlightShadowBias = R_Cvar( "r_dlightShadowBias", "0", CVAR_ARCHIVE, "Dynamic light shadow cube bias: 0 legacy slope offset, 1 texel scaled normal + clamped slope bias" );
	R_CvarRange( r_dlightShadowBias, 0, 1, qtrue );
	r_contactShadowSoft = R_Cvar( "r_contactShadowSoft", "0", CVAR_ARCHIVE, "Contact shadows: 0 first hit, 1 soft depth weighted hits with steps packed near the receiver" );
	R_CvarRange( r_contactShadowSoft, 0, 1, qtrue );
	r_entitySunLight = R_Cvar( "r_entitySunLight", "0", CVAR_ARCHIVE | CVAR_LATCH, "Sun on characters: light models from the real sun direction with per-pixel sun shadows instead of the light grid's merged light; needs r_sunlightMode and r_entityLightGrid 0. 0 off, 1 on");
	R_CvarRange( r_entitySunLight, 0, 1, qtrue );
	r_entityShadowAmbient = R_Cvar( "r_entityShadowAmbient", "0", CVAR_ARCHIVE, "Models (players, NPCs, items): share of the light grid ambient that the sun shadow darkens like the world lightmap (0 = legacy: only the directed light is shadowed, 1 = the whole ambient, as the lightmap). r_sunlightMode 1 only" );
	R_CvarRange( r_entityShadowAmbient, 0.0f, 1.0f, qfalse );
	r_ignoreDstAlpha = R_Cvar( "r_ignoreDstAlpha", "1", CVAR_ARCHIVE | CVAR_LATCH, "Treat blend modes using the framebuffer alpha as if it were 1: 0 off, 1 on" );
	r_refractionChromaticAberration = R_Cvar( "r_refractionChromaticAberration", "-1", CVAR_ARCHIVE, "Color fringes of refractive distortion: -1 Rend2's original fixed fringes, 0 none, above 0 the fringe width around the green index (0.05 a subtle fringe)");
	R_CvarRange(r_refractionChromaticAberration, -1, 0.5, qfalse);

	//
	// temporary latched variables that can only change over a restart
	//
	r_fullbright = R_Cvar( "r_fullbright", "0", CVAR_LATCH|CVAR_CHEAT, "Debug: draw everything without lighting: 0 off, 1 on" );
	r_mapOverBrightBits = R_Cvar( "r_mapOverBrightBits", "0", CVAR_LATCH, "Brightening of the map's baked lighting: 0 none, each step doubles it" );
	r_intensity = R_Cvar( "r_intensity", "1", CVAR_LATCH, "Global texture brightness multiplier" );
	r_singleShader = R_Cvar( "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH, "Debug: draw every surface with the default shader: 0 off, 1 on" );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = R_Cvar( "r_lodCurveError", "250", CVAR_ARCHIVE|CVAR_CHEAT, "Curved surface level of detail: higher keeps more detail at a distance" );
	r_lodbias = R_Cvar( "r_lodbias", "0", CVAR_ARCHIVE, "Model level of detail bias: 0 normal, higher picks simpler models sooner" );
	r_flares = R_Cvar( "r_flares", "0", CVAR_ARCHIVE, "Light flares: 0 off, 1 on" );
	r_znear = R_Cvar( "r_znear", "4", CVAR_CHEAT, "Near clipping plane distance" );
	R_CvarRange( r_znear, 0.001f, 200, qfalse );
	r_autolodscalevalue	= R_Cvar( "r_autolodscalevalue", "0", CVAR_ROM, "Model level of detail scale chosen by the field of view (set by the renderer)" );
	r_zproj = R_Cvar( "r_zproj", "64", CVAR_ARCHIVE, "Projection distance of the stereo convergence plane" );
	r_stereoSeparation = R_Cvar( "r_stereoSeparation", "64", CVAR_ARCHIVE, "Eye separation for stereo rendering" );
	r_ignoreGLErrors = R_Cvar( "r_ignoreGLErrors", "1", CVAR_ARCHIVE, "Ignore OpenGL errors instead of stopping: 0 stop, 1 ignore" );
	r_fastsky = R_Cvar( "r_fastsky", "0", CVAR_ARCHIVE, "Draw the sky as a flat color: 0 off, 1 on" );
	r_inGameVideo = R_Cvar( "r_inGameVideo", "1", CVAR_ARCHIVE, "Play in-game cinematics on surfaces: 0 off, 1 on" );
	r_drawSun = R_Cvar( "r_drawSun", "0", CVAR_ARCHIVE, "Draw the sun disc: 0 off, 1 on" );
	r_dynamiclight = R_Cvar( "r_dynamiclight", "1", CVAR_ARCHIVE, "Dynamic lights from sabers, shots and explosions: 0 off, 1 on" );
	r_finish = R_Cvar( "r_finish", "0", CVAR_ARCHIVE, "Wait for the GPU at the end of each frame: 0 off, 1 on" );
	r_textureMode = R_Cvar( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE, "Texture filter, for example GL_LINEAR_MIPMAP_LINEAR (trilinear) or GL_LINEAR_MIPMAP_NEAREST (bilinear)" );
	r_markcount = R_Cvar( "r_markcount", "100", CVAR_ARCHIVE, "Most impact marks (blaster scorches, blood) kept at once" );
	r_gamma = R_Cvar( "r_gamma", "1", CVAR_ARCHIVE, "Display gamma: 1 normal, higher is brighter" );
	r_facePlaneCull = R_Cvar( "r_facePlaneCull", "1", CVAR_ARCHIVE, "Cull flat surfaces facing away from the camera: 0 off, 1 on" );

	r_parallaxMapping = R_Cvar("r_parallaxMapping", "0", CVAR_ARCHIVE, "Disable/enable parallax mapping");
	r_pomSelfShadow = R_Cvar( "r_pomSelfShadow", "0", CVAR_ARCHIVE | CVAR_LATCH, "POM self shadowing of direct light (sun, dynamic lights), needs r_parallaxMapping 1; vid_restart to compile it in" );
	R_CvarRange( r_pomSelfShadow, 0, 1, qtrue );
	r_pomSelfShadowLightMode = R_Cvar( "r_pomSelfShadowLightMode", "1", CVAR_ARCHIVE, "POM self shadowed dynamic lights: 0 sun only, 1 sun + strongest local light, 2 sun + strongest r_pomSelfShadowMaxLocalLights, 3 all lights" );
	R_CvarRange( r_pomSelfShadowLightMode, 0, 3, qtrue );
	r_pomSelfShadowMaxLocalLights = R_Cvar( "r_pomSelfShadowMaxLocalLights", "2", CVAR_ARCHIVE, "Local lights with POM self shadow per pixel for r_pomSelfShadowLightMode 2" );
	R_CvarRange( r_pomSelfShadowMaxLocalLights, 1, 4, qtrue );
	r_pomSelfShadowSteps = R_Cvar( "r_pomSelfShadowSteps", "12", CVAR_ARCHIVE, "Height samples of a POM self shadow ray" );
	R_CvarRange( r_pomSelfShadowSteps, 4, 32, qtrue );
	r_pomSelfShadowStrength = R_Cvar( "r_pomSelfShadowStrength", "1", CVAR_ARCHIVE, "POM self shadow strength, 0 none .. 1 physical (times the pomSelfShadow material keyword)" );
	R_CvarRange( r_pomSelfShadowStrength, 0, 1, qfalse );
	r_pomSelfShadowBias = R_Cvar( "r_pomSelfShadowBias", "0.02", CVAR_ARCHIVE, "Start of the POM self shadow ray above the hit, in height field depth (0..1)" );
	R_CvarRange( r_pomSelfShadowBias, 0, 0.25, qfalse );
	r_pomSelfShadowSoftness = R_Cvar( "r_pomSelfShadowSoftness", "4", CVAR_ARCHIVE, "POM self shadow penumbra: occluder penetration to full shadow, higher = harder" );
	R_CvarRange( r_pomSelfShadowSoftness, 0.5, 64, qfalse );
	r_pomAdaptiveSteps = R_Cvar( "r_pomAdaptiveSteps", "0", CVAR_ARCHIVE, "POM view ray steps by view angle (r_pomMinSteps frontal .. r_pomMaxSteps grazing), 0 = legacy 16 + 8" );
	R_CvarRange( r_pomAdaptiveSteps, 0, 1, qtrue );
	r_pomMinSteps = R_Cvar( "r_pomMinSteps", "8", CVAR_ARCHIVE, "POM linear view ray steps at normal incidence (r_pomAdaptiveSteps 1)" );
	R_CvarRange( r_pomMinSteps, 4, 64, qtrue );
	r_pomMaxSteps = R_Cvar( "r_pomMaxSteps", "32", CVAR_ARCHIVE, "POM linear view ray steps at grazing angles (r_pomAdaptiveSteps 1)" );
	R_CvarRange( r_pomMaxSteps, 4, 64, qtrue );
	r_pomBinarySteps = R_Cvar( "r_pomBinarySteps", "8", CVAR_ARCHIVE, "POM binary refinement steps (r_pomAdaptiveSteps 1)" );
	R_CvarRange( r_pomBinarySteps, 0, 16, qtrue );
	r_pomFadeStart = R_Cvar( "r_pomFadeStart", "0", CVAR_ARCHIVE, "Distance where POM starts to fade to normal mapping (with r_pomFadeEnd > r_pomFadeStart, 0 = off)" );
	R_CvarRange( r_pomFadeStart, 0, 65536, qfalse );
	r_pomFadeEnd = R_Cvar( "r_pomFadeEnd", "0", CVAR_ARCHIVE, "Distance beyond which POM is skipped (normal mapping only), 0 = no fade" );
	R_CvarRange( r_pomFadeEnd, 0, 65536, qfalse );
	r_pomDebug = R_Cvar( "r_pomDebug", "0", CVAR_CHEAT | CVAR_LATCH, "POM debug view: 1 raw depth, 2 displaced UV, 3 view ray depth, 4 sun self shadow, 5 local light self shadow, 6 view steps, 7 shadow steps, 8 parallax fade (vid_restart)" );
	R_CvarRange( r_pomDebug, 0, 8, qtrue );
	r_pomDebugFreezeLight = R_Cvar( "r_pomDebugFreezeLight", "0", CVAR_CHEAT, "Freeze the sun direction used by POM self shadowing" );
	R_CvarRange( r_pomDebugFreezeLight, 0, 1, qtrue );
	r_pomSilhouette = R_Cvar( "r_pomSilhouette", "0", CVAR_ARCHIVE | CVAR_LATCH, "Silhouette parallax occlusion mapping for materials with the pomSilhouette keyword (needs r_parallaxMapping 1)" );
	R_CvarRange( r_pomSilhouette, 0, 1, qtrue );
	r_pomSilhouetteDistance = R_Cvar( "r_pomSilhouetteDistance", "512", CVAR_ARCHIVE, "Silhouette POM: view distance where the shell ends and ordinary POM takes over (0 = ordinary POM everywhere)" );
	R_CvarRange( r_pomSilhouetteDistance, 0.0f, 8192.0f, qfalse );
	r_pomSilhouetteFade = R_Cvar( "r_pomSilhouetteFade", "96", CVAR_ARCHIVE, "Silhouette POM: width of the dithered crossfade band to ordinary POM, world units (0 = hard switch)" );
	R_CvarRange( r_pomSilhouetteFade, 0.0f, 1024.0f, qfalse );
	r_pomSilhouetteSteps = R_Cvar( "r_pomSilhouetteSteps", "12", CVAR_ARCHIVE, "Silhouette POM: linear ray steps when looking along the surface normal" );
	R_CvarRange( r_pomSilhouetteSteps, 4, 64, qtrue );
	r_pomSilhouetteMaxSteps = R_Cvar( "r_pomSilhouetteMaxSteps", "48", CVAR_ARCHIVE, "Silhouette POM: linear ray steps at grazing angles" );
	R_CvarRange( r_pomSilhouetteMaxSteps, 4, 128, qtrue );
	r_pomSilhouetteBinarySteps = R_Cvar( "r_pomSilhouetteBinarySteps", "6", CVAR_ARCHIVE, "Silhouette POM: binary refinement steps after the linear search" );
	R_CvarRange( r_pomSilhouetteBinarySteps, 0, 16, qtrue );
	r_pomSilhouetteViewDependence = R_Cvar( "r_pomSilhouetteViewDependence", "1", CVAR_ARCHIVE, "Silhouette POM: how fast the step count grows towards grazing angles (0 = always r_pomSilhouetteSteps)" );
	R_CvarRange( r_pomSilhouetteViewDependence, 0.0f, 4.0f, qfalse );
	r_pomSilhouetteShadows = R_Cvar( "r_pomSilhouetteShadows", "1", CVAR_ARCHIVE, "Silhouette POM: shells cast the displaced surface into the sun shadow cascades" );
	R_CvarRange( r_pomSilhouetteShadows, 0, 1, qtrue );
	r_pomSilhouetteContactShadows = R_Cvar( "r_pomSilhouetteContactShadows", "0", CVAR_ARCHIVE, "Silhouette POM: apply screen-space sun contact shadows to shell pixels (they march the displaced depth and turn the grooves hard black)" );
	R_CvarRange( r_pomSilhouetteContactShadows, 0, 1, qtrue );
	r_pomSilhouetteDebug = R_Cvar( "r_pomSilhouetteDebug", "0", CVAR_CHEAT, "Silhouette POM debug view: 1 translucent shell, 2 shell wireframe, 3 original mesh wireframe, 4 top cap / walls, 5 boundary walls, 6 discarded shell pixels, 7 virtual hit depth, 8 ray steps, 10 linear depth, 11 material normal (ordinary / silhouette POM: r_pomSilhouetteCompare)");
	R_CvarRange( r_pomSilhouetteDebug, 0, 11, qtrue );
	r_autoPOMSilhouetteMode = R_Cvar( "r_autoPOMSilhouetteMode", "0", CVAR_ARCHIVE, "Silhouette POM for every material with an ordinary POM height map (set with the r_autoPOMSilhouette command, needs r_pomSilhouette 1)" );
	R_CvarRange( r_autoPOMSilhouetteMode, 0, 1, qtrue );

	r_ambientScale = R_Cvar( "r_ambientScale", "0.6", CVAR_CHEAT, "Scale of the ambient light of models from the light grid" );
	r_directedScale = R_Cvar( "r_directedScale", "1", CVAR_CHEAT, "Scale of the directed light of models from the light grid" );

	r_anaglyphMode = R_Cvar( "r_anaglyphMode", "0", CVAR_ARCHIVE, "Red/cyan stereo 3D: 0 off, 1 red-cyan, other values other color pairs" );
	r_mergeMultidraws = R_Cvar( "r_mergeMultidraws", "1", CVAR_ARCHIVE, "Merge draw calls of the same shader: 0 off, 1 on, 2 more aggressive" );
	r_mergeLeafSurfaces = R_Cvar( "r_mergeLeafSurfaces", "1", CVAR_ARCHIVE, "Merge world surfaces of the same leaf at load: 0 off, 1 on" );

	//
	// temporary variables that can change at any time
	//
	r_showImages = R_Cvar( "r_showImages", "0", CVAR_TEMP, "Debug: show all loaded textures on screen: 0 off, 1 on" );

	r_debugLight = R_Cvar( "r_debuglight", "0", CVAR_TEMP, "Debug: print the light grid values of models: 0 off, 1 on" );
	r_debugSort = R_Cvar( "r_debugSort", "0", CVAR_CHEAT, "Debug: only draw shaders with a sort value below this, 0 all" );
	r_printShaders = R_Cvar( "r_printShaders", "0", 0, "Debug: print the shaders as they load: 0 off, 1 on" );
	r_saveFontData = R_Cvar( "r_saveFontData", "0", 0, "Write font data files when fonts load: 0 off, 1 on" );

	r_forceParallaxBias = R_Cvar( "r_forceParallaxBias", "0", CVAR_TEMP, "Debug: parallax bias of every material instead of its own, 0 the material's" );
	R_CvarRange(r_forceParallaxBias, 0.0f, 1.0f, qfalse);

	r_nocurves = R_Cvar( "r_nocurves", "0", CVAR_CHEAT, "Debug: don't draw curved surfaces: 0 draw, 1 skip" );
	r_drawworld = R_Cvar( "r_drawworld", "1", CVAR_CHEAT, "Debug: draw the world: 0 skip, 1 draw" );
	r_drawfog = R_Cvar( "r_drawfog", "2", CVAR_CHEAT, "Fog: 0 off, 1 fog, 2 fog with the fog surfaces drawn as well" );
	r_lightmap = R_Cvar( "r_lightmap", "0", 0, "Debug: show only the lightmaps: 0 off, 1 on" );
	r_portalOnly = R_Cvar( "r_portalOnly", "0", CVAR_CHEAT, "Debug: only draw what is seen through portals and mirrors: 0 off, 1 on" );

	r_skipBackEnd = R_Cvar( "r_skipBackEnd", "0", CVAR_CHEAT, "Debug: skip all drawing (CPU timing): 0 off, 1 on" );

	r_measureOverdraw = R_Cvar( "r_measureOverdraw", "0", CVAR_CHEAT, "Debug: measure overdraw with the stencil buffer: 0 off, 1 on" );
	r_lodscale = R_Cvar( "r_lodscale", "5", CVAR_CHEAT, "Model level of detail scale: higher keeps detailed models farther" );
	r_norefresh = R_Cvar( "r_norefresh", "0", CVAR_CHEAT, "Debug: don't draw the 3D view: 0 draw, 1 skip" );
	r_drawentities = R_Cvar( "r_drawentities", "1", CVAR_CHEAT, "Debug: draw entities (models, sprites): 0 skip, 1 draw" );
	r_ignore = R_Cvar( "r_ignore", "1", CVAR_CHEAT, "Unused" );
	r_nocull = R_Cvar( "r_nocull", "0", CVAR_CHEAT, "Debug: don't cull surfaces outside the view: 0 cull, 1 draw all" );
	r_novis = R_Cvar( "r_novis", "0", CVAR_CHEAT, "Debug: ignore the map's visibility data: 0 use it, 1 ignore it" );
	r_showcluster = R_Cvar( "r_showcluster", "0", CVAR_CHEAT, "Debug: print the visibility cluster of the camera: 0 off, 1 on" );
	r_speeds = R_Cvar( "r_speeds", "0", CVAR_CHEAT, "Print rendering statistics every frame; the number selects the group (1 counts, 100 GPU timers and more)" );
	r_verbose = R_Cvar( "r_verbose", "0", CVAR_CHEAT, "Print extra renderer messages: 0 off, 1 on" );
	r_logFile = R_Cvar( "r_logFile", "0", CVAR_CHEAT, "Debug: log this many frames of OpenGL calls to a file, 0 off" );
	r_debugSurface = R_Cvar( "r_debugSurface", "0", CVAR_CHEAT, "Debug: draw curved surface control grids: 0 off, 1 on" );
	r_nobind = R_Cvar( "r_nobind", "0", CVAR_CHEAT, "Debug: skip texture binds: 0 bind, 1 skip" );
	r_showtris = R_Cvar( "r_showtris", "0", CVAR_CHEAT, "Debug: draw triangle outlines: 0 off, 1 on" );
	r_showsky = R_Cvar( "r_showsky", "0", CVAR_CHEAT, "Debug: draw the sky in front of everything: 0 off, 1 on" );
	r_shownormals = R_Cvar( "r_shownormals", "0", CVAR_CHEAT, "Debug: draw vertex normals: 0 off, 1 on" );
	r_clear = R_Cvar( "r_clear", "0", CVAR_CHEAT, "Clear the screen every frame (shows holes in pink): 0 off, 1 on" );
	r_offsetFactor = R_Cvar( "r_offsetfactor", "-1", CVAR_CHEAT, "Polygon offset factor of decals" );
	r_offsetUnits = R_Cvar( "r_offsetunits", "-2", CVAR_CHEAT, "Polygon offset units of decals" );

	r_shadowOffsetFactor = R_Cvar( "r_shadowOffsetFactor", "1.0", CVAR_CHEAT, "Polygon offset factor of shadow maps" );
	r_shadowOffsetUnits = R_Cvar( "r_shadowOffsetUnits", "1.0", CVAR_CHEAT, "Polygon offset units of shadow maps" );

	r_drawBuffer = R_Cvar( "r_drawBuffer", "GL_BACK", CVAR_CHEAT, "Debug: buffer to draw into, GL_BACK or GL_FRONT" );
	r_lockpvs = R_Cvar( "r_lockpvs", "0", CVAR_CHEAT, "Debug: keep the current visible set while the camera moves: 0 off, 1 on" );
	r_noportals = R_Cvar( "r_noportals", "0", CVAR_CHEAT, "Debug: don't draw portals and mirrors: 0 draw, 1 skip" );
	r_shadows = R_Cvar( "cg_shadows", "1", 0, "Character shadows: 0 off, 1 blobs, 2 stencil, 3 projected" );

	r_marksOnTriangleMeshes = R_Cvar( "r_marksOnTriangleMeshes", "0", CVAR_ARCHIVE, "Allow impact marks on triangle mesh surfaces: 0 off, 1 on" );

	r_aviMotionJpegQuality = R_Cvar( "r_aviMotionJpegQuality", "90", CVAR_ARCHIVE, "JPEG quality of recorded videos (0..100)" );
	r_screenshotJpegQuality = R_Cvar( "r_screenshotJpegQuality", "90", CVAR_ARCHIVE, "JPEG quality of screenshots (0..100)" );
	r_surfaceSprites = R_Cvar( "r_surfaceSprites", "1", CVAR_ARCHIVE, "Surface sprites (grass, flowers from map shaders): 0 off, 1 on" );

	r_aspectCorrectFonts = R_Cvar( "r_aspectCorrectFonts", "0", CVAR_ARCHIVE, "Keep text proportions on wide screens: 0 off, 1 on" );
	r_maxpolys = R_Cvar( "r_maxpolys", XSTRING( DEFAULT_MAX_POLYS ), 0, "");
	r_maxpolyverts = R_Cvar( "r_maxpolyverts", XSTRING( DEFAULT_MAX_POLYVERTS ), 0, "" );

/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
	r_noPrecacheGLA						= R_Cvar( "r_noPrecacheGLA", "0", CVAR_CHEAT, "Debug: don't precache animation files: 0 precache, 1 skip" );
#endif
	r_noServerGhoul2					= R_Cvar( "r_noserverghoul2", "0", CVAR_CHEAT, "Debug: don't load Ghoul2 models on the server: 0 load, 1 skip" );
	r_Ghoul2AnimSmooth					= R_Cvar( "r_ghoul2animsmooth", "0.3", CVAR_NONE, "Ghoul2 animation blending time between animations" );
	r_Ghoul2UnSqashAfterSmooth			= R_Cvar( "r_ghoul2unsqashaftersmooth", "1", CVAR_NONE, "Ghoul2: renormalize bones after blending: 0 off, 1 on" );
	broadsword							= R_Cvar( "broadsword", "0", CVAR_ARCHIVE, "Ragdolls (broadsword physics): 0 off, 1 on, 2 on with debug" );
	r_ragSoftLimits						= R_Cvar( "r_ragSoftLimits",					"0",						CVAR_ARCHIVE, "Ragdoll soft joint limits: tension in the last part of each range, bounce back, weak pull to the range centre" );
	r_ragSoftLimitZone					= R_Cvar( "r_ragSoftLimitZone",				"0.18",						CVAR_ARCHIVE, "Fraction of each half range where ragdoll joint tension builds" );
	r_ragSoftLimitStiffness				= R_Cvar( "r_ragSoftLimitStiffness",			"0.25",						CVAR_ARCHIVE, "Ragdoll soft limit tension at the hard end (zone widths per solver iteration^2)" );
	r_ragNeutralPull					= R_Cvar( "r_ragNeutralPull",					"0.0002",					CVAR_ARCHIVE, "Ragdoll pull toward the joint range centre (per solver iteration^2)" );
	broadsword_kickbones				= R_Cvar( "broadsword_kickbones", "1", CVAR_NONE, "Ragdolls: hits push bones: 0 off, 1 on" );
	broadsword_kickorigin				= R_Cvar( "broadsword_kickorigin", "1", CVAR_NONE, "Ragdolls: hits push the body: 0 off, 1 on" );
	broadsword_dontstopanim				= R_Cvar( "broadsword_dontstopanim", "0", CVAR_NONE, "Ragdolls: keep the animation running under the ragdoll: 0 off, 1 on" );
	broadsword_waitforshot				= R_Cvar( "broadsword_waitforshot", "0", CVAR_NONE, "Ragdolls: wait for a hit before ragdolling: 0 off, 1 on" );
	broadsword_playflop					= R_Cvar( "broadsword_playflop", "1", CVAR_NONE, "Ragdolls: play the fall animation: 0 off, 1 on" );
	broadsword_smallbbox				= R_Cvar( "broadsword_smallbbox", "0", CVAR_NONE, "Ragdolls: small bounding box: 0 off, 1 on" );
	broadsword_extra1					= R_Cvar( "broadsword_extra1", "0", CVAR_NONE, "Ragdolls: debug option 1" );
	broadsword_extra2					= R_Cvar( "broadsword_extra2", "0", CVAR_NONE, "Ragdolls: debug option 2" );
	broadsword_effcorr					= R_Cvar( "broadsword_effcorr", "1", CVAR_NONE, "Ragdolls: effector correction: 0 off, 1 on" );
	broadsword_ragtobase				= R_Cvar( "broadsword_ragtobase", "2", CVAR_NONE, "Ragdolls: blend from ragdoll back to the base pose (mode)" );
	broadsword_dircap					= R_Cvar( "broadsword_dircap", "64", CVAR_NONE, "Ragdolls: limit on the hit direction push" );
/*
Ghoul2 Insert End
*/

	r_patchStitching = R_Cvar("r_patchStitching", "1", CVAR_ARCHIVE, "Enable stitching of neighbouring patch surfaces" );

	se_language = R_Cvar( "se_language", "english", CVAR_ARCHIVE | CVAR_NORESTART, "Language of the game text, for example english, german, french" );

	// r_<feature>Compare of every feature (tr_compare.inc), after their masters
	R_CompareRegister();

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
		|| r_ssr->integer // hit cache needs previous transforms without color temporal
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
	R_ClearReconstructedLighting(); // before world/hunk pointers are reset
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
	R_ClearReconstructedLighting(); // also valid after a failed/partial R_Init

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
		R_ShutdownPlantSim();
		R_ClearAreaLights();
		R_FoliageInteractionReset();
		R_ShutdownPomSilhouette();
		R_ShutdownVolumetric();
		R_ShutdownRainLens();
		R_WaterSurfaceShutdown();
		R_WaterParticlesShutdown();

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

	// the DLL may be unloaded after this: no worker thread may outlive it
	if ( destroyWindow )
	{
		R_PrefetchReset();
		R_JobsShutdown();
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
	GLSL_EndRegistration();
	R_SourceScratchFlush();
	R_PrefetchFlush();
	if (R_ImageChecksumsEnabled())
		R_WriteImageChecksums("end of registration");
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
GetRefFoliageWaveAPI

Optional extension (tr_public.h): Force push / pull waves through the
foliage, tr_foliageinteract.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refFoliageWaveExport_t* QDECL GetRefFoliageWaveAPI ( void ) {
	static const refFoliageWaveExport_t foliageWaves = { RE_AddFoliageForceWave };
	return &foliageWaves;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefFoliageCutAPI

Optional extension (tr_public.h): attacking saber blades cut the MD3
plants, tr_plantcut.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refFoliageCutExport_t* QDECL GetRefFoliageCutAPI ( void ) {
	static const refFoliageCutExport_t foliageCut = { RE_AddFoliageSaberCut };
	return &foliageCut;
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

extern "C" Q_EXPORT const refPhysicalLightExport_t* QDECL GetRefPhysicalLightAPI ( void ) {
	static const refPhysicalLightExport_t physicalLights = { RE_AddPhysicalLightToScene };
	return &physicalLights;
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefLensWaterAPI

Optional extension (tr_public.h): lens water events, tr_rainlens.cpp
@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" Q_EXPORT const refLensWaterExport_t* QDECL GetRefLensWaterAPI ( void ) {
	static const refLensWaterExport_t lensWater = { RE_AddLensWaterEvent };
	return &lensWater;
}

extern "C" Q_EXPORT const refWaterInteractionExport_t* QDECL GetRefWaterInteractionAPI ( void ) {
	static const refWaterInteractionExport_t waterInteraction = { RE_AddWaterImpulse, RE_SetWaterSources };
	return &waterInteraction;
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
