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


#ifndef TR_LOCAL_H
#define TR_LOCAL_H

#include "qcommon/q_shared.h"
#include "qcommon/qfiles.h"
#include "qcommon/qcommon.h"
#include "rd-common/tr_public.h"
#include "rd-common/tr_common.h"
#include "tr_allocator.h"
#include "tr_extratypes.h"
#include "tr_extramath.h"
#include "tr_fbo.h"
#include "tr_postprocess.h"
#include "iqm.h"
#include "qgl.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#ifdef REND2_SP
typedef enum {
	h_high,
	h_low,
	h_dontcare
} ha_pref;
void* Hunk_Alloc(int size, ha_pref preference);
void* Hunk_AllocateTempMemory(int size);
void Hunk_FreeTempMemory(void* buf);
#else
void* R_Malloc(int iSize, memtag_t eTag);
void* R_Malloc(int iSize, memtag_t eTag, qboolean bZeroit);
#endif

#define GL_INDEX_TYPE		GL_UNSIGNED_INT
typedef unsigned int glIndex_t;

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

// 14 bits
// can't be increased without changing bit packing for drawsurfs
// see QSORT_SHADERNUM_SHIFT
#define MAX_FRAMES (2)
#define SHADERNUM_BITS	14
#define MAX_SHADERS		(1<<SHADERNUM_BITS)

#define	MAX_FBOS      512
#define MAX_VISCOUNTS 5
#define MAX_VBOS      4096
#define MAX_IBOS      4096
#define MAX_G2_BONES  256
#define MAX_GPU_FOGS  24

// Froxel volumetric fog (tr_volumetric.cpp) and its local fog volumes
// (tr_fogvolume.cpp): the scene takes up to MAX_REF_FOG_VOLUMES per frame, the
// most important MAX_GPU_FOG_VOLUMES reach the VolumetricFog block;
// XYZ membership masks are in an R32UI buffer, slice lists are CPU diagnostics.
#define FROXEL_MAX_SLICES     128
#define MAX_REF_FOG_VOLUMES   256
#define MAX_GPU_FOG_VOLUMES   64	// two 32-bit membership masks, UBO budget (see VolumetricFogBlock)
#define FROXEL_LOCAL_POOL     8192	// CPU-only slice diagnostics: 64 volumes x 128 slices
#define FROXEL_EXTINCTION_PALETTE 16	// distinct local volume extinction colors per frame (UBO budget)

// Volumetric FX particles (tr_volparticle.cpp): the scene takes up to
// MAX_REF_VOL_PARTICLES per frame, the MAX_GPU_VOL_PARTICLES most important in
// the froxel frustum reach the VolumetricParticles block, listed per slice in
// a pool of VOL_PARTICLE_POOL 16 bit indices.
#define MAX_REF_VOL_PARTICLES 1024
#define MAX_GPU_VOL_PARTICLES 128	// UBO budget (see VolumetricParticlesBlock)
#define VOL_PARTICLE_POOL     2560
#define MAX_GPU_EMISSIVE_PARTICLES 24	// emissive ones among them (UBO budget, own slots)

// an FX particle medium that scatters (extinction) or glows (emission), or both
#define R_VolParticleHasMedium(p) ((p)->extinction > 0.0f || (p)->emission[0] > 0.0f || \
	(p)->emission[1] > 0.0f || (p)->emission[2] > 0.0f)

#define MAX_CALC_PSHADOWS    64
#define MAX_DRAWN_PSHADOWS    32 // do not increase past 32, because bit flags are used on surfaces
#define PSHADOW_MAP_SIZE      1024
#define DSHADOW_MAP_SIZE      512

// Dynamic lights. The legacy path keeps MAX_DLIGHTS (rd-common/tr_types.h, 32:
// one bit per light in the surface dlight masks). Forward+ (r_forwardPlus,
// tr_forwardplus.cpp) raises the renderer side capacity to MAX_RENDER_DLIGHTS
// without changing the public API. Point light shadow cubes stay limited to
// the MAX_DLIGHTS * 6 layers of pointShadowArrayImage.
#define LEGACY_DLIGHT_LIMIT   MAX_DLIGHTS
#define MAX_RENDER_DLIGHTS    256
#define MAX_DLIGHT_SHADOWS    MAX_DLIGHTS
#define CUBE_MAP_MIPS      8
#define CUBE_MAP_ROUGHNESS_MIPS CUBE_MAP_MIPS - 2
#define CUBE_MAP_SIZE      (1 << CUBE_MAP_MIPS)
#define MAX_RUNTIME_CUBEMAPS 128
#define DIFFUSE_IRRADIANCE_SIZE 16

/*
=====================================================

Renderer-side Cvars
In Q3, these are defined in tr_common.h, which isn't very logical really
In JA, we define these in the tr_local.h, which is much more logical

=====================================================
*/

extern cvar_t	*r_verbose;
extern cvar_t	*r_ignore;

extern cvar_t	*r_detailTextures;

extern cvar_t	*r_znear;
extern cvar_t	*r_zproj;
extern cvar_t	*r_stereoSeparation;

extern cvar_t	*r_skipBackEnd;

extern cvar_t	*r_stereo;
extern cvar_t	*r_anaglyphMode;

extern cvar_t	*r_greyscale;

extern cvar_t	*r_ignorehwgamma;
extern cvar_t	*r_measureOverdraw;

extern cvar_t	*r_inGameVideo;
extern cvar_t	*r_fastsky;
extern cvar_t	*r_drawSun;
extern cvar_t	*r_dynamiclight;

extern cvar_t	*r_lodbias;
extern cvar_t	*r_lodscale;
extern cvar_t	*r_autolodscalevalue;

extern cvar_t	*r_norefresh;
extern cvar_t	*r_drawentities;
extern cvar_t	*r_drawworld;
extern cvar_t	*r_drawfog;
extern cvar_t	*r_speeds;
extern cvar_t	*r_fullbright;
extern cvar_t	*r_novis;
extern cvar_t	*r_nocull;
extern cvar_t	*r_facePlaneCull;
extern cvar_t	*r_showcluster;
extern cvar_t	*r_nocurves;

extern cvar_t	*r_volumetricFog;
extern cvar_t *r_entityLightGrid;
extern cvar_t *r_entityLightGridDebug;
extern cvar_t	*r_volumetricFogDefaultScale;
extern cvar_t	*r_volumetricFogSamples;
extern cvar_t	*r_volumetricFogScale;
extern cvar_t	*r_volumetricFogQuality;
extern cvar_t	*r_volumetricFogGridScale;
extern cvar_t	*r_volumetricFogSlices;
extern cvar_t	*r_volumetricFogFar;
extern cvar_t	*r_volumetricFogAnisotropy;
extern cvar_t	*r_volumetricFogTemporal;
extern cvar_t	*r_volumetricFogHistoryWeight;
extern cvar_t	*r_volumetricFogSunScale;
extern cvar_t	*r_volumetricFogDlightScale;
extern cvar_t	*r_volumetricFogLightTile;
extern cvar_t	*r_volumetricFogStaticScale;
extern cvar_t	*r_volumetricFogStaticDirectional;
extern cvar_t	*r_volumetricSelfShadow;
extern cvar_t	*r_volumetricSelfShadowSamples;
extern cvar_t	*r_volumetricSelfShadowDistance;
extern cvar_t	*r_volumetricSelfShadowOutsideHeightFog;
extern cvar_t	*r_volumetricSelfShadowMaxLights;
extern cvar_t	*r_volumetricMultiScatter;
extern cvar_t	*r_volumetricFogRGBExtinction;
extern cvar_t	*r_volumetricMultiScatterOctaves;
extern cvar_t	*r_volumetricMultiScatterAttenuation;
extern cvar_t	*r_volumetricMultiScatterContribution;
extern cvar_t	*r_volumetricMultiScatterPhase;
extern cvar_t	*r_volumetricMultiScatterLength;
extern cvar_t	*r_volumetricMultiScatterShadowFill;
extern cvar_t	*r_volumetricFogDlightShadows;
extern cvar_t	*r_volumetricFogBloom;
extern cvar_t	*r_volumetricEmission;
extern cvar_t	*r_volumetricFogReset;
extern cvar_t	*r_volumetricFogDebug;
extern cvar_t	*r_volumetricParticles;
extern cvar_t *fx_physicalizationAggregate;
extern cvar_t	*r_spotLights;
extern cvar_t	*r_spotLightShadows;
extern cvar_t	*r_spotLightDebug;
extern cvar_t	*r_spotLightCookies;
extern cvar_t	*r_spotLightCookieDebug;
extern cvar_t	*r_volumetricParticlesMax;
extern cvar_t	*r_volumetricParticlesScale;
extern cvar_t	*r_volumetricParticlesHistory;
extern cvar_t	*r_particleLighting;
extern cvar_t	*r_particleLightingMix;
extern cvar_t	*r_particleLightingScale;
extern cvar_t	*r_particleLightingFloor;
extern cvar_t	*r_particleLightingDebug;
extern cvar_t	*r_volumetricParticlesDebug;
extern cvar_t	*r_volumetricFogFreeze;
extern cvar_t	*r_volumetricFogHeight;
extern cvar_t	*r_volumetricFogHeightOpaqueDistance;
extern cvar_t	*r_volumetricFogHeightBase;
extern cvar_t	*r_volumetricFogHeightFalloff;
extern cvar_t	*r_volumetricFogHeightMaxDensity;
extern cvar_t	*r_volumetricFogHeightTopHeight;
extern cvar_t	*r_volumetricFogHeightColor;
extern cvar_t	*r_volumetricFogHeightExtinction;
extern cvar_t	*r_volumetricFogNoise;
extern cvar_t	*r_volumetricFogNoiseScale;
extern cvar_t	*r_volumetricFogNoiseContrast;
extern cvar_t	*r_volumetricFogNoiseDetailScale;
extern cvar_t	*r_volumetricFogNoiseDetailContrast;
extern cvar_t	*r_volumetricFogNoiseWind;

extern cvar_t	*r_allowExtensions;

extern cvar_t	*r_ext_compressed_textures;
extern cvar_t	*r_ext_multitexture;
extern cvar_t	*r_ext_compiled_vertex_array;
extern cvar_t	*r_ext_texture_env_add;
extern cvar_t	*r_ext_texture_filter_anisotropic;

extern cvar_t  *r_ext_draw_range_elements;
extern cvar_t  *r_ext_multi_draw_arrays;
extern cvar_t  *r_ext_texture_float;
extern cvar_t  *r_arb_half_float_pixel;
extern cvar_t  *r_ext_framebuffer_multisample;
extern cvar_t  *r_arb_seamless_cube_map;

extern cvar_t  *r_smaa;
extern cvar_t  *r_smaa_quality;

extern cvar_t  *r_cameraExposure;

extern cvar_t  *r_hdr;

extern cvar_t  *r_toneMap;
extern cvar_t  *r_forceToneMap;
extern cvar_t  *r_forceToneMapMin;
extern cvar_t  *r_forceToneMapAvg;
extern cvar_t  *r_forceToneMapMax;

extern cvar_t  *r_autoExposure;
extern cvar_t  *r_forceAutoExposure;
extern cvar_t  *r_forceAutoExposureMin;
extern cvar_t  *r_forceAutoExposureMax;

extern cvar_t  *r_toneMapMode;
extern cvar_t  *r_toneMapDebug;
extern cvar_t  *r_exposureCompensation;
extern cvar_t  *r_linearLighting;
extern cvar_t  *r_colorGrading;
extern cvar_t  *r_colorGradingCompare;
extern cvar_t  *r_colorGradingLUT;
extern cvar_t  *r_colorGradingIntensity;
extern cvar_t  *r_autoEmissive;

extern cvar_t  *r_depthPrepass;
extern cvar_t  *r_ssao;
extern cvar_t  *r_aoMode;
extern cvar_t  *r_aoApply;
extern cvar_t  *r_aoCompare;
extern cvar_t  *r_aoMultiBounce;
extern cvar_t  *r_aoLightmapFraction;
extern cvar_t  *r_aoSpecOcclusion;
extern cvar_t  *r_aoDebug;
extern cvar_t  *r_gtaoQuality;
extern cvar_t  *r_gtaoHalfRes;
extern cvar_t  *r_gtaoRadius;
extern cvar_t  *r_gtaoFalloff;
extern cvar_t  *r_gtaoThickness;
extern cvar_t  *r_gtaoPower;
extern cvar_t  *r_gtaoDenoise;
extern cvar_t  *r_gtaoBentNormals;
extern cvar_t  *r_contactShadows;
extern cvar_t  *r_contactShadowLength;
extern cvar_t  *r_contactShadowSteps;
extern cvar_t  *r_contactShadowThickness;
extern cvar_t  *r_contactShadowStrength;

extern cvar_t  *r_rainLens;
extern cvar_t  *r_rainLensQuality;
extern cvar_t  *r_rainLensDensity;
extern cvar_t  *r_rainLensRefraction;
extern cvar_t  *r_rainLensDropSize;
extern cvar_t  *r_rainLensFilm;
extern cvar_t  *r_rainLensBlur;
extern cvar_t  *r_rainLensReflection;
extern cvar_t  *r_rainLensInertia;
extern cvar_t  *r_rainLensDebug;
extern cvar_t  *r_rainLensFieldHeight;
extern cvar_t  *r_rainLensAgentLimit;
extern cvar_t  *r_rainLensPinning;
extern cvar_t  *r_rainLensMerge;
extern cvar_t  *r_rainLensFilmDecay;
extern cvar_t  *r_rainLensWetDecay;
extern cvar_t  *r_rainLensHeavyFlow;
extern cvar_t  *r_rainLensPeripheralBias;
extern cvar_t  *r_rainLensPBO;
extern cvar_t  *r_rainLensMipBlur;

extern cvar_t  *r_motionBlur;
extern cvar_t  *r_motionBlurShutterAngle;
extern cvar_t  *r_motionBlurReferenceFps;
extern cvar_t  *r_motionBlurShutterScale;
extern cvar_t  *r_motionBlurMaxPixels;
extern cvar_t  *r_motionBlurQuality;
extern cvar_t  *r_motionBlurSamples;
extern cvar_t  *r_motionBlurViewModelScale;
extern cvar_t  *r_motionBlurCutDistance;
extern cvar_t  *r_motionBlurCutAngle;
extern cvar_t  *r_motionBlurReset;
extern cvar_t  *r_motionBlurDebug;

extern cvar_t  *r_ssr;
extern cvar_t  *r_weatherWetness;
extern cvar_t  *r_weatherWetnessStrength;
extern cvar_t  *r_weatherWetnessRoughness;
extern cvar_t  *r_weatherWetnessDarkening;
extern cvar_t  *r_weatherWetnessNormal;
extern cvar_t  *r_weatherWetnessBias;
extern cvar_t  *r_weatherSurfaceDebug;
void R_WeatherMaterialList_f(void);
extern cvar_t  *r_weatherWetnessEntityFacing;
extern cvar_t  *r_weatherPuddles;
extern cvar_t  *r_weatherPuddleCoverage;
extern cvar_t  *r_weatherPuddleRoughness;
extern cvar_t  *r_weatherPuddleSlope;
extern cvar_t  *r_weatherPuddleScale;
extern cvar_t  *r_weatherPuddleUseHeightMap;
extern cvar_t  *r_weatherPuddleHeightSoftness;
extern cvar_t  *r_weatherPuddleWaterLevelBias;
extern cvar_t  *r_weatherPuddleRipples;
extern cvar_t  *r_weatherPuddleRippleStrength;
extern cvar_t  *r_weatherPuddleRippleScale;
extern cvar_t  *r_weatherPuddleRippleRate;
extern cvar_t  *r_weatherRunoff;
extern cvar_t  *r_weatherRunoffStrength;
extern cvar_t  *r_weatherRunoffSpeed;
extern cvar_t  *r_weatherRunoffScale;
extern cvar_t  *r_weatherRunoffProbe;
extern cvar_t  *r_weatherRunoffEntities;
extern cvar_t  *r_ssrQuality;
extern cvar_t  *r_ssrSteps;
extern cvar_t  *r_ssrRefineSteps;
extern cvar_t  *r_ssrMaxDistance;
extern cvar_t  *r_ssrThickness;
extern cvar_t  *r_ssrMaxRoughness;
extern cvar_t  *r_ssrEdgeFade;
extern cvar_t  *r_ssrHalfRes;
extern cvar_t  *r_ssrHiZ;
extern cvar_t  *r_ssrTemporal;
extern cvar_t  *r_ssrTemporalWeight;
extern cvar_t  *r_ssrBlendStrength;
extern cvar_t  *r_ssrCompare;
extern cvar_t  *r_ssrDebug;
extern cvar_t  *r_ssrEmitters;
extern cvar_t  *r_ssrEmitterIntensity;
extern cvar_t  *r_ssrEmitterMaxRoughness;
extern cvar_t  *r_ssrHitCache;
extern cvar_t  *r_ssrReceiverCull;

extern cvar_t  *r_ssgi;
extern cvar_t  *r_ssgiSource;
extern cvar_t  *r_ssgiIntensity;
extern cvar_t  *r_ssgiQuality;
extern cvar_t  *r_ssgiRays;
extern cvar_t  *r_ssgiSteps;
extern cvar_t  *r_ssgiMaxDistance;
extern cvar_t  *r_ssgiThickness;
extern cvar_t  *r_ssgiTemporal;
extern cvar_t  *r_ssgiHistoryWeight;
extern cvar_t  *r_ssgiDenoise;
extern cvar_t  *r_ssgiHalfRes;
extern cvar_t  *r_ssgiHiZ;
extern cvar_t  *r_ssgiEmissiveScale;
extern cvar_t  *r_ssgiGlowScale;
extern cvar_t  *r_ssgiCompare;
extern cvar_t  *r_ssgiDebug;
extern cvar_t  *r_ssgiFreezeHistory;

extern cvar_t  *r_skinSSS;
extern cvar_t  *r_skinSSSMixedHeads;
extern cvar_t  *r_skinSSSStrength;
extern cvar_t  *r_skinSSSWidth;
extern cvar_t  *r_skinSSSQuality;
extern cvar_t  *r_skinSSSWrap;
extern cvar_t  *r_skinSSSFollowSurface;
extern cvar_t  *r_skinSSSTransmission;
extern cvar_t  *r_skinSSSCompare;
extern cvar_t  *r_skinSSSDebug;

extern cvar_t  *r_autoPBR;
extern cvar_t  *r_autoPBRDebug;
extern cvar_t  *r_autoFoliage;
extern cvar_t  *r_autoFoliageDebug;
extern cvar_t  *r_grassCardMode;
extern cvar_t  *r_grassCardDebug;
extern cvar_t  *r_grassCardLodDist;
extern cvar_t  *r_grassCardWidth;
extern cvar_t  *r_foliageWind;
extern cvar_t  *r_foliageWindStrength;
extern cvar_t  *r_foliageWindSpeed;
extern cvar_t  *r_foliageWindDirection;
extern cvar_t  *r_foliageWindDebug;
extern cvar_t  *r_leafFlutter;
extern cvar_t  *r_leafFlutterStrength;
extern cvar_t  *r_leafFlutterSpeed;
extern cvar_t  *r_leafFlutterNormal;
extern cvar_t  *r_leafFlutterDebug;
extern cvar_t  *r_foliageInteraction;
extern cvar_t  *r_foliageInteractionStrength;
extern cvar_t  *r_foliageInteractionRadius;
extern cvar_t  *r_foliageInteractionMaxInteractors;
extern cvar_t  *r_foliageInteractionNPCs;
extern cvar_t  *r_foliageInteractionDebug;
extern cvar_t  *r_foliageBendField;
extern cvar_t  *r_foliageBendFieldSize;
extern cvar_t  *r_foliageBendFieldExtent;
extern cvar_t  *r_foliageBendFieldStrength;
extern cvar_t  *r_foliageBendFieldRecoveryTime;
extern cvar_t  *r_foliageBendFieldDamping;
extern cvar_t  *r_foliageBendFieldImpulse;
extern cvar_t  *r_foliageBendFieldDebug;
extern cvar_t  *r_plantWind;
extern cvar_t  *r_plantWindStrength;
extern cvar_t  *r_autoPBRConvert;
extern cvar_t  *r_autoPBRRoughness;
extern cvar_t  *r_diffuseBRDF;
extern cvar_t  *r_diffuseIBL;
extern cvar_t  *r_diffuseIBLStrength;
extern cvar_t  *r_diffuseIBLDebug;

extern cvar_t  *r_shaderProgramCache;
extern cvar_t  *r_shaderProgramCacheMaxMB;

extern cvar_t  *r_forwardPlus;
extern cvar_t  *r_forwardPlusTileSize;
extern cvar_t  *r_forwardPlusSlices;
extern cvar_t  *r_forwardPlusNearSlice;
extern cvar_t  *r_forwardPlusMaxLightsPerCluster;
extern cvar_t  *r_forwardPlusDebug;
extern cvar_t  *r_forwardPlusDebugLight;
extern cvar_t  *r_forwardPlusMaxShadowLights;

extern cvar_t  *r_ltcAreaLights;
extern cvar_t  *r_ltcDebug;
extern cvar_t  *r_ltcDebugLight;
extern cvar_t  *r_ltcIntensityScale;
extern cvar_t  *r_ltcStaticDiffuse;
extern cvar_t  *r_ltcMaxLights;
extern cvar_t  *r_ltcAutoAreaLights;
extern cvar_t  *r_ltcSaberAreaLights;

extern cvar_t  *r_normalMapping;
extern cvar_t  *r_specularMapping;
extern cvar_t  *r_deluxeMapping;
extern cvar_t  *r_deluxeSpecular;
extern cvar_t  *r_parallaxMapping;
extern cvar_t  *r_pomSelfShadow;
extern cvar_t  *r_pomSelfShadowLightMode;
extern cvar_t  *r_pomSelfShadowMaxLocalLights;
extern cvar_t  *r_pomSelfShadowSteps;
extern cvar_t  *r_pomSelfShadowStrength;
extern cvar_t  *r_pomSelfShadowBias;
extern cvar_t  *r_pomSelfShadowSoftness;
extern cvar_t  *r_pomAdaptiveSteps;
extern cvar_t  *r_pomMinSteps;
extern cvar_t  *r_pomMaxSteps;
extern cvar_t  *r_pomBinarySteps;
extern cvar_t  *r_pomFadeStart;
extern cvar_t  *r_pomFadeEnd;
extern cvar_t  *r_pomDebug;
extern cvar_t  *r_pomDebugFreezeLight;
extern cvar_t  *r_pomSilhouette;
extern cvar_t  *r_pomSilhouetteDistance;
extern cvar_t  *r_pomSilhouetteFade;
extern cvar_t  *r_pomSilhouetteSteps;
extern cvar_t  *r_pomSilhouetteMaxSteps;
extern cvar_t  *r_pomSilhouetteBinarySteps;
extern cvar_t  *r_pomSilhouetteViewDependence;
extern cvar_t  *r_pomSilhouetteShadows;
extern cvar_t  *r_pomSilhouetteContactShadows;
extern cvar_t  *r_pomSilhouetteDebug;
extern cvar_t  *r_autoPOMSilhouetteMode;
extern cvar_t  *r_normalAmbient;
extern cvar_t  *r_dlightMode;
extern cvar_t  *r_pshadowDist;
extern cvar_t  *r_imageUpsample;
extern cvar_t  *r_imageUpsampleMaxSize;
extern cvar_t  *r_imageUpsampleType;
extern cvar_t  *r_genNormalMaps;
extern cvar_t  *r_forceSun;
extern cvar_t  *r_forceSunMapLightScale;
extern cvar_t  *r_forceSunLightScale;
extern cvar_t  *r_forceSunAmbientScale;
extern cvar_t  *r_sunlightMode;
extern cvar_t  *r_drawSunRays;
extern cvar_t  *r_sunShadows;
extern cvar_t  *r_shadowFilter;
extern cvar_t  *r_shadowMapSize;
extern cvar_t  *r_shadowCascadeZNear;
extern cvar_t  *r_shadowCascadeZFar;
extern cvar_t  *r_shadowCascadeZBias;
extern cvar_t  *r_sunShadowMode;
extern cvar_t  *r_sunShadowAlphaCasters;
extern cvar_t  *r_shadowCascadeBlend;
extern cvar_t  *r_shadowDepthBias;
extern cvar_t  *r_shadowNormalBias;
extern cvar_t  *r_shadowSlopeBias;
extern cvar_t  *r_shadowReceiverBiasClamp;
extern cvar_t  *r_shadowPCSS;
extern cvar_t  *r_shadowPCSSQuality;
extern cvar_t  *r_shadowSunAngularDiameter;
extern cvar_t  *r_shadowPCSSMaxPenumbra;
extern cvar_t  *r_shadowDebug;
extern cvar_t  *r_shadowCasterLod;
extern cvar_t  *r_shadowCasterStats;
extern cvar_t  *r_dlightShadowBias;
extern cvar_t  *r_contactShadowSoft;
extern cvar_t  *r_ignoreDstAlpha;
extern cvar_t  *r_refractionChromaticAberration;

extern cvar_t	*r_ignoreGLErrors;
extern cvar_t	*r_logFile;

extern cvar_t	*r_stencilbits;
extern cvar_t	*r_depthbits;
extern cvar_t	*r_colorbits;
extern cvar_t	*r_texturebits;
extern cvar_t	*r_ext_multisample;

extern cvar_t	*r_drawBuffer;
extern cvar_t	*r_lightmap;
extern cvar_t	*r_vertexLight;
extern cvar_t	*r_uiFullScreen;
extern cvar_t	*r_shadows;
extern cvar_t	*r_flares;
extern cvar_t	*r_mode;
extern cvar_t	*r_nobind;
extern cvar_t	*r_singleShader;
extern cvar_t	*r_roundImagesDown;
extern cvar_t	*r_colorMipLevels;
extern cvar_t	*r_picmip;
extern cvar_t	*r_showtris;
extern cvar_t	*r_showsky;
extern cvar_t	*r_shownormals;
extern cvar_t	*r_finish;
extern cvar_t	*r_clear;
extern cvar_t	*r_swapInterval;
extern cvar_t	*r_markcount;
extern cvar_t	*r_textureMode;
extern cvar_t	*r_offsetFactor;
extern cvar_t	*r_offsetUnits;
extern cvar_t	*r_shadowOffsetFactor;
extern cvar_t	*r_shadowOffsetUnits;
extern cvar_t	*r_gamma;
extern cvar_t	*r_intensity;
extern cvar_t	*r_lockpvs;
extern cvar_t	*r_noportals;
extern cvar_t	*r_portalOnly;

extern cvar_t	*r_subdivisions;
extern cvar_t	*r_lodCurveError;

extern cvar_t	*r_fullscreen;
extern cvar_t  *r_noborder;

extern cvar_t	*r_customwidth;
extern cvar_t	*r_customheight;
extern cvar_t	*r_customPixelAspect;

extern cvar_t	*r_overBrightBits;
extern cvar_t	*r_mapOverBrightBits;

extern cvar_t	*r_debugSurface;
extern cvar_t	*r_simpleMipMaps;

extern cvar_t	*r_showImages;

extern cvar_t	*r_ambientScale;
extern cvar_t	*r_directedScale;
extern cvar_t	*r_debugLight;
extern cvar_t	*r_debugSort;
extern cvar_t	*r_printShaders;
extern cvar_t	*r_saveFontData;

extern cvar_t	*r_marksOnTriangleMeshes;

extern cvar_t	*r_aviMotionJpegQuality;
extern cvar_t	*r_screenshotJpegQuality;
extern cvar_t	*r_surfaceSprites;

extern cvar_t	*r_maxpolys;
extern int		max_polys;
extern cvar_t	*r_maxpolyverts;
extern int		max_polyverts;

extern	cvar_t	*r_aspectCorrectFonts;

/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
extern cvar_t	*r_noPrecacheGLA;
#endif

extern cvar_t	*r_noServerGhoul2;
extern cvar_t	*r_Ghoul2AnimSmooth;
extern cvar_t	*r_Ghoul2UnSqashAfterSmooth;
//extern cvar_t	*r_Ghoul2UnSqash;
//extern cvar_t	*r_Ghoul2TimeBase=0; from single player
//extern cvar_t	*r_Ghoul2NoLerp;
//extern cvar_t	*r_Ghoul2NoBlend;
//extern cvar_t	*r_Ghoul2BlendMultiplier=0;

extern cvar_t	*broadsword;
extern cvar_t	*broadsword_kickbones;
extern cvar_t	*broadsword_kickorigin;
extern cvar_t	*broadsword_playflop;
extern cvar_t	*broadsword_dontstopanim;
extern cvar_t	*broadsword_waitforshot;
extern cvar_t	*broadsword_smallbbox;
extern cvar_t	*broadsword_extra1;
extern cvar_t	*broadsword_extra2;

extern cvar_t	*broadsword_effcorr;
extern cvar_t	*broadsword_ragtobase;
extern cvar_t	*broadsword_dircap;

/*
Ghoul2 Insert End
*/

extern cvar_t	*r_patchStitching;

/*
End Cvars
*/

typedef enum
{
	AL_NONE,
	AL_SCENE,
	AL_VIEW,
	AL_STAGE
} annotationLayer_t;

void R_PushDebugGroup(annotationLayer_t layer, const char* name);

typedef enum
{
	IMGTYPE_COLORALPHA, // for color, lightmap, diffuse, and specular
	IMGTYPE_NORMAL,
	IMGTYPE_NORMALHEIGHT,
	IMGTYPE_DELUXE, // normals are swizzled, deluxe are not
} imgType_t;

typedef enum
{
	IMGFLAG_NONE           = 0x0000,
	IMGFLAG_MIPMAP         = 0x0001,
	IMGFLAG_PICMIP         = 0x0002,
	IMGFLAG_CUBEMAP        = 0x0004,
	IMGFLAG_NO_COMPRESSION = 0x0010,
	IMGFLAG_NOLIGHTSCALE   = 0x0020,
	IMGFLAG_CLAMPTOEDGE    = 0x0040,
	IMGFLAG_SRGB           = 0x0080,
	IMGFLAG_GENNORMALMAP   = 0x0100,
	IMGFLAG_MUTABLE        = 0x0200,
	IMGFLAG_HDR            = 0x0400,
	IMGFLAG_2D_ARRAY       = 0x0800,
	IMGFLAG_3D             = 0x1000,
	IMGLFAG_SHADOWCOMP     = 0x2000,
	IMGFLAG_TEXBUFFER      = 0x4000,	// buffer texture (GL_TEXTURE_BUFFER), tr_forwardplus.cpp
	IMGFLAG_NEAREST_3D     = 0x8000,	// manual light-grid interpolation
} imgFlags_t;

