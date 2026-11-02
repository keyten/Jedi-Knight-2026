// Intentionally no include guard: the one list of r_<feature>Compare cvars for
// SP and MP (tr_compare.cpp), the RC_* ids of tr_local.h and the GLSL header.
//
// COMPARE_FEATURE(id, cvar, master, activeMin, split, title, original, note)
//   cvar       the compare cvar: 0 normal, 1 split screen, 2 off, 3.. modes of
//              this feature (note)
//   master     the cvar that turns the feature on; the compare does something
//              only while master >= activeMin
//   split      qtrue: 1 draws the left of r_compareSplit as the original
//   original   what the original side (and 2) shows
//   note       qfalse split: why there is no split screen; otherwise extra
//              modes ("3 ...") or NULL
//
// Order is the bit order of the GLSL split mask (R_CompareOriginal); append
// new features anywhere, the ids are generated.

// image and camera
COMPARE_FEATURE(RC_TONEMAP, "r_toneMapCompare", "r_toneMapMode", 1, qtrue, "the tone mapping curve (r_toneMapMode)", "Rend2's original filmic curve", "  3  strips: original | ACES | AgX\n")
COMPARE_FEATURE(RC_CAMERA, "r_cameraCompare", "r_cameraMode", 1, qfalse, "the physical camera (r_cameraMode)", "the original exposure", "the scene is metered once per frame for the whole view")
COMPARE_FEATURE(RC_BLOOM, "r_bloomCompare", "r_bloom", 1, qtrue, "HDR bloom (r_bloom 1)", "the original dynamic glow (r_bloom -1)", NULL)
COMPARE_FEATURE(RC_COLORGRADING, "r_colorGradingCompare", "r_colorGrading", 1, qtrue, "color grading (r_colorGrading)", "ungraded colors", NULL)
COMPARE_FEATURE(RC_MOTIONBLUR, "r_motionBlurCompare", "r_motionBlur", 1, qtrue, "motion blur (r_motionBlur)", "no motion blur", NULL)
COMPARE_FEATURE(RC_SMAA, "r_smaaCompare", "r_smaa", 1, qtrue, "SMAA anti-aliasing (r_smaa)", "no anti-aliasing", NULL)
COMPARE_FEATURE(RC_REFRACTIONCA, "r_refractionChromaticAberrationCompare", "r_refractionChromaticAberration", 0.0f, qfalse, "adjustable distortion color fringes (r_refractionChromaticAberration)", "Rend2's original fringes", "the refraction is computed per vertex")

// materials
COMPARE_FEATURE(RC_AUTOPBR, "r_autoPBRCompare", "r_autoPBR", 1, qtrue, "automatic materials (r_autoPBR)", "Rend2's single matte non-metal material", NULL)
COMPARE_FEATURE(RC_AUTOPBRROUGHNESS, "r_autoPBRRoughnessCompare", "r_autoPBRRoughness", 1, qtrue, "roughness variation (r_autoPBRRoughness)", "one roughness per texture", NULL)
COMPARE_FEATURE(RC_AUTOEMISSIVE, "r_autoEmissiveCompare", "r_autoEmissive", 1, qfalse, "glowing surfaces as light emitters (r_autoEmissive)", "glow layers as plain added color", "a glow layer is drawn as emission or as added color for the whole surface")
COMPARE_FEATURE(RC_SPECULARAA, "r_specularAACompare", "r_specularAA", 1, qtrue, "specular anti-aliasing (r_specularAA)", "highlights without the added roughness", NULL)
COMPARE_FEATURE(RC_ENERGYCOMPENSATION, "r_brdfEnergyCompensationCompare", "r_brdfEnergyCompensation", 1, qtrue, "BRDF energy compensation (r_brdfEnergyCompensation)", "single-scattering GGX (rough metal darker)", NULL)
COMPARE_FEATURE(RC_DIFFUSEBRDF, "r_diffuseBRDFCompare", "r_diffuseBRDF", 1, qtrue, "Burley diffuse (r_diffuseBRDF)", "Lambert diffuse", NULL)
COMPARE_FEATURE(RC_POMSELFSHADOW, "r_pomSelfShadowCompare", "r_pomSelfShadow", 1, qtrue, "shadows inside relief (r_pomSelfShadow)", "relief without self shadows", NULL)
COMPARE_FEATURE(RC_POMSILHOUETTE, "r_pomSilhouetteCompare", "r_pomSilhouette", 1, qtrue, "relief silhouettes (r_pomSilhouette)", "ordinary POM", NULL)
COMPARE_FEATURE(RC_MATERIALMODELS, "r_materialModelsCompare", "r_materialModels", 1, qtrue, "hair and eye shading (r_materialModels)", "the standard material", NULL)

// lighting and shadows
COMPARE_FEATURE(RC_AO, "r_aoCompare", "r_aoMode", 2, qtrue, "GTAO (r_aoMode 2)", "the original SSAO setting (r_ssao)", NULL)
COMPARE_FEATURE(RC_CONTACTSHADOWS, "r_contactShadowsCompare", "r_contactShadows", 1, qtrue, "contact shadows (r_contactShadows)", "no contact shadows", NULL)
COMPARE_FEATURE(RC_FORWARDPLUS, "r_forwardPlusCompare", "r_forwardPlus", 1, qfalse, "Forward+ dynamic lights (r_forwardPlus)", "the original 32 dynamic lights", "both light paths can't run in one frame; the image differs only with more than 32 lights")
COMPARE_FEATURE(RC_SSGI, "r_ssgiCompare", "r_ssgi", 1, qtrue, "bounced light (r_ssgi)", "no bounced light", NULL)
COMPARE_FEATURE(RC_LTC, "r_ltcAreaLightsCompare", "r_ltcAreaLights", 1, qtrue, "area lights (r_ltcAreaLights)", "no map area lights", NULL)
COMPARE_FEATURE(RC_LTCSABER, "r_ltcSaberCompare", "r_ltcSaberAreaLights", 1, qfalse, "blade-shaped saber light (r_ltcSaberAreaLights)", "the original point saber light", "a saber adds either its blade light or its point light to the frame")
COMPARE_FEATURE(RC_SPOTLIGHTS, "r_spotLightsCompare", "r_spotLights", 1, qfalse, "spot light cones (r_spotLights)", "authored spots as point lights", "a spot's shadow is rendered as a cone or as a cube for the whole frame")
COMPARE_FEATURE(RC_SPOTCOOKIES, "r_spotLightCookiesCompare", "r_spotLightCookies", 1, qtrue, "projected spot patterns (r_spotLightCookies)", "spots without patterns", NULL)
COMPARE_FEATURE(RC_PCSS, "r_shadowPCSSCompare", "r_shadowPCSS", 1, qtrue, "soft sun shadows (r_shadowPCSS)", "uniformly soft shadow edges", NULL)
COMPARE_FEATURE(RC_SUNSHADOWSPLIT, "r_sunShadowModelLayerCompare", "r_sunShadowModelLayer", 1, qfalse, "sharp model sun shadows (r_sunShadowModelLayer)", "one shared sun shadow", "models go into one shadow map or the other when it is drawn")
COMPARE_FEATURE(RC_ENTITYLIGHTGRID, "r_entityLightGridCompare", "r_entityLightGrid", 1, qfalse, "light across the body (r_entityLightGrid)", "one light grid sample per model", "the light of a model comes from one or several grid samples for the whole model")
COMPARE_FEATURE(RC_ENTITYSUNSPLIT, "r_entitySunLightCompare", "r_entitySunLight", 1, qfalse, "sun on characters (r_entitySunLight)", "the light grid's merged light", "the light of a model is split on the CPU, once per model")
COMPARE_FEATURE(RC_ENTITYSHADOWAMBIENT, "r_entityShadowAmbientCompare", "r_entityShadowAmbient", 0.0001f, qtrue, "ambient darkening of models in sun shadow (r_entityShadowAmbient)", "only the main light darkens", NULL)
COMPARE_FEATURE(RC_DIFFUSEIBL, "r_diffuseIBLCompare", "r_diffuseIBL", 1, qtrue, "ambient light from the surroundings (r_diffuseIBL)", "the flat light grid ambient", NULL)
COMPARE_FEATURE(RC_SKINSSS, "r_skinSSSCompare", "r_skinSSS", 1, qtrue, "skin scattering (r_skinSSS)", "skin without scattering", NULL)
COMPARE_FEATURE(RC_RAGSOFTLIMITS, "r_ragSoftLimitsCompare", "r_ragSoftLimits", 1, qfalse, "soft ragdoll limits (r_ragSoftLimits)", "the hard joint limits", "a ragdoll is simulated one way or the other")
COMPARE_FEATURE(RC_ANIMSOFTLIMITS, "r_animSoftLimitsCompare", "r_animSoftLimits", 1, qfalse, "soft animation limits (r_animSoftLimits)", "the unmodified animation", "a pose is computed one way or the other")