typedef enum
{
	ANIMMAP_NORMAL,
	ANIMMAP_CLAMP,
	ANIMMAP_ONESHOT
} animMapType_t;

enum
{
	ATTR_INDEX_POSITION,
	ATTR_INDEX_TEXCOORD0,
	ATTR_INDEX_TEXCOORD1,
	ATTR_INDEX_TEXCOORD2,
	ATTR_INDEX_TEXCOORD3,
	ATTR_INDEX_TEXCOORD4,
	ATTR_INDEX_TANGENT,
	ATTR_INDEX_NORMAL,
	ATTR_INDEX_COLOR,
	ATTR_INDEX_LIGHTDIRECTION,
	ATTR_INDEX_BONE_INDEXES,
	ATTR_INDEX_BONE_WEIGHTS,

	// GPU vertex animations and some extra sprite info
	ATTR_INDEX_POSITION2,
#ifdef REND2_SP_MD3
	ATTR_INDEX_TANGENT2,
	ATTR_INDEX_NORMAL2,
#endif // REND2_SP

	ATTR_INDEX_MAX
};

enum
{
	XFB_VAR_POSITION,
	XFB_VAR_VELOCITY,
	XFB_VAR_IMPACT,		// weather: rain impact (r_rainSplashes)

	XFB_VAR_COUNT
};
static const int NO_XFB_VARS = 0;

typedef struct image_s {
	char		imgName[MAX_QPATH];		// game path, including extension
	int			width, height, layers;				// source image
	int			uploadWidth, uploadHeight;	// after power of two and picmip but not including clamp to MAX_TEXTURE_SIZE
	GLuint		texnum;					// gl texture binding

	int			frameUsed;			// for texture usage in frame statistics

	int			internalFormat;
	int			TMU;				// only needed for voodoo2

	imgType_t   type;
	int			flags;

	struct image_s *next;
	struct image_s *poolNext;

	// color of the bright part of the picture (luminance weighted average,
	// linear when sampled as sRGB), for SSR emitter reflections (tr_ssr.cpp)
	vec4_t		emissiveColor;

	// normalHeightMap: 2nd / 98th percentile of the flipped height (depth,
	// 0 = top), the relief the height aware puddles use (tr_weather.cpp);
	// 0 0 for every other image
	float		heightRange[2];

	// r_autoPBRRoughness map (<diffuse>_aORMS): mean and std-dev of the
	// roughness multiplier; 0 0 for every other image
	float		autoRoughness[2];
} image_t;

typedef struct cubemap_s {
	char name[MAX_QPATH];
	vec3_t origin;
	float parallaxRadius;
	image_t *image;
	image_t *diffuseIrradianceImage;
} cubemap_t;

typedef struct dlight_s {
	vec3_t	origin;
	vec3_t	color;				// range from 0.0 to 1.0, should be color normalized
	float	radius;

	vec3_t	transformed;		// origin in local coordinate system
	int		additive;			// texture detail is lost tho when the lightmap is dark

	// LTC area light (tr_arealights.cpp), Forward+ only. Point lights: type 0
	// and the fields below unused. Area lights: origin = centre, radius =
	// influence / cull radius (not an attenuation radius), color = emitted
	// radiance (not normalised), emitting side = cross(right, up)
	int		areaType;			// DLIGHT_POINT, DLIGHT_RECT, DLIGHT_LINE
	int		areaFlags;			// AREALIGHT_*
	int		areaId;				// map light index, -1 = scene (dynamic) light
	vec3_t	areaRight;			// unit
	vec3_t	areaUp;				// unit (LINE: unused, rebuilt per pixel)
	float	halfWidth;			// along right (LINE: half length)
	float	halfHeight;			// along up (LINE: tube radius)

	// spot lights (tr_spotlight.cpp): areaType stays DLIGHT_POINT (same
	// radial falloff, list, culling and shadow slot as a point light), the
	// cone multiplies it. Point lights: spot = qfalse, cosines -2 / -1 (the
	// cone factor is 1)
	qboolean spot;
	vec3_t	spotDir;			// unit, away from the light
	float	spotCosInner;		// full intensity at and above
	float	spotCosOuter;		// zero at and below
	qboolean spotNoShadow;
	// cookie (tr_lightcookie.cpp): layer of tr.lightCookieArray, -1 = none
	// (factor 1); roll of the cookie around the axis, radians, from the
	// stable basis of R_SpotShadowAxis
	int		cookieLayer;
	float	cookieRoll;
	// written by R_GatherFrameViews for this scene: shadow cube index whose
	// layer 0 holds the one perspective view (-1 = none: cube faces or
	// unshadowed) and its world to clip matrix
	int		spotShadowSlot;
	matrix_t spotShadowVP;
} dlight_t;

enum
{
	DLIGHT_POINT,
	DLIGHT_RECT,
	DLIGHT_LINE,
	// reserved: DLIGHT_DISK, DLIGHT_SPOT
};

// area light flags, packed in the Forward+ light data (lightall.glsl)
#define AREALIGHT_TWO_SIDED		1
#define AREALIGHT_SPECULAR_ONLY	2	// static stock lamps: the lightmap has the diffuse
#define AREALIGHT_DYNAMIC		4	// scene light (saber, API), else map file light
#define AREALIGHT_SELECTED		8	// r_ltcDebug highlight

// a trRefEntity_t has all the information passed in by
// the client game, as well as some locally derived info
typedef struct trRefEntity_s {
	refEntity_t	e;

	float		axisLength;		// compensate for non-normalized axis

	qboolean	needDlights;	// true for bmodels that touch a dlight
	qboolean	lightingCalculated;
	qboolean	mirrored;		// mirrored matrix, needs reversed culling
	vec3_t		lightDir;		// normalized direction towards light, in world space
	vec3_t      modelLightDir;  // normalized direction towards light, in model space
	vec3_t		ambientLight;	// color normalized to 0-255
	int			ambientLightInt;	// 32 bit rgba packed
	vec3_t		directedLight;
} trRefEntity_t;


typedef struct {
	vec3_t		origin;			// in world coordinates
	vec3_t		axis[3];		// orientation in world
	vec3_t		viewOrigin;		// viewParms->or.origin in local coordinates
	float		modelViewMatrix[16];
	float		modelMatrix[16];
} orientationr_t;

void R_SetOrientationOriginAndAxis(
	orientationr_t& orientation,
	const vec3_t origin,
	const vec3_t forward,
	const vec3_t left,
	const vec3_t up);
void R_SetOrientationOriginAndAxis(
	orientationr_t& orientation,
	const vec3_t origin,
	const matrix3_t axis);

typedef enum
{
	VBO_USAGE_STATIC,
	VBO_USAGE_DYNAMIC,
	VBO_USAGE_XFB
} vboUsage_t;

typedef struct VBO_s
{
	uint32_t        vertexesVBO;
	size_t          vertexesSize;	// amount of memory data allocated for all vertices in bytes

	uint32_t		offsets[ATTR_INDEX_MAX];
	uint32_t		strides[ATTR_INDEX_MAX];
	uint32_t		sizes[ATTR_INDEX_MAX];
	uint32_t		stepRates[ATTR_INDEX_MAX];
} VBO_t;

typedef struct IBO_s
{
	uint32_t        indexesVBO;
	size_t          indexesSize;	// amount of memory data allocated for all triangles in bytes
//  uint32_t        ofsIndexes;
} IBO_t;

//===============================================================================

typedef enum {
	SS_BAD,
	SS_PORTAL,			// mirrors, portals, viewscreens
	SS_ENVIRONMENT,		// sky box
	SS_OPAQUE,			// opaque

	SS_DECAL,			// scorch marks, etc.
	SS_SEE_THROUGH,		// ladders, grates, grills that may have small blended edges
						// in addition to alpha test
	SS_BANNER,

	SS_INSIDE,			// inside body parts (i.e. heart)
	SS_MID_INSIDE,
	SS_MIDDLE,
	SS_MID_OUTSIDE,
	SS_OUTSIDE,			// outside body parts (i.e. ribs)

	SS_FOG,

	SS_UNDERWATER,		// for items that should be drawn in front of the water plane

	SS_BLEND0,			// regular transparency and filters
	SS_BLEND1,			// generally only used for additive type effects
	SS_BLEND2,
	SS_BLEND3,

	SS_BLEND6,
	SS_STENCIL_SHADOW,
	SS_ALMOST_NEAREST,	// gun smoke puffs

	SS_NEAREST			// blood blobs
} shaderSort_t;


#define MAX_SHADER_STAGES 8

typedef enum {
	GF_NONE,

	GF_SIN,
	GF_SQUARE,
	GF_TRIANGLE,
	GF_SAWTOOTH,
	GF_INVERSE_SAWTOOTH,

	GF_NOISE,
	GF_RAND

} genFunc_t;


typedef enum {
	DEFORM_NONE,
	DEFORM_WAVE,
	DEFORM_NORMALS,
	DEFORM_BULGE,
	DEFORM_BULGE_UNIFORM,
	DEFORM_MOVE,
	DEFORM_PROJECTION_SHADOW,
	DEFORM_AUTOSPRITE,
	DEFORM_AUTOSPRITE2,
	DEFORM_TEXT0,
	DEFORM_TEXT1,
	DEFORM_TEXT2,
	DEFORM_TEXT3,
	DEFORM_TEXT4,
	DEFORM_TEXT5,
	DEFORM_TEXT6,
	DEFORM_TEXT7,
	DEFORM_DISINTEGRATION
} deform_t;

// deformVertexes types that can be handled by the GPU
typedef enum
{
	// do not edit: same as genFunc_t

	DGEN_NONE,
	DGEN_WAVE_SIN,
	DGEN_WAVE_SQUARE,
	DGEN_WAVE_TRIANGLE,
	DGEN_WAVE_SAWTOOTH,
	DGEN_WAVE_INVERSE_SAWTOOTH,
	DGEN_WAVE_NOISE,

	// do not edit until this line

	DGEN_BULGE,
	DGEN_NORMALS,
	DGEN_MOVE,
} deformGen_t;

typedef enum {
	AGEN_IDENTITY,
	AGEN_SKIP,
	AGEN_ENTITY,
	AGEN_ONE_MINUS_ENTITY,
	AGEN_VERTEX,
	AGEN_ONE_MINUS_VERTEX,
	AGEN_LIGHTING_SPECULAR,
	AGEN_WAVEFORM,
	AGEN_PORTAL,
	AGEN_CONST,
	AGEN_LIGHTING_SPECULAR_STATIC
} alphaGen_t;

typedef enum {
	CGEN_BAD,
	CGEN_IDENTITY_LIGHTING,	// tr.identityLight
	CGEN_IDENTITY,			// always (1,1,1,1)
	CGEN_ENTITY,			// grabbed from entity's modulate field
	CGEN_ONE_MINUS_ENTITY,	// grabbed from 1 - entity.modulate
	CGEN_EXACT_VERTEX,		// tess.vertexColors
	CGEN_VERTEX,			// tess.vertexColors * tr.identityLight
	CGEN_EXACT_VERTEX_LIT,	// like CGEN_EXACT_VERTEX but takes a light direction from the lightgrid
	CGEN_VERTEX_LIT,		// like CGEN_VERTEX but takes a light direction from the lightgrid
	CGEN_ONE_MINUS_VERTEX,
	CGEN_WAVEFORM,			// programmatically generated
	CGEN_LIGHTING_DIFFUSE,
	CGEN_LIGHTING_DIFFUSE_ENTITY, // diffuse lighting * entity
	CGEN_FOG,				// standard fog
	CGEN_CONST,				// fixed color
	CGEN_LIGHTMAPSTYLE,		// lightmap style
	CGEN_DISINTEGRATION_1,
	CGEN_DISINTEGRATION_2
} colorGen_t;

typedef enum {
	TCGEN_BAD,
	TCGEN_IDENTITY,			// clear to 0,0
	TCGEN_LIGHTMAP,
	TCGEN_LIGHTMAP1,
	TCGEN_LIGHTMAP2,
	TCGEN_LIGHTMAP3,
	TCGEN_TEXTURE,
	TCGEN_ENVIRONMENT_MAPPED,
	TCGEN_ENVIRONMENT_MAPPED_SP,
	TCGEN_ENVIRONMENT_MAPPED_SP_FP,
	TCGEN_FOG,
	TCGEN_VECTOR			// S and T from world coordinates
} texCoordGen_t;

typedef enum {
	ACFF_NONE,
	ACFF_MODULATE_RGB,
	ACFF_MODULATE_RGBA,
	ACFF_MODULATE_ALPHA
} acff_t;

typedef struct {
	genFunc_t	func;

	float base;
	float amplitude;
	float phase;
	float frequency;
} waveForm_t;

#define TR_MAX_TEXMODS 4

typedef enum {
	TMOD_NONE,
	TMOD_TRANSFORM,
	TMOD_TURBULENT,
	TMOD_SCROLL,
	TMOD_SCALE,
	TMOD_STRETCH,
	TMOD_ROTATE,
	TMOD_ENTITY_TRANSLATE
} texMod_t;

#define	MAX_SHADER_DEFORMS	3
typedef struct {
	deform_t	deformation;			// vertex coordinate modification type

	vec3_t		moveVector;
	waveForm_t	deformationWave;
	float		deformationSpread;

	float		bulgeWidth;
	float		bulgeHeight;
	float		bulgeSpeed;
} deformStage_t;


typedef struct {
	texMod_t		type;

	// used for TMOD_TURBULENT and TMOD_STRETCH
	waveForm_t		wave;

	// used for TMOD_TRANSFORM
	float			matrix[2][2];		// s' = s * m[0][0] + t * m[1][0] + trans[0]
	float			translate[2];		// t' = s * m[0][1] + t * m[0][1] + trans[1]

	// used for TMOD_SCALE
	float			scale[2];			// s *= scale[0]
	                                    // t *= scale[1]

	// used for TMOD_SCROLL
	float			scroll[2];			// s' = s + scroll[0] * time
										// t' = t + scroll[1] * time

	// + = clockwise
	// - = counterclockwise
	float			rotateSpeed;

} texModInfo_t;

enum surfaceSpriteType_t
{
	SURFSPRITE_NONE,
	SURFSPRITE_VERTICAL,
	SURFSPRITE_ORIENTED,
	SURFSPRITE_EFFECT,
	SURFSPRITE_WEATHERFX,
	SURFSPRITE_FLATTENED,
};

enum surfaceSpriteOrientation_t
{
	SURFSPRITE_FACING_NORMAL,
	SURFSPRITE_FACING_UP,
	SURFSPRITE_FACING_DOWN,
	SURFSPRITE_FACING_ANY,
};

struct SurfaceSpriteBlock
{
	vec2_t fxGrow;
	float fxDuration;
	float fadeStartDistance;
	float fadeEndDistance;
	float fadeScale;
	float wind;
	float windIdle;
	float fxAlphaStart;
	float fxAlphaEnd;
	float pad0[2];
};

struct CameraBlock
{
	matrix_t viewProjectionMatrix;
	vec4_t viewInfo;
	vec3_t viewOrigin;
	float pad0;
	vec3_t viewForward;
	float pad1;
	vec3_t viewLeft;
	float pad2;
	vec3_t viewUp;
	float pad3;
	// Forward+ cluster grid of this view (tr_forwardplus.cpp), lightall only
	int fplusGrid[4];		// grid texel base, light texel base, tiles x, tiles y
	vec4_t fplusParams;		// tile size (pixels), depth slices, slice scale, slice bias
	vec4_t fplusParams2;	// viewport x, viewport y, enabled, near slice distance
	vec4_t fplusDebug;		// r_forwardPlusDebug, selected light, max lights per cluster, r_ltcDebug
};

struct SceneBlock
{
	vec4_t primaryLightOrigin;
	vec3_t primaryLightAmbient;
	int	   globalFogIndex;
	vec3_t primaryLightColor;
	float primaryLightRadius;
	float currentTime;
	float frameTime;
	float pad0[2];
	// screen-space AO application, see RB_AOSceneParams (tr_ao.cpp)
	vec4_t aoParams;	// application mode, lightmap fraction, multi-bounce, split x
	vec4_t aoParams2;	// debug view, unused, unused, unused
	// screen-space GI source, see RB_SSGISceneParams (tr_ssgi.cpp); only
	// declared by lightall with USE_SSGI
	vec4_t ssgiParams;	// source bits, linear scene, emissive scale, glow scale
};

struct LightsBlock
{
	struct Light
	{
		vec4_t origin;
		vec3_t color;
		float radius;
		vec4_t spot;		// cone axis, cos outer angle (-2: point light)
		vec4_t spot2;		// cos inner angle (-1: point light), projected spot shadow (1/0), cookie layer (-1 none), cookie roll
	};

	matrix_t shadowVP1;
	matrix_t shadowVP2;
	matrix_t shadowVP3;
	vec4_t shadowSplits;			// three far distances, final fade start
	vec4_t shadowBlend;			// two half widths, final far distance, unused
	vec4_t shadowTexelSize;		// world units per texel for each cascade, inverse map size
	vec4_t shadowDepthSpan;		// light-space depth span for each cascade, unused
	vec4_t shadowBias;			// constant world bias, normal texels, receiver-plane scale, clamp
	vec4_t shadowPcss;			// tan angular radius, max world penumbra, enabled, quality
	vec4_t shadowDebug;			// debug mode, dlight shadow bias mode, unused

	int numLights;
	float pad0[3];

	Light lights[MAX_DLIGHTS];
	// spot light shadows (tr_spotlight.cpp): world to clip of the one
	// perspective view in layer 6 * slot of pointShadowArrayImage
	matrix_t spotShadowVP[MAX_DLIGHT_SHADOWS];
};

struct FogsBlock
{
	struct Fog
	{
		vec4_t plane;
		vec4_t color;
		float depthToOpaque;
		int hasPlane;
		float pad1[2];
	};

	int numFogs;
	float pad0[3];
	Fog fogs[MAX_GPU_FOGS];
};

struct EntityBlock
{
	matrix_t modelMatrix;
	vec4_t lightOrigin;
	vec3_t ambientLight;
	float entityTime;
	vec3_t directedLight;
	float fxVolumetricBase;
	vec3_t modelLightDir;
	float vertexLerp;
	// Multi-point entity lighting. Kept at the end so older shader blocks retain
	// their existing std140 offsets.
	vec4_t gridAmbient[3];
	vec4_t gridDirected[3];
	vec4_t gridDirection[3];
	vec4_t gridParams;       // lower Z, inverse height, mode, forced linear
	vec4_t gridMinimum;      // additive RGB, LDR ambient clamp
	vec4_t gridScale;        // ambient scale, directed scale, HDR grid, debug
	bool operator == (const EntityBlock &other) const
	{
		return (
			Matrix16Compare(this->modelMatrix, other.modelMatrix) == qtrue &&
			VectorCompare4(this->lightOrigin, other.lightOrigin) == qtrue &&
			VectorCompare(this->ambientLight, other.ambientLight) == qtrue &&
			this->entityTime == other.entityTime &&
			VectorCompare(this->directedLight, other.directedLight) == qtrue &&
			this->fxVolumetricBase == other.fxVolumetricBase &&
			VectorCompare(this->modelLightDir, other.modelLightDir) == qtrue &&
			this->vertexLerp == other.vertexLerp &&
			memcmp(this->gridAmbient, other.gridAmbient, sizeof(gridAmbient)) == 0 &&
			memcmp(this->gridDirected, other.gridDirected, sizeof(gridDirected)) == 0 &&
			memcmp(this->gridDirection, other.gridDirection, sizeof(gridDirection)) == 0 &&
			VectorCompare4(this->gridParams, other.gridParams) == qtrue &&
			VectorCompare4(this->gridMinimum, other.gridMinimum) == qtrue &&
			VectorCompare4(this->gridScale, other.gridScale) == qtrue
			);
	}
};

struct ShaderInstanceBlock
{
	vec4_t deformParams0;
	vec4_t deformParams1;
	float time;
	float portalRange;
	int deformType;
	int deformFunc;
};

struct SkeletonBoneMatricesBlock
{
	mat3x4_t matrices[MAX_G2_BONES];
};

struct TemporalBlock
{
	matrix_t previousViewProjectionMatrix;
	vec2_t currentJitter;
	vec2_t previousJitter;
	float previousTime;
	float pad0[3];
};

// Foliage character colliders (r_foliageInteraction, tr_foliageinteract.cpp).
// Same layout as the FoliageInteraction block of glsl/foliage_interact.glsl
// (std140). Per collider: (axis x, y, feet z, head z), (radius, velocity x,
// velocity y, unused).
struct FoliageInteractionBlock
{
	vec4_t params;		// current count, previous count, strength, 1 = no wind (debug)
	vec4_t current[MAX_FOLIAGE_INTERACTORS * 2];
	vec4_t previous[MAX_FOLIAGE_INTERACTORS * 2];
	// persistent bend field (r_foliageBendField, tr_foliagefield.cpp)
	vec4_t field;			// center x, y (world), 1 / extent, scale (0 = no field)
	vec4_t fieldPrevious;	// the same for the previous frame's field
	vec4_t fieldUpdate;		// update pass: time step, spring k, damping c, impulse
	vec4_t fieldShift;		// update pass: texel shift x, y, clear; draws: w 1 = no direct term
};

// Froxel volumetric fog (r_volumetricFog 2, tr_volumetric.cpp). Same layout
// as the VolumetricFog block of glsl/volumetric_common.glsl (std140).
struct VolumetricFogBlock
{
	matrix_t viewProjection;		// froxel camera: main view without jitter
	matrix_t invViewProjection;		// rendered (jittered) main view, depth reconstruction
	matrix_t prevViewProjection;	// froxel camera of the history volume
	vec4_t viewOrigin;				// xyz, w: 1 = volume built this frame
	vec4_t viewForward;				// xyz unit forward
	vec4_t rayForward;				// froxel ray = rayForward + ndc.x * rayRight + ndc.y * rayUp
	vec4_t rayRight;
	vec4_t rayUp;
	vec4_t viewport;				// view rectangle in render target texture coordinates
	vec4_t sliceParams;				// near, far, log2(far / near), sky distance
	vec4_t gridSize;				// froxels x, y, z, frame index
	vec4_t jitter;					// jitter in froxel units, w: temporal accumulation
	vec4_t temporalParams;			// history weight, history valid, emission scale (r_volumetricEmission), radiance clamp ratio
	vec4_t lightParams;				// anisotropy g, sun scale, dlight scale, static scale
	vec4_t sunColor;				// realtime sun radiance, w: cascaded shadow maps available
	vec4_t sunDirection;			// towards the sun, w: split light grid available
	vec4_t gridOrigin;				// light grid sample origin, w: vertical cell size
	vec4_t gridScale;				// world to light grid texture coordinates, w: horizontal cell size
	vec4_t shadowParams;			// cascade far distance, shadow map size, dlight shadows, bias
	vec4_t debugParams;				// debug view, bloom, frozen, directional baked light
	vec4_t heightFog;				// height fog: base extinction per unit (0 = off), base z, 1 / falloff, log(max scale)
	vec4_t heightFogColor;			// rgb albedo, w: fade out start above the base (top - fade)
	vec4_t heightFogTop;			// x: top above the base (0 = no cutoff), yzw: extinction color (r_volumetricFogRGBExtinction, 1 1 1)
	vec4_t noiseParams;				// density noise: 1 / macro period, 1 / detail period, macro contrast, detail contrast
	vec4_t noiseMacroOffset;		// wind offset of the macro noise (tile units), w: 1 = height fog is noisy
	vec4_t noiseDetailOffset;		// wind offset of the detail noise (tile units), w: history weight of noisy media
	vec4_t noiseLod;				// lod offsets log2(64 / period) - 1 (macro, detail), slice thickness / depth, w: 1 = noise on
	vec4_t noiseNormMacro[4];		// mean normalization of the macro noise at lod 0, 0.5, ..., 7.5
	vec4_t noiseNormDetail[4];		// same, detail noise
	vec4_t selfShadow;				// media self-shadow: mode (0 off, 1 sun, 2 + lights), sun samples, distance, 1 = analytic height fog beyond
	vec4_t selfShadowLights;		// indices of the self-shadowed dynamic lights in the light buffer, -1 = none
	vec4_t multiScatter;			// multiple scattering octaves: mode (0 off, 1 sun, 2 + self-shadowed lights), octaves, contribution b, medium length l
	vec4_t multiScatter2;			// attenuation a, phase c, shadow fill, unused
	int numFogs;
	int lightTileSize;				// dynamic light lists: froxels per tile side (0 = no lights)
	int lightTilesX;				// tiles per slice
	int lightTilesY;
	vec4_t fogColor[MAX_GPU_FOGS];	// rgb albedo (fog color), a: extinction per unit
	vec4_t fogPlane[MAX_GPU_FOGS];	// as the Fogs block
	vec4_t fogMins[MAX_GPU_FOGS];	// w: has plane
	vec4_t fogMaxs[MAX_GPU_FOGS];	// w: 1 = density noise applies to this fog
	vec4_t fogMedium[MAX_GPU_FOGS];	// x: anisotropy g (fogAnisotropy or r_volumetricFogAnisotropy), yzw: extinction color (r_volumetricFogRGBExtinction, 1 1 1)

	// local fog volumes (tr_fogvolume.cpp), nearest first
	vec4_t localParams;							// count, fade start, 1 / fade length, 1 = some volume emits
	vec4_t localX[MAX_GPU_FOG_VOLUMES];			// world to unit local space rows (xyz, w offset)
	vec4_t localY[MAX_GPU_FOG_VOLUMES];
	vec4_t localZ[MAX_GPU_FOG_VOLUMES];
	vec4_t localPrevX[MAX_GPU_FOG_VOLUMES];		// the rows of the previous frame
	vec4_t localPrevY[MAX_GPU_FOG_VOLUMES];
	vec4_t localPrevZ[MAX_GPU_FOG_VOLUMES];
	vec4_t localColor[MAX_GPU_FOG_VOLUMES];		// rgb albedo, a: extinction per unit (0: gone this frame)
	vec4_t localShape[MAX_GPU_FOG_VOLUMES];		// shape (0 ellipsoid, 1 box), inner, 1 / (1 - inner), noisy + 2 * extinction palette index + 32 * history break
	vec4_t localMotion[MAX_GPU_FOG_VOLUMES];	// changed: 0 no, else 1 + previous shape; previous extinction, inner, 1 / (1 - inner)
	vec4_t localEmission[MAX_GPU_FOG_VOLUMES];	// rgb emission per unit at full density (emissive * density), w: anisotropy g
	vec4_t localClusters;				// tile size, tiles X, tiles Y, unused

	// BSP fog volumes that may touch each slice (bit i = fog i, MAX_GPU_FOGS <= 32)
	int fogSlices[FROXEL_MAX_SLICES];			// ivec4[32]
	// BSP fog volumes that may lie beyond far (the analytic tail): x = mask (bit i = fog i),
	// y = loop bound (highest set bit + 1), zw unused
	int tailFogs[4];							// ivec4

	// extinction colors of the local fog volumes (r_volumetricFogRGBExtinction), indexed
	// by localShape.w: entry 0 = neutral (1 1 1)
	vec4_t extinctionPalette[FROXEL_EXTINCTION_PALETTE];
};

// below the 16 384 of GL_MAX_UNIFORM_BLOCK_SIZE guaranteed by GL 3.2 (13 760 with
// the extinction palette and the tail fog mask)
static_assert(sizeof(VolumetricFogBlock) <= 16384, "VolumetricFog block above the GL 3.2 minimum UBO size");

// Volumetric FX particles of the froxel injection (tr_volparticle.cpp). Same
// layout as the VolumetricParticles block of glsl/volumetric_common.glsl
// (std140), only in the injection and debug programs.
struct VolumetricParticlesBlock
{
	vec4_t params;										// count, fade start, 1 / fade length, history floor
	vec4_t center[MAX_GPU_VOL_PARTICLES];				// xyz, w: extinction per unit (0: gone this frame)
	vec4_t invExtent[MAX_GPU_VOL_PARTICLES];			// 1 / (radius * aspect) per world axis, w: inner | g (R_VolParticlePackInner)
	vec4_t color[MAX_GPU_VOL_PARTICLES];				// rgb albedo, w: previous extinction
	vec4_t prevCenter[MAX_GPU_VOL_PARTICLES];			// previous frame, w: changed (0/1) + 2 * (emission slot + 1)
	vec4_t prevInvExtent[MAX_GPU_VOL_PARTICLES];		// previous frame, w: inner | g (R_VolParticlePackInner)
	int slices[FROXEL_MAX_SLICES];						// per slice: first pool entry | count << 16 (ivec4[32])
	int index[VOL_PARTICLE_POOL / 2];					// 16 bit particle indices, 2 per int (ivec4[320])
	vec4_t emission[MAX_GPU_EMISSIVE_PARTICLES];		// rgb emission per unit at the center, w unused
};

// 16 272 bytes: below the 16 384 of GL_MAX_UNIFORM_BLOCK_SIZE guaranteed by GL 3.2
static_assert(sizeof(VolumetricParticlesBlock) <= 16384, "VolumetricParticles block above the GL 3.2 minimum UBO size");

struct surfaceSprite_t
{
	surfaceSpriteType_t type;
	uint8_t foliageClass; // explicit vegetation sprite stage

	float width;
	float height;
	float density;
	float wind;
	float windIdle;
	float fadeDist;
	float fadeMax;
	float fadeScale;
	float fxAlphaStart;
	float fxAlphaEnd;
	float fxDuration;
	float vertSkew;

	vec2_t variance;
	vec2_t fxGrow;
	surfaceSpriteOrientation_t facing;

	size_t spriteUboOffset;
};

#define	MAX_IMAGE_ANIMATIONS	(32)

typedef struct {
	image_t			*image[MAX_IMAGE_ANIMATIONS];
	int				numImageAnimations;
	float			imageAnimationSpeed;

	texCoordGen_t	tcGen;
	vec3_t			tcGenVectors[2];

	int				numTexMods;
	texModInfo_t	*texMods;

	int				videoMapHandle;
	qboolean		isLightmap;
	qboolean		oneShotAnimMap;
	qboolean		isVideoMap;
} textureBundle_t;

enum
{
	TB_COLORMAP    = 0,
	TB_DIFFUSEMAP  = 0,
	TB_LIGHTMAP    = 1,
	TB_LEVELSMAP   = 1,
	TB_COLORMAP2   = 1,
	TB_NORMALMAP   = 2,
	TB_DELUXEMAP   = 3,
	TB_COLORGRADINGLUT = 3, // tone map and refraction programs only
	TB_SPECULARMAP = 4,
	TB_ORMSMAP     = 4,
	TB_SHADOWMAP   = 5,
	TB_CUBEMAP     = 6,
	TB_ENVBRDFMAP  = 7,
	TB_SHADOWMAPARRAY  = 8,
	TB_SSAOMAP     = 9,
	TB_EMISSIVEMAP = 10,
	NUM_TEXTURE_BUNDLES = 11,

	// Forward+ buffer textures of lightall (not shader stage bundles)
	TB_FPLUS_LIGHTS  = 11,
	TB_FPLUS_GRID    = 12,
	TB_FPLUS_INDICES = 13,
	TB_DIFFUSEIRRADIANCEMAP = 14,
	TB_PROBEAVERAGEMAP = 15,
	TB_ENTITYGRID_AMBIENT = 16,
	TB_ENTITYGRID_DIRECTED = 17,
	TB_ENTITYGRID_DIRECTION = 18,

	// screen-space GI inputs of the ssgi_*.glsl programs (tr_ssgi.cpp)
	TB_SSGI_ALBEDO   = 14,
	TB_SSGI_RADIANCE = 15,
	TB_SSGI_SOURCE   = 16,

	// SSR ray hits of this and the previous frame (hit cache, tr_ssr.cpp)
	TB_SSR_HIT       = 10,
	TB_SSR_PREVHIT   = 11,

	// silhouette POM group footprints of lightall / fogpass / pom_silhouette_depth
	// (tr_pom_silhouette.cpp). The silhouette lightall set is lightmap / vertex
	// lit only, the entity grid units (LIGHT_VECTOR) are free there. Needs
	// GL_MAX_TEXTURE_IMAGE_UNITS > 16, else r_pomSilhouette stays off.
	TB_POM_GROUPS    = 16,

	// static rain occlusion depth map (tr.weatherDepthImage) of lightall,
	// r_weatherWetness (tr_weather.cpp). Needs GL_MAX_TEXTURE_IMAGE_UNITS > 19.
	TB_WEATHERDEPTH  = 19,

	// LTC area light lookup tables of lightall (tr_arealights.cpp, USE_LTC).
	// Needs GL_MAX_TEXTURE_IMAGE_UNITS > 21, else r_ltcAreaLights stays off.
	TB_LTC_MATRIX    = 20,
	TB_LTC_AMPLITUDE = 21,

	// per stage skin scatter mask of lightall (tr_skinsss.cpp, skinMask keyword).
	// Needs GL_MAX_TEXTURE_IMAGE_UNITS > 22, else masks are ignored.
	TB_SKINMASK      = 22,

	// persistent foliage bend field (tr_foliagefield.cpp), sampled by the
	// vertex shaders of the grass and plant draws: this frame's state and the
	// previous frame's (motion vectors). Bound once per frame, no other
	// program uses these units.
	TB_FOLIAGEFIELD      = 23,
	TB_FOLIAGEFIELD_PREV = 24,

	// spot light cookies (tr_lightcookie.cpp): one 2D array, a layer per
	// cookie, of lightall and volumetric_inject. Needs
	// GL_MAX_TEXTURE_IMAGE_UNITS > 25, else r_spotLightCookies stays off.
	TB_LIGHTCOOKIES      = 25,

	// froxel fog extinction of this frame (r_volumetricSelfShadow), in
	// volumetric_inject / volumetric_debug, which have no Forward+ index buffer
	TB_FROXELMEDIA       = 13,

	// RGB extinction (r_volumetricFogRGBExtinction): the integrated transmittance of the
	// froxel lookup (every program with USE_FROXEL_FOG), the injected extinction
	// (inject history, integrate source, debug) and the integration carry.
	// Needs GL_MAX_TEXTURE_IMAGE_UNITS > 28, else the scalar path is used.
	TB_FROXELTRANSMITTANCE = 26,
	TB_FROXELEXTINCTION    = 27,
	TB_FROXELCARRYT        = 28,
	MAX_TEXTURE_UNITS = 32	// glstate_t bookkeeping, GL_SelectTexture limit
};

// linear depth mip levels of the GTAO depth chain (tr_ao.cpp)
#define AO_DEPTH_MIPS 4

// screen-space reflections (tr_ssr.cpp): mip levels of the opaque scene color
// pyramid (roughness blur)
#define SSR_COLOR_MIPS 7

// shared screen-space infrastructure (tr_screenspace.cpp): mip levels of the
// closest depth pyramid (Hi-Z tracing of SSR and SSGI) and the attachments of
// renderFbo written by the opaque lightall stages. Absent ones are GL_NONE
// gaps: the fragment outputs have fixed locations.
#define SCREEN_HIZ_MIPS 7
enum
{
	SCREEN_ATTACHMENT_NORMAL		= 2,	// shared: octahedral normal, roughness, SSR receiver
	SCREEN_ATTACHMENT_SSR_SPECULAR	= 3,
	SCREEN_ATTACHMENT_SSR_CUBEMAP	= 4,
	SCREEN_ATTACHMENT_SSGI_ALBEDO	= 5,
	SCREEN_ATTACHMENT_SSGI_RADIANCE	= 6,
	SCREEN_ATTACHMENT_SKIN			= 7,	// skin SSS: scattering diffuse, view depth
	SCREEN_ATTACHMENT_FIRST			= SCREEN_ATTACHMENT_NORMAL,
	SCREEN_ATTACHMENT_LAST			= SCREEN_ATTACHMENT_SKIN,
};

// screen-space GI (tr_ssgi.cpp): mip levels of the half resolution source
#define SSGI_SOURCE_MIPS 5

// light saber and effect primitives reflected analytically by SSR (tr_ssr.cpp)
#define SSR_MAX_EMITTERS 32

typedef enum
{
	// material shader stage types
	ST_COLORMAP = 0,			// vanilla Q3A style shader treatening
	ST_DIFFUSEMAP = 0,          // treat color and diffusemap the same
	ST_GLSL
} stageType_t;

typedef enum
{
	SPEC_NONE,		// no specular found
	SPEC_SPECGLOSS,	// Specular Gloss
	SPEC_RMO,		// calculate spec from rmo  texture with a specular of 0.04 for dielectric materials
	SPEC_RMOS,		// calculate spec from rmos texture with a specular of 0.0 - 0.08 from input
	SPEC_MOXR,		// calculate spec from moxr texture with a specular of 0.04 for dielectric materials
	SPEC_MOSR,		// calculate spec from mosr texture with a specular of 0.0 - 0.08 from input
	SPEC_ORM,		// calculate spec from orm  texture with a specular of 0.04 for dielectric materials
	SPEC_ORMS,		// calculate spec from orms texture with a specular of 0.0 - 0.08 from input
} specularType_t;

// where the PBR parameters of a lightall stage come from (tr_autopbr.cpp)
typedef enum
{
	PBR_SOURCE_NONE,		// not a lit lightall stage, or r_specularMapping 0
	PBR_SOURCE_EXPLICIT,	// specMap / rmoMap / ormMap / moxrMap ... keyword
	PBR_SOURCE_DISCOVERED,	// <diffuse>_specGloss / _rmo / _orm found next to the diffuse
	PBR_SOURCE_SCALAR,		// no map, but specularScale / roughness / gloss ... keywords
	PBR_SOURCE_LEGACY,		// diffuse only: white ORMS + specularScale fallback, r_autoPBR applies
	PBR_SOURCE_LEGACY_SPEC,	// converted legacy shader (r_autoPBRConvert): ORMS built from its
							// alphaGen lightingSpecular mask, r_autoPBR applies
	PBR_SOURCE_COUNT
} pbrSource_t;

// heuristic material classes of legacy stages (r_autoPBR 2)
typedef enum
{
	MATCLASS_GENERIC,
	MATCLASS_METAL,
	MATCLASS_SKIN,
	MATCLASS_CLOTH,
	MATCLASS_LEATHER,
	MATCLASS_PLASTIC,	// plastic and rubber
	MATCLASS_HAIR,		// hair and fur
	MATCLASS_COUNT
} materialClass_t;

enum AlphaTestType
{
	ALPHA_TEST_NONE,
	ALPHA_TEST_GT0,
	ALPHA_TEST_LT128,
	ALPHA_TEST_GE128,
	ALPHA_TEST_GE192,
	ALPHA_TEST_E255,
};

// any change in the LIGHTMAP_* defines here MUST be reflected in
// R_FindShader() in tr_bsp.c
#define LIGHTMAP_EXTERNAL	-5
#define LIGHTMAP_2D         -4	// shader is for 2D rendering
#define LIGHTMAP_BY_VERTEX  -3	// pre-lit triangle models
#define LIGHTMAP_WHITEIMAGE -2
#define LIGHTMAP_NONE       -1

typedef struct {
	qboolean		active;
	qboolean		isDetail;
	qboolean		glow;
	qboolean		emissive;
	qboolean		cloth;
	qboolean		specularScaleAuthored;	// specularScale / roughness / gloss ... keywords
	qboolean		weatherScaleAuthored;	// stage level weatherResponse keyword
	vec3_t			weatherScale;			// weatherResponse: wetness, puddle, runoff (1 = automatic, 0 = off)

	AlphaTestType	alphaTestType;

	textureBundle_t	bundle[NUM_TEXTURE_BUNDLES];

	waveForm_t		rgbWave;
	colorGen_t		rgbGen;

	waveForm_t		alphaWave;
	alphaGen_t		alphaGen;

	float			constantColor[4];			// for CGEN_CONST and AGEN_CONST

	uint32_t		stateBits;					// GLS_xxxx mask

	acff_t			adjustColorsForFog;

	int				lightmapStyle;

	stageType_t     type;
	specularType_t  specularType;
	pbrSource_t     pbrSource;
	materialClass_t materialClass;
	const char     *materialReason;	// static string: rule that picked materialClass
	const char     *materialToken;	// static string: token that matched, or NULL
	qboolean        pbrDrawn;		// drawn since registration, for r_pbrDumpMaterials
	image_t        *legacySpecImage;	// r_autoPBRConvert: mask of the removed lightingSpecular stage
	qboolean        legacyEnvDropped;	// r_autoPBRConvert: fake tcGen environment stage removed
	image_t        *autoRoughnessImage;	// r_autoPBRRoughness: generated ORMS of a legacy stage, bound at draw time
	struct shaderProgram_s *glslShaderGroup;
	int glslShaderIndex;

	vec4_t normalScale;
	vec4_t specularScale;
	vec3_t emissiveColor;
	float  emissiveIntensity;
	float  parallaxBias;
	// pomSelfShadow <0..1> keyword (tr_pom.cpp), 1 when not given
	float  pomSelfShadowStrength;
	qboolean pomSelfShadowSet;
	// skin scattering (tr_skinsss.cpp): 0 = not skin, else the share of the
	// diffuse light that scatters; decided once by R_SkinSSSClassifyShader
	float    skinScatter;
	qboolean skinScatterSet;		// skinScatter <0..1> keyword
	const char *skinReason;			// static string: why skinScatter is what it is
	image_t *skinMaskImage;			// skinMask keyword: R = scatter per texel

	surfaceSprite_t	*ss;

} shaderStage_t;

struct shaderCommands_s;

typedef enum {
	CT_FRONT_SIDED,
	CT_BACK_SIDED,
	CT_TWO_SIDED
} cullType_t;

typedef enum {
	FP_NONE,		// surface is translucent and will just be adjusted properly
	FP_EQUAL,		// surface is opaque but possibly alpha tested
	FP_LE			// surface is trnaslucent, but still needs a fog pass (fog surface)
} fogPass_t;

typedef struct {
	float		cloudHeight;
	image_t		*outerbox[6];
} skyParms_t;

typedef struct {
	vec3_t	color;
	float	depthForOpaque;
	// optional froxel fog medium (r_volumetricFog 2), shader keywords fogAnisotropy /
	// fogAlbedo: without them the fog uses r_volumetricFogAnisotropy and its color
	qboolean	hasAnisotropy;
	float		anisotropy;			// Henyey-Greenstein g, -0.9..0.9
	qboolean	hasAlbedo;
	vec3_t		albedo;				// scattering albedo, linear like color
	// fogExtinctionColor (r_volumetricFogRGBExtinction): relative extinction per channel,
	// sigma_t.rgb = sigma * extinctionColor, normalized to mean 1 (the opacity
	// of depthForOpaque is kept on average); without it (1, 1, 1)
	qboolean	hasExtinctionColor;
	vec3_t		extinctionColor;
} fogParms_t;

typedef enum {
	DEPTHPREPASS_ALPHATESTED,
	DEPTHPREPASS_SIMPLE,
	DEPTHPREPASS_SKIP
} depthPrepass_t;

typedef enum {
	FOLIAGE_NONE,
	FOLIAGE_LEAF,
	FOLIAGE_PLANT,
	FOLIAGE_GRASS
} foliageClass_t;

typedef struct {
	uint16_t reasons;
	int8_t score;
	uint8_t cls;
} foliageResult_t;

enum {
	FOLIAGE_NAME_LEAF      = 1 << 0,
	FOLIAGE_NAME_PLANT     = 1 << 1,
	FOLIAGE_NAME_GRASS     = 1 << 2,
	FOLIAGE_NAME_VINE      = 1 << 3,
	FOLIAGE_ALPHA_TEST     = 1 << 4,
	FOLIAGE_TWO_SIDED      = 1 << 5,
	FOLIAGE_ALPHA_SHADOW   = 1 << 6,
	FOLIAGE_YAVIN          = 1 << 7,
	FOLIAGE_TREE_MATERIAL  = 1 << 8,
	FOLIAGE_CARD_GEOMETRY  = 1 << 9,
	FOLIAGE_NEGATIVE       = 1 << 10,
	FOLIAGE_ALPHA_BLEND    = 1 << 11,
	FOLIAGE_MODEL_TREE     = 1 << 12,
	FOLIAGE_MODEL_OBJECT   = 1 << 13
};

typedef struct shader_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			lightmapIndex[MAXLIGHTMAPS];	// for a shader to match, both name and all lightmapIndex must match
	byte		styles[MAXLIGHTMAPS];

	int			index;					// this shader == tr.shaders[index]
	int			sortedIndex;			// this shader == tr.sortedShaders[sortedIndex]

	float		sort;					// lower numbered shaders draw before higher numbered

	qboolean	defaultShader;			// we want to return index 0 if the shader failed to
										// load for some reason, but R_FindShader should
										// still keep a name allocated for it, so if
										// something calls RE_RegisterShader again with
										// the same name, we don't try looking for it again

	qboolean	explicitlyDefined;		// found in a .shader file
	qboolean	alphaShadow;			// q3map_alphashadow: use the base alpha as a sun-shadow cutout
	int8_t		particleLight;			// particleLighting keyword: 0 = auto (blend state), 1 = on, -1 = off (r_particleLighting)
	float		surfaceLight;			// q3map_surfacelight / surfacelight value, 0 = none (area light hint only)
	vec3_t		surfaceLightColor;		// q3map_lightRGB / lightColor, all 0 = not given
	uint16_t foliageSignals;       // registration-time material evidence
	uint8_t  foliageHint;          // foliageClass_t, before model/surface vetoes
	qboolean	pomSilhouette;			// pomSilhouette: displaced silhouette shell (tr_pom_silhouette.cpp)
	float		pomSilhouetteDistance;		// pomSilhouetteDistance: shell range limit, 0 = r_pomSilhouetteDistance
	int			pomSilhouetteSteps;		// pomSilhouetteSteps: max linear ray steps, 0 = r_pomSilhouetteMaxSteps
	int			pomSilhouetteSource;	// POM_SOURCE_*: why its surfaces got a shell (set at map load)
	int			pomOverride;			// r_autoPOMSilhouette <shader>: -1 none, 0 off, 1 on (cached)
	int			pomOverrideGeneration;	// generation of the override list pomOverride was looked up in

	int			surfaceFlags;			// if explicitlyDefined, this will have SURF_* flags
	int			contentFlags;

	qboolean	entityMergable;			// merge across entites optimizable (smoke, blood)

	qboolean	isSky;
	skyParms_t	sky;
	fogParms_t	fogParms;

	float		portalRange;			// distance to fog out at
	qboolean	isPortal;

	vec3_t		weatherScale;			// shader level weatherResponse, default of every stage

	cullType_t	cullType;				// CT_FRONT_SIDED, CT_BACK_SIDED, or CT_TWO_SIDED
	qboolean	polygonOffset;			// set for decals and other items that must be offset
	qboolean	noMipMaps;				// for console fonts, 2D elements, etc.
	qboolean	noPicMip;				// for images that must always be full resolution
	qboolean	noTC;					// for images that don't want to be texture compressed (eg skies)

	fogPass_t	fogPass;				// draw a blended pass, possibly with depth test equals

	int         vertexAttribs;          // not all shaders will need all data to be gathered

	int			numDeforms;
	deformStage_t	deforms[MAX_SHADER_DEFORMS];

	int			numUnfoggedPasses;
	int			numSurfaceSpriteStages;
	GLuint		spriteUbo;
	int			ShaderInstanceUboOffset;

	shaderStage_t	*stages[MAX_SHADER_STAGES];

	void		(*optimalStageIteratorFunc)( void );
	qboolean	isHDRLit;
	const char	*lightallSkipReason;	// static string: why CollapseStagesToGLSL kept the legacy path
	depthPrepass_t	depthPrepass;
	qboolean	useDistortion;

	float clampTime;                                  // time this shader is clamped to
	float timeOffset;                                 // current time offset for this shader

	struct shader_s *remappedShader;                  // current shader this one is remapped too

	struct	shader_s	*next;
} shader_t;

QINLINE qboolean ShaderRequiresCPUDeforms(const shader_t * shader)
{
	if ( shader->numDeforms > 1 )
	{
		return qtrue;
	}

	if ( shader->numDeforms > 0 )
	{
		switch (shader->deforms[0].deformation)
		{
			case DEFORM_NONE:
			case DEFORM_NORMALS:
			case DEFORM_WAVE:
			case DEFORM_BULGE:
			case DEFORM_MOVE:
			case DEFORM_PROJECTION_SHADOW:
				return qfalse;

			default:
				return qtrue;
		}
	}

	assert( shader->numDeforms == 0 );

	return qfalse;
}

enum
{
	GLS_SRCBLEND_ZERO					= (1 << 0),
	GLS_SRCBLEND_ONE					= (1 << 1),
	GLS_SRCBLEND_DST_COLOR				= (1 << 2),
	GLS_SRCBLEND_ONE_MINUS_DST_COLOR	= (1 << 3),
	GLS_SRCBLEND_SRC_ALPHA				= (1 << 4),
	GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA	= (1 << 5),
	GLS_SRCBLEND_DST_ALPHA				= (1 << 6),
	GLS_SRCBLEND_ONE_MINUS_DST_ALPHA	= (1 << 7),
	GLS_SRCBLEND_ALPHA_SATURATE			= (1 << 8),

	GLS_SRCBLEND_BITS					= GLS_SRCBLEND_ZERO
											| GLS_SRCBLEND_ONE
											| GLS_SRCBLEND_DST_COLOR
											| GLS_SRCBLEND_ONE_MINUS_DST_COLOR
											| GLS_SRCBLEND_SRC_ALPHA
											| GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA
											| GLS_SRCBLEND_DST_ALPHA
											| GLS_SRCBLEND_ONE_MINUS_DST_ALPHA
											| GLS_SRCBLEND_ALPHA_SATURATE,

	GLS_DSTBLEND_ZERO					= (1 << 9),
	GLS_DSTBLEND_ONE					= (1 << 10),
	GLS_DSTBLEND_SRC_COLOR				= (1 << 11),
	GLS_DSTBLEND_ONE_MINUS_SRC_COLOR	= (1 << 12),
	GLS_DSTBLEND_SRC_ALPHA				= (1 << 13),
	GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA	= (1 << 14),
	GLS_DSTBLEND_DST_ALPHA				= (1 << 15),
	GLS_DSTBLEND_ONE_MINUS_DST_ALPHA	= (1 << 16),

	GLS_DSTBLEND_BITS					= GLS_DSTBLEND_ZERO
											| GLS_DSTBLEND_ONE
											| GLS_DSTBLEND_SRC_COLOR
											| GLS_DSTBLEND_ONE_MINUS_SRC_COLOR
											| GLS_DSTBLEND_SRC_ALPHA
											| GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
											| GLS_DSTBLEND_DST_ALPHA
											| GLS_DSTBLEND_ONE_MINUS_DST_ALPHA,

	GLS_DEPTHMASK_TRUE					= (1 << 17),

	GLS_POLYMODE_LINE					= (1 << 18),

	GLS_DEPTHTEST_DISABLE				= (1 << 19),

	GLS_DEPTHFUNC_LESS					= (1 << 20),
	GLS_DEPTHFUNC_EQUAL					= (1 << 21),
	GLS_DEPTHFUNC_GREATER				= (1 << 22),

	GLS_DEPTHFUNC_BITS					= GLS_DEPTHFUNC_LESS
											| GLS_DEPTHFUNC_EQUAL
											| GLS_DEPTHFUNC_GREATER,

	GLS_REDMASK_FALSE					= (1 << 23),
	GLS_GREENMASK_FALSE					= (1 << 24),
	GLS_BLUEMASK_FALSE					= (1 << 25),
	GLS_ALPHAMASK_FALSE					= (1 << 26),

	GLS_COLORMASK_BITS					= GLS_REDMASK_FALSE
											| GLS_GREENMASK_FALSE
											| GLS_BLUEMASK_FALSE
											| GLS_ALPHAMASK_FALSE,

	GLS_STENCILTEST_ENABLE				= (1 << 27),

	GLS_POLYGON_OFFSET_FILL				= (1 << 28),

	GLS_COLORMASK_BUF1					= (1 << 29),

	GLS_DEPTH_CLAMP						= (1 << 30),

	GLS_DEFAULT							= GLS_DEPTHMASK_TRUE
};

struct Attribute
{
	int numComponents;
	bool integerAttribute;
	GLenum type;
	bool normalize;
	int offset;
};

const int MAX_ATTRIBUTES = 8;
struct VertexFormat
{
	Attribute attributes[MAX_ATTRIBUTES];
};

enum
{
	ATTR_POSITION		= 0x0001,
	ATTR_TEXCOORD0		= 0x0002,
	ATTR_TEXCOORD1		= 0x0004,
	ATTR_TEXCOORD2		= 0x0008,
	ATTR_TEXCOORD3		= 0x0010,
	ATTR_TEXCOORD4		= 0x0020,
	ATTR_TANGENT		= 0x0040,
	ATTR_NORMAL			= 0x0080,
	ATTR_COLOR			= 0x0100,
	ATTR_LIGHTDIRECTION = 0x0200,
	ATTR_BONE_INDEXES	= 0x0400,
	ATTR_BONE_WEIGHTS	= 0x0800,

	// for .md3 interpolation and some sprite data
	ATTR_POSITION2		= 0x1000,
#ifdef REND2_SP_MD3
	ATTR_TANGENT2		= 0x2000,
	ATTR_NORMAL2		= 0x4000,
#endif // REND2_SP_MD3

	ATTR_DEFAULT		= ATTR_POSITION,
	ATTR_BITS			= ATTR_POSITION |
							ATTR_TEXCOORD0 |
							ATTR_TEXCOORD1 |
							ATTR_TEXCOORD2 |
							ATTR_TEXCOORD3 |
							ATTR_TEXCOORD4 |
							ATTR_TANGENT |
							ATTR_NORMAL |
							ATTR_COLOR |
							ATTR_LIGHTDIRECTION |
							ATTR_BONE_INDEXES |
							ATTR_BONE_WEIGHTS |
							ATTR_POSITION2
#ifdef REND2_SP_MD3
							| 
							ATTR_TANGENT2 |
							ATTR_NORMAL2
#endif // REND2_SP_MD3
};

enum
{
	TEXCOLORDEF_USE_VERTICES = 0x0000,
	TEXCOLORDEF_SCREEN_TRIANGLE = 0x0001,

	TEXCOLORDEF_ALL = 0x0001,
	TEXCOLORDEF_COUNT = TEXCOLORDEF_ALL + 1,
};

enum
{
	GENERICDEF_USE_DEFORM_VERTEXES 		= 0x0001,
	GENERICDEF_USE_TCGEN_AND_TCMOD 		= 0x0002,
	GENERICDEF_USE_FOG             		= 0x0004,
	GENERICDEF_USE_RGBAGEN         		= 0x0008,
	GENERICDEF_USE_SKELETAL_ANIMATION	= 0x0010,
	GENERICDEF_USE_FLARE_TEST			= 0x0020,
	// GENERICDEF_USE_ALPHA_TEST			= 0x0040,
#ifdef REND2_SP_MD3
	GENERICDEF_USE_VERTEX_ANIMATION		= 0x0080,
	GENERICDEF_ALL						= 0x00FF,
#else
	GENERICDEF_ALL						= 0x003F,
#endif // REND2_SP

	GENERICDEF_COUNT                	= GENERICDEF_ALL + 1,
};

enum
{
	FOGDEF_USE_DEFORM_VERTEXES  		= 0x0001,
	FOGDEF_USE_SKELETAL_ANIMATION 		= 0x0002,
	//FOGDEF_USE_ALPHA_TEST				= 0x0004,
	FOGDEF_USE_FALLBACK_GLOBAL_FOG		= 0x0004,
#ifdef REND2_SP_MD3
	FOGDEF_USE_VERTEX_ANIMATION			= 0x0010,
	FOGDEF_ALL							= 0x001F,
#else
	FOGDEF_ALL							= 0x0007,
#endif // REND2_SP

	FOGDEF_COUNT                		= FOGDEF_ALL + 1,
};

enum
{
	VELOCITYDEF_USE_DEFORM_VERTEXES		= 0x0001,
	VELOCITYDEF_USE_SKELETAL_ANIMATION	= 0x0002,
	VELOCITYDEF_USE_TCGEN_AND_TCMOD		= 0x0004,
	VELOCITYDEF_USE_RGBAGEN				= 0x0008,
	VELOCITYDEF_USE_PARALLAXMAP			= 0x0010,
	//VELOCITYDEF_USE_ALPHA_TEST		= 0x0004,
#ifdef REND2_SP_MD3
	VELOCITYDEF_USE_VERTEX_ANIMATION	= 0x0004,
	VELOCITYDEF_ALL						= 0x0007,
#else
	VELOCITYDEF_ALL						= 0x001F,
#endif // REND2_SP
	VELOCITYDEF_COUNT					= VELOCITYDEF_ALL + 1,
};

enum
{
	MOTIONBLURDEF_DEFAULT	= 0,	// per sample velocity (reconstruction filter)
	MOTIONBLURDEF_LOW		= 1,	// center velocity only, fewer fetches
	MOTIONBLURDEF_DEBUG		= 2,	// r_motionBlurDebug views
	MOTIONBLURDEF_COUNT
};

enum
{
	RAINLENSDEF_FILM	= 0,	// wetness / film texture to lens field
	RAINLENSDEF_DROPS	= 1,	// instanced drops, micro drops and sheets to lens field
	RAINLENSDEF_DEBUG_AGENTS = 2,	// r_rainLensDebug 5, 7, 8 agent views
	RAINLENSDEF_COUNT
};

enum
{
	RAINLENSCOMPOSITE_DEFAULT = 0,	// ambient reflection (no cubemap)
	RAINLENSCOMPOSITE_CUBEMAP = 1,	// nearest environment cubemap reflection
	RAINLENSCOMPOSITE_COUNT
};

enum
{
	SSRDEF_TRACE		= 0,	// ray march, linear
	SSRDEF_TRACE_HIZ	= 1,	// ray march, hierarchical depth
	SSRDEF_CLASSIFY		= 2,	// early depth mask of the pixels that need a ray
	SSRDEF_COUNT
};

enum
{
	SSGIDEF_TRACE		= 0,	// ray march, linear
	SSGIDEF_TRACE_HIZ	= 1,	// ray march, hierarchical depth
	SSGIDEF_COUNT
};

// skin SSS programs (skin_sss.glsl)
enum
{
	SKINSSSDEF_BLUR_H		= 0,
	SKINSSSDEF_BLUR_V		= 1,
	SKINSSSDEF_COMPOSITE	= 2,
	SKINSSSDEF_COUNT
};

enum
{
	REFRACTIONDEF_USE_DEFORM_VERTEXES		= 0x0001,
	REFRACTIONDEF_USE_TCGEN_AND_TCMOD		= 0x0002,
	REFRACTIONDEF_USE_RGBAGEN				= 0x0004,
	REFRACTIONDEF_USE_SKELETAL_ANIMATION	= 0x0008,
	//REFRACTIONDEF_USE_ALPHA_TEST			= 0x0010,
	REFRACTIONDEF_USE_SRGB_TRANSFORM		= 0x0010,
#ifdef REND2_SP_MD3
	REFRACTIONDEF_USE_VERTEX_ANIMATION		= 0x0040,
	REFRACTIONDEF_ALL						= 0x007F,
#else
	REFRACTIONDEF_ALL						= 0x001F,
#endif // REND2_SP

	REFRACTIONDEF_COUNT						= REFRACTIONDEF_ALL + 1,
};

enum
{
	LIGHTDEF_USE_LIGHTMAP        		= 0x0001,
	LIGHTDEF_USE_LIGHT_VECTOR    		= 0x0002,
	LIGHTDEF_USE_LIGHT_VERTEX    		= 0x0003,
	LIGHTDEF_USE_TCGEN_AND_TCMOD 		= 0x0004,
	LIGHTDEF_USE_PARALLAXMAP     		= 0x0008,
	LIGHTDEF_USE_SKELETAL_ANIMATION 	= 0x0010,
	//LIGHTDEF_USE_ALPHA_TEST		 		= 0x0040,
	LIGHTDEF_USE_CLOTH_BRDF				= 0x0020,
	LIGHTDEF_USE_SPEC_GLOSS				= 0x0040,