// reflections
COMPARE_FEATURE(RC_SSR, "r_ssrCompare", "r_ssr", 1, qtrue, "screen-space reflections (r_ssr)", "cubemap reflections only", NULL)
COMPARE_FEATURE(RC_SSRREFLECTEDFOG, "r_ssrReflectedFogCompare", "r_ssrReflectedFog", 1, qtrue, "fog in reflections (r_ssrReflectedFog)", "reflections without fog along the ray", NULL)

// fog and effects
COMPARE_FEATURE(RC_VOLUMETRICFOG, "r_volumetricFogCompare", "r_volumetricFog", 2, qfalse, "volumetric fog (r_volumetricFog 2)", "the surfaces' own fog (as r_volumetricFog 1)", "opaque surfaces get their fog from one composite of the whole view or from their own fog passes")
COMPARE_FEATURE(RC_HEIGHTFOG, "r_volumetricFogHeightCompare", "r_volumetricFogHeight", 1, qtrue, "ground haze (r_volumetricFogHeight)", "no ground haze", NULL)
COMPARE_FEATURE(RC_FOGNOISE, "r_volumetricFogNoiseCompare", "r_volumetricFogNoise", 1, qtrue, "uneven fog (r_volumetricFogNoise)", "uniform fog", NULL)
COMPARE_FEATURE(RC_SELFSHADOW, "r_volumetricSelfShadowCompare", "r_volumetricSelfShadow", 1, qtrue, "shadows inside thick fog (r_volumetricSelfShadow)", "fog that doesn't shadow itself", NULL)
COMPARE_FEATURE(RC_MULTISCATTER, "r_volumetricMultiScatterCompare", "r_volumetricMultiScatter", 1, qtrue, "light bouncing inside fog (r_volumetricMultiScatter)", "single scattering", NULL)
COMPARE_FEATURE(RC_RGBEXTINCTION, "r_volumetricFogRGBCompare", "r_volumetricFogRGBExtinction", 1, qtrue, "colored fog absorption (r_volumetricFogRGBExtinction)", "gray absorption", NULL)
COMPARE_FEATURE(RC_FOGVOLUMES, "r_fogVolumesCompare", "r_volumetricFog", 2, qtrue, "local fog volumes (env.json FogVolumes)", "no local fog volumes", NULL)
COMPARE_FEATURE(RC_EMISSION, "r_volumetricEmissionCompare", "r_volumetricEmission", 0.0001f, qtrue, "glowing gas (r_volumetricEmission)", "fog without its own glow", NULL)
COMPARE_FEATURE(RC_VOLPARTICLES, "r_volumetricParticlesCompare", "r_volumetricParticles", 1, qtrue, "volumetric smoke (r_volumetricParticles)", "flat smoke sprites only", NULL)
COMPARE_FEATURE(RC_PARTICLELIGHTING, "r_particleLightingCompare", "r_particleLighting", 1, qtrue, "lit smoke sprites (r_particleLighting)", "smoke sprites in their painted color", NULL)

// sky
COMPARE_FEATURE(RC_ATMOSPHERE, "r_atmosphereCompare", "r_atmosphere", 1, qtrue, "the atmosphere (r_atmosphere)", "the original sky and distance", NULL)
COMPARE_FEATURE(RC_CLOUDS, "r_cloudsCompare", "r_clouds", 1, qtrue, "volumetric clouds (r_clouds)", "the original sky", NULL)
COMPARE_FEATURE(RC_CLOUDSHADOWS, "r_cloudShadowsCompare", "r_cloudShadows", 1, qtrue, "cloud shadows (r_cloudShadows)", "sunlight without cloud shadows", NULL)

// weather
COMPARE_FEATURE(RC_RAINSTREAKS, "r_rainStreaksCompare", "r_rainStreaks", 1, qfalse, "modern rain streaks (r_rainStreaks)", "the original rain lines", "each rain chunk is drawn as lines or as streaks with its own blending")
COMPARE_FEATURE(RC_RAINVOLUME, "r_rainVolumeCompare", "r_rainVolume", 1, qtrue, "rain haze (r_rainVolume)", "no rain haze", NULL)
COMPARE_FEATURE(RC_RAINLOD, "r_rainLODCompare", "r_rainLOD", 1, qfalse, "distant rain (r_rainLOD)", "full rain density at every distance", "the rain particles are thinned one way for the whole view")
COMPARE_FEATURE(RC_WETNESS, "r_weatherWetnessCompare", "r_weatherWetness", 1, qtrue, "wet surfaces (r_weatherWetness)", "dry surfaces", NULL)
COMPARE_FEATURE(RC_WETNESSPERSISTENCE, "r_weatherWetnessPersistenceCompare", "r_weatherWetnessPersistence", 1, qfalse, "wet after the rain (r_weatherWetnessPersistence)", "wetness that follows the rain at once", "without the stored water the shader cannot tell drying ground from dry ground")
COMPARE_FEATURE(RC_PUDDLES, "r_weatherPuddlesCompare", "r_weatherPuddles", 1, qtrue, "puddles (r_weatherPuddles)", "no puddles", NULL)
COMPARE_FEATURE(RC_PUDDLERIPPLES, "r_weatherPuddleRipplesCompare", "r_weatherPuddleRipples", 1, qtrue, "rain rings in puddles (r_weatherPuddleRipples)", "still puddles", NULL)
COMPARE_FEATURE(RC_RUNOFF, "r_weatherRunoffCompare", "r_weatherRunoff", 1, qtrue, "water running down walls (r_weatherRunoff)", "no runoff", NULL)
COMPARE_FEATURE(RC_RAINSPLASHES, "r_rainSplashesCompare", "r_rainSplashes", 1, qtrue, "rain splashes (r_rainSplashes)", "no splashes", NULL)
COMPARE_FEATURE(RC_RAINLENS, "r_rainLensCompare", "r_rainLens", 1, qtrue, "water on the camera lens (r_rainLens)", "a dry lens", NULL)
COMPARE_FEATURE(RC_ACIDRAIN, "r_acidRainCompare", "r_acidRainVisuals", 1, qfalse, "the acid rain tint (r_acidRainVisuals)", "each effect's own color", "the tint is chosen once per frame for every rain effect")