	LIGHTDEF_LIGHTTYPE_MASK      		= LIGHTDEF_USE_LIGHTMAP |
										  LIGHTDEF_USE_LIGHT_VECTOR |
										  LIGHTDEF_USE_LIGHT_VERTEX,

#ifdef REND2_SP_MD3
	LIGHTDEF_USE_VERTEX_ANIMATION		= 0x0200,
	LIGHTDEF_ALL						= 0x03FF,
#else
	LIGHTDEF_ALL						= 0x007F,
#endif // REND2_SP

	LIGHTDEF_COUNT               		= LIGHTDEF_ALL + 1
};

// silhouette POM programs (tr_pom_silhouette.cpp). The lightall set only
// covers world surfaces: light type (lightmap / vertex) x spec gloss x cloth.
enum
{
	POMSDEF_LIGHT_VERTEX			= 0x0001,	// else lightmap
	POMSDEF_SPEC_GLOSS			= 0x0002,
	POMSDEF_CLOTH_BRDF			= 0x0004,
	POMSDEF_LIGHTALL_COUNT		= 0x0008,

	POMSDEF_DEPTH_VELOCITY		= 0,		// depth prepass into depthVelocityFbo
	POMSDEF_DEPTH_ONLY			= 1,		// depth prepass without velocity, sun cascades
	POMSDEF_DEPTH_COUNT			= 2,
};

// tess.pomMode: what the batched surfaces are, see RB_SetPomMode
enum
{
	POM_MODE_NONE		= 0,
	POM_MODE_SHELL		= 1,	// silhouette shells, SF_POM_SHELL
	POM_MODE_FADEBASE	= 2,	// base surfaces inside the crossfade band, SF_POM_FADEBASE
};

enum
{
	SSDEF_FACE_CAMERA					= 0x01,
	//SSDEF_ALPHA_TEST					= 0x02,
	SSDEF_FACE_UP						= 0x02,
	SSDEF_FX_SPRITE						= 0x04,
	SSDEF_USE_FOG						= 0x08,
	SSDEF_FOG_MODULATE					= 0x10,
	SSDEF_ADDITIVE						= 0x20,
	SSDEF_FLATTENED						= 0x40,
	SSDEF_VELOCITY						= 0x80,
	SSDEF_AUTO_GRASS					= 0x100,	// r_grassCardMode: world stable cross/tri cards

	SSDEF_ALL							= 0x1FF,
	SSDEF_COUNT							= SSDEF_ALL + 1
};

enum
{
	GLSL_INT,
	GLSL_FLOAT,
	GLSL_VEC2,
	GLSL_VEC3,
	GLSL_VEC4,
	GLSL_MAT4x3,
	GLSL_MAT4x4,
};

enum uniformBlock_t
{
	UNIFORM_BLOCK_CAMERA,
	UNIFORM_BLOCK_SCENE,
	UNIFORM_BLOCK_LIGHTS,
	UNIFORM_BLOCK_FOGS,
	UNIFORM_BLOCK_ENTITY,
	UNIFORM_BLOCK_PREVIOUS_ENTITY,
	UNIFORM_BLOCK_SHADER_INSTANCE,
	UNIFORM_BLOCK_BONES,
	UNIFORM_BLOCK_PREVIOUS_BONES,
	UNIFORM_BLOCK_TEMPORAL_INFO,
	UNIFORM_BLOCK_SURFACESPRITE,
	UNIFORM_BLOCK_VOLUMETRIC_FOG,
	UNIFORM_BLOCK_FOLIAGE_INTERACTION,
	UNIFORM_BLOCK_VOLUMETRIC_PARTICLES,
	UNIFORM_BLOCK_COUNT
};

struct uniformBlockInfo_t
{
	int slot;
	const char *name;
	size_t size;
};
extern const uniformBlockInfo_t uniformBlocksInfo[UNIFORM_BLOCK_COUNT];

#define MAX_BLOCKS (32)
#define MAX_BLOCK_NAME_LEN (32)
struct Block
{
	const char *blockText;
	size_t blockTextLength;
	int blockTextFirstLine;

	const char *blockHeaderTitle;
	size_t blockHeaderTitleLength;

	const char *blockHeaderText;
	size_t blockHeaderTextLength;
};

enum GPUShaderType
{
	GPUSHADER_VERTEX,
	GPUSHADER_FRAGMENT,
	GPUSHADER_GEOMETRY,
	GPUSHADER_TYPE_COUNT
};

struct GPUShaderDesc
{
	GPUShaderType type;
	const char *source;
	int firstLineNumber;
};

struct GPUProgramDesc
{
	size_t numShaders;
	GPUShaderDesc *shaders;
};

// Needs to stay in sync with uniformInfo_t in tr_glsl.cpp
typedef enum
{
	UNIFORM_DIFFUSEMAP = 0,
	UNIFORM_LIGHTMAP,
	UNIFORM_NORMALMAP,
	UNIFORM_DELUXEMAP,
	UNIFORM_SPECULARMAP,
	UNIFORM_SSAOMAP,
	UNIFORM_EMISSIVEMAP,

	UNIFORM_TEXTUREMAP,
	UNIFORM_LEVELSMAP,
	UNIFORM_CUBEMAP,
	UNIFORM_ENVBRDFMAP,
	UNIFORM_DIFFUSEIRRADIANCEMAP,
	UNIFORM_PROBEAVERAGEMAP,

	UNIFORM_SCREENIMAGEMAP,
	UNIFORM_SCREENDEPTHMAP,

	UNIFORM_EDGEMAP,
	UNIFORM_AREAMAP,
	UNIFORM_SEARCHMAP,
	UNIFORM_BLENDMAP,
	UNIFORM_VELOCITYMAP,

	UNIFORM_VOLUMETRICLIGHTMAP,

	UNIFORM_LIGHTGRIDORIGIN,
	UNIFORM_LIGHTGRIDCELLINVERSESIZE,
	UNIFORM_ENTITYGRIDAMBIENT,
	UNIFORM_ENTITYGRIDDIRECTED,
	UNIFORM_ENTITYGRIDDIRECTION,

	UNIFORM_SHADOWMAP,
	UNIFORM_SHADOWMAP2,

	UNIFORM_SHADOWMVP,
	UNIFORM_SHADOWMVP2,
	UNIFORM_SHADOWMVP3,

	UNIFORM_ENABLETEXTURES,
	UNIFORM_EMISSIVEPARAMS, // rgb = scale; |w| = 0 off, 1 explicit, 2 auto source; sign = legacy/linear scene

	UNIFORM_DIFFUSETEXMATRIX,
	UNIFORM_DIFFUSETEXOFFTURB,

	UNIFORM_TCGEN0,
	UNIFORM_TCGEN0VECTOR0,
	UNIFORM_TCGEN0VECTOR1,
	UNIFORM_TCGEN1,

	UNIFORM_COLORGEN,
	UNIFORM_ALPHAGEN,
	UNIFORM_COLOR,
	UNIFORM_BASECOLOR,
	UNIFORM_VERTCOLOR,
	UNIFORM_CHROMATICABERRATIONDELTA,

	UNIFORM_LIGHTFORWARD,
	UNIFORM_LIGHTUP,
	UNIFORM_LIGHTRIGHT,
	UNIFORM_LIGHTORIGIN,
	UNIFORM_LIGHTRADIUS,
	UNIFORM_DISINTEGRATION,
	UNIFORM_LIGHTMASK,
	UNIFORM_FOGINDEX,

	UNIFORM_FOGCOLORMASK,

	UNIFORM_MODELVIEWPROJECTIONMATRIX,
	UNIFORM_SPRITEVIEWORIGIN,
	UNIFORM_SPRITEVIEWLEFT,
	UNIFORM_SPRITEVIEWUP,

	UNIFORM_VERTEXLERP,
	UNIFORM_NORMALSCALE,
	UNIFORM_SPECULARSCALE,
	UNIFORM_MATERIALDEBUG,	// r_autoPBRDebug: rgb = color, a = 1 when on (tr_autopbr.cpp)
	UNIFORM_FOLIAGEDEBUG,
	UNIFORM_AUTOGRASS,		// r_grassCardMode: cards, lod distance, debug mode, width scale
	UNIFORM_FOLIAGEWIND,	// r_foliageWind: wind dir x, y, amplitude, speed
	UNIFORM_FOLIAGEWINDPARAMS,	// r_foliageWind: mode, debug, frozen time, frozen flag
	UNIFORM_LEAFFLUTTER,		// r_leafFlutter: wind dir x, y, amplitude (0 = off), speed
	UNIFORM_LEAFFLUTTERPARAMS,	// r_leafFlutter: time, previous time, 1 / model xy radius, normal amount
	UNIFORM_LEAFFLUTTERDEBUG,	// r_leafFlutterDebug: 0, 4 = highlight, 8 = magnitude colour
	UNIFORM_PLANTBEND,			// FOLIAGE_PLANT: root (object space) xyz, 1 / plant size (0 = off)
	UNIFORM_PLANTBENDPARAMS,	// FOLIAGE_PLANT: wind dir x, y, wind bend, interaction (0 / 1)
	UNIFORM_PLANTBENDTIME,		// FOLIAGE_PLANT: time, previous time, wind speed, unused
	UNIFORM_FOLIAGEINTERACT,	// surface sprites: interaction (0 / 1), contact heat debug (0 / 1)
	UNIFORM_DIFFUSEBRDF,	// r_diffuseBRDF: 0 = Lambert, 1 = Burley/Disney
	UNIFORM_PARALLAXBIAS,

	UNIFORM_VIEWINFO, // znear, zfar, width/2, height/2

	UNIFORM_INVTEXRES,
	UNIFORM_AUTOEXPOSUREMINMAX,
	UNIFORM_TONEMINAVGMAXLINEAR,
	UNIFORM_TONEMAPPARAMS, // operator, debug view, legacy EV gain, scene-linear EV gain
	UNIFORM_COLORGRADINGLUT,
	UNIFORM_COLORGRADINGPARAMS, // mode, intensity, LUT size

	UNIFORM_CUBEMAPINFO,
	UNIFORM_DIFFUSEIBLPARAMS,

	UNIFORM_ALPHA_TEST_TYPE,

	UNIFORM_MAPZEXTENTS,
	UNIFORM_ZONEOFFSET,
	UNIFORM_ENVFORCE,
	UNIFORM_RANDOMOFFSET,
	UNIFORM_CHUNK_PARTICLES,

	UNIFORM_BLOOMSTRENGTH,
	UNIFORM_BLOOMMAP,
	UNIFORM_BLOOMPARAMS,
	UNIFORM_BLOOMSCENEMAP,

	UNIFORM_AODEPTHMAP,
	UNIFORM_AOMAP,
	UNIFORM_LEGACYAOMAP,
	UNIFORM_AOPROJECTION,	// P[0], P[5], P[8], P[9] of the view projection
	UNIFORM_AODEPTHPARAMS,	// P[14], P[10], zFar, sky depth threshold
	UNIFORM_AOVIEWPORT,		// view rectangle in texture coordinates
	UNIFORM_AOTEXELSIZE,	// 1 / source size, 1 / destination size
	UNIFORM_AOSETTINGS,		// pass specific
	UNIFORM_AOSETTINGS2,	// pass specific
	UNIFORM_AOSETTINGS3,	// pass specific
	UNIFORM_AOLIGHTDIR,		// view space direction to the sun
	UNIFORM_AOBENTMAP,		// GTAO bent normals
	UNIFORM_AOVIEWTOWORLD,	// AO view space -> world space rotation

	UNIFORM_MBINVVIEWPROJECTION,	// inverse of the current view projection
	UNIFORM_MBPREVVIEWPROJECTION,	// previous frame view projection
	UNIFORM_MBPARAMS,		// exposure scale, max length (px), max samples, velocity buffer valid
	UNIFORM_MBPARAMS2,		// view model scale, P[14], P[10], legacy (display encoded) HDR buffer
	UNIFORM_MBPARAMS3,		// debug view, quality, 0, 0

	UNIFORM_RAINLENSPARAMS,		// per pass, see tr_rainlens.cpp
	UNIFORM_RAINLENSPARAMS2,	// per pass: field size or debug view, legacy (display encoded) HDR buffer
	UNIFORM_RAINLENSOPTICS,		// vec4[8] lens reflection / glint lights, see tr_rainlens.cpp
	UNIFORM_RAINLENSDEBUG,		// vec4[4] r_rainLensDebug 9 controller panel

	UNIFORM_SSRNORMALMAP,	// tr_ssr.cpp, see the ssr_*.glsl headers
	UNIFORM_SSRSPECULARMAP,
	UNIFORM_SSRCUBEMAPMAP,
	UNIFORM_SSRSCENEMAP,
	UNIFORM_SSRTRACEMAP,
	UNIFORM_SSRHISTORYMAP,
	UNIFORM_SSRHISTORYGEOMMAP,
	UNIFORM_SSRHIZMAP,
	UNIFORM_SSRPROJECTION,	// P[0], P[5], P[8], P[9]
	UNIFORM_SSRDEPTHPARAMS,	// P[14], P[10], zFar, depth hack threshold
	UNIFORM_SSRVIEWPORT,	// view rectangle in texture coordinates
	UNIFORM_SSRTEXELSIZE,	// 1 / source size, 1 / destination size
	UNIFORM_SSRSETTINGS,	// pass specific
	UNIFORM_SSRSETTINGS2,	// pass specific
	UNIFORM_SSRSETTINGS3,	// pass specific
	UNIFORM_SSRSETTINGS4,	// pass specific
	UNIFORM_SSRWORLDTOVIEW,	// world -> SSR view space (x right, y up, z forward)
	UNIFORM_SSRREPROJECT,	// SSR view space -> previous frame clip space
	UNIFORM_SSRPREVVIEWTOVIEW,	// previous frame SSR view space -> SSR view space
	UNIFORM_SSRHITMAP,		// SSR ray hits of this frame
	UNIFORM_SSRPREVHITMAP,	// SSR ray hits of the previous frame (hit cache)
	UNIFORM_SSREMITTERS,	// SSR_MAX_EMITTERS * 3 vec4, see RB_SSRCollectEmitters
	UNIFORM_SSREMITTERPARAMS,	// count, 0, max roughness, 0
	UNIFORM_SSGIALBEDOMAP,		// tr_ssgi.cpp, see the ssgi_*.glsl headers
	UNIFORM_SSGIRADIANCEMAP,
	UNIFORM_SSGISOURCEMAP,

	UNIFORM_PARTICLELIGHT,	// sprite particle lighting (tr_volumetric.cpp): scale, floor, max gain, mix; inject: field on, term, 0, 0
	UNIFORM_PARTICLELIGHTVOLUME,	// particle light field (froxelParticleLightImage)

	UNIFORM_FROXELFOGMODE,	// 0 = legacy fog, 1 = froxel volume lookup, 2 = none (composited), tr_volumetric.cpp
	UNIFORM_FROXELVOLUME,	// integrated scattering / transmittance volume
	UNIFORM_FROXELTAIL,		// extinction and radiance of the last slice
	UNIFORM_FROXELSOURCE,	// injected (filtered) scattering volume
	UNIFORM_FROXELHISTORY,	// previous frame injected volume
	UNIFORM_FROXELDYNAMIC,	// dynamic light in-scattering volume (current frame only)
	UNIFORM_FROXELCARRY,	// integration state of the previous slice
	UNIFORM_VOLUMETRICSTATICGRID,	// baked light grid without the sun
	UNIFORM_VOLUMETRICSUNGRID,		// baked sun part of the light grid
	UNIFORM_VOLUMETRICDIRGRID,		// directed (non-sun) part of the light grid
	UNIFORM_VOLUMETRICDIRVECGRID,	// its direction weighted by its luminance
	UNIFORM_VOLUMETRICLEGACYGRID,	// merged legacy light grid (debug view 25)
	UNIFORM_FROXELSLICE,	// slice rendered by the injection / integration pass
	UNIFORM_FROXELNOISE,	// tiling density noise
	UNIFORM_FROXELMEDIA,	// extinction of this frame (r_volumetricSelfShadow)
	UNIFORM_FROXELTRANSMITTANCE,	// integrated RGB transmittance (r_volumetricFogRGBExtinction)
	UNIFORM_FROXELEXTINCTION,		// injected RGB extinction (history / source)
	UNIFORM_FROXELCARRYT,			// RGB transmittance of the previous slice

	UNIFORM_FPLUSLIGHTS,	// Forward+ light data (buffer texture)
	UNIFORM_FPLUSGRID,		// Forward+ cluster offset / count (buffer texture)
	UNIFORM_FPLUSINDICES,	// Forward+ cluster light indexes (buffer texture)

	UNIFORM_POMGROUPS,		// silhouette POM group footprints (buffer texture)
	UNIFORM_POMPARAMS,		// silhouette POM: min / max linear steps, binary steps, view dependence
	UNIFORM_POMPARAMS2,		// silhouette POM: draw mode, depth mode, ortho pixel footprint, debug view
	UNIFORM_POMFADE,		// silhouette POM: crossfade start, 1 / width, debug split x, unused

	UNIFORM_POMSHADOW,		// POM self shadow (tr_pom.cpp): strength (0 off), steps, bias, softness
	UNIFORM_POMTRAVERSAL,	// POM view ray: adaptive (0/1), min steps, max steps, binary steps
	UNIFORM_POMLOD,			// POM: fade start, 1 / fade width (0 off), local light mode, max local lights
	UNIFORM_POMDEBUG,		// POM: frozen sun direction (0 = live), debug view

	UNIFORM_LTCMATRIXMAP,		// LTC area lights: tr.ltcMatrixImage (USE_LTC)
	UNIFORM_LTCAMPLITUDEMAP,	// LTC area lights: tr.ltcAmplitudeImage (USE_LTC)

	UNIFORM_WEATHERDEPTHMAP,	// r_weatherWetness: tr.weatherDepthImage
	UNIFORM_WEATHERMVP,			// world -> weather depth clip space
	UNIFORM_WETNESSPARAMS,		// strength, roughness scale, darkening, normal flattening
	UNIFORM_WETNESSPARAMS2,		// depth bias, normal offset, debug mode, split x
	UNIFORM_WETNESSPARAMS3,		// facing floor, physical porosity (0/1), material class, unused
	UNIFORM_PUDDLEPARAMS,		// coverage (<= 0 off, < 0 ineligible), roughness, slope min, slope max
	UNIFORM_PUDDLEPARAMS2,		// 1 / scale
	UNIFORM_PUDDLEHEIGHT,		// relief depth low, 1 / (high - low) (0: no height), softness, fill bias
	UNIFORM_PUDDLERIPPLE,		// slope strength (0 off), 1 / cell size, ring clock (cycles mod 256), density
	UNIFORM_RUNOFFPARAMS,		// strength (0 off, < 0 ineligible), 1 / scale, flow clock (cells mod 256), probe offset (world)
	UNIFORM_RUNOFFPARAMS2,		// wind shear x, y (per unit of fall), windward amount, frame origin z
	UNIFORM_RUNOFFFRAME,		// pattern frame: horizontal axis a1 (world xy), origin xy
	UNIFORM_WEATHERMATERIAL,	// debug: weatherResponse wetness, puddle, runoff scale, exclusion reason

	UNIFORM_SKINPARAMS,		// skin SSS of this draw: scatter (0 = not skin), has mask, compare split x (< 0 off), unused
	UNIFORM_SKINWRAP,		// skin SSS: rgb = wrap widths (r_skinSSS 1), w = transmission strength
	UNIFORM_SKINMASKMAP,	// skin SSS: skinMask image (TB_SKINMASK)
	UNIFORM_SKINKERNEL,		// skin SSS blur: SKIN_SSS_MAX_TAPS vec4 (rgb weight, offset)
	UNIFORM_SKINSETTINGS,	// skin SSS blur / composite, pass specific
	UNIFORM_SKINSETTINGS2,	// skin SSS blur / composite, pass specific

	UNIFORM_WEATHERTYPE,		// weather.glsl: 0 legacy particle (snow, dust, sand, fog), 1 rain streak (r_rainStreaks)
	UNIFORM_RAINSTREAK,			// width scale, length scale, depth range of the weather map (world units), sRGB coverage (0/1)
	UNIFORM_RAINSHADE,			// opacity, lighting mix, world size of a pixel at distance 1, debug mode
	UNIFORM_RAINLIGHT,			// rgb = light without a grid, w = light grid valid (0/1)
	UNIFORM_CAMERAVELOCITY,		// smoothed view origin velocity, world units per ms

	UNIFORM_WEATHERSURFACEMAP,	// r_rainSplashes: world-only weather depth (tr.weatherSurfaceImage)
	UNIFORM_SPLASHPARAMS,		// r_rainSplashes, program specific (weatherUpdate / weatherSplash)
	UNIFORM_SPLASHPARAMS2,		// r_rainSplashes, program specific (weatherUpdate / weatherSplash)

	UNIFORM_FOLIAGEFIELDMAP,	// r_foliageBendField: this frame's bend field (TB_FOLIAGEFIELD)
	UNIFORM_FOLIAGEFIELDPREVMAP,	// r_foliageBendField: previous frame's bend field (TB_FOLIAGEFIELD_PREV)
	UNIFORM_FOLIAGEFIELDDEBUG,	// r_foliageBendFieldDebug 1 overlay: corner x, y, square size, bend of full heat

	UNIFORM_LIGHTCOOKIEMAP,		// spot light cookies: tr.lightCookieArray (TB_LIGHTCOOKIES)
	UNIFORM_LIGHTCOOKIEPARAMS,	// enabled (0/1), rgb (0/1), world size of a pixel / froxel at distance 1, debug mode

	UNIFORM_COUNT
} uniform_t;

struct UniformData
{
	uniform_t index;
	int numElements;

	// uniform data follows immediately afterwards
	//char data[1];
};

// shaderProgram_t represents a collection of GLSL shaders which form a
// GLSL shader program
typedef struct shaderProgram_s
{
	char *name;

	GLuint program;
	uint32_t attribs; // vertex array attributes
	uint32_t xfbVariables; // transform feedback variables

	// uniform parameters
	GLint *uniforms;
	short *uniformBufferOffsets;
	char  *uniformBuffer;

	// uniform blocks
	uint32_t uniformBlocks;
} shaderProgram_t;

// trRefdef_t holds everything that comes in refdef_t,
// as well as the locally generated scene information
typedef struct {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewaxis[3];		// transformation matrix

	stereoFrame_t	stereoFrame;

	int			time;				// time in milliseconds for shader effects and other time dependent rendering issues
	int			rdflags;			// RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte		areamask[MAX_MAP_AREA_BYTES];
	qboolean	areamaskModified;	// qtrue if areamask changed since last scene

	float		floatTime;			// tr.refdef.time / 1000.0
	float		frameTime;			// delta last frame to frame now
	float		lastTime;			// last frame time

	float		blurFactor;

	// text messages for deform text shaders
	char		text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];

	int			num_entities;
	trRefEntity_t	*entities;

	int			num_dlights;
	struct dlight_s	*dlights;

	int			num_fogVolumes;		// local fog volumes (tr_fogvolume.cpp)
	const refFogVolume_t	*fogVolumes;
	int			num_volParticles;	// volumetric FX particles (tr_volparticle.cpp)
	const refVolParticle_t	*volParticles;

	int			numPolys;
	struct srfPoly_s	*polys;

	int			fistDrawSurf;
	int			numDrawSurfs;
	struct drawSurf_s	*drawSurfs;

	int         num_pshadows;
	struct pshadow_s *pshadows;

	float       sunShadowMvp[3][16];
	float       sunShadowSplits[3];
	float       sunShadowBlendWidths[2];
	float       sunShadowTexelSize[3];
	float       sunShadowDepthSpan[3];
	float       sunDir[4];
	float       sunCol[4];
	float       sunAmbCol[4];
	float       colorScale;

	float       autoExposureMinMax[2];
	float       toneMinAvgMaxLinear[3];
	bool		doLAGoggles;
	bool		doFullbright;
} trRefdef_t;


//=================================================================================

// skins allow models to be retextured without modifying the model file
typedef struct {
	char		name[MAX_QPATH];
	shader_t	*shader;
} skinSurface_t;

typedef struct skin_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			numSurfaces;
	skinSurface_t	*surfaces[128];
} skin_t;


typedef struct {
	int			originalBrushNumber;
	vec3_t		bounds[2];

	vec4_t		color;
	float		tcScale;				// texture coordinate vector scales
	fogParms_t	parms;

	// for clipping distance in fog when outside
	qboolean	hasSurface;
	float		surface[4];
} fog_t;

enum viewParmFlag_t {
	VPF_NOVIEWMODEL     = 0x01, // Don't render the view model
	VPF_DEPTHSHADOW     = 0x02, // Rendering depth-only
	VPF_DEPTHCLAMP      = 0x04, // Perform depth clamping when rendering z pass
	VPF_ORTHOGRAPHIC    = 0x08, // Use orthographic projection
	VPF_USESUNLIGHT     = 0x10,
	VPF_FARPLANEFRUSTUM = 0x20, // Use far clipping plane
	VPF_NOCUBEMAPS      = 0x40, // Don't render cubemaps
	VPF_POINTSHADOW		= 0x80,// Rendering pointlight shadow
	VPF_SHADOWCASCADES	= 0x100,// Rendering sun shadow cascades
	VPF_NOCLEAR			= 0x200,
	VPF_NODIFFUSEIBL	= 0x400, // Probe captures must not sample partially generated irradiance
};
using viewParmFlags_t = uint32_t;

enum viewParmType_t {
	VPT_SKYPORTAL,
	VPT_SUN_SHADOWS,
	VPT_PLAYER_SHADOWS,
	VPT_POINT_SHADOWS,
	VPT_PORTAL,
	VPT_MAIN,
	VPT_ALL
};

typedef struct {
	orientationr_t	ori;
	orientationr_t	world;
	vec3_t			pvsOrigin;			// may be different than or.origin for portals
	qboolean		isPortal;			// true if this view is through a portal
	qboolean		isMirror;			// the portal is a mirror, invert the face culling
	qboolean		isSkyPortal;
	int				flags;
	int				frameSceneNum;		// copied from tr.frameSceneNum
	int				frameCount;			// copied from tr.frameCount
	cplane_t		portalPlane;		// clip anything behind this if mirroring
	int				viewportX, viewportY, viewportWidth, viewportHeight;
	FBO_t			*targetFbo;
	int				targetFboLayer;
	float			fovX, fovY;
	float			projectionMatrix[16];
	cplane_t		frustum[5];
	vec3_t			visBounds[2];
	float			zFar;
	float			zNear;
	stereoFrame_t	stereoFrame;
	int				currentViewParm;
	viewParmType_t	viewParmType;
} viewParms_t;


/*
==============================================================================

SURFACES

==============================================================================
*/
typedef byte color4ub_t[4];

// any changes in surfaceType must be mirrored in rb_surfaceTable[]
typedef enum surfaceType_e
{
	SF_BAD,
	SF_SKIP,				// ignore
	SF_FACE,
	SF_GRID,
	SF_TRIANGLES,
	SF_POLY,
	SF_MDV,
	SF_MDR,
	SF_IQM,
	SF_MDX,
	SF_FLARE,
	SF_ENTITY,				// beams, rails, lightning, etc that can be determined by entity
	SF_VBO_MESH,
	SF_VBO_MDVMESH,
	SF_SPRITES,
	SF_WEATHER,
	SF_POM_SHELL,			// silhouette POM shell of a world surface (srfPomShell_t)
	SF_POM_FADEBASE,		// base surface of a shell in the crossfade band (srfPomShell_t::fadeBaseType)

	SF_NUM_SURFACE_TYPES,
	SF_MAX = 0x7fffffff			// ensures that sizeof( surfaceType_t ) == sizeof( int )
} surfaceType_t;

/*
the drawsurf sort data is packed into a single 32 bit value so it can be
compared quickly during the qsorting process
*/
#define	QSORT_CUBEMAP_SHIFT		0
#define QSORT_CUBEMAP_BITS		6
#define QSORT_CUBEMAP_MASK		((1 << QSORT_CUBEMAP_BITS) - 1)

#define QSORT_ENTITYNUM_SHIFT	(QSORT_CUBEMAP_SHIFT + QSORT_CUBEMAP_BITS)
#define QSORT_ENTITYNUM_BITS	REFENTITYNUM_BITS
#define QSORT_ENTITYNUM_MASK	((1 << QSORT_ENTITYNUM_BITS) - 1)

#define	QSORT_SHADERNUM_SHIFT	(QSORT_ENTITYNUM_SHIFT + QSORT_ENTITYNUM_BITS)
#define QSORT_SHADERNUM_BITS	SHADERNUM_BITS
#define QSORT_SHADERNUM_MASK	((1 << QSORT_SHADERNUM_BITS) - 1)

#define QSORT_POSTRENDER_SHIFT	(QSORT_SHADERNUM_SHIFT + QSORT_SHADERNUM_BITS)
#define QSORT_POSTRENDER_BITS	1
#define QSORT_POSTRENDER_MASK	((1 << QSORT_POSTRENDER_BITS) - 1)

#if QSORT_POSTRENDER_SHIFT >= 32
	#error "Sort field needs to be expanded"
#endif

typedef struct drawSurf_s {
	uint32_t sort; // bit combination for fast compares
	uint32_t dlightBits;
	surfaceType_t *surface; // any of surface*_t
	int fogIndex;
	foliageResult_t foliage;
} drawSurf_t;

#define	MAX_FACE_POINTS		64

#define	MAX_PATCH_SIZE		32			// max dimensions of a patch mesh in map file
#define	MAX_GRID_SIZE		65			// max dimensions of a grid mesh in memory

// when cgame directly specifies a polygon, it becomes a srfPoly_t
// as soon as it is called
typedef struct srfPoly_s {
	surfaceType_t	surfaceType;
	struct srfPoly_s *next;
	qhandle_t		hShader;
	int				fogIndex;
	int				numVerts;
	polyVert_t		*verts;
} srfPoly_t;

typedef struct srfFlare_s {
	surfaceType_t	surfaceType;
	vec3_t			origin;
	vec3_t			normal;
	vec3_t			color;
	shader_t		*shader;
	bool			portal_ranged;
} srfFlare_t;

struct vertexAttribute_t;
struct srfSprites_t
{
	surfaceType_t surfaceType;

	shader_t *shader;
	const surfaceSprite_t *sprite;
	int baseVertex;
	int numSprites;
	int numIndices;
	VBO_t *vbo;
	IBO_t *ibo;

	int fogIndex;
	AlphaTestType alphaTestType;

	int numAttributes;
	vertexAttribute_t *attributes;

	// bounds of the sprite anchors, padded by the sprite size (r_grassCardMode lod)
	vec3_t spriteMins;
	vec3_t spriteMaxs;
};

struct srfWeather_t
{
	surfaceType_t surfaceType;
};

typedef struct
{
	vec3_t          xyz;
	vec2_t          st;
	vec2_t          lightmap[MAXLIGHTMAPS];
	vec3_t          normal;
	vec4_t          tangent;
	vec3_t          lightdir;
	vec4_t			vertexColors[MAXLIGHTMAPS];

#if DEBUG_OPTIMIZEVERTICES
	unsigned int    id;
#endif
} srfVert_t;

#ifdef _G2_GORE
typedef struct
{
	vec3_t			position;
	uint32_t		normal;
	vec2_t			texCoords;
	byte			bonerefs[4];
	byte			weights[4];
	uint32_t		tangents;
} g2GoreVert_t;

typedef struct srfG2GoreSurface_s
{
	surfaceType_t   surfaceType;

	// indexes
	int             numIndexes;
	glIndex_t      *indexes;

	// vertexes
	int             numVerts;
	g2GoreVert_t    *verts;

	// BSP VBO offsets
	int             firstVert;
	int             firstIndex;

	// VBO cache info
	bool			cachedInFrame[MAX_FRAMES];

} srfG2GoreSurface_t;
#endif

// srfBspSurface_t covers SF_GRID, SF_TRIANGLES, SF_POLY, and SF_VBO_MESH
typedef struct srfBspSurface_s
{
	surfaceType_t   surfaceType;

	// dynamic lighting information
	int				dlightBits;
	int             pshadowBits;

	// culling information
	vec3_t			cullBounds[2];
	vec3_t			cullOrigin;
	float			cullRadius;
	cplane_t        cullPlane;

	// indexes
	int             numIndexes;
	glIndex_t      *indexes;

	// vertexes
	int             numVerts;
	srfVert_t      *verts;

	// BSP VBO offsets
	int             firstVert;
	int             firstIndex;
	glIndex_t       minIndex;
	glIndex_t       maxIndex;

	// static render data
	VBO_t          *vbo;
	IBO_t          *ibo;

	// SF_GRID specific variables after here

	// lod information, which may be different
	// than the culling information to allow for
	// groups of curves that LOD as a unit
	vec3_t			lodOrigin;
	float			lodRadius;
	int				lodFixed;
	int				lodStitched;

	// vertexes
	int				width, height;
	float			*widthLodError;
	float			*heightLodError;
} srfBspSurface_t;

// Silhouette POM shell of one world surface (tr_pom_silhouette.cpp): a top
// cap and side walls around the displaced height field volume, only a
// conservative raster volume. Shading coordinates come from the ray / height
// field intersection with the base surface parameterisation.
typedef struct srfPomShell_s
{
	surfaceType_t   surfaceType;		// SF_POM_SHELL
	surfaceType_t   fadeBaseType;		// SF_POM_FADEBASE, drawSurf of the base in the crossfade band

	srfBspSurface_t *base;
	struct msurface_s *surf;

	int             numVerts;
	int             numIndexes;
	int             firstIndex;
	glIndex_t       minIndex;
	glIndex_t       maxIndex;
	VBO_t          *vbo;
	IBO_t          *ibo;
	image_t        *groupsImage;		// group footprint buffer texture of the world

	vec3_t          bounds[2];			// shell bounds (model space)
	float           above;				// shell extent above the base plane, world units
	float           below;				// and below it
	int             numGroups;
	int             numWalls;			// boundary edges with a side wall
} srfPomShell_t;

// inter-quake-model
typedef struct {
	int		num_vertexes;
	int		num_triangles;
	int		num_frames;
	int		num_surfaces;
	int		num_joints;
	int		num_poses;
	struct srfIQModel_s	*surfaces;

	float		*positions;
	float		*texcoords;
	float		*normals;
	float		*tangents;
	byte		*blendIndexes;
	union {
		float	*f;
		byte	*b;
	} blendWeights;
	byte		*colors;
	int		*triangles;

	// depending upon the exporter, blend indices and weights might be int/float
	// as opposed to the recommended byte/byte, for example Noesis exports
	// int/float whereas the official IQM tool exports byte/byte
	byte blendWeightsType; // IQM_UBYTE or IQM_FLOAT

	int		*jointParents;
	float		*jointMats;
	float		*poseMats;
	float		*bounds;
	char		*names;
} iqmData_t;

// inter-quake-model surface
typedef struct srfIQModel_s {
	surfaceType_t	surfaceType;
	char		name[MAX_QPATH];
	shader_t	*shader;
	iqmData_t	*data;
	int		first_vertex, num_vertexes;
	int		first_triangle, num_triangles;
} srfIQModel_t;

typedef struct srfVBOMDVMesh_s
{
	surfaceType_t   surfaceType;

	struct mdvModel_s *mdvModel;
	struct mdvSurface_s *mdvSurface;

	// backEnd stats
	int				indexOffset;
	int             numIndexes;
	int             numVerts;
	glIndex_t       minIndex;
	glIndex_t       maxIndex;

	// static render data
	VBO_t          *vbo;
	IBO_t          *ibo;
} srfVBOMDVMesh_t;

extern	void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])(void *);

/*
==============================================================================

SHADOWS

==============================================================================
*/

typedef struct pshadow_s
{
	float sort;

	int    numEntities;
	int    entityNums[8];
	vec3_t entityOrigins[8];
	float  entityRadiuses[8];

	float viewRadius;
	vec3_t viewOrigin;

	vec3_t lightViewAxis[3];
	vec3_t lightOrigin;
	float  lightRadius;
	cplane_t cullPlane;
} pshadow_t;


/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

#define CULLINFO_NONE   0
#define CULLINFO_BOX    1
#define CULLINFO_SPHERE 2
#define CULLINFO_PLANE  4

typedef struct cullinfo_s {
	int             type;
	vec3_t          bounds[2];
	vec3_t			localOrigin;
	float			radius;
	cplane_t        plane;
} cullinfo_t;

typedef struct msurface_s {
	struct shader_s		*shader;
	int					fogIndex;
	int                 cubemapIndex;
	cullinfo_t          cullinfo;

	int					numSurfaceSprites;
	srfSprites_t		*surfaceSprites;

	surfaceType_t		*data;			// any of srf*_t
	struct srfPomShell_s *pomShell;	// silhouette POM shell, r_pomSilhouette 1 only
} msurface_t;


#define	CONTENTS_NODE		-1
typedef struct mnode_s {
	// common with leaf and node
	int			contents;		// -1 for nodes, to differentiate from leafs
	int             visCounts[MAX_VISCOUNTS];	// node needs to be traversed if current
	vec3_t		mins, maxs;		// for bounding box culling
	struct mnode_s	*parent;

	// node specific
	cplane_t	*plane;
	struct mnode_s	*children[2];

	// leaf specific
	int			cluster;
	int			area;

	int         firstmarksurface;
	int			nummarksurfaces;
} mnode_t;

typedef struct {
	vec3_t		bounds[2];		// for culling
	int			worldIndex;
	int			firstSurface;
	int			numSurfaces;
} bmodel_t;

typedef struct
{
	byte		ambientLight[MAXLIGHTMAPS][3];
	byte		directLight[MAXLIGHTMAPS][3];
	byte		styles[MAXLIGHTMAPS];
	byte		latLong[2];
//	byte		pad[2];								// to align to a cache line
} mgrid_t;

typedef struct {
	char		name[MAX_QPATH];		// ie: maps/tim_dm2.bsp
	char		baseName[MAX_QPATH];	// ie: tim_dm2

	int			dataSize;

	int			numShaders;
	dshader_t	*shaders;

	int			numBModels;
	bmodel_t	*bmodels;

	int			numplanes;
	cplane_t	*planes;

	int			numnodes;		// includes leafs
	int			numDecisionNodes;
	mnode_t		*nodes;

	int         numWorldSurfaces;

	int			numsurfaces;
	msurface_t	*surfaces;
	int         *surfacesViewCount;
	int         *surfacesDlightBits;
	int			*surfacesPshadowBits;

	int			numMergedSurfaces;
	msurface_t	*mergedSurfaces;
	int         *mergedSurfacesViewCount;
	int         *mergedSurfacesDlightBits;
	int			*mergedSurfacesPshadowBits;

	int			nummarksurfaces;
	int         *marksurfaces;
	int         *viewSurfaces;

	int			numfogs;
	fog_t		*fogs;
	const fog_t	*globalFog;
	int			globalFogIndex;

	// local fog volumes of the map ("FogVolumes" of cubemaps/<map>/env.json,
	// tr_fogvolume.cpp), added to every world scene
	int			numFogVolumes;
	refFogVolume_t	*fogVolumes;

	// lens water sources of the map ("LensWaterEmitters" of env.json,
	// tr_rainlens.cpp): waterfalls and other spray near the camera
	int			numLensWaterEmitters;
	struct lensWaterEmitter_s	*lensWaterEmitters;

	// Optional env.json HeightFog: base, opaqueDistance, falloff, top. Missing
	// components use cvars; an automatic base is kept out of archived cvars.
	vec4_t heightFogSettings;
	int heightFogSettingsMask;
	float heightFogAutoBase;

	vec3_t		lightGridOrigin;
	vec3_t		lightGridSize;
	vec3_t		lightGridInverseSize;
	int			lightGridBounds[3];
	float		*hdrLightGrid;
	int			lightGridOffsets[8];

	vec3_t		lightGridStep;

	mgrid_t		*lightGridData;
	word		*lightGridArray;
	int			numGridArrayElements;

	image_t		*volumetricLightMaps[MAXLIGHTMAPS];
	image_t		*entityGridAmbient;
	image_t		*entityGridDirected;
	image_t		*entityGridDirection;
	color4ub_t	entityGridStyleColors[MAX_LIGHT_STYLES];
	// froxel volumetric fog (r_volumetricFog 2): light grid split by the sun
	// direction, see R_BuildVolumetricLightGrid (tr_volumetric.cpp)
	image_t		*volumetricStaticGrid;	// baked light without the sun (rgb), sun fraction (a)
	image_t		*volumetricSunGrid;		// baked sun part
	image_t		*volumetricDirGrid;		// directed non-sun part (rgb), its luminance (a)
	image_t		*volumetricDirVecGrid;	// direction towards the light * luminance (rgb)
	vec3_t		volumetricSunRadiance;	// realtime sun radiance estimated from the sunlit cells
	qboolean	volumetricHasSunCells;
	float		particleLightReference;	// mean luminance of the valid light grid cells (r_particleLighting: gain 1 there)

	int			skyboxportal;
	int			numClusters;
	int			clusterBytes;
	const byte	*vis;			// may be passed in by CM_LoadMap to save space
	byte		*novis; // clusterBytes of 0xff (everything is visible)

	char		*entityString;
	char		*entityParsePoint;

	// silhouette POM (tr_pom_silhouette.cpp), r_pomSilhouette 1 only
	int			numPomShells;
	srfPomShell_t	*pomShells;
	image_t		*pomGroupsImage;		// RGBA32F buffer texture: group headers + boundary edges
	GLuint		pomGroupsBuffer;
	int			pomGroupsTexels;
} world_t;


/*
==============================================================================
MDV MODELS - meta format for vertex animation models like .md2, .md3, .mdc
==============================================================================
*/
typedef struct
{
	float           bounds[2][3];
	float           localOrigin[3];
	float           radius;
} mdvFrame_t;

typedef struct
{
	float           origin[3];
	float           axis[3][3];
} mdvTag_t;

typedef struct
{
	char            name[MAX_QPATH];	// tag name
} mdvTagName_t;

typedef struct
{
	vec3_t          xyz;
	vec3_t          normal;
	vec3_t          tangent;
	vec3_t          bitangent;
} mdvVertex_t;

typedef struct
{
	float           st[2];
} mdvSt_t;

typedef struct mdvSurface_s
{
	surfaceType_t   surfaceType;

	char            name[MAX_QPATH];	// polyset name

	int             numShaderIndexes;
	int				*shaderIndexes;

	int             numVerts;
	mdvVertex_t    *verts;
	mdvSt_t        *st;

	int             numIndexes;
	glIndex_t      *indexes;
	uint16_t        foliageSignals; // cached name vetoes and card hint
	vec3_t          foliageMins;
	vec3_t          foliageMaxs;
	vec3_t          foliageRoot;    // candidate base in local MD3 coordinates
	float           foliageXYRadius;

	struct mdvModel_s *model;
} mdvSurface_t;

typedef struct mdvModel_s
{
	uint16_t        foliageSignals; // cached model path evidence
	vec3_t          foliageMins;    // union of frame bounds
	vec3_t          foliageMaxs;
	int             numFrames;
	mdvFrame_t     *frames;

	int             numTags;
	mdvTag_t       *tags;
	mdvTagName_t   *tagNames;

	int             numSurfaces;
	mdvSurface_t   *surfaces;

	int             numVBOSurfaces;
	srfVBOMDVMesh_t  *vboSurfaces;

	int             numSkins;
} mdvModel_t;


//======================================================================

typedef enum {
	MOD_BAD,
	MOD_BRUSH,
	MOD_MESH,
	MOD_MDR,
	MOD_IQM,
/*
Ghoul2 Insert Start
*/
   	MOD_MDXM,
	MOD_MDXA
/*
Ghoul2 Insert End
*/
} modtype_t;

typedef struct mdxmVBOMesh_s
{
	surfaceType_t surfaceType;

	int indexOffset;
	int minIndex;
	int maxIndex;
	int numIndexes;
	int numVertexes;

	VBO_t *vbo;
	IBO_t *ibo;
} mdxmVBOMesh_t;

typedef struct mdxmVBOModel_s
{
	int numVBOMeshes;
	mdxmVBOMesh_t *vboMeshes;

	VBO_t *vbo;
	IBO_t *ibo;
} mdxmVBOModel_t;

typedef struct mdxmData_s
{
	mdxmHeader_t *header;

	// int numLODs; // available in header->numLODs
	mdxmVBOModel_t *vboModels;
} mdxmData_t;

typedef struct model_s {
	char		name[MAX_QPATH];
	modtype_t	type;
	int			index;		// model = tr.models[model->index]

	int			dataSize;	// just for listing purposes
	union
	{
		bmodel_t		*bmodel;			// type == MOD_BRUSH
		mdvModel_t		*mdv[MD3_MAX_LODS];	// type == MOD_MESH
		mdrHeader_t		*mdr;				// type == MOD_MDR
		iqmData_t		*iqm;				// type == MOD_IQM
		mdxmData_t		*glm;				// type == MOD_MDXM
		mdxaHeader_t	*gla;				// type == MOD_MDXA
	} data;

	int			 numLods;
} model_t;


#define	MAX_MOD_KNOWN	1024

void		R_ModelInit (void);

model_t		*R_GetModelByHandle( qhandle_t hModel );
int			R_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame,
					 float frac, const char *tagName );
void		R_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs );

void		R_Modellist_f (void);

//====================================================

#define	MAX_DRAWIMAGES			2048
#define	MAX_SKINS				1024


#define	MAX_DRAWSURFS			0x10000
#define	DRAWSURF_MASK			(MAX_DRAWSURFS-1)

#define MAX_INSTANCES			256

extern	int gl_filter_min, gl_filter_max;

/*
** performanceCounters_t
*/
typedef struct {
	int		c_sphere_cull_patch_in, c_sphere_cull_patch_clip, c_sphere_cull_patch_out;
	int		c_box_cull_patch_in, c_box_cull_patch_clip, c_box_cull_patch_out;
	int		c_sphere_cull_md3_in, c_sphere_cull_md3_clip, c_sphere_cull_md3_out;
	int		c_box_cull_md3_in, c_box_cull_md3_clip, c_box_cull_md3_out;

	int		c_leafs;
	int		c_dlightSurfaces;
	int		c_dlightSurfacesCulled;
} frontEndCounters_t;

#define	FOG_TABLE_SIZE		256
#define FUNCTABLE_SIZE		1024
#define FUNCTABLE_SIZE2		10
#define FUNCTABLE_MASK		(FUNCTABLE_SIZE-1)

struct vertexAttribute_t
{
	VBO_t *vbo;
	int index;
	int numComponents;
	GLboolean integerAttribute;
	GLenum type;
	GLboolean normalize;
	int stride;
	int offset;
	int stepRate;
};

#define MAX_UBO_BINDINGS (16)
struct bufferBinding_t
{
	GLuint buffer;
	size_t offset;
	size_t size;
};

// the renderer front end should never modify glstate_t
typedef struct glstate_s {
	int			currenttextures[MAX_TEXTURE_UNITS];
	int			currenttmu;
	int			texEnv[2];
	int			faceCulling;
	bool		blend;
	bool		screenAuxWrite;	// color mask of the screen-space attachments of renderFbo
	float		minDepth;
	float		maxDepth;
	ivec2_t		viewportOrigin;
	ivec2_t		viewportSize;
	uint32_t	glStateBits;
	uint32_t		vertexAttribsState;
	vertexAttribute_t currentVaoAttribs[ATTR_INDEX_MAX];
	uint32_t        vertexAttribsNewFrame;
	uint32_t        vertexAttribsOldFrame;
	float           vertexAttribsInterpolation;
	qboolean        vertexAnimation;
	qboolean		skeletalAnimation;
	qboolean		genShadows;
	shaderProgram_t *currentProgram;
	FBO_t          *currentFBO;
	VBO_t          *currentVBO;
	IBO_t          *currentIBO;
	bufferBinding_t currentXFBBO;
	GLuint			currentGlobalUBO;
	bufferBinding_t currentUBOs[MAX_UBO_BINDINGS];
	matrix_t        modelview;
	matrix_t        projection;
	matrix_t		modelviewProjection;
} glstate_t;

typedef enum {
	MI_NONE,
	MI_NVX,
	MI_ATI
} memInfo_t;

typedef enum {
	TCR_NONE = 0x0000,
	TCR_LATC = 0x0001,
	TCR_BPTC = 0x0002,
} textureCompressionRef_t;

typedef enum {
	IHV_UNKNOWN,

	IHV_NVIDIA,
	IHV_AMD,
	IHV_INTEL
} gpuIhv_t;

// We can't change glConfig_t without breaking DLL/vms compatibility, so
// store extensions we have here.
typedef struct {
	int glMajorVersion;
	int glMinorVersion;
	int glslMajorVersion;
	int glslMinorVersion;

	// Hardware capabilities and startup policy are separate. Feature code
	// uses R_HasModernFeatures(), then checks the limits it actually needs.
	uint32_t modernFeatures;
	qboolean modernPaths;
	int maxComputeWorkGroupCount[3];
	int maxComputeWorkGroupSize[3];
	int maxComputeWorkGroupInvocations;
	int maxComputeSharedMemorySize;
	int maxComputeShaderStorageBlocks;
	int maxShaderStorageBufferBindings;
	GLint64 maxShaderStorageBlockSize;
	int shaderStorageBufferOffsetAlignment;
	int maxImageUnits;
	int maxComputeImageUniforms;

	gpuIhv_t hardwareVendor;

	memInfo_t   memInfo;

	int maxRenderbufferSize;
	int maxColorAttachments;

	int textureCompression;
	int uniformBufferOffsetAlignment;
	int maxUniformBlockSize;
	int maxTextureBufferSize;	// texels, Forward+ buffer capacity
	int maxUniformBufferBindings;

	qboolean immutableTextures;
	qboolean immutableBuffers;

	qboolean debugContext;
	qboolean timerQuery;
	qboolean programBinary;		// GL_ARB_get_program_binary with a binary format, r_shaderProgramCache

	qboolean floatLightmap;

	qboolean annotateResources;
} glRefConfig_t;

enum modernFeature_t
{
	MODERN_COMPUTE = 1u << 0,
	MODERN_SSBO = 1u << 1,
	MODERN_IMAGE_LOAD_STORE = 1u << 2
};

qboolean R_HasModernFeatures(uint32_t requiredFeatures);
void R_PrintModernCapabilities();

enum
{
	TRI_BIN_0_19,
	TRI_BIN_20_49,
	TRI_BIN_50_99,
	TRI_BIN_100_299,
	TRI_BIN_300_599,
	TRI_BIN_600_999,
	TRI_BIN_1000_1499,
	TRI_BIN_1500_1999,
	TRI_BIN_2000_2999,
	TRI_BIN_3000_PLUS,

	NUM_TRI_BINS,
};

typedef struct {
	int		c_surfaces, c_shaders, c_vertexes, c_indexes, c_totalIndexes;
	int     c_surfBatches;
	float	c_overDraw;

	int		c_vboVertexBuffers;
	int		c_vboIndexBuffers;
	int		c_vboVertexes;
	int		c_vboIndexes;

	int     c_staticVboDraws;
	int     c_dynamicVboDraws;
	size_t  c_dynamicVboTotalSize;

	int     c_multidraws;
	int     c_multidrawsMerged;

	int     c_spriteDraws;
	int     c_spriteCards;

	int		c_dlightVertexes;
	int		c_dlightIndexes;

	int		c_flareAdds;
	int		c_flareTests;
	int		c_flareRenders;

	int     c_glslShaderBinds;
	int     c_genericDraws;
	int     c_lightallDraws;
	int     c_pomShellSurfaces;	// silhouette POM shells drawn (all passes)
	int     c_pomShellTriangles;
	int     c_pomFadeSurfaces;	// base surfaces drawn in the crossfade band
	int     c_fogDraws;
	int     c_dlightDraws;

	int		c_triangleCountBins[NUM_TRI_BINS];

	int		msec;			// total msec for backend run
} backEndCounters_t;

// all state modified by the back end is seperated
// from the front end state
typedef struct {
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	orientationr_t	ori;
	backEndCounters_t	pc;
	trRefEntity_t	*currentEntity;
	qboolean	skyRenderedThisView;	// flag for drawing sun
	uint32_t	skyNumber;

	qboolean	projection2D;	// if qtrue, drawstretchpic doesn't need to change modes
	float		color2D[4];
	trRefEntity_t	entity2D;		// currentEntity will point at this when doing 2D rendering
	trRefEntity_t	entityFlare;	// currentEntity will point at this when doing flare rendering

	FBO_t *last2DFBO;
	qboolean    colorMask[4];
	qboolean    framePostProcessed;
	qboolean    depthFill;
	qboolean    refractionFill;
	image_t    *screenAoImage;	// AO / contact shadow map lightall samples in this view (TB_SSAOMAP)
	qboolean    ssrView;		// this view gets SSR, see RB_ScreenSpaceBeginView
	qboolean    ssgiView;		// this view gets SSGI, see RB_ScreenSpaceBeginView
	qboolean    skinSSSView;	// this view gets the skin diffusion, see RB_ScreenSpaceBeginView
	int         skinSSSDraws;	// skin stages queued in this view (screenAux draws with skinScatter > 0)
	qboolean    screenAuxView;	// opaque lightall stages write the screen-space attachments
	qboolean    volumetricView;	// this view uses the froxel volume, see RB_VolumetricBeginView
	qboolean    volumetricComposited;	// the froxel fog composite of this view ran
} backEndState_t;