// water
COMPARE_FEATURE(RC_WATERSURFACE, "r_waterSurfaceCompare", "r_waterSurface", 1, qtrue, "modern water (r_waterSurface)", "the original water shaders", NULL)
COMPARE_FEATURE(RC_VOLUMETRICWATER, "r_volumetricWaterCompare", "r_volumetricWater", 1, qfalse, "underwater fog (r_volumetricWater)", "the original screen tint", "the original tint is drawn by the game over the whole screen")
COMPARE_FEATURE(RC_WATERSNELL, "r_waterSnellCompare", "r_waterSnell", 1, qfalse, "looking up from underwater (r_waterSnell)", "the plain surface from below", "Snell's window is a program of its own for the whole water draw")
COMPARE_FEATURE(RC_WATERWAVES, "r_waterWavesCompare", "r_waterWaves", 1, qtrue, "waves (r_waterWaves)", "the original scrolling surface", NULL)
COMPARE_FEATURE(RC_WATERGEOMETRY, "r_waterGeometryCompare", "r_waterGeometry", 1, qfalse, "wave geometry (r_waterGeometry, r_waterTessellation)", "a flat surface", "the surface mesh moves for the whole view")
COMPARE_FEATURE(RC_WATERINTERACTION, "r_waterInteractionCompare", "r_waterInteraction", 1, qtrue, "ripples from movement and impacts (r_waterInteraction)", "no ripples", NULL)
COMPARE_FEATURE(RC_WATERFLOW, "r_waterFlowCompare", "r_waterFlow", 1, qfalse, "flowing water (r_waterFlow)", "still water", "the flow moves the waves, foam and ripples of a whole body")
COMPARE_FEATURE(RC_WATERFOAM, "r_waterFoamCompare", "r_waterFoamField", 1, qtrue, "foam (r_waterFoamField, r_waterIntersectionFoam)", "no foam", NULL)
COMPARE_FEATURE(RC_WATERSHORELINE, "r_waterShorelineCompare", "r_waterShoreline", 1, qfalse, "shorelines (r_waterShoreline)", "a hard water edge", "the shoreline also lowers the waves of the surface mesh")
COMPARE_FEATURE(RC_WATERCAUSTICS, "r_waterCausticsCompare", "r_waterCausticsMode", 1, qtrue, "underwater light patterns (r_waterCausticsMode)", "no caustics", NULL)
COMPARE_FEATURE(RC_WATERFALL, "r_waterfallCompare", "r_waterfall", 1, qfalse, "modern waterfalls (r_waterfall)", "the original waterfall shaders", "r_waterSurfaceCompare 1 splits waterfalls and water together")
COMPARE_FEATURE(RC_WHITEWATER, "r_waterWhitewaterCompare", "r_waterWhitewater", 1, qtrue, "white water (r_waterWhitewater)", "no white water", NULL)
COMPARE_FEATURE(RC_WATERPARTICLES, "r_waterParticlesCompare", "r_waterParticles", 1, qtrue, "underwater specks and bubbles (r_waterParticles)", "clear water", NULL)

// vegetation
COMPARE_FEATURE(RC_GRASSCARDS, "r_grassCardCompare", "r_grassCardMode", 1, qfalse, "fuller grass (r_grassCardMode)", "camera-facing grass sprites", "a grass tuft is built one way or the other")
COMPARE_FEATURE(RC_FOLIAGEWIND, "r_foliageWindCompare", "r_foliageWind", 1, qfalse, "wind through grass (r_foliageWind)", "the original grass sway", "moving geometry can't be split")
COMPARE_FEATURE(RC_LEAFFLUTTER, "r_leafFlutterCompare", "r_leafFlutter", 1, qfalse, "leaf flutter (r_leafFlutter)", "still leaves", "moving geometry can't be split")
COMPARE_FEATURE(RC_PLANTWIND, "r_plantWindCompare", "r_plantWind", 1, qfalse, "fronds in the wind (r_plantWind)", "rigid plants", "the plants are simulated for the whole world")
COMPARE_FEATURE(RC_FOLIAGEINTERACTION, "r_foliageInteractionCompare", "r_foliageInteraction", 1, qfalse, "walking through plants (r_foliageInteraction)", "plants that don't react", "the plants are simulated for the whole world")
COMPARE_FEATURE(RC_FORCEWAVES, "r_foliageForceWavesCompare", "r_foliageForceWaves", 1, qfalse, "Force push and pull on plants (r_foliageForceWaves)", "plants that ignore the Force", "the plants are simulated for the whole world")
COMPARE_FEATURE(RC_SABERCUT, "r_foliageSaberCutCompare", "r_foliageSaberCut", 1, qfalse, "cutting plants with a saber (r_foliageSaberCut)", "uncut plants", "cut plants stay cut for the whole world")