/*
** trGlobals_t
**
** Most renderer globals are defined here.
** backend functions should never modify any of these fields,
** but may read fields that aren't dynamically modified
** by the frontend.
*/
struct weatherSystem_t;
typedef struct trGlobals_s {
	qboolean				registered;		// cleared at shutdown, set at beginRegistration

	window_t				window;

	fileHandle_t			debugFile;
	int						numFramesToCapture;

	int						visIndex;
	int						visClusters[MAX_VISCOUNTS];
	int						visCounts[MAX_VISCOUNTS];	// incremented every time a new vis cluster is entered

	int						frameCount;		// incremented every frame
	int						sceneCount;		// incremented every scene
	int						viewCount;		// incremented every view (twice a scene if portaled)
											// and every R_MarkFragments call

	int						frameSceneNum;	// zeroed at RE_BeginFrame

	GLuint					globalVao;

	qboolean				worldMapLoaded;
	qboolean				worldInternalLightmapping; // qtrue indicates lightmap atlasing
	qboolean				worldDeluxeMapping;
	qboolean				worldInternalDeluxeMapping;
	vec2_t                  autoExposureMinMax;
	vec3_t                  toneMinAvgMaxLevel;
	world_t					*world;
	char					worldName[MAX_QPATH];

	const byte				*externalVisData;	// from RE_SetWorldVisData, shared with CM_Load

	image_t					*defaultImage;
	image_t					*scratchImage[32];
	image_t					*fogImage;
	image_t					*dlightImage;	// inverse-quare highlight for projective adding
	image_t					*flareImage;
	image_t					*whiteImage;			// full of 0xff
	image_t					*whiteImage3D;
	image_t					*blackImage3D;
	image_t					*identityLightImage;	// full of tr.identityLightByte

	image_t					*renderImage;
	image_t					*glowImage;
	image_t					*velocityImage;
	image_t					*temporalResolveImage;
	image_t					*historyImage;
	image_t					*glowImageScaled[6];
	image_t					*sunRaysImage;
	image_t					*renderDepthImage;
	image_t					*pshadowArrayImage;
	image_t					*textureScratchImage[2];
	image_t                 *quarterImage[2];
	image_t					*calcLevelsImage;
	image_t					*targetLevelsImage;
	image_t					*fixedLevelsImage;
	image_t					*identityLutImage;
	image_t					*colorGradingLutImage;	// NULL when no LUT is used
	char					colorGradingLutName[MAX_QPATH];
	char					mapColorGradingLut[MAX_QPATH];
	image_t					*sunShadowArrayImage;
	image_t					*pointShadowArrayImage;
	image_t					*lightCookieArray;	// tr_lightcookie.cpp, NULL until a cookie is registered
	image_t                 *screenSsaoImage;	// legacy SSAO: r = AO, g = 1
	image_t					*hdrDepthImage;
	image_t					*aoDepthImage;		// GTAO: linear view depth, AO_DEPTH_MIPS levels
	image_t					*gtaoImage[2];		// GTAO: r = visibility, gba = view normal
	image_t					*gtaoBentImage[2];	// GTAO: rg = octahedral view space bent normal
	image_t					*screenAoImage;		// final: r = AO, g = sun contact shadow, ba = octahedral world bent normal
	image_t                 *renderCubeImage;
	image_t                 *renderCubeDepthImage;
	image_t					*envBrdfImage;
	image_t					*ltcMatrixImage;	// LTC inverse matrix (tr_ltc_data.h)
	image_t					*ltcAmplitudeImage;	// LTC norm, fresnel, sphere form factor
	image_t					*probeAverageImage;
	image_t					*textureDepthImage;
	image_t					*weatherDepthImage;
	image_t					*weatherSurfaceImage;	// world geometry only, maps with weather brushes (r_rainSplashes)
	image_t					*smaaSearchImage;
	image_t					*smaaAreaImage;
	image_t					*smaaEdgeImage;
	image_t					*smaaBlendImage;
	image_t					*smaaResolveImage;
	image_t					*motionBlurImage;	// motion blur output (HDR)
	image_t					*rainLensImage;		// lens rain output (HDR)
	image_t					*rainLensFieldImage;	// lower resolution offset, mask, blur field
	image_t					*rainLensFilmImage;	// persistent wetness (R) and thin film (G), CPU updated
	image_t					*rainLensInstanceImage;	// lens drop / sheet instance records (RGBA32F)
	image_t					*rainLensMipImage;	// r_rainLensMipBlur: half resolution mipped scene copy
	image_t					*froxelInjectImage[2];	// froxel fog: injected + temporally filtered media (history ping-pong)
	image_t					*froxelDynamicImage;	// froxel fog: dynamic light in-scattering of this frame (no history)
	image_t					*froxelParticleLightImage;	// froxel fog: incident light of the sprite particles (r_particleLighting), no sigma / albedo
	image_t					*froxelIntegratedImage;	// froxel fog: integrated in-scattering (rgb), transmittance (a)
	image_t					*froxelCarryImage[2];	// froxel fog: integration state between slices
	image_t					*froxelTailImage;	// froxel fog: last slice radiance (rgb) and extinction (a)
	image_t					*froxelNoiseImage;
	image_t					*froxelExtinctionImage[2];	// froxel fog (r_volumetricFogRGBExtinction): injected sigma_t.rgb, history ping-pong with froxelInjectImage
	image_t					*froxelTransmittanceImage;	// froxel fog (r_volumetricFogRGBExtinction): integrated T.rgb
	image_t					*froxelCarryTImage[2];	// froxel fog (r_volumetricFogRGBExtinction): T.rgb between slices
	image_t					*froxelMediaImage;	// froxel fog: extinction of this frame, no history (r_volumetricSelfShadow light rays)	// froxel fog: tiling density noise, r = macro, g = detail (64^3, mips)
	// shared screen-space infrastructure (tr_screenspace.cpp)
	image_t					*screenNormalImage;	// rg = octahedral world normal, b = roughness, a = SSR receiver
	image_t					*screenHiZImage;	// closest linear view depth, SCREEN_HIZ_MIPS levels
	// screen-space reflections (tr_ssr.cpp)
	image_t					*ssrSpecularImage;	// rgb = sqrt(specular IBL weight)
	image_t					*ssrCubemapImage;	// rgb = cubemap specular added by lightall, a = view depth
	image_t					*ssrColorImage;		// opaque HDR scene, SSR_COLOR_MIPS levels
	image_t					*ssrTraceImage[2];	// trace resolution, xy = hit uv, z = hit depth, w = confidence (ssr_common.glsl), ping-pong: hit cache
	image_t					*ssrResolveImage;	// rgb = reflected radiance, a = confidence
	image_t					*ssrHistoryImage[2];
	image_t					*ssrHistoryGeomImage[2];	// x = view depth, yz = octahedral normal, w = roughness
	// screen-space GI (tr_ssgi.cpp)
	image_t					*ssgiAlbedoImage;	// rgb = sRGB diffuse albedo, a = receiver
	image_t					*ssgiRadianceImage;	// rgb = linear GI source radiance, a = view depth
	image_t					*ssgiSceneImage;	// copy of the opaque scene color (legacy scene / full scene source)
	image_t					*ssgiSourceImage;	// half resolution GI source radiance, SSGI_SOURCE_MIPS levels
	image_t					*ssgiTraceImage;	// rgb = GI (albedo free), a = confidence
	image_t					*ssgiHitImage;		// x = hit distance / max, y = hit fraction
	image_t					*ssgiHistoryImage[2];
	image_t					*ssgiHistoryGeomImage[2];	// x = view depth, yz = octahedral normal, w = accumulated length
	image_t					*ssgiDenoiseImage[2];
	// skin SSS (tr_skinsss.cpp)
	image_t					*skinDiffuseImage;	// rgb = scattering skin diffuse (scene space), a = view depth (0 = not skin)
	image_t					*skinBlurImage[2];	// horizontal, vertical blur of skinDiffuseImage

	FBO_t					*renderFbo;
	FBO_t					*depthVelocityFbo;
	FBO_t					*glowFboScaled[6];
	FBO_t					*msaaResolveFbo;
	FBO_t					*msaaResolveVelocityFbo;
	FBO_t					*sunRaysFbo;
	FBO_t					*depthFbo;
	FBO_t					*pshadowFbos[MAX_DRAWN_PSHADOWS];
	FBO_t					*shadowCubeFbo[MAX_DLIGHTS*6];
	FBO_t					*textureScratchFbo[2];
	FBO_t                   *quarterFbo[2];
	FBO_t					*calcLevelsFbo;
	FBO_t					*targetLevelsFbo;
	FBO_t					*sunShadowFbo[3];
	FBO_t					*screenSsaoFbo;
	FBO_t					*hdrDepthFbo;
	FBO_t					*aoDepthFbo[AO_DEPTH_MIPS];
	FBO_t					*gtaoFbo[2];
	FBO_t					*screenAoFbo;
	FBO_t                   *renderCubeFbo[6];
	FBO_t                   *filterCubeFbo;
	FBO_t					*weatherDepthFbo;
	FBO_t					*weatherSurfaceFbo;
	FBO_t					*smaaEdgeFbo;
	FBO_t					*smaaBlendFbo;
	FBO_t					*smaaResolveFbo;
	FBO_t					*temporalResolveFbo;
	FBO_t					*historyFbo;
	FBO_t					*motionBlurFbo;
	FBO_t					*rainLensFbo;
	FBO_t					*rainLensFieldFbo;
	FBO_t					*rainLensMipFbo;
	FBO_t					*froxelMediaFbo;		// froxelMediaImage, layered (r_volumetricSelfShadow)
	FBO_t					*froxelInjectFbo;		// layers attached per slice
	FBO_t					*froxelIntegrateFbo;	// layers attached per slice
	FBO_t					*froxelCompositeFbo;	// color + glow of renderFbo, no depth
	FBO_t					*ssrColorFbo[SSR_COLOR_MIPS];
	FBO_t					*ssrTraceFbo[2];
	FBO_t					*ssrResolveFbo;
	FBO_t					*ssrHistoryFbo[2];
	FBO_t					*screenHiZFbo[SCREEN_HIZ_MIPS];
	FBO_t					*screenCompositeFbo;	// renderFbo color 0 only
	FBO_t					*ssgiSceneFbo;
	FBO_t					*ssgiSourceFbo;
	FBO_t					*ssgiTraceFbo;		// trace + hit
	FBO_t					*ssgiHistoryFbo[2];	// history + geometry
	FBO_t					*ssgiDenoiseFbo[2];
	// skin SSS (tr_skinsss.cpp)
	FBO_t					*skinBlurFbo[2];	// 0 = horizontal, 1 = vertical

	shader_t				*defaultShader;
	shader_t				*shadowShader;
	shader_t				*distortionShader;
	shader_t				*projectionShadowShader;
	shader_t				*weatherInternalShader;

	shader_t				*flareShader;
	shader_t				*sunShader;
	shader_t				*sunFlareShader;

	shader_t				*volumetricFogCapShader;

	int						numLightmaps;
	int						lightmapSize;
	image_t					**lightmaps;
	image_t					**deluxemaps;

	qboolean				hdrLighting;
	qboolean				linearLight;		// the scene is lit in linear light: HDR lightmaps or r_linearLighting
	qboolean				forcedLinearLight;	// r_linearLighting on a map without HDR lightmaps, its sRGB lighting data gets decoded

	vec2i_t					lightmapAtlasSize;
	vec2i_t					lightmapsPerAtlasSide;

	int                     numCubemaps;
	cubemap_t               *cubemaps;

	trRefEntity_t			worldEntity;		// point currentEntity at this when rendering world
	model_t					*currentModel;

	weatherSystem_t			*weatherSystem;

	// GPU shader programs
	// Make sure splashScreenShader is the first shaderProgram_t or edit
	// R_ClearTr to make sure shaderPrograms are cached correctly
	shaderProgram_t splashScreenShader;
	shaderProgram_t genericShader[GENERICDEF_COUNT];
	shaderProgram_t refractionShader[REFRACTIONDEF_COUNT];
	shaderProgram_t textureColorShader[TEXCOLORDEF_COUNT];
	shaderProgram_t fogShader[FOGDEF_COUNT];
	shaderProgram_t velocityShader[VELOCITYDEF_COUNT];
	shaderProgram_t lightallShader[LIGHTDEF_COUNT];
	// silhouette POM (r_pomSilhouette 1 at load only), tr_pom_silhouette.cpp
	shaderProgram_t lightallSilhouetteShader[POMSDEF_LIGHTALL_COUNT];
	shaderProgram_t pomSilhouetteDepthShader[POMSDEF_DEPTH_COUNT];
	shaderProgram_t fogSilhouetteShader[2];	// without, with FOGDEF_USE_FALLBACK_GLOBAL_FOG
	shaderProgram_t pshadowShader;
	shaderProgram_t volumeShadowShader;
	shaderProgram_t down4xShader;
	shaderProgram_t bokehShader;
	shaderProgram_t tonemapShader[2];
	shaderProgram_t calclevels4xShader[2];
	shaderProgram_t ssaoShader;
	shaderProgram_t highpassShader;
	shaderProgram_t depthBlurShader[2];
	shaderProgram_t prefilterEnvMapShader;
	shaderProgram_t probeAverageShader;
	shaderProgram_t diffuseIrradianceShader;
	shaderProgram_t gaussianBlurShader[2];
	shaderProgram_t dglowDownsample;
	shaderProgram_t dglowUpsample;
	shaderProgram_t bloomPrefilter;
	shaderProgram_t spriteShader[SSDEF_COUNT];
	shaderProgram_t weatherUpdateShader;
	shaderProgram_t weatherShader;
	shaderProgram_t weatherSplashShader;
	shaderProgram_t weatherUpdateSplashShader;	// weatherUpdate + impact state (r_rainSplashes)
	shaderProgram_t smaaEdgeShader;
	shaderProgram_t smaaBlendShader;
	shaderProgram_t smaaResolveShader;
	shaderProgram_t smaaTemporalResolveShader;
	shaderProgram_t gtaoDepthShader[2];		// 0 = linearize, 1 = downsample mip
	shaderProgram_t gtaoShader[2];			// 1 = + bent normal (BENT_NORMAL)
	shaderProgram_t gtaoDenoiseShader[2];	// 1 = + bent normal (BENT_NORMAL)
	shaderProgram_t aoCompositeShader;
	shaderProgram_t aoDebugShader;
	shaderProgram_t motionBlurShader[MOTIONBLURDEF_COUNT];
	shaderProgram_t rainLensShader[RAINLENSDEF_COUNT];
	shaderProgram_t rainLensCompositeShader[RAINLENSCOMPOSITE_COUNT];
	shaderProgram_t volumetricInjectShader;
	shaderProgram_t volumetricIntegrateShader;
	shaderProgram_t volumetricInjectComputeShader;
	shaderProgram_t volumetricMediaComputeShader;	// r_volumetricSelfShadow media pass
	shaderProgram_t volumetricIntegrateComputeShader;
	shaderProgram_t volumetricCompositeShader;
	shaderProgram_t volumetricDebugShader;
	shaderProgram_t foliageFieldShader;			// r_foliageBendField update pass
	shaderProgram_t foliageFieldDebugShader;	// r_foliageBendFieldDebug 1 overlay
	shaderProgram_t ssrDownsampleShader[2];	// 0: premultiplied mips, 1: first level (masks the view model)
	shaderProgram_t ssrTraceShader[SSRDEF_COUNT];
	shaderProgram_t ssrResolveShader;
	shaderProgram_t ssrTemporalShader;
	shaderProgram_t ssrCompositeShader;
	shaderProgram_t ssrDebugShader;
	shaderProgram_t screenHiZShader[2];		// 0 = linearize, 1 = downsample mip (tr_screenspace.cpp)
	shaderProgram_t ssgiSourceShader;
	shaderProgram_t ssgiTraceShader[SSGIDEF_COUNT];
	shaderProgram_t ssgiTemporalShader;
	shaderProgram_t ssgiDenoiseShader;
	shaderProgram_t ssgiCompositeShader;
	shaderProgram_t ssgiDebugShader;
	shaderProgram_t skinSSSShader[SKINSSSDEF_COUNT];	// tr_skinsss.cpp
	// Make sure staticUbo is right behind all shaderProgram_t or edit 
	// R_ClearTr to make sure shaderPrograms are cached correctly

	GLuint staticUbo;
	GLuint spriteUbos[MAX_SUB_BSP + 1];
	GLuint shaderInstanceUbo;
	size_t shaderInstanceUboWriteOffset;
	size_t entity2DUboOffset;
	size_t camera2DUboOffset;
	size_t entityFlareUboOffset;
	size_t defaultLightsUboOffset;
	size_t defaultSceneUboOffset;
	size_t defaultFogsUboOffset;
	size_t defaultShaderInstanceUboOffset;

	long cameraUboOffsets[3 + MAX_DLIGHTS * 6 + 3 + MAX_DRAWN_PSHADOWS];
	long sceneUboOffset;
	long temporalInfoUboOffset;
	long lightsUboOffset;
	long fogsUboOffset;
	long volumetricFogUboOffset;
	long volParticlesUboOffset;
	long foliageInteractionUboOffset;
	long skyEntityUboOffset;
	long entityUboOffsets[REFENTITYNUM_WORLD + 1];
	long previousEntityUboOffsets[REFENTITYNUM_WORLD + 1];
	long animationBoneUboOffset;
	long previousAnimationBoneUboOffset;

	// false on the first frame after a map load, camera cut or teleport: the
	// previous frame data must not be used (tr_motionblur.cpp). Always true
	// when motion blur is off.
	qboolean temporalHistoryValid;

	// -----------------------------------------

	viewParms_t				viewParms;
	viewParms_t				cachedViewParms[3 + MAX_DLIGHTS * 6 + 3 + MAX_DRAWN_PSHADOWS];
	int						numCachedViewParms;
	qboolean				portalRenderedThisFrame;

	viewParms_t				skyPortalParms;
	byte					skyPortalAreaMask[MAX_MAP_AREA_BYTES];
	int						skyPortalEntities;

	float					identityLight;		// 1.0 / ( 1 << overbrightBits )
	int						identityLightByte;	// identityLight * 255
	int						overbrightBits;		// r_overbrightBits->integer, but set to 0 if no hw gamma

	orientationr_t			ori;					// for current entity

	trRefdef_t				refdef;

	int						viewCluster;

	float                   mapLightScale;
	float                   sunShadowScale;

	qboolean                sunShadows;
	qboolean                sunParsed;		// a sky shader of this level set the sun (sun, q3map_sun, ...)
	vec3_t					sunLight;			// from the sky shader for this level
	vec3_t					sunDirection;

	float					volumetricFogScale;

	frontEndCounters_t		pc;
	int						frontEndMsec;		// not in pc due to clearing issue

	int						numTimedBlocks;

	//
	// put large tables at the end, so most elements will be
	// within the +/32K indexed range on risc processors
	//
	model_t					*models[MAX_MOD_KNOWN];
	int						numModels;

	world_t					*bspModels[MAX_SUB_BSP];
	int						numBspModels;

	int						numImages;
	image_t					*images;
	image_t					*imagesFreeList;

	int						numFBOs;
	FBO_t					*fbos[MAX_FBOS];

	int						numVBOs;
	VBO_t					*vbos[MAX_VBOS];

	int						numIBOs;
	IBO_t					*ibos[MAX_IBOS];



	// shader indexes from other modules will be looked up in tr.shaders[]
	// shader indexes from drawsurfs will be looked up in sortedShaders[]
	// lower indexed sortedShaders must be rendered first (opaque surfaces before translucent)
	int						numShaders;
	shader_t				*shaders[MAX_SHADERS];
	shader_t				*sortedShaders[MAX_SHADERS];

	int						numSkins;
	skin_t					*skins[MAX_SKINS];

	GLuint					sunFlareQuery[2];
	int						sunFlareQueryIndex;
	qboolean				sunFlareQueryActive[2];

	float					sinTable[FUNCTABLE_SIZE];
	float					squareTable[FUNCTABLE_SIZE];
	float					triangleTable[FUNCTABLE_SIZE];
	float					sawToothTable[FUNCTABLE_SIZE];
	float					inverseSawToothTable[FUNCTABLE_SIZE];
	float					fogTable[FOG_TABLE_SIZE];

	float					rangedFog;
	float					distanceCull, distanceCullSquared; //rwwRMG - added

	// Specific to Jedi Academy
	int						numBSPModels;
	int						currentLevel;
} trGlobals_t;

struct glconfigExt_t
{
	glconfig_t *glConfig;

	qboolean textureFilterAnisotropicAvailable;
	qboolean doGammaCorrectionWithShaders;
	qboolean doStencilShadowsInOneDrawcall;
	const char *originalExtensionString;
};

extern backEndState_t	backEnd;
extern trGlobals_t	tr;
extern glstate_t	glState;		// outside of TR since it shouldn't be cleared during ref re-init
extern glconfigExt_t glConfigExt;
extern glRefConfig_t glRefConfig;
extern window_t		window;

//
// cvars
//
extern cvar_t	*r_railWidth;
extern cvar_t	*r_railCoreWidth;
extern cvar_t	*r_railSegmentLength;

extern cvar_t	*r_ignore;				// used for debugging anything
extern cvar_t	*r_verbose;				// used for verbose debug spew

extern cvar_t	*r_znear;				// near Z clip plane
extern cvar_t	*r_zproj;				// z distance of projection plane
extern cvar_t	*r_stereoSeparation;			// separation of cameras for stereo rendering

extern cvar_t	*r_measureOverdraw;		// enables stencil buffer overdraw measurement

extern cvar_t	*r_lodbias;				// push/pull LOD transitions
extern cvar_t	*r_lodscale;

extern cvar_t	*r_inGameVideo;				// controls whether in game video should be draw
extern cvar_t	*r_fastsky;				// controls whether sky should be cleared or drawn
extern cvar_t	*r_drawSun;				// controls drawing of sun quad
extern cvar_t	*r_dynamiclight;		// dynamic lights enabled/disabled

extern	cvar_t	*r_norefresh;			// bypasses the ref rendering
extern	cvar_t	*r_drawentities;		// disable/enable entity rendering
extern	cvar_t	*r_drawworld;			// disable/enable world rendering
extern	cvar_t	*r_speeds;				// various levels of information display
extern  cvar_t	*r_detailTextures;		// enables/disables detail texturing stages
extern	cvar_t	*r_novis;				// disable/enable usage of PVS
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_facePlaneCull;		// enables culling of planar surfaces with back side test
extern	cvar_t	*r_nocurves;
extern	cvar_t	*r_showcluster;

extern cvar_t	*r_gamma;

extern  cvar_t  *r_ext_draw_range_elements;
extern  cvar_t  *r_ext_multi_draw_arrays;
extern  cvar_t  *r_ext_framebuffer_object;
extern  cvar_t  *r_ext_texture_float;
extern  cvar_t  *r_arb_half_float_pixel;
extern  cvar_t  *r_ext_framebuffer_multisample;
extern  cvar_t  *r_arb_seamless_cube_map;
extern  cvar_t  *r_arb_vertex_type_2_10_10_10_rev;
extern	cvar_t	*r_arb_buffer_storage;

extern	cvar_t	*r_nobind;						// turns off binding to appropriate textures
extern	cvar_t	*r_singleShader;				// make most world faces use default shader
extern	cvar_t	*r_roundImagesDown;
extern	cvar_t	*r_colorMipLevels;				// development aid to see texture mip usage
extern	cvar_t	*r_picmip;						// controls picmip values
extern	cvar_t	*r_finish;
extern	cvar_t	*r_textureMode;
extern	cvar_t	*r_offsetFactor;
extern	cvar_t	*r_offsetUnits;

extern	cvar_t	*r_fullbright;					// avoid lightmap pass
extern	cvar_t	*r_lightmap;					// render lightmaps only
extern	cvar_t	*r_vertexLight;					// vertex lighting mode for better performance
extern	cvar_t	*r_uiFullScreen;				// ui is running fullscreen

extern	cvar_t	*r_logFile;						// number of frames to emit GL logs
extern	cvar_t	*r_showtris;					// enables wireframe rendering of the world
extern	cvar_t	*r_showsky;						// forces sky in front of all surfaces
extern	cvar_t	*r_shownormals;					// draws wireframe normals
extern	cvar_t	*r_clear;						// force screen clear every frame

extern	cvar_t	*r_shadows;						// controls shadows: 0 = none, 1 = blur, 2 = stencil, 3 = black planar projection
extern	cvar_t	*r_flares;						// light flares

extern	cvar_t	*r_intensity;

extern	cvar_t	*r_lockpvs;
extern	cvar_t	*r_noportals;
extern	cvar_t	*r_portalOnly;

extern	cvar_t	*r_subdivisions;
extern	cvar_t	*r_lodCurveError;
extern	cvar_t	*r_skipBackEnd;

extern	cvar_t	*r_anaglyphMode;

extern  cvar_t  *r_mergeMultidraws;
extern  cvar_t  *r_mergeLeafSurfaces;

extern	cvar_t	*r_externalGLSL;

extern  cvar_t  *r_hdr;
extern  cvar_t  *r_floatLightmap;

extern  cvar_t  *r_toneMap;
extern  cvar_t  *r_forceToneMap;
extern  cvar_t  *r_forceToneMapMin;
extern  cvar_t  *r_forceToneMapAvg;
extern  cvar_t  *r_forceToneMapMax;

extern  cvar_t  *r_autoExposure;
extern  cvar_t  *r_forceAutoExposure;
extern  cvar_t  *r_forceAutoExposureMin;
extern  cvar_t  *r_forceAutoExposureMax;

extern  cvar_t  *r_cameraExposure;

extern  cvar_t  *r_depthPrepass;
extern  cvar_t  *r_ssao;

extern  cvar_t  *r_normalMapping;
extern  cvar_t  *r_specularMapping;
extern  cvar_t  *r_deluxeMapping;
extern  cvar_t  *r_parallaxMapping;
extern  cvar_t  *r_forceParallaxBias;
extern  cvar_t  *r_cubeMapping;
extern  cvar_t  *r_cubeMappingBounces;
extern  cvar_t  *r_baseNormalX;
extern  cvar_t  *r_baseNormalY;
extern  cvar_t  *r_baseParallax;
extern  cvar_t  *r_baseSpecular;
extern  cvar_t  *r_dlightMode;
extern  cvar_t  *r_pshadowDist;
extern  cvar_t  *r_recalcMD3Normals;
extern  cvar_t  *r_imageUpsample;
extern  cvar_t  *r_imageUpsampleMaxSize;
extern  cvar_t  *r_imageUpsampleType;
extern  cvar_t  *r_genNormalMaps;
extern  cvar_t  *r_forceSun;
extern  cvar_t  *r_forceSunMapLightScale;
extern  cvar_t  *r_forceSunLightScale;
extern  cvar_t  *r_forceSunAmbientScale;
extern  cvar_t  *r_sunlightMode;
extern  cvar_t  *r_drawSunRays;
extern  cvar_t  *r_sunShadows;
extern  cvar_t  *r_shadowFilter;
extern  cvar_t  *r_shadowMapSize;
extern  cvar_t  *r_shadowCascadeZNear;
extern  cvar_t  *r_shadowCascadeZFar;
extern  cvar_t  *r_shadowCascadeZBias;
extern  cvar_t  *r_sunShadowMode;
extern  cvar_t  *r_sunShadowAlphaCasters;
extern  cvar_t  *r_shadowCascadeBlend;
extern  cvar_t  *r_shadowDepthBias;
extern  cvar_t  *r_shadowNormalBias;
extern  cvar_t  *r_shadowSlopeBias;
extern  cvar_t  *r_shadowReceiverBiasClamp;
extern  cvar_t  *r_shadowPCSS;
extern  cvar_t  *r_shadowPCSSQuality;
extern  cvar_t  *r_shadowSunAngularDiameter;
extern  cvar_t  *r_shadowPCSSMaxPenumbra;
extern  cvar_t  *r_shadowDebug;

extern	cvar_t	*r_greyscale;

extern	cvar_t	*r_ignoreGLErrors;

extern	cvar_t	*r_overBrightBits;
extern	cvar_t	*r_mapOverBrightBits;

extern	cvar_t	*r_debugSurface;
extern	cvar_t	*r_simpleMipMaps;

extern	cvar_t	*r_showImages;
extern	cvar_t	*r_debugSort;

extern	cvar_t	*r_printShaders;

extern cvar_t	*r_marksOnTriangleMeshes;

extern cvar_t	*r_dynamicGlow;
extern cvar_t	*r_dynamicGlowPasses;
extern cvar_t	*r_dynamicGlowDelta;
extern cvar_t	*r_dynamicGlowIntensity;
extern cvar_t	*r_dynamicGlowSoft;
extern cvar_t	*r_dynamicGlowWidth;
extern cvar_t	*r_dynamicGlowHeight;
extern cvar_t	*r_dynamicGlowBloom;
extern cvar_t	*r_bloom;
extern cvar_t	*r_bloomIntensity;
extern cvar_t	*r_bloomThreshold;
extern cvar_t	*r_bloomKnee;
extern cvar_t	*r_bloomScatter;
extern cvar_t	*r_bloomSceneIntensity;

extern cvar_t	*r_debugContext;
extern cvar_t *r_gl43;
extern cvar_t	*r_debugWeather;
extern cvar_t	*r_weatherCull;
extern cvar_t	*r_weatherDebugChunks;
extern cvar_t	*r_rainStreaks;
extern cvar_t	*r_rainStreakWidth;
extern cvar_t	*r_rainStreakLength;
extern cvar_t	*r_rainStreakOpacity;
extern cvar_t	*r_rainStreakLighting;
extern cvar_t	*r_rainStreakDebug;
extern cvar_t	*r_rainSplashes;
extern cvar_t	*r_rainSplashSize;
extern cvar_t	*r_rainSplashLifetime;
extern cvar_t	*r_rainSplashOpacity;
extern cvar_t	*r_rainSplashDebug;

//====================================================================

struct packedVertex_t
{
	vec3_t position;
	uint32_t normal;
	uint32_t tangent;
	vec2_t texcoords[1 + MAXLIGHTMAPS];
	vec4_t colors[MAXLIGHTMAPS];
	uint32_t lightDirection;
};

struct packedTangentSpace_t
{
	vec4_t tangentAndSign;
};

void R_GenerateDrawSurfs( viewParms_t *viewParms, trRefdef_t *refdef );
void R_SetupViewParmsForOrthoRendering(
	int viewportWidth,
	int viewportHeight,
	FBO_t *fbo,
	viewParmFlags_t viewParmsFlags,
	const orientationr_t& orientation,
	const vec3_t viewBounds[2]);
void R_SortAndSubmitDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs );

void R_SwapBuffers( int );

void R_RenderView( viewParms_t *parms );
void R_RenderDlightCubemaps(const refdef_t *fd);
void R_SetupPshadowMaps(const refdef_t *fd);
void R_RenderCubemapSide( int cubemapIndex, int cubemapSide, bool bounce);
void R_GatherFrameViews(trRefdef_t *refdef);

void R_AddMD3Surfaces( trRefEntity_t *e, int entityNum );
void R_AddPolygonSurfaces( const trRefdef_t *refdef );

void R_DecomposeSort( uint32_t sort, int *entityNum, shader_t **shader, int *cubemap, int *postRender );
uint32_t R_CreateSortKey(int entityNum, int sortedShaderIndex, int cubemapIndex, int postRender);
void R_AddDrawSurf( surfaceType_t *surface, int entityNum, shader_t *shader,
				   int fogIndex, int dlightMap, int postRender, int cubemap,
				   foliageResult_t foliage = {} );
bool R_IsPostRenderEntity ( const trRefEntity_t *refEntity );

void R_CalcMikkTSpaceBSPSurface(int numSurfaces, packedVertex_t *vertices, glIndex_t *indices);
void R_CalcMikkTSpaceMD3Surface(int numSurfaces, mdvVertex_t *verts, uint32_t *tangents, mdvSt_t *texcoords, glIndex_t *indices);
void R_CalcMikkTSpaceGlmSurface(int numSurfaces, mdxmVertex_t *vertices, mdxmVertexTexCoord_t *textureCoordinates, uint32_t *tangents, glIndex_t *indices);

void R_CalcTexDirs(vec3_t sdir, vec3_t tdir, const vec3_t v1, const vec3_t v2,
					const vec3_t v3, const vec2_t w1, const vec2_t w2, const vec2_t w3);
void R_CalcTbnFromNormalAndTexDirs(vec3_t tangent, vec3_t bitangent, vec3_t normal, vec3_t sdir, vec3_t tdir);
qboolean R_CalcTangentVectors(srfVert_t * dv[3]);

#define	CULL_IN		0		// completely unclipped
#define	CULL_CLIP	1		// clipped by one or more planes
#define	CULL_OUT	2		// completely outside the clipping planes
void R_LocalNormalToWorld (const vec3_t local, vec3_t world);
void R_LocalPointToWorld (const vec3_t local, vec3_t world);
int R_CullBox (vec3_t bounds[2]);
int R_CullBoxView(vec3_t bounds[2], viewParms_t *viewParms);
int R_CullLocalBox (vec3_t bounds[2]);
int R_CullPointAndRadiusEx( const vec3_t origin, float radius, const cplane_t* frustum, int numPlanes );
int R_CullPointAndRadius( const vec3_t origin, float radius );
int R_CullLocalPointAndRadius( const vec3_t origin, float radius );

void R_SetupProjection(viewParms_t *dest, float zProj, float zFar, qboolean computeFrustum);
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms, orientationr_t *ori );
void R_BindAnimatedImageToTMU( textureBundle_t *bundle, int tmu );

/*
** GL wrapper/helper functions
*/
void	GL_Bind( image_t *image );
void	GL_BindToTMU( image_t *image, int tmu );
void	GL_SetDefaultState (void);
void	GL_SelectTexture( int unit );
void	GL_TextureMode( const char *string );
void	GL_CheckErrs( const char *file, int line );
#define GL_CheckErrors(...) GL_CheckErrs(__FILE__, __LINE__)
void	GL_State( uint32_t stateVector );
void    GL_SetProjectionMatrix(matrix_t matrix);
void    GL_SetModelviewMatrix(matrix_t matrix);
void	GL_SetViewportAndScissor(int viewportX, int viewportY, int viewportWidth, int viewportHeight);
void	GL_Cull( int cullType );
void	GL_DepthRange( float min, float max );
void	GL_VertexAttribPointers(size_t numAttributes,
								vertexAttribute_t *attributes);
void	GL_DrawIndexed(GLenum primitiveType, int numIndices, GLenum indexType,
						size_t offset, int numInstances, int baseVertex);
void	GL_MultiDrawIndexed(GLenum primitiveType, int *numIndices,
							glIndex_t **offsets, int numDraws);
void	GL_Draw( GLenum primitiveType, int firstVertex, int numVertices, int numInstances );

#define LERP( a, b, w ) ( ( a ) * ( 1.0f - ( w ) ) + ( b ) * ( w ) )
#define LUMA( red, green, blue ) ( 0.2126f * ( red ) + 0.7152f * ( green ) + 0.0722f * ( blue ) )

extern glconfig_t  glConfig;
extern glconfigExt_t	glConfigExt;

void	RE_StretchRaw (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty);
void	RE_UploadCinematic (int cols, int rows, const byte *data, int client, qboolean dirty);
void	RE_SetRangedFog ( float range );
#ifdef REND2_SP
byte	*RB_ReadPixels(int x, int y, int width, int height, size_t *offset, int *padlen);
void	RE_GetScreenShot(byte *data, int w, int h);
byte*	RE_TempRawImage_ReadFromFile(const char *psLocalFilename, int *piWidth, int *piHeight, byte *pbReSampleBuffer, qboolean qbVertFlip);
void	RE_TempRawImage_CleanUp();
#endif

void		RE_BeginRegistration( glconfig_t *glconfig );
void		RE_LoadWorldMap( const char *mapname );
void		RE_SetWorldVisData( const byte *vis );
qhandle_t	RE_RegisterServerModel( const char *name );
qhandle_t	RE_RegisterModel( const char *name );
qhandle_t	RE_RegisterServerSkin( const char *name );
qhandle_t	RE_RegisterSkin( const char *name );
void		RE_Shutdown(qboolean destroyWindow, qboolean restarting);
world_t		*R_LoadBSP(const char *name, int *bspIndex = nullptr);

#ifdef REND2_SP
int			RE_GetAnimationCFG(const char* psCFGFilename, char* psDest, int iDestSize);
#endif

qboolean	R_GetEntityToken( char *buffer, int size );

model_t		*R_AllocModel( void );

void    	R_Init( void );
void		R_UpdateSubImage( image_t *image, byte *pic, int x, int y, int width, int height );

void		R_SetColorMappings( void );
void		R_GammaCorrect( byte *buffer, int bufSize );

void	R_ImageList_f( void );
void	R_SkinList_f( void );
void	R_FontList_f( void );
// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=516
const void *RB_TakeScreenshotCmd( const void *data );

void R_SaveScreenshot(struct screenshotReadback_t *screenshotReadback);

void	R_ScreenShotTGA_f( void );
void	R_ScreenShotPNG_f( void );
void	R_ScreenShotJPEG_f( void );

void	R_InitFogTable( void );
float	R_FogFactor( float s, float t );
void	R_InitImagesPool();
void	R_InitImages( void );
void	R_LoadHDRImage(const char *filename, byte **data, int *width, int *height);
void	R_DeleteTextures( void );
int		R_SumOfUsedImages( void );
void	R_InitSkins( void );
skin_t	*R_GetSkinByHandle( qhandle_t hSkin );

int R_ComputeLOD( trRefEntity_t *ent );

const void *RB_TakeVideoFrameCmd( const void *data );
void RE_HunkClearCrap(void);

//
// tr_shader.c
//
extern const int lightmapsNone[MAXLIGHTMAPS];
extern const int lightmaps2d[MAXLIGHTMAPS];
extern const int lightmapsVertex[MAXLIGHTMAPS];
extern const int lightmapsFullBright[MAXLIGHTMAPS];
extern const byte stylesDefault[MAXLIGHTMAPS];

shader_t	*R_FindShader( const char *name, const int *lightmapIndexes, const byte *styles, qboolean mipRawImage );
shader_t	*R_GetShaderByHandle( qhandle_t hShader );
shader_t *R_FindShaderByName( const char *name );
void		R_InitShaders( qboolean server );
void		R_ShaderList_f( void );
void    R_RemapShader(const char *oldShader, const char *newShader, const char *timeOffset);
shader_t *R_CreateShaderFromTextureBundle(
		const char *name,
		const textureBundle_t *bundle,
		uint32_t stateBits);

/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

QINLINE void GLimp_LogComment( char *comment ) {}
void GLimp_InitExtensions();
void GLimp_InitCoreFunctions();
void GLimp_ConfigureContext(windowDesc_t *desc);
void GLimp_InitModernFunctions();

/*
====================================================================

TESSELATOR/SHADER DECLARATIONS

====================================================================
*/

typedef struct stageVars
{
	color4ub_t	colors[SHADER_MAX_VERTEXES];
	vec2_t		texcoords[NUM_TEXTURE_BUNDLES][SHADER_MAX_VERTEXES];
} stageVars_t;

#define MAX_MULTIDRAW_PRIMITIVES	16384

const int NUM_TESS_TEXCOORDS = 1 + MAXLIGHTMAPS;
struct shaderCommands_s
{
	glIndex_t	indexes[SHADER_MAX_INDEXES] QALIGN(16);
	vec4_t		xyz[SHADER_MAX_VERTEXES] QALIGN(16);
	uint32_t	normal[SHADER_MAX_VERTEXES] QALIGN(16);
	uint32_t	tangent[SHADER_MAX_VERTEXES] QALIGN(16);
	vec2_t		texCoords[SHADER_MAX_VERTEXES][NUM_TESS_TEXCOORDS] QALIGN(16);
	vec4_t		vertexColors[SHADER_MAX_VERTEXES] QALIGN(16);
	uint32_t    lightdir[SHADER_MAX_VERTEXES] QALIGN(16);
	//int			vertexDlightBits[SHADER_MAX_VERTEXES] QALIGN(16);

	IBO_t		*externalIBO;
	qboolean    useInternalVBO;

	stageVars_t	svars QALIGN(16);

	//color4ub_t	constantColor255[SHADER_MAX_VERTEXES] QALIGN(16);

	shader_t	*shader;
	uint8_t foliageDebugClass; // color only when r_autoFoliageDebug is enabled
	uint8_t foliageMotion;     // FOLIAGE_LEAF (r_leafFlutter), FOLIAGE_PLANT (root bend) or FOLIAGE_NONE
	float		shaderTime;
	int			fogNum;
	int         cubemapIndex;
	bool		entityMergable;
#ifdef REND2_SP_GORE
	bool		scale;		// uses texCoords[input->firstIndex] for storage
	bool		fade;		// uses svars.colors[input->firstIndex] for storage
#endif
	int			dlightBits;	// or together of all vertexDlightBits
	int         pshadowBits;

	int			firstIndex;
	int			numIndexes;
	int			numVertexes;
	int			pomMode;		// POM_MODE_*: silhouette POM shells / crossfade base
	image_t		*pomGroupsImage;	// silhouette POM group footprints of the batch
	glIndex_t   minIndex;
	glIndex_t   maxIndex;

	int         multiDrawPrimitives;
	GLsizei     multiDrawNumIndexes[MAX_MULTIDRAW_PRIMITIVES];
	glIndex_t  *multiDrawFirstIndex[MAX_MULTIDRAW_PRIMITIVES];
	glIndex_t  *multiDrawLastIndex[MAX_MULTIDRAW_PRIMITIVES];
	glIndex_t   multiDrawMinIndex[MAX_MULTIDRAW_PRIMITIVES];
	glIndex_t   multiDrawMaxIndex[MAX_MULTIDRAW_PRIMITIVES];

	// info extracted from current shader
	int			numPasses;
	void		(*currentStageIteratorFunc)( void );
	shaderStage_t	**xstages;
};

struct drawState_t
{
	uint32_t stateBits;
};

#ifdef _WIN32
	typedef __declspec(align(16)) shaderCommands_s	shaderCommands_t;
#else
	typedef struct shaderCommands_s  shaderCommands_t;
#endif
extern	shaderCommands_t	tess;
extern	color4ub_t	styleColors[MAX_LIGHT_STYLES];

void RB_BeginSurface(shader_t *shader, int fogNum, int cubemapIndex );
void RB_EndSurface(void);
void RB_CheckOverflow( int verts, int indexes );
#define RB_CHECKOVERFLOW(v,i) if (tess.numVertexes + (v) >= SHADER_MAX_VERTEXES || tess.numIndexes + (i) >= SHADER_MAX_INDEXES ) {RB_CheckOverflow(v,i);}

void R_DrawElementsVBO( int numIndexes, glIndex_t firstIndex, glIndex_t minIndex, glIndex_t maxIndex );
void RB_StageIteratorGeneric( void );
void RB_StageIteratorSky( void );

void RB_AddQuadStamp( vec3_t origin, vec3_t left, vec3_t up, float color[4] );
void RB_AddQuadStampExt( vec3_t origin, vec3_t left, vec3_t up, float color[4], float s1, float t1, float s2, float t2 );
void RB_InstantQuad( vec4_t quadVerts[4] );
void RB_InstantQuad2(vec4_t quadVerts[4], vec2_t texCoords[4]);
void RB_InstantTriangle();

void RB_ShowImages( void );


/*
============================================================

WORLD MAP

============================================================
*/
world_t *R_GetWorld(int worldIndex);
void R_AddBrushModelSurfaces( trRefEntity_t *e, int entityNum );
void R_AddWorldSurfaces( viewParms_t *viewParms, trRefdef_t *refdef );
void R_MarkLeaves(void);
void R_RecursiveWorldNode(mnode_t *node, int planeBits, int dlightBits, int pshadowBits);
qboolean R_inPVS( const vec3_t p1, const vec3_t p2, byte *mask );

/*
============================================================

FLARES

============================================================
*/

void R_ClearFlares( void );

void RB_AddFlare( void *surface, int fogNum, vec3_t point, vec3_t color, vec3_t normal );
void RB_AddDlightFlares( void );
void RB_RenderFlares (void);

/*
============================================================

LIGHTS

============================================================
*/

void R_DlightBmodel( bmodel_t *bmodel, trRefEntity_t *ent );
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent );
void R_UpdateEntityLightGridTextures(world_t *world);
void R_BuildLightGridColorTexture(world_t *world);
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *ori );
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
int R_LightDirForPoint( vec3_t point, vec3_t lightDir, vec3_t normal, world_t *world );
int R_DLightsForPoint(const vec3_t point, const float radius);
int R_CubemapForPoint( const vec3_t point );
#ifdef REND2_SP
qboolean RE_GetLighting(const vec3_t origin, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir);
#endif

/*
============================================================

SKIES

============================================================
*/

void R_BuildCloudData( shaderCommands_t *shader );
void R_InitSkyTexCoords( float cloudLayerHeight );
void RB_DrawSun( float scale, shader_t *shader );
void RB_ClipSkyPolygons( shaderCommands_t *shader );

/*
============================================================

CURVE TESSELATION

============================================================
*/

srfBspSurface_t *R_SubdividePatchToGrid( int width, int height,
								srfVert_t points[MAX_PATCH_SIZE*MAX_PATCH_SIZE] );
srfBspSurface_t *R_GridInsertColumn( srfBspSurface_t *grid, int column, int row, vec3_t point, float loderror );
srfBspSurface_t *R_GridInsertRow( srfBspSurface_t *grid, int row, int column, vec3_t point, float loderror );
void R_FreeSurfaceGridMesh( srfBspSurface_t *grid );

/*
============================================================

MARKERS, POLYGON PROJECTION ON WORLD POLYGONS

============================================================
*/

int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );


/*
============================================================

VERTEX BUFFER OBJECTS

============================================================
*/

struct VertexArraysProperties
{
	size_t vertexDataSize;
	int numVertexArrays;

	int enabledAttributes[ATTR_INDEX_MAX];
	size_t offsets[ATTR_INDEX_MAX];
	size_t sizes[ATTR_INDEX_MAX];
	size_t strides[ATTR_INDEX_MAX];
	size_t streamStrides[ATTR_INDEX_MAX];
	void *streams[ATTR_INDEX_MAX];
	size_t stepRates[ATTR_INDEX_MAX];
};

uint32_t R_VboPackTangent(vec4_t v);
uint32_t R_VboPackNormal(vec3_t v);
void R_VboUnpackTangent(vec4_t v, uint32_t b);
void R_VboUnpackNormal(vec3_t v, uint32_t b);

VBO_t          *R_CreateVBO(byte * vertexes, size_t vertexesSize, vboUsage_t usage, const char *debugName);
IBO_t          *R_CreateIBO(byte * indexes, size_t indexesSize, vboUsage_t usage, const char *debugName);

void            R_BindVBO(VBO_t * vbo);
void            R_BindNullVBO(void);

void            R_BindIBO(IBO_t * ibo);
void            R_BindNullIBO(void);

void			R_InitGPUBuffers(void);
void            R_DestroyGPUBuffers(void);
void            R_VBOList_f(void);

void            RB_UpdateVBOs(unsigned int attribBits);
#ifdef _G2_GORE
void			RB_UpdateGoreVertexData(struct gpuFrame_t* currentFrame, srfG2GoreSurface_t* goreSurface, bool updateFirstVertAndIndex);
#endif
void			RB_CommitInternalBufferData();

void			RB_BindUniformBlock(GLuint ubo, uniformBlock_t block, size_t offset);
size_t			RB_BindAndUpdateFrameUniformBlock(uniformBlock_t block, void *data);
void			RB_AddShaderToShaderInstanceUBO(shader_t *shader);
size_t			RB_AddShaderInstanceBlock(void *data);
void			RB_UpdateConstants(const trRefdef_t *refdef);
void			RB_BeginConstantsUpdate(struct gpuFrame_t *frame);
void			RB_EndConstantsUpdate(const struct gpuFrame_t *frame);
size_t			RB_AppendConstantsData(struct gpuFrame_t *frame, const void *data, size_t dataSize);
void			CalculateVertexArraysProperties(uint32_t attributes, VertexArraysProperties *properties);
void			CalculateVertexArraysFromVBO(uint32_t attributes, const VBO_t *vbo, VertexArraysProperties *properties);

/*
============================================================

SHADOWS

============================================================
*/

void RB_ShadowTessEnd(shaderCommands_t *input, const VertexArraysProperties *vertexArrays);
void RB_ShadowFinish(void);
void RB_ProjectionShadowDeform(void);

/*
============================================================

GLSL

============================================================
*/

void GLSL_InitSplashScreenShader();
// source is a GLSL body (version 430 is prepended); program must be empty.
// Failure is nonfatal so the owning feature can keep its legacy path.
bool GLSL_InitComputeShader(shaderProgram_t *program, const char *name,
	const char *source, uint32_t requiredFeatures);
void GLSL_DeleteGPUShader(shaderProgram_t *program);
void GLSL_LoadGPUShaders();
void GLSL_ShutdownGPUShaders(void);
void GLSL_VertexAttribsState(uint32_t stateBits, VertexArraysProperties *vertexArrays);
void GLSL_VertexAttribPointers(const VertexArraysProperties *vertexArrays);
void GL_VertexArraysToAttribs( vertexAttribute_t *attribs,
	size_t attribsCount, const VertexArraysProperties *vertexArrays );
void GLSL_BindProgram(shaderProgram_t * program);
void GLSL_BindNullProgram(void);

void GLSL_SetUniformInt(shaderProgram_t *program, int uniformNum, GLint value);
void GLSL_SetUniformFloat(shaderProgram_t *program, int uniformNum, GLfloat value);
void GLSL_SetUniformFloatN(shaderProgram_t *program, int uniformNum, const float *v, int numFloats);
void GLSL_SetUniformVec2(shaderProgram_t *program, int uniformNum, const vec2_t v);
void GLSL_SetUniformVec2N(shaderProgram_t *program, int uniformNum, const float *v, int numVec2s);
void GLSL_SetUniformVec4N(shaderProgram_t *program, int uniformNum, const float *v, int numVec4s);
void GLSL_SetUniformVec3(shaderProgram_t *program, int uniformNum, const vec3_t v);
void GLSL_SetUniformVec4(shaderProgram_t *program, int uniformNum, const vec4_t v);
void GLSL_SetUniformMatrix4x3(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements = 1);
void GLSL_SetUniformMatrix4x4(shaderProgram_t *program, int uniformNum, const float *matrix, int numElements = 1);
void GLSL_SetUniforms( shaderProgram_t *program, UniformData *uniformData );

shaderProgram_t *GLSL_GetGenericShaderProgram(int stage);

/*
============================================================

SCENE GENERATION

============================================================
*/

void R_InitNextFrame( void );

void RE_ClearScene( void );
void RE_AddRefEntityToScene( const refEntity_t *ent );
void RE_AddMiniRefEntityToScene( const miniRefEntity_t *miniRefEnt );
void RE_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t *verts, int num = 1);
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void RE_BeginScene( const refdef_t *fd );
void RE_RenderScene( const refdef_t *fd );
void RE_EndScene( void );

/*
=============================================================

UNCOMPRESSING BONES

=============================================================
*/

#define MC_BITS_X (16)
#define MC_BITS_Y (16)
#define MC_BITS_Z (16)
#define MC_BITS_VECT (16)

#define MC_SCALE_X (1.0f/64)
#define MC_SCALE_Y (1.0f/64)
#define MC_SCALE_Z (1.0f/64)

/*
=============================================================

ANIMATED MODELS

=============================================================
*/

void R_MDRAddAnimSurfaces( trRefEntity_t *ent, int entityNum );
void RB_MDRSurfaceAnim( mdrSurface_t *surface );
qboolean R_LoadIQM (model_t *mod, void *buffer, size_t filesize, const char *name );
void R_AddIQMSurfaces( trRefEntity_t *ent, int entityNum );
void RB_IQMSurfaceAnim( surfaceType_t *surface );
int R_IQMLerpTag( orientation_t *tag, iqmData_t *data,
                  int startFrame, int endFrame,
                  float frac, const char *tagName );

/*
Ghoul2 Insert Start
*/
#ifdef _MSC_VER
#pragma warning (disable: 4512)	//default assignment operator could not be gened
#endif
class CRenderableSurface
{
public:
	// ident of this surface - required so the materials renderer knows what
	// sort of surface this refers to
	int ident;

	CBoneCache *boneCache;
	mdxmVBOMesh_t *vboMesh;

	// tell the renderer to render shadows for this surface
	qboolean genShadows;
	int dlightBits;
	int pshadowBits;

	// pointer to surface data loaded into file - only used by client renderer
	// DO NOT USE IN GAME SIDE - if there is a vid restart this will be out of
	// wack on the game
	mdxmSurface_t *surfaceData;

#ifdef _G2_GORE
	// alternate texture coordinates
	srfG2GoreSurface_t *alternateTex;
	void *goreChain;

	float scale;
	float fade;

	// this is a number between 0 and 1 that dictates the progression of the
	// bullet impact
	float impactTime;
#endif

	CRenderableSurface& operator =( const CRenderableSurface& src )
	{
		ident = src.ident;
		boneCache = src.boneCache;
		surfaceData = src.surfaceData;
#ifdef _G2_GORE
		alternateTex = src.alternateTex;
		goreChain = src.goreChain;
#endif
		vboMesh = src.vboMesh;

		return *this;
	}

	CRenderableSurface()
		: ident(SF_MDX)
		, boneCache(nullptr)
		, vboMesh(nullptr)
		, genShadows(qfalse)
		, dlightBits(0)
		, pshadowBits(0)
		, surfaceData(nullptr)
#ifdef _G2_GORE
		, alternateTex(nullptr)
		, goreChain(nullptr)
		, scale(1.0f)
		, fade(0.0f)
		, impactTime(0.0f)
#endif
	{
	}

	void Init()
	{
		ident = SF_MDX;
		boneCache = nullptr;
		surfaceData = nullptr;
#ifdef _G2_GORE
		alternateTex = nullptr;
		goreChain = nullptr;
#endif
		vboMesh = nullptr;
		genShadows = qfalse;
	}
};

void R_AddGhoulSurfaces( trRefEntity_t *ent, int entityNum );
void R_G2ShadowStatsEndFrame( void );
void RB_SurfaceGhoul( CRenderableSurface *surf );
void RB_TransformBones(const trRefEntity_t *ent, const trRefdef_t *refdef, int currentFrameNum, gpuFrame_t *frame);
int RB_GetBoneUboOffset(CRenderableSurface *surf);
int RB_GetPreviousBoneUboOffset(CRenderableSurface *surf);
void RB_SetBoneUboOffset(CRenderableSurface *surf, int offset, int currentFrameNum);
void RB_FillBoneBlock(CRenderableSurface *surf, mat3x4_t *outMatrices);
/*
Ghoul2 Insert End
*/

/*
=============================================================
=============================================================
*/
void	R_TransformModelToClip( const vec3_t src, const float *modelViewMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst );
void	R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window );

void	RB_DeformTessGeometry( void );

void	RB_CalcFogTexCoords( float *dstTexCoords );

void	RB_CalcScaleTexMatrix( const float scale[2], float *matrix );
void	RB_CalcScrollTexMatrix( const float scrollSpeed[2], float *matrix );
void	RB_CalcRotateTexMatrix( float degsPerSecond, float *matrix );
void	RB_CalcTurbulentFactors( const waveForm_t *wf, float *amplitude, float *now );
void	RB_CalcTransformTexMatrix( const texModInfo_t *tmi, float *matrix  );
void	RB_CalcStretchTexMatrix( const waveForm_t *wf, float *matrix );

void	RB_CalcModulateColorsByFog( unsigned char *dstColors );
float	RB_CalcWaveAlphaSingle( const waveForm_t *wf );
float	RB_CalcWaveColorSingle( const waveForm_t *wf );

/*
=============================================================

RENDERER BACK END FUNCTIONS

=============================================================
*/

void RB_ExecuteRenderCommands( const void *data );

/*
=============================================================

RENDERER BACK END COMMAND QUEUE

=============================================================
*/

#define	MAX_RENDER_COMMANDS	0x80000

typedef struct renderCommandList_s {
	byte	cmds[MAX_RENDER_COMMANDS];
	int		used;
} renderCommandList_t;

typedef struct setColorCommand_s {
	int		commandId;
	float	color[4];
} setColorCommand_t;

typedef struct drawBufferCommand_s {
	int		commandId;
	int		buffer;
} drawBufferCommand_t;

typedef struct subImageCommand_s {
	int		commandId;
	image_t	*image;
	int		width;
	int		height;
	void	*data;
} subImageCommand_t;

typedef struct swapBuffersCommand_s {
	int		commandId;
} swapBuffersCommand_t;

typedef struct endFrameCommand_s {
	int		commandId;
	int		buffer;
} endFrameCommand_t;

typedef struct stretchPicCommand_s {
	int		commandId;
	shader_t	*shader;
	float	x, y;
	float	w, h;
	float	s1, t1;
	float	s2, t2;
} stretchPicCommand_t;

typedef struct rotatePicCommand_s {
	int		commandId;
	shader_t	*shader;
	float	x, y;
	float	w, h;
	float	s1, t1;
	float	s2, t2;
	float	a;
} rotatePicCommand_t;

typedef struct scissorCommand_s {
	int	commandId;
	float x, y;
	float w, h;
} scissorCommand_t;

typedef struct drawSurfsCommand_s {
	int		commandId;
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	drawSurf_t *drawSurfs;
	int		numDrawSurfs;
} drawSurfsCommand_t;

typedef enum {
	SSF_JPEG,
	SSF_TGA,
	SSF_PNG
} screenshotFormat_t;

typedef struct screenShotCommand_s {
	int commandId;
	int x;
	int y;
	int width;
	int height;
	char *fileName;
	screenshotFormat_t format;
} screenshotCommand_t;

typedef struct videoFrameCommand_s {
	int						commandId;
	int						width;
	int						height;
	byte					*captureBuffer;
	byte					*encodeBuffer;
	qboolean			motionJpeg;
} videoFrameCommand_t;

typedef struct colorMaskCommand_s {
	int commandId;

	GLboolean rgba[4];
} colorMaskCommand_t;

typedef struct clearDepthCommand_s {
	int commandId;
} clearDepthCommand_t;

typedef struct convolveCubemapCommand_s {
	int			commandId;
	cubemap_t	*cubemap;
	int			cubemapId;
	qboolean	filterSpecular;
	qboolean	filterDiffuse;
} convolveCubemapCommand_t;

// tr_rainlens.cpp: weather and camera input of the lens water, front end
#define RAINLENS_MAX_EVENTS		8
#define RAINLENS_EVENT_SUBMERGE	16	// camera entered water (besides LENSWATER_*)

typedef struct lensWaterEmitter_s {
	vec3_t		origin;
	vec3_t		dir;		// world direction of the water, zero = none
	float		radius;
	float		strength;
	float		interval;	// LENSWATER_SPLASH: mean seconds between splashes
	int			type;		// LENSWATER_SPRAY or LENSWATER_SPLASH
	int			nextTime;	// game time of the next splash
} lensWaterEmitter_t;

typedef struct rainLensEvent_s {
	int			type;		// LENSWATER_* or RAINLENS_EVENT_SUBMERGE
	float		strength;	// after distance falloff
	float		dirLens[2];	// lens space side the water comes from
	float		duration;
} rainLensEvent_t;

typedef struct rainLensInput_s {
	qboolean	active;		// main world view that owns the lens
	qboolean	submerged;	// camera in water: lens not drawn
	float		intensity;	// rain particle count / 5000, 0 = no rain
	float		exposed;	// 1 = new rain reaches the lens (outside)
	float		facing;		// 0..1, lens facing into the falling rain
	int			weather;	// rainWeather_t
	float		windLens[2];
	int			numEvents;
	rainLensEvent_t	events[RAINLENS_MAX_EVENTS];
	float		sprayStrength;	// continuous spray of map emitters
	float		sprayDir[2];
	// optics, lens space (x right, y up, z toward the viewer)
	float		keyDir[3];		// dominant light (sun or light grid direction)
	float		keyColor[3];	// its radiance at the camera, zero = none
	float		pointDir[3];	// brightest nearby dynamic light
	float		pointColor[3];
	float		ambient[3];		// light grid ambient: reflection without a cubemap
	float		cameraLuma;		// light grid luma at the camera (cubemap normalisation)
	int			cubemapIndex;	// R_CubemapForPoint(vieworg), 0 = none
} rainLensInput_t;

typedef struct postProcessCommand_s {
	int		commandId;
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	rainLensInput_t	rainLens;
} postProcessCommand_t;

typedef struct beginTimedBlockCommand_s {
	int commandId;
	qhandle_t timerHandle;
	const char *name;
} beginTimedBlockCommand_t;

typedef struct endTimedBlockCommand_s {
	int commandId;
	qhandle_t timerHandle;
} endTimedBlockCommand_t;

// r_foliageBendField (tr_foliagefield.cpp): the bend field of this frame, before
// the draws of the first world scene
typedef struct foliageFieldCommand_s {
	int		commandId;
	GLuint	ubo;			// FoliageInteraction block of the scene
	long	uboOffset;
	int		source;			// state read (previous frame)
	int		target;			// state written, -1: no update this frame
	int		current;		// bound to TB_FOLIAGEFIELD after the pass
	int		previous;		// bound to TB_FOLIAGEFIELD_PREV
	qboolean clear;			// also clear both states first
} foliageFieldCommand_t;

typedef enum {
	RC_END_OF_LIST,
	RC_SET_COLOR,
	RC_STRETCH_PIC,
	RC_ROTATE_PIC,
	RC_ROTATE_PIC2,
	RC_DRAW_SURFS,
	RC_DRAW_BUFFER,
	RC_SWAP_BUFFERS,
	RC_SCREENSHOT,
	RC_VIDEOFRAME,
	RC_SCISSOR,
	RC_COLORMASK,
	RC_CLEARDEPTH,
	RC_CONVOLVECUBEMAP,
	RC_POSTPROCESS,
	RC_BEGIN_TIMED_BLOCK,
	RC_END_TIMED_BLOCK,
	RC_FOLIAGE_FIELD
} renderCommand_t;

struct gpuTimer_t
{
	const char *name;
	GLuint queryName;
};

struct gpuTimedBlock_t
{
	const char *name;
	GLuint beginTimer;
	GLuint endTimer;
};

struct screenshotReadback_t
{
	GLuint pbo;
	int strideInBytes;
	int rowInBytes;
	int width;
	int height;
	screenshotFormat_t format;
	char filename[MAX_QPATH];
};

struct ghoul2UboCache_t
{
	void *ghoulPointer;
	qhandle_t model;
	int boltIndex;
	int boneUboOffset;
};

struct modelUboCache_t
{
	modtype_t type;
	qhandle_t hModel;
	void      *ghoulPointer;
	vec3_t    origin;
	vec3_t    velocity;
	int       modelUboOffset;
};

#define MAX_GPU_TIMERS (512)
#define MAX_SCENES (3)
struct gpuFrame_t
{
	GLsync sync;
	byte   currentScene;
	GLuint ubo[MAX_SCENES];
	size_t uboWriteOffset[MAX_SCENES];
	size_t uboSize[MAX_SCENES];
	size_t uboMapBase[MAX_SCENES];
	void *uboMemory[MAX_SCENES];

	screenshotReadback_t screenshotReadback;

	VBO_t *dynamicVbo;
	void *dynamicVboMemory;
	size_t dynamicVboWriteOffset;
	size_t dynamicVboCommitOffset;

	IBO_t *dynamicIbo;
	void *dynamicIboMemory;
	size_t dynamicIboWriteOffset;
	size_t dynamicIboCommitOffset;

#ifdef _G2_GORE
	VBO_t					*goreVBO;
	void					*goreVBOMemory;
	int						goreVBOCurrentIndex;
	IBO_t					*goreIBO;
	void					*goreIBOMemory;
	int						goreIBOCurrentIndex;
#endif

	int numTimers;
	int numTimedBlocks;

	int numCachedGhoulUboOffsets;
	ghoul2UboCache_t cachedGhoulUboOffsets[MAX_GENTITIES];

	int numCachedModelUboOffsets;
	modelUboCache_t cachedModelUboOffsets[MAX_REFENTITIES];

	matrix_t viewProjectionMatrix;
	float    time;

	// main world view of scene 0, for the temporal history checks (tr_motionblur.cpp)
	qboolean hasMainView;
	vec3_t   viewOrigin;
	vec3_t   viewForward;
	float    fovX;
	double   realTime;		// seconds
	const void *world;

	gpuTimer_t timers[MAX_GPU_TIMERS];
	gpuTimedBlock_t timedBlocks[MAX_GPU_TIMERS / 2]; // Each block will need 2 timer queries.
};

// all of the information needed by the back end must be
// contained in a backEndData_t.

#define PER_FRAME_MEMORY_BYTES (32 * 1024 * 1024)
class Allocator;
struct Pass;
typedef struct backEndData_s {
	unsigned realFrameNumber;
	gpuFrame_t frames[MAX_FRAMES];
	gpuFrame_t *currentFrame;
	gpuFrame_t *previousFrame;
	Allocator *perFrameMemory;
	Pass *currentPass;

	bool cachePreviousFrameUbos;
	uint32_t numFrameUbos;
	GLuint *frameUbos;

	drawSurf_t	drawSurfs[MAX_DRAWSURFS];
	dlight_t	dlights[MAX_RENDER_DLIGHTS];	// MAX_DLIGHTS used unless Forward+
	refFogVolume_t	fogVolumes[MAX_REF_FOG_VOLUMES];	// local fog volumes, same lifetime as dlights
	refVolParticle_t	volParticles[MAX_REF_VOL_PARTICLES];	// volumetric FX particles, same lifetime
	trRefEntity_t	entities[MAX_REFENTITIES];
	srfPoly_t	*polys;//[MAX_POLYS];
	polyVert_t	*polyVerts;//[MAX_POLYVERTS];
	pshadow_t pshadows[MAX_CALC_PSHADOWS];
	renderCommandList_t	commands;
} backEndData_t;

extern	int		max_polys;
extern	int		max_polyverts;

extern	backEndData_t	*backEndData;


void *R_GetCommandBuffer( int bytes );
void RB_ExecuteRenderCommands( const void *data );

void R_IssuePendingRenderCommands( void );

void R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs );
void R_AddConvolveCubemapCmd(cubemap_t *cubemap, int cubemapId, qboolean filterSpecular, qboolean filterDiffuse);
void R_AddPostProcessCmd (void);
qhandle_t R_BeginTimedBlockCmd( const char *name );
void R_EndTimedBlockCmd( qhandle_t timerHandle );


void RE_SetColor( const float *rgba );
void RE_StretchPic ( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );
void RE_RotatePic ( float x, float y, float w, float h, float s1, float t1, float s2, float t2, float a, qhandle_t hShader );
void RE_RotatePic2 ( float x, float y, float w, float h, float s1, float t1, float s2, float t2,float a, qhandle_t hShader );
#ifdef REND2_SP
void RE_LAGoggles(void);
void RE_Scissor(float x, float y, float w, float h);
#endif
void RE_BeginFrame( stereoFrame_t stereoFrame );
void R_NewFrameSync();
void RE_EndFrame( int *frontEndMsec, int *backEndMsec );
void RE_TakeVideoFrame( int width, int height,
		byte *captureBuffer, byte *encodeBuffer, qboolean motionJpeg );

// tr_ghoul2.cpp
#ifdef REND2_SP
void Create_Matrix(const float* angle, mdxaBone_t* matrix);
void G2_SetSurfaceOnOffFromSkin(CGhoul2Info *ghlInfo, qhandle_t renderSkin);	//tr_ghoul2.cpp
#endif
void Mat3x4_Multiply(mdxaBone_t *out, const mdxaBone_t *in2, const mdxaBone_t *in);
void Mat3x4_Scale( mdxaBone_t *result, const mdxaBone_t *lhs, const float scale );
void Mat3x4_Lerp(
	mdxaBone_t *result,
	const mdxaBone_t *lhs,
	const mdxaBone_t *rhs,
	const float t );
const mdxaBone_t operator +( const mdxaBone_t& lhs, const mdxaBone_t& rhs );
const mdxaBone_t operator -( const mdxaBone_t& lhs, const mdxaBone_t& rhs );
const mdxaBone_t operator *( const mdxaBone_t& lhs, const mdxaBone_t& rhs );
const mdxaBone_t operator *( const mdxaBone_t& lhs, const float scale );
const mdxaBone_t operator *( const float scale, const mdxaBone_t& rhs );

qboolean R_LoadMDXM( model_t *mod, void *buffer, const char *name, qboolean &bAlreadyCached );
qboolean R_LoadMDXA( model_t *mod, void *buffer, const char *name, qboolean &bAlreadyCached );
void RE_InsertModelIntoHash( const char *name, model_t *mod );
void ResetGhoul2RenderableSurfaceHeap();

void R_InitDecals( void );
void RE_ClearDecals( void );
void RE_AddDecalToScene ( qhandle_t shader, const vec3_t origin, const vec3_t dir, float orientation, float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary );
void R_AddDecals( void );

image_t	*R_FindImageFile( const char *name, imgType_t type, int flags );
void R_LoadPackedMaterialImage(shaderStage_t *stage, const char *packedImageName, int flags);
image_t *R_BuildSDRSpecGlossImage(shaderStage_t *stage, const char *specImageName, int flags);
image_t *R_BuildNormalHeightImage(const char *normalName, const char *heightName, int flags);
image_t *R_BuildLegacySpecORMSImage(const char *specImageName, int flags);
image_t *R_BuildAutoRoughnessORMSImage(const char *diffuseName, int flags);
extern int autoRoughnessMaps, autoRoughnessMsec;
qhandle_t RE_RegisterShader( const char *name );
qhandle_t RE_RegisterShaderNoMip( const char *name );
const char		*RE_ShaderNameFromIndex(int index);
image_t *R_CreateImage( const char *name, byte *pic, int width, int height, imgType_t type, int flags, int internalFormat );
// mipData, when provided, contains every level including level 0 (instead of data).
image_t *R_CreateImage3D(const char *name, byte *data, int width, int height, int depth, int internalFormat, int flags = IMGFLAG_CLAMPTOEDGE, const byte *const *mipData = NULL);
image_t *R_Create2DImageArray(const char *name, byte *pic, int width, int height, int layers, imgType_t type, int flags, int internalFormat);
image_t *R_GetLoadedImage(const char *name, int flags);

void R_CreateColorGradingImages(void);

/*
============================================================

SCREEN-SPACE AO AND CONTACT SHADOWS, tr_ao.cpp

============================================================
*/

typedef enum
{
	AO_MODE_OFF,
	AO_MODE_LEGACY,
	AO_MODE_GTAO
} aoMode_t;

qboolean R_AOResourcesEnabled(void);
int R_AOMode(void);
void R_CreateAOImages(int width, int height);
void R_CreateAOFBOs(void);
void RB_RenderScreenSpaceLighting(void);
void RB_AOSceneParams(vec4_t aoParams, vec4_t aoParams2);
qboolean RB_AODebugBypassesToneMap(void);

/*
============================================================
AUTO PBR, tr_autopbr.cpp
============================================================
*/
void R_ClassifyMaterial(shaderStage_t *stage, const char *shaderName, const char *diffuseName);
typedef struct { materialClass_t cls; const char *reason; const char *token; } materialMatch_t;
void R_ClassifyMaterialName(materialMatch_t *match, const char *shaderName, const char *diffuseName);
qboolean R_AutoPBRSpecularScale(const shaderStage_t *stage, vec4_t out);
qboolean R_AutoPBRDebugColor(const shaderStage_t *stage, vec4_t out);
image_t *R_AutoPBRRoughnessImage(const shaderStage_t *stage);
const char *R_MaterialClassName(materialClass_t cls);
// r_weatherWetness response of the stage class: darkening, wet roughness scale, normal flattening
void R_WetnessResponse(const shaderStage_t *stage, vec3_t out);
qboolean R_IsAutoPBRSource(pbrSource_t source);
qboolean R_IsGouraudStage(const shaderStage_t *stage);
void R_PBRDumpMaterials_f(void);
void R_ClassifyFoliageShader(shader_t *shader, shaderStage_t *stages);
void R_InitFoliageModel(mdvModel_t *model, const char *path);
void R_InitFoliageSurface(mdvSurface_t *surface, int numFrames);
foliageResult_t R_ResolveAutoFoliage(const mdvModel_t *model,
	const mdvSurface_t *surface, const shader_t *shader, int mode);
const char *R_FoliageClassName(int cls);
void R_FoliageDebugColor(int cls, vec4_t out);
void R_PrintAutoFoliage_f(void);

// tr_leafflutter.cpp
class UniformDataWriter;
bool R_LeafFlutterActive(void);
bool R_LeafFlutterSurface(const drawSurf_t *drawSurf);
float R_LeafFlutterCullMargin(const mdvModel_t *model);
void RB_LeafFlutterBeginFrame(float currentTime, float previousTime);
void RB_SetLeafFlutterUniforms(UniformDataWriter& writer, bool active);
bool RB_LeafFlutterDebugColor(vec4_t color);

// tr_foliageinteract.cpp: character colliders bending grass and MD3 plants
struct UniformBlockBinding;
bool R_FoliageInteractionActive(void);
bool R_PlantBendActive(void);
uint8_t R_FoliageMotionClass(const drawSurf_t *drawSurf);
float R_PlantBendCullMargin(const mdvModel_t *model);
void RE_SetFoliageInteractors(const foliageInteractor_t *interactors, int count);
void R_FoliageInteractionBeginScene(const refdef_t *fd);
void R_FoliageInteractionReset(void);
void R_FoliageInteractionList_f(void);
void RB_UpdateFoliageInteractionConstants(struct gpuFrame_t *frame, const trRefdef_t *refdef, float previousTime);
UniformBlockBinding RB_GetFoliageInteractionBlockUniformBinding(void);
void RB_SetFoliageMotionUniforms(UniformDataWriter& writer, uint8_t cls);
void RB_SetSpriteInteractionUniforms(UniformDataWriter& writer);
bool RB_FoliageInteractionDebugColor(uint8_t cls, vec4_t color);

// tr_foliagefield.cpp: persistent bend field of the character interaction
bool R_FoliageFieldActive(void);
void R_CreateFoliageFieldImages(void);
void R_CreateFoliageFieldFBOs(void);
void R_FoliageFieldReset(void);
void R_FoliageFieldLatch(const refdef_t *fd, bool consecutive, const foliageInteractor_t *player);
void R_FoliageFieldBlock(FoliageInteractionBlock *block, int debugBits);
void R_FoliageFieldQueueUpdate(GLuint ubo, long uboOffset);
void R_FoliageFieldClear_f(void);
void R_FoliageFieldInfo(void);
const void *RB_FoliageFieldCommand(const void *data);
void RB_FoliageFieldDebugOverlay(void);
bool R_FoliageFieldBounds(vec2_t mins, vec2_t maxs);
void RB_AODebugOverlay(void);

qboolean R_MotionBlurEnabled(void);
void R_CreateMotionBlurImages(int width, int height, int hdrFormat);
void RB_MotionBlurUpdateHistory(struct gpuFrame_t *frame, const struct gpuFrame_t *previousFrame, const trRefdef_t *refdef);
qboolean RB_MotionBlurActive(void);
qboolean RB_MotionBlur(FBO_t *srcFbo);
void RB_MotionBlurDebugOverlay(void);

// tr_rainlens.cpp
void R_CreateRainLensImages(int width, int height, int hdrFormat);
void R_RainLensInput(const trRefdef_t *refdef, const viewParms_t *viewParms, rainLensInput_t *input);
qboolean RB_RainLensUpdate(const rainLensInput_t *input);
void RB_RainLens(FBO_t *srcFbo);
void RB_RainLensDebugOverlay(void);
void R_RainLensClear_f(void);
void RE_AddLensWaterEvent(const refLensWaterEvent_t *event);
void R_ShutdownRainLens(void);
float R_RainLensRefractionScale(void);
void R_LensWaterCommand(const char *args);
void R_LoadLensWaterEmittersJson(world_t *world, const char *json, const char *jsonEnd, const char *filename);
void R_RainLensEvent_f(void);
void R_RainLensProfile_f(void);
void R_RainLensStats_f(void);

/*
============================================================

FROXEL VOLUMETRIC FOG, tr_volumetric.cpp

============================================================
*/

class UniformDataWriter;
class SamplerBindingsWriter;
struct UniformBlockBinding;

qboolean R_VolumetricFroxelEnabled(void);
qboolean R_VolumetricFroxelRGB(void);	// r_volumetricFogRGBExtinction active (latched, resources created)
void R_VolumetricExtinctionColor(const float *in, vec3_t out);	// relative sigma_t.rgb, mean 1 (NULL: neutral)
void R_CreateVolumetricImages(int width, int height);
void R_CreateVolumetricFBOs(void);
void R_ShutdownVolumetric(void);
qboolean R_VolumetricComputeAvailable(void);
void R_VolumetricEnsureRasterCarry(void);
void R_BuildVolumetricLightGrid(world_t *world);
void R_SetHeightFogBase(world_t *worldData);
void R_VolumetricFog_f(void);
void R_VolumetricLightStats_f(void);
void RB_UpdateVolumetricConstants(struct gpuFrame_t *frame, const trRefdef_t *refdef);
UniformBlockBinding RB_GetVolumetricFogBlockUniformBinding(void);
void RB_VolumetricBeginView(void);
int RB_VolumetricFogMode(float sort);
qboolean RB_VolumetricHeightFogSurface(float sort);
void RB_VolumetricSetupFogDraw(int mode, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers);
// the fog pass of a surface in RGB mode: 0 = draw once, 1 = the transmittance
// multiply (u_FroxelFogMode 3), 2 = the in-scattering add (4)
void RB_VolumetricSetupFogPassDraw(int rgbPass, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers);
// sprite particle lighting (r_particleLighting): PARTICLE_LIGHT_* class of a generic stage
enum { PARTICLE_LIGHT_NONE, PARTICLE_LIGHT_LIT, PARTICLE_LIGHT_UNLIT };
int RB_ParticleLightClass(const shader_t *shader, const shaderStage_t *stage);
qboolean RB_ParticleLightNeedsFogProgram(const shader_t *shader, const shaderStage_t *stage);
void RB_ParticleLightSetupDraw(const shader_t *shader, const shaderStage_t *stage, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers);
void RB_VolumetricBuild(void);
qboolean RB_VolumetricCompositeActive(void);
void RB_VolumetricComposite(void);
void RB_VolumetricDebugOverlay(void);

/*
============================================================

LOCAL FOG VOLUMES, tr_fogvolume.cpp

============================================================
*/

qboolean R_FogVolumeHasMedium(const refFogVolume_t *volume);
void R_FogVolumesBindClusters(void);
void R_FogVolumesShutdown(void);
void RE_AddFogVolumeToScene(const refFogVolume_t *volume);
void R_FogVolumesBeginScene(const refdef_t *fd);
void R_LoadFogVolumesJson(world_t *world, const char *json, const char *jsonEnd, const char *filename);
qboolean R_FogVolumesInFrustum(const viewParms_t *view, const trRefdef_t *refdef, float farZ);
int R_FogVolumesBuild(VolumetricFogBlock *block, const viewParms_t *view, const trRefdef_t *refdef,
	const vec3_t forward, float nearZ, float farZ, int numSlices, qboolean noise);
void R_FogVolume_f(void);
qboolean R_FogVolumeSphereInFrustum(const viewParms_t *view, const vec3_t forward,
	const vec3_t center, float radius, float maxDepth, float *depth);

/*
============================================================

VOLUMETRIC FX PARTICLES, tr_volparticle.cpp

============================================================
*/

void RE_AddVolumetricParticleToScene(const refVolParticle_t *particle);
qboolean R_VolParticlesInFrustum(const viewParms_t *view, const trRefdef_t *refdef, float farZ);
int R_VolParticlesBuild(VolumetricParticlesBlock *block, const viewParms_t *view, const trRefdef_t *refdef,
	const vec3_t forward, float nearZ, float farZ, int numSlices);
void R_VolParticles_f(void);

/*
============================================================

SPOT LIGHTS, tr_spotlight.cpp

============================================================
*/

dlight_t *R_AddSceneDynamicLight(const vec3_t org, float intensity, float r, float g, float b, int additive);
void RE_AddSpotLightToScene(const refSpotLight_t *light);
float R_SpotConeAttenuation(const dlight_t *dl, const vec3_t point);
void R_SpotBoundingSphere(const dlight_t *dl, vec3_t center, float *radius);
qboolean R_SpotSphereInCone(const dlight_t *dl, const vec3_t center, float radius);
qboolean R_SpotProjectedShadow(const dlight_t *dl);
qboolean R_DlightCastsShadow(const dlight_t *dl);
float R_SpotShadowFov(const dlight_t *dl);
void R_SpotShadowAxis(const dlight_t *dl, vec3_t axis[3]);
void R_SpotLightsBeginScene(const refdef_t *fd, int firstSceneDlight);
void R_Spot_f(void);

/*
============================================================

LIGHT COOKIES, tr_lightcookie.cpp

============================================================
*/

int RE_RegisterLightCookie(const char *name);
qboolean R_LightCookiesActive(void);
void R_LightCookiesShutdown(void);
const char *R_LightCookieName(int layer);
void R_SpotSetCookie(dlight_t *dl, int handle, const vec3_t up);
qboolean R_SpotCookieUV(const dlight_t *dl, const vec3_t point, vec2_t uv);
void R_LightCookieParams(float footprintPerDistance, vec4_t out);
image_t *R_LightCookieImage(void);

/*
============================================================

SILHOUETTE PARALLAX OCCLUSION MAPPING, tr_pom_silhouette.cpp

============================================================
*/

struct packedVertex_t;
void R_PomSilhouetteBeginWorld(world_t *world);
// shader_t::pomSilhouetteSource
enum
{
	POM_SOURCE_NONE		= 0,
	POM_SOURCE_KEYWORD	= 1,	// pomSilhouette keyword
	POM_SOURCE_AUTO		= 2,	// ordinary POM height map, r_autoPOMSilhouette
};

// R_PomSilhouetteSurfaceMode: what R_AddWorldSurface adds for a surface
enum
{
	POM_SURF_ORDINARY	= 1,	// the surface itself (legacy path, R_CullSurface)
	POM_SURF_SHELL		= 2,	// its silhouette POM shell
	POM_SURF_FADEBASE	= 4,	// the surface as the far side of the crossfade band
};

// batchVerts / batchIndexes: world VBO data of R_CreateWorldVBOs, with tangents
void R_PomSilhouetteCollect(world_t *world, msurface_t *surf, const packedVertex_t *batchVerts, const glIndex_t *batchIndexes);
void R_PomSilhouetteFinishWorld(world_t *world);
qboolean R_PomSilhouetteActive(void);
void R_PomSilhouetteBeginFrame(void);

// POM self shadowing / adaptive traversal (tr_pom.cpp)
void R_PomBeginFrame(void);
void R_PomSetUniforms(const shaderStage_t *stage, UniformDataWriter& uniforms);
int R_PomSilhouetteSurfaceMode(msurface_t *surf);
void R_PomSilhouetteAddDrawSurfs(msurface_t *surf, int mode, int entityNum, int fogIndex, int dlightBits, bool isPostRenderEntity, int cubemapIndex);
void RB_SetPomMode(int mode);
void RB_SurfacePomShell(srfPomShell_t *shell);
void RB_SurfacePomFadeBase(surfaceType_t *fadeBaseType);
shaderProgram_t *RB_PomSilhouetteLightallProgram(const shaderStage_t *stage);
shaderProgram_t *RB_PomSilhouetteDepthProgram(void);
shaderProgram_t *RB_PomSilhouetteFogProgram(int fogBits);
uint32_t RB_PomSilhouetteStateBits(uint32_t stateBits, qboolean fogPass);
void RB_PomSilhouetteSetupDraw(const shaderStage_t *stage, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers, qboolean fogPass);
void RB_PomSilhouetteNoteDebugBase(const srfBspSurface_t *base);
int RB_PomSilhouetteDebugBases(const srfBspSurface_t * const **bases);
void RB_PomSilhouetteClearDebugBases(void);
qboolean RB_PomSilhouetteDebugBypassesToneMap(void);
void R_PomSilhouetteInfo_f(void);
void R_AutoPomSilhouette_f(void);
void R_ShutdownPomSilhouette(void);

/*
============================================================

FORWARD+ / CLUSTERED DYNAMIC LIGHTS, tr_forwardplus.cpp

============================================================
*/

qboolean R_ForwardPlusActive(void);
int R_DlightCapacity(void);
void R_ForwardPlusBeginFrame(void);
void R_ForwardPlusAddTestLights(const refdef_t *fd);
void R_ForwardPlusNoteDroppedLight(void);
void R_ForwardPlusPrepareScene(const trRefdef_t *refdef);
int R_ForwardPlusNumShadowSlots(void);
int R_ForwardPlusShadowSlotLight(int slot);
int R_GetUboDlights(const trRefdef_t *refdef, int *lightIndexes, int *shadowLayers);
int R_GetDlightList(const trRefdef_t *refdef, int *lightIndexes, int *shadowLayers, int maxLights);
void RB_UpdateForwardPlus(struct gpuFrame_t *frame, const trRefdef_t *refdef);
void RB_ForwardPlusCameraParams(int viewParm, CameraBlock *cameraBlock);
qboolean RB_ForwardPlusViewEnabled(int viewParm);
void RB_ForwardPlusBindTextures(SamplerBindingsWriter& samplers);
qboolean RB_ForwardPlusDebugBypassesToneMap(void);
void R_ForwardPlusSetMainViewTimer(int timerHandle);
void R_ForwardPlusCollectGpuTimes(struct gpuFrame_t *frame);
void R_ShutdownForwardPlus(void);
void R_ForwardPlusStats_f(void);
void R_SpawnTestLights_f(void);
void R_ForwardPlusBenchmark_f(void);

/*
============================================================

LTC AREA LIGHTS, tr_arealights.cpp (Forward+ only)

============================================================
*/

qboolean R_AreaLightsActive(void);
dlight_t *R_AllocSceneDlight(void);
void R_AreaLightsBeginFrame(void);
void R_CreateLtcImages(void);
void R_LoadAreaLights(const char *mapName);
void R_ClearAreaLights(void);
void R_AddAreaLightsToScene(const refdef_t *fd);
void R_AreaLightsDrawDebug(void);
void RB_AreaLightsBindTextures(SamplerBindingsWriter& samplers);
float R_AreaLightsDebugParam(void);
void RE_AddAreaLightToScene(const vec3_t center, const vec3_t right, const vec3_t up,
	float halfWidth, float halfHeight, float range, float r, float g, float b, int twoSided);
qboolean RE_AddLineLightToScene(const vec3_t start, const vec3_t end, float radius,
	float range, float r, float g, float b);
void R_ReloadAreaLights_f(void);
void R_AreaLightsList_f(void);
void R_AreaLightsNearest_f(void);
void R_ExtractAreaLights_f(void);

/*
============================================================

SHARED SCREEN-SPACE INFRASTRUCTURE, tr_screenspace.cpp

============================================================
*/

struct screenViewInfo_t
{
	vec4_t projection;	// P[0], P[5], P[8], P[9]
	vec4_t depthParams;	// P[14], P[10], zFar, view space size of one pixel at depth 1
	vec4_t viewport;	// view rectangle in texture coordinates
	matrix_t worldToView;
	matrix_t viewToWorld;
	matrix_t viewProjection;
};

// temporal history of one screen-space consumer (main view)
struct screenHistory_t
{
	qboolean valid;
	unsigned frameNumber;
	const world_t *world;
	float traceScale;
	vec3_t origin;
	vec3_t forward;
	float fovX;
	float fovY;
	int viewport[4];
	matrix_t viewProjection;
	int current;			// history image written last
};

qboolean R_ScreenSpaceResourcesEnabled(void);
void R_CreateScreenSpaceImages(int width, int height, int hdrFormat);
void R_AttachScreenSpaceRenderTargets(FBO_t *fbo, int multisample);
void R_CreateScreenSpaceFBOs(void);
void R_ScreenImageFilter(image_t *image, int maxLevel, qboolean linear);
image_t *R_ScreenCreateImage(const char *name, int width, int height, int internalFormat, qboolean linear);
image_t *R_ScreenCreateMipImage(const char *name, int width, int height, int internalFormat,
	GLenum format, GLenum type, int numLevels, qboolean linear);
FBO_t *R_ScreenCreateLevelFBO(const char *name, image_t *image, int level);
FBO_t *R_ScreenCreatePairFBO(const char *name, image_t *image0, image_t *image1);
void RB_ScreenSpaceBeginView(void);
qboolean RB_ScreenSpaceActive(void);
void RB_RenderScreenSpaceOpaque(void);
void RB_ScreenSpaceDebugOverlay(void);
void GL_SetScreenAuxWrite(bool enable);
void GL_ResetScreenAuxWrite(void);
int RB_ScreenBeginTimer(const char *name);
void RB_ScreenEndTimer(int handle);
void RB_RainSplashQuery(bool begin);	// r_rainSplashDebug splash counter (tr_weather.cpp)
void RB_ScreenSetViewUniforms(shaderProgram_t *sp, const screenViewInfo_t& info);
void RB_ScreenBeginPass(FBO_t *fbo, shaderProgram_t *sp, int width, int height, uint32_t stateBits = GLS_DEPTHTEST_DISABLE);
void RB_ScreenTexelSize(vec4_t out, int srcWidth, int srcHeight, int dstWidth, int dstHeight);
void RB_ScreenSetLevelRange(image_t *image, int tmu, int baseLevel, int maxLevel);
void RB_ScreenBindGeometry(void);
qboolean RB_ScreenHistoryValid(const screenHistory_t& history, float traceScale);
void RB_ScreenStoreHistory(screenHistory_t& history, const screenViewInfo_t& info, float traceScale, int written);
qboolean RB_ScreenVelocityValid(void);

/*
============================================================

SCREEN-SPACE REFLECTIONS, tr_ssr.cpp

============================================================
*/

qboolean R_SSRResourcesEnabled(void);

// rain wetness of lightall (tr_weather.cpp)
qboolean R_WeatherWetnessEnabled(void);
void RB_WeatherWetnessBind(const shader_t *shader, const shaderStage_t *pStage,
	class UniformDataWriter &uniformDataWriter, class SamplerBindingsWriter &samplerBindingsWriter);
qboolean R_SSRWantsVelocity(void);
void R_SSRSelectResources(void);
void R_CreateSSRImages(int width, int height, int hdrFormat);
void R_CreateSSRFBOs(void);
qboolean RB_SSRWantsView(void);
int RB_SSRDepthLevels(void);
void RB_RenderSSR(const screenViewInfo_t& info);
void RB_SSRDebugOverlay(void);

/*
============================================================

SCREEN-SPACE DIFFUSE GI, tr_ssgi.cpp

============================================================
*/

qboolean R_SSGIResourcesEnabled(void);
qboolean R_SSGIWantsVelocity(void);
void R_SSGISelectResources(void);
void R_CreateSSGIImages(int width, int height, int hdrFormat);
void R_CreateSSGIFBOs(void);
void R_SSGICheckDependencies(void);
qboolean RB_SSGIWantsView(void);
int RB_SSGIDepthLevels(void);
void RB_RenderSSGI(const screenViewInfo_t& info);
void RB_SSGIDebugOverlay(void);
void RB_SSGISceneParams(vec4_t ssgiParams);

/*
============================================================

SKIN SUBSURFACE SCATTERING, tr_skinsss.cpp

============================================================
*/

#define SKIN_SSS_MAX_TAPS 25

int R_SkinSSSMode(void);				// 0 off, 1 wrap, 2 screen-space diffusion (as built)
qboolean R_SkinSSSResourcesEnabled(void);	// mode 2: attachment + passes
void R_SkinSSSSelectResources(void);
void R_CreateSkinSSSImages(int width, int height);
void R_CreateSkinSSSFBOs(void);
void R_SkinSSSClassifyShader(const shader_t *sh, shaderStage_t *stages, int numStages);
void R_SkinSSSSetupDraw(const shaderStage_t *stage, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers);
qboolean R_SkinSSSDebugColor(const shaderStage_t *stage, vec4_t out);
const char *R_SkinSSSStageInfo(const shaderStage_t *stage);
qboolean RB_SkinSSSWantsView(void);
void RB_RenderSkinSSS(const screenViewInfo_t& info);
void R_SkinSSSKernel_f(void);
void R_SkinSSSList_f(void);
qboolean RB_SkinSSSDebugBypassesToneMap(void);
void R_SetMapColorGrading(const char *worldName);
void R_UpdateColorGrading(void);

float ProjectRadius( float r, vec3_t location );
void RE_RegisterModels_StoreShaderRequest(const char *psModelFileName, const char *psShaderName, int *piShaderIndexPoke);
qboolean ShaderHashTableExists(void);
void R_ImageLoader_Init(void);

class Allocator;
GPUProgramDesc ParseProgramSource( Allocator& allocator, const char *text );

struct DepthRange
{
	float minDepth;
	float maxDepth;
};

struct SamplerBinding
{
	image_t *image;
	qhandle_t videoMapHandle;
	uint8_t slot;
};

struct UniformBlockBinding
{
	GLuint ubo;
	size_t offset;
	uniformBlock_t block;
};

enum DrawCommandType
{
	DRAW_COMMAND_MULTI_INDEXED,
	DRAW_COMMAND_INDEXED,
	DRAW_COMMAND_ARRAYS
};

struct DrawCommand
{
	DrawCommandType type;
	GLenum primitiveType;
	int numInstances;

	union DrawParams
	{
		struct MultiDrawIndexed
		{
			int numDraws;
			GLsizei *numIndices;
			glIndex_t **firstIndices;
		} multiIndexed;

		struct DrawIndexed
		{
			GLenum indexType;
			GLsizei numIndices;
			glIndex_t firstIndex;
			glIndex_t baseVertex;
		} indexed;

		struct DrawArrays
		{
			glIndex_t firstVertex;
			GLsizei numVertices;
		} arrays;
	} params;
};

struct RenderState
{
	DepthRange depthRange;
	uint32_t stateBits;
	uint32_t cullType; // this is stupid

	bool transformFeedback;

	// also write the screen-space attachments of renderFbo (tr_screenspace.cpp)
	bool screenAux;
};

struct DrawItem
{
	RenderState renderState;

	IBO_t *ibo;
	shaderProgram_t *program;

	uint32_t numAttributes;
	vertexAttribute_t *attributes;

	uint32_t numSamplerBindings;
	SamplerBinding *samplerBindings;

	uint32_t numUniformBlockBindings;
	UniformBlockBinding *uniformBlockBindings;

	bufferBinding_t transformFeedbackBuffer;

	UniformData *uniformData;

	DrawCommand draw;
};

void DrawItemSetSamplerBindings(
	DrawItem& drawItem,
	const SamplerBinding *bindings,
	uint32_t count,
	Allocator& allocator);
void DrawItemSetUniformBlockBindings(
	DrawItem& drawItem,
	const UniformBlockBinding *bindings,
	uint32_t count,
	Allocator& allocator);
void DrawItemSetVertexAttributes(
	DrawItem& drawItem,
	const vertexAttribute_t *attributes,
	uint32_t count,
	Allocator& allocator);

template<int N>
void DrawItemSetUniformBlockBindings(
	DrawItem& drawItem,
	const UniformBlockBinding(&bindings)[N],
	Allocator& allocator)
{
	DrawItemSetUniformBlockBindings(drawItem, &bindings[0], N, allocator);
}

class UniformDataWriter
{
public:
	UniformDataWriter();

	void Start( shaderProgram_t *sp );

	UniformDataWriter& SetUniformInt( uniform_t uniform, int value );
	UniformDataWriter& SetUniformFloat( uniform_t uniform, float value );
	UniformDataWriter& SetUniformFloat( uniform_t uniform, float *values, size_t count );

	UniformDataWriter& SetUniformVec2( uniform_t uniform, float x, float y );
	UniformDataWriter& SetUniformVec2( uniform_t uniform, const float *values, size_t count = 1 );

	UniformDataWriter& SetUniformVec3( uniform_t uniform, float x, float y, float z );
	UniformDataWriter& SetUniformVec3( uniform_t uniform, const float *values, size_t count = 1 );

	UniformDataWriter& SetUniformVec4( uniform_t uniform, float x, float y, float z, float w );
	UniformDataWriter& SetUniformVec4( uniform_t uniform, const float *values, size_t count = 1 );

	UniformDataWriter& SetUniformMatrix4x3( uniform_t uniform, const float *matrix, size_t count = 1 );
	UniformDataWriter& SetUniformMatrix4x4( uniform_t uniform, const float *matrix, size_t count = 1 );

	UniformData *Finish( Allocator& destHeap );

private:
	bool failed;
	shaderProgram_t *shaderProgram;
	char scratchBuffer[2048];
	Allocator scratch;
};

class SamplerBindingsWriter
{
public:
	SamplerBindingsWriter();

	SamplerBindingsWriter( const SamplerBindingsWriter& ) = delete;
	SamplerBindingsWriter& operator=( const SamplerBindingsWriter& ) = delete;

	SamplerBindingsWriter& AddStaticImage( image_t *image, int unit );

	SamplerBindingsWriter& AddAnimatedImage( textureBundle_t *bundle, int unit );

	SamplerBinding *Finish( Allocator& destHeap, uint32_t* numBindings );

private:
	SamplerBinding scratch[32];
	bool failed;
	int count;
};

void RB_FillDrawCommand(
	DrawCommand& drawCmd,
	GLenum primitiveType,
	int numInstances,
	const shaderCommands_t *input
);

uint32_t RB_CreateSkySortKey(const DrawItem& item, int stage, int skyNumber, int layer);
uint32_t RB_CreateSortKey( const DrawItem& item, int stage, int layer );
void RB_AddDrawItem( Pass *pass, uint32_t sortKey, const DrawItem& drawItem );
DepthRange RB_GetDepthRange( const trRefEntity_t *re, const shader_t *shader );

#endif //TR_LOCAL_H
